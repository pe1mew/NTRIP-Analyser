package nl.pe1mew.ntripanalyser

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import androidx.core.content.ContextCompat
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
    MaterialTheme(colorScheme = if (isSystemInDarkThemeSafe()) darkColorScheme() else lightColorScheme()) {
        content()
    }
}

@Composable
private fun isSystemInDarkThemeSafe(): Boolean =
    androidx.compose.foundation.isSystemInDarkTheme()

/** Verdict colours, matching the desktop's green / amber / red idiom. */
private fun verdictColour(v: Verdict): Color = when (v) {
    Verdict.PASS -> Color(0xFF46AF5A)
    Verdict.WARN -> Color(0xFFE6A014)
    Verdict.FAIL -> Color(0xFFD72828)
    Verdict.PENDING -> Color(0xFF9E9E9E)
}

private fun runColour(v: RunVerdict): Color = when (v) {
    RunVerdict.OK -> Color(0xFF46AF5A)
    RunVerdict.CAUTION -> Color(0xFFE6A014)
    RunVerdict.FAILED -> Color(0xFFD72828)
    RunVerdict.RUNNING -> Color(0xFF5A7DAF)
}

/** Which full-screen destination is showing. */
/**
 * The two modes.
 *
 * **Station** is the sixty-second check: does this station meet the
 * basic KPIs? **Analysis** is everything else -- what is it actually
 * doing? Free renders analysis from what the station check captured,
 * frozen at its end; pro runs analysis as a session of its own, started
 * and stopped at will, and that session is what watch mode measures.
 */
enum class Screen { STATION, ANALYSIS }

/** Which analysis view is showing. */
enum class AnalysisTab { SKY, SIGNAL, ELEVATION }

@OptIn(ExperimentalMaterial3Api::class)
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
    var screen by remember { mutableStateOf(Screen.STATION) }
    var tab by remember { mutableStateOf(AnalysisTab.SKY) }

    // Positions come from the phone's own GNSS -- what both editions
    // share, and all the free edition has. Permission is asked when the
    // sky view is first opened, never at launch.
    val phoneGnss = remember { PhoneGnss(context) }
    val positions by phoneGnss.positions.collectAsStateWithLifecycle(emptyMap())
    var haveLocation by remember { mutableStateOf(hasLocationPermission(context)) }
    var openSky by remember { mutableStateOf(false) }
    var rinexName by remember { mutableStateOf(Settings.rinexName(context)) }
    var menuOpen by remember { mutableStateOf(false) }
    var showAbout by remember { mutableStateOf(false) }
    var notice by remember { mutableStateOf<String?>(null) }

    // The same config.json the CLI and the GUI read and write, so a
    // configuration moves between desktop and phone unchanged.
    val pickConfig = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri ->
        if (uri != null) {
            val cfg = ConfigFile.load(context, uri)
            notice = if (cfg == null) {
                context.getString(R.string.config_load_failed)
            } else {
                // Name the mountpoint just loaded, not the one replaced:
                // `settings` is derived from the store as it was when
                // this composition ran, so it still holds the old value.
                val next = cfg.toSettings(settings)
                store = Settings.save(context, next)
                context.getString(R.string.config_loaded, next.mountpoint)
            }
        }
    }

    // The daemon's format, not the desktop's: a list rather than one
    // connection. docs/jsonConfigs.md sets out which is which.
    val exportAll = rememberLauncherForActivityResult(
        ActivityResultContracts.CreateDocument("application/json")
    ) { uri ->
        if (uri != null) {
            val n = store.profiles.count { it.isComplete }
            notice = if (ConfigFile.exportAll(context, uri, store))
                context.getString(R.string.export_all_ok, n)
            else context.getString(R.string.export_all_failed)
        }
    }

    val saveConfig = rememberLauncherForActivityResult(
        ActivityResultContracts.CreateDocument("application/json")
    ) { uri ->
        if (uri != null) {
            notice = if (ConfigFile.save(context, uri, settings))
                context.getString(R.string.config_saved)
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
            val result = withContext(Dispatchers.IO) {
                val name = Settings.stageRinex(context, uri)
                if (name == null) {
                    null to 0
                } else {
                    // Checked where it was staged, and promoted only if it
                    // carries orbits: a file that turns out to be the
                    // wrong one must not cost the user the one that works.
                    val n = NtripBridge.loadNavFile(
                        Settings.stagedRinexFile(context).absolutePath
                    )
                    if (n > 0) Settings.commitRinex(context, name)
                    else Settings.discardStagedRinex(context)
                    name to n
                }
            }
            val (name, records) = result
            notice = when {
                name == null ->
                    context.getString(R.string.rinex_unreadable)
                records <= 0 ->
                    context.getString(R.string.rinex_no_orbits, name)
                else -> {
                    rinexName = name
                    MonitorService.rinexPath =
                        Settings.rinexFile(context).absolutePath
                    context.getString(R.string.rinex_loaded, name, records)
                }
            }
        }
    }

    LaunchedEffect(Unit) {
        if (Settings.rinexFile(context).exists()) {
            MonitorService.rinexPath = Settings.rinexFile(context).absolutePath
        }
    }

    val askLocation = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        haveLocation = granted
        if (granted) screen = Screen.ANALYSIS
    }

    LaunchedEffect(openSky) {
        if (!openSky) return@LaunchedEffect
        openSky = false
        if (haveLocation) screen = Screen.ANALYSIS
        else askLocation.launch(Manifest.permission.ACCESS_FINE_LOCATION)
    }

    DisposableEffect(haveLocation) {
        if (haveLocation) phoneGnss.start()
        onDispose { phoneGnss.stop() }
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
    val elevSamples = remember { mutableStateListOf<ElevationSample>() }

    // Keyed on the document and gated on the run, not keyed on
    // `plotted`: keying on `plotted` re-fired every time the phone's
    // GNSS updated, so a stopped analysis went on adding its last
    // document's samples for ever -- the scatter grew while nothing was
    // measuring.
    LaunchedEffect(liveDoc, runState.running) {
        if (!runState.running) return@LaunchedEffect
        plotted.forEach { p ->
            if (p.cn0 > 0f) {
                elevSamples.add(ElevationSample(p.gnss, p.elevationDeg, p.cn0))
            }
        }
        // A long analysis would grow this without bound.
        while (elevSamples.size > 20000) elevSamples.removeAt(0)
    }

    // The system back key belongs to the app while a screen is open:
    // minimising from Analysis loses the user's place for no reason.
    BackHandler(enabled = screen != Screen.STATION) { screen = Screen.STATION }

    if (screen == Screen.ANALYSIS) {
        val footer = liveDoc?.stats?.let { st ->
            buildString {
                append(st.mountpoint)
                if (st.arpValid && st.arpLat != null && st.arpLon != null) {
                    append("  ARP: %.6f, %.6f".format(st.arpLat, st.arpLon))
                }
            }
        }.orEmpty()

        Scaffold(
            topBar = {
                TopAppBar(
                    title = { Text(stringResource(R.string.mode_analysis)) },
                    navigationIcon = {
                        TextButton(onClick = { screen = Screen.STATION }) {
                            Text(stringResource(R.string.action_back))
                        }
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
                        Button(onClick = {
                            if (runState.running) MonitorService.stop(context)
                            else MonitorService.start(context, settings, watch = true)
                        }) {
                            Text(stringResource(
                                if (runState.running) R.string.action_stop
                                else R.string.action_analyse
                            ))
                        }
                        Spacer(Modifier.width(12.dp))
                        liveDoc?.watch?.let { w ->
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
                            onClick = { tab = t },
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

                Box(Modifier.weight(1f)) {
                    when (tab) {
                        AnalysisTab.SKY -> SkyView(
                            sats = plotted,
                            missing = ((liveDoc?.sats?.size ?: 0) - plotted.size)
                                .coerceAtLeast(0),
                            source = when {
                                usedOrbits > 0 && MonitorService.usedEphStream ->
                                    PositionSource.EPHEMERIS
                                usedOrbits > 0 -> PositionSource.RINEX
                                haveLocation -> PositionSource.PHONE_GNSS
                                else -> PositionSource.NONE
                            },
                            footer = footer,
                        )
                        AnalysisTab.SIGNAL ->
                            SignalBars(signal, liveValues = live)
                        AnalysisTab.ELEVATION -> ElevationView(elevSamples.toList())
                    }
                }
            }
        }
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
                        onExportAll = {
                            menuOpen = false
                            exportAll.launch("monitord.json")
                        },
                        onSaveConfig = {
                            menuOpen = false
                            saveConfig.launch("config.json")
                        },
                        onAbout = { menuOpen = false; showAbout = true },
                    )
                },
            )
        }
    ) { padding ->
        Column(
            Modifier
                .padding(padding)
                .padding(16.dp)
                .fillMaxSize()
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            val doc = runState.document

            VerdictBadge(doc, runState.running, runState.outcome, settings.isComplete)

            // What this run is pointed at, always: a measurement whose
            // subject is off screen is a measurement of nothing in
            // particular, and hiding it mid-run made the one tap into
            // the caster settings disappear with it. Tapping opens them.
            // One slot means the tile is a shortcut to its settings;
            // several mean it is the way to choose between them.
            ConfigSummary(settings) {
                if (Features.MAX_MOUNTPOINTS > 1) showPicker = true
                else showSettings = true
            }

            if (settings.caster.isNotBlank() && !runState.running) {
                OutlinedButton(
                    onClick = { showSourcetable = true },
                    modifier = Modifier.fillMaxWidth(),
                ) { Text(stringResource(R.string.action_browse)) }
            }

            doc?.let { StreamChips(it) }

            runState.error?.let {
                Text(it, color = MaterialTheme.colorScheme.error)
            }

            doc?.watch?.let { WatchCard(it) }

            doc?.kpi?.items?.forEachIndexed { i, item ->
                KpiRow(i + 1, item, doc.stats, doc.arp)
            }

            Spacer(Modifier.height(4.dp))

            // Two ways to run, chosen at the moment of running: grade the
            // station once, or watch it.  Neither is a caster setting.
            if (runState.running) {
                // Analysis must stay reachable during a run: watching a
                // check unfold is the point of having the views, and
                // hiding the way in until it finishes made a running
                // test unobservable.
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(
                        onClick = { MonitorService.stop(context) },
                        modifier = Modifier.weight(1f),
                    ) { Text(stringResource(R.string.action_stop)) }
                    OutlinedButton(
                        onClick = { openSky = true },
                        modifier = Modifier.weight(1f),
                    ) { Text(stringResource(R.string.mode_analysis)) }
                }
            } else {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(
                        onClick = { MonitorService.start(context, settings, watch = false) },
                        enabled = settings.isComplete,
                        modifier = Modifier.weight(1f),
                    ) {
                        Text(stringResource(
                            if (doc != null) R.string.action_again else R.string.action_run
                        ))
                    }
                    // Analysis is a mode, not a second kind of run. In
                    // pro it starts its own session; in free it shows
                    // what this check captured.
                    OutlinedButton(
                        onClick = { openSky = true },
                        enabled = doc != null && doc.sats.isNotEmpty(),
                        modifier = Modifier.weight(1f),
                    ) { Text(stringResource(R.string.mode_analysis)) }
                }
            }

            // Where the orbits stand: incompleteness and age are shown,
            // never left implicit.
            if (!runState.running) {
                doc?.eph?.let {
                    EphCard(it, rinexName, phonePlaced = plotted.size - usedOrbits)
                }
            }



            if (!settings.isComplete) {
                Text(
                    stringResource(R.string.hint_configure),
                    style = MaterialTheme.typography.bodySmall,
                )
            }
        }
    }

    if (showSourcetable) {
        SourcetableDialog(
            settings = settings,
            onDismiss = { showSourcetable = false },
            onPick = { mp ->
                store = Settings.save(context, settings.copy(mountpoint = mp))
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

/**
 * Choose among the saved connections.
 *
 * It hangs off the configuration tile because the tile already names the
 * connection in use and already opened settings when tapped. The main
 * screen exists to show one verdict; a permanent control for something
 * touched once a week does not belong on it.
 */
@Composable
private fun ProfilePicker(
    store: ProfileStore,
    onSelect: (Int) -> Unit,
    onEdit: () -> Unit,
    onAdd: () -> Unit,
    onDelete: (Int) -> Unit,
    onDismiss: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.profiles_title)) },
        text = {
            Column {
                store.profiles.forEachIndexed { i, p ->
                    val isActive = i == store.activeIndex
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .clickable { onSelect(i) }
                            .padding(vertical = 8.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Column(Modifier.weight(1f)) {
                            Text(
                                p.label,
                                fontWeight = if (isActive) FontWeight.Bold
                                             else FontWeight.Normal,
                            )
                            Text(
                                if (p.isComplete) "${p.caster}:${p.port}"
                                else stringResource(R.string.profiles_empty),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                fontFamily = FontFamily.Monospace,
                            )
                        }
                        if (isActive) {
                            TextButton(onClick = onEdit) {
                                Text(stringResource(R.string.action_edit))
                            }
                        }
                        // The last connection is emptied rather than
                        // removed: an app with none at all has nowhere to
                        // put the next one.
                        if (store.profiles.size > 1) {
                            TextButton(onClick = { onDelete(i) }) {
                                Text(stringResource(R.string.action_delete))
                            }
                        }
                    }
                }
            }
        },
        confirmButton = {
            if (store.profiles.size < Features.MAX_MOUNTPOINTS) {
                TextButton(onClick = onAdd) {
                    Text(stringResource(R.string.action_add_profile))
                }
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text(stringResource(R.string.action_close))
            }
        },
    )
}

@Composable
private fun VerdictBadge(
    doc: BridgeDocument?,
    running: Boolean,
    outcome: MonitorService.Outcome,
    configured: Boolean,
) {
    val verdict = doc?.kpi?.overallEnum ?: RunVerdict.RUNNING
    // "READY" is a claim, and an app with no mountpoint is not ready for
    // anything.  Saying so invites the user to tap the settings card
    // rather than to wonder why the run button does nothing.
    val label = when {
        doc != null -> doc.kpi.overallName
        !configured -> stringResource(R.string.verdict_unconfigured)
        else -> stringResource(R.string.verdict_idle)
    }

    Surface(
        color = if (doc == null && !configured) Color(0xFF9E9E9E)
                else runColour(verdict),
        shape = RoundedCornerShape(12.dp),
        modifier = Modifier.fillMaxWidth(),
    ) {
        Column(Modifier.padding(20.dp), horizontalAlignment = Alignment.CenterHorizontally) {
            Text(
                label,
                color = Color.White,
                fontWeight = FontWeight.Bold,
                fontSize = 26.sp,
            )
            doc?.kpi?.let { k ->
                Spacer(Modifier.height(8.dp))
                Text(
                    // Once the window is met the countdown has served its
                    // purpose; a watch showing "101 of 60 s" reads as a
                    // fault in the counter.
                    if (k.sustainedS >= k.sustainTargetS)
                        stringResource(R.string.sustained_held, dur(k.sustainedS))
                    else
                        stringResource(
                            R.string.sustained_of,
                            k.sustainedS.toInt(), k.sustainTargetS.toInt(),
                        ),
                    color = Color.White,
                    fontSize = 13.sp,
                )
                Spacer(Modifier.height(8.dp))
                if (running) {
                    LinearProgressIndicator(
                        progress = { k.sustainFraction },
                        modifier = Modifier.fillMaxWidth(),
                        color = Color.White,
                    )
                } else if (outcome == MonitorService.Outcome.RUNNING) {
                    Text(
                        stringResource(R.string.run_watching),
                        color = Color.White,
                        fontSize = 13.sp,
                    )
                } else {
                    // "Finished" and "stopped" are deliberately different
                    // words: a run cut short did not measure what a run
                    // that reached its verdict measured.
                    Text(
                        stringResource(
                            when (outcome) {
                                MonitorService.Outcome.STOPPED -> R.string.run_stopped
                                MonitorService.Outcome.LIMIT_REACHED -> R.string.run_limit
                                else -> R.string.run_finished
                            },
                            k.elapsedS.toInt(),
                        ),
                        color = Color.White,
                        fontSize = 13.sp,
                    )
                }
            }
        }
    }
}

/**
 * The configured target, shown on the main screen.
 *
 * The password is deliberately never displayed -- only whether one is
 * set. A screen someone may hold up to show a colleague, or photograph
 * for a report, should not carry a credential.
 */
@Composable
private fun ConfigSummary(s: CasterSettings, onEdit: () -> Unit) {
    Card(
        Modifier
            .fillMaxWidth()
            .clickable { onEdit() }
    ) {
        Column(Modifier.padding(12.dp)) {
            if (!s.isComplete) {
                Text(
                    stringResource(R.string.config_none),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                return@Column
            }
            Text(
                "${s.caster}:${s.port}",
                style = MaterialTheme.typography.bodyMedium,
                fontFamily = FontFamily.Monospace,
            )
            Text(
                s.mountpoint,
                fontWeight = FontWeight.Bold,
                fontFamily = FontFamily.Monospace,
            )
            Text(
                if (s.user.isBlank()) stringResource(R.string.config_anon)
                else stringResource(
                    R.string.config_user,
                    s.user,
                    stringResource(
                        if (s.password.isBlank()) R.string.config_nopw
                        else R.string.config_pw
                    ),
                ),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            if (s.sendGga) {
                Text(
                    stringResource(R.string.config_gga, s.latitude, s.longitude),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}

@Composable
private fun StreamChips(doc: BridgeDocument) {
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        AssistChip(
            onClick = {},
            label = { Text("${doc.stats.bytesPerS?.toInt() ?: 0} B/s") },
        )
        AssistChip(
            onClick = {},
            label = { Text("${doc.stats.satsTotal} SV") },
        )
        if (doc.stats.mountpoint.isNotBlank()) {
            AssistChip(onClick = {}, label = { Text(doc.stats.mountpoint) })
        }
    }
}

/**
 * The evidence behind one KPI's verdict.
 *
 * The verdict line says *what* was decided; these lines say *from what*.
 * They are read straight out of the snapshot the C engine judged, so the
 * user is looking at the same numbers the verdict came from rather than
 * a second, possibly disagreeing, calculation.
 */
private fun evidenceFor(index: Int, s: Stats, arp: ArpInfo? = null): List<Pair<String, String>> {
    fun f(v: Double?, unit: String, dp: Int = 1) =
        if (v == null) "not measured" else "%.${dp}f %s".format(v, unit).trim()

    return when (index) {
        1 -> listOf(
            "Connected" to if (s.connected) "yes" else "no",
            "Throughput" to f(s.bytesPerS, "B/s", 0),
            "Received" to "%.1f kB".format(s.bytesTotal / 1024.0),
            "Uptime" to f(s.uptimeS, "s", 0),
            "Reconnects" to "${s.reconnects}",
        )
        2 -> buildList {
            add("CRC-valid frames" to "${s.framesOk}")
            add("Message types seen" to "${s.types.size}")
            add("Latency" to f(s.latencyS, "s", 2))
            // The types themselves, not merely how many: knowing a base
            // sends 1077/1087/1097/1127 and no 1005 is the difference
            // between a diagnosis and a number.
            if (Features.IS_PRO && s.types.isNotEmpty()) {
                add("Types received" to
                    s.types.map { it.type }.sorted().joinToString(", "))
            }
        }
        3 -> buildList {
            add("ARP received" to if (s.arpValid) "yes" else "no")
            add("Latitude" to (s.arpLat?.let { "%.6f°".format(it) } ?: "—"))
            add("Longitude" to (s.arpLon?.let { "%.6f°".format(it) } ?: "—"))
            add("Height" to f(s.arpAlt, "m", 2))
            // Everything else the message states. Only in pro, and only
            // when a 1005/1006 has actually been decoded.
            if (Features.IS_PRO && arp != null) {
                add("Message" to "RTCM ${arp.msg}")
                add("Station ID" to "${arp.stationId}")
                add("ITRF year" to
                    if (arp.itrfYear > 0) "20%02d".format(arp.itrfYear) else "not stated")
                add("Advertises" to arp.serves)
                add("Station type" to
                    if (arp.reference) "reference station" else "receiver, own position")
                add("Oscillator" to if (arp.singleOsc) "single" else "not single")
                add("ECEF X" to "%.4f m".format(arp.x))
                add("ECEF Y" to "%.4f m".format(arp.y))
                add("ECEF Z" to "%.4f m".format(arp.z))
                arp.antennaHeight?.let {
                    add("Antenna height" to "%.4f m".format(it))
                }
            }
        }
        4 -> s.types.filter { it.type in 1071..1237 && (it.type % 10) in 4..7 }
                .sortedBy { it.type }
                .map { t ->
                    "MSM ${t.type}" to
                        (t.avgDt?.let { "%.1f s (%d epochs)".format(it, t.epochs) }
                            ?: "${t.epochs} epochs")
                }
                .ifEmpty { listOf("MSM messages" to "none seen") }
        5 -> s.gnss.filter { it.satsTracked > 0 }
                .map { it.label to "${it.satsTracked} SV" }
                .plus("Total" to "${s.satsTotal} SV")
        6 -> s.gnss.filter { (it.cnrMedian ?: 0.0) > 0.0 }
                .map { g ->
                    g.label to "median %.1f, min %.1f dB-Hz".format(
                        g.cnrMedian ?: 0.0, g.cnrMin ?: 0.0)
                }
                .ifEmpty { listOf("C/N0" to "not carried (MSM7 required)") }
                .plus("All constellations" to f(s.cnrMeanAll, "dB-Hz mean"))
        7 -> listOf(
            "Frames checked" to "${s.framesOk + s.framesCrcError}",
            "CRC failures" to "${s.framesCrcError}",
            "Error rate" to (s.crcErrorRate?.let { "%.4f%%".format(it * 100) } ?: "—"),
        )
        8 -> buildList {
            if (!s.advertisedKnown) {
                add("Sourcetable" to "no entry for this mountpoint")
                return@buildList
            }
            add("Types advertised" to "${s.advertisedCount}")
            add("Not being sent" to "${s.typesMissing}")
            add("Off advertised rate" to "${s.typesOffrate}")
            add("Sent but not advertised" to "${s.typesExtra}")

            // The sourcetable's NavSys field is the actual
            // advertisement, and what the verdict is judged against.
            val advertised = (1..7).filter { (s.advertisedGnss shr it) and 1 == 1 }
                .joinToString("+") { Gnss.label(it) }
            if (advertised.isNotEmpty()) {
                add("Sourcetable advertises" to advertised)
            }
            val streaming = s.gnss.filter { it.satsTracked > 0 }
                .joinToString("+") { Gnss.label(it.gnssId) }
            if (streaming.isNotEmpty()) add("Streaming now" to streaming)

            // The systems the station claims against the ones it sends.
            //
            // Only three are comparable: 1005/1006 carries indicator
            // bits for GPS, GLONASS and Galileo and for nothing else, so
            // a station streaming BeiDou cannot advertise it there.
            // Listing those alongside as though they were undeclared
            // would show a mismatch the verdict rightly ignores.
            if (arp != null) {
                val comparable = setOf(Gnss.GPS, Gnss.GLONASS, Gnss.GALILEO)
                val streamed = s.gnss.filter { it.satsTracked > 0 }
                val shown = streamed.filter { it.gnssId in comparable }
                    .joinToString("+") { Gnss.label(it.gnssId) }
                val others = streamed.filterNot { it.gnssId in comparable }
                    .joinToString("+") { Gnss.label(it.gnssId) }

                add("1005/1006 states" to arp.serves)
                add("Streams, of those" to (shown.ifEmpty { "none" }))
                if (others.isNotEmpty()) {
                    add("Also streams" to "$others (1005/1006 cannot state these)")
                }
            }
        }
        else -> emptyList()
    }
}

@Composable
private fun KpiRow(index: Int, item: KpiItem, stats: Stats, arp: ArpInfo? = null) {
    var expanded by remember { mutableStateOf(false) }

    Card(
        Modifier
            .fillMaxWidth()
            .clickable { expanded = !expanded }
    ) {
        Row(
            Modifier.padding(12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Surface(
                color = verdictColour(item.verdictEnum),
                shape = RoundedCornerShape(6.dp),
            ) {
                Text(
                    item.verdictName,
                    color = Color.White,
                    fontSize = 11.sp,
                    fontWeight = FontWeight.Bold,
                    fontFamily = FontFamily.Monospace,
                    modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp),
                )
            }
            Spacer(Modifier.width(12.dp))
            Column(Modifier.weight(1f)) {
                Text("$index. ${item.label}", fontWeight = FontWeight.Medium)
                Text(
                    item.detail,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            Text(
                if (expanded) "▴" else "▾",
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }

        if (expanded) {
            Column(
                Modifier.padding(start = 12.dp, end = 12.dp, bottom = 12.dp),
            ) {
                HorizontalDivider()
                Spacer(Modifier.height(8.dp))
                val rows = evidenceFor(index, stats, arp)
                if (rows.isEmpty()) {
                    Text(
                        stringResource(R.string.no_evidence),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                rows.forEach { (k, v) ->
                    // A long value -- a list of message types, say --
                    // squeezes the label to nothing when both share a
                    // row, so it takes a line of its own instead. The
                    // label is what makes the value mean something.
                    if (v.length > 20) {
                        Column(Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
                            Text(
                                k,
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                            Text(
                                v,
                                style = MaterialTheme.typography.bodySmall,
                                fontFamily = FontFamily.Monospace,
                            )
                        }
                    } else {
                        Row(Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
                            Text(
                                k,
                                Modifier.weight(1f),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                            Text(
                                v,
                                style = MaterialTheme.typography.bodySmall,
                                fontFamily = FontFamily.Monospace,
                            )
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun SettingsDialog(
    initial: CasterSettings,
    onDismiss: () -> Unit,
    onSave: (CasterSettings) -> Unit,
) {
    var caster by remember { mutableStateOf(initial.caster) }
    var port by remember { mutableStateOf(initial.port.toString()) }
    var mountpoint by remember { mutableStateOf(initial.mountpoint) }
    var user by remember { mutableStateOf(initial.user) }
    var password by remember { mutableStateOf(initial.password) }
    var lat by remember { mutableStateOf(initial.latitude.toString()) }
    var lon by remember { mutableStateOf(initial.longitude.toString()) }
    var gga by remember { mutableStateOf(initial.sendGga) }
    var ephCaster by remember { mutableStateOf(initial.ephCaster) }
    var ephPort by remember { mutableStateOf(initial.ephPort.toString()) }
    var ephMp by remember { mutableStateOf(initial.ephMountpoint) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.settings_title)) },
        text = {
            Column(
                Modifier.verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                // A URI keyboard, so the IME stops "helping": with a text
                // keyboard EMUI inserts a space after every full stop and
                // the field reads "ntrip. kadaster. nl".  Filtering those
                // out as the user types is worse than it sounds -- it
                // fights the IME's composing region and duplicates
                // characters ("ntrip..kadaster.nl").  Choosing a keyboard
                // that never inserts them avoids the fight entirely;
                // save-time sanitising below still catches pasted text.
                OutlinedTextField(
                    caster,
                    { caster = it },
                    label = { Text(stringResource(R.string.field_caster)) },
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri),
                )
                OutlinedTextField(port, { port = it.filter(Char::isDigit) },
                    label = { Text(stringResource(R.string.field_port)) }, singleLine = true)
                OutlinedTextField(mountpoint, { mountpoint = it },
                    label = { Text(stringResource(R.string.field_mountpoint)) }, singleLine = true)
                OutlinedTextField(user, { user = it },
                    label = { Text(stringResource(R.string.field_user)) }, singleLine = true)
                OutlinedTextField(password, { password = it },
                    label = { Text(stringResource(R.string.field_password)) }, singleLine = true)
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Checkbox(gga, { gga = it })
                    Text(stringResource(R.string.field_gga))
                }

                if (gga) {
                    OutlinedTextField(lat, { lat = it },
                        label = { Text(stringResource(R.string.field_lat)) }, singleLine = true)
                    OutlinedTextField(lon, { lon = it },
                        label = { Text(stringResource(R.string.field_lon)) }, singleLine = true)
                }

                HorizontalDivider(Modifier.padding(vertical = 8.dp))
                Text(
                    stringResource(R.string.eph_explain),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                OutlinedTextField(
                    ephCaster, { ephCaster = it },
                    label = { Text(stringResource(R.string.field_eph_caster)) },
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri),
                )
                OutlinedTextField(ephPort, { ephPort = it.filter(Char::isDigit) },
                    label = { Text(stringResource(R.string.field_eph_port)) }, singleLine = true)
                OutlinedTextField(ephMp, { ephMp = it },
                    label = { Text(stringResource(R.string.field_eph_mp)) }, singleLine = true)
            }
        },
        confirmButton = {
            TextButton(onClick = {
                onSave(
                    CasterSettings(
                        // Belt and braces: the field filters as typed,
                        // this catches anything that arrives another way.
                        caster = caster.filter {
                            it.isLetterOrDigit() || it == '.' || it == '-'
                        },
                        port = port.toIntOrNull() ?: 2101,
                        mountpoint = mountpoint.trim(),
                        user = user,
                        password = password,
                        latitude = lat.toDoubleOrNull() ?: 52.0,
                        longitude = lon.toDoubleOrNull() ?: 6.0,
                        sendGga = gga,
                        ephCaster = ephCaster.filter {
                            it.isLetterOrDigit() || it == '.' || it == '-'
                        },
                        ephPort = ephPort.toIntOrNull() ?: 2101,
                        ephMountpoint = ephMp.trim(),
                    )
                )
            }) { Text(stringResource(R.string.action_save)) }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text(stringResource(R.string.action_cancel)) }
        },
    )
}

/** Compact duration: "45 s", "12 min", "3 h 07 m". */
private fun dur(seconds: Double): String {
    val s = seconds.toInt()
    return when {
        s < 90 -> "$s s"
        s < 5400 -> "${s / 60} min"
        else -> "%d h %02d m".format(s / 3600, (s % 3600) / 60)
    }
}

/**
 * The long-run picture, shown only while watching.
 *
 * A spot check answers "does it pass?"; these lines answer "does it keep
 * passing?" -- which a 90-second check cannot see at all.
 */
@Composable
private fun WatchCard(w: Watch) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp)) {
            Text(
                stringResource(R.string.watch_title),
                fontWeight = FontWeight.Bold,
            )
            Spacer(Modifier.height(6.dp))
            Text(stringResource(R.string.watch_for, dur(w.elapsedS)))
            w.availability?.let {
                Text(stringResource(R.string.watch_availability, "%.1f%%".format(it * 100)))
            }
            Text(stringResource(R.string.watch_streak, dur(w.streakS), dur(w.bestStreakS)))
            Text(
                if (w.degradations > 0)
                    stringResource(R.string.watch_drops, w.degradations, w.worstName)
                else
                    stringResource(R.string.watch_clean, w.worstName),
                color = if (w.degradations > 0) MaterialTheme.colorScheme.error
                        else MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

/**
 * A caster's mountpoint list.
 *
 * Fetching blocks on the network, so it runs on Dispatchers.IO; the
 * dialog shows a spinner until it returns.
 *
 * In the free edition the list is readable but inert: the mountpoint
 * must be typed into settings by hand. That is deliberate rather than
 * mean -- the information is what makes the free app useful, and the
 * workflow of picking among many mountpoints is what the paid edition
 * sells (android/design/editions.md).
 */
@Composable
private fun SourcetableDialog(
    settings: CasterSettings,
    onDismiss: () -> Unit,
    onPick: (String) -> Unit,
) {
    var entries by remember { mutableStateOf<List<SourceEntry>?>(null) }
    var error by remember { mutableStateOf<String?>(null) }
    var filter by remember { mutableStateOf("") }

    LaunchedEffect(settings.caster, settings.port) {
        val json = withContext(Dispatchers.IO) {
            NtripBridge.sourcetable(
                settings.caster, settings.port, settings.user, settings.password
            )
        }
        if (json == null) {
            error = "unreachable"
        } else {
            runCatching { bridgeJson.decodeFromString<Sourcetable>(json) }
                .onSuccess { entries = it.entries }
                .onFailure { error = it.message }
        }
    }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.sourcetable_title, settings.caster)) },
        text = {
            Column {
                when {
                    error != null -> Text(
                        stringResource(R.string.sourcetable_failed),
                        color = MaterialTheme.colorScheme.error,
                    )
                    entries == null -> Row(verticalAlignment = Alignment.CenterVertically) {
                        CircularProgressIndicator(Modifier.size(20.dp))
                        Spacer(Modifier.width(12.dp))
                        Text(stringResource(R.string.sourcetable_loading))
                    }
                    else -> {
                        val all = entries.orEmpty()
                        val shown = if (filter.isBlank()) all else all.filter {
                            it.mountpoint.contains(filter, true) ||
                                it.identifier.contains(filter, true)
                        }
                        OutlinedTextField(
                            filter, { filter = it },
                            label = { Text(stringResource(R.string.sourcetable_filter)) },
                            singleLine = true,
                            modifier = Modifier.fillMaxWidth(),
                        )
                        Spacer(Modifier.height(4.dp))
                        Text(
                            stringResource(R.string.sourcetable_count, shown.size, all.size),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                        if (!Features.SOURCETABLE_SELECTABLE) {
                            Text(
                                stringResource(R.string.sourcetable_readonly),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                        Spacer(Modifier.height(4.dp))
                        LazyColumn(Modifier.heightIn(max = 420.dp)) {
                            items(shown.size) { i -> SourceRow(shown[i], onPick) }
                        }
                    }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) { Text(stringResource(R.string.action_close)) }
        },
    )
}

@Composable
private fun SourceRow(e: SourceEntry, onPick: (String) -> Unit) {
    Column(
        Modifier
            .fillMaxWidth()
            .then(
                if (Features.SOURCETABLE_SELECTABLE)
                    Modifier.clickable { onPick(e.mountpoint) }
                else Modifier
            )
            .padding(vertical = 6.dp)
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                e.mountpoint,
                fontWeight = FontWeight.Bold,
                fontFamily = FontFamily.Monospace,
            )
            if (e.nmea) {
                Spacer(Modifier.width(6.dp))
                Text(
                    stringResource(R.string.sourcetable_gga),
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.primary,
                )
            }
        }
        Text(
            listOf(e.identifier, e.format, e.navSystems)
                .filter { it.isNotBlank() }
                .joinToString("  ·  "),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
    HorizontalDivider()
}

/** Whether the phone's GNSS may be read for satellite positions. */
fun hasLocationPermission(context: android.content.Context): Boolean =
    ContextCompat.checkSelfPermission(
        context, Manifest.permission.ACCESS_FINE_LOCATION
    ) == PackageManager.PERMISSION_GRANTED

/**
 * Where the orbits stand.
 *
 * A sky view drawn from partial or stale orbits looks exactly like one
 * drawn from fresh, complete ones, so the difference is stated here
 * rather than left for the user to infer from a sparse plot.
 */
@Composable
private fun EphCard(eph: EphState, rinexName: String?, phonePlaced: Int) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp)) {
            Text(
                stringResource(R.string.eph_title),
                fontWeight = FontWeight.Bold,
            )
            Spacer(Modifier.height(4.dp))

            // Coverage is worth stating once there are orbits to cover
            // with. With none at all -- the free edition's ordinary
            // state, having no ephemeris stream -- "0 of 41" in red
            // announces a fault that is not one; what the sky view is
            // actually drawing from is said below instead.
            val hasOrbits = eph.placeable > 0 || eph.cached > 0
            if (hasOrbits) {
                Text(
                    stringResource(R.string.eph_coverage, eph.placeable, eph.tracked, eph.cached),
                    style = MaterialTheme.typography.bodySmall,
                    color = if (eph.isComplete) MaterialTheme.colorScheme.onSurfaceVariant
                            else MaterialTheme.colorScheme.error,
                )
            }

            // This card counts orbits, and the sky view can also place a
            // satellite from the phone's own GNSS. Saying only the first
            // read as a contradiction: "0 of 41 can be placed" beside a
            // plot that was plainly placing them.
            if (phonePlaced > 0) {
                Text(
                    stringResource(
                        if (eph.placeable > 0) R.string.eph_from_phone
                        else R.string.eph_only_phone,
                        phonePlaced,
                    ),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }

            val age = eph.ageS
            Text(
                when {
                    // "cannot place anything" is only true when the phone
                    // is not placing them either; with no orbits but a
                    // populated plot the honest statement is which source
                    // drew it, not that nothing was drawn.
                    age == null && phonePlaced > 0 ->
                        stringResource(R.string.eph_none_phone)
                    age == null -> stringResource(R.string.eph_none)
                    age < 3600 -> stringResource(R.string.eph_age_min, (age / 60).toInt())
                    else -> stringResource(R.string.eph_age_hour, age / 3600.0)
                },
                style = MaterialTheme.typography.bodySmall,
                color = if (eph.isStale) MaterialTheme.colorScheme.error
                        else MaterialTheme.colorScheme.onSurfaceVariant,
            )

            Spacer(Modifier.height(4.dp))
            Text(
                rinexName?.let { stringResource(R.string.eph_file, it) }
                    ?: stringResource(R.string.eph_no_file),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

/**
 * Everything that is not a measurement.
 *
 * Config files and the RINEX import live here rather than on the main
 * screen: the main screen is for the station under test, and a button
 * that is used once a month should not compete with one used every day.
 */
@Composable
private fun AppMenu(
    open: Boolean,
    onDismiss: () -> Unit,
    onSettings: () -> Unit,
    onImportRinex: () -> Unit,
    onLoadConfig: () -> Unit,
    onSaveConfig: () -> Unit,
    onExportAll: () -> Unit,
    onAbout: () -> Unit,
) {
    DropdownMenu(expanded = open, onDismissRequest = onDismiss) {
        DropdownMenuItem(
            text = { Text(stringResource(R.string.menu_settings)) },
            onClick = onSettings,
        )
        DropdownMenuItem(
            text = { Text(stringResource(R.string.action_import_rinex)) },
            onClick = onImportRinex,
        )
        // Loading and saving whole configurations is a pro capability;
        // the free edition keeps the one mountpoint it is typed into.
        if (Features.IS_PRO) {
            HorizontalDivider()
            DropdownMenuItem(
                text = { Text(stringResource(R.string.menu_load_config)) },
                onClick = onLoadConfig,
            )
            DropdownMenuItem(
                text = { Text(stringResource(R.string.menu_save_config)) },
                onClick = onSaveConfig,
            )
            if (Features.MAX_MOUNTPOINTS > 1) {
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.menu_export_all)) },
                    onClick = onExportAll,
                )
            }
        }
        HorizontalDivider()
        DropdownMenuItem(
            text = { Text(stringResource(R.string.menu_about)) },
            onClick = onAbout,
        )
    }
}

/** What this is, who made it, and where to go for more. */
@Composable
private fun AboutDialog(onDismiss: () -> Unit) {
    val uriHandler = androidx.compose.ui.platform.LocalUriHandler.current
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.app_name)) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(
                    stringResource(R.string.about_version,
                                   BuildConfig.VERSION_NAME),
                    style = MaterialTheme.typography.bodySmall,
                )
                Text(
                    stringResource(R.string.about_blurb),
                    style = MaterialTheme.typography.bodySmall,
                )
                TextButton(onClick = { uriHandler.openUri(REPO_URL) }) {
                    Text(stringResource(R.string.about_repo))
                }
                TextButton(onClick = { uriHandler.openUri(HELP_URL) }) {
                    Text(stringResource(R.string.about_help))
                }
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) {
                Text(stringResource(R.string.action_close))
            }
        },
    )
}

private const val REPO_URL = "https://github.com/pe1mew/NTRIP-Analyser"
private const val HELP_URL =
    "https://github.com/pe1mew/NTRIP-Analyser/blob/main/docs/readme.md"
