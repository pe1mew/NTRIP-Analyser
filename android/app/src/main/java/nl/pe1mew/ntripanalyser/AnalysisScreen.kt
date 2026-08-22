/**
 * @file AnalysisScreen.kt
 * @brief The three live views.
 *
 * A destination of its own (GUI v2, P1.4), taking what it draws as
 * parameters rather than reaching into the shell's state: same three
 * tabs in the same order, same pager.
 *
 * Reached by the Analysis button and left by Back -- the swipe that used
 * to do both was removed in P1.7, so every move in this app is a control
 * and every way back is Back.
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
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Tab
import androidx.compose.material3.TabRow
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.graphics.rememberGraphicsLayer
import androidx.compose.ui.graphics.layer.drawLayer
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.launch

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
    menu: MenuActions,
) {
    val uriHandler = LocalUriHandler.current
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    // What the share action sends: whatever the pager is showing,
    // recorded as it is drawn rather than re-rendered, so the picture
    // that leaves is the picture that was on screen.
    val plotLayer = rememberGraphicsLayer()
    val surface = MaterialTheme.colorScheme.surface

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

    // Which of the three is on screen, named once: the share caption
    // says it, and P3.1 will put it in the tab row's explainer band.
    val viewName = stringResource(when (tab) {
        AnalysisTab.SKY -> R.string.view_sky
        AnalysisTab.SIGNAL -> R.string.view_bars
        AnalysisTab.ELEVATION -> R.string.view_elev
    })

    AppScaffold(
        onBack = onLeave,
        menu = menu,
        // Share what is on screen: the plot, as it is drawn. A plot's
        // natural artefact is a picture, which is why this screen sends
        // one and the hub sends text.
        onShare = {
            scope.launch {
                val bitmap = plotLayer.toImageBitmap().asAndroidBitmap()
                sharePlot(
                    context,
                    bitmap,
                    caption = context.getString(
                        R.string.share_plot_caption, viewName,
                        doc?.stats?.mountpoint.orEmpty()),
                    subject = context.getString(
                        R.string.share_plot_subject,
                        doc?.stats?.mountpoint.orEmpty(), viewName),
                    chooser = context.getString(R.string.share_chooser),
                )
            }
        },
    ) { pad ->
        Column(Modifier.padding(pad).fillMaxSize()) {

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

            // Under the tabs, not above them: the template's second band
            // is the selector, and everything after it belongs to the
            // view that selector chose.
            Row(
                Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 4.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Box(Modifier.weight(1f)) {
                    // Pro runs analysis as its own session; free shows
                    // what the station check captured, frozen at its end.
                    if (Features.HAS_WATCH) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Button(onClick = { onToggleWatch() }) {
                                Text(stringResource(
                                    if (running) R.string.action_stop
                                    else R.string.action_analyse
                                ))
                            }
                            Spacer(Modifier.width(12.dp))
                            doc?.watch?.let { w ->
                                Text(
                                    stringResource(R.string.analysis_running,
                                                   dur(w.elapsedS)),
                                    style = MaterialTheme.typography.bodySmall,
                                )
                            }
                        }
                    } else {
                        Text(
                            stringResource(R.string.analysis_static),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
            }

            // The pager keeps its own overscroll now. It was suppressed
            // to feed a leave-gesture that read the drag the stretch
            // effect consumed; with the gesture gone, the platform's
            // own behaviour is the right one.
            Box(
                Modifier
                    .weight(1f)
                    .drawWithContent {
                        // The surface is painted by the Scaffold behind
                        // this layer, not inside it, so a recording of
                        // the content alone comes out on transparency --
                        // a plot that looks fine in a gallery and turns
                        // into invisible ink on a white page. Fill it
                        // first, with the theme's own surface: white in
                        // the light theme, and dark where the plot draws
                        // in light ink.
                        plotLayer.record {
                            drawRect(surface)
                            this@drawWithContent.drawContent()
                        }
                        drawLayer(plotLayer)
                    }
            ) {
                HorizontalPager(state = pagerState, modifier = Modifier.fillMaxSize()) { page ->
                  when (AnalysisTab.entries[page]) {
                    AnalysisTab.SKY -> SkyView(
                        sats = plotted,
                        missing = ((doc?.sats?.size ?: 0) - plotted.size)
                            .coerceAtLeast(0),
                        source = skySource(usedOrbits, doc, haveLocation),
                        footer = footer,
                        rinexAgeS = rinexAgeS,
                        onSourceClick = { uriHandler.openUri(ORBITS_URL) },
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
