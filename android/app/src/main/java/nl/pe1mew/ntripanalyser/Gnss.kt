package nl.pe1mew.ntripanalyser

import android.annotation.SuppressLint
import android.content.Context
import android.location.GnssStatus
import android.location.LocationListener
import android.location.LocationManager
import androidx.compose.ui.graphics.Color
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Constellations, and where satellite positions come from.
 *
 * The numbering is the core's, from `get_gnss_id_from_rtcm()`:
 * 1 GPS, 2 GLONASS, 3 Galileo, 4 QZSS, 5 BeiDou, 6 SBAS, 7 NavIC.
 * Android's `GnssStatus` uses a different set, so the two are mapped
 * explicitly below rather than assumed to agree — they do not.
 */
object Gnss {

    const val GPS = 1
    const val GLONASS = 2
    const val GALILEO = 3
    const val QZSS = 4
    const val BEIDOU = 5
    const val SBAS = 6
    const val NAVIC = 7

    fun label(id: Int): String = when (id) {
        GPS -> "GPS"; GLONASS -> "GLONASS"; GALILEO -> "Galileo"
        QZSS -> "QZSS"; BEIDOU -> "BeiDou"; SBAS -> "SBAS"; NAVIC -> "NavIC"
        else -> "GNSS $id"
    }

    /** One letter, as RINEX and the desktop label satellites: G05, R21. */
    fun letter(id: Int): Char = when (id) {
        GPS -> 'G'; GLONASS -> 'R'; GALILEO -> 'E'; QZSS -> 'J'
        BEIDOU -> 'C'; SBAS -> 'S'; NAVIC -> 'I'
        else -> '?'
    }

    /** `G05`, `R21`, `E11` — the desktop Sky Plot's satellite labels. */
    fun name(id: Int, prn: Int): String = "%c%02d".format(letter(id), prn)

    /** Constellation colours, matching the desktop legend. */
    fun colour(id: Int): Color = when (id) {
        GPS -> Color(0xFF2E9E3E)
        GLONASS -> Color(0xFFD32F2F)
        GALILEO -> Color(0xFF1E56C8)
        QZSS -> Color(0xFF9C27B0)
        BEIDOU -> Color(0xFFEF8A17)
        SBAS -> Color(0xFF7A7A7A)
        NAVIC -> Color(0xFF00B8B8)
        else -> Color(0xFF9E9E9E)
    }

    /**
     * Translate an Android constellation to the core's numbering.
     *
     * @return the core id, or 0 for a constellation the core does not track.
     */
    fun fromAndroid(constellation: Int): Int = when (constellation) {
        GnssStatus.CONSTELLATION_GPS -> GPS
        GnssStatus.CONSTELLATION_GLONASS -> GLONASS
        GnssStatus.CONSTELLATION_GALILEO -> GALILEO
        GnssStatus.CONSTELLATION_QZSS -> QZSS
        GnssStatus.CONSTELLATION_BEIDOU -> BEIDOU
        GnssStatus.CONSTELLATION_SBAS -> SBAS
        GnssStatus.CONSTELLATION_IRNSS -> NAVIC
        else -> 0
    }

    /**
     * Translate an Android `svid` to the PRN the RTCM stream uses.
     *
     * The two numbering conventions agree for GPS, Galileo and BeiDou and
     * disagree for the rest, so each exception is converted explicitly:
     *
     * - **GLONASS** — Android may report the slot (1–24) or the slot
     *   offset by 64 (65–96), depending on the device.
     * - **QZSS** — Android numbers 193–200; RTCM counts from 1.
     * - **SBAS** — Android uses the real PRN (120–158); RTCM stores it
     *   relative to 119.
     *
     * Getting this wrong does not fail loudly: it silently plots a
     * satellite under another one's identity, or drops it for want of a
     * match. Hence one place, documented.
     */
    fun prnFromAndroid(coreGnss: Int, svid: Int): Int = when (coreGnss) {
        GLONASS -> if (svid > 64) svid - 64 else svid
        QZSS -> if (svid >= 193) svid - 192 else svid
        SBAS -> if (svid >= 120) svid - 119 else svid
        else -> svid
    }
}

/** Where a satellite is, and how the app came to know. */
data class SatPosition(
    val gnss: Int,
    val prn: Int,
    val azimuthDeg: Float,
    val elevationDeg: Float,
    /** The phone's own C/N0 for this satellite; 0 when it has no lock. */
    val phoneCn0: Float = 0f,
)

/** Which source supplied the positions, for the header to name. */
enum class PositionSource { NONE, PHONE_GNSS, EPHEMERIS, RINEX }

/**
 * Satellite positions from the phone's own GNSS receiver.
 *
 * Reports every satellite `GnssStatus` lists — including ones the phone
 * is not currently tracking, which it can still place from its almanac.
 * See `android/design/views.md` for why that is sound near the base and
 * what its limits are.
 *
 * Needs `ACCESS_FINE_LOCATION`, requested when a view that needs it is
 * first opened rather than at launch.
 */
class PhoneGnss(private val context: Context) {

    private val _positions = MutableStateFlow<Map<Long, SatPosition>>(emptyMap())

    /** Keyed by [key] so the sky view can join by (constellation, PRN). */
    val positions: StateFlow<Map<Long, SatPosition>> = _positions.asStateFlow()

    private val _fix = MutableStateFlow<Fix?>(null)

    /**
     * The phone's own position, when it has one.
     *
     * The GGA uplink in the paid edition is sent from here. Null until
     * the receiver has a fix -- indoors that can be for ever, which is
     * why the caller falls back to the configured position rather than
     * sending nothing or a zero.
     */
    val fix: StateFlow<Fix?> = _fix.asStateFlow()

    private var callback: GnssStatus.Callback? = null
    private var locationListener: LocationListener? = null

    @SuppressLint("MissingPermission")   // the caller checks; see MainActivity
    fun start() {
        if (callback != null) return
        val lm = context.getSystemService(LocationManager::class.java) ?: return

        val cb = object : GnssStatus.Callback() {
            override fun onSatelliteStatusChanged(status: GnssStatus) {
                val map = HashMap<Long, SatPosition>(status.satelliteCount)
                for (i in 0 until status.satelliteCount) {
                    val g = Gnss.fromAndroid(status.getConstellationType(i))
                    if (g == 0) continue
                    // A satellite with no azimuth or elevation cannot be
                    // placed; the engine reports 0/0 when it has neither
                    // almanac nor ephemeris for it.
                    val el = status.getElevationDegrees(i)
                    val az = status.getAzimuthDegrees(i)
                    if (el <= 0f && az == 0f) continue

                    val prn = Gnss.prnFromAndroid(g, status.getSvid(i))
                    map[key(g, prn)] = SatPosition(
                        gnss = g, prn = prn,
                        azimuthDeg = az, elevationDeg = el,
                        phoneCn0 = status.getCn0DbHz(i),
                    )
                }
                _positions.value = map
            }
        }
        runCatching { lm.registerGnssStatusCallback(cb, null) }
            .onSuccess { callback = cb }

        // Registering for status is not enough: the GNSS receiver only
        // runs while something is actively requesting location, and a
        // powered-down receiver reports no satellites at all.  This
        // request is what turns the engine on.
        //
        // The fixes were discarded until the paid edition needed a live
        // GGA position; they are kept now, and go nowhere unless that
        // edition is running and the user has consented to transmit.
        val listener = LocationListener { loc ->
            _fix.value = Fix(loc.latitude, loc.longitude, loc.accuracy)
        }
        runCatching {
            lm.requestLocationUpdates(
                LocationManager.GPS_PROVIDER, 1000L, 0f, listener,
            )
        }.onSuccess { locationListener = listener }
    }

    fun stop() {
        _fix.value = null
        val lm = context.getSystemService(LocationManager::class.java)
        callback?.let { lm?.unregisterGnssStatusCallback(it) }
        locationListener?.let { lm?.removeUpdates(it) }
        callback = null
        locationListener = null
    }

    companion object {
        /** Join key shared with the C side's (gnss_id, prn). */
        fun key(gnss: Int, prn: Int): Long = gnss.toLong() * 1000L + prn
    }
}

/** A position from the phone's own receiver, in degrees and metres. */
data class Fix(val lat: Double, val lon: Double, val accuracyM: Float)
