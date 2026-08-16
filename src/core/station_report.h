/**
 * @file station_report.h
 * @brief Tier 2: has this station *been* fit, and is it staying that way?
 *
 * The eight KPIs (core/kpi.h) answer whether a station is fit **now**, in
 * about ninety seconds, and that bound is what makes the verdict worth
 * having.  This answers a different question, and one that no
 * ninety-second window can reach at any price: stability over hours.
 *
 * Two rules follow from that difference, and both are load-bearing.
 *
 * **It never borrows tier 1's words.** A station can be fit right now and
 * have been unstable all week, or stable for a month and failing this
 * minute; those are not contradictions and must not read as one. So this
 * says STABLE / DEGRADED / UNSTABLE, and `STATION OK` belongs to the
 * check alone.
 *
 * **Windows are stream time, never wall clock.** The caller supplies the
 * clock, and for a replay it must come from the data — the newest MSM
 * epoch — not from the host.  That is what makes a captured session
 * reproduce its report exactly, at any replay speed, which in turn is
 * what makes an archived `.rtcm3` a record rather than a souvenir.
 *
 * Fed from @ref NsStatsSnapshot, which every frontend already has and the
 * daemon already writes once an interval.  Nothing new is measured here:
 * the skeleton exists to prove the shape before new metrics are funded.
 *
 * See design/work-items/measurement-tiers.md.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#ifndef STATION_REPORT_H
#define STATION_REPORT_H

#include <stdbool.h>
#include "core/ns_stats.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Thresholds ──────────────────────────────────────────────────────
 * Here, never in a frontend.  The rule that makes a free verdict worth
 * as much as a paid one applies to this tier or it is worth less than
 * the first. */

/** Reconnections per hour: an occasional drop, then a flapping link. */
#define SR_RECONNECTS_WARN_PER_H   1.0
#define SR_RECONNECTS_BAD_PER_H    4.0

/** Worst CRC error rate observed.  The warn level is KPI 7's floor. */
#define SR_CRC_WARN                0.001
#define SR_CRC_BAD                 0.01

/** Fall in mean C/N0 from the best the window saw, dB-Hz. */
#define SR_CNR_DROP_WARN           3.0
#define SR_CNR_DROP_BAD            6.0

/** Fewest satellites seen, against what a healthy station holds. */
#define SR_SATS_WARN               25
#define SR_SATS_BAD                15

/** Share of samples where an advertised type was arriving off-rate. */
#define SR_OFFRATE_WARN            0.10
#define SR_OFFRATE_BAD             0.33

/* Ionosphere reuses core/iono.h's own thresholds; space weather does not
 * mean something different here than it does there. */

/** Evidence required before any verdict is offered at all. */
#define SR_MIN_WINDOW_S            600.0
#define SR_MIN_SAMPLES             10

/** @brief One metric's verdict, and the report's roll-up. */
typedef enum {
    SR_INSUFFICIENT = 0,  /**< not enough evidence yet -- a real state  */
    SR_STABLE,
    SR_DEGRADED,          /**< worth looking at; not yet unusable       */
    SR_UNSTABLE,
} SrVerdict;

/** @brief The metrics this skeleton reports, in display order. */
typedef enum {
    SR_AVAILABILITY = 0,  /**< reconnections per hour                   */
    SR_INTEGRITY,         /**< worst CRC error rate                     */
    SR_SIGNAL,            /**< fall in mean C/N0 from the window's best */
    SR_SATELLITES,        /**< fewest satellites held                   */
    SR_IONOSPHERE,        /**< worst median ROTI                        */
    SR_DELIVERY,          /**< share of samples with an off-rate type   */
    SR_METRIC_COUNT,
} SrMetricId;

/** @brief One line of the report. */
typedef struct {
    const char *label;
    double      value;
    int         verdict;   /**< @ref SrVerdict                          */
    bool        live_only; /**< a replay cannot reproduce this          */
    bool        available; /**< false when live_only and built offline  */
    char        detail[96];
} SrMetric;

/** @brief The finished report. */
typedef struct {
    double   window_s;     /**< stream time covered                      */
    int      samples;      /**< snapshots that went into it              */
    bool     from_capture; /**< built from a replay: live-only is absent */
    int      overall;      /**< @ref SrVerdict                           */
    char     headline[160];/**< the worst finding, with its evidence     */
    SrMetric metric[SR_METRIC_COUNT];
} StationReport;

/** @brief Accumulator.  Opaque in spirit; exposed so callers can stack it. */
typedef struct {
    bool     started;
    bool     from_capture;
    double   t_first, t_last;   /**< stream time, seconds                */
    int      samples;

    int      reconnects_first, reconnects_last;
    double   crc_worst;
    float    cnr_best, cnr_last;
    int      sats_min;
    float    roti_worst;
    int      offrate_samples;
} SrState;

/**
 * @brief Begin a report.
 *
 * @param from_capture true when the source is a replayed `.rtcm3`, which
 *        makes the live-only metrics unavailable rather than zero.
 */
void sr_reset(SrState *s, bool from_capture);

/**
 * @brief Add one snapshot.
 *
 * @param t_stream Seconds on the **stream's** clock, not the host's.  For
 *        a live session the two agree; for a replay this must come from
 *        the data, or the report will not reproduce.
 */
void sr_feed(SrState *s, const NsStatsSnapshot *snap, double t_stream);

/** @brief Build the report from what has been fed so far. */
void sr_build(const SrState *s, StationReport *out);

/** @brief "STABLE", "DEGRADED", "UNSTABLE", "INSUFFICIENT EVIDENCE". */
const char *sr_verdict_name(int verdict);

#ifdef __cplusplus
}
#endif

#endif /* STATION_REPORT_H */
