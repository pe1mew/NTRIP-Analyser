package nl.pe1mew.ntripanalyser

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.geometry.Offset
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
            drawElevationNumbers(cx, cy, radius, faint, density.density)

            for (s in sats) {
                val (x, y) = polar(cx, cy, radius, s.azimuthDeg, s.elevationDeg)
                val c = Gnss.colour(s.gnss)
                val markerR = with(density) { 7.dp.toPx() }

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

/** Azimuth clockwise from north, radius linear in elevation. */
private fun polar(
    cx: Float, cy: Float, radius: Float, azDeg: Float, elDeg: Float,
): Pair<Float, Float> {
    val r = radius * (90f - elDeg.coerceIn(0f, 90f)) / 90f
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
    cx: Float, cy: Float, radius: Float, colour: Color, density: Float,
) {
    val paint = android.graphics.Paint().apply {
        color = colour.toArgb()
        textSize = 10f * density
        isAntiAlias = true
    }
    drawContext.canvas.nativeCanvas.apply {
        for (el in 15..75 step 15) {
            val r = radius * (90f - el) / 90f
            drawText("$el", cx + 4f * density, cy - r + 4f * density, paint)
        }
    }
}

@Composable
private fun ConstellationLegend(ids: List<Int>, modifier: Modifier = Modifier) {
    Row(modifier, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
        ids.forEach { id ->
            Row(verticalAlignment = Alignment.CenterVertically) {
                Canvas(Modifier.size(10.dp)) { drawCircle(Gnss.colour(id)) }
                Spacer(Modifier.width(4.dp))
                Text(Gnss.label(id), style = MaterialTheme.typography.labelSmall)
            }
        }
    }
}

// ── 2. C/N0 per satellite ────────────────────────────────────────────

/**
 * A bar per satellite, coloured by constellation.
 *
 * @param liveValues true when the bars show this epoch (pro), false when
 *                   they show the session mean over the capture (free).
 */
@Composable
fun SignalBars(
    sats: List<PlottedSat>,
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

        Canvas(
            Modifier
                .weight(1f)
                .fillMaxWidth()
                .padding(start = 34.dp, end = 12.dp, top = 12.dp, bottom = 26.dp)
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

        ConstellationLegend(
            shown.map { it.gnss }.distinct().sorted(),
            Modifier.padding(horizontal = 16.dp, vertical = 6.dp),
        )
    }
}

// ── 3. C/N0 versus elevation ─────────────────────────────────────────

/** One accumulated sample: the antenna diagnostic's raw material. */
data class ElevationSample(val gnss: Int, val elevationDeg: Float, val cn0: Float)

/**
 * C/N0 against elevation for the whole session.
 *
 * A healthy installation climbs smoothly from roughly 35 dB-Hz at the
 * horizon to about 50 at zenith. A flat or dented curve is the antenna,
 * its siting or an obstruction — not the receiver.
 */
@Composable
fun ElevationView(samples: List<ElevationSample>, modifier: Modifier = Modifier) {
    val faint = MaterialTheme.colorScheme.onSurfaceVariant
    val density = LocalDensity.current

    Column(modifier.fillMaxSize()) {
        Text(
            stringResource(R.string.elev_header, samples.size),
            style = MaterialTheme.typography.bodyMedium,
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
        )
        Text(
            stringResource(R.string.elev_explain),
            style = MaterialTheme.typography.bodySmall,
            color = faint,
            modifier = Modifier.padding(horizontal = 16.dp),
        )

        Canvas(
            Modifier
                .weight(1f)
                .fillMaxWidth()
                .padding(start = 34.dp, end = 12.dp, top = 12.dp, bottom = 26.dp)
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

            val r = max(1.2f, 1.6f * density.density)
            samples.forEach { s ->
                drawCircle(
                    Gnss.colour(s.gnss).copy(alpha = 0.45f), r,
                    Offset(xFor(s.elevationDeg), yFor(s.cn0.coerceIn(lo, hi))),
                )
            }
        }

        ConstellationLegend(
            samples.map { it.gnss }.distinct().sorted(),
            Modifier.padding(horizontal = 16.dp, vertical = 6.dp),
        )
    }
}

private fun sourceName(s: PositionSource): String = when (s) {
    PositionSource.PHONE_GNSS -> "phone GNSS"
    PositionSource.EPHEMERIS -> "ephemeris stream"
    PositionSource.RINEX -> "RINEX"
    PositionSource.NONE -> "no source"
}



