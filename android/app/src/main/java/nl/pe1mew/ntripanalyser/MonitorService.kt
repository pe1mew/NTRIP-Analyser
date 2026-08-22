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
import androidx.annotation.RequiresApi
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
     *
     * [LIMIT_REACHED] is the third case: cut short, but by neither the
     * station nor the user.  Android 15 ends a `dataSync` foreground
     * service after about six hours in a day, and a watch that ran into
     * that ceiling measured everything it reports -- it simply stopped
     * measuring earlier than asked.  See [onTimeout].
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
        /**
         * Which run this is.
         *
         * Stamped when a run starts and carried unchanged through every
         * snapshot of it, so a screen can tell "the same run, one second
         * later" from "a different run". The hub uses it to fold its
         * rows shut when a new run begins: what a row had open belonged
         * to the measurement underneath it.
         */
        val runId: Long = 0L,
    )

    private var worker: Thread? = null
    private var lastFailure = 0
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
            ggaLive = intent.getBooleanExtra(EXTRA_GGA_LIVE, false),
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
        lastFailure = 0
        _state.value = RunState(running = true, outcome = Outcome.RUNNING,
                                runId = runId)

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
                                        outcome = Outcome.FINISHED, runId = runId)
                stopSelf()
                return@thread
            }

            Log.i(TAG, "run started: ${settings.caster}:${settings.port}/${settings.mountpoint}")
            usedEphStream = false

            // A user-supplied RINEX file, if one has been imported. Read
            // before any stream is considered: a current file means there
            // is nothing to fetch.
            var rinexSeeded = false
            rinexPath?.let { path ->
                val n = bridge.loadRinex(path)
                rinexSeeded = n > 0
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
                // The best coverage seen and the ephemerides counted off
                // the observation stream, with the moment either last
                // moved: together they say whether orbits are still
                // arriving from what is already connected.
                var bestPlaceable = 0
                var lastObsEph = 0
                var filledAtS = 0.0

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
                                if (doc.stats.failure != 0 &&
                                    doc.stats.failure != lastFailure) {
                                    // Logged once per change, not per
                                    // snapshot: a refused connection
                                    // retries, and a line per attempt
                                    // buries the first one.
                                    lastFailure = doc.stats.failure
                                    Log.w(TAG, "failure ${doc.stats.failure}:" +
                                               " ${doc.stats.failureDetail}")
                                }
                                _state.value = RunState(running, doc, null, outcome,
                                                        skyAvailable = liveBridge != null,
                                                        runId = runId)
                                updateNotification(doc)
                            }
                            .onFailure { Log.w(TAG, "snapshot decode failed", it) }
                    }
                }

                // Whether this run may report where the phone is. Decided
                // once, from the run's own settings, so a consent
                // withdrawn mid-run cannot be second-guessed here -- the
                // UI clears [livePosition], and the uplink falls back to
                // the configured position within one interval.
                val liveGga = settings.sendGga && settings.ggaLive &&
                    Features.HAS_LIVE_GGA
                var reportedLive = false

                while (!stopRequested) {
                    val nowS = (SystemClock.elapsedRealtime() - t0) / 1000.0
                    val alive = b.pump(PUMP_TIMEOUT_MS, nowS) >= 0

                    // The rover moves, so the uplink follows it. Nothing
                    // is sent from here: the C side keeps the cadence and
                    // will not transmit at all unless the mountpoint asked
                    // for GGA. Without a fix this simply does not run, and
                    // the configured position stays in force -- a GGA of
                    // 0,0 is a valid sentence that puts the rover in the
                    // Atlantic, and a VRS will answer it.
                    if (liveGga) {
                        livePosition?.let { fix ->
                            b.setPosition(fix.lat, fix.lon)
                            if (!reportedLive) {
                                reportedLive = true
                                Log.i(TAG, "GGA now reports the phone's own " +
                                    "position (accuracy ${fix.accuracyM} m)")
                            }
                        }
                    }

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
                    // what is being tracked and has stopped trying; close
                    // it the moment it can. A connection held open for
                    // hours to receive a few messages is rude to the
                    // caster and pointless to the user, so it is borrowed
                    // and returned -- and on a station that broadcasts its
                    // own orbits it is never borrowed at all.
                    if (Features.HAS_EPH_STREAM && settings.hasEph) {
                        val (tracked, placeable) = b.coverage()
                        val complete = tracked > 0 && placeable >= tracked

                        // Two signs that nothing needs fetching: coverage
                        // still climbing, or this station still sending
                        // orbits of its own. The second matters on its
                        // own because the first saturates -- 40 of 41
                        // placeable, with a satellite that has just risen
                        // waiting for its turn in the broadcast cycle,
                        // stops climbing and looks stalled. Measured: it
                        // dialled a caster at 179 s for that one
                        // satellite, on a station that was broadcasting
                        // ephemerides throughout.
                        val obsEph = b.obsEph()
                        if (placeable > bestPlaceable || obsEph > lastObsEph) {
                            bestPlaceable = maxOf(bestPlaceable, placeable)
                            lastObsEph = obsEph
                            filledAtS = nowS
                        }
                        val filling = nowS - filledAtS < EPH_FILL_QUIET_S

                        /* An imported navigation file is not a reason to
                         * skip the stream the user configured. It fills
                         * the cache on the first pump, so `complete` is
                         * true before a single frame has arrived and the
                         * stream was never dialled: the sky was drawn
                         * from a file read off the disk while a live
                         * source sat unused, and the header said so.
                         *
                         * The file is a fallback, not a substitute. Where
                         * the station broadcasts its own orbits nothing
                         * is dialled either way -- that stays the best
                         * case, and this only covers the station that
                         * broadcasts none. */
                        val fileIsTheOnlySource =
                            rinexSeeded && obsEph == 0 && !usedEphStream

                        if (!ephOpen && (fileIsTheOnlySource ||
                                         (!complete && !filling)) &&
                            nowS >= ephRetryAtS) {
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
                                   nowS - ephOpenedAtS >= EPH_MIN_OPEN_S &&
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
                    // What placed the satellites, in the one line a field
                    // report is built from: an empty sky and a station
                    // that sends no orbits look identical without it.
                    val fromObs = _state.value.document?.eph?.fromObs ?: 0
                    Log.i(TAG, "final sky render: $ok (${b.ephCount()} orbits cached, " +
                        "$fromObs ephemerides off the observation stream, " +
                        "${b.ephFrames()} frames off the ephemeris stream)")
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

    private fun stopRun() = endRun(Outcome.STOPPED)

    /**
     * Wind the run up and say how it ended.
     *
     * One path for every ending that is not the pump thread's own, so
     * that a second reason to stop cannot quietly acquire a different
     * shutdown: the thread is asked to stop, joined, the notification
     * taken down, and the service released.
     */
    private fun endRun(outcome: Outcome) {
        stopRequested = true
        worker?.join(2000)
        worker = null
        _state.value = _state.value.copy(running = false, outcome = outcome)
        stopForegroundCompat()
        stopSelf()
    }

    /**
     * Android 15 ending a long watch, on its own schedule.
     *
     * An app targeting API 35 may hold a `dataSync` foreground service
     * for about six hours in a day. When that runs out the system calls
     * this and gives the service a few seconds to stop itself; a service
     * that does not is killed with `ForegroundServiceDidNotStopException`
     * -- so an unhandled timeout would turn an overnight watch into a
     * crash report.
     *
     * This is the one ending the user did not ask for, so it is the one
     * that most needs saying. The measurement is kept and the outcome
     * says the system stopped it, rather than borrowing the word for
     * what the user does with the Stop button. A final notification
     * carries the same sentence, because a phone that has been watching
     * for six hours is in a pocket.
     *
     * The two-argument form is the one that matters here: the
     * single-argument `onTimeout(startId)` added in API 34 fires only for
     * `shortService`, which this app never uses.
     *
     * Untested on hardware -- the test handset is Android 10, where this
     * is never called. What is exercised is the path it delegates to,
     * which is the same shutdown the Stop button uses.
     */
    @RequiresApi(35)
    override fun onTimeout(startId: Int, fgsType: Int) {
        Log.w(TAG, "foreground service timed out (type $fgsType); stopping")
        endRun(Outcome.LIMIT_REACHED)
        // Posted after endRun has cancelled the ongoing notification, so
        // this is the one the user finds: dismissible, and not a progress
        // indicator for something that is no longer running.
        getSystemService(NotificationManager::class.java).notify(
            NOTIFICATION_ID + 1,
            buildNotification(getString(R.string.notif_timeout),
                              ongoing = false),
        )
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
        /**
     * The least a borrowed ephemeris stream is given before it is
     * returned.
     *
     * With a navigation file loaded the cache is complete the moment the
     * run starts, so without a floor the stream would be opened and shut
     * on consecutive pumps, having received nothing. A broadcast cycle
     * is about twelve seconds on the stations measured.
     */
    private const val EPH_MIN_OPEN_S = 20.0

    private const val EPH_RETRY_S = 900.0

        /**
         * How long orbits must stop arriving before a stream is dialled.
         *
         * Many stations broadcast ephemerides on the observation stream,
         * which the C side decodes, so the cache often fills with no help
         * at all -- but not instantly, and a policy that only asked "is it
         * complete yet?" opened a second connection on the first pump,
         * before the first frame had arrived. Waiting for the flow to stop
         * asks the right question of any source: something is still
         * feeding the cache, or nothing is and the rest must be fetched.
         *
         * Twenty seconds is set by measurement: caster.centipede.fr/NEAR
         * sends its ephemerides in bursts about twelve seconds apart and
         * placed all 38 tracked satellites within 24 s of connecting. A
         * station that sends none goes quiet from the start and is dialled
         * at 20 s -- late enough to have looked, early enough to be well
         * inside a ~90 s check.
         */
        private const val EPH_FILL_QUIET_S = 20.0

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
        private const val EXTRA_GGA_LIVE = "gga_live"
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

        /**
         * The phone's current position, for the GGA uplink.
         *
         * Published by the UI, which owns the receiver, and read by the
         * pump thread. Null means *do not report a phone position* --
         * before consent, without permission, with no fix yet, or once
         * the screen is gone -- and the run then keeps sending the
         * configured position. Nothing here ever reaches a caster in the
         * free edition, which has no live uplink at all.
         */
        @Volatile
        var livePosition: Fix? = null

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

        /**
         * Which run is current, stamped when one is asked for.
         *
         * In the companion rather than on the service, because a run
         * that ends destroys the service: an instance field would read
         * 0 again afterwards, and the hub -- which folds its rows shut
         * when this changes -- would close them at the end of a run
         * instead of at the start of the next.
         */
        @Volatile private var runId = 0L

        private val _state = MutableStateFlow(RunState())

        /** Observed by the UI; survives the activity, as the run does. */
        val state: StateFlow<RunState> = _state.asStateFlow()

        fun start(context: Context, s: CasterSettings, watch: Boolean = false) {
            // Stamped where the run is asked for, so it is set before
            // any state built from it can be published.
            runId = SystemClock.elapsedRealtime()
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
                // Re-checked here as well as in the UI that offers the
                // switch: this is the single door every run goes through,
                // and transmitting a position without consent is not a
                // mistake to leave to one screen's correctness.
                putExtra(
                    EXTRA_GGA_LIVE,
                    s.ggaLive && Features.HAS_LIVE_GGA &&
                        Settings.liveGgaConsent(context),
                )
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
