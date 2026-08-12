package nl.pe1mew.ntripanalyser

/**
 * Kotlin's half of the JNI boundary.
 *
 * Wraps the native bridge handle in an object with a definite lifetime,
 * so no reachable Kotlin code holds a raw pointer. Every method must be
 * called from the thread that created the instance -- the native session
 * is single-threaded by design, exactly as it is for the desktop tools.
 *
 * The verdict is computed in C, not here: this class carries the JSON
 * across and nothing more. See `ntrip_bridge.h` for why.
 */
class NtripBridge private constructor(private var handle: Long) : AutoCloseable {

    /** True until [close] has run. */
    val isOpen: Boolean get() = handle != 0L

    /**
     * Service the stream and refresh the verdicts.
     *
     * @param timeoutMs how long to wait for bytes.
     * @param nowS caller's clock in seconds; only differences matter.
     * @return >= 0 while the stream is alive, < 0 once it has ended.
     */
    fun pump(timeoutMs: Int, nowS: Double): Int =
        if (handle == 0L) -1 else nativePump(handle, timeoutMs, nowS)

    /** The combined snapshot + KPI document, or null if unavailable. */
    fun snapshotJson(): String? =
        if (handle == 0L) null else nativeSnapshotJson(handle)

    /** Overall verdict as a [RunVerdict] ordinal, without parsing JSON. */
    fun overall(): Int = if (handle == 0L) 0 else nativeOverall(handle)

    override fun close() {
        if (handle != 0L) {
            nativeClose(handle)
            handle = 0L
        }
    }

    companion object {
        init {
            System.loadLibrary("ntrip_android")
        }

        /**
         * Open a session against a caster.
         *
         * Returns immediately; the connection is made by the first
         * [pump] calls, so this never blocks.
         */
        fun open(
            caster: String,
            port: Int,
            mountpoint: String,
            user: String,
            password: String,
            lat: Double,
            lon: Double,
            sendGga: Boolean,
            watch: Boolean,
        ): NtripBridge? {
            val h = nativeOpen(caster, port, mountpoint, user, password, lat, lon, sendGga, watch)
            return if (h == 0L) null else NtripBridge(h)
        }

        /** Open a session replaying a captured `.rtcm3` file. */
        fun openFile(path: String, watch: Boolean = false): NtripBridge? {
            val h = nativeOpenFile(path, watch)
            return if (h == 0L) null else NtripBridge(h)
        }

        @JvmStatic
        private external fun nativeOpen(
            caster: String, port: Int, mountpoint: String,
            user: String, password: String,
            lat: Double, lon: Double, sendGga: Boolean, watch: Boolean,
        ): Long

        @JvmStatic private external fun nativeOpenFile(path: String, watch: Boolean): Long
        @JvmStatic private external fun nativePump(h: Long, timeoutMs: Int, nowS: Double): Int
        @JvmStatic private external fun nativeSnapshotJson(h: Long): String?
        @JvmStatic private external fun nativeOverall(h: Long): Int
        @JvmStatic private external fun nativeClose(h: Long)
    }
}
