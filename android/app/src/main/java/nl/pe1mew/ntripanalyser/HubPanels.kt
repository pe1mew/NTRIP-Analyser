/**
 * @file HubPanels.kt
 * @brief The station hub's own panels, in the order they appear.
 *
 * Each is a thin wrapper around a card that already existed in
 * `StationCards.kt`: the drawing is unchanged, and what is new is only
 * that the hub asks a list rather than naming each one (GUI v2, P1.2).
 *
 * The run controls and the browse button are panels too, though they
 * draw verbs rather than results. That is deliberate: if some things on
 * the hub were panels and others were furniture rendered around them,
 * "the list is the layout" would stop being true and every later
 * addition would have to ask where it is allowed to appear.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
package nl.pe1mew.ntripanalyser

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/** The verdict, or what is standing in for one while a run is going. */
object VerdictPanel : Panel {
    override val key = "verdict"

    @Composable
    override fun Content(state: HubState, actions: HubActions) {
        val context = LocalContext.current
        VerdictBadge(
            state.doc, state.run.running, state.run.outcome,
            state.settings.isComplete,
            failure = state.doc?.stats?.failure
                ?.takeIf { it != Failure.NONE }
                ?.let { failureSentence(context, it, state.settings) },
        )
    }

    override fun shareSection(state: HubState): ShareSection? {
        val k = state.doc?.kpi ?: return null
        return ShareSection("Verdict", listOf(
            k.overallName,
            "held %.0f s of %.0f required".format(k.sustainedS, k.sustainTargetS),
            "run lasted %.0f s".format(k.elapsedS),
        ))
    }
}

/**
 * What this run is pointed at, always.
 *
 * A measurement whose subject is off screen is a measurement of nothing
 * in particular, and hiding it mid-run once made the single tap into the
 * caster settings disappear with it.
 */
object ConnectionPanel : Panel {
    override val key = "connection"

    @Composable
    override fun Content(state: HubState, actions: HubActions) {
        ConfigSummary(state.settings) { actions.editConnection() }
    }

    override fun affordance(state: HubState) = Affordance.FORWARD

    /**
     * The subject of the measurement, and **only** the subject.
     *
     * Caster, port and mountpoint: what a reader needs to know which
     * station this report is about. Never the username, never the
     * password — a report is a document that leaves the phone, and the
     * one thing it must not carry is the way back in.
     */
    override fun shareSection(state: HubState): ShareSection {
        val s = state.settings
        return ShareSection("Stream", listOf(
            "${s.caster}:${s.port}/${s.mountpoint}",
        ))
    }
}

/** Reading the caster's sourcetable; not offered while a run is going. */
object BrowsePanel : Panel {
    override val key = "browse"

    @Composable
    override fun Content(state: HubState, actions: HubActions) {
        if (state.settings.caster.isNotBlank() && !state.run.running) {
            OutlinedButton(
                onClick = { actions.browseSourcetable() },
                modifier = Modifier.fillMaxWidth(),
            ) { Text(stringResource(R.string.action_browse)) }
        }
    }

    // Same condition as the card above, deliberately duplicated: a mark
    // over a row that is not drawn would be a control that is not there.
    override fun affordance(state: HubState) =
        if (state.settings.caster.isNotBlank() && !state.run.running)
            Affordance.FORWARD else Affordance.NONE
}

/** Rate, satellites, mountpoint -- the stream at a glance. */
object ChipsPanel : Panel {
    override val key = "chips"

    @Composable
    override fun Content(state: HubState, actions: HubActions) {
        state.doc?.let { StreamChips(it) }
    }

    override fun shareSection(state: HubState): ShareSection? {
        val st = state.doc?.stats ?: return null
        val lines = mutableListOf<String>()
        st.bytesPerS?.let { lines += "%.0f B/s".format(it) }
        lines += "${st.satsTotal} satellites"
        lines += "${st.framesOk} frames"
        if (st.arpValid && st.arpLat != null && st.arpLon != null) {
            lines += "ARP %.6f, %.6f".format(st.arpLat, st.arpLon)
        }
        return ShareSection("Measured", lines)
    }
}

/** Whatever went wrong, in the user's way rather than in a log. */
object ErrorPanel : Panel {
    override val key = "error"

    @Composable
    override fun Content(state: HubState, actions: HubActions) {
        state.run.error?.let {
            Text(it, color = MaterialTheme.colorScheme.error)
        }
    }

    /** What went wrong belongs in the report; it is usually the reason for it. */
    override fun shareSection(state: HubState): ShareSection? =
        state.run.error?.let { ShareSection("Error", listOf(it)) }
}

/** How long this station has been watched, and what happened in it. */
object WatchPanel : Panel {
    override val key = "watch"

    @Composable
    override fun Content(state: HubState, actions: HubActions) {
        state.doc?.watch?.let { WatchCard(it) }
    }

    override fun shareSection(state: HubState): ShareSection? {
        val w = state.doc?.watch ?: return null
        val lines = mutableListOf(
            "watched for ${dur(w.elapsedS)}",
            "worst seen: ${w.worstName}",
            "${w.degradations} degradation(s)",
            "longest good streak ${dur(w.bestStreakS)}",
        )
        w.availability?.let { lines += "availability %.2f %%".format(it) }
        return ShareSection("Over time", lines)
    }
}

/** The eight checks. One panel, because they are one verdict's evidence. */
object KpiPanel : Panel {
    override val key = "kpi"

    @Composable
    override fun Content(state: HubState, actions: HubActions) {
        val doc = state.doc ?: return
        doc.kpi.items.forEachIndexed { i, item ->
            KpiRow(i + 1, item, doc.stats, doc.arp)
        }
    }

    /**
     * The eight, with the words the screen used.
     *
     * `verdictName` and `detail` come from the C engine, so the report
     * says what the app said -- not a second wording of the same
     * judgement, which is how two descriptions of one measurement start
     * disagreeing.
     */
    override fun shareSection(state: HubState): ShareSection? {
        val items = state.doc?.kpi?.items?.takeIf { it.isNotEmpty() } ?: return null
        return ShareSection("The eight checks", items.mapIndexed { i, it ->
            "${i + 1}. ${it.label}: ${it.verdictName} — ${it.detail}"
        })
    }
}

/**
 * Two ways to run, chosen at the moment of running: grade the station
 * once, or watch it. Neither is a caster setting.
 *
 * Analysis stays reachable during a run -- watching a check unfold is
 * the point of having the views, and hiding the way in until it finished
 * made a running test unobservable.
 */
object RunControlsPanel : Panel {
    override val key = "run"

    @Composable
    override fun Content(state: HubState, actions: HubActions) {
        // Analysis has left this panel. It is a way out of the screen
        // rather than a thing to run, and the template gives it a bar of
        // its own at the bottom -- where it stays put instead of sliding
        // away under eight KPI rows the moment a run starts.
        // The marks here are the template's, and they are transport
        // marks rather than affordances: Dia2 starts a run with ▶ and
        // Dia4 stops one with ■. They sit inside the button, at its
        // right edge, where the row marks sit on the cards above.
        if (state.run.running) {
            Button(
                onClick = { actions.stopRun() },
                modifier = Modifier.fillMaxWidth(),
                contentPadding = TRANSPORT_PADDING,
            ) { TransportLabel(stringResource(R.string.action_stop), "■") }
        } else {
            Button(
                onClick = { actions.startCheck() },
                enabled = state.settings.isComplete,
                modifier = Modifier.fillMaxWidth(),
                contentPadding = TRANSPORT_PADDING,
            ) {
                TransportLabel(
                    stringResource(
                        if (state.doc != null) R.string.action_again
                        else R.string.action_run
                    ),
                    "▶",
                )
            }
        }
    }
}

/**
 * Where the orbits stand: incompleteness and age are shown, never left
 * implicit. Not while a run is going, when the numbers move under the
 * reader.
 */
/**
 * A button's own padding, trimmed on the right.
 *
 * Material gives a button 24.dp each side, which would put its transport
 * mark 12.dp further in than every mark on the cards above it. The right
 * side matches [HUB_MARK_INSET] instead, so one column of marks runs
 * down the screen; the left keeps the button's own measure, because the
 * label is a label and not a row of text.
 */
private val TRANSPORT_PADDING = PaddingValues(
    start = 24.dp, top = 8.dp, end = HUB_MARK_INSET, bottom = 8.dp,
)

/** A button's label and its transport mark, one at each end. */
@Composable
private fun TransportLabel(label: String, mark: String) {
    Row(
        Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(label)
        Text(mark, fontSize = 12.sp)
    }
}

object EphemerisPanel : Panel {
    override val key = "ephemeris"

    @Composable
    override fun Content(state: HubState, actions: HubActions) {
        if (state.run.running) return
        state.doc?.eph?.let {
            EphCard(it, state.rinexName, state.rinexAgeS, state.phonePlaced)
        }
    }

    /**
     * Where the sky plot's positions came from.
     *
     * A plot is only as good as its orbits, and a report that shows
     * satellites without saying how they were placed invites the reader
     * to trust a picture drawn from the phone's own almanac.
     */
    override fun shareSection(state: HubState): ShareSection? {
        val e = state.doc?.eph ?: return null
        val lines = mutableListOf("${e.cached} orbit(s) cached")
        if (state.phonePlaced > 0) lines += "${state.phonePlaced} placed from this phone"
        state.rinexName?.let { lines += "navigation file: $it" }
        return ShareSection("Orbits", lines)
    }
}

/** The nudge that appears until there is something to run against. */
object SetupHintPanel : Panel {
    override val key = "hint"

    @Composable
    override fun Content(state: HubState, actions: HubActions) {
        if (!state.settings.isComplete) {
            Text(
                stringResource(R.string.hint_configure),
                style = MaterialTheme.typography.bodySmall,
            )
        }
    }
}
