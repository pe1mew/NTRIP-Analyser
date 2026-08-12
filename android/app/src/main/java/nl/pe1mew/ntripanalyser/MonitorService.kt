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
import android.util.Log
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

    /**
     * How a run ended.
     *
     * Finishing and being stopped are different events and must read
     * differently.  A run that reached its verdict is **finished**;
     * calling that "stopped" borrows the word for aborting and tells the
     * user their measurement was cut short when it was not.
     */
    enum class Outcome { IDLE, RUNNING, FINISHED, STOPPED, LIMIT_REACHED }

    /** What the UI observes. */
    data class RunState(
        val running: Boolean = false,
        val document: BridgeDocument? = null,
        val error: String? = null,
        val outcome: Outcome = Outcome.IDLE,
        /**
         * Whether a sky plot can be shown.
         *
         * Part of the observed state rather than a function the UI polls:
         * Compose recomposes on state changes, so a plain `hasSky()`
         * call would go on returning its first answer for ever.
         */
        val skyAvailable: Boolean = false,
    )

    private var worker: Thread? = null
    private var lastNotificationText: String? = null
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

        // Watch is a property of *this run*, not of the mountpoint: the
        // caster settings say what to connect to, the run mode says how
        // to test it.  So it arrives with the start request and is never
        // persisted alongside credentials.
        val watchMode = intent.getBooleanExtra(EXTRA_WATCH, false) &&
            Features.HAS_WATCH

        val settings = CasterSettings(
            caster = intent.getStringExtra(EXTRA_CASTER).orEmpty(),
            port = intent.getIntExtra(EXTRA_PORT, 2101),
            mountpoint = intent.getStringExtra(EXTRA_MOUNTPOINT).orEmpty(),
            user = intent.getStringExtra(EXTRA_USER).orEmpty(),
            password = intent.getStringExtra(EXTRA_PASSWORD).orEmpty(),
            latitude = intent.getDoubleExtra(EXTRA_LAT, 52.0),
            longitude = intent.getDoubleExtra(EXTRA_LON, 6.0),
            sendGga = intent.getBooleanExtra(EXTRA_GGA, false),
            ephCaster = intent.getStringExtra(EXTRA_EPH_CASTER).orEmpty(),
            ephPort = intent.getIntExtra(EXTRA_EPH_PORT, 2101),
            ephMountpoint = intent.getStringExtra(EXTRA_EPH_MP).orEmpty(),
        )

        createChannel()
        startForeground(NOTIFICATION_ID, buildNotification(getString(R.string.notif_connecting)))

        stopRequested = false
        lastNotificationText = null
        lastSky = null            // a new run supersedes the old coverage
        lastEphCount = 0
        _state.value = RunState(running = true, outcome = Outcome.RUNNING)

        worker = thread(name = "ntrip-pump") {
            val bridge = NtripBridge.open(
                settings.caster, settings.port, settings.mountpoint,
                settings.user, settings.password,
                settings.latitude, settings.longitude, settings.sendGga,
                watchMode,
            )
            if (bridge == null) {
                Log.e(TAG, "bridge_open returned null")
                _state.value = RunState(running = false, error = getString(R.string.err_open),
                                        outcome = Outcome.FINISHED)
                stopSelf()
                return@thread
            }

            Log.i(TAG, "run started: ${settings.caster}:${settings.port}/${settings.mountpoint}")
            usedEphStream = false

            // A user-supplied RINEX file, if one has been imported. Read
            // before any stream is considered: a current file means there
            // is nothing to fetch.
            rinexPath?.let { path ->
                val n = bridge.loadRinex(path)
                Log.i(TAG, "RINEX '$path': $n records")
            }

            liveBridge = bridge

            bridge.use { b ->
                val t0 = SystemClock.elapsedRealtime()
                var lastPublish = -1L
                var endedAtS = -1.0
                var ephOpen = false
                var ephOpenedAtS = 0.0
                var ephRetryAtS = 0.0

                // Publishing is a named action because the final
                // document must be forced out below.  Rate-limiting it
                // alone left the verdict invisible: the loop published at
                // most once a second and then broke on the verdict, so a
                // verdict reached mid-second was never published and the
                // screen kept the previous one -- reading RUNNING 59 of
                // 60 s forever on a run that had in fact passed.
                fun publish(running: Boolean, outcome: Outcome) {
                    lastPublish = SystemClock.elapsedRealtime()
                    b.snapshotJson()?.let { json ->
                        runCatching { bridgeJson.decodeFromString<BridgeDocument>(json) }
                            .onSuccess { doc ->
                                _state.value = RunState(running, doc, null, outcome,
                                                        skyAvailable = liveBridge != null)
                                updateNotification(doc)
                            }
                            .onFailure { Log.w(TAG, "snapshot decode failed", it) }
                    }
                }

                while (!stopRequested) {
                    val nowS = (SystemClock.elapsedRealtime() - t0) / 1000.0
                    val alive = b.pump(PUMP_TIMEOUT_MS, nowS) >= 0

                    // One document per second: the C side recomputes the
                    // snapshot at 1 Hz, so polling faster only burns battery.
                    if (SystemClock.elapsedRealtime() - lastPublish >= 1000) {
                        publish(true, Outcome.RUNNING)
                    }

                    // A spot check is done once it has a verdict.  A watch
                    // is not: the verdict is the thing being observed, and
                    // a station that passes now and fails in an hour is
                    // exactly what the mode exists to catch.  It ends only
                    // when the user stops it.
                    // ── Ephemeris policy ────────────────────────────
                    // Open the stream only when the cache cannot place
                    // what is being tracked; close it the moment it can.
                    // A connection held open for hours to receive a few
                    // messages is rude to the caster and pointless to the
                    // user, so it is borrowed and returned.
                    if (Features.HAS_EPH_STREAM && settings.hasEph) {
                        val (tracked, placeable) = b.coverage()
                        val complete = tracked > 0 && placeable >= tracked

                        if (!ephOpen && !complete && nowS >= ephRetryAtS) {
                            ephOpen = b.openEph(
                                settings.ephCaster, settings.ephPort,
                                settings.ephMountpoint,
                                settings.user, settings.password,
                            )
                            ephOpenedAtS = nowS
                            if (ephOpen) usedEphStream = true
                            Log.i(TAG, "ephemeris stream opened: $placeable of " +
                                "$tracked satellites placeable")
                        } else if (ephOpen &&
                                   (complete || nowS - ephOpenedAtS > EPH_MAX_OPEN_S)) {
                            b.closeEph()
                            ephOpen = false
                            // Do not reopen until the orbits have aged;
                            // an incomplete cache is not a reason to keep
                            // dialling a caster that is not delivering.
                            ephRetryAtS = nowS + EPH_RETRY_S
                            Log.i(TAG, "ephemeris stream closed after " +
                                "${(nowS - ephOpenedAtS).toInt()} s: " +
                                "$placeable of $tracked placeable")
                        }
                    }

                    // Negative means the verdict has settled -- OK or
                    // CAUTION held its window, or a hard failure. A
                    // caution used to leave the run going to its ceiling
                    // because only OK and FAILED counted as done.
                    val verdict = b.overall()
                    if (!watchMode && verdict < 0) {
                        Log.i(TAG, "verdict settled: ${-verdict - 1} after ${nowS.toInt()} s")
                        publish(false, Outcome.FINISHED)   // the state that matters
                        break
                    }

                    // A stream that never opens -- a bad hostname, a
                    // refused login -- used to end the run here, and the
                    // user saw the screen fall silently back to READY.
                    // The KPI engine needs about ten seconds to declare
                    // KPI 1 failed, so keep evaluating until it reaches
                    // that verdict rather than leaving the failure
                    // invisible.  The pump stays cheap: it returns
                    // immediately once the session is finished.
                    if (!alive) {
                        if (endedAtS < 0.0) {
                            endedAtS = nowS
                            Log.w(TAG, "stream ended at ${nowS.toInt()} s; letting the KPI verdict settle")
                        } else if (!watchMode && nowS - endedAtS > STREAM_END_GRACE_S) {
                            publish(false, Outcome.FINISHED)
                            break
                        }
                        Thread.sleep(200)   // pump no longer blocks; do not spin
                    }
                }

                // Render the coverage while the session is still open.
                // `use` closes the bridge on the way out, and a closed
                // bridge renders nothing -- placing this after the block
                // measured a dead handle and reported no ephemerides at
                // all, which looked like a broken ephemeris stream.
                runCatching {
                    val px = IntArray(SKY_SIZE * SKY_SIZE)
                    val ok = b.skyPixels(px, SKY_SIZE, SKY_SIZE)
                    if (ok) {
                        lastSky = px
                        lastEphCount = b.ephCount()
                    }
                    Log.i(TAG, "final sky render: $ok (${b.ephCount()} ephemerides, " +
                        "${b.ephFrames()} eph frames)")
                }.onFailure { Log.w(TAG, "final sky render threw", it) }
            }

            liveBridge = null
            Log.i(TAG, "run finished")

            // Whatever path got here, the run is no longer running.  Keep
            // an outcome already set above; only fill one in if the loop
            // exited by request.
            _state.value = _state.value.copy(
                running = false,
                outcome = if (_state.value.outcome == Outcome.RUNNING) Outcome.STOPPED
                          else _state.value.outcome,
                skyAvailable = lastSky != null,
            )
            worker = null
            stopForegroundCompat()
            stopSelf()
        }
    }

    private fun stopRun() {
        stopRequested = true
        worker?.join(2000)
        worker = null
        _state.value = _state.value.copy(running = false, outcome = Outcome.STOPPED)
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

    private fun buildNotification(text: String, ongoing: Boolean = true): Notification {
        val open = PendingIntent.getActivity(
            this, 0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE,
        )
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle(getString(R.string.notif_title))
            .setContentText(text)
            // The stream flows down: the app receives corrections.
            // Only the optional GGA keep-alive goes the other way.
            .setSmallIcon(android.R.drawable.stat_sys_download)
            .setContentIntent(open)
            .setOngoing(ongoing)
            .setSilent(true)
            .build()
    }

    private fun updateNotification(doc: BridgeDocument) {
        val w = doc.watch
        val text = when {
            // Watching: the headline is the record, not the instant.
            w != null -> getString(
                R.string.notif_watching,
                doc.kpi.overallName, (w.elapsedS / 60).toInt(), w.degradations,
            )
            doc.kpi.overallEnum == RunVerdict.RUNNING -> getString(
                R.string.notif_running,
                doc.kpi.sustainedS.toInt(), doc.kpi.sustainTargetS.toInt(),
            )
            else -> doc.kpi.overallName
        }
        // Only re-post when the text actually changes.  Posting every
        // second ran to 1500+ notifications in a session, which EMUI
        // starts demoting as spam -- and the text only changes once a
        // second at most anyway.
        if (text == lastNotificationText) return
        lastNotificationText = text
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
        // stopForeground detaches the service from the notification, but
        // the notification was also posted directly through notify() to
        // carry progress -- and that posting outlives the detachment on
        // EMUI, leaving an ongoing download icon animating with no run
        // behind it.  Cancel it explicitly.
        getSystemService(NotificationManager::class.java).cancel(NOTIFICATION_ID)
    }

    companion object {
        private const val CHANNEL_ID = "ntrip_run"
        private const val NOTIFICATION_ID = 1
        private const val PUMP_TIMEOUT_MS = 200

        /** How long to keep evaluating after the stream ends,
         *  so the KPI engine can reach a FAILED verdict. */
        private const val STREAM_END_GRACE_S = 15.0

        /** Longest a borrowed ephemeris stream is held before giving up. */
        private const val EPH_MAX_OPEN_S = 120.0

        /** How long before an incomplete cache is worth another attempt. */
        private const val EPH_RETRY_S = 900.0

        /** A RINEX navigation file the user imported, if any. */
        @Volatile
        var rinexPath: String? = null

        /**
         * Whether this run's orbits came off an ephemeris stream.
         *
         * Both sources fill the same cache, so the plot cannot tell them
         * apart afterwards -- and the free edition, which has no stream
         * at all, credited one for orbits that came from the user's own
         * file. Recorded here at the moment the stream opens.
         */
        @Volatile
        var usedEphStream: Boolean = false

        private const val TAG = "ntrip_android"

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
        private const val EXTRA_WATCH = "watch"
        private const val EXTRA_EPH_CASTER = "eph_caster"
        private const val EXTRA_EPH_PORT = "eph_port"
        private const val EXTRA_EPH_MP = "eph_mp"

        /**
         * The running bridge, for the sky screen to render from.
         *
         * The pump thread owns it; rendering is called from a coroutine
         * on Dispatchers.Default. The C session is single-threaded, but
         * sky rendering only reads the accumulated sector grid and never
         * touches the socket, so this is the one safe exception -- and
         * the reason it is exposed rather than the handle itself.
         */
        @Volatile
        private var liveBridge: NtripBridge? = null

        /** The size the sky is rendered and retained at. */
        const val SKY_SIZE = 700

        /** The last completed run's coverage, so it outlives the session. */
        @Volatile
        private var lastSky: IntArray? = null

        /** The ephemeris count at the moment [lastSky] was captured. */
        @Volatile
        private var lastEphCount: Int = 0

        /**
         * Render the current sky coverage, or null when there is nothing
         * to draw: no run, no station position, or no ephemerides yet.
         */
        fun renderSky(pixels: IntArray, w: Int, h: Int): Boolean {
            liveBridge?.let { return it.skyPixels(pixels, w, h) }
            // No run: show the last coverage measured, rather than
            // nothing.  Only at the size it was captured.
            val last = lastSky
            if (last != null && w == SKY_SIZE && h == SKY_SIZE) {
                last.copyInto(pixels)
                return true
            }
            return false
        }

        /** True when there is a sky to show, live or from the last run. */
        fun hasSky(): Boolean = liveBridge != null || lastSky != null

        /**
         * Satellites with a usable ephemeris.
         *
         * Falls back to the completed run's count: the live bridge is
         * gone once a run ends, and reporting 0 beneath a sky plot that
         * plainly has satellites in it reads as a fault.
         */
        fun ephCount(): Int = liveBridge?.ephCount() ?: lastEphCount

        private val _state = MutableStateFlow(RunState())

        /** Observed by the UI; survives the activity, as the run does. */
        val state: StateFlow<RunState> = _state.asStateFlow()

        fun start(context: Context, s: CasterSettings, watch: Boolean = false) {
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
                putExtra(EXTRA_WATCH, watch)
                putExtra(EXTRA_EPH_CASTER, s.ephCaster)
                putExtra(EXTRA_EPH_PORT, s.ephPort)
                putExtra(EXTRA_EPH_MP, s.ephMountpoint)
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
