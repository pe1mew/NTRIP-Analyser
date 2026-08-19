/**
 * @file Dialogs.kt
 * @brief Everything modal, and the overflow menu that opens most of it.
 *
 * Moved out of MainActivity.kt unchanged (GUI v2, P1.1). Dialogs are
 * not destinations: they sit above whatever is on screen and the system
 * back key dismisses them, which is why they are not in the back stack
 * in Navigation.kt.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

package nl.pe1mew.ntripanalyser

import android.Manifest
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.util.Locale

/**
 * Choose among the saved connections.
 *
 * It hangs off the configuration tile because the tile already names the
 * connection in use and already opened settings when tapped. The main
 * screen exists to show one verdict; a permanent control for something
 * touched once a week does not belong on it.
 */
@Composable
internal fun ProfilePicker(
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
internal fun SettingsDialog(
    initial: CasterSettings,
    onDismiss: () -> Unit,
    onSave: (CasterSettings) -> Unit,
) {
    var caster by remember { mutableStateOf(initial.caster) }
    var port by remember { mutableStateOf(initial.port.toString()) }
    var mountpoint by remember { mutableStateOf(initial.mountpoint) }
    var user by remember { mutableStateOf(initial.user) }
    var password by remember { mutableStateOf(initial.password) }
    // Never remembered across openings: the dialog reverts to
    // masked every time it is shown.
    var showPassword by remember { mutableStateOf(false) }
    var lat by remember { mutableStateOf(initial.latitude.toString()) }
    var lon by remember { mutableStateOf(initial.longitude.toString()) }
    var gga by remember { mutableStateOf(initial.sendGga) }
    var ephCaster by remember { mutableStateOf(initial.ephCaster) }
    var ephPort by remember { mutableStateOf(initial.ephPort.toString()) }
    var ephMp by remember { mutableStateOf(initial.ephMountpoint) }

    val context = androidx.compose.ui.platform.LocalContext.current
    val scope = rememberCoroutineScope()
    var live by remember { mutableStateOf(initial.ggaLive) }
    var consent by remember { mutableStateOf(Settings.liveGgaConsent(context)) }
    var askConsent by remember { mutableStateOf(false) }
    var looking by remember { mutableStateOf(false) }
    // One line under the position fields, for whatever the last action
    // has to say: a map that would not open, a paste that held no
    // coordinates, a mountpoint the caster does not list.
    var hint by remember { mutableStateOf<String?>(null) }

    fun show(v: Double) = "%.6f".format(Locale.US, v)

    val askLocation = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        live = granted
        if (!granted) hint = context.getString(R.string.gga_needs_location)
    }

    /** Turn the live uplink on, asking for whatever is still missing. */
    fun enableLive() {
        when {
            !consent -> askConsent = true
            !hasLocationPermission(context) ->
                askLocation.launch(Manifest.permission.ACCESS_FINE_LOCATION)
            else -> live = true
        }
    }

    /** Fill the position from the mountpoint's own sourcetable entry. */
    fun fromStation() {
        val mp = mountpoint.trim()
        if (caster.isBlank() || mp.isEmpty()) return
        scope.launch {
            looking = true
            val json = withContext(Dispatchers.IO) {
                NtripBridge.sourcetable(
                    caster, port.toIntOrNull() ?: 2101, user, password)
            }
            looking = false
            val entry = json
                ?.let {
                    runCatching { bridgeJson.decodeFromString<Sourcetable>(it) }
                        .getOrNull()
                }
                ?.entries?.firstOrNull { it.mountpoint.equals(mp, true) }
            hint = when {
                entry == null ->
                    context.getString(R.string.station_lookup_failed, mp)
                entry.lat == 0.0 && entry.lon == 0.0 ->
                    context.getString(R.string.station_lookup_no_pos, mp)
                else -> {
                    lat = show(entry.lat)
                    lon = show(entry.lon)
                    null
                }
            }
        }
    }

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
                // The same keyboard the caster field asks for, and for
                // the same reason: with a text keyboard EMUI commits a
                // full stop and a space when the field loses focus, so
                // "APEL0" is saved as "APEL0. " and the caster answers
                // that no such mountpoint exists. Measured on the test
                // handset -- every attempt to type a mountpoint by hand
                // produced it, and the trim() on save cannot remove a
                // character the user never typed.
                OutlinedTextField(mountpoint, { mountpoint = it },
                    label = { Text(stringResource(R.string.field_mountpoint)) },
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri))
                OutlinedTextField(user, { user = it },
                    label = { Text(stringResource(R.string.field_user)) },
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri))
                // Masked by default, with a way to look.
                //
                // A credential on screen is a credential in every
                // screenshot, over every shoulder and in every screen
                // recording -- one was caught on the way to a store
                // listing. Revealing it is still a tap away, because a
                // password typed on a phone keyboard and never checked
                // is the other way this field goes wrong.
                OutlinedTextField(password, { password = it },
                    label = { Text(stringResource(R.string.field_password)) },
                    singleLine = true,
                    visualTransformation =
                        if (showPassword) VisualTransformation.None
                        else PasswordVisualTransformation(),
                    trailingIcon = {
                        TextButton(onClick = { showPassword = !showPassword }) {
                            Text(stringResource(
                                if (showPassword) R.string.action_hide
                                else R.string.action_show))
                        }
                    },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri))
                // Said here rather than only in the documentation: the
                // credential is stored encrypted but travels as NTRIP
                // specifies, which is base64 over plain TCP. A user
                // typing a password deserves to know that at the moment
                // they type it (security review, F3).
                if (user.isNotBlank()) {
                    Text(
                        stringResource(R.string.field_password_plain),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Checkbox(gga, { gga = it })
                    Text(stringResource(R.string.field_gga))
                }

                if (gga) {
                    // Which position is reported is the edition
                    // difference, and it maps onto two different
                    // questions: does this station serve the area it
                    // claims (a fixed point, repeatable between runs),
                    // or am I served properly *here* (the phone's own).
                    // See android/design/editions.md.
                    if (Features.HAS_LIVE_GGA) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Checkbox(live, { on ->
                                if (on) enableLive() else live = false
                            })
                            Text(stringResource(R.string.field_gga_live))
                        }
                    }
                    Text(
                        stringResource(
                            if (live && Features.HAS_LIVE_GGA)
                                R.string.field_gga_live_explain
                            else R.string.field_gga_fixed_explain
                        ),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                    OutlinedTextField(lat, { lat = it },
                        label = { Text(stringResource(R.string.field_lat)) }, singleLine = true)
                    OutlinedTextField(lon, { lon = it },
                        label = { Text(stringResource(R.string.field_lon)) }, singleLine = true)

                    // The map is somebody else's: a geo: intent hands the
                    // job to whatever map app is installed, or to
                    // OpenStreetMap in the browser, and the answer comes
                    // back through the clipboard. The Windows GUI does the
                    // same with its own browser page; see MapPick.
                    Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                        TextButton(onClick = {
                            val ok = MapPick.open(
                                context,
                                lat.toDoubleOrNull() ?: 52.0,
                                lon.toDoubleOrNull() ?: 6.0,
                            )
                            hint = context.getString(
                                if (ok) R.string.map_hint else R.string.map_no_app)
                        }) { Text(stringResource(R.string.action_map)) }
                        TextButton(onClick = {
                            val picked = MapPick.parse(MapPick.clipboard(context))
                            if (picked == null) {
                                hint = context.getString(R.string.map_paste_failed)
                            } else {
                                lat = show(picked.first)
                                lon = show(picked.second)
                                hint = null
                            }
                        }) { Text(stringResource(R.string.action_paste)) }
                        TextButton(
                            onClick = { fromStation() },
                            enabled = !looking && caster.isNotBlank() &&
                                mountpoint.isNotBlank(),
                        ) { Text(stringResource(R.string.action_from_station)) }
                    }
                    if (looking) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            CircularProgressIndicator(Modifier.size(16.dp))
                            Spacer(Modifier.width(8.dp))
                            Text(
                                stringResource(R.string.sourcetable_loading),
                                style = MaterialTheme.typography.bodySmall,
                            )
                        }
                    }
                    hint?.let {
                        Text(
                            it,
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }

                HorizontalDivider(Modifier.padding(vertical = 8.dp))

                // Free does not dial an ephemeris stream -- MonitorService
                // gates it on Features.HAS_EPH_STREAM -- so free must not
                // ask for one. Fields that are saved and then ignored are
                // worse than absent: a user who filled them in has been
                // told the app will do something it will not, and the sky
                // view falling back to the phone then looks like a fault.
                // The space says what the edition does instead.
                if (Features.HAS_EPH_STREAM) {
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
                        label = { Text(stringResource(R.string.field_eph_port)) },
                        singleLine = true)
                    OutlinedTextField(ephMp, { ephMp = it },
                        label = { Text(stringResource(R.string.field_eph_mp)) },
                        singleLine = true,
                        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri))
                } else {
                    Text(
                        stringResource(R.string.eph_free),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
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
                        ggaLive = live && Features.HAS_LIVE_GGA && consent,
                        // Carried through untouched where the fields
                        // are not shown. The configuration file is shared
                        // with pro and with the desktop tools, so saving
                        // settings in free must not quietly strip the
                        // ephemeris mountpoint out of somebody's config.
                        ephCaster = if (Features.HAS_EPH_STREAM)
                            ephCaster.filter {
                                it.isLetterOrDigit() || it == '.' || it == '-'
                            } else initial.ephCaster,
                        ephPort = if (Features.HAS_EPH_STREAM)
                            (ephPort.toIntOrNull() ?: 2101) else initial.ephPort,
                        ephMountpoint = if (Features.HAS_EPH_STREAM)
                            ephMp.trim() else initial.ephMountpoint,
                    )
                )
            }) { Text(stringResource(R.string.action_save)) }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text(stringResource(R.string.action_cancel)) }
        },
    )

    // Asked once, before the first run that would transmit a position,
    // and it names the caster it goes to. A line in a settings screen is
    // not consent for sending someone's location to a third party's
    // server (android/design/editions.md).
    if (askConsent) {
        AlertDialog(
            onDismissRequest = { askConsent = false },
            title = { Text(stringResource(R.string.gga_consent_title)) },
            text = {
                Text(stringResource(
                    R.string.gga_consent_body,
                    caster.ifBlank { stringResource(R.string.gga_consent_caster) },
                ))
            },
            confirmButton = {
                TextButton(onClick = {
                    askConsent = false
                    consent = true
                    Settings.setLiveGgaConsent(context, true)
                    if (hasLocationPermission(context)) live = true
                    else askLocation.launch(Manifest.permission.ACCESS_FINE_LOCATION)
                }) { Text(stringResource(R.string.gga_consent_allow)) }
            },
            dismissButton = {
                TextButton(onClick = {
                    askConsent = false
                    live = false
                }) { Text(stringResource(R.string.gga_consent_deny)) }
            },
        )
    }
}

/**
 * What this app is built on, and what those licences ask for.
 *
 * cJSON is MIT and requires its notice to travel with the software;
 * everything else is Apache 2.0 and requires attribution. Reachable from
 * About, in both editions, because the obligation is the same in both --
 * and read from a generated resource, so the versions it states are the
 * versions the build resolved (`tools/make_notices.py`).
 */
@Composable
private fun NoticesDialog(onDismiss: () -> Unit) {
    val context = androidx.compose.ui.platform.LocalContext.current
    val text = remember {
        runCatching {
            context.resources.openRawResource(R.raw.notices)
                .bufferedReader().use { it.readText() }
        }.getOrDefault("")
    }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.about_notices)) },
        text = {
            Text(
                text,
                modifier = Modifier
                    .heightIn(max = 460.dp)
                    .verticalScroll(rememberScrollState()),
                // Small enough that the notice's own 68-column layout
                // fits the width: a licence re-flowed by the renderer
                // reads as damaged, and re-flowing it here would mean
                // editing text that is quoted for legal reasons.
                style = MaterialTheme.typography.bodySmall.copy(
                    fontSize = 10.sp,
                    lineHeight = 14.sp,
                ),
                fontFamily = FontFamily.Monospace,
            )
        },
        confirmButton = {
            TextButton(onClick = onDismiss) {
                Text(stringResource(R.string.action_close))
            }
        },
    )
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
internal fun SourcetableDialog(
    settings: CasterSettings,
    onDismiss: () -> Unit,
    onPick: (SourceEntry) -> Unit,
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
private fun SourceRow(e: SourceEntry, onPick: (SourceEntry) -> Unit) {
    Column(
        Modifier
            .fillMaxWidth()
            .then(
                if (Features.SOURCETABLE_SELECTABLE)
                    Modifier.clickable { onPick(e) }
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

/**
 * Everything that is not a measurement.
 *
 * Config files and the RINEX import live here rather than on the main
 * screen: the main screen is for the station under test, and a button
 * that is used once a month should not compete with one used every day.
 */
@Composable
internal fun AppMenu(
    open: Boolean,
    onDismiss: () -> Unit,
    onSettings: () -> Unit,
    onImportRinex: () -> Unit,
    onLoadConfig: () -> Unit,
    onSaveConfig: () -> Unit,
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
internal fun AboutDialog(onDismiss: () -> Unit) {
    val uriHandler = androidx.compose.ui.platform.LocalUriHandler.current
    var notices by remember { mutableStateOf(false) }

    if (notices) {
        NoticesDialog { notices = false }
        return
    }

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
                TextButton(onClick = { uriHandler.openUri(PRIVACY_URL) }) {
                    Text(stringResource(R.string.about_privacy))
                }
                TextButton(onClick = { notices = true }) {
                    Text(stringResource(R.string.about_notices))
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

