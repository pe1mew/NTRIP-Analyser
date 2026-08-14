package nl.pe1mew.ntripanalyser

import android.content.Context
import android.content.SharedPreferences
import android.util.Log
import androidx.security.crypto.EncryptedSharedPreferences
import androidx.security.crypto.MasterKeys
import kotlinx.serialization.json.Json

/**
 * Saved connections, persisted in encrypted SharedPreferences.
 *
 * Credentials are encrypted at rest with a key held in the Android
 * Keystore. Storing one caster's password in the app sandbox was an
 * accepted limitation; storing a set of them is the same limitation with
 * a larger blast radius, and the move happened alongside the storage
 * change to profiles so there is one migration rather than two.
 *
 * Exported configuration files are a different matter entirely: those
 * are plain text by necessity, and `docs/jsonConfigs.md` says so in as
 * many words.
 */
object Settings {

    /** The pre-profiles preferences file: eleven flat keys, plain text. */
    private const val LEGACY_FILE = "caster"

    /** The profile store: one JSON document, encrypted. */
    private const val FILE = "caster_secure"
    private const val KEY_STORE = "profiles"
    private const val KEY_GGA_CONSENT = "gga_live_consent"

    private const val RINEX = "brdc.rnx"
    private const val KEY_RINEX_UTC = "rinex_newest_utc"
    private const val TAG = "ntrip_settings"

    private val json = Json { ignoreUnknownKeys = true; encodeDefaults = true }

    /*
     * androidx.security-crypto is pinned at 1.0.0, the stable release.
     * The profiles work first used 1.1.0-alpha06 and its MasterKey
     * builder; shipping an alpha in a paid app to guard credentials is
     * not a decision to inherit by accident (security review, F7).
     *
     * 1.0.0 offers the older MasterKeys alias API instead. Both create
     * the same Keystore entry -- `_androidx_security_master_key_` -- with
     * the same AES256-GCM spec and the same Tink schemes, so a store
     * written by the alpha is readable here; verified on the test
     * handset by installing over it and finding the saved connections
     * intact.
     */
    @Suppress("DEPRECATION")
    private fun prefs(context: Context): SharedPreferences =
        runCatching {
            val alias = MasterKeys.getOrCreate(MasterKeys.AES256_GCM_SPEC)
            EncryptedSharedPreferences.create(
                FILE, alias, context,
                EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
                EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM,
            ) as SharedPreferences
        }.getOrElse {
            // A device whose Keystore refuses to produce a key must still
            // be usable: losing the app is a worse outcome than storing
            // credentials the way every previous version already did.
            Log.w(TAG, "encrypted preferences unavailable; falling back", it)
            context.getSharedPreferences(FILE, Context.MODE_PRIVATE)
        }

    /**
     * Every saved connection, migrating the old single-connection form
     * on first use.
     *
     * The migration is the part that matters: an install that predates
     * profiles keeps its caster, credentials and position as profile 0
     * rather than opening to an empty form, which a user would rightly
     * report as data loss.
     */
    fun loadProfiles(context: Context): ProfileStore {
        val p = prefs(context)
        p.getString(KEY_STORE, null)?.let { raw ->
            runCatching { json.decodeFromString<ProfileStore>(raw) }
                .onSuccess { if (it.profiles.isNotEmpty()) return it }
                .onFailure { Log.w(TAG, "profile store unreadable; rebuilding", it) }
        }

        val legacy = context.getSharedPreferences(LEGACY_FILE, Context.MODE_PRIVATE)
        val store = if (legacy.contains("caster")) {
            Log.i(TAG, "migrating the pre-profiles configuration")
            ProfileStore(listOf(readLegacy(legacy)), 0)
        } else {
            ProfileStore()
        }
        saveProfiles(context, store)
        // The old file held a password in the clear; it has been copied
        // into encrypted storage, so leaving it behind would keep a
        // plain-text copy for no reason.
        legacy.edit().clear().apply()
        return store
    }

    private fun readLegacy(p: SharedPreferences) = CasterSettings(
        name = "",
        caster = p.getString("caster", "").orEmpty(),
        port = p.getInt("port", 2101),
        mountpoint = p.getString("mountpoint", "").orEmpty(),
        user = p.getString("user", "").orEmpty(),
        password = p.getString("password", "").orEmpty(),
        latitude = p.getFloat("lat", 52.0f).toDouble(),
        longitude = p.getFloat("lon", 6.0f).toDouble(),
        sendGga = p.getBoolean("gga", false),
        ephCaster = p.getString("eph_caster", "").orEmpty(),
        ephPort = p.getInt("eph_port", 2101),
        ephMountpoint = p.getString("eph_mp", "").orEmpty(),
    )

    fun saveProfiles(context: Context, store: ProfileStore) {
        val bounded = store.copy(
            profiles = store.profiles.take(Features.MAX_MOUNTPOINTS.coerceAtLeast(1)),
            active = store.activeIndex,
        )
        prefs(context).edit()
            .putString(KEY_STORE, json.encodeToString(ProfileStore.serializer(), bounded))
            .apply()
    }

    /** The connection in use. */
    fun load(context: Context): CasterSettings = loadProfiles(context).current

    /**
     * Whether the user has agreed to transmit this phone's position.
     *
     * Kept beside the connections rather than inside one: consent is
     * about sending a location to a third party at all, so it is asked
     * once and applies to every profile. Revoking it stops every live
     * uplink, which a per-profile flag could not do.
     *
     * Absent in the free edition, which never transmits a position
     * (`design/editions.md`).
     */
    fun liveGgaConsent(context: Context): Boolean =
        prefs(context).getBoolean(KEY_GGA_CONSENT, false)

    fun setLiveGgaConsent(context: Context, granted: Boolean) {
        prefs(context).edit().putBoolean(KEY_GGA_CONSENT, granted).apply()
    }

    /** Replace the connection in use, leaving the others alone. */
    fun save(context: Context, s: CasterSettings): ProfileStore {
        val store = loadProfiles(context)
        val i = store.activeIndex
        val list = store.profiles.toMutableList()
        if (i in list.indices) list[i] = s else list.add(s)
        val next = store.copy(profiles = list, active = i)
        saveProfiles(context, next)
        return next
    }

    /** Add a connection and make it the active one; returns the new store. */
    fun addProfile(context: Context, s: CasterSettings): ProfileStore {
        val store = loadProfiles(context)
        if (store.profiles.size >= Features.MAX_MOUNTPOINTS) return store
        val list = store.profiles + s
        val next = store.copy(profiles = list, active = list.lastIndex)
        saveProfiles(context, next)
        return next
    }

    /** Remove a connection; the last one is emptied rather than removed. */
    fun removeProfile(context: Context, index: Int): ProfileStore {
        val store = loadProfiles(context)
        if (index !in store.profiles.indices) return store
        val next = if (store.profiles.size == 1) {
            ProfileStore(listOf(CasterSettings()), 0)
        } else {
            val list = store.profiles.toMutableList().also { it.removeAt(index) }
            store.copy(profiles = list, active = store.activeIndex.coerceAtMost(list.lastIndex))
        }
        saveProfiles(context, next)
        return next
    }

    /** What [mergeConnections] did, so the app can say so. */
    data class MergeResult(
        val store: ProfileStore,
        val added: Int,
        val updated: Int,
        val dropped: Int,
    )

    /**
     * Take the connections a file describes into the saved set.
     *
     * Merged rather than replacing: a user loading a colleague's file
     * should gain a connection, not lose the five they had. An entry that
     * names a caster and mountpoint already saved updates that one --
     * otherwise loading the same file twice would quietly fill the list
     * with duplicates.
     *
     * Anything past [Features.MAX_MOUNTPOINTS] is dropped, and counted so
     * the caller can say how many. In the free edition that limit is one,
     * which is the same rule the CLI and the GUI follow: use the first
     * connection, mention the rest.
     */
    fun mergeConnections(
        context: Context, incoming: List<CasterSettings>,
    ): MergeResult {
        val store = loadProfiles(context)
        val list = store.profiles.toMutableList()
        var added = 0
        var updated = 0
        var dropped = 0
        var firstTouched = -1

        fun sameConnection(a: CasterSettings, b: CasterSettings) =
            a.caster.equals(b.caster, ignoreCase = true) &&
            a.port == b.port &&
            a.mountpoint.equals(b.mountpoint, ignoreCase = true)

        for (c in incoming) {
            // An empty slot -- a fresh install, or one just added -- is
            // filled rather than left beside the connection just loaded.
            val blank = list.indexOfFirst { !it.isComplete }
            val existing = list.indexOfFirst { sameConnection(it, c) }
            when {
                existing >= 0 -> {
                    list[existing] = c
                    updated++
                    if (firstTouched < 0) firstTouched = existing
                }
                blank >= 0 -> {
                    list[blank] = c
                    added++
                    if (firstTouched < 0) firstTouched = blank
                }
                list.size < Features.MAX_MOUNTPOINTS -> {
                    list.add(c)
                    added++
                    if (firstTouched < 0) firstTouched = list.lastIndex
                }
                else -> dropped++
            }
        }

        val next = ProfileStore(list, if (firstTouched >= 0) firstTouched else store.activeIndex)
        saveProfiles(context, next)
        return MergeResult(next, added, updated, dropped)
    }

    /** Switch to a saved connection. */
    fun selectProfile(context: Context, index: Int): ProfileStore {
        val store = loadProfiles(context)
        if (index !in store.profiles.indices) return store
        val next = store.copy(active = index)
        saveProfiles(context, next)
        return next
    }

    /** Where the imported navigation file lives. */
    fun rinexFile(context: Context): java.io.File =
        java.io.File(context.filesDir, RINEX)

    /** The name of the imported file, or null when none has been. */
    fun rinexName(context: Context): String? =
        context.getSharedPreferences(FILE, Context.MODE_PRIVATE)
            .getString("rinex_name", null)
            ?.takeIf { rinexFile(context).exists() }

    /**
     * How old the imported file's newest record is, in seconds; null
     * when nothing is imported or the date could not be read.
     *
     * Stored at import as the record's own UTC date and aged against the
     * clock on every read, so a file that was current when picked
     * becomes stale on its own -- which is what happens to broadcast
     * ephemerides, and what the badge has to show.
     */
    fun rinexAgeS(context: Context): Double? {
        if (rinexName(context) == null) return null
        val utc = context.getSharedPreferences(FILE, Context.MODE_PRIVATE)
            .getLong(KEY_RINEX_UTC, 0L)
        if (utc <= 0L) return null
        return (System.currentTimeMillis() / 1000.0) - utc
    }

    /**
     * The window inside which orbits may be used to place a satellite.
     *
     * Four hours, because that is what `sv_eph_is_valid_at()` enforces
     * in `src/core/sv_ephemeris.c` -- the app does not decide this
     * separately, it reports the rule the placement code already
     * applies. BeiDou is allowed six there; four is the honest headline
     * for a mixed file.
     */
    const val RINEX_FRESH_S = 4 * 3600.0

    /**
     * Copy a navigation file the user picked to a staging file.
     *
     * Copied into app storage because `rinex_nav_load()` takes a path and
     * a `content://` URI is not one. Gzip is unwrapped on the way in,
     * since archives serve these files compressed and asking the user to
     * decompress on a phone would be a poor joke.
     *
     * Staged rather than written in place so that picking the wrong file
     * costs nothing: an import that overwrote the live file destroyed a
     * working set of orbits before anything had established the new file
     * was even readable.
     *
     * @return the display name on success, null when it could not be read.
     *         On success the bytes are at @ref stagedRinexFile, awaiting
     *         @ref commitRinex.
     */
    fun stageRinex(context: Context, uri: android.net.Uri): String? {
        val name = context.contentResolver.query(uri, null, null, null, null)
            ?.use { c ->
                val i = c.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
                if (i >= 0 && c.moveToFirst()) c.getString(i) else null
            } ?: uri.lastPathSegment ?: "navigation file"

        return runCatching {
            context.contentResolver.openInputStream(uri)!!.use { raw ->
                val head = java.io.BufferedInputStream(raw)
                head.mark(2)
                val b0 = head.read()
                val b1 = head.read()
                head.reset()
                // 0x1f 0x8b is the gzip magic; anything else is plain text.
                val src = if (b0 == 0x1f && b1 == 0x8b)
                    java.util.zip.GZIPInputStream(head) else head
                stagedRinexFile(context).outputStream().use { out ->
                    src.copyTo(out)
                }
            }
            name
        }.getOrNull()
    }

    /** Where a staged file waits while it is being checked. */
    fun stagedRinexFile(context: Context): java.io.File =
        java.io.File(context.filesDir, "$RINEX.staged")

    /**
     * Promote a checked staging file to the one runs will read.
     *
     * @param newestUtc UTC epoch seconds of the file's newest record, as
     *        [NtripBridge.navFileNewestUtc] reports it; 0 when unknown.
     */
    fun commitRinex(context: Context, name: String, newestUtc: Long = 0L) {
        val staged = stagedRinexFile(context)
        runCatching {
            rinexFile(context).delete()
            staged.renameTo(rinexFile(context))
        }
        context.getSharedPreferences(FILE, Context.MODE_PRIVATE).edit()
            .putString("rinex_name", name)
            .putLong(KEY_RINEX_UTC, newestUtc)
            .apply()
    }

    /**
     * Record the date of the file already imported.
     *
     * For an import made before the app read dates at all: the file is
     * there, its records carry their own dates, and one parse recovers
     * what was never stored.
     */
    fun setRinexNewestUtc(context: Context, newestUtc: Long) {
        context.getSharedPreferences(FILE, Context.MODE_PRIVATE).edit()
            .putLong(KEY_RINEX_UTC, newestUtc).apply()
    }

    /**
     * Discard a staged file, leaving any previous import untouched.
     *
     * Used when a file copies cleanly but holds no orbits: whatever was
     * already imported keeps working, which is the outcome a user
     * picking the wrong file from a crowded Downloads folder wants.
     */
    fun discardStagedRinex(context: Context) {
        runCatching { stagedRinexFile(context).delete() }
    }
}
