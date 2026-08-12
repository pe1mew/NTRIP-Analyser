package nl.pe1mew.ntripanalyser

import android.content.Context
import android.net.Uri
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json

/**
 * The project's `config.json`, as the CLI and the Windows GUI write it.
 *
 * The same file, field for field — a config saved on a desktop opens on
 * the phone and vice versa. That is the whole point: an installer should
 * not have to retype a caster because they changed device, and a support
 * request should be answerable by asking for the file.
 *
 * The field names are the C struct's, uppercase and all, which is why
 * this class does not follow Kotlin naming: matching the file matters
 * more than matching the language.
 */
@Serializable
data class ConfigFile(
    @SerialName("NTRIP_CASTER") val caster: String = "",
    @SerialName("NTRIP_PORT") val port: Int = 2101,
    @SerialName("MOUNTPOINT") val mountpoint: String = "",
    @SerialName("USERNAME") val username: String = "",
    @SerialName("PASSWORD") val password: String = "",
    @SerialName("LATITUDE") val latitude: Double = 52.0,
    @SerialName("LONGITUDE") val longitude: Double = 6.0,
    @SerialName("EPH_CASTER") val ephCaster: String = "",
    @SerialName("EPH_PORT") val ephPort: Int = 2101,
    @SerialName("EPH_MOUNTPOINT") val ephMountpoint: String = "",
    @SerialName("EPH_USERNAME") val ephUsername: String = "",
    @SerialName("EPH_PASSWORD") val ephPassword: String = "",
) {
    fun toSettings(existing: CasterSettings) = existing.copy(
        caster = caster,
        port = port,
        mountpoint = mountpoint,
        user = username,
        password = password,
        latitude = latitude,
        longitude = longitude,
        ephCaster = ephCaster,
        ephPort = ephPort,
        ephMountpoint = ephMountpoint,
    )

    companion object {
        private val json = Json {
            ignoreUnknownKeys = true     // a desktop file may carry more
            prettyPrint = true
            encodeDefaults = true
        }

        fun from(s: CasterSettings) = ConfigFile(
            caster = s.caster,
            port = s.port,
            mountpoint = s.mountpoint,
            username = s.user,
            password = s.password,
            latitude = s.latitude,
            longitude = s.longitude,
            ephCaster = s.ephCaster,
            ephPort = s.ephPort,
            ephMountpoint = s.ephMountpoint,
            // The ephemeris stream uses the same credentials as the
            // observation stream, as the phone has only one pair to
            // give; written out so a desktop reading the file finds
            // what it expects rather than blanks.
            ephUsername = s.user,
            ephPassword = s.password,
        )

        /** Read a config the user picked; null when it cannot be parsed. */
        fun load(context: Context, uri: Uri): ConfigFile? = runCatching {
            context.contentResolver.openInputStream(uri)!!.use { input ->
                json.decodeFromString<ConfigFile>(input.readBytes().decodeToString())
            }
        }.getOrNull()

        /** Write settings to a file the user chose; true on success. */
        fun save(context: Context, uri: Uri, s: CasterSettings): Boolean =
            runCatching {
                context.contentResolver.openOutputStream(uri, "wt")!!.use { out ->
                    out.write(
                        json.encodeToString(serializer(), from(s)).toByteArray()
                    )
                }
                true
            }.getOrDefault(false)
    }
}
