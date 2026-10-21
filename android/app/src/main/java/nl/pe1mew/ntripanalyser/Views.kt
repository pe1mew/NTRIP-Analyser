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

    val skyHelp = stringResource(R.string.sky_help).let {
        // The tracks sentence only where tracks are drawn: free never
        // describes a line it does not show.
        if (Features.HAS_TRACKS) it + "\n\n" + stringResource(R.string.sky_help_tracks)
        else it
    }
    AnalysisBands(
        modifier = modifier,
        topic = stringResource(R.string.sky_summary),
        helpTitle = stringResource(R.string.view_sky),
        help = skyHelp,
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
        // The reference the other polar plots match (R12): the shared
        // frame at its fixed size, the page scrolling when it must.
        polar = true,
        plot = { m -> SkyCanvas(sats, onSurface, faint, surface, density,
                                m, tracks, trackRevision) },
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
 * @param topic     the screen's one-line summary, beside its ⓘ
 *                  (`design/appGuiReview.md`, R1 and S2). Required: every
 *                  analysis screen has one, and a screen without one is
 *                  the drift this template exists to prevent.
 * @param helpTitle the help dialog's title -- the screen's own name.
 * @param help      what the ⓘ opens: the explanation that used to stand
 *                  on the page, paragraphs separated by a blank line.
 * @param summary   the numbers behind the plot.
 * @param footer    what the plot is of -- the station and its ARP on
 *                  the sky view, nothing on the other two.
 * @param legend    the colours, last, nearest the plot they describe.
 */
@Composable
internal fun AnalysisBands(
    topic: String,
    helpTitle: String,
    help: String,
    summary: @Composable ColumnScope.() -> Unit,
    plot: @Composable (Modifier) -> Unit,
    modifier: Modifier = Modifier,
    footer: @Composable ColumnScope.() -> Unit = {},
    legend: @Composable ColumnScope.() -> Unit = {},
    /**
     * A polar plot: drawn at [PolarCanvas]'s fixed size, with the page
     * scrolling, rather than in whatever height the words leave -- the
     * rule that makes every polar plot the same size (R12).
     */
    polar: Boolean = false,
) {
    if (polar) {
        Column(modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
            SummaryRow(topic, helpTitle, help)
            summary()
            plot(Modifier.fillMaxWidth())
            footer()
            legend()
        }
        return
    }
    PlotLayout(
        modifier = modifier,
        above = {
            SummaryRow(topic, helpTitle, help)
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
        // Elevation rings, in the shared polar frame (R12), which draws
        // the grid, N E S W and the ring numbers: the radius is linear in
        // elevation, as the desktop draws it, 90 deg at the centre and 0
        // at the rim; every ring but the horizon carries its number, with
        // its degree sign -- the numbers are an axis and carry their unit.
        PolarCanvas(
            rings = (0..75 step 15).map { el ->
                PolarRing((90f - el) / 90f, if (el == 0) null else "$el°")
            },
            modifier = modifier,
        ) { cx, cy, radius ->
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

            // The ring numbers come last, from the frame, so a satellite
            // that happens to sit on one never hides it.
        }
}

/**
 * A ring of a polar plot: its radius as a fraction of the frame's, and
 * its label -- null leaves it unlabelled, as the sky view's horizon is.
 */
internal data class PolarRing(val fraction: Float, val label: String?)

/**
 * The frame every polar plot is drawn in (design/appGuiReview.md, R12):
 * the sky view's geometry, written once. A square at the width of the
 * screen, 12 dp in; a circle 18 dp inside that, which is the room the
 * cardinal letters need outside it; the rings, the dashed axes, N E S W;
 * and each ring's label where it crosses the north line, just right of
 * the line, on a patch of the page so a track or a dot under it cannot
 * make it unreadable (R6). The plot draws its own content in [content],
 * between the grid and the labels.
 *
 * The size is a rule, not what the page leaves over: three plots under
 * three different headers were three different sizes for exactly that
 * reason. A page whose words and plot do not fit scrolls.
 */
@Composable
internal fun PolarCanvas(
    rings: List<PolarRing>,
    modifier: Modifier = Modifier,
    content: DrawScope.(cx: Float, cy: Float, radius: Float) -> Unit,
) {
    val onSurface = MaterialTheme.colorScheme.onSurface
    val faint = MaterialTheme.colorScheme.onSurfaceVariant
    val page = MaterialTheme.colorScheme.surface
    val density = LocalDensity.current
    Canvas(modifier.fillMaxWidth().aspectRatio(1f).padding(POLAR_PAD)) {
        val cx = size.width / 2f
        val cy = size.height / 2f
        val radius = min(cx, cy) - POLAR_MARGIN.toPx()
        if (radius < 20f) return@Canvas

        for (ring in rings) {
            drawCircle(
                color = faint.copy(alpha = if (ring.fraction >= 0.999f) 0.6f else 0.25f),
                radius = radius * ring.fraction, center = Offset(cx, cy),
                style = Stroke(width = 1f),
            )
        }
        val dash = PathEffect.dashPathEffect(floatArrayOf(6f, 8f))
        drawLine(faint.copy(alpha = 0.4f), Offset(cx - radius, cy),
                 Offset(cx + radius, cy), pathEffect = dash)
        drawLine(faint.copy(alpha = 0.4f), Offset(cx, cy - radius),
                 Offset(cx, cy + radius), pathEffect = dash)
        drawCardinals(cx, cy, radius, onSurface, density.density)

        content(cx, cy, radius)

        // Last, so no mark of the plot can cover the scale.
        drawRingLabels(cx, cy, radius, rings, faint, page, density.density)
    }
}

/** The frame's padding and the room it leaves for N E S W: the sky view's. */
internal val POLAR_PAD = 12.dp
internal val POLAR_MARGIN = 18.dp

/**
 * Each ring's label where it crosses the north line, just right of the
 * line and just inside the ring, on a patch of the page (R6).
 */
private fun DrawScope.drawRingLabels(
    cx: Float, cy: Float, radius: Float, rings: List<PolarRing>,
    colour: Color, page: Color, density: Float,
) {
    val paint = android.graphics.Paint().apply {
        color = colour.toArgb()
        textSize = 10f * density
        isAntiAlias = true
    }
    val patch = android.graphics.Paint().apply {
        color = page.copy(alpha = 0.85f).toArgb()
    }
    val fm = paint.fontMetrics
    drawContext.canvas.nativeCanvas.apply {
        for (ring in rings) {
            val text = ring.label ?: continue
            val x = cx + 4f * density
            val y = cy - radius * ring.fraction + 4f * density - fm.ascent
            drawRect(x - 2f * density, y + fm.ascent - 1f * density,
                     x + paint.measureText(text) + 2f * density,
                     y + fm.descent + 1f * density, patch)
            drawText(text, x, y, paint)
        }
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
        topic = stringResource(
            if (liveValues) R.string.bars_live else R.string.bars_mean
        ),
        helpTitle = stringResource(R.string.view_bars),
        help = stringResource(R.string.bars_help),
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
 * The hand-over view: the analysis screens’ template, filled with
 * the reference position’s story. Pro's last analysis tab since
 * 2026-10-12; before that, the detail screen behind a hub card.
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
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun HandoverDetail(
    stats: Stats,
    service: ServiceDoc?,
    gga: GgaDoc?,
    ggaIsPhone: Boolean,
    roverLat: Double,
    roverLon: Double,
    mountpoint: String,
    roverIsFix: Boolean,
    worstJumpM: Double?,
    /**
     * A3's verdict from the network-RTK check, which the distance is
     * coloured with: the core's judgement of the same distance, so the
     * screen and the check's card cannot disagree. Null or PENDING draws
     * it neutral (design/appGuiReview.md, R11.6).
     */
    a3: Verdict? = null,
) {
    val sum = hoSummary(stats, service, roverLat, roverLon)
    val trail = MonitorService.arpTrail
    // Read so a dot recorded between documents still redraws us; the
    // 1 Hz document is the usual trigger.
    @Suppress("UNUSED_EXPRESSION") trail.revision

    val faint = MaterialTheme.colorScheme.onSurfaceVariant

    AnalysisBands(
        topic = stringResource(R.string.ho_explainer),
        helpTitle = stringResource(R.string.ho_title),
        help = stringResource(R.string.ho_help),
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
                // A3 judges the distance from the position *sent to the
                // caster*; this line is the distance from the rover drawn
                // here. They are one distance when the phone's fix is what
                // is sent, or when the rover is the set position that is
                // sent -- and only then does A3's colour apply. Otherwise
                // neutral: a desk with a set position near the station
                // showed a 155,7 km phone distance in A3's green
                // (design/appGuiReview.md, R11.6, author 2026-10-21).
                color = if (a3 == null || a3 == Verdict.PENDING ||
                            (roverIsFix && !ggaIsPhone))
                            MaterialTheme.colorScheme.onSurface
                        else verdictColour(a3),
                fontWeight = FontWeight.Medium,
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
            )
            val moves = stats.arpMoves
            Text(
                when {
                    moves == 0 -> stringResource(R.string.ho_stable)
                    sum.isNetwork -> stringResource(R.string.ho_moves_net, moves)
                    sum.isFixed -> stringResource(R.string.ho_moves_fixed, moves)
                    else -> stringResource(R.string.ho_moves_unknown, moves)
                },
                style = MaterialTheme.typography.bodySmall,
                // Red only where the core says a fixed base moved.
                color = if (moves > 0 && sum.isFixed)
                    MaterialTheme.colorScheme.error else faint,
                modifier = Modifier.padding(horizontal = 16.dp),
            )
            // Which it is, and the evidence: the core's words (H9).
            service?.let { s ->
                Text(
                    stringResource(when (s.kind) {
                        "network" -> R.string.ho_service_net
                        "fixed" -> R.string.ho_service_fixed
                        else -> R.string.ho_service_unknown
                    }, s.why),
                    style = MaterialTheme.typography.bodySmall,
                    color = faint,
                    modifier = Modifier.padding(horizontal = 16.dp),
                )
            }
            // What the caster has been told. A hand-over that happens
            // when the screen comes back on is the phone catching up, not
            // the station changing -- said here rather than left for the
            // user to work out (H9; the boundary itself is unchanged).
            Text(
                when {
                    gga == null -> stringResource(R.string.ho_gga_none)
                    !ggaIsPhone -> stringResource(R.string.ho_gga_set,
                        "%.5f, %.5f".format(gga.lat, gga.lon))
                    gga.unchangedS < 60.0 -> stringResource(R.string.ho_gga_live_now)
                    else -> stringResource(R.string.ho_gga_live, dur(gga.unchangedS))
                },
                style = MaterialTheme.typography.bodySmall,
                color = faint,
                modifier = Modifier.padding(horizontal = 16.dp),
            )
            // Carried over from the hub card this view replaced.
            if (moves > 0 && worstJumpM != null) {
                Text(
                    stringResource(R.string.ho_jump, distanceText(worstJumpM)),
                    style = MaterialTheme.typography.bodySmall,
                    color = faint,
                    modifier = Modifier.padding(horizontal = 16.dp),
                )
            }
        },
        // The shared polar frame at its fixed size, the strip under it at
        // a height of its own, the page scrolling to reach it (R5, R12).
        polar = true,
        plot = { m ->
            Column(m) {
                HandoverPolar(trail, stats, roverLat, roverLon, roverIsFix)
                DistanceStrip(
                    trail,
                    Modifier.padding(horizontal = 12.dp).fillMaxWidth()
                        .height(110.dp),
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
            // The sky view's legend, in its dress (R12): a swatch and a
            // name per entry, wrapping between entries.
            FlowRow(
                Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
                horizontalArrangement = Arrangement.spacedBy(10.dp),
                verticalArrangement = Arrangement.spacedBy(4.dp),
            ) {
                LegendDot(HO_ROVER, stringResource(R.string.ho_legend_rover))
                LegendLine(HO_ARP, stringResource(R.string.ho_legend_arp))
                LegendDot(HO_STATIONS.first(), stringResource(R.string.ho_legend_hist))
            }
        },
    )
}

/** The desktop VRS monitor's rover blue and in-use red, kept so screenshots agree. */
private val HO_ROVER = Color(0xFF1E50C8)
private val HO_ARP   = Color(0xFFD22828)

/**
 * One colour per station, given on first sight and kept for the run
 * (design/appGuiReview.md, R8): no red, which is the baseline to the
 * station in use (R7), and no rover blue. Seven, reused in order when a
 * drive passes more stations than that -- the letter beside each dot
 * still tells them apart.
 */
internal val HO_STATIONS = listOf(
    Color(0xFF2E7D32),   // green
    Color(0xFFEF6C00),   // orange
    Color(0xFF6A1B9A),   // purple
    Color(0xFF00838F),   // teal
    Color(0xFF6D4C41),   // brown
    Color(0xFF9E9D24),   // olive
    Color(0xFF546E7A),   // slate
)

private fun stationColour(index: Int) = HO_STATIONS[index % HO_STATIONS.size]

/** A, B, …, Z, then A2, B2, …: a station's letter from its index (R9). */
internal fun stationLetter(index: Int): String {
    val l = ('A' + index % 26).toString()
    return if (index < 26) l else l + (index / 26 + 1)
}

/** A legend entry in the sky view's dress (R12): swatch, then name. */
@Composable
private fun LegendDot(colour: Color, label: String) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Canvas(Modifier.size(10.dp)) { drawCircle(colour) }
        Spacer(Modifier.width(4.dp))
        Text(label, style = MaterialTheme.typography.labelSmall, maxLines = 1)
    }
}

/** A legend entry for a line rather than a mark: the baseline in use. */
@Composable
private fun LegendLine(colour: Color, label: String) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Canvas(Modifier.size(width = 16.dp, height = 10.dp)) {
            drawLine(colour, Offset(0f, size.height / 2f),
                     Offset(size.width, size.height / 2f), strokeWidth = 2.5f)
        }
        Spacer(Modifier.width(4.dp))
        Text(label, style = MaterialTheme.typography.labelSmall, maxLines = 1)
    }
}

/**
 * Rover centred; everything else at true bearing, radius to scale, in
 * the shared polar frame (R12).
 *
 * The station in use is joined to the rover by a red line, the baseline
 * the corrections come over (R7); earlier stations are not joined to
 * anything. Each station has its own colour and letter (R8, R9), and the
 * rover's track runs through the run in the colour of the station that
 * served each stretch of it -- no track where the rover is a position set
 * by hand, which does not move.
 */
@Composable
private fun HandoverPolar(
    trail: ArpTrail,
    stats: Stats,
    roverLat: Double,
    roverLon: Double,
    roverIsFix: Boolean,
) {
    val track = if (roverIsFix) trail.track() else emptyList()
    val scaleM = polarScaleM(trail, stats, roverLat, roverLon, track)
    val onSurface = MaterialTheme.colorScheme.onSurface
    val density = LocalDensity.current
    val stations = trail.stations()
    val current = trail.current

    PolarCanvas(
        rings = listOf(
            PolarRing(0.5f, distanceText(scaleM / 2.0)),
            PolarRing(1f, distanceText(scaleM)),
        ),
    ) { cx, cy, radius ->
        fun place(lat: Double, lon: Double): Offset {
            val d = geoDistanceM(roverLat, roverLon, lat, lon)
            val b = geoBearingDeg(roverLat, roverLon, lat, lon) *
                Math.PI / 180.0
            val r = (d / scaleM).toFloat().coerceAtMost(1f) * radius
            return Offset(cx + r * kotlin.math.sin(b).toFloat(),
                          cy - r * kotlin.math.cos(b).toFloat())
        }

        // The rover's track, oldest first, each step in the colour of
        // the station that was serving the rover when it got there.
        // Thinned to what the screen can show: a point within
        // TRACK_DRAW_PX of the last one drawn adds nothing visible, and
        // drawing every point cost 266 ms a frame at the 50 000-point
        // cap on the Huawei (design/appGuiReview.md, step 4). A change
        // of station and the newest point are always drawn, so each
        // colour starts and ends where it did.
        val skipM = scaleM / radius * TRACK_DRAW_PX
        val flat = FlatMetres(roverLat)
        var prev: Offset? = null
        var kept: ArpTrail.RoverPoint? = null
        for ((i, p) in track.withIndex()) {
            val k = kept
            if (k != null && k.station == p.station && i != track.lastIndex &&
                flat.dist2(k.lat, k.lon, p.lat, p.lon) < skipM * skipM) continue
            val here = place(p.lat, p.lon)
            prev?.let { drawLine(stationColour(p.station), it, here,
                                 strokeWidth = 3f) }
            prev = here
            kept = p
        }

        // The baseline to the station in use, under the dots it joins.
        val inUse = stations.firstOrNull { it.index == current }
        if (inUse != null)
            drawLine(HO_ARP, place(inUse.lat, inUse.lon), Offset(cx, cy),
                     strokeWidth = 3f)

        // The stations, unjoined, each in its own colour with its letter;
        // the one in use larger and ringed, on top of the others.
        val markR = with(density) { 6.dp.toPx() }
        val labelPaint = android.graphics.Paint().apply {
            color = onSurface.toArgb()
            textSize = with(density) { 11.sp.toPx() }
            isAntiAlias = true
            isFakeBoldText = true
        }
        for (s in stations.sortedBy { it.index == current }) {
            val at = place(s.lat, s.lon)
            val big = s.index == current
            val r = if (big) markR * 1.5f else markR
            drawCircle(stationColour(s.index), r, at)
            drawCircle(Color.White, r, at, style = Stroke(if (big) 3f else 1.5f))
            drawContext.canvas.nativeCanvas.drawText(
                stationLetter(s.index), at.x + r + 3f, at.y + r / 2f, labelPaint)
        }

        // The rover, centre of its own world.
        drawCircle(HO_ROVER, markR * 1.5f, Offset(cx, cy))
        drawCircle(Color.White, markR * 1.5f, Offset(cx, cy), style = Stroke(2f))
    }
}

/** How many metres the outer ring stands for: the furthest thing, padded. */
private fun polarScaleM(
    trail: ArpTrail, stats: Stats, roverLat: Double, roverLon: Double,
    track: List<ArpTrail.RoverPoint>,
): Double {
    var worst = 100.0                       // floor: a 2 m ARP still draws
    val alat = stats.arpLat
    val alon = stats.arpLon
    if (stats.arpValid && alat != null && alon != null)
        worst = maxOf(worst, geoDistanceM(roverLat, roverLon, alat, alon))
    for (d in trail.dots())
        worst = maxOf(worst, geoDistanceM(roverLat, roverLon, d.lat, d.lon))
    // And the rover's track as drawn, which can reach further than any
    // station on a long drive. The furthest point is found on the flat
    // approximation and only it measured on the sphere: one trig call
    // instead of one per point, of which there may be 50 000.
    val flat = FlatMetres(roverLat)
    var far: ArpTrail.RoverPoint? = null
    var farD2 = -1.0
    for (p in track) {
        val d2 = flat.dist2(roverLat, roverLon, p.lat, p.lon)
        if (d2 > farD2) { farD2 = d2; far = p }
    }
    far?.let { worst = maxOf(worst, geoDistanceM(roverLat, roverLon, it.lat, it.lon)) }
    return worst * 1.2
}

/**
 * Squared distances on a plane tangent at one latitude: no trig per
 * call. Good enough to compare points or to skip ones a pixel apart,
 * never to label a distance -- [geoDistanceM] does that.
 */
private class FlatMetres(lat0: Double) {
    private val mPerDegLat = 6371000.0 * Math.PI / 180.0
    private val mPerDegLon = mPerDegLat * kotlin.math.cos(lat0 * Math.PI / 180.0)

    fun dist2(lat1: Double, lon1: Double, lat2: Double, lon2: Double): Double {
        var dlo = lon2 - lon1
        if (dlo > 180.0) dlo -= 360.0 else if (dlo < -180.0) dlo += 360.0
        val y = (lat2 - lat1) * mPerDegLat
        val x = dlo * mPerDegLon
        return x * x + y * y
    }
}

/** Track points closer than this on screen are drawn as one. */
private const val TRACK_DRAW_PX = 1.5f

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
        val style = labelStyle.copy(color = faint)
        val title = measurer.measure(label, style)
        drawText(title, topLeft = Offset(6f, 2f))
        if (km.size < 2) return@Canvas

        var maxKm = 0.001f
        for (v in km) if (!v.isNaN() && v > maxKm) maxKm = v

        // A scale, so the line can be read: the top of the plot is the
        // largest distance in view, the bottom zero. Without it a
        // tester's 750 km to 47 km step showed as a shape with no size
        // (2026-10-14). Title and scale share a band of their own above
        // the line, so neither sits on it.
        val top = measurer.measure("max " + distanceText(maxKm * 1000.0), style)
        drawText(top, topLeft = Offset(size.width - top.size.width - 6f, 2f))
        val bottom = measurer.measure("0", style)
        drawText(bottom, topLeft = Offset(size.width - bottom.size.width - 6f,
                                          size.height - bottom.size.height - 2f))
        val plotTop = title.size.height + 6f
        val plotH = size.height - plotTop - 2f

        val stepX = size.width / (ArpTrail.RING_N - 1).toFloat()
        var prev: Offset? = null
        km.forEachIndexed { i, v ->
            if (v.isNaN()) { prev = null; return@forEachIndexed }
            val x = size.width - (km.size - 1 - i) * stepX
            val y = plotTop + plotH * (1f - v / maxKm)
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

    /** A visit to a station, in the order the run made them. */
    data class Dot(val lat: Double, val lon: Double, val station: Int = 0)

    /** A distinct reference position: its index is its letter and colour. */
    data class Station(val lat: Double, val lon: Double, val index: Int)

    /** Where the rover was, and which station it was being served by. */
    data class RoverPoint(val lat: Double, val lon: Double, val station: Int)

    private val dots = ArrayList<Dot>()
    private val stations = ArrayList<Station>()
    private val track = ArrayList<RoverPoint>()

    /** km, NaN where no sample; a ring, newest at [head]-1. */
    private val ring = FloatArray(RING_N) { Float.NaN }
    private var head = 0
    private var ringCount = 0

    /** Bumped on any recording, because Compose cannot see inside. */
    var revision: Int = 0
        private set

    /**
     * Note a broadcast reference position; records it if it moved.
     *
     * Each distinct position is a station, kept with the index it got on
     * first sight: its letter and its colour on the plot come from that
     * index, so neither changes at a hand-over -- including a hand-back
     * to a station seen before (design/appGuiReview.md, R8, R9). The
     * visits themselves are kept in order, for the hand-over count and
     * the largest jump.
     */
    @Synchronized
    fun offerDot(lat: Double, lon: Double) {
        val last = dots.lastOrNull()
        if (last != null &&
            geoDistanceM(last.lat, last.lon, lat, lon) <= MOVE_M) return
        val known = stations.firstOrNull {
            geoDistanceM(it.lat, it.lon, lat, lon) <= MOVE_M
        }
        val station = known ?: Station(lat, lon, stations.size).also {
            if (stations.size < DOT_CAP) stations.add(it)
        }
        if (dots.size >= DOT_CAP) dots.removeAt(0)
        dots.add(Dot(lat, lon, station.index))
        revision++
    }

    /**
     * Note where the rover is, while the phone has a fix. A point only
     * once it has moved [TRACK_STEP_M], so a parked phone adds nothing;
     * tagged with the station serving it then, whose colour its stretch
     * of track takes (R8). The whole run is kept: at the cap the track is
     * thinned to every other point rather than cut, so its shape stays.
     */
    @Synchronized
    fun offerRover(lat: Double, lon: Double) {
        val station = dots.lastOrNull()?.station ?: return
        val last = track.lastOrNull()
        if (last != null &&
            geoDistanceM(last.lat, last.lon, lat, lon) < TRACK_STEP_M) return
        if (track.size >= TRACK_CAP) {
            val kept = track.filterIndexed { i, _ -> i % 2 == 0 }
            track.clear()
            track.addAll(kept)
        }
        track.add(RoverPoint(lat, lon, station))
        revision++
    }

    @Synchronized
    fun stations(): List<Station> = ArrayList(stations)

    @Synchronized
    fun track(): List<RoverPoint> = ArrayList(track)

    /** The station in use: the last one visited, or null before any. */
    val current: Int? @Synchronized get() = dots.lastOrNull()?.station

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
        stations.clear()
        track.clear()
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

        /** The rover moves this far before its track gains a point. */
        const val TRACK_STEP_M = 10.0

        /**
         * The track's safety net, not its window: 12 h at motorway speed,
         * one point a second, is about 43 000 points, so a whole day's
         * drive thins once or twice.
         */
        const val TRACK_CAP = 50_000
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
        topic = stringResource(R.string.elev_explain),
        helpTitle = stringResource(R.string.view_elev),
        help = stringResource(R.string.elev_help),
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



