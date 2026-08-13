package nl.pe1mew.ntripanalyser

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json

/**
 * The data model mirroring the bridge's JSON document.
 *
 * Only the fields Normal mode shows are declared; `ignoreUnknownKeys`
 * carries the rest of the snapshot harmlessly, so the C side can add
 * fields without breaking the app -- exactly how the daemon's snapshot
 * has grown so far.
 */

/** Per-KPI verdict; ordinals match `KpiVerdict` in `core/kpi.h`. */
enum class Verdict { PENDING, PASS, WARN, FAIL }

/** Overall verdict; ordinals match `KpiRunVerdict` in `core/kpi.h`. */
enum class RunVerdict { RUNNING, OK, CAUTION, FAILED }

@Serializable
data class KpiItem(
    val verdict: Int = 0,
    @SerialName("verdict_name") val verdictName: String = "",
    val value: Double = 0.0,
    val label: String = "",
    val detail: String = "",
) {
    val verdictEnum: Verdict
        get() = Verdict.entries.getOrElse(verdict) { Verdict.PENDING }
}

@Serializable
data class KpiReport(
    val overall: Int = 0,
    @SerialName("overall_name") val overallName: String = "RUNNING",
    @SerialName("elapsed_s") val elapsedS: Double = 0.0,
    @SerialName("sustained_s") val sustainedS: Double = 0.0,
    @SerialName("sustain_target_s") val sustainTargetS: Double = 60.0,
    /** The verdict has held its window, or failed outright. */
    val settled: Boolean = false,
    val items: List<KpiItem> = emptyList(),
) {
    val overallEnum: RunVerdict
        get() = RunVerdict.entries.getOrElse(overall) { RunVerdict.RUNNING }

    /** 0..1 across the sustain window, for the progress indicator. */
    val sustainFraction: Float
        get() = if (sustainTargetS <= 0.0) 0f
        else (sustainedS / sustainTargetS).coerceIn(0.0, 1.0).toFloat()
}

/**
 * The subset of the statistics snapshot Normal mode displays.
 *
 * **Every double here is nullable, and must stay that way.**
 * `ns_stats_to_json()` serialises any unmeasured double as JSON `null`
 * rather than 0, deliberately, so "not measured" is distinguishable from
 * "measured as zero" — a station with no ARP yet is not a station at
 * 0°N 0°E. Declaring these non-nullable made the very first document
 * fail to decode, and because a decode failure publishes nothing, the
 * screen sat at READY with no error while the run was working fine.
 */
@Serializable
data class Stats(
    @SerialName("schema_version") val schemaVersion: Int = 0,
    val mountpoint: String = "",
    val connected: Boolean = false,
    @SerialName("bytes_per_s") val bytesPerS: Double? = null,
    @SerialName("frames_ok") val framesOk: Long = 0,
    @SerialName("sats_total") val satsTotal: Int = 0,
    @SerialName("arp_valid") val arpValid: Boolean = false,
    @SerialName("arp_lat") val arpLat: Double? = null,
    @SerialName("arp_lon") val arpLon: Double? = null,
    @SerialName("arp_alt") val arpAlt: Double? = null,
    @SerialName("uptime_s") val uptimeS: Double? = null,
    @SerialName("bytes_total") val bytesTotal: Long = 0,
    @SerialName("frames_crc_error") val framesCrcError: Long = 0,
    @SerialName("crc_error_rate") val crcErrorRate: Double? = null,
    @SerialName("latency_s") val latencyS: Double? = null,
    val reconnects: Int = 0,
    @SerialName("cnr_mean_all") val cnrMeanAll: Double? = null,
    /* The advertised-versus-observed roll-up, behind KPI 8. */
    @SerialName("advertised_known") val advertisedKnown: Boolean = false,
    @SerialName("advertised_count") val advertisedCount: Int = 0,
    @SerialName("types_missing") val typesMissing: Int = 0,
    @SerialName("types_offrate") val typesOffrate: Int = 0,
    @SerialName("types_extra") val typesExtra: Int = 0,
    /** Constellations the sourcetable advertises, as a bitmask by id. */
    @SerialName("advertised_gnss") val advertisedGnss: Int = 0,
    val types: List<TypeStat> = emptyList(),
    val gnss: List<GnssStat> = emptyList(),
)

/** One `STR` record from a caster's sourcetable. */
@Serializable
data class SourceEntry(
    val mountpoint: String = "",
    val identifier: String = "",
    val format: String = "",
    @SerialName("nav_systems") val navSystems: String = "",
    val country: String = "",
    val lat: Double = 0.0,
    val lon: Double = 0.0,
    val carrier: Int = 0,
    /** The caster expects a GGA uplink: a network-RTK mountpoint. */
    val nmea: Boolean = false,
)

@Serializable
data class Sourcetable(val entries: List<SourceEntry> = emptyList())

/** One RTCM message type as the stream actually delivers it. */
@Serializable
data class TypeStat(
    val type: Int = 0,
    val frames: Long = 0,
    val epochs: Long = 0,
    @SerialName("min_dt") val minDt: Double? = null,
    @SerialName("max_dt") val maxDt: Double? = null,
    @SerialName("avg_dt") val avgDt: Double? = null,
)

/**
 * One satellite as the stream carries it.
 *
 * No position: an observation stream never carries one. The views join
 * azimuth and elevation from whatever source is available, by
 * (gnss, prn) -- see `android/design/views.md`.
 */
@Serializable
data class SatEntry(
    val gnss: Int = 0,
    val prn: Int = 0,
    /** Most recent C/N0; 0 when the stream carries none (MSM4/5/6). */
    val cn0: Float = 0f,
    /** Session mean, averaged in power rather than in decibels. */
    @SerialName("cn0_mean") val cn0Mean: Float = 0f,
    val samples: Int = 0,
    /**
     * Azimuth and elevation from the station's cached orbit, or null
     * when no orbit is available for this satellite.
     *
     * Null rather than zero: a satellite at 0,0 would be drawn on the
     * horizon due north, and the plot cannot tell that from a fact.
     */
    val az: Float? = null,
    val el: Float? = null,
)

/** One constellation's contribution. */
@Serializable
data class GnssStat(
    @SerialName("gnss_id") val gnssId: Int = 0,
    @SerialName("sats_tracked") val satsTracked: Int = 0,
    @SerialName("cnr_mean") val cnrMean: Double? = null,
    @SerialName("cnr_median") val cnrMedian: Double? = null,
    @SerialName("cnr_min") val cnrMin: Double? = null,
) {
    /** One mapping for the whole app; see [Gnss]. */
    val label: String get() = Gnss.label(gnssId)
}

/**
 * Long-run health, present only in watch mode.
 *
 * `availability` and `lastDegradeS` are nullable for the same reason the
 * snapshot's doubles are: nothing measured yet must not read as zero.
 */
@Serializable
data class Watch(
    @SerialName("elapsed_s") val elapsedS: Double = 0.0,
    @SerialName("ok_s") val okS: Double = 0.0,
    @SerialName("caution_s") val cautionS: Double = 0.0,
    @SerialName("failed_s") val failedS: Double = 0.0,
    @SerialName("streak_s") val streakS: Double = 0.0,
    @SerialName("best_streak_s") val bestStreakS: Double = 0.0,
    val degradations: Int = 0,
    val worst: Int = 0,
    @SerialName("worst_name") val worstName: String = "",
    val availability: Double? = null,
    @SerialName("last_degrade_s") val lastDegradeS: Double? = null,
)

/**
 * What the station says about itself in its 1005/1006.
 *
 * More than a position: the station ID it will appear under, the
 * realisation its coordinates belong to, and which systems it claims to
 * serve — which is not always what it streams, and that discrepancy is
 * worth seeing.
 */
@Serializable
data class ArpInfo(
    /** 1005 or 1006; only 1006 carries an antenna height. */
    val msg: Int = 0,
    @SerialName("station_id") val stationId: Int = 0,
    /** ITRF realisation year; 0 when the station does not state one. */
    @SerialName("itrf_year") val itrfYear: Int = 0,
    val gps: Boolean = false,
    val glonass: Boolean = false,
    val galileo: Boolean = false,
    /** A real reference station, rather than a receiver reporting itself. */
    val reference: Boolean = false,
    @SerialName("single_osc") val singleOsc: Boolean = false,
    val x: Double = 0.0,
    val y: Double = 0.0,
    val z: Double = 0.0,
    @SerialName("antenna_height") val antennaHeight: Double? = null,
) {
    /** What it advertises, as the sourcetable would phrase it. */
    val serves: String
        get() = listOfNotNull(
            "GPS".takeIf { gps },
            "GLONASS".takeIf { glonass },
            "Galileo".takeIf { galileo },
        ).joinToString("+").ifEmpty { "none stated" }
}

/**
 * How well the orbit cache serves this stream, and how old it is.
 *
 * Shown in the app rather than left implicit: a sky view drawn from
 * stale or partial orbits looks exactly like one drawn from fresh,
 * complete ones.
 */
@Serializable
data class EphState(
    /** Satellites the stream is carrying. */
    val tracked: Int = 0,
    /** Of those, how many have an orbit and can be drawn. */
    val placeable: Int = 0,
    /** Satellites in the cache, tracked or not. */
    val cached: Int = 0,
    /** Seconds since the newest orbit was issued; null when empty. */
    @SerialName("age_s") val ageS: Double? = null,
) {
    val isComplete: Boolean get() = tracked > 0 && placeable >= tracked

    /**
     * A sky plot tolerates far older orbits than positioning does -- a
     * kilometre of orbit error at 20 000 km is about 0.01 degrees -- so
     * this is generous next to the two-to-four-hour fit interval.
     */
    val isStale: Boolean get() = (ageS ?: 0.0) > 6 * 3600
}

@Serializable
data class BridgeDocument(
    val stats: Stats = Stats(),
    val kpi: KpiReport = KpiReport(),
    val watch: Watch? = null,
    val sats: List<SatEntry> = emptyList(),
    val eph: EphState = EphState(),
    val arp: ArpInfo? = null,
)

/** Tolerant by policy: an added C field must never crash the phone. */
val bridgeJson = Json {
    ignoreUnknownKeys = true
    isLenient = true
}

/**
 * Connection settings, persisted between runs.
 *
 * The paid edition saves several of these; see [ProfileStore]. A saved
 * entry is a whole connection rather than a mountpoint name, because the
 * expensive part of setting one up is the caster, the port and the
 * credentials.
 */
@Serializable
data class CasterSettings(
    /** What the user calls this connection; the mountpoint when unnamed. */
    val name: String = "",
    val caster: String = "",
    val port: Int = 2101,
    val mountpoint: String = "",
    val user: String = "",
    val password: String = "",
    val latitude: Double = 52.0,
    val longitude: Double = 6.0,
    val sendGga: Boolean = false,
    /**
     * Ephemeris stream, which the sky plot needs: observations say which
     * satellites are tracked, not where they are. Blank disables it.
     */
    val ephCaster: String = "",
    val ephPort: Int = 2101,
    val ephMountpoint: String = "",
) {
    val isComplete: Boolean get() =
        caster.isNotBlank() && mountpoint.isNotBlank() && port > 0

    /** The sky plot needs an ephemeris source; without one it is hidden. */
    val hasEph: Boolean get() =
        ephCaster.isNotBlank() && ephMountpoint.isNotBlank()

    /** What to show in a list: the user's name, or the mountpoint. */
    val label: String get() = when {
        name.isNotBlank()       -> name
        mountpoint.isNotBlank() -> mountpoint
        else                    -> "New connection"
    }
}

/**
 * Every saved connection, and which one is in use.
 *
 * One profile is the free edition's whole world; the paid edition keeps
 * up to [Features.MAX_MOUNTPOINTS]. The active index is stored rather
 * than a copy of the active profile, so there is exactly one place a
 * connection's details live and no way for the two to disagree.
 */
@Serializable
data class ProfileStore(
    val profiles: List<CasterSettings> = listOf(CasterSettings()),
    val active: Int = 0,
) {
    /** The profile in use, and never out of range. */
    val current: CasterSettings
        get() = profiles.getOrNull(active) ?: profiles.firstOrNull()
            ?: CasterSettings()

    val activeIndex: Int
        get() = if (active in profiles.indices) active else 0
}
