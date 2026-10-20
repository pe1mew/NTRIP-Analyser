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
 * **Where it lives** (author, 2026-10-12): the last tab of the analysis
 * view, after C/N0 vs elevation, and not on the hub. [SelfPosPanel]
 * stays in pro's registry only to put its section in the shared report,
 * which is assembled from the registry.
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
 * Verified against the GUI on RFSEE01, 2026-10-12, both captured in the
 * same second: the same latest epoch (9 satellites, residual 2.08 m),
 * and every window figure within what eight extra minutes in the
 * phone's run explain.
 *
 * Pro only ([Features.HAS_SELFPOS]); free shows less, never different.
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
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.TextLayoutResult
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.style.TextOverflow
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
 * desktop's rule. Capped at 7200 points, two hours
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
        /** Two hours. The desktop's `SELFPOS_CAP` was this until it went to
         *  twelve (2026-10-14) for overnight S exports; the phone exports no
         *  trail, and draws only what it keeps. */
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
 * Self-position's part of the shared report, and nothing else.
 *
 * The view itself is an analysis tab ([SelfPosView]); the hub card it
 * first had was removed on the author's direction (2026-10-12). The
 * panel stays in pro's registry because the report is assembled from
 * the registry -- a panel that draws nothing and contributes a section,
 * which the contract allows.
 */
object SelfPosPanel : Panel {
    override val key = "selfpos"

    @Composable
    override fun Content(state: HubState, actions: HubActions) {
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

/* The desktop's colours (gui_selfpos_window.c), kept so screenshots agree. */
private val SP_DOT      = Color(0xFF466EC8)   // the run
private val SP_DOT_LAST = Color(0xFFDC8214)   // the epoch just solved
private val SP_MEAN     = Color(0xFFC82828)   // where the run centres
private val SP_ARP      = Color(0xFF5A5A6E)   // the broadcast reference

/** What the plot draws, so the legend can draw the very same mark. */
private enum class SpMark { EPOCH, LATEST, MEAN, ARP }

/** One mark at [c], scaled to [unit] px: shared by plot and legend. */
private fun DrawScope.spMark(mark: SpMark, c: Offset, unit: Float) {
    when (mark) {
        SpMark.EPOCH  -> drawCircle(SP_DOT, 2.5f * unit, c)
        SpMark.LATEST -> drawCircle(SP_DOT_LAST, 6f * unit, c)
        SpMark.MEAN -> {
            drawLine(SP_MEAN, Offset(c.x - 12f * unit, c.y), Offset(c.x + 12f * unit, c.y), 4f * unit)
            drawLine(SP_MEAN, Offset(c.x, c.y - 12f * unit), Offset(c.x, c.y + 12f * unit), 4f * unit)
        }
        SpMark.ARP -> {
            drawLine(SP_ARP, Offset(c.x - 9f * unit, c.y), Offset(c.x + 9f * unit, c.y), 3f * unit)
            drawLine(SP_ARP, Offset(c.x, c.y - 9f * unit), Offset(c.x, c.y + 9f * unit), 3f * unit)
        }
    }
}

/**
 * The analysis tab: state and figures, the plot at the full width of
 * the screen, the latest epoch, and the legend.
 *
 * Its own scroll rather than the analysis template's weighted plot. A
 * circle needs as much height as width, and the template hands the plot
 * whatever the words leave: fourteen lines of them left a dot, and a
 * compact page still left a circle narrower than the screen. Here the
 * plot is square at full width, always, and the page scrolls when the
 * screen is short -- which on a 1080x2340 phone it does not.
 */
@Composable
internal fun SelfPosView(doc: BridgeDocument?) {
    val trail = MonitorService.selfPos
    // Read so a point recorded between documents still redraws us; the
    // 1 Hz document is the usual trigger.
    @Suppress("UNUSED_EXPRESSION") trail.revision
    val faint = MaterialTheme.colorScheme.onSurfaceVariant
    val pad = Modifier.padding(horizontal = 16.dp, vertical = 2.dp)
    val sr = doc?.sr
    val stats = doc?.stats

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        // The summary on the page, the explanation behind the ⓘ (R1):
        // the paragraph that stood here, then the two added with it.
        SummaryRow(
            summary = stringResource(R.string.sp_summary),
            helpTitle = stringResource(R.string.view_selfpos),
            help = stringResource(R.string.sp_explainer) + "\n\n" +
                stringResource(R.string.sp_help),
        )

        if (sr == null) {
            Text(stringResource(R.string.sp_no_run),
                 style = MaterialTheme.typography.bodyMedium, modifier = pad)
            return@Column
        }

        val st = spState(sr)
        Text(
            stringResource(
                when (st) {
                    SpState.SOLVED -> R.string.sp_state_solved
                    SpState.NOT_COMPUTABLE -> R.string.sp_state_not_computable
                    SpState.NOT_SOLVED -> R.string.sp_state_not_solved
                    SpState.GATHERING -> R.string.sp_state_gathering
                }),
            style = MaterialTheme.typography.bodyMedium,
            fontWeight = FontWeight.Bold,
            modifier = pad,
        )
        if (st != SpState.SOLVED) {
            Text(spWhy(st, sr.selfposStatus),
                 style = MaterialTheme.typography.bodySmall, color = faint,
                 modifier = pad)
        } else {
            val e = sr.selfposMeanE
            val n = sr.selfposMeanN
            val u = sr.selfposMeanU
            Column(Modifier.padding(horizontal = 16.dp, vertical = 2.dp)) {
                SpFigure(stringResource(R.string.sp_f_epochs),
                         stringResource(R.string.sp_v_of, sr.selfposSamples, sr.samples))
                if (e != null && n != null && u != null)
                    SpFigure(stringResource(R.string.sp_f_offset),
                             String.format(Locale.US, "%+.2f %+.2f %+.2f m", e, n, u))
                sr.selfposScatter?.let {
                    SpFigure(stringResource(R.string.sp_f_scatter),
                             String.format(Locale.US, "%.3f m", it))
                }
                sr.selfposRmsWorst?.let {
                    SpFigure(stringResource(R.string.sp_f_rms),
                             String.format(Locale.US, "%.3f m", it))
                }
                SpFigure(stringResource(R.string.sp_f_motion),
                         sr.selfposSpeedMedian?.let {
                             String.format(Locale.US, "%.1f mm/s", it)
                         } ?: stringResource(R.string.sp_v_no_rates))
            }
        }

        // Square, at the full width of the screen.
        SelfPosPlot(trail.points(), Modifier.fillMaxWidth().aspectRatio(1f))

        // The latest epoch: a diagnostic, labelled as one -- one epoch
        // is never the measurement. One line with the station.
        if (stats != null) {
            val latest = if (stats.selfposStatus == SPP_OK && stats.selfposSats > 0)
                stringResource(
                    R.string.sp_latest_line, stats.selfposSats,
                    String.format(Locale.US, "%.1f", stats.selfposPdop ?: 0.0),
                    String.format(Locale.US, "%.2f m", stats.selfposCodeRms ?: 0.0))
            else null
            Text(
                if (latest != null) "${stats.mountpoint} · $latest" else stats.mountpoint,
                style = MaterialTheme.typography.bodySmall,
                color = faint, modifier = pad,
            )
            // Shown only when it is news: a station whose receiver
            // encodes its rates backwards, which a rover trusting them
            // inherits (RFSEE01, 2026-10-10).
            if (latest != null && stats.selfposRateSign < 0)
                Text(stringResource(R.string.sp_reversed),
                     style = MaterialTheme.typography.bodySmall,
                     color = MaterialTheme.colorScheme.error, modifier = pad)
        }

        Row(
            Modifier.padding(horizontal = 16.dp, vertical = 6.dp),
            horizontalArrangement = Arrangement.spacedBy(14.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            SpLegend(SpMark.EPOCH, stringResource(R.string.sp_legend_epochs))
            SpLegend(SpMark.LATEST, stringResource(R.string.sp_legend_latest))
            SpLegend(SpMark.MEAN, stringResource(R.string.sp_legend_mean))
            SpLegend(SpMark.ARP, stringResource(R.string.sp_legend_arp))
        }
    }
}

/**
 * One figure: its name, and its value to the right, on one line. Never
 * wrapped -- "Mean offset E / N / U" broke onto a second line on the
 * Huawei with its value beside it (2026-10-12).
 */
@Composable
private fun SpFigure(label: String, value: String) {
    Row(Modifier.fillMaxWidth().padding(vertical = 1.dp),
        verticalAlignment = Alignment.CenterVertically) {
        Text(label, style = MaterialTheme.typography.bodySmall,
             color = MaterialTheme.colorScheme.onSurfaceVariant,
             maxLines = 1, softWrap = false, overflow = TextOverflow.Ellipsis,
             modifier = Modifier.weight(1f))
        Spacer(Modifier.width(8.dp))
        Text(value, style = MaterialTheme.typography.bodySmall,
             fontFamily = FontFamily.Monospace, maxLines = 1, softWrap = false)
    }
}

/**
 * A legend entry: the plot's own mark, drawn by the same function, and
 * its name -- colour alone tells a reader who cannot see colour nothing.
 */
@Composable
private fun SpLegend(mark: SpMark, label: String) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Canvas(Modifier.size(16.dp)) {
            spMark(mark, Offset(size.width / 2f, size.height / 2f), 1.4f)
        }
        Spacer(Modifier.width(4.dp))
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
 * one of the two things this view exists to show.
 */
@Composable
private fun SelfPosPlot(points: List<SelfPosTrail.Point>, modifier: Modifier) {
    val faint = MaterialTheme.colorScheme.onSurfaceVariant
    val labelStyle = MaterialTheme.typography.bodySmall.copy(color = faint)
    val arpStyle = MaterialTheme.typography.bodySmall.copy(color = SP_ARP)
    val page = MaterialTheme.colorScheme.surface
    val measurer = rememberTextMeasurer()
    val noneText = stringResource(R.string.sp_plot_empty)

    Canvas(modifier) {
        val cx = size.width / 2f
        val cy = size.height / 2f
        // As little margin as the N and E letters need: the circle is
        // the screen's width less two letters.
        val half = minOf(cx, cy) - 14.dp.toPx()
        if (half < 20f) return@Canvas

        var widest = 0.0
        for (p in points) {
            val d = sqrt((p.e * p.e + p.n * p.n).toDouble())
            if (d > widest) widest = d
        }
        val radius = spPlotRadius(widest)
        val scale = (half / radius).toFloat()

        // A label on a patch of the page's own colour; [above] puts its
        // baseline at y rather than its top.
        fun label(t: TextLayoutResult, x: Float, y: Float, above: Boolean = false) {
            val top = if (above) y - t.size.height else y
            drawRect(page, Offset(x - 3f, top),
                     Size(t.size.width + 6f, t.size.height.toFloat()))
            drawText(t, topLeft = Offset(x, top))
        }

        // Rings at the full radius and half of it, and the cross through
        // the reference.
        val grid = faint.copy(alpha = 0.3f)
        drawCircle(grid, half, Offset(cx, cy), style = Stroke(1.5f))
        drawCircle(grid, half / 2f, Offset(cx, cy), style = Stroke(1.5f))
        drawLine(grid, Offset(cx - half, cy), Offset(cx + half, cy), 1f)
        drawLine(grid, Offset(cx, cy - half), Offset(cx, cy + half), 1f)
        val nText = measurer.measure("N", labelStyle)
        drawText(nText, topLeft = Offset(cx + 6f, cy - half - nText.size.height / 2f))
        val eText = measurer.measure("E", labelStyle)
        drawText(eText, topLeft = Offset(cx + half - eText.size.width - 2f,
                                         cy - eText.size.height - 4f))

        spMark(SpMark.ARP, Offset(cx, cy), 1f)

        // Rings are labelled only once points set the scale: an empty
        // plot labelled "0.125 m" read as a station holding to a tenth
        // of a metre (desktop, 2026-10-09).
        if (points.isEmpty()) {
            label(measurer.measure("ARP", arpStyle), cx + 10f, cy - 4f, above = true)
            val t = measurer.measure(noneText, labelStyle)
            drawText(t, topLeft = Offset(cx - t.size.width / 2f,
                                         cy - half / 2f - t.size.height))
            return@Canvas
        }

        // The run: small, because an hour is three and a half thousand
        // points and a visible dot would be a solid disc long before.
        var sumE = 0.0
        var sumN = 0.0
        for (p in points) {
            sumE += p.e
            sumN += p.n
            spMark(SpMark.EPOCH, Offset(cx + p.e * scale, cy - p.n * scale), 1f)
        }

        // Labels over the run, each on a patch of the page, so a point
        // under one hides behind it instead of reading as part of a
        // letter -- a dot sat in "2.50 m" (Huawei, 2026-10-12). Drawn
        // before the mean and the latest epoch, which stay on top.
        // "ARP" above the axis, where the desktop puts it below: the
        // ring labels sit below the axis, and on a phone the half ring's
        // label began where "ARP" ended -- "AR2.50 m" (Huawei, 2026-10-12).
        label(measurer.measure("ARP", arpStyle), cx + 10f, cy - 4f, above = true)
        val full = measurer.measure(spDistance(radius), labelStyle)
        label(full, cx + half - 4f - full.size.width, cy + 4f)
        val halfLbl = measurer.measure(spDistance(radius / 2.0), labelStyle)
        label(halfLbl, cx + half / 2f - 4f - halfLbl.size.width, cy + 4f)

        // Where the run centres, from the points drawn: the plot's own
        // crosshair, not a published figure -- that is the core's.
        spMark(SpMark.MEAN,
               Offset(cx + (sumE / points.size).toFloat() * scale,
                      cy - (sumN / points.size).toFloat() * scale), 1f)

        // The epoch that just solved, large enough to follow.
        val last = points.last()
        spMark(SpMark.LATEST, Offset(cx + last.e * scale, cy - last.n * scale), 1f)
    }
}
