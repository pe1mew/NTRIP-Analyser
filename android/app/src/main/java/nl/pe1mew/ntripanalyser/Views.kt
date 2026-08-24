package nl.pe1mew.ntripanalyser

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.cos
import kotlin.math.log10
import kotlin.math.ln
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow
import kotlin.math.sin

/**
 * The three diagnostic views, drawn with Compose so labels stay crisp at
 * any density and satellites can be touched. The C core supplies the
 * measurements; nothing here decides anything about a station.
 *
 * Free and pro draw *the same* views. The difference is when: free draws
 * once at the end of its capture with session-mean C/N0, pro redraws
 * continuously with the live value.
 */

/**
 * A satellite's signal, with no position involved.
 *
 * C/N0 comes from the observation stream and needs no orbit, so the
 * signal views must never be fed the *positioned* subset: a satellite
 * the app cannot place is still a satellite the base is hearing, and
 * dropping it from the bars understates the station.
 */
data class SignalSat(val gnss: Int, val prn: Int, val cn0: Float)

/** A satellite ready to draw: measurement from C, position from a source. */
data class PlottedSat(
    val gnss: Int,
    val prn: Int,
    val cn0: Float,          // the value this edition shows
    val azimuthDeg: Float,
    val elevationDeg: Float,
    /**
     * Whether the orbits placed this one, or only the handset could.
     *
     * The service records what the orbits place, because it goes on
     * decoding with the screen off; the phone's own fixes exist only
     * where the UI is, so those stay here. One satellite has one source,
     * so nothing is counted twice.
     */
    val fromOrbit: Boolean = true,
)

/** Toward white by @p t, which is how a trail says "this was". */
private fun Color.lerpToWhite(t: Float) = Color(
    red = red + (1f - red) * t,
    green = green + (1f - green) * t,
    blue = blue + (1f - blue) * t,
    alpha = alpha,
)

/** Brightness by C/N0: strong satellites read solid, weak ones faint. */
private fun satAlpha(cn0: Float): Float =
    if (cn0 <= 0f) 0.35f else ((cn0 - 25f) / 25f).coerceIn(0.35f, 1f)

// ── 1. Sky view ──────────────────────────────────────────────────────

/**
 * The satellite sky: one marker per satellite at its azimuth and
 * elevation, north up, the horizon at the rim.
 *
 * @param sats      satellites that have a position
 * @param missing   satellites measured but unplaceable — stated, never
 *                  silently dropped, so a sparse plot is not misread as
 *                  a station tracking poorly
 * @param source    named in the header for the same reason
 */
@Composable
fun SkyView(
    sats: List<PlottedSat>,
    missing: Int,
    source: PositionSource,
    footer: String,
    rinexAgeS: Double?,
    onSourceClick: () -> Unit,
    modifier: Modifier = Modifier,
    /** Where each satellite has been; empty in free. */
    tracks: TrackAccumulator? = null,
    trackRevision: Int = 0,
) {
    val onSurface = MaterialTheme.colorScheme.onSurface
    val faint = MaterialTheme.colorScheme.onSurfaceVariant
    val surface = MaterialTheme.colorScheme.surface
    val density = LocalDensity.current

    AnalysisBands(
        modifier = modifier,
        // The screen says what this view is showing, above the tabs;
        // saying it again here would be the same sentence twice.
        explainer = null,
        summary = {
            // The provenance is in the sentence rather than in a chip
            // beside it, coloured by the same judgement the chip made
            // and leading to the same page. A reader looking for where
            // the picture came from looks at the words about it.
            val phrase = orbitSourcePhrase(source, rinexAgeS)
            val tint = orbitSourceTint(source, rinexAgeS)
            val line = stringResource(
                R.string.sky_header, sats.size, sats.size + missing, phrase,
            )
            val at = line.lastIndexOf(phrase)
            Text(
                buildAnnotatedString {
                    append(line)
                    if (at >= 0) {
                        addStyle(
                            SpanStyle(color = tint, fontWeight = FontWeight.Medium),
                            at, at + phrase.length,
                        )
                    }
                },
                style = MaterialTheme.typography.bodyMedium,
                modifier = Modifier
                    .clickable(onClick = onSourceClick)
                    .padding(horizontal = 16.dp, vertical = 4.dp),
            )
            if (missing > 0) {
                Text(
                    stringResource(R.string.sky_missing, missing, sourceName(source)),
                    style = MaterialTheme.typography.bodySmall,
                    color = faint,
                    modifier = Modifier.padding(horizontal = 16.dp),
                )
            }
        },
        plot = { m -> SkyCanvas(sats, onSurface, faint, surface, density,
                                m.padding(12.dp), tracks, trackRevision) },
        footer = {
            Text(
                footer,
                style = MaterialTheme.typography.bodySmall,
                color = faint,
                fontFamily = FontFamily.Monospace,
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 6.dp),
            )
        },
        legend = {
            ConstellationLegend(
                sats.map { it.gnss }.distinct().sorted(),
                Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
            )
        },
    )
}

/**
 * The bands of an analysis screen, in the order the template fixes.
 *
 * GUI v3, P3.1 (`design/guiV3spec.md` §4): what this view is, then the
 * numbers behind the picture, then the picture, then whatever the
 * picture is of, then the key to its colours. A view supplies the parts
 * and cannot choose the order, which is the point -- the three of them
 * had drifted into three different orders, with the sky view's legend at
 * the top and the other two at the bottom.
 *
 * @param explainer one sentence saying what is being shown, or `null`
 *                  where the screen itself has already said it.
 * @param summary   the numbers behind the plot.
 * @param footer    what the plot is of -- the station and its ARP on
 *                  the sky view, nothing on the other two.
 * @param legend    the colours, last, nearest the plot they describe.
 */
@Composable
internal fun AnalysisBands(
    explainer: String?,
    summary: @Composable ColumnScope.() -> Unit,
    plot: @Composable (Modifier) -> Unit,
    modifier: Modifier = Modifier,
    footer: @Composable ColumnScope.() -> Unit = {},
    legend: @Composable ColumnScope.() -> Unit = {},
) {
    val faint = MaterialTheme.colorScheme.onSurfaceVariant
    PlotLayout(
        modifier = modifier,
        above = {
            if (explainer != null) {
                Text(
                    explainer,
                    style = MaterialTheme.typography.bodySmall,
                    color = faint,
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
                )
            }
            summary()
        },
        plot = plot,
        below = {
            footer()
            legend()
        },
    )
}

/**
 * Words and a plot, arranged for the shape of the screen.
 *
 * A stacked column always, as all three views have always been — but
 * when the viewport is too short to give the plot a usable share, the
 * plot keeps its size and the screen scrolls instead. Squeezing it into
 * whatever the words left over is what made the sky view a dot and the
 * C/N0 plot a line in landscape.
 *
 * Shared by all three analysis views: the same bug had three instances,
 * and fixing it three times is how they start behaving differently.
 */
@Composable
internal fun PlotLayout(
    above: @Composable ColumnScope.() -> Unit,
    plot: @Composable (Modifier) -> Unit,
    modifier: Modifier = Modifier,
    below: @Composable ColumnScope.() -> Unit = {},
) {
    BoxWithConstraints(modifier.fillMaxSize()) {
        if (maxHeight >= TALL_ENOUGH) {
            // Tall enough to stack: exactly what all three views always
            // did, so portrait is unchanged.
            Column(Modifier.fillMaxSize()) {
                above()
                plot(Modifier.weight(1f).fillMaxWidth())
                below()
            }
        } else {
            // Short: keep the plot readable and let the screen scroll,
            // rather than squeezing it into what the words left over.
            // Landscape on a phone gave the sky plot about a fifth of
            // the height it needs -- rings collapsed to a dot, the
            // C/N0 plot to a line.
            Column(
                Modifier
                    .fillMaxSize()
                    .verticalScroll(rememberScrollState())
            ) {
                above()
                plot(Modifier.fillMaxWidth().height(MIN_PLOT))
                below()
            }
        }
    }
}

/** Below this the screen scrolls instead of squeezing the plot. */
private val TALL_ENOUGH = 400.dp

/** What a plot needs to stay readable: rings, labels and all. */
private val MIN_PLOT = 340.dp

/**
 * The plot itself: rings, cardinals, satellites, elevation numbers.
 *
 * Separated from [SkyView] so the drawing is one thing and the layout
 * around it another, rather than the two being tangled and the plot
 * existing twice.
 */
@Composable
private fun SkyCanvas(
    sats: List<PlottedSat>,
    onSurface: Color,
    faint: Color,
    surface: Color,
    density: Density,
    modifier: Modifier = Modifier,
    tracks: TrackAccumulator? = null,
    trackRevision: Int = 0,
) {
    // The arcs to draw, built once per document rather than once per
    // frame. `runsFor` copies and splits a satellite's whole history,
    // and a day of it across forty satellites is 57 000 points -- cheap
    // at 1 Hz, not at 60. Keyed on the revision as well as the list,
    // because a point can be added without the satellites changing.
    val trails: List<Pair<Int, List<List<TrackAccumulator.Point>>>> =
        remember(tracks, trackRevision, sats) {
            if (tracks == null) emptyList()
            else sats.map { s -> s.gnss to tracks.runsFor(s.gnss, s.prn) }
        }
        Canvas(modifier) {
            val cx = size.width / 2f
            val cy = size.height / 2f
            val radius = min(cx, cy) - with(density) { 18.dp.toPx() }

            // Elevation rings. The radius is linear in elevation, as the
            // desktop draws it: 90 deg at the centre, 0 at the rim.
            for (el in 0..75 step 15) {
                val r = radius * (90f - el) / 90f
                drawCircle(
                    color = faint.copy(alpha = if (el == 0) 0.6f else 0.25f),
                    radius = r, center = Offset(cx, cy),
                    style = Stroke(width = 1f),
                )
            }

            val dash = PathEffect.dashPathEffect(floatArrayOf(6f, 8f))
            drawLine(faint.copy(alpha = 0.4f), Offset(cx - radius, cy),
                     Offset(cx + radius, cy), pathEffect = dash)
            drawLine(faint.copy(alpha = 0.4f), Offset(cx, cy - radius),
                     Offset(cx, cy + radius), pathEffect = dash)

            drawCardinals(cx, cy, radius, onSurface, density.density)

            // Markers and their labels scale with the plot, not with the
            // screen. In landscape the whole plot is about a third the
            // size it is in portrait, and fixed 7 dp markers with 11 sp
            // labels turned it into a heap: forty satellites drawn at
            // full size on a plot too small to separate them.
            val markerR = minOf(with(density) { 7.dp.toPx() }, radius * 0.075f)
            val labelPx = minOf(with(density) { 11.sp.toPx() }, radius * 0.11f)
            // Trails first, so a marker always sits on top of its own
            // history. The hue is the constellation's, lightened 30 %
            // toward white -- the desktop's rule, and for its reason:
            // history should not compete with the live position.
            if (trails.isNotEmpty()) {
                for ((gnss, runs) in trails) {
                    val trail = Gnss.colour(gnss).lerpToWhite(0.3f)
                    for (run in runs) {
                        var prev: Offset? = null
                        var prevAz = 0f
                        for (p in run) {
                            val (px, py) = polar(cx, cy, radius, p.azDeg, p.elDeg)
                            val here = Offset(px, py)
                            // A step across north is a wrap, not a
                            // journey: drawn as a line it would be a
                            // chord straight through the plot.
                            val wrapped = prev != null &&
                                kotlin.math.abs(p.azDeg - prevAz) > 180f
                            if (prev != null && !wrapped) {
                                // Thinner than the marker it belongs
                                // to, deliberately: at 0.35 the trail
                                // carried the same visual weight as the
                                // satellite and the plot read as a mesh.
                                drawLine(trail, prev, here, strokeWidth = markerR * 0.16f)
                            }
                            drawCircle(trail, markerR * 0.13f, here)
                            prev = here
                            prevAz = p.azDeg
                        }
                    }
                }
            }

            for (s in sats) {
                val (x, y) = polar(cx, cy, radius, s.azimuthDeg, s.elevationDeg,
                                   markerR)
                val c = Gnss.colour(s.gnss)

                drawCircle(c.copy(alpha = satAlpha(s.cn0)), markerR, Offset(x, y))
                drawCircle(Color.White.copy(alpha = 0.9f), markerR,
                           Offset(x, y), style = Stroke(width = 1.5f))

                drawContext.canvas.nativeCanvas.apply {
                    val paint = android.graphics.Paint().apply {
                        color = onSurface.toArgb()
                        textSize = labelPx
                        isAntiAlias = true
                    }
                    drawText(Gnss.name(s.gnss, s.prn),
                             x + markerR + 3f, y + markerR / 2f, paint)
                }
            }

            // Last, so a ring label never disappears under a satellite
            // that happens to sit on it -- the numbers are the plot's
            // elevation axis and have to stay readable.
            drawElevationNumbers(cx, cy, radius, faint, surface, density.density)
        }
}

/**
 * Azimuth clockwise from north, radius linear in elevation.
 *
 * @param markerR half a marker's width, held back from the rim so a
 *        satellite on the horizon is drawn just inside the plot rather
 *        than straddling it. Only the lowest couple of degrees move at
 *        all, so markers stay aligned with the elevation rings.
 */
private fun polar(
    cx: Float, cy: Float, radius: Float, azDeg: Float, elDeg: Float,
    markerR: Float = 0f,
): Pair<Float, Float> {
    val r = min(radius * (90f - elDeg.coerceIn(0f, 90f)) / 90f,
                radius - markerR)
    val a = Math.toRadians(azDeg.toDouble())
    return (cx + r * sin(a).toFloat()) to (cy - r * cos(a).toFloat())
}

private fun DrawScope.drawCardinals(
    cx: Float, cy: Float, radius: Float, colour: Color, density: Float,
) {
    val paint = android.graphics.Paint().apply {
        color = colour.toArgb()
        textSize = 13f * density
        isAntiAlias = true
        textAlign = android.graphics.Paint.Align.CENTER
    }
    val pad = 13f * density
    drawContext.canvas.nativeCanvas.apply {
        drawText("N", cx, cy - radius - pad * 0.3f, paint)
        drawText("S", cx, cy + radius + pad, paint)
        drawText("E", cx + radius + pad * 0.8f, cy + pad * 0.35f, paint)
        drawText("W", cx - radius - pad * 0.8f, cy + pad * 0.35f, paint)
    }
}

private fun DrawScope.drawElevationNumbers(
    cx: Float, cy: Float, radius: Float, colour: Color, halo: Color,
    density: Float,
) {
    val paint = android.graphics.Paint().apply {
        color = colour.toArgb()
        textSize = 10f * density
        isAntiAlias = true
    }
    // Drawn under each number in the background colour, so the label
    // reads over a marker instead of merging into it.
    val outline = android.graphics.Paint(paint).apply {
        color = halo.toArgb()
        style = android.graphics.Paint.Style.STROKE
        strokeWidth = 3f * density
    }
    drawContext.canvas.nativeCanvas.apply {
        for (el in 15..75 step 15) {
            val r = radius * (90f - el) / 90f
            // The degree sign on every ring, not only the first: the
            // numbers are an elevation axis and must carry their unit.
            val x = cx + 4f * density
            val y = cy - r + 4f * density
            drawText("$el°", x, y, outline)
            drawText("$el°", x, y, paint)
        }
    }
}

/**
 * The constellations on show, swatch and name together.
 *
 * Wraps onto a second line when they do not fit. Seven constellations
 * on a phone do not: the last entry was squeezed into whatever space
 * was left, so "NavIC" arrived broken across the screen edge or its
 * swatch was flattened to an ellipse. Each entry is one unbreakable
 * unit -- a colour means nothing without the name beside it -- so the
 * wrapping happens between entries, never inside one.
 *
 * Used by all three analysis views, which is why it is one composable.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ConstellationLegend(ids: List<Int>, modifier: Modifier = Modifier) {
    FlowRow(
        modifier,
        horizontalArrangement = Arrangement.spacedBy(10.dp),
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        ids.forEach { id ->
            Row(verticalAlignment = Alignment.CenterVertically) {
                Canvas(Modifier.size(10.dp)) { drawCircle(Gnss.colour(id)) }
                Spacer(Modifier.width(4.dp))
                Text(
                    Gnss.label(id),
                    style = MaterialTheme.typography.labelSmall,
                    maxLines = 1,
                )
            }
        }
    }
}

// ── 2. C/N0 per satellite ────────────────────────────────────────────

/**
 * A bar per satellite, coloured by constellation.
 *
 * @param liveValues true while a run is in progress, when the bars show
 *                   this epoch; false once it has stopped, when they show
 *                   the session mean.
 *
 *                   **Not an edition difference.** This read "(pro)" and
 *                   "(free)" for months, but the call site passes
 *                   `runState.running` and has never consulted
 *                   `Features`. Both editions draw these bars.
 */
@Composable
fun SignalBars(
    sats: List<SignalSat>,
    liveValues: Boolean,
    modifier: Modifier = Modifier,
) {
    val faint = MaterialTheme.colorScheme.onSurfaceVariant
    val onSurface = MaterialTheme.colorScheme.onSurface
    val density = LocalDensity.current
    val shown = sats.filter { it.cn0 > 0f }.sortedWith(
        compareBy({ it.gnss }, { it.prn })
    )

    AnalysisBands(
        modifier = modifier,
        explainer = stringResource(
            if (liveValues) R.string.bars_live else R.string.bars_mean
        ),
        summary = {
        val meanPower = if (shown.isEmpty()) 0f else
            10f * log10(shown.map { 10f.pow(it.cn0 / 10f) }.average().toFloat())

        Text(
            stringResource(
                R.string.bars_header, shown.size, meanPower.toDouble(),
                (shown.minOfOrNull { it.cn0 } ?: 0f).toDouble(),
                (shown.maxOfOrNull { it.cn0 } ?: 0f).toDouble(),
            ),
            style = MaterialTheme.typography.bodyMedium,
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
        )

        Text(
            stringResource(R.string.axis_cn0),
            style = MaterialTheme.typography.labelSmall,
            color = faint,
            modifier = Modifier.padding(start = 16.dp, top = 6.dp),
        )

    }, plot = { plotModifier ->
        Canvas(
            plotModifier
                .padding(start = 40.dp, end = 12.dp, top = 4.dp, bottom = 26.dp)
        ) {
            if (shown.isEmpty()) return@Canvas
            val lo = 20f
            val hi = 60f
            fun yFor(v: Float) = size.height * (1f - (v - lo) / (hi - lo))

            val axis = android.graphics.Paint().apply {
                color = faint.toArgb()
                textSize = 10f * density.density
                isAntiAlias = true
                textAlign = android.graphics.Paint.Align.RIGHT
            }
            for (v in 20..60 step 10) {
                val y = yFor(v.toFloat())
                drawLine(faint.copy(alpha = 0.2f), Offset(0f, y), Offset(size.width, y))
                drawContext.canvas.nativeCanvas.drawText(
                    "$v", -4f, y + 3f * density.density, axis)
            }

            val slot = size.width / shown.size
            val barW = max(2f, slot * 0.7f)
            val label = android.graphics.Paint().apply {
                color = onSurface.toArgb()
                textSize = 8f * density.density
                isAntiAlias = true
                textAlign = android.graphics.Paint.Align.CENTER
            }
            shown.forEachIndexed { i, s ->
                val x = i * slot + slot / 2f
                val y = yFor(s.cn0.coerceIn(lo, hi))
                drawRect(
                    color = Gnss.colour(s.gnss),
                    topLeft = Offset(x - barW / 2f, y),
                    size = androidx.compose.ui.geometry.Size(barW, size.height - y),
                )
                // Only label when the bars are wide enough to read.
                if (slot > 16f * density.density) {
                    drawContext.canvas.nativeCanvas.drawText(
                        "%d".format(s.prn), x, size.height + 12f * density.density, label)
                }
            }
        }

    }, footer = {
        Text(
            stringResource(R.string.axis_sat),
            style = MaterialTheme.typography.labelSmall,
            color = faint,
            modifier = Modifier.align(Alignment.CenterHorizontally),
        )
    }, legend = {
        ConstellationLegend(
            shown.map { it.gnss }.distinct().sorted(),
            Modifier.padding(horizontal = 16.dp, vertical = 6.dp),
        )
    })
}

// ── 3. C/N0 versus elevation ─────────────────────────────────────────

/**
 * Where each satellite has been, for as long as one screen wants it.
 *
 * Phase 2 item 1, pro only (`design/work-items/satellite-tracks.md`).
 * A sibling of [ElevationAccumulator], and here for the same reason: a
 * trail is not a measurement, it is a record of positions the core has
 * already computed, kept by the screen that draws them. The desktop
 * accumulates its own the same way (`gui/gui_state.h`).
 *
 * The rules are the desktop's, so that a trail means the same thing in
 * both products: one sample per [INTERVAL_S] per satellite, and a run
 * broken where consecutive samples are more than [GAP_BREAK_S] apart --
 * a satellite that sets and rises again is two arcs, not a chord across
 * the plot.
 *
 * The **cap** is not the desktop's. It keeps 24 hours per SV at 11.3 MB;
 * a phone keeps four, at about 300 kB, which covers a watch-mode session
 * without asking a handset to hold a day of sky.
 */
class TrackAccumulator {

    /** One remembered position. */
    data class Point(val azDeg: Float, val elDeg: Float, val tSeconds: Double)

    private val trails = HashMap<Int, ArrayDeque<Point>>()

    /** Bumped whenever a point is added, because Compose cannot see inside. */
    var revision: Int = 0
        private set

    /**
     * Offer a position. Kept only if [INTERVAL_S] has passed since this
     * satellite's last kept sample, so the trail is evenly spaced however
     * often the plot happens to refresh.
     *
     * @return true if the point was kept.
     */
    @Synchronized
    fun offer(gnss: Int, prn: Int, azDeg: Float, elDeg: Float, tSeconds: Double): Boolean {
        val key = gnss * 256 + prn
        val trail = trails.getOrPut(key) { ArrayDeque() }
        val last = trail.lastOrNull()
        if (last != null && tSeconds - last.tSeconds < INTERVAL_S) return false

        trail.addLast(Point(azDeg, elDeg, tSeconds))
        while (trail.size > CAP) trail.removeFirst()
        revision++
        return true
    }

    /**
     * The runs to draw for one satellite: each an unbroken arc.
     *
     * Split where the gap in time says the satellite was away, so a rise
     * after a set is drawn as its own arc. Azimuth wrap is left to the
     * renderer, which is where the projection lives.
     */
    @Synchronized
    fun runsFor(gnss: Int, prn: Int): List<List<Point>> {
        val trail = trails[gnss * 256 + prn] ?: return emptyList()
        if (trail.isEmpty()) return emptyList()

        val runs = ArrayList<List<Point>>()
        var current = ArrayList<Point>()
        for (p in trail) {
            val last = current.lastOrNull()
            if (last != null && p.tSeconds - last.tSeconds > GAP_BREAK_S) {
                if (current.size > 1) runs.add(current)
                current = ArrayList()
            }
            current.add(p)
        }
        if (current.size > 1) runs.add(current)
        return runs
    }

    /** How many points are held, across every satellite. Tests and probes. */
    val size: Int @Synchronized get() = trails.values.sumOf { it.size }

    /** A trail belongs to one run, as the elevation scatter does. */
    @Synchronized
    fun clear() {
        trails.clear()
        revision++
    }

    companion object {
        /** Seconds between kept samples. The desktop's value. */
        const val INTERVAL_S = 60.0

        /** A longer gap than this starts a new arc. The desktop's value. */
        const val GAP_BREAK_S = 300.0

        /**
         * A day per satellite, which is the desktop's number too.
         *
         * It was four hours, on the reasoning that a phone holds a
         * watch-mode session rather than a day. A nine-hour capture then
         * showed nine hours of measurement under four hours of arc, and
         * the reader has no way to tell a cap from a satellite that was
         * not there. 1440 points across forty satellites is under two
         * megabytes, and the runs are built once a minute rather than
         * once a frame, so the drawing does not care either.
         */
        const val CAP = 1440
    }
}

/**
 * The hand-over detail: the analysis screens’ template, filled with
 * the reference position’s story.
 *
 * Laid out through [AnalysisBands] by the author’s direction
 * (2026-08-24): the same six bands as the three analysis views, in the
 * same order, through the same composables — so this screen inherits
 * what `PlotLayout` already solved (a plot squeezed into leftover
 * height in landscape) instead of rediscovering it.
 *
 * The plot is the desktop’s VRS monitor, redrawn: rover at the
 * centre, the reference position at its true bearing with the radius
 * scaled to fit, the history as dots joined faintly in order — a
 * hand-over reads as a jump in the dots — and the five-minute
 * distance ring as a strip chart beneath.
 */
@Composable
internal fun HandoverDetail(
    stats: Stats,
    vrs: VrsDoc?,
    roverLat: Double,
    roverLon: Double,
    mountpoint: String,
    roverIsFix: Boolean,
) {
    val sum = hoSummary(stats, vrs, roverLat, roverLon)
    val trail = MonitorService.arpTrail
    // Read so a dot recorded between documents still redraws us; the
    // 1 Hz document is the usual trigger.
    @Suppress("UNUSED_EXPRESSION") trail.revision

    val faint = MaterialTheme.colorScheme.onSurfaceVariant

    AnalysisBands(
        explainer = stringResource(R.string.ho_explainer),
        summary = {
            if (sum == null) return@AnalysisBands
            Text(
                stringResource(R.string.ho_bearing,
                               stringResource(
                                   if (roverIsFix) R.string.ho_distance_fix
                                   else R.string.ho_distance_set,
                                   distanceText(sum.distM), sum.compass),
                               sum.bearingDeg.toInt()),
                style = MaterialTheme.typography.bodyMedium,
                color = hoDistColour(sum.distM),
                fontWeight = FontWeight.Medium,
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
            )
            val moves = stats.arpMoves
            Text(
                when {
                    moves == 0 -> stringResource(R.string.ho_stable)
                    sum.isNetwork -> stringResource(R.string.ho_moves_net, moves)
                    else -> stringResource(R.string.ho_moves_fixed, moves)
                },
                style = MaterialTheme.typography.bodySmall,
                color = if (moves > 0 && !sum.isNetwork)
                    MaterialTheme.colorScheme.error else faint,
                modifier = Modifier.padding(horizontal = 16.dp),
            )
        },
        plot = { m ->
            Column(m.padding(12.dp)) {
                HandoverPolar(
                    trail, stats, roverLat, roverLon,
                    Modifier.weight(3f).fillMaxWidth(),
                )
                Spacer(Modifier.height(8.dp))
                DistanceStrip(
                    trail,
                    Modifier.weight(1f).fillMaxWidth(),
                )
            }
        },
        footer = {
            Text(
                stringResource(
                    R.string.ho_footer, mountpoint,
                    stringResource(
                        if (roverIsFix) R.string.ho_rover_fix
                        else R.string.ho_rover_set)),
                style = MaterialTheme.typography.bodySmall,
                color = faint,
                fontFamily = FontFamily.Monospace,
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 6.dp),
            )
        },
        legend = {
            Row(
                Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
                horizontalArrangement = Arrangement.spacedBy(16.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                LegendDot(HO_ROVER, stringResource(R.string.ho_legend_rover))
                LegendDot(HO_ARP, stringResource(R.string.ho_legend_arp))
                LegendDot(HO_HIST, stringResource(R.string.ho_legend_hist))
            }
        },
    )
}

/** The desktop VRS monitor’s colours, kept so screenshots agree. */
private val HO_ROVER = Color(0xFF1E50C8)
private val HO_ARP   = Color(0xFFD22828)
private val HO_HIST  = Color(0xFFF0AAAA)

@Composable
private fun LegendDot(colour: Color, label: String) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Canvas(Modifier.size(10.dp)) { drawCircle(colour) }
        Spacer(Modifier.width(6.dp))
        Text(label, style = MaterialTheme.typography.bodySmall,
             color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

/** Rover centred; everything else at true bearing, radius to scale. */
@Composable
private fun HandoverPolar(
    trail: ArpTrail,
    stats: Stats,
    roverLat: Double,
    roverLon: Double,
    modifier: Modifier,
) {
    val faint = MaterialTheme.colorScheme.onSurfaceVariant
    val ringLabel = stringResource(
        R.string.ho_ring,
        distanceText(polarScaleM(trail, stats, roverLat, roverLon)))
    val labelStyle = MaterialTheme.typography.bodySmall
    val measurer = rememberTextMeasurer()

    Canvas(modifier) {
        val cx = size.width / 2f
        val cy = size.height / 2f
        val radius = minOf(cx, cy) * 0.86f
        val scaleM = polarScaleM(trail, stats, roverLat, roverLon)

        fun place(lat: Double, lon: Double): Offset {
            val d = geoDistanceM(roverLat, roverLon, lat, lon)
            val b = geoBearingDeg(roverLat, roverLon, lat, lon) *
                Math.PI / 180.0
            val r = (d / scaleM).toFloat().coerceAtMost(1f) * radius
            return Offset(cx + r * kotlin.math.sin(b).toFloat(),
                          cy - r * kotlin.math.cos(b).toFloat())
        }

        // Rings: full scale and half.
        drawCircle(faint.copy(alpha = 0.35f), radius, Offset(cx, cy),
                   style = Stroke(1.5f))
        drawCircle(faint.copy(alpha = 0.25f), radius / 2f, Offset(cx, cy),
                   style = Stroke(1.5f))

        // History, oldest to newest, joined faintly: a hand-over is a
        // long segment between two dots.
        val dots = trail.dots()
        var prev: Offset? = null
        for (d in dots) {
            val here = place(d.lat, d.lon)
            prev?.let { drawLine(HO_HIST, it, here, strokeWidth = 3f) }
            drawCircle(HO_HIST, 6f, here)
            prev = here
        }

        // The reference position now, on top of its own history.
        val alat = stats.arpLat
        val alon = stats.arpLon
        if (stats.arpValid && alat != null && alon != null)
            drawCircle(HO_ARP, 10f, place(alat, alon))

        // The rover, centre of its own world.
        drawCircle(HO_ROVER, 10f, Offset(cx, cy))

        drawText(
            measurer, ringLabel, Offset(cx + 8f, cy - radius + 6f),
            style = labelStyle.copy(color = faint),
        )
    }
}

/** How many metres the outer ring stands for: the furthest thing, padded. */
private fun polarScaleM(
    trail: ArpTrail, stats: Stats, roverLat: Double, roverLon: Double,
): Double {
    var worst = 100.0                       // floor: a 2 m ARP still draws
    val alat = stats.arpLat
    val alon = stats.arpLon
    if (stats.arpValid && alat != null && alon != null)
        worst = maxOf(worst, geoDistanceM(roverLat, roverLon, alat, alon))
    for (d in trail.dots())
        worst = maxOf(worst, geoDistanceM(roverLat, roverLon, d.lat, d.lon))
    return worst * 1.2
}

/** The last five minutes of rover-to-ARP distance, oldest at the left. */
@Composable
private fun DistanceStrip(trail: ArpTrail, modifier: Modifier) {
    val faint = MaterialTheme.colorScheme.onSurfaceVariant
    val label = stringResource(R.string.ho_chart)
    val labelStyle = MaterialTheme.typography.bodySmall
    val measurer = rememberTextMeasurer()

    Canvas(modifier) {
        val km = trail.distances()
        drawRect(faint.copy(alpha = 0.08f))
        drawText(measurer, label, Offset(6f, 2f),
                 style = labelStyle.copy(color = faint))
        if (km.size < 2) return@Canvas

        var maxKm = 0.001f
        for (v in km) if (!v.isNaN() && v > maxKm) maxKm = v

        val stepX = size.width / (ArpTrail.RING_N - 1).toFloat()
        var prev: Offset? = null
        km.forEachIndexed { i, v ->
            if (v.isNaN()) { prev = null; return@forEachIndexed }
            val x = size.width - (km.size - 1 - i) * stepX
            val y = size.height * (1f - (v / maxKm) * 0.9f)
            val here = Offset(x, y)
            prev?.let { drawLine(HO_ARP, it, here, strokeWidth = 2.5f) }
            prev = here
        }
    }
}

/**
 * Where the reference position has been, and how far away it was.
 *
 * The hand-over history (phase 2 item 3): a dot per distinct ARP, on
 * the desktop's rule -- a new position more than 10 m from the last
 * *recorded* one -- capped at 32, plus a five-minute ring of the
 * rover-to-ARP distance for the strip chart. Run-scoped and owned by
 * `MonitorService`'s companion, because a record of a run outlives the
 * screen that draws it.
 */
class ArpTrail {

    data class Dot(val lat: Double, val lon: Double)

    private val dots = ArrayList<Dot>()

    /** km, NaN where no sample; a ring, newest at [head]-1. */
    private val ring = FloatArray(RING_N) { Float.NaN }
    private var head = 0
    private var ringCount = 0

    /** Bumped on any recording, because Compose cannot see inside. */
    var revision: Int = 0
        private set

    /** Note a broadcast reference position; records it if it moved. */
    @Synchronized
    fun offerDot(lat: Double, lon: Double) {
        val last = dots.lastOrNull()
        if (last != null &&
            geoDistanceM(last.lat, last.lon, lat, lon) <= MOVE_M) return
        if (dots.size >= DOT_CAP) dots.removeAt(0)
        dots.add(Dot(lat, lon))
        revision++
    }

    /** One rover-to-ARP distance sample, at the publish cadence. */
    @Synchronized
    fun offerDistance(km: Float) {
        ring[head] = km
        head = (head + 1) % RING_N
        if (ringCount < RING_N) ringCount++
        revision++
    }

    @Synchronized
    fun dots(): List<Dot> = ArrayList(dots)

    /** Oldest first, NaN-free tail trimmed by the caller if it cares. */
    @Synchronized
    fun distances(): FloatArray {
        val out = FloatArray(ringCount)
        for (i in 0 until ringCount)
            out[i] = ring[(head - ringCount + i + RING_N) % RING_N]
        return out
    }

    /** Hand-overs seen by this trail: recorded positions minus one. */
    val moves: Int @Synchronized get() = (dots.size - 1).coerceAtLeast(0)

    @Synchronized
    fun clear() {
        dots.clear()
        ring.fill(Float.NaN)
        head = 0
        ringCount = 0
        revision++
    }

    companion object {
        /** The desktop's move threshold; `NS_ARP_MOVE_M` in the core. */
        const val MOVE_M = 10.0

        /** The desktop keeps 32 positions; so does the phone. */
        const val DOT_CAP = 32

        /** Five minutes at the 1 Hz publish, the desktop's chart. */
        const val RING_N = 300
    }
}

/** Initial great-circle bearing, degrees clockwise from north. */
internal fun geoBearingDeg(lat1: Double, lon1: Double,
                           lat2: Double, lon2: Double): Double {
    val d = Math.PI / 180.0
    val dlo = (lon2 - lon1) * d
    val y = kotlin.math.sin(dlo) * kotlin.math.cos(lat2 * d)
    val x = kotlin.math.cos(lat1 * d) * kotlin.math.sin(lat2 * d) -
        kotlin.math.sin(lat1 * d) * kotlin.math.cos(lat2 * d) *
        kotlin.math.cos(dlo)
    val deg = kotlin.math.atan2(y, x) / d
    return (deg + 360.0) % 360.0
}

/** An eight-point compass name for a bearing. */
internal fun compassPoint(deg: Double): String {
    val names = arrayOf("N", "NE", "E", "SE", "S", "SW", "W", "NW")
    return names[(((deg + 22.5) % 360.0) / 45.0).toInt()]
}

/** "850 m" under a kilometre, "4.2 km" above. */
internal fun distanceText(m: Double): String =
    if (m < 1000.0) "%.0f m".format(m) else "%.1f km".format(m / 1000.0)

/** Great-circle distance in metres, spherical earth. */
internal fun geoDistanceM(lat1: Double, lon1: Double,
                          lat2: Double, lon2: Double): Double {
    val d = Math.PI / 180.0
    val dla = (lat2 - lat1) * d
    val dlo = (lon2 - lon1) * d
    val sa = kotlin.math.sin(dla / 2)
    val so = kotlin.math.sin(dlo / 2)
    val a = sa * sa +
        kotlin.math.cos(lat1 * d) * kotlin.math.cos(lat2 * d) * so * so
    return 2.0 * 6371000.0 *
        kotlin.math.atan2(kotlin.math.sqrt(a), kotlin.math.sqrt(1.0 - a))
}

/**
 * The elevation scatter, accumulated into the plot rather than into a
 * list of samples.
 *
 * A sample is only ever drawn: once its position is known, the sample
 * itself carries no further information, so it is counted into the cell
 * it lands in and forgotten. That is what makes the memory bounded --
 * one counter per (constellation, degree of elevation, half-decibel) --
 * where the list it replaces held every sample and, past a cap, began
 * discarding the oldest.
 *
 * The cap was 20 000, which sounds generous and is about eight minutes:
 * roughly forty satellites arrive every second. A watch run of several
 * hours therefore plotted its last eight minutes and quietly dropped
 * the rest, and paid for it -- every new sample shifted a 20 000-element
 * list and recomposed the plot. Neither is visible on screen, which is
 * the worst property a measurement can have.
 *
 * Nothing is lost now: an eight-hour run and a two-minute one both keep
 * every sample they took, in the same 250 kB.
 */
class ElevationAccumulator {

    /** Cell counts, indexed [gnss][elevation degree][half-decibel]. */
    private val cells = IntArray(GNSS_SLOTS * EL_BINS * CN0_BINS)

    /** Samples counted, which is now the true total for the session. */
    var total: Long = 0L
        private set

    private val seen = HashSet<Int>()

    /**
     * Constellations that have contributed, for the legend.
     *
     * Synchronised because the service adds while the screen draws;
     * `forEachCell` deliberately is not -- it reads plain counters, and
     * a cell one sample behind is invisible, where holding the lock for
     * a whole plot would not be.
     */
    val constellations: List<Int> @Synchronized get() = seen.sorted()

    /** Bumped on every sample, so a view can key on it as tracks do. */
    var revision: Int = 0
        private set

    @Synchronized
    fun add(gnss: Int, elevationDeg: Float, cn0: Float) {
        if (gnss !in 1 until GNSS_SLOTS) return
        val el = (elevationDeg / EL_STEP).toInt().coerceIn(0, EL_BINS - 1)
        val cn = (cn0 / CN0_STEP).toInt().coerceIn(0, CN0_BINS - 1)
        cells[(gnss * EL_BINS + el) * CN0_BINS + cn]++
        total++
        seen.add(gnss)
        revision++
    }

    @Synchronized
    fun clear() {
        cells.fill(0)
        total = 0L
        seen.clear()
        revision++
    }

    /**
     * Visit every occupied cell: constellation, and the elevation and
     * C/N0 of its **lower corner**, so the caller can draw the cell as
     * the area it stands for rather than as a dot in the middle of it.
     */
    inline fun forEachCell(action: (Int, Float, Float, Int) -> Unit) {
        for (g in 1 until GNSS_SLOTS) {
            for (el in 0 until EL_BINS) {
                for (cn in 0 until CN0_BINS) {
                    val n = countAt(g, el, cn)
                    if (n > 0) action(g, el * EL_STEP, cn * CN0_STEP, n)
                }
            }
        }
    }

    /** For [forEachCell], which cannot see a private array from inline. */
    fun countAt(gnss: Int, el: Int, cn: Int): Int =
        cells[(gnss * EL_BINS + el) * CN0_BINS + cn]

    companion object {
        /** 0..7, so a constellation id indexes directly. */
        const val GNSS_SLOTS = 8

        /*
         * Cell size, set by the coarsest thing the data can be, not by
         * the finest the plot could draw.
         *
         * **C/N0 arrives quantised, and how coarsely depends on the
         * message.** MSM4 and MSM5 carry it in six bits -- whole
         * decibels, nothing between them. So a half-decibel cell can
         * only ever fill every second row, and the plot draws a blank
         * row between every filled one: horizontal white lines through
         * the scatter, on the station rather than in the renderer.
         * Measured on caster.centipede.fr/NEAR4, which is MSM4; a
         * station sending MSM7 at a sixteenth of a decibel showed none
         * of it, which is what made it look like an edition difference.
         *
         * A whole decibel is the coarsest any stream delivers, so at
         * this size every stream fills the rows it touches: 1/16 dB from
         * MSM6 and MSM7 and a quarter from the legacy messages simply
         * land in the same row. A cell is then about ten pixels by
         * seventeen on a handset -- and 205 kB, fixed however long the
         * run.
         */
        const val EL_STEP = 1.0f
        const val CN0_STEP = 1.0f
        /** 0..90 degrees at [EL_STEP]. */
        const val EL_BINS = 91
        /** 0..70 dB-Hz at [CN0_STEP]. */
        const val CN0_BINS = 71
    }
}

/**
 * C/N0 against elevation for the whole session.
 *
 * A healthy installation climbs smoothly from roughly 35 dB-Hz at the
 * horizon to about 50 at zenith. A flat or dented curve is the antenna,
 * its siting or an obstruction — not the receiver.
 */
@Composable
fun ElevationView(
    samples: ElevationAccumulator,
    revision: Int,
    modifier: Modifier = Modifier,
) {
    val faint = MaterialTheme.colorScheme.onSurfaceVariant
    val density = LocalDensity.current

    AnalysisBands(
        modifier = modifier,
        explainer = stringResource(R.string.elev_explain),
        summary = {
        Text(
            stringResource(R.string.elev_header, samples.total),
            style = MaterialTheme.typography.bodyMedium,
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
        )

        Text(
            stringResource(R.string.axis_cn0),
            style = MaterialTheme.typography.labelSmall,
            color = faint,
            modifier = Modifier.padding(start = 16.dp, top = 6.dp),
        )

    }, plot = { plotModifier ->
        Canvas(
            plotModifier
                .padding(start = 40.dp, end = 12.dp, top = 4.dp, bottom = 26.dp)
        ) {
            val lo = 20f
            val hi = 60f
            fun yFor(v: Float) = size.height * (1f - (v - lo) / (hi - lo))
            fun xFor(el: Float) = size.width * (el / 90f)

            val axis = android.graphics.Paint().apply {
                color = faint.toArgb()
                textSize = 10f * density.density
                isAntiAlias = true
            }
            for (v in 20..60 step 10) {
                val y = yFor(v.toFloat())
                drawLine(faint.copy(alpha = 0.2f), Offset(0f, y), Offset(size.width, y))
                drawContext.canvas.nativeCanvas.drawText(
                    "$v", -26f * density.density, y + 3f * density.density, axis)
            }
            for (el in 0..90 step 15) {
                val x = xFor(el.toFloat())
                drawLine(faint.copy(alpha = 0.15f), Offset(x, 0f), Offset(x, size.height))
                drawContext.canvas.nativeCanvas.drawText(
                    "$el", x, size.height + 12f * density.density, axis)
            }

            // Read once so the Canvas re-runs when the accumulator has
            // changed: the object itself is not observable, and drawing
            // from a mutable object nothing subscribes to is how a plot
            // silently stops updating.
            @Suppress("UNUSED_EXPRESSION") revision

            // Each cell is drawn as the area it stands for, so the
            // marks meet instead of leaving the grid visible. Density
            // rather than a heap of identical marks: a cell hit a
            // thousand times and one hit once looked the same before,
            // which flattered a stream that spends its life at one
            // elevation. Opacity is logarithmic, so a busy cell reads as
            // solid without a quiet one disappearing.
            val cellW = size.width * (ElevationAccumulator.EL_STEP / 90f)
            val cellH = size.height * (ElevationAccumulator.CN0_STEP / (hi - lo))
            samples.forEachCell { gnss, el, cn0, count ->
                if (cn0 < lo - ElevationAccumulator.CN0_STEP || cn0 > hi) {
                    return@forEachCell
                }
                val weight = (ln(count.toFloat() + 1f) / ln(64f)).coerceIn(0.15f, 1f)
                val top = yFor((cn0 + ElevationAccumulator.CN0_STEP).coerceAtMost(hi))
                drawRect(
                    color = Gnss.colour(gnss).copy(alpha = 0.25f + 0.55f * weight),
                    topLeft = Offset(xFor(el), top),
                    size = Size(cellW, cellH),
                )
            }
        }

    }, footer = {
        Text(
            stringResource(R.string.axis_elev),
            style = MaterialTheme.typography.labelSmall,
            color = faint,
            modifier = Modifier.align(Alignment.CenterHorizontally),
        )
    }, legend = {
        ConstellationLegend(
            samples.constellations,
            Modifier.padding(horizontal = 16.dp, vertical = 6.dp),
        )
    })
}

private fun sourceName(s: PositionSource): String = when (s) {
    PositionSource.PHONE_GNSS -> "phone GNSS"
    PositionSource.OBS_STREAM -> "the station's own stream"
    PositionSource.EPHEMERIS -> "ephemeris stream"
    PositionSource.RINEX -> "navigation file"
    PositionSource.NONE -> "no source"
}

/**
 * How healthy the orbits behind a sky plot are, as a colour.
 *
 * This was a chip in the corner of the analysis screen until P3.2. The
 * template has no slot for a chip, and the summary line already names
 * the source, so the judgement moved onto those words: the reader sees
 * *what* drew the plot and *whether to trust it* in one phrase, and can
 * tap it for the page that explains the difference.
 *
 * Order matters, and it took a handset to get it right.
 *
 * A working source outranks the disk: pro with a live ephemeris stream
 * is green even though a month-old file sits in its storage, because
 * the file has nothing to do with what is on screen.
 *
 * Below that, a *stale file* outranks the phone. Both are true at once
 * -- a file too old to place anything is exactly why the phone ended up
 * doing the work -- and of the two, the file is the one the user can
 * fix. Ordered the other way this state read amber "Phone GNSS" and the
 * red was unreachable: the only way to place nothing from a file is to
 * fall back to the phone.
 */
@Composable
internal fun orbitSourceTint(source: PositionSource, rinexAgeS: Double?): Color {
    val fresh = rinexAgeS != null && rinexAgeS <= Settings.RINEX_FRESH_S
    val staleFile = rinexAgeS != null && !fresh
    val green = Color(0xFF46AF5A)
    val amber = Color(0xFFE6A014)
    val red = Color(0xFFD72828)
    return when {
        source == PositionSource.EPHEMERIS -> green
        source == PositionSource.OBS_STREAM -> green
        source == PositionSource.RINEX && fresh -> green
        staleFile -> red
        source == PositionSource.PHONE_GNSS -> amber
        rinexAgeS != null -> green
        else -> MaterialTheme.colorScheme.onSurfaceVariant
    }
}

/**
 * What drew this plot, in words: the source, and how old it is where
 * that is knowable.
 */
@Composable
internal fun orbitSourcePhrase(source: PositionSource, rinexAgeS: Double?): String {
    val name = sourceName(source)
    // "now" is not an age -- "navigation file, now old" is what saying
    // it like one produces. A file young enough to be called current
    // says only what it is.
    return if (source == PositionSource.RINEX && rinexAgeS != null &&
               rinexAgeS >= 90.0)
        stringResource(R.string.sky_source_age, name, ageShort(rinexAgeS))
    else name
}

/** "3 h", "45 min" -- short enough for a badge. */
fun ageShort(seconds: Double): String = when {
    seconds < 90.0 -> "now"
    seconds < 5400.0 -> "${(seconds / 60).toInt()} min"
    else -> "${(seconds / 3600).toInt()} h"
}



