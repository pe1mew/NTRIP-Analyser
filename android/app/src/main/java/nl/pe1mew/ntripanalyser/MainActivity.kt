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
import androidx.compose.foundation.gestures.Orientation
import androidx.compose.foundation.gestures.draggable
import androidx.compose.foundation.gestures.rememberDraggableState
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
import androidx.compose.ui.unit.sp
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
    val shareLabel = stringResource(R.string.action_share)
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
    var menuOpen by remember { mutableStateOf(false) }
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
    }

    // The system back key belongs to the app while a screen is open:
    // minimising from Analysis loses the user's place for no reason.
    BackHandler(enabled = nav.canGoBack) { nav.pop() }

    /*
     * Swiping between the screens, in both directions.
     *
     * The two screens are one sequence to the user -- the station, then
     * the three views of it -- so the phone's own idiom applies: swipe
     * left to go deeper, right to come back. The buttons and the tab row
     * stay exactly as they were; this is another way in, not a
     * replacement, and nothing is reachable only by gesture.
     *
     * The same threshold governs every edge, so the gesture feels the
     * same wherever it is made.
     */
    val swipePx = with(LocalDensity.current) { SwipeThreshold.toPx() }
    val analysisReachable = runState.running ||
        (liveDoc != null && liveDoc.sats.isNotEmpty())

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
        )
        return
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(stringResource(R.string.app_name)) },
                navigationIcon = {
                    // One way into everything that is not a measurement:
                    // settings, files, and where to get help. The main
                    // screen stays about the station under test.
                    IconButton(onClick = { menuOpen = true }) {
                        Text("☰", fontSize = 22.sp)
                    }
                    AppMenu(
                        open = menuOpen,
                        onDismiss = { menuOpen = false },
                        onSettings = { menuOpen = false; showSettings = true },
                        onImportRinex = {
                            menuOpen = false
                            pickRinex.launch(arrayOf("*/*"))
                        },
                        onLoadConfig = {
                            menuOpen = false
                            pickConfig.launch(arrayOf("application/json", "*/*"))
                        },
                        onSaveConfig = {
                            menuOpen = false
                            saveConfig.launch("config.json")
                        },
                        onAbout = { menuOpen = false; showAbout = true },
                    )
                },
                // The report is text the panels wrote themselves, in the
                // order they appear below. Offered only once there is a
                // run to describe: sharing "nothing has been measured"
                // helps nobody.
                actions = {
                    // A glyph, not an icon asset: the menu beside it is
                    // "☰" for the same reason, and neither is worth a
                    // dependency on the Material icon set. The label
                    // stays as the accessibility description, so a
                    // screen reader says "Share" rather than an arrow.
                    IconButton(
                        onClick = {
                            shareReport(
                                context,
                                buildReport(
                                    hubPanels,
                                    HubState(runState, settings, rinexName,
                                             rinexAgeS, plotted.size - usedOrbits),
                                    context.getString(R.string.share_header,
                                                      BuildConfig.VERSION_NAME),
                                ),
                                context.getString(R.string.share_subject,
                                                  settings.mountpoint),
                                context.getString(R.string.share_chooser),
                            )
                        },
                        enabled = runState.document != null,
                    ) {
                        Text(
                            // Larger than the menu's "☰": this glyph
                            // draws thin strokes in a corner of its box,
                            // so at the same point size it reads smaller
                            // than the bars do.
                            "⤴",
                            fontSize = 28.sp,
                            modifier = Modifier.semantics {
                                contentDescription = shareLabel
                            },
                        )
                    }
                },
            )
        }
    ) { padding ->
        // Horizontal only: the column scrolls vertically, and the two
        // gestures do not compete.  Nothing happens when there is
        // nothing to analyse yet -- the same condition that disables the
        // button, rather than a screen that opens onto an empty plot.
        var carried by remember { mutableStateOf(0f) }
        StationHub(
            panels = hubPanels,
            state = HubState(
                run = runState,
                settings = settings,
                rinexName = rinexName,
                rinexAgeS = rinexAgeS,
                phonePlaced = plotted.size - usedOrbits,
            ),
            actions = HubActions(
                editConnection = {
                    // One slot means the tile is a shortcut to its
                    // settings; several mean it is the way to choose
                    // between them.
                    if (Features.MAX_MOUNTPOINTS > 1) showPicker = true
                    else showSettings = true
                },
                browseSourcetable = { showSourcetable = true },
                startCheck = { MonitorService.start(context, settings, watch = false) },
                stopRun = { MonitorService.stop(context) },
                openAnalysis = { openSky = true },
            ),
            modifier = Modifier
                .padding(padding)
                .padding(16.dp)
                .fillMaxSize()
                .draggable(
                    state = rememberDraggableState { carried += it },
                    orientation = Orientation.Horizontal,
                    onDragStopped = {
                        if (carried < -swipePx && analysisReachable) openSky = true
                        carried = 0f
                    },
                )
                .verticalScroll(rememberScrollState()),
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

