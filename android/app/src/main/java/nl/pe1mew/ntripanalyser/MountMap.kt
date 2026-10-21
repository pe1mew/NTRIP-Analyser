/**
 * @file MountMap.kt
 * @brief Every mountpoint of a caster on a map: zoom, pan, tap one.
 *
 * The author's shape for it (mountpoint-map.md, D3 and D4, 2026-10-13):
 * full screen, zoomed and panned, closed by Back; tapping a mountpoint
 * shows what it is in a balloon; pro selects it from there, free is told
 * what pro adds. Both editions see the map -- the information is free,
 * the workflow is paid, exactly as with the sourcetable list.
 *
 * Under it, Natural Earth's outlines from the app's own asset: nothing
 * is fetched but the sourcetable, which the list fetches too.
 *
 * What is drawn about a mountpoint is the core's: its kind and the
 * reason, whether its position is usable, its distance and bearing
 * from the centre (`bridge_sourcetable_to_json`, M6a). The projection
 * is the one in which those land exactly where the outlines put them
 * ([Aeqd]), so this file only places and paints.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
package nl.pe1mew.ntripanalyser

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.material3.Card
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.drawscope.CanvasDrawScope
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.runtime.snapshotFlow
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.collectLatest
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.withTransform
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlin.math.PI
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.floor
import kotlin.math.hypot
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow
import kotlin.math.roundToInt
import kotlin.math.sin
import kotlin.math.sqrt

/** The GUI's map colours (`gui/gui_mountmap.c`), so the two read alike. */
private val MM_SEA = Color(0xFFCFE3EF)
private val MM_LAND = Color(0xFFF4F1EA)
private val MM_COAST = Color(0xFFB9B09F)
private val MM_BORDER = Color(0xFF8C7F6C)
private val MM_RING = Color(0x99455A6E)
private val MM_ORIGIN = Color(0xFF0F766E)

/**
 * A kind's mark: the GUI's colour, and a shape of its own, because
 * colour alone tells a reader who cannot see it nothing.
 */
private enum class MmKind(val key: String, val label: Int,
                          val line: Color, val fill: Color?) {
    FIXED("fixed", R.string.mm_kind_fixed, Color(0xFF1D4ED8), Color(0xFF3B82F6)),
    NETWORK("network", R.string.mm_kind_network, Color(0xFFC2410C), null),
    PRODUCT("product", R.string.mm_kind_product, Color(0xFF6D28D9), Color(0xFFA78BFA)),
    UNKNOWN("unknown", R.string.mm_kind_unknown, Color(0xFF4B5563), Color(0x809CA3AF));

    companion object {
        fun of(key: String) = entries.firstOrNull { it.key == key } ?: UNKNOWN
    }
}

/** Circle, diamond, triangle, square: fixed, network, product, unknown. */
private fun DrawScope.mmMark(kind: MmKind, at: Offset, r: Float) {
    // Nearly every mark is a fixed base -- 5929 of onocoy's 5933 -- so
    // that one is two circles, not a path built per mark per frame.
    if (kind == MmKind.FIXED) {
        drawCircle(kind.fill!!, r, at)
        drawCircle(kind.line, r, at, style = Stroke(r * 0.25f))
        return
    }
    val path = Path().apply {
        when (kind) {
            MmKind.FIXED -> addOval(androidx.compose.ui.geometry.Rect(at, r))
            MmKind.NETWORK -> {
                val q = r * 1.35f
                moveTo(at.x, at.y - q); lineTo(at.x + q, at.y)
                lineTo(at.x, at.y + q); lineTo(at.x - q, at.y); close()
            }
            MmKind.PRODUCT -> {
                val q = r * 1.25f
                moveTo(at.x, at.y - q); lineTo(at.x + q, at.y + q * 0.8f)
                lineTo(at.x - q, at.y + q * 0.8f); close()
            }
            MmKind.UNKNOWN -> {
                val q = r * 0.9f
                addRect(androidx.compose.ui.geometry.Rect(at.x - q, at.y - q, at.x + q, at.y + q))
            }
        }
    }
    kind.fill?.let { drawPath(path, it) }
    drawPath(path, kind.line, style = Stroke(if (kind == MmKind.NETWORK) r * 0.5f else r * 0.25f))
}

/** 1, 2 or 5 times a power of ten, at least [v]. */
private fun nice125(v: Double): Double {
    if (!(v > 0.0)) return 1.0
    val decade = 10.0.pow(floor(log10(v)))
    val n = v / decade
    return decade * when {
        n <= 1.0 -> 1.0
        n <= 2.0 -> 2.0
        n <= 5.0 -> 5.0
        else -> 10.0
    }
}

/** The outlines as an image, and the view they were drawn for. */
private class BaseImage(val image: ImageBitmap, val scale: Float, val pan: Offset)

/**
 * Draw the outlines once, for one view, into an image the screen moves
 * and scales between renders. Drawn live, every frame, they cost a
 * median 81 ms a frame while panning onocoy at 60 km (Huawei,
 * 2026-10-13): Europe and Asia are one polygon, and at that scale its
 * path covers far more pixels than the screen holds.
 */
private fun renderBase(o: ProjectedBasemap, w: Int, h: Int, scale: Float, pan: Offset,
                       dash: Float, density: androidx.compose.ui.unit.Density): ImageBitmap {
    val img = ImageBitmap(w, h)
    CanvasDrawScope().draw(density, LayoutDirection.Ltr,
                           androidx.compose.ui.graphics.Canvas(img), Size(w.toFloat(), h.toFloat())) {
        drawRect(MM_SEA)
        withTransform({
            translate(w / 2f + pan.x, h / 2f + pan.y)
            scale(scale, -scale, pivot = Offset.Zero)
        }) {
            drawPath(o.landFill, MM_LAND)
            drawPath(o.landLine, MM_COAST, style = Stroke(Stroke.HairlineWidth))
            drawPath(o.borderLine, MM_BORDER, style = Stroke(Stroke.HairlineWidth))
            drawPath(o.borderOther, MM_BORDER, style = Stroke(
                Stroke.HairlineWidth,
                pathEffect = PathEffect.dashPathEffect(floatArrayOf(dash / scale, dash / scale))))
        }
    }
    return img
}

/** A centre for a map with no origin: the mean direction of the points. */
private fun meanCentre(pts: List<SourceEntry>): Pair<Double, Double> {
    var x = 0.0; var y = 0.0; var z = 0.0
    val d = PI / 180.0
    for (e in pts) {
        x += cos(e.lat * d) * cos(e.lon * d)
        y += cos(e.lat * d) * sin(e.lon * d)
        z += sin(e.lat * d)
    }
    return atan2(z, sqrt(x * x + y * y)) / d to atan2(y, x) / d
}

/**
 * @param settings  the connection whose caster is mapped.
 * @param onSelect  pro: take this mountpoint, as tapping its list row
 *                  does. Null in free, where the balloon says what pro
 *                  adds instead.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun MountMapScreen(
    settings: CasterSettings,
    onSelect: ((SourceEntry) -> Unit)?,
    onBack: () -> Unit,
    menu: MenuActions,
) {
    val context = LocalContext.current
    val density = LocalDensity.current

    // The centre is fixed when the map opens: the core measured every
    // distance from it, and a centre that moved under the figures would
    // make the rings disagree with the balloons. The phone where it has
    // a fix (display only, it never leaves the device), the set position
    // otherwise -- which tap-to-use fills with a station's own
    // coordinates, so the header says which it is.
    val origin = remember {
        MonitorService.displayPosition?.let { Triple(it.lat, it.lon, true) }
            ?: Triple(settings.latitude, settings.longitude, false)
    }

    var entries by remember { mutableStateOf<List<SourceEntry>?>(null) }
    var failed by remember { mutableStateOf<String?>(null) }
    LaunchedEffect(settings.caster, settings.port) {
        fetchSourcetable(settings, origin.first, origin.second)
            .onSuccess { entries = it }
            .onFailure { failed = sourcetableFailureText(context, it, settings) }
    }

    val all = entries.orEmpty()
    val drawn = remember(entries) { all.filter { it.usable } }
    // Whether the core accepted the centre: it gives a distance to every
    // usable entry when it did, and to none when it did not.
    val haveOrigin = drawn.any { it.distanceM != null }
    val centre = remember(entries) {
        if (haveOrigin || drawn.isEmpty()) origin.first to origin.second
        else meanCentre(drawn)
    }

    // Marks in metres east and north of the centre: the core's distance
    // along the core's bearing where there is an origin, which is what
    // the projection does with a coordinate; projected here otherwise.
    val xy = remember(entries) {
        val a = FloatArray(2 * drawn.size)
        val proj = Aeqd(centre.first, centre.second)
        drawn.forEachIndexed { i, e ->
            val d = e.distanceM
            val b = e.bearingDeg
            if (haveOrigin && d != null && b != null) {
                a[2 * i] = (d * sin(b * PI / 180.0)).toFloat()
                a[2 * i + 1] = (d * cos(b * PI / 180.0)).toFloat()
            } else {
                proj.project(e.lat, e.lon, a, 2 * i)
            }
        }
        a
    }

    val kinds = remember(entries) { drawn.map { MmKind.of(it.kind) } }

    var outlines by remember { mutableStateOf<ProjectedBasemap?>(null) }
    LaunchedEffect(centre) {
        outlines = withContext(Dispatchers.Default) {
            runCatching {
                Basemap.load(context).project(Aeqd(centre.first, centre.second))
            }.getOrNull()
        }
    }

    // The view: pixels per metre, and where the centre sits on screen.
    var box by remember { mutableStateOf(IntSize.Zero) }
    var scale by remember { mutableFloatStateOf(0f) }
    var pan by remember { mutableStateOf(Offset.Zero) }
    var picked by remember { mutableIntStateOf(-1) }

    // The opening view. With an origin: the tenth-nearest mountpoint at
    // the edge, so the question "which is near me?" opens answered.
    // Without: every mountpoint in view.
    fun home() {
        if (box == IntSize.Zero) return
        val far = if (haveOrigin) {
            val ds = drawn.mapNotNull { it.distanceM }.sorted()
            if (ds.isEmpty()) 50_000.0 else ds[min(9, ds.size - 1)] * 1.15
        } else {
            var m = 0.0
            for (i in drawn.indices) m = max(m, hypot(xy[2 * i].toDouble(), xy[2 * i + 1].toDouble()))
            m * 1.1
        }
        val radius = far.coerceIn(2_000.0, PI * Aeqd.EARTH_RADIUS_M)
        val half = min(box.width, box.height) / 2f - with(density) { 16.dp.toPx() }
        scale = (half / radius).toFloat()
        pan = Offset.Zero
        picked = -1
    }
    // Once there is something to frame: framed on an empty table, the
    // view stayed at 2 km round nothing when 5900 entries arrived
    // (Huawei, 2026-10-13).
    LaunchedEffect(entries, box) { if (entries != null && scale == 0f) home() }

    // Re-drawn once the view has rested 150 ms. The view is watched
    // through a flow, not read here: read in composition, every step of
    // a gesture recomposed the whole screen -- a median 73 ms a frame on
    // the Huawei (2026-10-13) -- where only the canvas needs redrawing.
    var base by remember { mutableStateOf<BaseImage?>(null) }
    LaunchedEffect(outlines, box) {
        val o = outlines ?: return@LaunchedEffect
        if (box.width == 0 || box.height == 0) return@LaunchedEffect
        val dash = with(density) { 4.dp.toPx() }
        snapshotFlow { scale to pan }.collectLatest { (s, p) ->
            if (s == 0f) return@collectLatest
            delay(150)
            val t0 = android.os.SystemClock.elapsedRealtime()
            base = BaseImage(
                withContext(Dispatchers.Default) {
                    renderBase(o, box.width, box.height, s, p, dash, density)
                }, s, p)
            android.util.Log.d("ntrip_android", "mountmap outlines rendered in " +
                "${android.os.SystemClock.elapsedRealtime() - t0} ms")
        }
    }

    val maxScale = (min(box.width, box.height) / 2f) / 200f   // 200 m across half
    val minScale = (min(box.width, box.height) / 2f) / (PI * Aeqd.EARTH_RADIUS_M).toFloat()

    fun screenOf(i: Int): Offset = Offset(
        box.width / 2f + pan.x + xy[2 * i] * scale,
        box.height / 2f + pan.y - xy[2 * i + 1] * scale,
    )

    val measurer = rememberTextMeasurer()
    val labelStyle = MaterialTheme.typography.labelSmall.copy(color = Color(0xFF334155))
    val markR = with(density) { 5.dp.toPx() }
    val reach = with(density) { 24.dp.toPx() }

    AppScaffold(onBack = onBack, menu = menu) { pad ->
        Column(Modifier.padding(pad).fillMaxSize()) {
            // What is on the map, and from where.
            Column(Modifier.padding(horizontal = 16.dp, vertical = 4.dp)) {
                Text(if (entries == null) settings.caster
                     else stringResource(R.string.mm_drawn, settings.caster, drawn.size),
                     style = MaterialTheme.typography.bodyMedium,
                     fontWeight = FontWeight.Medium)
                val omitted = all.size - drawn.size
                if (entries != null && omitted > 0)
                    Text(stringResource(R.string.mm_omitted, omitted),
                         style = MaterialTheme.typography.bodySmall,
                         color = MaterialTheme.colorScheme.onSurfaceVariant)
                if (entries != null)
                    Text(stringResource(when {
                            !haveOrigin -> R.string.mm_centre_none
                            origin.third -> R.string.mm_centre_fix
                            else -> R.string.mm_centre_set
                         }),
                         style = MaterialTheme.typography.bodySmall,
                         color = MaterialTheme.colorScheme.onSurfaceVariant)
            }

            // Clipped: a ring of 3 km on a 1 km view is drawn whole, and
            // unclipped it painted over the header and the key (Huawei,
            // 2026-10-13).
            Box(Modifier.weight(1f).fillMaxWidth().clipToBounds().onSizeChanged { box = it }) {
                Canvas(
                    Modifier
                        .fillMaxSize()
                        .pointerInput(entries) {
                            detectTransformGestures { centroid, move, zoom, _ ->
                                if (scale == 0f) return@detectTransformGestures
                                val s = (scale * zoom).coerceIn(minScale, maxScale)
                                // Keep the point under the fingers under them.
                                val c = Offset(size.width / 2f, size.height / 2f)
                                val f = s / scale
                                pan = (centroid - c) - (centroid - c - pan) * f + move
                                scale = s
                            }
                        }
                        .pointerInput(entries) {
                            detectTapGestures { tap ->
                                var best = -1
                                var bestD = reach
                                val s = scale
                                val ox = size.width / 2f + pan.x
                                val oy = size.height / 2f + pan.y
                                for (i in drawn.indices) {
                                    val d = hypot(ox + xy[2 * i] * s - tap.x,
                                                  oy - xy[2 * i + 1] * s - tap.y)
                                    if (d < bestD) { bestD = d; best = i }
                                }
                                picked = best
                            }
                        }
                ) {
                    drawRect(MM_SEA)
                    if (scale == 0f) return@Canvas
                    val cx = size.width / 2f + pan.x
                    val cy = size.height / 2f + pan.y

                    // The last rendered outlines, moved and scaled to
                    // this view: a pixel q of the image is the place
                    // (q - c - p0) / s0, which is now at c + p + that * s.
                    base?.let { b ->
                        val f = scale / b.scale
                        val c = Offset(size.width / 2f, size.height / 2f)
                        val t = c + pan - (c + b.pan) * f
                        withTransform({
                            translate(t.x, t.y)
                            scale(f, f, pivot = Offset.Zero)
                        }) { drawImage(b.image) }
                    }

                    // Rings from the centre, 1-2-5 apart, when there is a
                    // centre they are true from.
                    if (haveOrigin) {
                        val view = min(size.width, size.height) / 2f / scale
                        val step = nice125(view / 3.0)
                        val reachM = hypot(size.width.toDouble(), size.height.toDouble()) / scale +
                                     hypot(pan.x.toDouble(), pan.y.toDouble()) / scale
                        var k = 1
                        while (k * step < reachM && k <= 40) {
                            val r = (k * step * scale).toFloat()
                            drawCircle(MM_RING, r, Offset(cx, cy), style = Stroke(1.dp.toPx()))
                            val t = measurer.measure(distanceText(k * step), labelStyle)
                            // Where the ring crosses the north line, just
                            // right of it and just inside the ring: the
                            // place every polar plot labels its rings
                            // (design/appGuiReview.md, R6), on the patch
                            // the map already drew its labels on.
                            val at = Offset(cx + 4.dp.toPx(), cy - r + 4.dp.toPx())
                            drawRect(MM_SEA.copy(alpha = 0.8f), at, Size(t.size.width.toFloat(), t.size.height.toFloat()))
                            drawText(t, topLeft = at)
                            k++
                        }
                        // The centre itself.
                        drawCircle(MM_ORIGIN, markR * 0.9f, Offset(cx, cy))
                        drawCircle(Color.White, markR * 0.9f, Offset(cx, cy), style = Stroke(2f))
                    }

                    // North is up at the centre; the meridians converge
                    // away from it, as they must on this projection.
                    val n = measurer.measure("N ↑", labelStyle)
                    drawText(n, topLeft = Offset(size.width - n.size.width - 12f, 8f))

                    // The view read once, not twice a mark: 5900 marks
                    // reading the view's state each cost a median 57 ms
                    // a frame (Huawei, 2026-10-13).
                    val s = scale
                    val w = size.width
                    val h = size.height
                    for (i in drawn.indices) {
                        val x = cx + xy[2 * i] * s
                        val y = cy - xy[2 * i + 1] * s
                        if (x < -markR || y < -markR || x > w + markR || y > h + markR) continue
                        mmMark(kinds[i], Offset(x, y), markR)
                    }
                    if (picked in drawn.indices) {
                        drawCircle(Color.Black, markR * 2.2f, screenOf(picked), style = Stroke(2.5f))
                    }
                }

                when {
                    failed != null -> Text(failed.orEmpty(),
                                   color = MaterialTheme.colorScheme.error,
                                   modifier = Modifier.align(Alignment.Center).padding(16.dp))
                    entries == null -> CircularProgressIndicator(Modifier.align(Alignment.Center))
                }

                OutlinedButton(
                    onClick = { home() },
                    modifier = Modifier.align(Alignment.TopStart).padding(8.dp),
                ) { Text(stringResource(R.string.mm_recentre)) }

                if (picked in drawn.indices) {
                    Balloon(drawn[picked], screenOf(picked), box, haveOrigin,
                            onSelect, onClose = { picked = -1 })
                }
            }

            // The key to the marks, and the credit for what is under them.
            // Wrapping: four in one row ran "Unknown" off a phone's edge.
            FlowRow(
                Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 4.dp),
                horizontalArrangement = Arrangement.spacedBy(10.dp),
                verticalArrangement = Arrangement.spacedBy(2.dp),
            ) {
                MmKind.entries.forEach { k ->
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Canvas(Modifier.size(14.dp)) {
                            mmMark(k, Offset(size.width / 2f, size.height / 2f), size.width / 3.2f)
                        }
                        Spacer(Modifier.width(3.dp))
                        Text(stringResource(k.label), style = MaterialTheme.typography.labelSmall)
                    }
                }
            }
            Text(stringResource(R.string.mm_credit),
                 style = MaterialTheme.typography.labelSmall,
                 color = MaterialTheme.colorScheme.onSurfaceVariant,
                 modifier = Modifier.padding(start = 16.dp, bottom = 6.dp))
        }
    }
}

/**
 * What a tapped mountpoint is, beside it: above it in the lower half of
 * the map, below it in the upper, and never off the edge.
 */
@Composable
private fun Balloon(
    e: SourceEntry,
    at: Offset,
    box: IntSize,
    haveOrigin: Boolean,
    onSelect: ((SourceEntry) -> Unit)?,
    onClose: () -> Unit,
) {
    val density = LocalDensity.current
    var size by remember { mutableStateOf(IntSize.Zero) }
    val gap = with(density) { 14.dp.toPx() }
    val margin = with(density) { 8.dp.toPx() }
    val x = (at.x - size.width / 2f)
        .coerceIn(margin, max(margin, box.width - size.width - margin))
    val y = if (at.y > box.height / 2f) at.y - size.height - gap else at.y + gap
    val faint = MaterialTheme.colorScheme.onSurfaceVariant

    Card(
        Modifier
            .offset { IntOffset(x.roundToInt(), y.coerceIn(0f, max(0f, box.height - size.height.toFloat())).roundToInt()) }
            .widthIn(max = 320.dp)
            .onSizeChanged { size = it }
            .pointerInput(e) { detectTapGestures { } },   // a tap on it is not a tap on the map
    ) {
        Column(Modifier.padding(12.dp)) {
            Text(e.mountpoint, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
            if (e.identifier.isNotBlank()) Text(e.identifier, style = MaterialTheme.typography.bodySmall)
            Text(listOf(e.format, e.navSystems).filter { it.isNotBlank() }.joinToString("  ·  "),
                 style = MaterialTheme.typography.bodySmall, color = faint)
            val where = listOf(e.network, e.country).filter { it.isNotBlank() }
            if (where.isNotEmpty())
                Text(where.joinToString("  ·  "), style = MaterialTheme.typography.bodySmall, color = faint)
            Spacer(Modifier.height(4.dp))
            val kind = MmKind.of(e.kind)
            Text(stringResource(kind.label) +
                     if (e.kindWhy.isNotBlank()) ": ${e.kindWhy}" else "",
                 style = MaterialTheme.typography.bodySmall, color = kind.line)
            val d = e.distanceM
            val b = e.bearingDeg
            if (haveOrigin && d != null && b != null)
                Text(stringResource(R.string.mm_distance, distanceText(d), compassPoint(b),
                                    b.roundToInt() % 360),
                     style = MaterialTheme.typography.bodySmall)
            if (e.nmea)
                Text(stringResource(R.string.mm_gga), style = MaterialTheme.typography.bodySmall,
                     color = MaterialTheme.colorScheme.primary)
            Spacer(Modifier.height(4.dp))
            if (onSelect != null) {
                Row {
                    TextButton(onClick = onClose) { Text(stringResource(R.string.action_close)) }
                    Spacer(Modifier.weight(1f))
                    TextButton(onClick = { onSelect(e) }) { Text(stringResource(R.string.mm_select)) }
                }
            } else {
                Text(stringResource(R.string.mm_pro_line),
                     style = MaterialTheme.typography.bodySmall, color = faint)
                TextButton(onClick = onClose) { Text(stringResource(R.string.action_close)) }
            }
        }
    }
}
