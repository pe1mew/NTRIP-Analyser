package nl.pe1mew.ntripanalyser

/**
 * What the free edition offers.
 *
 * Supplied by the `free` source set, so the paid screens are not merely
 * hidden - they are not compiled into this APK. See
 * `android/design/editions.md` for the split and its reasoning.
 *
 * Nothing here touches the verdict. The eight KPIs, their thresholds and
 * the sustain window come from `src/core/kpi.c` and are identical in both
 * editions: a free STATION OK means exactly what a paid one means.
 */
object Features {

    const val IS_PRO = false

    /**
     * Watch mode is a paid capability.
     *
     * The free edition is a spot checker: it answers "does this station
     * pass?" without limit. Answering "does it keep passing?" takes hours
     * of measurement, and that is the paid proposition.
     */
    const val HAS_WATCH = false

    /** One saved mountpoint; the paid edition keeps several. */

    /**
     * A live ephemeris mountpoint, opened on demand for orbits.
     *
     * The paid capability among the position sources. Both editions read
     * a RINEX navigation file the user supplies, and both fall back to
     * the phone's own GNSS; only pro may borrow a second connection from
     * the caster to fill the cache in seconds.
     */
    const val HAS_EPH_STREAM = false

    /** Trails on the sky plot: where each satellite has been. */
    const val HAS_TRACKS = false

    /** The network-RTK check screen is paid; the engine is shared. */
    const val HAS_VRS_CHECK = false

    /** The hand-over view is paid; arp_moves itself is measurement. */
    const val HAS_HANDOVER = false

    /** Statistics export is paid; the report shares the same numbers as prose. */
    const val HAS_EXPORT = false

    const val MAX_MOUNTPOINTS = 1

    /**
     * The sourcetable can be read but not used as a picker: the
     * mountpoint is typed by hand. The information is free; the
     * workflow is the paid proposition.
     */
    const val SOURCETABLE_SELECTABLE = false

    /**
     * The GGA uplink carries a position the user set, never the phone's.
     *
     * Prefilled from the mountpoint's own sourcetable entry -- test as if
     * standing at the station -- which needs no location permission and
     * is always inside the network's coverage. The phone's position is
     * read on this device for the sky view and never leaves it.
     */
    const val HAS_LIVE_GGA = false
}
