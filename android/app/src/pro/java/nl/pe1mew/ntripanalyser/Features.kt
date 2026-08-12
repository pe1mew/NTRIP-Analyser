package nl.pe1mew.ntripanalyser

/**
 * What the paid edition offers.
 *
 * The counterpart of the free edition's object, supplied by the `pro`
 * source set. See `android/design/editions.md`.
 *
 * The paid edition shows *more*, never *different*: the verdict engine
 * is the same `src/core/kpi.c` in both.
 */
object Features {

    const val IS_PRO = true

    /** Watch until the user stops it. */
    const val HAS_WATCH = true


    /**
     * A live ephemeris mountpoint, opened on demand for orbits.
     *
     * The paid capability among the position sources. Both editions read
     * a RINEX navigation file the user supplies, and both fall back to
     * the phone's own GNSS; only pro may borrow a second connection from
     * the caster to fill the cache in seconds.
     */
    const val HAS_EPH_STREAM = true

    const val MAX_MOUNTPOINTS = 16

    const val SOURCETABLE_SELECTABLE = true
}
