/**
 * @file kpi.h
 * @brief Station acceptance test: the seven KPIs as a verdict engine.
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
 *  - FAIL on a hard KPI (1, 2, 3, 4 or 7)                ->  FAILED
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

/** KPI 6: minimum median C/N0, dB-Hz, all bands (design-review D4). */
#define KPI_MIN_CNR_MEDIAN  40.0

/** KPI 7: maximum CRC error rate (1 in 1000). */
#define KPI_MAX_CRC_RATE    0.001

/** @brief Number of KPIs in the rudimentary set. */
#define KPI_COUNT 7

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
    double all_pass_since; /**< when all seven last became PASS; <0 none */
    bool   arp_ever;       /**< a 1005/1006 has been seen this run      */
} KpiRun;

/** @brief A full report: the seven results plus the roll-up. */
typedef struct {
    KpiResult kpi[KPI_COUNT];
    int    overall;        /**< @ref KpiRunVerdict                      */
    double elapsed_s;      /**< since the run began                     */
    double sustained_s;    /**< how long all seven have held PASS       */
} KpiReport;

/**
 * @brief Begin (or restart) an acceptance run.
 *
 * @param run Run state to initialise.
 * @param now Current time, seconds; same clock as @ref kpi_update.
 */
void kpi_run_start(KpiRun *run, double now);

/**
 * @brief Evaluate the seven KPIs against the latest snapshot.
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

/** @brief Short name for a KPI verdict: "PASS", "warn", ... */
const char *kpi_verdict_name(int v);

/** @brief Short name for a run verdict: "STATION OK", "FAILED", ... */
const char *kpi_run_verdict_name(int v);

#ifdef __cplusplus
}
#endif

#endif /* KPI_H */
