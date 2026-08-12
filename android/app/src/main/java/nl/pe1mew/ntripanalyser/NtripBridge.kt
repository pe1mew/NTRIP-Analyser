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

    /**
     * Attach an ephemeris side-stream, enabling the sky plot.
     *
     * The observation stream says which satellites are tracked but not
     * where they are; azimuth and elevation need broadcast ephemerides
     * from a separate mountpoint.
     */
    fun openEph(
        caster: String, port: Int, mountpoint: String,
        user: String, password: String,
    ): Boolean = handle != 0L &&
        nativeOpenEph(handle, caster, port, mountpoint, user, password)

    /** Frames seen on the ephemeris stream, for diagnosis. */
    fun ephFrames(): Int = if (handle == 0L) -1 else nativeEphFrames(handle)

    /** Satellites with a usable ephemeris; 0 means nothing to place. */
    fun ephCount(): Int = if (handle == 0L) 0 else nativeEphCount(handle)

    /**
     * Render the sky heatmap into [pixels] as ARGB.
     *
     * The caller owns the array so the bitmap can be reused between
     * refreshes. Returns false when there is nothing to draw yet.
     */
    fun skyPixels(pixels: IntArray, width: Int, height: Int): Boolean =
        handle != 0L && nativeSkyPixels(handle, pixels, width, height)

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

        /**
         * Fetch and parse a caster's sourcetable.
         *
         * **Blocking** - opens a connection and waits for the caster, so
         * call it off the main thread. Returns null when the caster
         * cannot be reached.
         */
        fun sourcetable(
            caster: String, port: Int, user: String, password: String,
        ): String? = nativeSourcetable(caster, port, user, password)

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
        @JvmStatic private external fun nativeSourcetable(
            caster: String, port: Int, user: String, password: String,
        ): String?
        @JvmStatic private external fun nativePump(h: Long, timeoutMs: Int, nowS: Double): Int
        @JvmStatic private external fun nativeSnapshotJson(h: Long): String?
        @JvmStatic private external fun nativeOverall(h: Long): Int
        @JvmStatic private external fun nativeOpenEph(
            h: Long, caster: String, port: Int, mountpoint: String,
            user: String, password: String,
        ): Boolean
        @JvmStatic private external fun nativeEphCount(h: Long): Int
        @JvmStatic private external fun nativeEphFrames(h: Long): Int
        @JvmStatic private external fun nativeSkyPixels(
            h: Long, pixels: IntArray, width: Int, height: Int,
        ): Boolean
        @JvmStatic private external fun nativeClose(h: Long)
    }
}
