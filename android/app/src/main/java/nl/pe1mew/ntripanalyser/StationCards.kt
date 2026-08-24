/**
 * @file StationCards.kt
 * @brief The cards the station hub is built from.
 *
 * Moved out of MainActivity.kt unchanged (GUI v2, P1.1): the shell owns
 * navigation, and what it arranges lives beside it rather than inside
 * it. Each of these becomes a registered panel in P1.2 -- a card, a
 * destination and a share section -- so this file is where that
 * contract will be applied first.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

package nl.pe1mew.ntripanalyser

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

@Composable
internal fun VerdictBadge(
    doc: BridgeDocument?,
    running: Boolean,
    outcome: MonitorService.Outcome,
    configured: Boolean,
    failure: String? = null,
) {
    val verdict = doc?.kpi?.overallEnum ?: RunVerdict.RUNNING
    // "READY" is a claim, and an app with no mountpoint is not ready for
    // anything.  Saying so invites the user to tap the settings card
    // rather than to wonder why the run button does nothing.
    val label = when {
        doc != null -> doc.kpi.overallName
        !configured -> stringResource(R.string.verdict_unconfigured)
        else -> stringResource(R.string.verdict_idle)
    }

    Surface(
        color = if (doc == null && !configured) Color(0xFF9E9E9E)
                else runColour(verdict),
        shape = RoundedCornerShape(12.dp),
        modifier = Modifier.fillMaxWidth(),
    ) {
        Column(Modifier.padding(20.dp), horizontalAlignment = Alignment.CenterHorizontally) {
            Text(
                label,
                color = Color.White,
                fontWeight = FontWeight.Bold,
                fontSize = 26.sp,
            )
            doc?.kpi?.let { k ->
                Spacer(Modifier.height(8.dp))
                // What went wrong, where the countdown would be. A run
                // that could not connect has no sustain window to count,
                // and "0 of 60 s sustained" is a true statement that
                // explains nothing (GUI v3, D2).
                if (failure != null) {
                    Text(
                        failure,
                        color = Color.White,
                        fontSize = 13.sp,
                        textAlign = TextAlign.Center,
                    )
                } else
                Text(
                    // Once the window is met the countdown has served its
                    // purpose; a watch showing "101 of 60 s" reads as a
                    // fault in the counter.
                    if (k.sustainedS >= k.sustainTargetS)
                        stringResource(R.string.sustained_held, dur(k.sustainedS))
                    else
                        stringResource(
                            R.string.sustained_of,
                            k.sustainedS.toInt(), k.sustainTargetS.toInt(),
                        ),
                    color = Color.White,
                    fontSize = 13.sp,
                )
                Spacer(Modifier.height(8.dp))
                if (running) {
                    LinearProgressIndicator(
                        progress = { k.sustainFraction },
                        modifier = Modifier.fillMaxWidth(),
                        color = Color.White,
                    )
                } else if (outcome == MonitorService.Outcome.RUNNING) {
                    Text(
                        stringResource(R.string.run_watching),
                        color = Color.White,
                        fontSize = 13.sp,
                    )
                } else {
                    // "Finished" and "stopped" are deliberately different
                    // words: a run cut short did not measure what a run
                    // that reached its verdict measured.
                    Text(
                        stringResource(
                            when (outcome) {
                                MonitorService.Outcome.STOPPED -> R.string.run_stopped
                                MonitorService.Outcome.LIMIT_REACHED -> R.string.run_limit
                                else -> R.string.run_finished
                            },
                            k.elapsedS.toInt(),
                        ),
                        color = Color.White,
                        fontSize = 13.sp,
                    )
                }
            }
        }
    }
}

/**
 * The configured target, shown on the main screen.
 *
 * The password is deliberately never displayed -- only whether one is
 * set. A screen someone may hold up to show a colleague, or photograph
 * for a report, should not carry a credential.
 */
@Composable
internal fun ConfigSummary(s: CasterSettings, onEdit: () -> Unit) {
    Card(
        Modifier
            .fillMaxWidth()
            .clickable { onEdit() }
    ) {
        Column(Modifier.padding(12.dp)) {
            if (!s.isComplete) {
                Text(
                    stringResource(R.string.config_none),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                return@Column
            }
            Text(
                "${s.caster}:${s.port}",
                style = MaterialTheme.typography.bodyMedium,
                fontFamily = FontFamily.Monospace,
            )
            Text(
                s.mountpoint,
                fontWeight = FontWeight.Bold,
                fontFamily = FontFamily.Monospace,
            )
            Text(
                if (s.user.isBlank()) stringResource(R.string.config_anon)
                else stringResource(
                    R.string.config_user,
                    s.user,
                    stringResource(
                        if (s.password.isBlank()) R.string.config_nopw
                        else R.string.config_pw
                    ),
                ),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            if (s.sendGga) {
                Text(
                    if (s.ggaLive && Features.HAS_LIVE_GGA)
                        stringResource(R.string.config_gga_live,
                                       s.latitude, s.longitude)
                    else
                        stringResource(R.string.config_gga,
                                       s.latitude, s.longitude),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}

@Composable
internal fun StreamChips(doc: BridgeDocument) {
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        AssistChip(
            onClick = {},
            label = { Text("${doc.stats.bytesPerS?.toInt() ?: 0} B/s") },
        )
        AssistChip(
            onClick = {},
            label = { Text("${doc.stats.satsTotal} SV") },
        )
        if (doc.stats.mountpoint.isNotBlank()) {
            AssistChip(onClick = {}, label = { Text(doc.stats.mountpoint) })
        }
    }
}

/**
 * The evidence behind one KPI's verdict.
 *
 * The verdict line says *what* was decided; these lines say *from what*.
 * They are read straight out of the snapshot the C engine judged, so the
 * user is looking at the same numbers the verdict came from rather than
 * a second, possibly disagreeing, calculation.
 */
private fun evidenceFor(index: Int, s: Stats, arp: ArpInfo? = null): List<Pair<String, String>> {
    fun f(v: Double?, unit: String, dp: Int = 1) =
        if (v == null) "not measured" else "%.${dp}f %s".format(v, unit).trim()

    return when (index) {
        1 -> listOf(
            "Connected" to if (s.connected) "yes" else "no",
            "Throughput" to f(s.bytesPerS, "B/s", 0),
            "Received" to "%.1f kB".format(s.bytesTotal / 1024.0),
            "Uptime" to f(s.uptimeS, "s", 0),
            "Reconnects" to "${s.reconnects}",
        )
        2 -> buildList {
            add("CRC-valid frames" to "${s.framesOk}")
            add("Message types seen" to "${s.types.size}")
            add("Latency" to f(s.latencyS, "s", 2))
            // The types themselves, not merely how many: knowing a base
            // sends 1077/1087/1097/1127 and no 1005 is the difference
            // between a diagnosis and a number.
            if (Features.IS_PRO && s.types.isNotEmpty()) {
                add("Types received" to
                    s.types.map { it.type }.sorted().joinToString(", "))
            }
        }
        3 -> buildList {
            add("ARP received" to if (s.arpValid) "yes" else "no")
            add("Latitude" to (s.arpLat?.let { "%.6f°".format(it) } ?: "—"))
            add("Longitude" to (s.arpLon?.let { "%.6f°".format(it) } ?: "—"))
            add("Height" to f(s.arpAlt, "m", 2))
            // Everything else the message states. Only in pro, and only
            // when a 1005/1006 has actually been decoded.
            if (Features.IS_PRO && arp != null) {
                add("Message" to "RTCM ${arp.msg}")
                add("Station ID" to "${arp.stationId}")
                add("ITRF year" to
                    if (arp.itrfYear > 0) "20%02d".format(arp.itrfYear) else "not stated")
                add("Advertises" to arp.serves)
                add("Station type" to
                    if (arp.reference) "reference station" else "receiver, own position")
                add("Oscillator" to if (arp.singleOsc) "single" else "not single")
                add("ECEF X" to "%.4f m".format(arp.x))
                add("ECEF Y" to "%.4f m".format(arp.y))
                add("ECEF Z" to "%.4f m".format(arp.z))
                arp.antennaHeight?.let {
                    add("Antenna height" to "%.4f m".format(it))
                }
            }
        }
        4 -> s.types.filter { it.type in 1071..1237 && (it.type % 10) in 4..7 }
                .sortedBy { it.type }
                .map { t ->
                    "MSM ${t.type}" to
                        (t.avgDt?.let { "%.1f s (%d epochs)".format(it, t.epochs) }
                            ?: "${t.epochs} epochs")
                }
                .ifEmpty { listOf("MSM messages" to "none seen") }
        5 -> s.gnss.filter { it.satsTracked > 0 }
                .map { it.label to "${it.satsTracked} SV" }
                .plus("Total" to "${s.satsTotal} SV")
        6 -> s.gnss.filter { (it.cnrMedian ?: 0.0) > 0.0 }
                .map { g ->
                    g.label to "median %.1f, min %.1f dB-Hz".format(
                        g.cnrMedian ?: 0.0, g.cnrMin ?: 0.0)
                }
                // Not "not carried": a legacy stream carries C/N0 that
                // is not read yet (src/core/sv_track.c).
                .ifEmpty { listOf("C/N0" to "read from MSM4, 5, 6 and 7") }
                .plus("All constellations" to f(s.cnrMeanAll, "dB-Hz mean"))
        7 -> listOf(
            "Frames checked" to "${s.framesOk + s.framesCrcError}",
            "CRC failures" to "${s.framesCrcError}",
            "Error rate" to (s.crcErrorRate?.let { "%.4f%%".format(it * 100) } ?: "—"),
        )
        8 -> buildList {
            if (!s.advertisedKnown) {
                add("Sourcetable" to "no entry for this mountpoint")
                return@buildList
            }
            add("Types advertised" to "${s.advertisedCount}")
            add("Not being sent" to "${s.typesMissing}")
            add("Off advertised rate" to "${s.typesOffrate}")
            add("Sent but not advertised" to "${s.typesExtra}")

            // The sourcetable's NavSys field is the actual
            // advertisement, and what the verdict is judged against.
            val advertised = (1..7).filter { (s.advertisedGnss shr it) and 1 == 1 }
                .joinToString("+") { Gnss.label(it) }
            if (advertised.isNotEmpty()) {
                add("Sourcetable advertises" to advertised)
            }
            val streaming = s.gnss.filter { it.satsTracked > 0 }
                .joinToString("+") { Gnss.label(it.gnssId) }
            if (streaming.isNotEmpty()) add("Streaming now" to streaming)

            // The systems the station claims against the ones it sends.
            //
            // Only three are comparable: 1005/1006 carries indicator
            // bits for GPS, GLONASS and Galileo and for nothing else, so
            // a station streaming BeiDou cannot advertise it there.
            // Listing those alongside as though they were undeclared
            // would show a mismatch the verdict rightly ignores.
            if (arp != null) {
                val comparable = setOf(Gnss.GPS, Gnss.GLONASS, Gnss.GALILEO)
                val streamed = s.gnss.filter { it.satsTracked > 0 }
                val shown = streamed.filter { it.gnssId in comparable }
                    .joinToString("+") { Gnss.label(it.gnssId) }
                val others = streamed.filterNot { it.gnssId in comparable }
                    .joinToString("+") { Gnss.label(it.gnssId) }

                add("1005/1006 states" to arp.serves)
                add("Streams, of those" to (shown.ifEmpty { "none" }))
                if (others.isNotEmpty()) {
                    add("Also streams" to "$others (1005/1006 cannot state these)")
                }
            }
        }
        else -> emptyList()
    }
}

/**
 * Where the reference position stands relative to the rover, and
 * whether it has moved.
 *
 * The distance takes the usability colours A3 judges by -- green under
 * 5 km, amber under 50, red beyond. The movement sentence is chosen by
 * what the service *is*, on the desktop's own evidence rule: a
 * reference position within 150 m of the position being sent is a
 * network answering you (`gui_events.c`, ClassifyStation), and a
 * resolved gate test outranks the guess. The same count reads as a
 * network doing its job or a base that should worry you -- never both.
 */
/** What both the card and the detail screen say about the ARP. */
internal data class HoSummary(
    val distM: Double,
    val bearingDeg: Double,
    val isNetwork: Boolean,
) {
    val compass: String get() = compassPoint(bearingDeg)
}

/** Null when there is no valid reference position to summarise. */
internal fun hoSummary(
    stats: Stats, vrs: VrsDoc?, roverLat: Double, roverLon: Double,
): HoSummary? {
    val alat = stats.arpLat ?: return null
    val alon = stats.arpLon ?: return null
    if (!stats.arpValid) return null
    val distM = geoDistanceM(roverLat, roverLon, alat, alon)
    return HoSummary(
        distM = distM,
        bearingDeg = geoBearingDeg(roverLat, roverLon, alat, alon),
        // The desktop's evidence rule, and the gate test outranks it.
        isNetwork = (vrs != null && vrs.gate == 2) || distM < 150.0,
    )
}

internal fun hoDistColour(distM: Double) = when {
    distM < 5_000.0 -> verdictColour(Verdict.PASS)
    distM < 50_000.0 -> verdictColour(Verdict.WARN)
    else -> verdictColour(Verdict.FAIL)
}

@Composable
internal fun HandoverCard(
    stats: Stats,
    vrs: VrsDoc?,
    roverLat: Double,
    roverLon: Double,
    roverIsFix: Boolean,
    moves: Int,
    worstJumpM: Double?,
) {
    val sum = hoSummary(stats, vrs, roverLat, roverLon) ?: return
    val distM = sum.distM
    val bearing = sum.compass
    val distColour = hoDistColour(distM)
    val isNetwork = sum.isNetwork

    // A plain card, deliberately not a FoldableCard: this row *leads*
    // somewhere, and a row cannot honestly carry both the fold mark and
    // the forward mark -- nor can an inner fold-toggle share a tap with
    // the drill-in the shell wraps around it. Three short lines do not
    // need folding; they need to be a door.
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp)) {
            Text(
                stringResource(R.string.ho_title),
                fontWeight = FontWeight.Bold,
            )
            Spacer(Modifier.height(6.dp))
            Text(
                stringResource(
                    if (roverIsFix) R.string.ho_distance_fix
                    else R.string.ho_distance_set,
                    distanceText(distM), bearing),
                style = MaterialTheme.typography.bodySmall,
                color = distColour,
                fontWeight = FontWeight.Medium,
            )
            Text(
                when {
                    moves == 0 -> stringResource(R.string.ho_stable)
                    isNetwork -> stringResource(R.string.ho_moves_net, moves)
                    else -> stringResource(R.string.ho_moves_fixed, moves)
                },
                style = MaterialTheme.typography.bodySmall,
                color =
                    if (moves > 0 && !isNetwork) MaterialTheme.colorScheme.error
                    else MaterialTheme.colorScheme.onSurfaceVariant,
            )
            if (moves > 0 && worstJumpM != null) {
                Text(
                    stringResource(R.string.ho_jump, distanceText(worstJumpM)),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}

/**
 * The network-RTK assertions, drawn in the eight checks' own dress.
 *
 * The rows are [KpiItem]s from the same engine family and read the same
 * way; what differs is the last line: the gate is a **classification**,
 * not a verdict, so it is worded as what the service *is* — a fixed
 * base answering NOT gated has passed the question, not failed it.
 */
@Composable
internal fun VrsCard(v: VrsDoc) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp)) {
            Text(
                stringResource(R.string.vrs_title),
                fontWeight = FontWeight.Bold,
            )
            Spacer(Modifier.height(6.dp))
            v.items.forEachIndexed { i, item ->
                Row(
                    Modifier.padding(vertical = 4.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Surface(
                        color = verdictColour(item.verdictEnum),
                        shape = RoundedCornerShape(6.dp),
                    ) {
                        Text(
                            item.verdictName,
                            color = Color.White,
                            fontSize = 11.sp,
                            fontWeight = FontWeight.Bold,
                            fontFamily = FontFamily.Monospace,
                            modifier = Modifier.padding(
                                horizontal = 8.dp, vertical = 4.dp),
                        )
                    }
                    Spacer(Modifier.width(12.dp))
                    Column(Modifier.weight(1f)) {
                        Text("A${i + 1}. ${item.label}",
                             fontWeight = FontWeight.Medium)
                        Text(
                            item.detail,
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
            }
            Spacer(Modifier.height(4.dp))
            Text(
                if (v.gateResolved)
                    stringResource(R.string.vrs_service, v.gateName)
                else
                    stringResource(R.string.vrs_running),
                style = MaterialTheme.typography.bodySmall,
                fontWeight =
                    if (v.gateResolved) FontWeight.Medium else FontWeight.Normal,
                color =
                    if (v.gateResolved) MaterialTheme.colorScheme.onSurface
                    else MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

@Composable
internal fun KpiRow(
    index: Int,
    item: KpiItem,
    stats: Stats,
    arp: ArpInfo? = null,
) {
    val id = "kpi-$index"
    val expanded = FoldState.isOpen(id)

    Card(
        Modifier
            .fillMaxWidth()
            .clickable { FoldState.toggle(id) }
    ) {
        Row(
            Modifier.padding(12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Surface(
                color = verdictColour(item.verdictEnum),
                shape = RoundedCornerShape(6.dp),
            ) {
                Text(
                    item.verdictName,
                    color = Color.White,
                    fontSize = 11.sp,
                    fontWeight = FontWeight.Bold,
                    fontFamily = FontFamily.Monospace,
                    modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp),
                )
            }
            Spacer(Modifier.width(12.dp))
            Column(Modifier.weight(1f)) {
                Text("$index. ${item.label}", fontWeight = FontWeight.Medium)
                Text(
                    item.detail,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            AffordanceMark(
                if (expanded) Affordance.COLLAPSE else Affordance.EXPAND,
                Modifier,
            )
        }

        if (expanded) {
            Column(
                Modifier.padding(start = 12.dp, end = 12.dp, bottom = 12.dp),
            ) {
                HorizontalDivider()
                Spacer(Modifier.height(8.dp))
                val rows = evidenceFor(index, stats, arp)
                if (rows.isEmpty()) {
                    Text(
                        stringResource(R.string.no_evidence),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                rows.forEach { (k, v) ->
                    // A long value -- a list of message types, say --
                    // squeezes the label to nothing when both share a
                    // row, so it takes a line of its own instead. The
                    // label is what makes the value mean something.
                    if (v.length > 20) {
                        Column(Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
                            Text(
                                k,
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                            Text(
                                v,
                                style = MaterialTheme.typography.bodySmall,
                                fontFamily = FontFamily.Monospace,
                            )
                        }
                    } else {
                        Row(Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
                            Text(
                                k,
                                Modifier.weight(1f),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                            Text(
                                v,
                                style = MaterialTheme.typography.bodySmall,
                                fontFamily = FontFamily.Monospace,
                            )
                        }
                    }
                }
            }
        }
    }
}

/**
 * What actually placed the satellites now on screen.
 *
 * One cache, several possible fillers, and no per-satellite provenance
 * in it -- so this names the source that did the work, most specific
 * first. The station's own stream is claimed only when it really
 * delivered ephemerides, and it outranks an imported file because it is
 * this run's own measurement rather than something read off a disk.
 *
 * A single function because two callers ask it: the sky view's header
 * and the badge above it. Answered twice, they would eventually
 * disagree, and the screen would contradict itself about its own data.
 */
internal fun skySource(
    usedOrbits: Int,
    doc: BridgeDocument?,
    haveLocation: Boolean,
): PositionSource = when {
    usedOrbits > 0 && MonitorService.usedEphStream -> PositionSource.EPHEMERIS
    usedOrbits > 0 && (doc?.eph?.fromObs ?: 0) > 0 -> PositionSource.OBS_STREAM
    usedOrbits > 0 -> PositionSource.RINEX
    haveLocation -> PositionSource.PHONE_GNSS
    else -> PositionSource.NONE
}

/** Compact duration: "45 s", "12 min", "3 h 07 m". */
internal fun dur(seconds: Double): String {
    val s = seconds.toInt()
    return when {
        s < 90 -> "$s s"
        s < 5400 -> "${s / 60} min"
        else -> "%d h %02d m".format(s / 3600, (s % 3600) / 60)
    }
}

/**
 * The long-run picture, shown only while watching.
 *
 * A spot check answers "does it pass?"; these lines answer "does it keep
 * passing?" -- which a 90-second check cannot see at all.
 */
/**
 * A card that folds.
 *
 * GUI v3, P2.2. The header is what the row is worth at a glance; the
 * body is what it is worth when asked. The mark comes from the same
 * renderer the hub uses for the rows that lead somewhere, so a screen of
 * eight of them reads as one grammar rather than two.
 *
 * @param runKey which run this is showing. When it changes the card
 *               folds shut: what was open belonged to the measurement
 *               underneath it, and that measurement has been replaced.
 *               Rotation does not change it, so an opened row survives
 *               turning the phone.
 */
@Composable
internal fun FoldableCard(
    id: String,
    header: @Composable ColumnScope.() -> Unit,
    body: @Composable ColumnScope.() -> Unit,
) {
    val open = FoldState.isOpen(id)
    Card(
        Modifier
            .fillMaxWidth()
            .clickable { FoldState.toggle(id) }
    ) {
        Row(Modifier.padding(12.dp)) {
            Column(Modifier.weight(1f)) {
                header()
                if (open) body()
            }
            AffordanceMark(
                if (open) Affordance.COLLAPSE else Affordance.EXPAND,
                Modifier,
            )
        }
    }
}

@Composable
internal fun WatchCard(w: Watch, reconnects: Int = 0) {
    FoldableCard(
        "watch",
        header = {
            Text(
                stringResource(R.string.watch_title),
                fontWeight = FontWeight.Bold,
            )
            Spacer(Modifier.height(6.dp))
            // The card's lines are the size a KPI row's evidence is:
            // this is the same kind of reading, and two kinds of type
            // for one kind of fact is what made the hub look assembled
            // rather than designed.
            Text(
                stringResource(R.string.watch_for, dur(w.elapsedS)),
                style = MaterialTheme.typography.bodySmall,
            )
        },
        body = {
            w.availability?.let {
                Text(
                    stringResource(R.string.watch_availability,
                                   "%.1f%%".format(it * 100)),
                    style = MaterialTheme.typography.bodySmall,
                )
            }
            Text(
                stringResource(R.string.watch_streak,
                               dur(w.streakS), dur(w.bestStreakS)),
                style = MaterialTheme.typography.bodySmall,
            )
            Text(
                if (w.degradations > 0)
                    stringResource(R.string.watch_drops, w.degradations, w.worstName)
                else
                    stringResource(R.string.watch_clean, w.worstName),
                style = MaterialTheme.typography.bodySmall,
                color = if (w.degradations > 0) MaterialTheme.colorScheme.error
                        else MaterialTheme.colorScheme.onSurfaceVariant,
            )
            // Only when it happened: a nought here would be one more
            // number to read past on every watch that went cleanly, and
            // the fact worth surfacing is the exception.
            //
            // Under the degradations line because it explains it. Three
            // degradations with two reconnects is a link that dropped;
            // three with none is a station that faltered, and those are
            // different diagnoses.
            if (reconnects > 0) {
                Text(
                    stringResource(R.string.watch_reconnects, reconnects),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        },
    )
}

/**
 * Where the orbits stand.
 *
 * A sky view drawn from partial or stale orbits looks exactly like one
 * drawn from fresh, complete ones, so the difference is stated here
 * rather than left for the user to infer from a sparse plot.
 */
@Composable
internal fun EphCard(
    eph: EphState,
    rinexName: String?,
    rinexAgeS: Double?,
    phonePlaced: Int,
) {
    FoldableCard(
        "orbits",
        header = {
            Text(
                stringResource(R.string.eph_title),
                fontWeight = FontWeight.Bold,
            )
            Spacer(Modifier.height(4.dp))

            // Coverage is worth stating once there are orbits to cover
            // with. With none at all -- a station that broadcasts none,
            // with no file imported and no stream attached -- "0 of 41"
            // in red announces a fault that is not one; what the sky view
            // is actually drawing from is said below instead.
            val hasOrbits = eph.placeable > 0 || eph.cached > 0
            if (hasOrbits) {
                Text(
                    stringResource(R.string.eph_coverage, eph.placeable, eph.tracked, eph.cached),
                    style = MaterialTheme.typography.bodySmall,
                    color = if (eph.isComplete) MaterialTheme.colorScheme.onSurfaceVariant
                            else MaterialTheme.colorScheme.error,
                )
            }

        },
        body = {
            // A fact about the station, not just about this app's plot:
            // an installer signing off a base wants to know it serves
            // orbits as well as observations, and a user comparing the
            // two editions wants to know why this one needed no stream.
            if (eph.fromObs > 0) {
                Text(
                    stringResource(R.string.eph_from_station, eph.fromObs),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }

            // This card counts orbits, and the sky view can also place a
            // satellite from the phone's own GNSS. Saying only the first
            // read as a contradiction: "0 of 41 can be placed" beside a
            // plot that was plainly placing them.
            if (phonePlaced > 0) {
                Text(
                    stringResource(
                        if (eph.placeable > 0) R.string.eph_from_phone
                        else R.string.eph_only_phone,
                        phonePlaced,
                    ),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }

            val age = eph.ageS
            Text(
                when {
                    // "cannot place anything" is only true when the phone
                    // is not placing them either; with no orbits but a
                    // populated plot the honest statement is which source
                    // drew it, not that nothing was drawn.
                    age == null && phonePlaced > 0 ->
                        stringResource(R.string.eph_none_phone)
                    age == null -> stringResource(R.string.eph_none)
                    age < 3600 -> stringResource(R.string.eph_age_min, (age / 60).toInt())
                    else -> stringResource(R.string.eph_age_hour, age / 3600.0)
                },
                style = MaterialTheme.typography.bodySmall,
                color = if (eph.isStale) MaterialTheme.colorScheme.error
                        else MaterialTheme.colorScheme.onSurfaceVariant,
            )

            Spacer(Modifier.height(4.dp))
            // The file's own age, from the dates in it, not from the
            // cache: a day-old GLONASS record wraps in the cache and
            // reads as hours old, which is how a file that could place
            // nothing described itself as fresh.
            val fileStale = rinexAgeS != null &&
                rinexAgeS > Settings.RINEX_FRESH_S
            Text(
                rinexName?.let { name ->
                    if (rinexAgeS != null)
                        stringResource(R.string.eph_file_age, name,
                                       ageShort(rinexAgeS))
                    else stringResource(R.string.eph_file, name)
                } ?: stringResource(R.string.eph_no_file),
                style = MaterialTheme.typography.bodySmall,
                color = if (fileStale) MaterialTheme.colorScheme.error
                        else MaterialTheme.colorScheme.onSurfaceVariant,
            )
        },
    )
}

