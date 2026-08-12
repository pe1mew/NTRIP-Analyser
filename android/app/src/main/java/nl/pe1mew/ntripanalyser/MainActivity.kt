package nl.pe1mew.ntripanalyser

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
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

    var settings by remember { mutableStateOf(Settings.load(context)) }
    var showSettings by remember { mutableStateOf(!Settings.load(context).isComplete) }
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

    // The app never downloads a navigation file: the user obtains it and
    // so holds the relationship with the data provider, including its
    // licence and usage rules (android/design/views.md).
    val pickRinex = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri ->
        if (uri != null) {
            val name = Settings.importRinex(context, uri)
            if (name != null) {
                rinexName = name
                MonitorService.rinexPath = Settings.rinexFile(context).absolutePath
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
    // Positions come from the station's own orbits where they exist --
    // exact, complete, and independent of the handset -- and from the
    // phone only for satellites no orbit covers. Preferring the phone
    // would discard the better source: the orbit cache placed 47 of 47
    // satellites where the phone managed 23.
    var usedOrbits by remember { mutableStateOf(0) }
    val plotted = remember(liveDoc, positions) {
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
                cn0 = if (Features.IS_PRO) sat.cn0 else sat.cn0Mean,
                azimuthDeg = az, elevationDeg = el,
            )
        }
        usedOrbits = fromOrbit
        out
    }

    // Signal quality needs no position at all: C/N0 comes from the
    // stream. Feeding it the positioned subset dropped satellites the
    // base was hearing perfectly well, which understates the station.
    val signal = remember(liveDoc) {
        liveDoc?.sats.orEmpty().map { sat ->
            SignalSat(
                gnss = sat.gnss, prn = sat.prn,
                cn0 = if (Features.IS_PRO) sat.cn0 else sat.cn0Mean,
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

                liveDoc?.watch?.let { WatchCard(it) }

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
                                usedOrbits > 0 -> PositionSource.EPHEMERIS
                                haveLocation -> PositionSource.PHONE_GNSS
                                else -> PositionSource.NONE
                            },
                            footer = footer,
                        )
                        AnalysisTab.SIGNAL ->
                            SignalBars(signal, liveValues = Features.IS_PRO)
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
                actions = {
                    IconButton(onClick = { showSettings = true }) {
                        Text("⚙", fontSize = 20.sp)
                    }
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

            // What this run is (or would be) pointed at, without making
            // the user open the settings screen to find out.
            ConfigSummary(settings) { showSettings = true }

            if (settings.caster.isNotBlank()) {
                TextButton(onClick = { showSourcetable = true }) {
                    Text(stringResource(R.string.action_browse))
                }
            }

            doc?.let { StreamChips(it) }

            runState.error?.let {
                Text(it, color = MaterialTheme.colorScheme.error)
            }

            doc?.watch?.let { WatchCard(it) }

            doc?.kpi?.items?.forEachIndexed { i, item ->
                KpiRow(i + 1, item, doc.stats)
            }

            Spacer(Modifier.height(4.dp))

            // Two ways to run, chosen at the moment of running: grade the
            // station once, or watch it.  Neither is a caster setting.
            if (runState.running) {
                Button(
                    onClick = { MonitorService.stop(context) },
                    modifier = Modifier.fillMaxWidth(),
                ) { Text(stringResource(R.string.action_stop)) }
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
            doc?.eph?.let { EphCard(it, rinexName) { pickRinex.launch(arrayOf("*/*")) } }



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
                settings = settings.copy(mountpoint = mp)
                Settings.save(context, settings)
                showSourcetable = false
            },
        )
    }

    if (showSettings) {
        SettingsDialog(
            initial = settings,
            onDismiss = { showSettings = false },
            onSave = {
                settings = it
                Settings.save(context, it)
                showSettings = false
            },
        )
    }
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
private fun evidenceFor(index: Int, s: Stats): List<Pair<String, String>> {
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
        2 -> listOf(
            "CRC-valid frames" to "${s.framesOk}",
            "Message types seen" to "${s.types.size}",
            "Latency" to f(s.latencyS, "s", 2),
        )
        3 -> listOf(
            "ARP received" to if (s.arpValid) "yes" else "no",
            "Latitude" to (s.arpLat?.let { "%.6f°".format(it) } ?: "—"),
            "Longitude" to (s.arpLon?.let { "%.6f°".format(it) } ?: "—"),
            "Height" to f(s.arpAlt, "m", 2),
        )
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
        else -> emptyList()
    }
}

@Composable
private fun KpiRow(index: Int, item: KpiItem, stats: Stats) {
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
                val rows = evidenceFor(index, stats)
                if (rows.isEmpty()) {
                    Text(
                        stringResource(R.string.no_evidence),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                rows.forEach { (k, v) ->
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
private fun EphCard(eph: EphState, rinexName: String?, onImport: () -> Unit) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp)) {
            Text(
                stringResource(R.string.eph_title),
                fontWeight = FontWeight.Bold,
            )
            Spacer(Modifier.height(4.dp))

            Text(
                stringResource(R.string.eph_coverage, eph.placeable, eph.tracked, eph.cached),
                style = MaterialTheme.typography.bodySmall,
                color = if (eph.isComplete) MaterialTheme.colorScheme.onSurfaceVariant
                        else MaterialTheme.colorScheme.error,
            )

            val age = eph.ageS
            Text(
                when {
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
            TextButton(onClick = onImport) {
                Text(stringResource(R.string.action_import_rinex))
            }
        }
    }
}
