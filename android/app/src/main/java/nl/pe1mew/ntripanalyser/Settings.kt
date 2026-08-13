package nl.pe1mew.ntripanalyser

import android.content.Context
import android.content.SharedPreferences
import android.util.Log
import androidx.security.crypto.EncryptedSharedPreferences
import androidx.security.crypto.MasterKey
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

    private const val RINEX = "brdc.rnx"
    private const val TAG = "ntrip_settings"

    private val json = Json { ignoreUnknownKeys = true; encodeDefaults = true }

    private fun prefs(context: Context): SharedPreferences =
        runCatching {
            val key = MasterKey.Builder(context)
                .setKeyScheme(MasterKey.KeyScheme.AES256_GCM)
                .build()
            EncryptedSharedPreferences.create(
                context, FILE, key,
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

    /** Promote a checked staging file to the one runs will read. */
    fun commitRinex(context: Context, name: String) {
        val staged = stagedRinexFile(context)
        runCatching {
            rinexFile(context).delete()
            staged.renameTo(rinexFile(context))
        }
        context.getSharedPreferences(FILE, Context.MODE_PRIVATE).edit()
            .putString("rinex_name", name).apply()
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
