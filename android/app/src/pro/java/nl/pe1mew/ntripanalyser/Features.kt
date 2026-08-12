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

    const val MAX_MOUNTPOINTS = 16

    const val SOURCETABLE_SELECTABLE = true
}
