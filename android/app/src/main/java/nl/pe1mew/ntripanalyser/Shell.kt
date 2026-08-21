/**
 * @file Shell.kt
 * @brief The frame every screen is drawn in.
 *
 * GUI v3, P1.1 and P1.2 (`design/guiV3rollout.md`, specification
 * `guiV3spec.md`
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
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
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
 * @param menu     what the `⋮` menu does. Not a slot: a screen says
 *                 what the rows *do* and never what they *are*, so no
 *                 screen can offer a different menu from the next one.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AppScaffold(
    onBack: (() -> Unit)? = null,
    onShare: (() -> Unit)? = null,
    shareEnabled: Boolean = true,
    menu: MenuActions? = null,
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
                    if (menu != null) OverflowMenu(menu)
                },
            )
        },
        content = content,
    )
}

/**
 * What the overflow menu's rows do.
 *
 * The rows themselves are fixed (`AppMenu` in `Dialogs.kt`, which gates
 * the configuration rows on the edition); this is only the wiring, held
 * by the one screen that owns the state and the file pickers. Every
 * other screen passes it on without knowing what is in it.
 */
@Immutable
class MenuActions(
    val settings: () -> Unit,
    val importRinex: () -> Unit,
    val loadConfig: () -> Unit,
    val saveConfig: () -> Unit,
    val about: () -> Unit,
)

/**
 * The `⋮` and the menu under it.
 *
 * Its open/closed state lives here rather than in the screen: three
 * screens showing one menu should not each carry a boolean for it, and
 * a menu that is open on one screen is not open on another.
 */
@Composable
private fun OverflowMenu(actions: MenuActions) {
    var open by remember { mutableStateOf(false) }
    val label = stringResource(R.string.action_menu)

    IconButton(onClick = { open = true }) {
        Text(
            "⋮",
            fontSize = 24.sp,
            modifier = Modifier.semantics { contentDescription = label },
        )
    }
    AppMenu(
        open = open,
        onDismiss = { open = false },
        onSettings = { open = false; actions.settings() },
        onImportRinex = { open = false; actions.importRinex() },
        onLoadConfig = { open = false; actions.loadConfig() },
        onSaveConfig = { open = false; actions.saveConfig() },
        onAbout = { open = false; actions.about() },
    )
}
