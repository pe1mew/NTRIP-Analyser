package nl.pe1mew.ntripanalyser

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.IBinder
import android.os.SystemClock
import androidx.core.app.NotificationCompat
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlin.concurrent.thread

/**
 * The acceptance run, as a foreground service.
 *
 * A KPI run takes at least sixty continuous seconds, which is longer
 * than Android will reliably let a backgrounded activity keep a socket
 * open. Anything short of a foreground service risks the system pausing
 * the pump loop mid-window, which would look to the user like a station
 * that cannot hold its verdict -- a measurement artefact reported as a
 * fault. So the run owns a notification for its whole life.
 *
 * The pump thread is the only thread that touches [NtripBridge]; it
 * publishes immutable documents through [state], which Compose collects.
 */
class MonitorService : Service() {

    /** What the UI observes. */
    data class RunState(
        val running: Boolean = false,
        val document: BridgeDocument? = null,
        val error: String? = null,
    )

    private var worker: Thread? = null
    @Volatile private var stopRequested = false

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> { stopRun(); return START_NOT_STICKY }
            ACTION_START -> startRun(intent)
        }
        return START_NOT_STICKY
    }

    private fun startRun(intent: Intent) {
        if (worker != null) return

        val settings = CasterSettings(
            caster = intent.getStringExtra(EXTRA_CASTER).orEmpty(),
            port = intent.getIntExtra(EXTRA_PORT, 2101),
            mountpoint = intent.getStringExtra(EXTRA_MOUNTPOINT).orEmpty(),
            user = intent.getStringExtra(EXTRA_USER).orEmpty(),
            password = intent.getStringExtra(EXTRA_PASSWORD).orEmpty(),
            latitude = intent.getDoubleExtra(EXTRA_LAT, 52.0),
            longitude = intent.getDoubleExtra(EXTRA_LON, 6.0),
            sendGga = intent.getBooleanExtra(EXTRA_GGA, false),
        )

        createChannel()
        startForeground(NOTIFICATION_ID, buildNotification(getString(R.string.notif_connecting)))

        stopRequested = false
        _state.value = RunState(running = true)

        worker = thread(name = "ntrip-pump") {
            val bridge = NtripBridge.open(
                settings.caster, settings.port, settings.mountpoint,
                settings.user, settings.password,
                settings.latitude, settings.longitude, settings.sendGga,
            )
            if (bridge == null) {
                _state.value = RunState(running = false, error = getString(R.string.err_open))
                stopSelf()
                return@thread
            }

            bridge.use { b ->
                val t0 = SystemClock.elapsedRealtime()
                var lastPublish = -1L

                while (!stopRequested) {
                    val nowS = (SystemClock.elapsedRealtime() - t0) / 1000.0
                    val alive = b.pump(PUMP_TIMEOUT_MS, nowS) >= 0

                    // One document per second: the C side recomputes the
                    // snapshot at 1 Hz, so polling faster only burns battery.
                    val nowMs = SystemClock.elapsedRealtime()
                    if (nowMs - lastPublish >= 1000) {
                        lastPublish = nowMs
                        b.snapshotJson()?.let { json ->
                            runCatching { bridgeJson.decodeFromString<BridgeDocument>(json) }
                                .onSuccess { doc ->
                                    _state.value = RunState(running = true, document = doc)
                                    updateNotification(doc)
                                }
                        }
                    }

                    if (!alive) break
                    val verdict = b.overall()
                    if (verdict == RunVerdict.OK.ordinal || verdict == RunVerdict.FAILED.ordinal) break
                }
            }

            _state.value = _state.value.copy(running = false)
            worker = null
            stopForegroundCompat()
            stopSelf()
        }
    }

    private fun stopRun() {
        stopRequested = true
        worker?.join(2000)
        worker = null
        _state.value = _state.value.copy(running = false)
        stopForegroundCompat()
        stopSelf()
    }

    override fun onDestroy() {
        stopRequested = true
        super.onDestroy()
    }

    // ── Notification ─────────────────────────────────────────────────

    private fun createChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return
        val mgr = getSystemService(NotificationManager::class.java)
        if (mgr.getNotificationChannel(CHANNEL_ID) != null) return
        mgr.createNotificationChannel(
            NotificationChannel(
                CHANNEL_ID,
                getString(R.string.channel_name),
                NotificationManager.IMPORTANCE_LOW,   // silent: it is a progress indicator
            ).apply { description = getString(R.string.channel_desc) }
        )
    }

    private fun buildNotification(text: String): Notification {
        val open = PendingIntent.getActivity(
            this, 0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE,
        )
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle(getString(R.string.notif_title))
            .setContentText(text)
            .setSmallIcon(android.R.drawable.stat_sys_upload)
            .setContentIntent(open)
            .setOngoing(true)
            .setSilent(true)
            .build()
    }

    private fun updateNotification(doc: BridgeDocument) {
        val text = when (doc.kpi.overallEnum) {
            RunVerdict.RUNNING -> getString(
                R.string.notif_running,
                doc.kpi.sustainedS.toInt(), doc.kpi.sustainTargetS.toInt(),
            )
            else -> doc.kpi.overallName
        }
        getSystemService(NotificationManager::class.java)
            .notify(NOTIFICATION_ID, buildNotification(text))
    }

    @Suppress("DEPRECATION")
    private fun stopForegroundCompat() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            stopForeground(STOP_FOREGROUND_REMOVE)
        } else {
            stopForeground(true)
        }
    }

    companion object {
        private const val CHANNEL_ID = "ntrip_run"
        private const val NOTIFICATION_ID = 1
        private const val PUMP_TIMEOUT_MS = 200

        const val ACTION_START = "nl.pe1mew.ntripanalyser.START"
        const val ACTION_STOP = "nl.pe1mew.ntripanalyser.STOP"

        private const val EXTRA_CASTER = "caster"
        private const val EXTRA_PORT = "port"
        private const val EXTRA_MOUNTPOINT = "mountpoint"
        private const val EXTRA_USER = "user"
        private const val EXTRA_PASSWORD = "password"
        private const val EXTRA_LAT = "lat"
        private const val EXTRA_LON = "lon"
        private const val EXTRA_GGA = "gga"

        private val _state = MutableStateFlow(RunState())

        /** Observed by the UI; survives the activity, as the run does. */
        val state: StateFlow<RunState> = _state.asStateFlow()

        fun start(context: Context, s: CasterSettings) {
            val i = Intent(context, MonitorService::class.java).apply {
                action = ACTION_START
                putExtra(EXTRA_CASTER, s.caster)
                putExtra(EXTRA_PORT, s.port)
                putExtra(EXTRA_MOUNTPOINT, s.mountpoint)
                putExtra(EXTRA_USER, s.user)
                putExtra(EXTRA_PASSWORD, s.password)
                putExtra(EXTRA_LAT, s.latitude)
                putExtra(EXTRA_LON, s.longitude)
                putExtra(EXTRA_GGA, s.sendGga)
            }
            context.startForegroundService(i)
        }

        fun stop(context: Context) {
            context.startService(
                Intent(context, MonitorService::class.java).apply { action = ACTION_STOP }
            )
        }
    }
}
