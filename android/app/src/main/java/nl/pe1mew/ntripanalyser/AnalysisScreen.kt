/**
 * @file AnalysisScreen.kt
 * @brief The three live views, and the gesture out of them.
 *
 * A destination of its own (GUI v2, P1.4), taking what it draws as
 * parameters rather than reaching into the shell's state. Nothing about
 * the screen changed in the move: same three tabs in the same order,
 * same pager, and the same nested-scroll trick for leaving it.
 *
 * No tab is added here in phase 1, and none in phase 2 either --
 * satellite tracks are drawn inside the sky canvas, not beside it.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
package nl.pe1mew.ntripanalyser

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.LocalOverscrollConfiguration
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Tab
import androidx.compose.material3.TabRow
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.input.nestedscroll.NestedScrollConnection
import androidx.compose.ui.input.nestedscroll.NestedScrollSource
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp

/**
 * @param doc          the latest snapshot, or null before anything ran.
 * @param running      whether a session is live; the bars show this
 *                     epoch when it is, and the session mean when not.
 * @param plotted      satellites that could be placed in the sky.
 * @param usedOrbits   how many of those came from real orbits rather
 *                     than the phone, which is what the badge reports.
 * @param onLeave      go back; the caller decides what that means.
 */
@OptIn(ExperimentalMaterial3Api::class, ExperimentalFoundationApi::class)
@Composable
fun AnalysisScreen(
    doc: BridgeDocument?,
    running: Boolean,
    plotted: List<PlottedSat>,
    usedOrbits: Int,
    signal: List<SignalSat>,
    elevSamples: ElevationAccumulator,
    elevRevision: Int,
    haveLocation: Boolean,
    rinexAgeS: Double?,
    tab: AnalysisTab,
    onTab: (AnalysisTab) -> Unit,
    onToggleWatch: () -> Unit,
    onLeave: () -> Unit,
) {
    val uriHandler = LocalUriHandler.current
    val swipePx = with(LocalDensity.current) { SwipeThreshold.toPx() }

    val footer = doc?.stats?.let { st ->
        buildString {
            append(st.mountpoint)
            if (st.arpValid && st.arpLat != null && st.arpLon != null) {
                append("  ARP: %.6f, %.6f".format(st.arpLat, st.arpLon))
            }
        }
    }.orEmpty()

    // One page per view, so a swipe moves between them with the
    // content following the finger rather than cutting.
    val pagerState = rememberPagerState(initialPage = tab.ordinal) {
        AnalysisTab.entries.size
    }
    // Kept in step both ways: the tab row still selects a page, and
    // a swipe still moves the selected tab.
    LaunchedEffect(tab) {
        if (pagerState.currentPage != tab.ordinal)
            pagerState.animateScrollToPage(tab.ordinal)
    }
    LaunchedEffect(pagerState.currentPage) {
        onTab(AnalysisTab.entries[pagerState.currentPage])
    }

    // Dragging the first view further right has nowhere to go inside
    // the pager, so what it does not consume comes back here and
    // means "out of this screen" -- the mirror of the swipe that
    // opened it.
    val leaveAnalysis = remember(swipePx) {
        object : NestedScrollConnection {
            var carried = 0f
            override fun onPostScroll(
                consumed: Offset, available: Offset, source: NestedScrollSource,
            ): Offset {
                if (pagerState.currentPage == 0 && available.x > 0f) {
                    carried += available.x
                    if (carried > swipePx) {
                        carried = 0f
                        onLeave()
                    }
                } else if (available.x < 0f) {
                    carried = 0f
                }
                return Offset.Zero
            }
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(stringResource(R.string.mode_analysis)) },
                navigationIcon = {
                    TextButton(onClick = { onLeave() }) {
                        Text(stringResource(R.string.action_back))
                    }
                },
                // Both views on this screen are drawn from satellite
                // positions, and neither can say where those came
                // from once it is drawn. The badge says it here, for
                // both of them at once, and leads to the page that
                // explains what to do about it.
                actions = {
                    OrbitSourceBadge(
                        source = skySource(usedOrbits, doc, haveLocation),
                        rinexAgeS = rinexAgeS,
                        onClick = { uriHandler.openUri(ORBITS_URL) },
                    )
                },
            )
        }
    ) { pad ->
        Column(Modifier.padding(pad).fillMaxSize()) {

            // Pro runs analysis as its own session; free shows what
            // the station check captured, frozen at its end.
            if (Features.HAS_WATCH) {
                Row(
                    Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Button(onClick = { onToggleWatch() }) {
                        Text(stringResource(
                            if (running) R.string.action_stop
                            else R.string.action_analyse
                        ))
                    }
                    Spacer(Modifier.width(12.dp))
                    doc?.watch?.let { w ->
                        Text(
                            stringResource(R.string.analysis_running, dur(w.elapsedS)),
                            style = MaterialTheme.typography.bodySmall,
                        )
                    }
                }
            } else {
                Text(
                    stringResource(R.string.analysis_static),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
                )
            }

            // No watch card here. It is absent when the screen opens
            // and appears a second later, shoving the plot down the
            // moment the user has started reading it. The elapsed
            // time is beside the Stop button already; the rest of the
            // long-run picture belongs on the station screen, where
            // nothing moves under it.

            TabRow(selectedTabIndex = tab.ordinal) {
                AnalysisTab.entries.forEach { t ->
                    Tab(
                        selected = tab == t,
                        onClick = { onTab(t) },
                        text = {
                            Text(stringResource(when (t) {
                                AnalysisTab.SKY -> R.string.view_sky
                                AnalysisTab.SIGNAL -> R.string.view_bars
                                AnalysisTab.ELEVATION -> R.string.view_elev
                            }))
                        },
                    )
                }
            }

            Box(Modifier.weight(1f).nestedScroll(leaveAnalysis)) {
              // No overscroll on this pager, or the gesture above
              // never happens. From Android 12 the stretch effect
              // *consumes* the drag that runs past the first page --
              // the very leftover this screen reads as "leave" -- so
              // the swipe worked on the Android 10 handset, where the
              // older glow only draws, and did nothing on the S23.
              // A stretch animation is a fair price for the gesture
              // that navigates.
              CompositionLocalProvider(
                  LocalOverscrollConfiguration provides null
              ) {
                HorizontalPager(state = pagerState, modifier = Modifier.fillMaxSize()) { page ->
                  when (AnalysisTab.entries[page]) {
                    AnalysisTab.SKY -> SkyView(
                        sats = plotted,
                        missing = ((doc?.sats?.size ?: 0) - plotted.size)
                            .coerceAtLeast(0),
                        source = skySource(usedOrbits, doc, haveLocation),
                        footer = footer,
                    )
                    AnalysisTab.SIGNAL ->
                        SignalBars(signal, liveValues = running)
                    AnalysisTab.ELEVATION ->
                        ElevationView(elevSamples, elevRevision)
                  }
                }
              }
            }
        }
    }
}
