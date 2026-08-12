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
}
