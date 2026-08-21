/**
 * @file Panel.kt
 * @brief What the station hub is made of, and the contract each part keeps.
 *
 * GUI v2, P1.2 (`design/guiV2rollout.md`). A capability is one file
 * contributing up to three things -- a card on the hub, a destination
 * behind it, and a section of the shared report -- and a registry lists
 * the panels an edition contains. The framework is identical in free and
 * pro; only the list differs, which is what keeps the editions from
 * drifting apart while nothing paid is compiled into free.
 *
 * The hub renders the registry in order, so **the list is the layout**.
 * Reordering the screen means reordering one list, and the shared report
 * comes out in the order the screen did.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
package nl.pe1mew.ntripanalyser

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.Stable
import androidx.compose.runtime.key
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp

/**
 * One part of the shared report.
 *
 * Deliberately stupid -- a title and lines of text. Three of its
 * producers do not exist yet, and a dumb type is cheap to be wrong
 * about; units, severity or structure wait until a second consumer asks
 * for them. Assembling and emitting these is P1.6.
 *
 * **Credentials never appear here.** A section may name the caster and
 * the mountpoint; it may not carry a username or a password. That holds
 * by construction for anything built from [HubState.doc], which has no
 * credentials in it at all, and by rule for anything that reaches into
 * the settings.
 */
data class ShareSection(val title: String, val lines: List<String>)

/**
 * Everything a panel may read.
 *
 * One object rather than a parameter list, so a panel that later needs
 * another field does not change every other panel's signature -- the
 * lesson of the composable that grew six arguments and was passed
 * through four layers.
 */
@Immutable
data class HubState(
    val run: MonitorService.RunState,
    val settings: CasterSettings,
    val rinexName: String?,
    val rinexAgeS: Double?,
    /** Satellites placed from the phone's own GNSS rather than orbits. */
    val phonePlaced: Int,
) {
    val doc: BridgeDocument? get() = run.document
}

/**
 * What a panel may ask the shell to do.
 *
 * Panels never navigate or start work themselves: they are given the
 * verbs, and the shell decides what each one means. That is what lets
 * the same card sit on a hub today and inside something else later
 * without being rewritten.
 */
@Stable
class HubActions(
    val editConnection: () -> Unit,
    val browseSourcetable: () -> Unit,
    val startCheck: () -> Unit,
    val stopRun: () -> Unit,
    val openAnalysis: () -> Unit,
    /** Drill into a panel's own screen. The shell owns the stack. */
    val openDetail: (Dest) -> Unit,
)

/**
 * A part of the hub.
 *
 * Most panels draw a card and nothing else; a panel with a
 * [destination] is one the card drills into; a panel with a
 * [shareSection] contributes to the report. None of the three is
 * required -- the run controls are a panel that only draws.
 */
interface Panel {

    /**
     * Stable identity, for the hub's `key` and for debugging a report
     * that came out in the wrong order. Frozen once shipped, like a
     * route in [Dest].
     */
    val key: String

    /** The card, in hub order. May draw nothing when it has nothing to say. */
    @Composable
    fun Content(state: HubState, actions: HubActions)

    /**
     * The screen this card drills into, if it has one.
     *
     * Return `Dest.Detail(key)` and the card becomes tappable, the shell
     * pushes it, the system back key pops it, and [Detail] is asked to
     * draw. Nothing else needs editing — that is the whole point of the
     * contract, and it is why this is wired before any panel uses it.
     */
    fun destination(): Dest? = null

    /**
     * That screen's content, drawn under a back bar the shell provides.
     *
     * Only called when [destination] is non-null. A panel with no detail
     * never implements it.
     */
    @Composable
    fun Detail(state: HubState, actions: HubActions) {
    }

    /** The detail screen's title. */
    @Composable
    fun detailTitle(): String = ""

    /** This panel's part of the shared report, if it has anything to add. */
    fun shareSection(state: HubState): ShareSection? = null
}

/**
 * The hub: the registry, rendered in order.
 *
 * The whole screen is this list. Nothing here knows what any panel is,
 * which is the point -- adding a capability is adding a file and a line,
 * not editing this function.
 */
@Composable
fun StationHub(
    panels: List<Panel>,
    state: HubState,
    actions: HubActions,
    modifier: Modifier = Modifier,
) {
    Column(modifier, verticalArrangement = Arrangement.spacedBy(12.dp)) {
        panels.forEach { panel ->
            key(panel.key) {
                val dest = panel.destination()
                if (dest == null) {
                    panel.Content(state, actions)
                } else {
                    // A card with a screen behind it is tappable, and
                    // the tap is added here rather than inside every
                    // such panel -- so a capability cannot ship a card
                    // that drills nowhere because its author forgot.
                    Box(Modifier.clickable { actions.openDetail(dest) }) {
                        panel.Content(state, actions)
                    }
                }
            }
        }
    }
}

/**
 * A panel's own screen, with the back bar the shell owns.
 *
 * Title and content come from the panel; the way back does not, so every
 * detail screen leaves the same way and no panel has to remember to
 * offer it.
 */
@Composable
fun DetailScreen(
    panel: Panel,
    state: HubState,
    actions: HubActions,
    onBack: () -> Unit,
    menu: MenuActions,
) {
    val context = LocalContext.current
    AppScaffold(
        onBack = onBack,
        menu = menu,
        // A detail screen is one section of the report seen close up, so
        // its share sends the report: whoever asks for a section wants
        // the run it came out of.
        onShare = {
            shareReport(
                context,
                buildReport(
                    hubPanels, state,
                    context.getString(R.string.share_header,
                                      BuildConfig.VERSION_NAME),
                ),
                context.getString(R.string.share_subject,
                                  state.settings.mountpoint),
                context.getString(R.string.share_chooser),
            )
        },
        shareEnabled = state.doc != null,
    ) { pad ->
        Column(
            Modifier
                .padding(pad)
                .padding(16.dp)
                .fillMaxSize()
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            // The bar says the name of the app on every screen now, so
            // the name of *this* screen is the first line under it.
            Text(panel.detailTitle(), style = MaterialTheme.typography.titleLarge)
            panel.Detail(state, actions)
        }
    }
}
