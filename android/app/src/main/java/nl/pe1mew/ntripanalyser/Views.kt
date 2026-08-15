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
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
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
    modifier: Modifier = Modifier,
) {
    val onSurface = MaterialTheme.colorScheme.onSurface
    val faint = MaterialTheme.colorScheme.onSurfaceVariant
    val surface = MaterialTheme.colorScheme.surface
    val density = LocalDensity.current

    Column(modifier.fillMaxSize()) {
        Text(
            stringResource(
                R.string.sky_header, sats.size, sats.size + missing,
                sourceName(source),
            ),
            style = MaterialTheme.typography.bodyMedium,
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
        )
        if (missing > 0) {
            Text(
                stringResource(R.string.sky_missing, missing, sourceName(source)),
                style = MaterialTheme.typography.bodySmall,
                color = faint,
                modifier = Modifier.padding(horizontal = 16.dp),
            )
        }

        ConstellationLegend(
            sats.map { it.gnss }.distinct().sorted(),
            Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
        )

        Canvas(
            Modifier
                .weight(1f)
                .fillMaxWidth()
                .padding(12.dp)
        ) {
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

            val markerR = with(density) { 7.dp.toPx() }
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
                        textSize = with(density) { 11.sp.toPx() }
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

        Text(
            footer,
            style = MaterialTheme.typography.bodySmall,
            color = faint,
            fontFamily = FontFamily.Monospace,
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 6.dp),
        )
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

    Column(modifier.fillMaxSize()) {
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
            stringResource(
                if (liveValues) R.string.bars_live else R.string.bars_mean
            ),
            style = MaterialTheme.typography.bodySmall,
            color = faint,
            modifier = Modifier.padding(horizontal = 16.dp),
        )

        Text(
            stringResource(R.string.axis_cn0),
            style = MaterialTheme.typography.labelSmall,
            color = faint,
            modifier = Modifier.padding(start = 16.dp, top = 6.dp),
        )

        Canvas(
            Modifier
                .weight(1f)
                .fillMaxWidth()
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

        Text(
            stringResource(R.string.axis_sat),
            style = MaterialTheme.typography.labelSmall,
            color = faint,
            modifier = Modifier.align(Alignment.CenterHorizontally),
        )

        ConstellationLegend(
            shown.map { it.gnss }.distinct().sorted(),
            Modifier.padding(horizontal = 16.dp, vertical = 6.dp),
        )
    }
}

// ── 3. C/N0 versus elevation ─────────────────────────────────────────

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

    /** Constellations that have contributed, for the legend. */
    val constellations: List<Int> get() = seen.sorted()

    fun add(gnss: Int, elevationDeg: Float, cn0: Float) {
        if (gnss !in 1 until GNSS_SLOTS) return
        val el = (elevationDeg / EL_STEP).toInt().coerceIn(0, EL_BINS - 1)
        val cn = (cn0 / CN0_STEP).toInt().coerceIn(0, CN0_BINS - 1)
        cells[(gnss * EL_BINS + el) * CN0_BINS + cn]++
        total++
        seen.add(gnss)
    }

    fun clear() {
        cells.fill(0)
        total = 0L
        seen.clear()
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

    Column(modifier.fillMaxSize()) {
        Text(
            stringResource(R.string.elev_header, samples.total),
            style = MaterialTheme.typography.bodyMedium,
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
        )
        Text(
            stringResource(R.string.elev_explain),
            style = MaterialTheme.typography.bodySmall,
            color = faint,
            modifier = Modifier.padding(horizontal = 16.dp),
        )

        Text(
            stringResource(R.string.axis_cn0),
            style = MaterialTheme.typography.labelSmall,
            color = faint,
            modifier = Modifier.padding(start = 16.dp, top = 6.dp),
        )

        Canvas(
            Modifier
                .weight(1f)
                .fillMaxWidth()
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

        Text(
            stringResource(R.string.axis_elev),
            style = MaterialTheme.typography.labelSmall,
            color = faint,
            modifier = Modifier.align(Alignment.CenterHorizontally),
        )

        ConstellationLegend(
            samples.constellations,
            Modifier.padding(horizontal = 16.dp, vertical = 6.dp),
        )
    }
}

private fun sourceName(s: PositionSource): String = when (s) {
    PositionSource.PHONE_GNSS -> "phone GNSS"
    PositionSource.OBS_STREAM -> "the station's own stream"
    PositionSource.EPHEMERIS -> "ephemeris stream"
    PositionSource.RINEX -> "navigation file"
    PositionSource.NONE -> "no source"
}

/**
 * Where the sky view's positions are coming from, in the corner of the
 * Analysis screen.
 *
 * Both editions, one implementation, differing only in what they can
 * reach: free never has [PositionSource.EPHEMERIS], so that arm is dead
 * code there rather than a second version of this badge.
 *
 * The colour answers "can I trust what I am looking at?" before the
 * words are read:
 *
 * - **green** — a real orbit source: the station's own broadcast, an
 *   ephemeris stream, or a navigation file still inside the four-hour
 *   window that `sv_eph_is_valid_at()` enforces.
 * - **amber** — the phone's own GNSS. It draws a sky, but it is *this
 *   handset's* sky, not the station's; near the base they agree closely
 *   and at distance they do not.
 * - **red** — a navigation file too old to place anything. This is the
 *   state that used to be invisible: the file loads, the count is large,
 *   and every record is outside the window.
 * - **white** — nothing imported and nothing broadcast, so there is
 *   nothing to be confident or worried about yet.
 *
 * Tapping opens the wiki page that explains what orbits are for and
 * where to get a file, because a badge that says something is wrong owes
 * the reader the fix.
 */
@Composable
fun OrbitSourceBadge(
    source: PositionSource,
    rinexAgeS: Double?,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val fresh = rinexAgeS != null && rinexAgeS <= Settings.RINEX_FRESH_S
    val green = Color(0xFF46AF5A)
    val amber = Color(0xFFE6A014)
    val red = Color(0xFFD72828)

    // Order matters, and it took a handset to get it right.
    //
    // A working source outranks the disk: pro with a live ephemeris
    // stream is green even though a month-old file sits in its storage,
    // because the file has nothing to do with what is on screen.
    //
    // Below that, a *stale file* outranks the phone. Both are true at
    // once -- a file too old to place anything is exactly why the phone
    // ended up doing the work -- and of the two, the file is the one the
    // user can fix. Ordered the other way this state read amber "Phone
    // GNSS" and the red was unreachable: the only way to place nothing
    // from a file is to fall back to the phone.
    val staleFile = rinexAgeS != null && !fresh
    val (label, colour) = when {
        source == PositionSource.EPHEMERIS ->
            stringResource(R.string.badge_eph_stream) to green
        source == PositionSource.OBS_STREAM ->
            stringResource(R.string.badge_station) to green
        source == PositionSource.RINEX && fresh ->
            stringResource(R.string.badge_rinex, ageShort(rinexAgeS!!)) to green
        staleFile ->
            stringResource(R.string.badge_rinex, ageShort(rinexAgeS!!)) to red
        source == PositionSource.PHONE_GNSS ->
            stringResource(R.string.badge_phone) to amber
        rinexAgeS != null ->
            stringResource(R.string.badge_rinex, ageShort(rinexAgeS)) to green
        else ->
            stringResource(R.string.badge_none) to Color.White
    }

    // Same chip as every verdict in this app: 6 dp corners, bold
    // monospace, white on the colour, and no outline. A border made it a
    // different kind of object from the PASS/WARN chips it sits above,
    // and drew the eye to its edge rather than to what it says.
    //
    // The one state that cannot take white text is the white chip, which
    // is by design the quiet one: nothing is imported and nothing is
    // wrong yet.
    val ink = if (colour == Color.White)
        MaterialTheme.colorScheme.onSurfaceVariant else Color.White

    Surface(
        color = colour,
        shape = RoundedCornerShape(6.dp),
        modifier = modifier
            .padding(end = 12.dp)
            .clickable(onClick = onClick)
            // The tap target is the chip, not the glyphs: the label stays
            // at the size the rest of the app uses and the padding does
            // the reaching.
            .semantics { contentDescription = label },
    ) {
        Text(
            label,
            color = ink,
            fontSize = 11.sp,
            fontWeight = FontWeight.Bold,
            fontFamily = FontFamily.Monospace,
            modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp),
        )
    }
}

/** "3 h", "45 min" -- short enough for a badge. */
fun ageShort(seconds: Double): String = when {
    seconds < 90.0 -> "now"
    seconds < 5400.0 -> "${(seconds / 60).toInt()} min"
    else -> "${(seconds / 3600).toInt()} h"
}



