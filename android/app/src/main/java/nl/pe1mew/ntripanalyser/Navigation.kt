package nl.pe1mew.ntripanalyser

import androidx.compose.runtime.Composable
import androidx.compose.runtime.Stable
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.saveable.Saver
import androidx.compose.runtime.saveable.listSaver
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.unit.dp

/**
 * @file Navigation.kt
 * @brief Where the user is, and how they get back.
 *
 * A hand-rolled stack rather than `androidx.navigation-compose`, decided
 * in `design/guiV2rollout.md` (P1.1): what this app needs is five
 * destinations, two deep, with no arguments, no deep links and one back
 * stack. That is a stack, not a graph. The decision records what would
 * reverse it -- choosing bottom navigation, where a back stack per
 * destination is exactly what the library is for.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

/**
 * A full-screen destination.
 *
 * **Hub** is the station: the sixty-second check and its verdict, and
 * from here every other result is one tap away. **Analysis** is the
 * pager of live views -- what the stream is actually doing. Free renders
 * analysis from what the station check captured, frozen at its end; pro
 * runs analysis as a session of its own, started and stopped at will,
 * and that session is what watch mode measures.
 *
 * Detail destinations arrive with their panels, one per paid capability,
 * and each is a `data object` here plus one line in [Dest.of].
 */
sealed interface Dest {

    /**
     * Stable key, and the only thing that is saved.
     *
     * The stack survives process death by being written into a `Bundle`,
     * so what goes in has to be a primitive rather than an object
     * graph. Keys are frozen once shipped for the same reason a JSON
     * field name is: a restored stack from an older build must still
     * mean what it meant.
     */
    val route: String

    data object Hub : Dest { override val route = "hub" }
    data object Analysis : Dest { override val route = "analysis" }

    /**
     * The screen behind a hub card.
     *
     * Identified by the panel's own key rather than by a name the shell
     * knows, which is what lets a capability arrive as one file and one
     * registry line: the shell finds the panel whose destination this
     * is and asks *it* to draw. Nothing here has to learn what a VRS
     * assertion or a hand-over is.
     */
    data class Detail(val key: String) : Dest {
        override val route = "$DETAIL_PREFIX$key"
    }

    companion object {
        private const val DETAIL_PREFIX = "detail/"

        /**
         * @return the destination for [route], or null if this build has
         *         no such destination.
         *
         * Null rather than an exception, deliberately: a stack restored
         * after an update -- or into an edition that does not contain a
         * paid screen -- must land the user somewhere sensible instead
         * of crashing on the first frame. Unknown routes are dropped and
         * the stack falls back to [Hub].
         */
        fun of(route: String): Dest? = when {
            route == Hub.route -> Hub
            route == Analysis.route -> Analysis
            // A detail whose panel this edition does not contain simply
            // has no key that matches at render time, and the shell
            // sends the user to the hub -- the same outcome as an
            // unknown route, without a special case for it.
            route.startsWith(DETAIL_PREFIX) ->
                Detail(route.removePrefix(DETAIL_PREFIX))
            else -> null
        }
    }
}

/**
 * The back stack: a list whose last entry is what is on screen.
 *
 * Deliberately small. Push, pop, and a flag saying whether back means
 * anything -- everything the hub-and-drill-down layout needs, and
 * nothing that would have to be understood again when something goes
 * wrong at three in the morning.
 *
 * Never empty: [pop] refuses to remove the root, so `current` always has
 * an answer and no caller needs a null check.
 */
@Stable
class NavStack internal constructor(initial: List<Dest>) {

    private val stack = mutableStateListOf<Dest>().apply {
        addAll(if (initial.isEmpty()) listOf(Dest.Hub) else initial)
    }

    /** What is on screen. */
    val current: Dest get() = stack.last()

    /** Whether the system back key belongs to the app right now. */
    val canGoBack: Boolean get() = stack.size > 1

    /**
     * Open [dest].
     *
     * Pushing what is already on screen does nothing, so an event that
     * fires twice -- a permission result arriving alongside the gesture
     * that asked for it -- cannot bury the user under two identical
     * screens that each need their own back press.
     */
    fun push(dest: Dest) {
        if (stack.last() != dest) stack.add(dest)
    }

    /** Go back one, or nowhere if this is the root. */
    fun pop() {
        if (canGoBack) stack.removeAt(stack.lastIndex)
    }

    companion object {
        val Saver: Saver<NavStack, Any> = listSaver(
            save = { it.stack.map(Dest::route) },
            restore = { saved ->
                NavStack(saved.filterIsInstance<String>().mapNotNull(Dest::of))
            },
        )
    }
}

/* There is no swipe threshold here any more.
 *
 * A drag distance was the whole of the old navigation between the
 * station and the analysis views, in both directions. It is gone (GUI
 * v2, P1.7): moves are controls and back is Back, so nothing in this
 * app navigates by how far a finger travelled. */

/**
 * A back stack that survives rotation and process death.
 *
 * `rememberSaveable`, not `remember`: the app used neither before GUI v2
 * and so lost the open screen on every rotation. That was invisible
 * while there was one level to lose -- the pager restores itself -- and
 * becomes a reported bug the moment a detail screen can be open.
 */
@Composable
fun rememberNavStack(): NavStack =
    rememberSaveable(saver = NavStack.Saver) { NavStack(listOf(Dest.Hub)) }
