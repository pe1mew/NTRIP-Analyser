/**
 * @file Share.kt
 * @brief The station report, assembled from the panels that drew it.
 *
 * GUI v2, P1.6 (`design/guiV2rollout.md`). The report is not written
 * here: each panel contributes its own [ShareSection] and this walks the
 * registry, so the text comes out in **hub order** and reads like the
 * screen it came from. Adding a capability later adds its part of the
 * report by existing, not by editing this file.
 *
 * Two things go out, and which one depends on where you are. The hub
 * sends the report as `text/plain`; the analysis screen sends the plot
 * you are looking at as a PNG, because a plot's natural artefact is a
 * picture. The image path needs a `FileProvider` -- `ConfigFile.kt`
 * writes through SAF, which `ACTION_SEND` cannot reuse -- and that
 * provider now exists, which is what statistics export will use when it
 * arrives.
 *
 * **What may never appear here.** A section may name the caster and the
 * mountpoint; it may not carry a username or a password. That holds by
 * construction for anything built from the snapshot, which contains no
 * credentials at all, and by rule for anything reaching into the
 * settings -- checked by `tools/check_release.py`, which reads every
 * `shareSection` and fails if one mentions either.
 *
 * The phone's own position never appears either. A configured position
 * may, because the user typed it and can see it on screen; the live fix
 * is read under a permission granted for the sky view and stays on the
 * device.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
package nl.pe1mew.ntripanalyser

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import androidx.core.content.FileProvider
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Assemble the report from whatever the registry contains.
 *
 * @param panels the edition's panels, in hub order.
 * @param state  what they all read.
 * @param header the first line -- the app and version that measured it,
 *               because a report that outlives the phone it came from
 *               should say what produced it.
 */
fun buildReport(panels: List<Panel>, state: HubState, header: String): String {
    val stamp = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(Date())
    return buildString {
        appendLine(header)
        appendLine(stamp)
        panels.forEach { panel ->
            panel.shareSection(state)?.let { section ->
                appendLine()
                appendLine(section.title)
                section.lines.forEach { appendLine("  $it") }
            }
        }
    }
}

/**
 * Send a plot as a PNG, with a caption saying what it is.
 *
 * The bitmap is written into the one cache directory the file provider
 * declares (`res/xml/file_paths.xml`) and handed over as a content URI
 * with a read grant. Nothing else the app holds is reachable that way:
 * not the configuration, not an imported navigation file.
 *
 * One file, overwritten each time rather than accumulating. A plot that
 * has been sent is the receiving app's problem, and a cache that grows
 * a PNG per share is a bug reported six months later as "the app is
 * using a gigabyte".
 *
 * @return false if the file could not be written — the caller says so
 *         rather than opening a chooser that would share nothing.
 */
fun sharePlot(
    context: Context,
    bitmap: Bitmap,
    caption: String,
    subject: String,
    chooser: String,
): Boolean {
    val dir = File(context.cacheDir, "shared")
    if (!dir.exists() && !dir.mkdirs()) return false
    val file = File(dir, "station-plot.png")
    try {
        FileOutputStream(file).use { out ->
            bitmap.compress(Bitmap.CompressFormat.PNG, 100, out)
        }
    } catch (e: IOException) {
        return false
    }

    val uri = FileProvider.getUriForFile(
        context, "${context.packageName}.files", file
    )
    val send = Intent(Intent.ACTION_SEND).apply {
        type = "image/png"
        putExtra(Intent.EXTRA_STREAM, uri)
        putExtra(Intent.EXTRA_SUBJECT, subject)
        putExtra(Intent.EXTRA_TEXT, caption)
        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
    }
    context.startActivity(Intent.createChooser(send, chooser))
    return true
}

/**
 * Hand the report to whatever the user picked to receive it.
 *
 * A chooser every time rather than a remembered default: this is a
 * document about somebody's station going to somebody else, and the app
 * should not decide once and for all where it goes.
 */
fun shareReport(context: Context, text: String, subject: String, chooser: String) {
    val send = Intent(Intent.ACTION_SEND).apply {
        type = "text/plain"
        putExtra(Intent.EXTRA_SUBJECT, subject)
        putExtra(Intent.EXTRA_TEXT, text)
    }
    context.startActivity(Intent.createChooser(send, chooser))
}
