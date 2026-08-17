/**
 * @file kpi.h
 * @brief Station acceptance test: the eight KPIs as a verdict engine.
 *
 * Implements the Normal-mode check from `android/design/sandBox.md`:
 * seven pass/warn/fail criteria over a live stream, rolled into one
 * verdict -- **STATION OK / CAUTION / FAILED** -- once every criterion
 * has held for a sustained window.
 *
 * Lives in core, per design-review decision D1: one engine serves the
 * Android app, a CLI `--check` acceptance run, and anything else, so a
 * station can never pass on the phone and fail in a script.  The engine
 * only *reads* @ref NsStatsSnapshot; it owns the thresholds and the
 * sustain logic, nothing else.
 *
 * ## The verdict model
 *
 * Each KPI is evaluated per snapshot as PASS / WARN / FAIL, with PENDING
 * while the stream has not yet had time to show the evidence (an ARP is
 * only *missing* after its 30 s allowance, not before).  The overall
 * verdict follows the design's rule:
 *
 *  - every KPI PASS continuously for @ref KPI_SUSTAIN_S  ->  OK
 *  - any WARN (or a FAIL on the soft KPIs 5 or 6)        ->  CAUTION
 *  - FAIL on a hard KPI (1, 2, 3, 4, 7 or 8)             ->  FAILED
 *  - evidence still accumulating                          ->  RUNNING
 *
 * Core module: no I/O, no platform headers, no allocation.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * @copyright Apache License 2.0 with Commons Clause (see LICENSE for details)
 */

#ifndef KPI_H
#define KPI_H

#include <stdbool.h>
#include "core/ns_stats.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Seconds every KPI must hold PASS before the verdict becomes OK. */
#define KPI_SUSTAIN_S       60.0

/** KPI 1: minimum sustained throughput, bytes per second. */
#define KPI_MIN_BYTES_PER_S 100.0

/** KPI 3: seconds allowed for the first 1005/1006 before ARP is missing. */
#define KPI_ARP_DEADLINE_S  30.0

/** KPI 4: slowest acceptable MSM epoch interval, seconds (0.5 Hz). */
#define KPI_MSM_MAX_DT_S    2.0

/** KPI 5: minimum satellites in view (warn below, fail at half). */
#define KPI_MIN_SATS        25

/**
 * @brief Satellites a healthy station is expected to deliver, per
 *        constellation, indexed by the core's 1-based GNSS id.
 *
 * Above a 10-degree mask at mid latitude, and deliberately modest: this
 * decides a verdict, so it is the number a station should comfortably
 * exceed rather than the best it could manage. Measured against our own
 * runs -- Kadaster's APEL00NLD0 delivers 47 across five systems where
 * this table expects 29, and Centipede's NEAR 40 across five where it
 * expects 30.
 *
 * The point of a table rather than one number: a GPS+GLONASS station
 * cannot reach a flat 25 whatever its health, and failing it for that
 * is failing it for its age. See `design/legacy-observations.md`.
 */
#define KPI_EXPECT_SATS { 0, 8, 6, 6, 1, 8, 2, 2 }

/** @brief Fallback when neither the sourcetable nor the stream says. */
#define KPI_EXPECT_UNKNOWN  KPI_MIN_SATS

/** KPI 6: minimum median C/N0, dB-Hz, all bands (design-review D4). */
#define KPI_MIN_CNR_MEDIAN  40.0

/** KPI 7: maximum CRC error rate (1 in 1000). */
#define KPI_MAX_CRC_RATE    0.001

/**
 * KPI 7: frame integrity as the **share of frames that passed CRC**,
 * which is how the question is asked: 100 % is a clean stream and the
 * figure falls as frames fail.  Equivalent to @ref KPI_MAX_CRC_RATE --
 * 1 error in 1000 is 99.9 % passing -- and to tier 2's thresholds, so
 * the two tiers cannot disagree about what a clean stream is.
 */
#define KPI_MIN_INTEGRITY_PCT   99.9
#define KPI_BAD_INTEGRITY_PCT   99.0

/**
 * KPI 7: seconds of stream each integrity reading covers.
 *
 * The rate on the snapshot is cumulative since the session opened, so
 * it dilutes: a burst in the first seconds is diluted by every clean
 * second after it, and a burst late in a long session is diluted by
 * everything before. Measured instead over the window the verdict is
 * held over, which is @ref KPI_SUSTAIN_S -- a KPI has to be able to
 * change within the run, or the sustain clock is timing nothing.
 */
#define KPI_INTEGRITY_WINDOW_S  KPI_SUSTAIN_S

/** @brief Number of KPIs in the rudimentary set. */
#define KPI_COUNT 8

/**
 * @struct KpiPolicy
 * @brief The thresholds a run is conducted under.
 *
 * Every figure above as data rather than as a constant, so that a user
 * can disagree with them — a control network and a hobby base are not
 * held to the same standard, and neither should inherit the other's
 * numbers by accident. See design/work-items/thresholds-track.md, and
 * docs/thresholds.md for what each one means and how well founded it
 * is.
 *
 * **The defaults are the macros above**, so a program that supplies no
 * policy behaves exactly as it always has. That equivalence is what the
 * test suite checks; it is not an aspiration.
 *
 * A policy belongs to a *run*, not to a sample: it is copied into
 * @ref KpiRun at @ref kpi_run_start so that a verdict cannot change
 * standard halfway through, and so the caller's policy need not outlive
 * the call.
 */
typedef struct {
    double sustain_s;          /**< @ref KPI_SUSTAIN_S               */
    double min_bytes_per_s;    /**< @ref KPI_MIN_BYTES_PER_S         */
    double arp_deadline_s;     /**< @ref KPI_ARP_DEADLINE_S          */
    double msm_max_dt_s;       /**< @ref KPI_MSM_MAX_DT_S            */
    int    expect_sats[8];     /**< @ref KPI_EXPECT_SATS, by GNSS id */
    int    expect_unknown;     /**< @ref KPI_EXPECT_UNKNOWN          */
    double min_cnr_median;     /**< @ref KPI_MIN_CNR_MEDIAN          */
    double min_integrity_pct;  /**< @ref KPI_MIN_INTEGRITY_PCT       */
    double bad_integrity_pct;  /**< @ref KPI_BAD_INTEGRITY_PCT       */
    double integrity_window_s; /**< @ref KPI_INTEGRITY_WINDOW_S      */
} KpiPolicy;

/** @brief Fill @p p with the built-in thresholds. */
void kpi_policy_defaults(KpiPolicy *p);

/**
 * @brief Decimal places @ref KpiResult::value is meaningful to.
 *
 * Six of the eight are counts -- frames, satellites, constellations,
 * message types -- and printing one as `40.00` invites the reader to
 * wonder what a hundredth of a satellite is.
 *
 * KPI 7 is why this exists. Its value is a *rate*, and at two decimals
 * an elevated one reads `0.00`: the check window showed
 * `WARN  0.00  Elevated CRC error rate`, a warning whose number said
 * nothing at all. Five decimals shows `0.00430` against the `0.001`
 * this threshold is documented as, so the two compare without the
 * reader converting anything.
 *
 * Here rather than in a frontend, for the same reason the thresholds
 * are: the CLI, the GUI and the phone then print one number one way.
 */
int kpi_value_decimals(int kpi_index);

/**
 * @brief The unit @ref KpiResult::value is in, or "" for a bare count.
 *
 * `1562` and `46.0` and `100.000` in one column are three numbers a
 * reader has to already know the meaning of. Beside the decimals, and
 * in core, so every frontend labels them the same way.
 */
const char *kpi_value_unit(int kpi_index);

/** @brief Verdict for one KPI at one instant. */
typedef enum {
    KPI_PENDING = 0,   /**< too early to judge -- evidence still due   */
    KPI_PASS,
    KPI_WARN,          /**< marginal: within 10 % of its threshold     */
    KPI_FAIL,
} KpiVerdict;

/** @brief Overall verdict of a run. */
typedef enum {
    KPI_RUN_RUNNING = 0,  /**< evidence accumulating, no verdict yet   */
    KPI_RUN_OK,           /**< all seven held PASS for KPI_SUSTAIN_S   */
    KPI_RUN_CAUTION,      /**< warns, or a soft KPI failing            */
    KPI_RUN_FAILED,       /**< a hard KPI failed                       */
} KpiRunVerdict;

/**
 * @brief Which side of its limit a value has to be on to pass.
 *
 * A verdict without the number that decided it cannot be argued with:
 * "Median C/N0 — 45.7 — healthy" invites the question *healthy compared
 * with what?* and answers nothing. So every check that has a limit
 * carries it, in the units its value is already in.
 *
 * @ref KPI_LIMIT_NONE for the structural checks -- whether RTCM decodes
 * at all, whether an ARP has arrived -- which are not comparisons and
 * have no number to show.
 */
typedef enum {
    KPI_LIMIT_NONE = 0,
    KPI_LIMIT_MIN,         /**< pass at or above @ref KpiResult::limit */
    KPI_LIMIT_MAX,         /**< pass at or below it                    */
} KpiLimitDir;

/** @brief One KPI's evaluation, with the number behind it. */
typedef struct {
    int    verdict;        /**< @ref KpiVerdict                        */
    double value;          /**< the measured figure the verdict is on  */
    const char *label;     /**< static short name, e.g. "ARP broadcast" */
    const char *detail;    /**< static explanation of the current state */
    /**
     * The figure @ref value is judged against, in the same units.
     *
     * Set where the verdict is decided rather than written into a
     * frontend, so that a screen cannot show a threshold the engine is
     * not using -- and so that KPI 5, whose expectation depends on the
     * constellations the station streams, can show the number it was
     * actually held to.
     */
    double limit;
    int    limit_dir;      /**< @ref KpiLimitDir                       */
} KpiResult;

/**
 * @struct KpiRun
 * @brief State of one acceptance run.  Plain value type; zero to reset.
 */
typedef struct {
    double t_start;        /**< when the run began (caller's clock)     */
    /**
     * When the current verdict last became stable; <0 when it has not.
     *
     * Stability, not perfection. Timing only unbroken PASS meant a
     * single warning stopped the clock for ever: the run sat at CAUTION
     * until something else ended it and never reached a conclusion. A
     * caution held for a minute is as much a finding as an OK held for
     * a minute.
     */
    double stable_since;
    int    stable_verdict; /**< the verdict being timed                 */
    bool   arp_ever;       /**< a 1005/1006 has been seen this run      */

    /* KPI 7 over a window rather than the session: the counters and the
     * time at the start of the window being measured. */
    uint64_t crc_base_frames;
    uint64_t crc_base_errors;
    double   crc_base_t;
    bool     crc_have_base;
    double   crc_pct;      /**< last completed reading, percent        */
    bool     crc_have_pct;

    /** The thresholds this run is judged by; a copy, so the caller's
     *  policy need not outlive @ref kpi_run_start. */
    KpiPolicy pol;
} KpiRun;

/** @brief A full report: the seven results plus the roll-up. */
typedef struct {
    KpiResult kpi[KPI_COUNT];
    int    overall;        /**< @ref KpiRunVerdict                      */
    double elapsed_s;      /**< since the run began                     */
    double sustained_s;    /**< how long the current verdict has held   */
    /**
     * The verdict has held for @ref KPI_SUSTAIN_S, or failed outright.
     *
     * What a caller waits for: a run is done when its verdict settles,
     * whether that verdict is OK or CAUTION.
     */
    bool   settled;
} KpiReport;

/**
 * @brief Write a check's limit as text: "min 40.0 dB-Hz", "min 29", "".
 *
 * Formatted here rather than in each frontend so every screen shows a
 * user the same sentence, in the check's own units and precision.
 * Writes "" for the structural checks, which have no number.
 *
 * @return @p out, always, so it can be used inline.
 */
const char *kpi_limit_text(const KpiResult *k, int kpi_index,
                           char *out, size_t cap);

/**
 * @brief Begin (or restart) an acceptance run.
 *
 * @param run Run state to initialise.
 * @param now Current time, seconds; same clock as @ref kpi_update.
 * @param pol Thresholds to judge by, copied into the run.  **NULL for
 *            the built-in ones**, which is what every caller passed
 *            before policies existed and what most will pass for ever.
 */
void kpi_run_start(KpiRun *run, double now, const KpiPolicy *pol);

/**
 * @brief Evaluate the eight KPIs against the latest snapshot.
 *
 * Call once per snapshot refresh (typically 1 Hz).  The report is
 * rebuilt in full on every call; the only memory between calls is the
 * sustain clock and whether an ARP has ever been seen.
 *
 * @param run  Run state from @ref kpi_run_start.
 * @param s    Latest statistics snapshot.
 * @param now  Current time, same clock as @ref kpi_run_start.
 * @param out  [out] Report; fully overwritten.
 */
void kpi_update(KpiRun *run, const NsStatsSnapshot *s, double now,
                KpiReport *out);

/**
 * @struct KpiWatch
 * @brief Long-run health accumulator: how a station behaves over hours.
 *
 * A @ref KpiRun answers "does this station pass?" and stops.  A watch
 * keeps asking, and records what it saw: how much of the watch was
 * healthy, how many times health was lost, the longest unbroken healthy
 * stretch, and the worst state reached.  That is the difference between
 * grading a station and observing one -- a base that passes a 90-second
 * check and drops twice an hour is a different base, and only a watch
 * can tell them apart.
 *
 * Timekeeping starts at @ref kpi_watch_start, but *recording* starts
 * when the run first reaches a verdict, so warm-up is not mistaken for
 * misbehaviour.
 *
 * Plain value type; zero to reset.  Fed from the same @ref KpiReport the
 * spot check produces, so both modes judge health identically.
 */
typedef struct {
    double t_start;
    double last_t;          /**< previous update, for interval accounting */
    bool   started;
    /**
     * Set once the run first resolves to OK or FAILED.
     *
     * Nothing is recorded before that.  A run passes through CAUTION on
     * its way up -- satellites are still being counted, C/N0 has no
     * median yet -- and folding that into the record would report
     * "worst: CAUTION" for a station that never once misbehaved, and
     * count a degradation that was only the instrument warming up.
     */
    bool   armed;

    double ok_s;            /**< time held at STATION OK                  */
    double caution_s;
    double failed_s;
    double warmup_s;        /**< time before any verdict was reached      */

    double streak_s;        /**< current unbroken OK stretch              */
    double best_streak_s;   /**< longest such stretch this watch          */

    int    degradations;    /**< transitions out of OK                    */
    int    worst;           /**< worst @ref KpiRunVerdict seen            */
    double last_degrade_t;  /**< elapsed time of the last one; <0 = never */
    int    last_overall;
} KpiWatch;

/**
 * @brief Begin a watch.
 * @param w   Watch state to initialise.
 * @param now Current time, seconds; same clock as @ref kpi_watch_update.
 */
void kpi_watch_start(KpiWatch *w, double now);

/**
 * @brief Fold one report into the watch.
 *
 * Call once per @ref kpi_update, with the same clock.  Intervals are
 * attributed to the state that was in force during them, so a watch
 * paused and resumed does not invent healthy time.
 *
 * @param w   Watch state.
 * @param rep The report just produced.
 * @param now Current time.
 */
void kpi_watch_update(KpiWatch *w, const KpiReport *rep, double now);

/** @brief Seconds since the watch began. */
double kpi_watch_elapsed(const KpiWatch *w, double now);

/** @brief Share of judged time spent at STATION OK, 0..1; <0 if none. */
double kpi_watch_availability(const KpiWatch *w);

/** @brief Short name for a KPI verdict: "PASS", "warn", ... */
const char *kpi_verdict_name(int v);

/** @brief Short name for a run verdict: "STATION OK", "FAILED", ... */
const char *kpi_run_verdict_name(int v);

#ifdef __cplusplus
}
#endif

#endif /* KPI_H */
