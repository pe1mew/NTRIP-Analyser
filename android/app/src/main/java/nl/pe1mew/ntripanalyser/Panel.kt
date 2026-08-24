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
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.Stable
import androidx.compose.runtime.key
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.Layout
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/**
 * The hub's one vertical distance.
 *
 * Between two cards, between two KPI rows, between a panel of several
 * cards and the next panel: the same. A screen whose gaps vary reads as
 * a screen assembled from parts, which is exactly what it is and exactly
 * what it should not look like.
 */
internal val HUB_GAP = 12.dp

/**
 * How far a mark sits from the right edge of the row it belongs to.
 *
 * The same for a card the hub marks and for a row that marks itself, so
 * a column of them lines up down the screen. The cards that fold pad
 * their own contents by this much already, which is why they pass a bare
 * modifier and the hub adds this.
 */
internal val HUB_MARK_INSET = 12.dp

/**
 * Which rows are folded open.
 *
 * Above the rows rather than inside them, and deliberately. A row's own
 * `rememberSaveable` dies with the row, and rows do leave: when a run
 * ends the hub is rebuilt around a finished document, and every fold the
 * reader had opened while watching the run shut itself at the moment
 * they wanted to read it.
 *
 * Cleared when a new run begins -- what a row had open belonged to the
 * measurement underneath it -- and not before.
 */
internal object FoldState {
    private val open = mutableStateMapOf<String, Boolean>()
    private var run = 0L

    fun isOpen(id: String): Boolean = open[id] == true

    fun toggle(id: String) { open[id] = !isOpen(id) }

    /** Called from an effect, never during composition. */
    fun startOf(runKey: Long) {
        if (runKey == run) return
        run = runKey
        open.clear()
    }
}

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
 * The mark a row wears, and the whole of what it promises.
 *
 * GUI v3, P2.1 (`design/guiV3spec.md` §2). The template gives every
 * touchable row a triangle saying what touching it does, and the
 * absence of one saying that nothing will happen. Drawn by the hub from
 * this, never by a card: a capability that arrives later is marked
 * because it exists, and cannot ship a row that lies about itself.
 *
 * `FORWARD` is *forward* rather than *configure*: the author's own
 * mockup puts it on **Run the check** and on the analysis bar, and
 * neither of those configures anything.
 */
enum class Affordance { NONE, FORWARD, EXPAND, COLLAPSE }

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
    val startVrsCheck: () -> Unit,
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
     * What this row's mark says, given what it is currently drawing.
     *
     * Takes the state because a panel that draws nothing must promise
     * nothing: `BrowsePanel` disappears while a run is going, and a
     * triangle hovering over the gap would be a control that is not
     * there.
     *
     * The default is the honest one for a panel with a screen behind
     * it. Anything that folds open in place says so itself.
     */
    fun affordance(state: HubState): Affordance =
        if (destination() != null) Affordance.FORWARD else Affordance.NONE

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
    // A Column would do, were it not for the panels that draw nothing:
    // an empty one still occupies a slot, and `spacedBy` puts a gap
    // around it, so the hub's rhythm depended on which panels happened
    // to have something to say. This lays out only what has height, with
    // one gap between neighbours -- the same gap the KPI rows use, so
    // the whole screen keeps a single vertical beat.
    // Folds belong to the run they were opened over.
    LaunchedEffect(state.run.runId) { FoldState.startOf(state.run.runId) }

    Layout(
        modifier = modifier,
        content = {
        panels.forEach { panel ->
            key(panel.key) {
                val dest = panel.destination()
                // A card with a screen behind it is tappable, and the
                // tap is added here rather than inside every such panel
                // -- so a capability cannot ship a card that drills
                // nowhere because its author forgot. The mark it wears
                // is added in the same place and for the same reason.
                Box(
                    if (dest == null) Modifier
                    else Modifier.clickable { actions.openDetail(dest) }
                ) {
                    // A Column, not the Box itself. A panel may draw
                    // more than one card -- the eight KPI rows are one
                    // panel -- and a Box stacks its children, which put
                    // all eight on the same spot with the last on top.
                    // The spacing matches the hub's own, so a panel of
                    // several cards sits in the run of them evenly.
                    Column(verticalArrangement = Arrangement.spacedBy(HUB_GAP)) {
                        panel.Content(state, actions)
                    }
                    AffordanceMark(
                        panel.affordance(state),
                        Modifier
                            .align(Alignment.CenterEnd)
                            .padding(end = HUB_MARK_INSET),
                    )
                }
            }
        }
        },
    ) { measurables, constraints ->
        val gap = HUB_GAP.roundToPx()
        val drawn = measurables
            .map { it.measure(constraints.copy(minHeight = 0)) }
            .filter { it.height > 0 }
        val width =
            if (constraints.hasBoundedWidth) constraints.maxWidth
            else drawn.maxOfOrNull { it.width } ?: 0
        // Honour the minimum we were given.  `fillMaxSize()` sits ahead
        // of `verticalScroll` in the caller's modifier chain, so the
        // scroll hands this layout a minimum of the whole viewport; a
        // layout that reports less than its minimum is centred in the
        // slack, which put half the empty space above the first card and
        // half below the last -- a band under the title on any screen
        // taller than its own content.  Any slack belongs at the bottom.
        val height = (drawn.sumOf { it.height } +
            gap * (drawn.size - 1).coerceAtLeast(0))
            .coerceAtLeast(constraints.minHeight)
        layout(width, height) {
            var y = 0
            drawn.forEach { p ->
                p.placeRelative(0, y)
                y += p.height + gap
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
        // No scroll wrapper, deliberately -- learned from the first real
        // detail. A screen wrapped in verticalScroll hands its children
        // infinite height, and the analysis band template answers
        // infinity by taking its tall branch, whose weighted plot then
        // collapses: maxHeight(-6) on an S23, a crash. The template
        // already scrolls *itself* when the viewport is short, which is
        // the whole point of PlotLayout -- so the frame stays fixed and
        // a detail that needs scrolling brings its own, knowing what is
        // inside it.
        Column(
            Modifier
                .padding(pad)
                .padding(16.dp)
                .fillMaxSize(),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            // The bar says the name of the app on every screen now, so
            // the name of *this* screen is the first line under it.
            Text(panel.detailTitle(), style = MaterialTheme.typography.titleLarge)
            panel.Detail(state, actions)
        }
    }
}

/**
 * The triangle itself.
 *
 * Right-aligned and vertically centred in whatever the panel drew, a
 * fixed distance from its right edge, so eight of them line up down the
 * screen. It draws no touch target of its own: the row is the target,
 * and a mark that could be missed by a thumb would be a worse lie than
 * no mark at all.
 */
@Composable
internal fun AffordanceMark(mark: Affordance, modifier: Modifier) {
    val glyph = when (mark) {
        Affordance.NONE -> return
        Affordance.FORWARD -> "▶"
        Affordance.EXPAND -> "▼"
        Affordance.COLLAPSE -> "▲"
    }
    Text(
        glyph,
        modifier = modifier,
        fontSize = 12.sp,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
}
