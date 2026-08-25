package nl.pe1mew.ntripanalyser

import android.content.Context
import android.net.Uri
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json

/**
 * The single-connection layout every release before the shared format
 * wrote: `NTRIP_CASTER` and friends, upper case, at the top level.
 *
 * Read-only now. The project has one exchange format — the
 * `mountpoints` array in [MonitordConfig] — and nothing writes this one
 * any more, but files in it exist on disks, in support e-mails and in
 * released assets, and they still say exactly what they meant.
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
            // An unnamed connection writes no name key at all, rather
            // than an empty one in every entry of a file people edit.
            explicitNulls = false
        }

        /**
         * Read every connection a file describes.
         *
         * The project has one exchange format -- a `mountpoints` array --
         * and this reads it. Files written by releases before that format
         * existed carry the older single-connection layout instead, and
         * are read as a list of one rather than rejected: they still say
         * exactly what they meant.
         *
         * @return the connections, or null when the file is neither.
         */
        fun loadConnections(context: Context, uri: Uri): List<CasterSettings>? {
            val text = runCatching {
                context.contentResolver.openInputStream(uri)!!
                    .use { it.readBytes().decodeToString() }
            }.getOrNull() ?: return null

            runCatching { json.decodeFromString<MonitordConfig>(text) }
                .getOrNull()
                ?.takeIf { it.mountpoints.isNotEmpty() }
                ?.let { doc -> return doc.mountpoints.map { it.toSettings() } }

            return runCatching { json.decodeFromString<ConfigFile>(text) }
                .getOrNull()
                ?.takeIf { it.caster.isNotBlank() || it.mountpoint.isNotBlank() }
                ?.let { listOf(it.toSettings(CasterSettings())) }
        }

        /**
         * Write every saved connection, in the shared exchange format.
         *
         * One file for one job: the analysers read the first entry, the
         * daemon reads them all, and the phone reads them back. See
         * `docs/jsonConfigs.md`, including its warning that the passwords
         * in these files are in the clear.
         *
         * @return false when nothing was written, including when no saved
         *         connection is complete enough to be worth writing.
         */
        fun saveConnections(
            context: Context, uri: Uri, store: ProfileStore,
        ): Boolean {
            val doc = MonitordConfig(
                mountpoints = store.profiles.filter { it.isComplete }
                    .map { MonitordMountpoint.from(it) },
            )
            if (doc.mountpoints.isEmpty()) return false
            return runCatching {
                context.contentResolver.openOutputStream(uri, "wt")!!.use { out ->
                    out.write(
                        json.encodeToString(MonitordConfig.serializer(), doc)
                            .toByteArray()
                    )
                }
                true
            }.getOrDefault(false)
        }

    }
}

/**
 * One entry of the `mountpoints[]` array.
 *
 * The ephemeris block is optional here exactly as it was in the older
 * single-connection format: absent means the sky plot has no stream to
 * borrow, not that the file is wrong.
 */
@Serializable
data class MonitordMountpoint(
    val name: String? = null,
    val caster: String = "",
    val port: Int = 2101,
    val mountpoint: String = "",
    val username: String = "",
    val password: String = "",
    @SerialName("send_gga") val sendGga: Boolean = false,
    val tls: Boolean = false,
    val latitude: Double = 0.0,
    val longitude: Double = 0.0,
    @SerialName("eph_caster") val ephCaster: String? = null,
    @SerialName("eph_port") val ephPort: Int? = null,
    @SerialName("eph_mountpoint") val ephMountpoint: String? = null,
    @SerialName("eph_username") val ephUsername: String? = null,
    @SerialName("eph_password") val ephPassword: String? = null,
    @SerialName("eph_tls") val ephTls: Boolean? = null,
) {
    fun toSettings() = CasterSettings(
        name = name.orEmpty(),
        caster = caster,
        port = port,
        mountpoint = mountpoint,
        user = username,
        password = password,
        latitude = latitude,
        longitude = longitude,
        sendGga = sendGga,
        tls = tls,
        ephCaster = ephCaster.orEmpty(),
        ephPort = ephPort ?: 2101,
        ephMountpoint = ephMountpoint.orEmpty(),
        ephTls = ephTls ?: false,
    )

    companion object {
        fun from(s: CasterSettings) = MonitordMountpoint(
            name = s.name.ifBlank { null },
            caster = s.caster,
            port = s.port,
            mountpoint = s.mountpoint,
            username = s.user,
            password = s.password,
            sendGga = s.sendGga,
            tls = s.tls,
            latitude = s.latitude,
            longitude = s.longitude,
            // The ephemeris stream borrows the observation stream's
            // credentials, as the phone has only one pair to give.
            ephCaster = s.ephCaster.ifBlank { null },
            ephPort = if (s.ephCaster.isBlank()) null else s.ephPort,
            ephMountpoint = s.ephMountpoint.ifBlank { null },
            ephUsername = if (s.ephCaster.isBlank()) null else s.user,
            ephPassword = if (s.ephCaster.isBlank()) null else s.password,
            ephTls = if (s.ephCaster.isBlank()) null else s.ephTls,
        )
    }
}

/**
 * The monitoring daemon's configuration file.
 *
 * The defaults are the daemon's own: `output_dir` must match the Munin
 * plugin's `env.statedir`, and ten seconds is the interval the example
 * ships with. A user exporting from the phone gets a file that runs
 * as-is on a stock installation.
 */
@Serializable
data class MonitordConfig(
    @SerialName("output_dir") val outputDir: String = "/var/lib/ntrip-monitor",
    @SerialName("interval_s") val intervalS: Int = 10,
    val mountpoints: List<MonitordMountpoint> = emptyList(),
)
