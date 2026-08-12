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
)

@Serializable
data class BridgeDocument(
    val stats: Stats = Stats(),
    val kpi: KpiReport = KpiReport(),
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
) {
    val isComplete: Boolean get() = caster.isNotBlank() && mountpoint.isNotBlank() && port > 0
}
