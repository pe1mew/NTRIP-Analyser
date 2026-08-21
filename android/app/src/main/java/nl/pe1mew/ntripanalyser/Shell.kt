/**
 * @file Shell.kt
 * @brief The frame every screen is drawn in.
 *
 * GUI v3, P1.1 (`design/guiV3rollout.md`, specification `guiV3spec.md`
 * §1). The template gives the top bar four slots and fixes what may sit
 * in each: a leading control that is either back or nothing, the app's
 * own name, share, and the overflow menu. Every screen gets that bar
 * from here.
 *
 * **One composable, not three that agree.** The review this came from
 * exists because three of them had already drifted: the hub grew a `☰`
 * in the slot the template reserves for back, the analysis screen grew a
 * filled `Phone GNSS` pill where the template has nothing, and each of
 * them titled itself differently. A caller cannot pass a title here --
 * there is no parameter for one -- so the editions and the screens
 * cannot disagree about what the app is called.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
package nl.pe1mew.ntripanalyser

import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.IconButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.sp

/**
 * The application frame: top bar, and whatever the screen puts under it.
 *
 * @param onBack   what the leading control does, or `null` on a screen
 *                 with nowhere to go back to -- which leaves the slot
 *                 empty rather than filling it with something else.
 * @param onShare  what the share control sends, or `null` to hide it.
 *                 Every screen that has something to send should send
 *                 it: the hub sends the report, a plot sends a picture.
 * @param shareEnabled  whether there is anything to share yet. Shown but
 *                 disabled rather than hidden, so the control does not
 *                 appear and disappear as a run starts.
 * @param overflow the `⋮` menu and its trigger. A slot rather than a
 *                 fixed menu while P1.2 is still to come; the trigger
 *                 lives here so that the actions row is share-then-menu
 *                 on every screen, which is the template's order.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AppScaffold(
    onBack: (() -> Unit)? = null,
    onShare: (() -> Unit)? = null,
    shareEnabled: Boolean = true,
    overflow: @Composable () -> Unit = {},
    content: @Composable (PaddingValues) -> Unit,
) {
    val backLabel = stringResource(R.string.action_back)
    val shareLabel = stringResource(R.string.action_share)

    Scaffold(
        topBar = {
            TopAppBar(
                // No title parameter, deliberately. The name of the app
                // is the title on every screen; which screen you are on
                // is said by the back arrow and by the tabs.
                title = { Text(stringResource(R.string.app_name)) },
                navigationIcon = {
                    if (onBack != null) {
                        IconButton(onClick = onBack) {
                            Text(
                                "←",
                                fontSize = 26.sp,
                                modifier = Modifier.semantics {
                                    contentDescription = backLabel
                                },
                            )
                        }
                    }
                },
                actions = {
                    if (onShare != null) {
                        IconButton(onClick = onShare, enabled = shareEnabled) {
                            // Glyphs rather than icon assets, as before:
                            // this arrow draws thin strokes in a corner
                            // of its box, so it needs more point size
                            // than a solid glyph to read the same.
                            Text(
                                "⤴",
                                fontSize = 28.sp,
                                modifier = Modifier.semantics {
                                    contentDescription = shareLabel
                                },
                            )
                        }
                    }
                    overflow()
                },
            )
        },
        content = content,
    )
}
