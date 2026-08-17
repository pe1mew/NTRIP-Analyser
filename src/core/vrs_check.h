/**
 * @file vrs_check.h
 * @brief Network-RTK / VRS assertion set, layered on the seven-KPI test.
 *
 * Turns the desktop's manual VRS checks into pass/fail assertions (the
 * gap recorded as backlog 2.4), in shared core per design-review D2 so
 * the CLI `--check-vrs`, the Android test screen and the GUI can all
 * judge a network service identically.
 *
 * ## Division of labour
 *
 * The **caller** owns the workflow: it opens the session, sends each GGA
 * (via `ns_send_gga()`) and tells this module when, and decides when to
 * enter the optional gate test.  This module owns the **assertions**:
 * it watches the snapshot and the reported times and turns them into
 * verdicts.  That split is what lets a phone drive the same engine with
 * a tapped button where the CLI uses a timer.
 *
 * ## The assertions (from android/design/sandBox.md)
 *
 *  A1  GGA accepted    -- no disconnect within 5 s of the first GGA
 *  A2  RTCM after GGA  -- first CRC-valid frame within 10 s of it
 *  A3  ARP near rover  -- broadcast ARP within 50 km of the GGA position
 *  A4  Keep-alive holds-- stream continuous for 60 s at GGA cadence
 *  A5  GGA-gated       -- after GGA stops, the stream drops within 90 s
 *
 * A5 is a **classification, not a pass/fail**: a physical base ignores
 * GGA and never drops, which is correct behaviour for what it is.  The
 * report says "gated" or "not gated (fixed base?)" rather than failing.
 *
 * Not yet asserted (remaining from the design): station-ID sanity via
 * 1007/1033, and the two-position shift test -- both need caller
 * workflow that does not exist yet on any frontend.
 *
 * Core module: no I/O, no platform headers, no allocation.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * @copyright Apache License 2.0 with Commons Clause (see LICENSE for details)
 */

#ifndef VRS_CHECK_H
#define VRS_CHECK_H

#include <stdbool.h>
#include "core/ns_stats.h"
#include "core/kpi.h"      /* KpiVerdict is reused for the assertions */

#ifdef __cplusplus
extern "C" {
#endif

#define VRS_ACCEPT_S     5.0    /**< A1: disconnect after this is unrelated */
#define VRS_RTCM_S      10.0    /**< A2: first frame deadline after GGA     */
#define VRS_ARP_MAX_KM  50.0    /**< A3: VRS/nearest ARP distance ceiling   */
#define VRS_HOLD_S      60.0    /**< A4: keep-alive window                  */
#define VRS_GATE_S      90.0    /**< A5: drop deadline after GGA stops      */

#define VRS_ASSERT_COUNT 5

/** @brief Gate-test classification (assertion A5). */
typedef enum {
    VRS_GATE_UNTESTED = 0,
    VRS_GATE_TESTING,       /**< GGA stopped, watching for the drop      */
    VRS_GATE_GATED,         /**< dropped after GGA stopped: a real VRS   */
    VRS_GATE_NOT_GATED,     /**< kept streaming: fixed base behaviour    */
} VrsGate;

/**
 * @struct VrsPolicy
 * @brief The deadlines a network-RTK run is tested against.
 *
 * The five values above as data rather than constants, on the same
 * mechanism as @ref KpiPolicy and @ref SrPolicy. They are the **least**
 * likely of this project's thresholds to need changing — they describe
 * what casters actually do rather than what a station ought to achieve
 * — but a network with unusual keep-alive behaviour would otherwise
 * have no recourse at all, and "unlikely to need changing" is not a
 * reason to make something unarguable.
 *
 * Copied into @ref VrsRun at @ref vrs_run_start, so a run cannot change
 * standard halfway and the caller's policy need not outlive the call.
 */
typedef struct {
    double accept_s;    /**< @ref VRS_ACCEPT_S    */
    double rtcm_s;      /**< @ref VRS_RTCM_S      */
    double arp_max_km;  /**< @ref VRS_ARP_MAX_KM  */
    double hold_s;      /**< @ref VRS_HOLD_S      */
    double gate_s;      /**< @ref VRS_GATE_S      */
} VrsPolicy;

/** @brief Fill @p p with the built-in deadlines. */
void vrs_policy_defaults(VrsPolicy *p);

/** @brief State of one VRS test run.  Plain value type; zero to reset. */
typedef struct {
    double t_start;
    double t_first_gga;       /**< <0 until the caller reports one       */
    double gga_lat, gga_lon;  /**< position of the most recent GGA       */
    uint64_t frames_at_gga;   /**< frames_ok when the first GGA went out */
    double t_first_frame;     /**< first frame after the GGA; <0 pending */
    double t_disconnect;      /**< when connected went false; <0 never   */
    bool   was_connected;
    double t_gate_start;      /**< when the caller stopped GGA; <0 none  */

    /** The deadlines this run is judged by; a copy, so the caller's
     *  policy need not outlive @ref vrs_run_start. */
    VrsPolicy pol;
} VrsRun;

/** @brief One assertion's outcome. */
typedef struct {
    int    verdict;        /**< @ref KpiVerdict                          */
    double value;          /**< the figure behind it (s, km, ...)        */
    const char *label;
    const char *detail;
} VrsResult;

/** @brief Full VRS report. */
typedef struct {
    VrsResult a[VRS_ASSERT_COUNT];
    int    gate;           /**< @ref VrsGate                             */
    bool   failed;         /**< any of A1..A4 FAIL                       */
    bool   complete;       /**< A1..A4 resolved (and gate, if started)   */
} VrsReport;

/**
 * @brief Begin a VRS test run.
 *
 * @param pol Deadlines to test against, copied into the run.  **NULL
 *            for the built-in ones**, which is what every caller passed
 *            before policies existed.
 */
void vrs_run_start(VrsRun *run, double now, const VrsPolicy *pol);

/**
 * @brief Tell the engine a GGA was just sent, and from where.
 *
 * Call after every `ns_send_gga()`; the first call starts the A1/A2
 * clocks, later calls only refresh the position used by A3.
 *
 * @param run Run state.
 * @param s   Snapshot at the moment of sending (for the frame counter).
 * @param now Current time, caller's clock.
 * @param lat GGA latitude, degrees.
 * @param lon GGA longitude, degrees.
 */
void vrs_note_gga(VrsRun *run, const NsStatsSnapshot *s, double now,
                  double lat, double lon);

/**
 * @brief Enter the gate test: the caller has stopped sending GGA.
 *
 * Only meaningful once A4 has resolved; A5 then watches for the drop.
 */
void vrs_begin_gate_test(VrsRun *run, double now);

/**
 * @brief Evaluate the assertions against the latest snapshot.
 *
 * Call once per snapshot refresh, like @ref kpi_update.
 *
 * @param run Run state.
 * @param s   Latest snapshot.
 * @param now Current time.
 * @param out [out] Report; fully overwritten.
 */
void vrs_update(VrsRun *run, const NsStatsSnapshot *s, double now,
                VrsReport *out);

/** @brief Name for a gate classification. */
const char *vrs_gate_name(int gate);

#ifdef __cplusplus
}
#endif

#endif /* VRS_CHECK_H */
