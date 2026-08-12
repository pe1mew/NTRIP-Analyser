package nl.pe1mew.ntripanalyser

/**
 * What the free edition offers.
 *
 * Supplied by the `free` source set, so the paid screens are not merely
 * hidden - they are not compiled into this APK. See
 * `android/design/editions.md` for the split and its reasoning.
 *
 * Nothing here touches the verdict. The seven KPIs, their thresholds and
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
    const val MAX_MOUNTPOINTS = 1

    /**
     * The sourcetable can be read but not used as a picker: the
     * mountpoint is typed by hand. The information is free; the
     * workflow is the paid proposition.
     */
    const val SOURCETABLE_SELECTABLE = false
}
