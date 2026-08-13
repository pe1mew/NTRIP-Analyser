package nl.pe1mew.ntripanalyser

import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.util.Log
import java.util.Locale

/**
 * Picking a position on a map, without carrying a map.
 *
 * The Windows GUI does this by writing a Leaflet page to a temporary
 * file, opening it in the user's browser and letting the page copy the
 * clicked coordinates to the clipboard; the app then pastes them
 * (`gui/gui_events.c`, `OnMapPick`). The shape is kept here -- **hand
 * the job to a map the user already has, take the answer back through
 * the clipboard** -- because it is what makes the map free of
 * consequences for this app:
 *
 * - **No map SDK, no tiles fetched by us.** Embedding osmdroid (as
 *   `ttnmapper-android-v3` does) is licence-compatible, Apache 2.0, but
 *   it would move tile requests into this process. Those requests carry
 *   where the user is looking, to a tile host, from an app that
 *   otherwise collects nothing -- and it would change the Play
 *   data-safety answer from "no data collected or shared".
 * - **No tile-policy exposure.** The OSM Foundation's tile policy
 *   forbids bulk or offline prefetching, which is exactly what field use
 *   of an embedded map wants. A browser or maps app visiting its own
 *   provider is that provider's user, not ours.
 * - **No attribution to get wrong.** Whatever the user opens shows its
 *   own.
 *
 * The Android half differs from the GUI's in mechanism only: a local
 * `file://` page cannot be handed to a modern Android browser (a
 * `content://` HTML document is downloaded rather than rendered), so
 * instead a `geo:` intent opens whatever map app is installed, falling
 * back to OpenStreetMap in the browser. Both routes end the same way --
 * the user copies coordinates, and [parse] reads them.
 */
object MapPick {

    private const val TAG = "ntrip_map"

    /** Neighbourhood scale, as the GUI uses: close enough to see a mast. */
    private const val ZOOM = 15

    /**
     * Open a map at [lat],[lon] for the user to look around.
     *
     * @return false when the device has neither a maps app nor a browser,
     *         so the caller can say so rather than appear to do nothing.
     */
    fun open(context: Context, lat: Double, lon: Double): Boolean {
        // The query keeps a pin at the starting point: without it some
        // map apps centre the view and leave the user guessing which
        // position the app is actually holding.
        // Locale.US, not the device's: a Dutch or German locale
        // formats 52.1 as "52,1", and a comma is the separator between
        // the two numbers -- the URI would name a different place.
        val geo = Uri.parse("geo:%.6f,%.6f?q=%.6f,%.6f&z=%d"
            .format(Locale.US, lat, lon, lat, lon, ZOOM))
        if (start(context, geo)) return true

        val web = Uri.parse("https://www.openstreetmap.org/#map=%d/%.6f/%.6f"
            .format(Locale.US, ZOOM, lat, lon))
        return start(context, web)
    }

    private fun start(context: Context, uri: Uri): Boolean = runCatching {
        context.startActivity(
            Intent(Intent.ACTION_VIEW, uri).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        )
        true
    }.getOrElse {
        Log.i(TAG, "no handler for $uri")
        false
    }

    /** Whatever the user last copied, as plain text. */
    fun clipboard(context: Context): String? {
        val cm = context.getSystemService(ClipboardManager::class.java) ?: return null
        val clip = cm.primaryClip ?: return null
        if (clip.itemCount == 0) return null
        return clip.getItemAt(0).coerceToText(context)?.toString()
    }

    /**
     * Read a position out of whatever the user copied.
     *
     * Deliberately generous, because the useful sources all phrase it
     * differently and the user should not have to know which:
     *
     * | Source | Looks like |
     * |---|---|
     * | The Windows GUI's map picker, and plain typing | `52.123456, 6.123456` |
     * | An OpenStreetMap address bar | `.../#map=15/52.123456/6.123456` |
     * | A Google Maps address bar | `.../@52.123456,6.123456,15z/...` |
     * | A shared place, or a `geo:` link | `geo:52.123456,6.123456` |
     *
     * Anything out of range is rejected rather than clamped: a paste that
     * did not contain a position must fail visibly, not silently move the
     * rover to the nearest legal point.
     *
     * @return latitude to longitude, or null when the text holds neither.
     */
    fun parse(text: String?): Pair<Double, Double>? {
        val t = text?.trim().orEmpty()
        if (t.isEmpty()) return null

        // An OSM permalink states the coordinates slash-separated after
        // the zoom, so the plain "two numbers" rule below would read the
        // zoom as the latitude.
        Regex("""#map=\d+(?:\.\d+)?/(-?\d+\.?\d*)/(-?\d+\.?\d*)""")
            .find(t)?.let { m -> return check(m.groupValues[1], m.groupValues[2]) }

        Regex("""@(-?\d+\.?\d*),(-?\d+\.?\d*)""")
            .find(t)?.let { m -> return check(m.groupValues[1], m.groupValues[2]) }

        Regex("""(-?\d+\.?\d*)[,;\s]+(-?\d+\.?\d*)""")
            .find(t.removePrefix("geo:"))
            ?.let { m -> return check(m.groupValues[1], m.groupValues[2]) }

        return null
    }

    private fun check(latText: String, lonText: String): Pair<Double, Double>? {
        val lat = latText.toDoubleOrNull() ?: return null
        val lon = lonText.toDoubleOrNull() ?: return null
        if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) return null
        return lat to lon
    }
}
