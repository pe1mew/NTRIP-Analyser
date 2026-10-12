/**
 * @file SelfPosition.kt
 * @brief Self-position on the phone (P7, `design/work-items/station-self-position.md`).
 *
 * Where the station's **own** observations place it, against the
 * position it broadcasts in 1005/1006. Nothing is measured here: the
 * solve runs in the shared core every epoch, in both editions, and the
 * bridge publishes the latest epoch in the snapshot and the window's
 * figures in the tier-2 report -- including the core's own word for
 * what can be said yet (`selfpos_state`, report schema 4). This file
 * draws them, and keeps the run's points for the plot.
 *
 * The rules are the desktop's (`gui/gui_selfpos_window.c`,
 * `design/gui-design.md` §16), kept so a screenshot of one means what a
 * screenshot of the other means: the broadcast position at the centre
 * and staying there, north up, a ring scale of 1, 2 or 5 times a power
 * of ten with no ceiling, rings labelled only once points set the scale,
 * the run's points small, the newest larger, the mean as a cross, and
 * **no verdict** -- no threshold for a station's own scatter has been
 * established from evidence.
 *
 * Pro's registry only ([Features.HAS_SELFPOS]); free shows less, never
 * different.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
package nl.pe1mew.ntripanalyser

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import java.util.Locale
import kotlin.math.floor
import kotlin.math.log10
import kotlin.math.pow
import kotlin.math.sqrt

/**
 * The run's solved epochs, east/north/up against the broadcast position.
 *
 * A point per solved epoch, keyed by the stream's own clock so a
 * document republished for the same epoch is not plotted twice -- the
 * desktop's rule. Capped as the desktop caps it: 7200 points, two hours
 * at one epoch a second, the newest kept. Run-scoped and owned by
 * `MonitorService`'s companion, because a record of a run outlives the
 * screen that draws it.
 */
class SelfPosTrail {

    data class Point(val t: Double, val e: Float, val n: Float, val u: Float)

    private val ring = arrayOfNulls<Point>(CAP)
    private var head = 0
    private var count = 0
    private var lastT = Double.NEGATIVE_INFINITY

    /** Bumped on any recording, because Compose cannot see inside. */
    var revision: Int = 0
        private set

    /** One solved epoch; ignored if its stream time is not newer. */
    @Synchronized
    fun offer(t: Double, e: Double, n: Double, u: Double) {
        if (!(t > lastT)) return
        lastT = t
        ring[head] = Point(t, e.toFloat(), n.toFloat(), u.toFloat())
        head = (head + 1) % CAP
        if (count < CAP) count++
        revision++
    }

    /** Oldest first. */
    @Synchronized
    fun points(): List<Point> {
        val out = ArrayList<Point>(count)
        val start = if (count < CAP) 0 else head
        for (i in 0 until count) ring[(start + i) % CAP]?.let { out += it }
        return out
    }

    @Synchronized
    fun clear() {
        ring.fill(null)
        head = 0
        count = 0
        lastT = Double.NEGATIVE_INFINITY
        revision++
    }

    companion object {
        /** The desktop's `SELFPOS_CAP`. */
        const val CAP = 7200
    }
}

/** The core's `SrSelfPosState`, as the report's frozen key names it. */
private enum class SpState { GATHERING, SOLVED, NOT_COMPUTABLE, NOT_SOLVED }

private fun spState(sr: SrDoc): SpState = when (sr.selfposState) {
    "solved"         -> SpState.SOLVED
    "not_computable" -> SpState.NOT_COMPUTABLE
    "not_solved"     -> SpState.NOT_SOLVED
    else             -> SpState.GATHERING
}

/**
 * Why nothing has solved, in the app's words for the core's `SppStatus`.
 * The numbers are the C enum's (`src/core/spp.h`), checked against it by
 * `tools/check_release.py`, as the failure codes are.
 */
@Composable
private fun spWhy(state: SpState, status: Int): String = stringResource(
    when (state) {
        SpState.SOLVED -> R.string.sp_no_verdict
        SpState.NOT_COMPUTABLE -> R.string.sp_why_single_freq
        else -> when (status) {
            SPP_NO_EPHEMERIS -> R.string.sp_why_no_eph
            SPP_TOO_FEW_SATS -> R.string.sp_why_too_few
            SPP_DIVERGED     -> R.string.sp_why_diverged
            else             -> R.string.sp_why_first_epoch
        }
    })

/* SppStatus, src/core/spp.h. */
internal const val SPP_OK = 0
internal const val SPP_NO_EPOCH = 1
internal const val SPP_SINGLE_FREQ = 2
internal const val SPP_NO_EPHEMERIS = 3
internal const val SPP_TOO_FEW_SATS = 4
internal const val SPP_DIVERGED = 5

/** `%+.3f m`, as the desktop prints an offset. */
private fun signedM(v: Double) = String.format(Locale.US, "%+.3f m", v)

/**
 * The hub's self-position panel: a card, and a screen with the plot.
 */
object SelfPosPanel : Panel {
    override val key = "selfpos"

    override fun destination(): Dest = Dest.Detail(key)

    /** The mark only over a card that is actually drawn. */
    override fun affordance(state: HubState): Affordance =
        if (state.doc?.sr != null) Affordance.FORWARD else Affordance.NONE

    @Composable
    override fun Content(state: HubState, actions: HubActions) {
        val sr = state.doc?.sr ?: return
        SelfPosCard(sr)
    }

    @Composable
    override fun detailTitle(): String = stringResource(R.string.sp_title)

    @Composable
    override fun Detail(state: HubState, actions: HubActions) {
        val doc = state.doc ?: return
        val sr = doc.sr ?: return
        SelfPosDetail(sr, doc.stats, state.settings.mountpoint)
    }

    override fun shareSection(state: HubState): ShareSection? {
        val sr = state.doc?.sr ?: return null
        val lines = when (spState(sr)) {
            SpState.SOLVED -> listOfNotNull(
                "%d epoch(s) solved, no verdict".format(sr.selfposSamples),
                sr.selfposMeanE?.let { e ->
                    "mean offset E %s N %s U %s from the broadcast position".format(
                        signedM(e), signedM(sr.selfposMeanN ?: 0.0),
                        signedM(sr.selfposMeanU ?: 0.0))
                },
                sr.selfposScatter?.let { "scatter about the mean %.3f m".format(Locale.US, it) },
                sr.selfposSpeedMedian?.let {
                    "median apparent motion %.1f mm/s".format(Locale.US, it)
                },
            )
            SpState.NOT_COMPUTABLE -> listOf("not computable: single-frequency station")
            SpState.NOT_SOLVED -> listOf("not solved over the window")
            SpState.GATHERING -> listOf("gathering")
        }
        return ShareSection("Self-position", lines)
    }
}

/** The card: what can be said, and the window's figures when solved. */
@Composable
private fun SelfPosCard(sr: SrDoc) {
    val faint = MaterialTheme.colorScheme.onSurfaceVariant
    val st = spState(sr)
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp)) {
            Text(stringResource(R.string.sp_title), fontWeight = FontWeight.Bold)
            Spacer(Modifier.height(6.dp))
            Text(
                stringResource(
                    when (st) {
                        SpState.SOLVED -> R.string.sp_state_solved
                        SpState.NOT_COMPUTABLE -> R.string.sp_state_not_computable
                        SpState.NOT_SOLVED -> R.string.sp_state_not_solved
                        SpState.GATHERING -> R.string.sp_state_gathering
                    }),
                fontWeight = FontWeight.Medium,
                fontFamily = FontFamily.Monospace,
            )
            if (st == SpState.SOLVED) {
                val e = sr.selfposMeanE
                val n = sr.selfposMeanN
                val u = sr.selfposMeanU
                if (e != null && n != null && u != null)
                    Text(stringResource(R.string.sp_offset_line,
                                        signedM(e), signedM(n), signedM(u)),
                         style = MaterialTheme.typography.bodySmall)
                sr.selfposScatter?.let {
                    Text(stringResource(R.string.sp_scatter_line,
                                        String.format(Locale.US, "%.3f m", it)),
                         style = MaterialTheme.typography.bodySmall)
                }
            }
            Text(
                spWhy(st, sr.selfposStatus),
                style = MaterialTheme.typography.bodySmall,
                color = faint,
            )
        }
    }
}

/* The desktop's colours (gui_selfpos_window.c), kept so screenshots agree. */
private val SP_DOT      = Color(0xFF466EC8)   // the run
private val SP_DOT_LAST = Color(0xFFDC8214)   // the epoch just solved
private val SP_MEAN     = Color(0xFFC82828)   // where the run centres
private val SP_ARP      = Color(0xFF5A5A6E)   // the broadcast reference

/**
 * The detail: the analysis screens' template, with the figures above the
 * plot and the latest epoch below it.
 */
@Composable
private fun SelfPosDetail(sr: SrDoc, stats: Stats, mountpoint: String) {
    val trail = MonitorService.selfPos
    // Read so a point recorded between documents still redraws us; the
    // 1 Hz document is the usual trigger.
    @Suppress("UNUSED_EXPRESSION") trail.revision
    val faint = MaterialTheme.colorScheme.onSurfaceVariant
    val st = spState(sr)

    AnalysisBands(
        explainer = stringResource(R.string.sp_explainer),
        summary = {
            val pad = Modifier.padding(horizontal = 16.dp, vertical = 2.dp)
            Text(
                stringResource(
                    when (st) {
                        SpState.SOLVED -> R.string.sp_state_solved
                        SpState.NOT_COMPUTABLE -> R.string.sp_state_not_computable
                        SpState.NOT_SOLVED -> R.string.sp_state_not_solved
                        SpState.GATHERING -> R.string.sp_state_gathering
                    }),
                fontWeight = FontWeight.Bold,
                fontFamily = FontFamily.Monospace,
                modifier = pad,
            )
            Text(spWhy(st, sr.selfposStatus),
                 style = MaterialTheme.typography.bodySmall, color = faint,
                 modifier = pad)
            if (st == SpState.SOLVED) {
                val e = sr.selfposMeanE
                val n = sr.selfposMeanN
                val u = sr.selfposMeanU
                Text(stringResource(R.string.sp_epochs_line, sr.selfposSamples, sr.samples),
                     style = MaterialTheme.typography.bodySmall, modifier = pad)
                if (e != null && n != null && u != null)
                    Text(stringResource(R.string.sp_offset_line,
                                        signedM(e), signedM(n), signedM(u)),
                         style = MaterialTheme.typography.bodySmall, modifier = pad)
                Text(stringResource(R.string.sp_offset_note),
                     style = MaterialTheme.typography.bodySmall, color = faint,
                     modifier = pad)
                sr.selfposScatter?.let {
                    Text(stringResource(R.string.sp_scatter_line,
                                        String.format(Locale.US, "%.3f m", it)),
                         style = MaterialTheme.typography.bodySmall, modifier = pad)
                }
                sr.selfposRmsWorst?.let {
                    Text(stringResource(R.string.sp_rms_line,
                                        String.format(Locale.US, "%.3f m", it)),
                         style = MaterialTheme.typography.bodySmall, modifier = pad)
                }
                Text(
                    sr.selfposSpeedMedian?.let {
                        stringResource(R.string.sp_motion_line,
                                       String.format(Locale.US, "%.1f mm/s", it))
                    } ?: stringResource(R.string.sp_motion_none),
                    style = MaterialTheme.typography.bodySmall, modifier = pad,
                )
            }
        },
        plot = { m -> SelfPosPlot(trail.points(), Modifier.then(m).padding(12.dp)) },
        footer = {
            // The latest epoch: a diagnostic, labelled as one -- one
            // epoch is never the measurement.
            val pad = Modifier.padding(horizontal = 16.dp, vertical = 2.dp)
            if (stats.selfposStatus == SPP_OK && stats.selfposSats > 0) {
                Text(
                    stringResource(
                        R.string.sp_latest_line, stats.selfposSats,
                        String.format(Locale.US, "%.1f", stats.selfposPdop ?: 0.0),
                        String.format(Locale.US, "%.3f m", stats.selfposCodeRms ?: 0.0)),
                    style = MaterialTheme.typography.bodySmall,
                    fontFamily = FontFamily.Monospace,
                    color = faint, modifier = pad,
                )
                // Shown only when it is news: a station whose receiver
                // encodes its rates backwards, which a rover trusting
                // them inherits (RFSEE01, 2026-10-10).
                if (stats.selfposRateSign < 0)
                    Text(stringResource(R.string.sp_reversed),
                         style = MaterialTheme.typography.bodySmall,
                         color = MaterialTheme.colorScheme.error, modifier = pad)
            }
            Text(stringResource(R.string.sp_footer, mountpoint),
                 style = MaterialTheme.typography.bodySmall,
                 fontFamily = FontFamily.Monospace, color = faint,
                 modifier = Modifier.padding(horizontal = 16.dp, vertical = 6.dp))
        },
        legend = {
            Row(
                Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
                horizontalArrangement = Arrangement.spacedBy(14.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                SpLegend(SP_DOT, stringResource(R.string.sp_legend_epochs))
                SpLegend(SP_DOT_LAST, stringResource(R.string.sp_legend_latest))
                SpLegend(SP_MEAN, stringResource(R.string.sp_legend_mean))
                SpLegend(SP_ARP, stringResource(R.string.sp_legend_arp))
            }
        },
    )
}

@Composable
private fun SpLegend(colour: Color, label: String) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Canvas(Modifier.size(10.dp)) { drawCircle(colour) }
        Spacer(Modifier.width(5.dp))
        Text(label, style = MaterialTheme.typography.bodySmall,
             color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

/**
 * The outer ring's radius for a cloud whose widest point is [widest]
 * metres from the centre: 1, 2 or 5 times a power of ten, computed and
 * not tabulated, so it has no ceiling -- a table on the desktop stopped
 * at 500 m and drew a station 7.8 km out as an empty frame. No finer
 * than a quarter metre. The desktop's `PlotRadius`, line for line.
 */
internal fun spPlotRadius(widest: Double): Double {
    val want = widest * 1.15
    if (!(want > 0.0)) return 0.25
    val decade = 10.0.pow(floor(log10(want)))
    val norm = want / decade
    val mant = when {
        norm <= 1.0 -> 1.0
        norm <= 2.0 -> 2.0
        norm <= 5.0 -> 5.0
        else -> 10.0
    }
    val r = mant * decade
    return if (r < 0.25) 0.25 else r
}

/** `%.3g m`, or km from a kilometre up: the desktop's `FormatDistance`. */
private fun spDistance(m: Double): String =
    if (m >= 1000.0) String.format(Locale.US, "%.3g km", m / 1000.0)
    else String.format(Locale.US, "%.3g m", m)

/**
 * East/north about the broadcast position: the reference at the centre,
 * and staying there, because self-centring would hide the offset --
 * one of the two things this screen exists to show.
 */
@Composable
private fun SelfPosPlot(points: List<SelfPosTrail.Point>, modifier: Modifier) {
    val faint = MaterialTheme.colorScheme.onSurfaceVariant
    val labelStyle = MaterialTheme.typography.bodySmall.copy(color = faint)
    val arpStyle = MaterialTheme.typography.bodySmall.copy(color = SP_ARP)
    val measurer = rememberTextMeasurer()
    val noneText = stringResource(R.string.sp_plot_empty)

    Canvas(modifier) {
        val cx = size.width / 2f
        val cy = size.height / 2f
        val half = minOf(cx, cy) - 18.dp.toPx()
        if (half < 20f) return@Canvas

        var widest = 0.0
        for (p in points) {
            val d = sqrt((p.e * p.e + p.n * p.n).toDouble())
            if (d > widest) widest = d
        }
        val radius = spPlotRadius(widest)
        val scale = (half / radius).toFloat()

        // Rings at the full radius and half of it, and the cross through
        // the reference.
        val grid = faint.copy(alpha = 0.3f)
        drawCircle(grid, half, Offset(cx, cy), style = Stroke(1.5f))
        drawCircle(grid, half / 2f, Offset(cx, cy), style = Stroke(1.5f))
        drawLine(grid, Offset(cx - half, cy), Offset(cx + half, cy), 1f)
        drawLine(grid, Offset(cx, cy - half), Offset(cx, cy + half), 1f)
        drawText(measurer, "N", Offset(cx + 6f, cy - half), style = labelStyle)
        drawText(measurer, "E", Offset(cx + half - 14f, cy - 30f), style = labelStyle)

        // The origin, named: every offset is measured from it.
        drawLine(SP_ARP, Offset(cx - 9f, cy), Offset(cx + 9f, cy), 3f)
        drawLine(SP_ARP, Offset(cx, cy - 9f), Offset(cx, cy + 9f), 3f)
        drawText(measurer, "ARP", Offset(cx + 10f, cy + 4f), style = arpStyle)

        // Rings are labelled only once points set the scale: an empty
        // plot labelled "0.125 m" read as a station holding to a tenth
        // of a metre (desktop, 2026-10-09).
        if (points.isEmpty()) {
            val t = measurer.measure(noneText, labelStyle)
            drawText(t, topLeft = Offset(cx - t.size.width / 2f,
                                         cy - half / 2f - t.size.height))
            return@Canvas
        }
        drawText(measurer, spDistance(radius),
                 Offset(cx + half - 4f - measurer.measure(spDistance(radius), labelStyle).size.width,
                        cy + 4f), style = labelStyle)
        drawText(measurer, spDistance(radius / 2.0),
                 Offset(cx + half / 2f - 4f - measurer.measure(spDistance(radius / 2.0), labelStyle).size.width,
                        cy + 4f), style = labelStyle)

        // The run: small, because an hour is three and a half thousand
        // points and a visible dot would be a solid disc long before.
        var sumE = 0.0
        var sumN = 0.0
        for (p in points) {
            sumE += p.e
            sumN += p.n
            drawCircle(SP_DOT, 2.5f, Offset(cx + p.e * scale, cy - p.n * scale))
        }

        // Where the run centres, from the points drawn: the plot's own
        // crosshair, not a published figure -- that is the core's.
        val mx = cx + (sumE / points.size).toFloat() * scale
        val my = cy - (sumN / points.size).toFloat() * scale
        drawLine(SP_MEAN, Offset(mx - 12f, my), Offset(mx + 12f, my), 4f)
        drawLine(SP_MEAN, Offset(mx, my - 12f), Offset(mx, my + 12f), 4f)

        // The epoch that just solved, large enough to follow.
        val last = points.last()
        drawCircle(SP_DOT_LAST, 6f, Offset(cx + last.e * scale, cy - last.n * scale))
    }
}
