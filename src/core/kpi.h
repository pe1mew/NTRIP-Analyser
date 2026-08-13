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

/** @brief Number of KPIs in the rudimentary set. */
#define KPI_COUNT 8

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

/** @brief One KPI's evaluation, with the number behind it. */
typedef struct {
    int    verdict;        /**< @ref KpiVerdict                        */
    double value;          /**< the measured figure the verdict is on  */
    const char *label;     /**< static short name, e.g. "ARP broadcast" */
    const char *detail;    /**< static explanation of the current state */
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
 * @brief Begin (or restart) an acceptance run.
 *
 * @param run Run state to initialise.
 * @param now Current time, seconds; same clock as @ref kpi_update.
 */
void kpi_run_start(KpiRun *run, double now);

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
