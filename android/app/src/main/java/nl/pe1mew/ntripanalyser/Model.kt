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

/** One constellation's contribution. */
@Serializable
data class GnssStat(
    @SerialName("gnss_id") val gnssId: Int = 0,
    @SerialName("sats_tracked") val satsTracked: Int = 0,
    @SerialName("cnr_mean") val cnrMean: Double? = null,
    @SerialName("cnr_median") val cnrMedian: Double? = null,
    @SerialName("cnr_min") val cnrMin: Double? = null,
) {
    /** RTCM constellation numbering, as used throughout the core. */
    val label: String get() = when (gnssId) {
        0 -> "GPS"; 1 -> "GLONASS"; 2 -> "Galileo"; 3 -> "BeiDou"
        4 -> "QZSS"; 5 -> "NavIC"; 6 -> "SBAS"
        else -> "GNSS $gnssId"
    }
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

@Serializable
data class BridgeDocument(
    val stats: Stats = Stats(),
    val kpi: KpiReport = KpiReport(),
    val watch: Watch? = null,
)

/** Tolerant by policy: an added C field must never crash the phone. */
val bridgeJson = Json {
    ignoreUnknownKeys = true
    isLenient = true
}

/** Connection settings, persisted between runs. */
data class CasterSettings(
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
}
