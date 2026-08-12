package nl.pe1mew.ntripanalyser

import android.content.Context

/**
 * Connection settings, persisted in SharedPreferences.
 *
 * Phase 1 stores the password in plain preferences, which is private to
 * the app's sandbox but not encrypted at rest. That is a deliberate,
 * recorded limitation rather than an oversight: NTRIP credentials are
 * per-caster registrations, and moving to EncryptedSharedPreferences (or
 * the Keystore) is a self-contained change once the app does anything
 * else worth protecting. It is listed in android/readme.md.
 */
object Settings {

    private const val FILE = "caster"
    private const val RINEX = "brdc.rnx"

    fun load(context: Context): CasterSettings {
        val p = context.getSharedPreferences(FILE, Context.MODE_PRIVATE)
        return CasterSettings(
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
    }

    fun save(context: Context, s: CasterSettings) {
        context.getSharedPreferences(FILE, Context.MODE_PRIVATE).edit().apply {
            putString("caster", s.caster)
            putInt("port", s.port)
            putString("mountpoint", s.mountpoint)
            putString("user", s.user)
            putString("password", s.password)
            putFloat("lat", s.latitude.toFloat())
            putFloat("lon", s.longitude.toFloat())
            putBoolean("gga", s.sendGga)
            putString("eph_caster", s.ephCaster)
            putInt("eph_port", s.ephPort)
            putString("eph_mp", s.ephMountpoint)
            apply()
        }
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
