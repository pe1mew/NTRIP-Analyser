package nl.pe1mew.ntripanalyser

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
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

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MainScreen() {
    val context = androidx.compose.ui.platform.LocalContext.current
    val runState by MonitorService.state.collectAsStateWithLifecycle(MonitorService.RunState())

    var settings by remember { mutableStateOf(Settings.load(context)) }
    var showSettings by remember { mutableStateOf(!Settings.load(context).isComplete) }

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

            VerdictBadge(doc, runState.running, runState.outcome)

            // What this run is (or would be) pointed at, without making
            // the user open the settings screen to find out.
            ConfigSummary(settings) { showSettings = true }

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
                    OutlinedButton(
                        onClick = { MonitorService.start(context, settings, watch = true) },
                        enabled = settings.isComplete,
                        modifier = Modifier.weight(1f),
                    ) { Text(stringResource(R.string.action_watch)) }
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
) {
    val verdict = doc?.kpi?.overallEnum ?: RunVerdict.RUNNING
    val label = doc?.kpi?.overallName ?: stringResource(R.string.verdict_idle)

    Surface(
        color = runColour(verdict),
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
                    )
                )
            }) { Text(stringResource(R.string.action_save)) }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text(stringResource(R.string.action_cancel)) }
        },
    )
}

@Composable
private fun stringResource(id: Int): String =
    androidx.compose.ui.res.stringResource(id)

@Composable
private fun stringResource(id: Int, vararg args: Any): String =
    androidx.compose.ui.res.stringResource(id, *args)

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
