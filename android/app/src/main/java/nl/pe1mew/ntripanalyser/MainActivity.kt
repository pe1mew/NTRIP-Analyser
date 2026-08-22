package nl.pe1mew.ntripanalyser

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import android.app.Activity
import androidx.activity.compose.setContent
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import androidx.core.content.ContextCompat
import androidx.core.view.WindowCompat
import androidx.lifecycle.compose.collectAsStateWithLifecycle

/**
 * Normal mode: one screen, one verdict, seven rows.
 *
 * The screen renders what the C engine decided and never decides
 * anything itself -- no thresholds live in Kotlin (design-review D1).
 */
class MainActivity : ComponentActivity() {

    private val notificationPermission =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        requestNotificationPermissionIfNeeded()
        setContent { AppTheme { MainScreen() } }
    }

    /**
     * The run lives in a foreground service, which on Android 13+ needs
     * notification permission to show its ongoing notification. Asking
     * up front avoids a run that starts and is then silently curtailed.
     */
    private fun requestNotificationPermissionIfNeeded() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) return
        val granted = ContextCompat.checkSelfPermission(
            this, Manifest.permission.POST_NOTIFICATIONS
        ) == PackageManager.PERMISSION_GRANTED
        if (!granted) notificationPermission.launch(Manifest.permission.POST_NOTIFICATIONS)
    }
}

@Composable
private fun AppTheme(content: @Composable () -> Unit) {
    val dark = isSystemInDarkThemeSafe()

    // Whose bars are these? Android 16 draws the app behind the status
    // and navigation bars and will not let an app targeting 36 opt out,
    // so the system's icons now sit on *our* background -- and their
    // colour is ours to set. Left alone they stayed light, which on this
    // light theme is invisible: the clock and the signal bars vanished
    // on the S23. Follow the theme, dark icons on the light scheme and
    // light icons on the dark one.
    val view = LocalView.current
    if (!view.isInEditMode) {
        val window = (view.context as Activity).window
        SideEffect {
            WindowCompat.getInsetsController(window, view).apply {
                isAppearanceLightStatusBars = !dark
                isAppearanceLightNavigationBars = !dark
            }
        }
    }

    MaterialTheme(colorScheme = if (dark) darkColorScheme() else lightColorScheme()) {
        content()
    }
}

@Composable
private fun isSystemInDarkThemeSafe(): Boolean =
    androidx.compose.foundation.isSystemInDarkTheme()

/** Verdict colours, matching the desktop's green / amber / red idiom. */
internal fun verdictColour(v: Verdict): Color = when (v) {
    Verdict.PASS -> Color(0xFF46AF5A)
    Verdict.WARN -> Color(0xFFE6A014)
    Verdict.FAIL -> Color(0xFFD72828)
    Verdict.PENDING -> Color(0xFF9E9E9E)
}

internal fun runColour(v: RunVerdict): Color = when (v) {
    RunVerdict.OK -> Color(0xFF46AF5A)
    RunVerdict.CAUTION -> Color(0xFFE6A014)
    RunVerdict.FAILED -> Color(0xFFD72828)
    RunVerdict.RUNNING -> Color(0xFF5A7DAF)
}

/* Where the user is now lives in Navigation.kt: `Dest` and a back stack
 * that survives rotation, because a hub with drill-down needs more than
 * one level and an enum cannot express "go back". */

/** Which analysis view is showing. */
enum class AnalysisTab { SKY, SIGNAL, ELEVATION }

@OptIn(ExperimentalMaterial3Api::class, ExperimentalFoundationApi::class)
@Composable
fun MainScreen() {
    val context = androidx.compose.ui.platform.LocalContext.current
    val runState by MonitorService.state.collectAsStateWithLifecycle(MonitorService.RunState())

    // One source of truth for every saved connection; the active one is
    // derived rather than copied, so the two cannot disagree.
    var store by remember { mutableStateOf(Settings.loadProfiles(context)) }
    val settings = store.current
    var showSettings by remember { mutableStateOf(!store.current.isComplete) }
    var showPicker by remember { mutableStateOf(false) }
    var showSourcetable by remember { mutableStateOf(false) }
    val nav = rememberNavStack()
    // Saveable for the same reason the stack is: coming back to the app
    // on the tab you left is the behaviour, and losing it is a bug that
    // only shows once there is somewhere to lose.
    var tab by rememberSaveable { mutableStateOf(AnalysisTab.SKY) }

    // Positions come from the phone's own GNSS -- what both editions
    // share, and all the free edition has. Permission is asked when the
    // sky view is first opened, never at launch.
    val phoneGnss = remember { PhoneGnss(context) }
    val positions by phoneGnss.positions.collectAsStateWithLifecycle(emptyMap())
    val fix by phoneGnss.fix.collectAsStateWithLifecycle(null)
    var haveLocation by remember { mutableStateOf(hasLocationPermission(context)) }
    var ggaConsent by remember { mutableStateOf(Settings.liveGgaConsent(context)) }
    var openSky by remember { mutableStateOf(false) }
    var rinexName by remember { mutableStateOf(Settings.rinexName(context)) }
    // Aged against the clock, so a file that was fresh when imported goes
    // stale while the app is open -- which is exactly what happens to
    // broadcast ephemerides, and what the badge exists to show.
    var rinexAgeS by remember { mutableStateOf(Settings.rinexAgeS(context)) }
    val uriHandler = androidx.compose.ui.platform.LocalUriHandler.current
    var showAbout by remember { mutableStateOf(false) }
    var notice by remember { mutableStateOf<String?>(null) }

    // The same config.json the CLI and the GUI read and write, so a
    // configuration moves between desktop and phone unchanged.
    val pickConfig = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri ->
        if (uri != null) {
            val incoming = ConfigFile.loadConnections(context, uri)
            notice = if (incoming.isNullOrEmpty()) {
                context.getString(R.string.config_load_failed)
            } else {
                val r = Settings.mergeConnections(context, incoming)
                store = r.store
                // Say what happened to every connection in the file. A
                // user who exported five and sees one needs to know
                // whether the file or the app dropped the rest.
                if (r.dropped > 0)
                    context.getString(
                        R.string.config_loaded_dropped,
                        r.added + r.updated, r.store.current.mountpoint, r.dropped)
                else
                    context.getString(
                        R.string.config_loaded_n,
                        r.added + r.updated, r.store.current.mountpoint)
            }
        }
    }

    val saveConfig = rememberLauncherForActivityResult(
        ActivityResultContracts.CreateDocument("application/json")
    ) { uri ->
        if (uri != null) {
            // One format for everything: a list, even when there is one
            // connection in it. See docs/jsonConfigs.md.
            val n = store.profiles.count { it.isComplete }
            notice = if (ConfigFile.saveConnections(context, uri, store))
                context.getString(R.string.config_saved_n, n)
            else context.getString(R.string.config_save_failed)
        }
    }

    // The app never downloads a navigation file: the user obtains it and
    // so holds the relationship with the data provider, including its
    // licence and usage rules (android/design/views.md).
    val scope = rememberCoroutineScope()
    val pickRinex = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri ->
        if (uri != null) scope.launch {
            // Copying and parsing a couple of megabytes is not main-thread
            // work, and the result is worth waiting for: an import that
            // silently produced nothing was indistinguishable from one
            // that worked until a run came up empty an hour later.
            val result: Triple<String?, Int, Long> = withContext(Dispatchers.IO) {
                val name = Settings.stageRinex(context, uri)
                if (name == null) {
                    Triple(null, 0, 0L)
                } else {
                    // Checked where it was staged, and promoted only if it
                    // carries orbits: a file that turns out to be the
                    // wrong one must not cost the user the one that works.
                    val n = NtripBridge.loadNavFile(
                        Settings.stagedRinexFile(context).absolutePath
                    )
                    // Asked while the file is the one just read: the date
                    // belongs to that load and the next one overwrites it.
                    val utc = if (n > 0) NtripBridge.navFileNewestUtc() else 0L
                    if (n > 0) Settings.commitRinex(context, name, utc)
                    else Settings.discardStagedRinex(context)
                    Triple(name, n, utc)
                }
            }
            val (name, records, newestUtc) = result
            notice = when {
                name == null ->
                    context.getString(R.string.rinex_unreadable)
                records <= 0 ->
                    context.getString(R.string.rinex_no_orbits, name)
                else -> {
                    rinexName = name
                    MonitorService.rinexPath =
                        Settings.rinexFile(context).absolutePath
                    rinexAgeS = Settings.rinexAgeS(context)
                    // An import that worked and an import that is useless
                    // look identical otherwise: the file reads, the count
                    // is large, and nothing can be placed from any of it
                    // because every record is outside the four-hour
                    // window. Say so at the moment it can still be fixed.
                    val age = if (newestUtc > 0L)
                        (System.currentTimeMillis() / 1000.0) - newestUtc
                    else null
                    if (age != null && age > Settings.RINEX_FRESH_S)
                        context.getString(R.string.rinex_loaded_stale, name,
                                          records, ageShort(age))
                    else
                        context.getString(R.string.rinex_loaded, name, records)
                }
            }
        }
    }

    LaunchedEffect(Unit) {
        if (Settings.rinexFile(context).exists()) {
            MonitorService.rinexPath = Settings.rinexFile(context).absolutePath

            // A file imported before this app knew to record its date has
            // no date, and an unknown age would leave the badge unable to
            // say anything about a file plainly sitting there. The date
            // is in the file, so read it: one parse, once, off the main
            // thread, and never again for that import.
            if (Settings.rinexAgeS(context) == null) {
                withContext(Dispatchers.IO) {
                    val path = Settings.rinexFile(context).absolutePath
                    if (NtripBridge.loadNavFile(path) > 0) {
                        Settings.setRinexNewestUtc(
                            context, NtripBridge.navFileNewestUtc())
                    }
                }
                rinexAgeS = Settings.rinexAgeS(context)
            }
        }
    }

    val askLocation = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        haveLocation = granted
        if (granted) nav.push(Dest.Analysis)
    }

    LaunchedEffect(openSky) {
        if (!openSky) return@LaunchedEffect
        openSky = false
        if (haveLocation) nav.push(Dest.Analysis)
        else askLocation.launch(Manifest.permission.ACCESS_FINE_LOCATION)
    }

    DisposableEffect(haveLocation) {
        if (haveLocation) phoneGnss.start()
        onDispose { phoneGnss.stop() }
    }

    // Permission may have been granted inside the settings dialog, which
    // owns its own launcher; without this the receiver would not start
    // until the next launch and a live uplink would silently send the
    // fixed position instead.
    // The overflow menu, wired once. This screen owns the state and
    // the file pickers behind it; the analysis and detail screens are
    // handed the same object, which is what makes the menu identical
    // everywhere rather than merely similar.
    val menuActions = remember {
        MenuActions(
            settings = { showSettings = true },
            importRinex = { pickRinex.launch(arrayOf("*/*")) },
            loadConfig = { pickConfig.launch(arrayOf("application/json", "*/*")) },
            saveConfig = { saveConfig.launch("config.json") },
            about = { showAbout = true },
        )
    }

    LaunchedEffect(showSettings) {
        if (!showSettings) {
            haveLocation = hasLocationPermission(context)
            ggaConsent = Settings.liveGgaConsent(context)
        }
    }

    // The one place the phone's position is offered to a run. Null means
    // "do not report it", and a run then keeps sending the configured
    // position -- so revoking consent, losing the fix or leaving the
    // screen all fall back the same way, within one uplink interval.
    //
    // While the app is off screen Android stops delivering location to a
    // dataSync foreground service, so the last position stands until the
    // user returns. That is the fallback working, not a stale reading
    // dressed up as a live one: the alternative is a location-typed
    // service tracking the user in the background, which this app will
    // not do to answer a coverage question.
    val liveGgaOn = Features.HAS_LIVE_GGA && settings.ggaLive && ggaConsent
    DisposableEffect(fix, liveGgaOn) {
        MonitorService.livePosition = if (liveGgaOn) fix else null
        onDispose { MonitorService.livePosition = null }
    }

    // Satellites the stream measured, joined to a position where one
    // exists. Free shows the session mean, pro the live value.
    val liveDoc = runState.document

    /**
     * Whether the numbers on screen are being measured right now.
     *
     * This governs live-versus-session-mean C/N0, and it follows the
     * *run*, not the edition. Keying it on IS_PRO meant free and pro
     * showed different numbers for the very same finished capture --
     * pro the last epoch, free the mean over the whole of it -- so two
     * people looking at one station disagreed for no reason. Both now
     * show live while measuring and the capture mean once stopped; what
     * pro buys is being able to keep measuring, not different arithmetic.
     */
    val live = runState.running
    // Positions come from the station's own orbits where they exist --
    // exact, complete, and independent of the handset -- and from the
    // phone only for satellites no orbit covers. Preferring the phone
    // would discard the better source: the orbit cache placed 47 of 47
    // satellites where the phone managed 23.
    var usedOrbits by remember { mutableStateOf(0) }
    val plotted = remember(liveDoc, positions, live) {
        var fromOrbit = 0
        val out = liveDoc?.sats.orEmpty().mapNotNull { sat ->
            val az: Float
            val el: Float
            val a = sat.az
            val e = sat.el
            if (a != null && e != null) {
                az = a; el = e; fromOrbit++
            } else {
                val pos = positions[PhoneGnss.key(sat.gnss, sat.prn)]
                    ?: return@mapNotNull null
                az = pos.azimuthDeg; el = pos.elevationDeg
            }
            // Below the horizon the station cannot see it, whatever the
            // stream carries; a marker there would fall outside the
            // plot's own geometry.
            if (el < 0f) return@mapNotNull null
            PlottedSat(
                gnss = sat.gnss, prn = sat.prn,
                cn0 = if (live) sat.cn0 else sat.cn0Mean,
                azimuthDeg = az, elevationDeg = el,
            )
        }
        usedOrbits = fromOrbit
        out
    }

    // Signal quality needs no position at all: C/N0 comes from the
    // stream. Feeding it the positioned subset dropped satellites the
    // base was hearing perfectly well, which understates the station.
    val signal = remember(liveDoc, live) {
        liveDoc?.sats.orEmpty().map { sat ->
            SignalSat(
                gnss = sat.gnss, prn = sat.prn,
                cn0 = if (live) sat.cn0 else sat.cn0Mean,
            )
        }
    }

    // The elevation scatter accumulates over the session. Elevation comes
    // from the phone and C/N0 from the stream, so the join -- and so the
    // accumulation -- can only happen here.
    //
    // Counted into the plot's own cells rather than kept as samples: see
    // ElevationAccumulator. `elevRevision` is what the view observes,
    // because the accumulator is a plain object and Compose cannot see
    // inside it.
    val elevSamples = remember { ElevationAccumulator() }
    var elevRevision by remember { mutableStateOf(0) }

    // Where each satellite has been, for the trails pro draws behind
    // them. Fed from the same list the plot draws, so a trail can only
    // contain positions that were on screen (phase 2 item 1).
    val tracks = remember { TrackAccumulator() }
    var trackRevision by remember { mutableStateOf(0) }

    // Keyed on the document and gated on the run, not keyed on
    // `plotted`: keying on `plotted` re-fired every time the phone's
    // GNSS updated, so a stopped analysis went on adding its last
    // document's samples for ever -- the scatter grew while nothing was
    // measuring.
    // A plot belongs to one run.  Without this the scatter carried
    // samples from whatever was measured before -- a different caster,
    // a different antenna, a different sky -- into a view whose header
    // says "this session", and two stations' curves were drawn on top of
    // each other with nothing to say which was which.
    LaunchedEffect(runState.running) {
        if (runState.running) {
            elevSamples.clear()
            elevRevision++
            tracks.clear()
            trackRevision++
        }
    }

    LaunchedEffect(liveDoc, runState.running) {
        if (!runState.running) return@LaunchedEffect
        var added = false
        plotted.forEach { p ->
            if (p.cn0 > 0f) {
                elevSamples.add(p.gnss, p.elevationDeg, p.cn0)
                added = true
            }
        }
        if (added) elevRevision++

        // Trails keep their own clock: one point per satellite per
        // minute, however often the plot refreshes.
        if (Features.HAS_TRACKS) {
            val t = System.currentTimeMillis() / 1000.0
            var kept = false
            plotted.forEach { p ->
                if (tracks.offer(p.gnss, p.prn, p.azimuthDeg, p.elevationDeg, t))
                    kept = true
            }
            if (kept) trackRevision++
        }
    }

    // The system back key belongs to the app while a screen is open:
    // minimising from Analysis loses the user's place for no reason.
    BackHandler(enabled = nav.canGoBack) { nav.pop() }

    /*
     * Every move is a control, and every way back is Back.
     *
     * The swipe between the station and the analysis views is gone (GUI
     * v2, P1.7): the Analysis button opens the pager, a card with a
     * screen behind it opens on a tap, and the app bar's Back and the
     * system back key both pop the stack. One way in, one way out, the
     * same at every level -- where the gesture was a second, invisible
     * way that existed only between two particular screens and broke on
     * an Android release nobody could have predicted.
     */

    // One set of verbs for the hub and for every detail screen behind
    // it, so a panel is handed the same actions wherever it is drawn.
    val hubActions = HubActions(
        editConnection = {
            // One slot means the tile is a shortcut to its settings;
            // several mean it is the way to choose between them.
            if (Features.MAX_MOUNTPOINTS > 1) showPicker = true
            else showSettings = true
        },
        browseSourcetable = { showSourcetable = true },
        startCheck = { MonitorService.start(context, settings, watch = false) },
        stopRun = { MonitorService.stop(context) },
        openAnalysis = { openSky = true },
        openDetail = { nav.push(it) },
    )

    // A panel's own screen. Which panel is decided by the key in the
    // route, so the shell never learns what any of them contains -- and
    // a route whose panel this edition lacks simply matches nothing and
    // falls back to the hub.
    (nav.current as? Dest.Detail)?.let { d ->
        val panel = hubPanels.firstOrNull { it.destination() == d }
        if (panel == null) {
            nav.pop()
        } else {
            DetailScreen(
                panel = panel,
                state = HubState(runState, settings, rinexName, rinexAgeS,
                                 plotted.size - usedOrbits),
                actions = hubActions,
                onBack = { nav.pop() },
                menu = menuActions,
            )
            return
        }
    }

    if (nav.current == Dest.Analysis) {
        AnalysisScreen(
            doc = liveDoc,
            running = runState.running,
            plotted = plotted,
            usedOrbits = usedOrbits,
            signal = signal,
            elevSamples = elevSamples,
            elevRevision = elevRevision,
            haveLocation = haveLocation,
            rinexAgeS = rinexAgeS,
            tab = tab,
            onTab = { tab = it },
            onToggleWatch = {
                if (runState.running) MonitorService.stop(context)
                else MonitorService.start(context, settings, watch = true)
            },
            onLeave = { nav.pop() },
            menu = menuActions,
            // Free passes none, so its canvas draws exactly what it did.
            tracks = if (Features.HAS_TRACKS) tracks else null,
            trackRevision = trackRevision,
        )
        return
    }

    AppScaffold(
        // The report is text the panels wrote themselves, in the order
        // they appear below. Offered only once there is a run to
        // describe: sharing "nothing has been measured" helps nobody.
        onShare = {
            shareReport(
                context,
                buildReport(
                    hubPanels,
                    HubState(runState, settings, rinexName,
                             rinexAgeS, plotted.size - usedOrbits),
                    context.getString(R.string.share_header,
                                      BuildConfig.VERSION_NAME),
                ),
                context.getString(R.string.share_subject, settings.mountpoint),
                context.getString(R.string.share_chooser),
            )
        },
        shareEnabled = runState.document != null,
        menu = menuActions,
        // Pinned, so it is in the same place whatever the hub is
        // showing. Enabled while a run is live and afterwards for as
        // long as it left satellites behind; the permission dance stays
        // in openAnalysis, which is the only thing that knows the sky
        // view needs a position first.
        analysis = AnalysisBar(
            enabled = runState.running ||
                      runState.document?.sats?.isNotEmpty() == true,
            onOpen = { hubActions.openAnalysis() },
        ),
    ) { padding ->
        StationHub(
            panels = hubPanels,
            state = HubState(
                run = runState,
                settings = settings,
                rinexName = rinexName,
                rinexAgeS = rinexAgeS,
                phonePlaced = plotted.size - usedOrbits,
            ),
            actions = hubActions,
            modifier = Modifier
                .padding(padding)
                // Width, not size. `fillMaxSize` ahead of the scroll
                // hands the hub a minimum height of the whole viewport,
                // which made a short hub scrollable inside its own
                // slack: drag it and the cards left a band under the
                // title and another above the analysis bar. A hub is
                // as tall as what it holds, and scrolls only when that
                // is more than fits.
                .fillMaxWidth()
                .verticalScroll(rememberScrollState())
                // Inside the scroll, deliberately. Outside it the
                // margins are a frame the content can never fill: a
                // strip of nothing under the app bar and another above
                // the analysis bar, on every screen, for ever. Inside,
                // they are the first and last thing in the list --
                // breathing room at rest, and scrolled away when there
                // is more to read than fits.
                .padding(horizontal = 16.dp, vertical = 16.dp),
        )
    }

    if (showSourcetable) {
        SourcetableDialog(
            settings = settings,
            onDismiss = { showSourcetable = false },
            onPick = { e ->
                // The entry answers more than which mountpoint: whether
                // it wants a GGA uplink at all, and where it is. Taking
                // both is what makes "test as if standing at the
                // station" the default rather than a position the user
                // has to find and type.
                store = Settings.save(context, settings.copy(
                    mountpoint = e.mountpoint,
                    sendGga = e.nmea,
                    latitude = if (e.lat != 0.0 || e.lon != 0.0) e.lat
                               else settings.latitude,
                    longitude = if (e.lat != 0.0 || e.lon != 0.0) e.lon
                                else settings.longitude,
                ))
                showSourcetable = false
            },
        )
    }

    notice?.let { text ->
        AlertDialog(
            onDismissRequest = { notice = null },
            text = { Text(text) },
            confirmButton = {
                TextButton(onClick = { notice = null }) {
                    Text(stringResource(R.string.action_close))
                }
            },
        )
    }

    if (showAbout) {
        AboutDialog { showAbout = false }
    }

    if (showSettings) {
        SettingsDialog(
            initial = settings,
            // Where the last failure points, if it pointed anywhere.
            focus = failureFix(runState.document?.stats?.failure ?: Failure.NONE),
            onDismiss = { showSettings = false },
            onSave = {
                store = Settings.save(context, it)
                showSettings = false
            },
        )
    }

    if (showPicker) {
        ProfilePicker(
            store = store,
            onSelect = { store = Settings.selectProfile(context, it); showPicker = false },
            onEdit = { showPicker = false; showSettings = true },
            onAdd = {
                store = Settings.addProfile(context, CasterSettings())
                showPicker = false
                showSettings = true
            },
            onDelete = { store = Settings.removeProfile(context, it) },
            onDismiss = { showPicker = false },
        )
    }
}

/** Whether the phone's GNSS may be read for satellite positions. */
fun hasLocationPermission(context: android.content.Context): Boolean =
    ContextCompat.checkSelfPermission(
        context, Manifest.permission.ACCESS_FINE_LOCATION
    ) == PackageManager.PERMISSION_GRANTED

