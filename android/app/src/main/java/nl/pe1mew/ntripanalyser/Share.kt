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
 * `text/plain` for now. The plot as a PNG needs a `FileProvider`, which
 * the app does not have -- `ConfigFile.kt` writes through SAF, which
 * `ACTION_SEND` cannot reuse -- so attachments wait for whichever needs
 * one first, the sky plot or statistics export.
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
