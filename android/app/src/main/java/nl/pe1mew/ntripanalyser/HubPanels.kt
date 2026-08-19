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
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp

/** The verdict, or what is standing in for one while a run is going. */
object VerdictPanel : Panel {
    override val key = "verdict"

    @Composable
    override fun Content(state: HubState, actions: HubActions) {
        VerdictBadge(
            state.doc, state.run.running, state.run.outcome,
            state.settings.isComplete,
        )
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
}

/** Rate, satellites, mountpoint -- the stream at a glance. */
object ChipsPanel : Panel {
    override val key = "chips"

    @Composable
    override fun Content(state: HubState, actions: HubActions) {
        state.doc?.let { StreamChips(it) }
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
}

/** How long this station has been watched, and what happened in it. */
object WatchPanel : Panel {
    override val key = "watch"

    @Composable
    override fun Content(state: HubState, actions: HubActions) {
        state.doc?.watch?.let { WatchCard(it) }
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
        Spacer(Modifier.height(4.dp))
        if (state.run.running) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(
                    onClick = { actions.stopRun() },
                    modifier = Modifier.weight(1f),
                ) { Text(stringResource(R.string.action_stop)) }
                OutlinedButton(
                    onClick = { actions.openAnalysis() },
                    modifier = Modifier.weight(1f),
                ) { Text(stringResource(R.string.mode_analysis)) }
            }
        } else {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(
                    onClick = { actions.startCheck() },
                    enabled = state.settings.isComplete,
                    modifier = Modifier.weight(1f),
                ) {
                    Text(stringResource(
                        if (state.doc != null) R.string.action_again
                        else R.string.action_run
                    ))
                }
                // Analysis is a mode, not a second kind of run. In pro it
                // starts its own session; in free it shows what this
                // check captured.
                OutlinedButton(
                    onClick = { actions.openAnalysis() },
                    enabled = state.doc?.sats?.isNotEmpty() == true,
                    modifier = Modifier.weight(1f),
                ) { Text(stringResource(R.string.mode_analysis)) }
            }
        }
    }
}

/**
 * Where the orbits stand: incompleteness and age are shown, never left
 * implicit. Not while a run is going, when the numbers move under the
 * reader.
 */
object EphemerisPanel : Panel {
    override val key = "ephemeris"

    @Composable
    override fun Content(state: HubState, actions: HubActions) {
        if (state.run.running) return
        state.doc?.eph?.let {
            EphCard(it, state.rinexName, state.rinexAgeS, state.phonePlaced)
        }
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
