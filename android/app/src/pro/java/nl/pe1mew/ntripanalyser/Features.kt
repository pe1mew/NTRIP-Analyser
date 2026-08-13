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

    /**
     * The GGA uplink can carry the phone's live position, so a network
     * service answers for where the user actually is -- the field
     * question, and what a professional is paid to answer.
     *
     * Transmitted only after an explicit one-time consent, and only when
     * the receiver has a fix; otherwise the configured position is sent,
     * because a GGA of 0,0 puts the rover in the Atlantic and a VRS will
     * answer it.
     */
    const val HAS_LIVE_GGA = true
}
