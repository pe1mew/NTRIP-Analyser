/**
 * @file Basemap.kt
 * @brief Natural Earth's outlines, read from the app's asset and
 *        projected about a point, for the mountpoint map.
 *
 * The asset is `basemap_ne50m.bin`, written by `tools/make_basemap.py`
 * from the same values as the GUI's `gui/basemap_ne50m.js`, so the two
 * draw the same coastlines (mountpoint-map.md, M6b). Its layout is in
 * that tool's docstring; this reader is its other half.
 *
 * Natural Earth is public domain; it is credited on the map and in the
 * app's Notices all the same.
 *
 * Nothing here measures. The projection places outlines on a screen;
 * the distance and bearing a mountpoint is shown with are the core's
 * (`bridge_sourcetable_to_json`, M6a), and the projection is chosen so
 * the two agree: see [Aeqd].
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
package nl.pe1mew.ntripanalyser

import android.content.Context
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathFillType
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.PI
import kotlin.math.acos
import kotlin.math.cos
import kotlin.math.sin

/**
 * The outlines as the asset holds them: thousandths of a degree,
 * longitude then latitude, per point.
 */
class Basemap(
    /** Natural Earth's release, from the asset itself. */
    val version: String,
    /** Each land polygon: its rings, outer first. */
    val land: List<List<IntArray>>,
    /** Each border line: class (0 international, 1 otherwise) and points. */
    val borders: List<Pair<Int, IntArray>>,
) {
    companion object {
        private const val ASSET = "basemap_ne50m.bin"

        @Volatile private var cached: Basemap? = null

        /**
         * Read once per process; 650 kB, so call it off the main thread.
         * Throws on a malformed asset rather than drawing half a world --
         * the asset is built and checked with the app, so that would be
         * a build fault worth hearing about.
         */
        fun load(context: Context): Basemap =
            cached ?: context.assets.open(ASSET).use { parse(it.readBytes()) }
                .also { cached = it }

        internal fun parse(bytes: ByteArray): Basemap {
            val b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
            val magic = ByteArray(4).also { b.get(it) }
            require(String(magic, Charsets.US_ASCII) == "NEBM") { "not a basemap" }
            require(b.get().toInt() == 1) { "basemap format" }
            val vlen = b.get().toInt() and 0xFF
            val version = String(ByteArray(vlen).also { b.get(it) }, Charsets.US_ASCII)

            fun points(): IntArray {
                val n = b.int
                return IntArray(2 * n).also { a -> for (i in a.indices) a[i] = b.int }
            }

            val land = List(b.int) {
                List(b.short.toInt() and 0xFFFF) { points() }
            }
            val borders = List(b.int) {
                val cls = b.get().toInt()
                cls to points()
            }
            require(!b.hasRemaining()) { "trailing bytes in the basemap" }
            return Basemap(version, land, borders)
        }
    }
}

/**
 * Azimuthal equidistant, about one point.
 *
 * Chosen because it is the projection in which distance and bearing
 * **from the centre** are true everywhere on the screen -- a point's
 * position is its great-circle distance along its initial bearing. The
 * core's `distance_m` and `bearing_deg` for a mountpoint therefore land
 * exactly where this projection puts its coordinates, and a ring of
 * radius r is every place r away. Outlines further from the centre are
 * stretched sideways, increasingly towards the far side of the world;
 * the map says so.
 *
 * On the sphere the core uses: [EARTH_RADIUS_M] is `GEO_EARTH_RADIUS_M`
 * in `src/core/geo.h`, checked equal by `tools/check_release.py`.
 */
class Aeqd(lat0Deg: Double, lon0Deg: Double) {
    private val sp0 = sin(lat0Deg * D2R)
    private val cp0 = cos(lat0Deg * D2R)
    private val l0 = lon0Deg * D2R

    /**
     * Metres east and north of the centre into [out] at [i], [i]+1.
     * @return the angular distance from the centre, radians (0..pi).
     */
    fun project(latDeg: Double, lonDeg: Double, out: FloatArray, i: Int): Double {
        val p = latDeg * D2R
        val sp = sin(p)
        val cp = cos(p)
        val dl = lonDeg * D2R - l0
        val cdl = cos(dl)
        val cc = (sp0 * sp + cp0 * cp * cdl).coerceIn(-1.0, 1.0)
        val c = acos(cc)
        val k = if (c < 1e-9) 1.0 else c / sin(c)
        out[i] = (EARTH_RADIUS_M * k * cp * sin(dl)).toFloat()
        out[i + 1] = (EARTH_RADIUS_M * k * (cp0 * sp - sp0 * cp * cdl)).toFloat()
        return c
    }

    companion object {
        /** `GEO_EARTH_RADIUS_M`, src/core/geo.h -- the core's sphere. */
        const val EARTH_RADIUS_M = 6371008.8
        private const val D2R = PI / 180.0
    }
}

/** A basemap projected about one centre: paths in metres, north up. */
class ProjectedBasemap(
    /** Polygons that project cleanly, to be filled. */
    val landFill: Path,
    /** Every land ring's outline, broken where it nears the far side. */
    val landLine: Path,
    val borderLine: Path,
    /** Disputed, lines of control, indefinite: drawn dashed. */
    val borderOther: Path,
)

/**
 * Near the far side of the world the projection tears: the antipode
 * is a whole circle. Points past this angle break a line instead of
 * joining it, and a polygon reaching past it is outlined, not filled.
 */
private const val FAR_SIDE = 0.9 * PI

fun Basemap.project(proj: Aeqd): ProjectedBasemap {
    // Even-odd: a polygon's inner rings are holes in it, and land
    // polygons never overlap one another.
    val fill = Path().apply { fillType = PathFillType.EvenOdd }
    val line = Path()
    val border = Path()
    val other = Path()
    val xy = FloatArray(2)

    fun trace(path: Path, pts: IntArray, close: Boolean): Boolean {
        var pen = false
        var whole = true
        var i = 0
        while (i < pts.size) {
            val c = proj.project(pts[i + 1] / 1000.0, pts[i] / 1000.0, xy, 0)
            if (c > FAR_SIDE) {
                pen = false
                whole = false
            } else if (pen) {
                path.lineTo(xy[0], xy[1])
            } else {
                path.moveTo(xy[0], xy[1])
                pen = true
            }
            i += 2
        }
        if (close && whole) path.close()
        return whole
    }

    for (rings in land) {
        // Fill only what projects whole; the outline is drawn either way.
        val whole = rings.all { r -> trace(line, r, close = true) }
        if (whole) rings.forEach { r -> trace(fill, r, close = true) }
    }
    for ((cls, pts) in borders) trace(if (cls == 0) border else other, pts, close = false)
    return ProjectedBasemap(fill, line, border, other)
}
