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
 * clock, and it must be @ref NsStatsSnapshot::stream_time_s — elapsed
 * time as the observation epochs measure it — not the host's.  That is
 * what makes a captured session reproduce its report exactly, at any
 * replay speed, which in turn is what makes an archived `.rtcm3` a
 * record rather than a souvenir.  It is the better clock live too: an
 * NTP correction steps the host's sideways and cannot step this one.
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
#include <stddef.h>
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

/**
 * Frames that must accumulate before an interval's CRC rate is judged.
 *
 * The rate on the snapshot is **cumulative since the session opened**,
 * and the maximum of a running mean is not what this metric claims to
 * report. It fails in both directions:
 *
 *   - **The first seconds dominate.** Two errors against a small early
 *     denominator read as 0.93 % and stay the "worst" for the rest of
 *     the run, while the session settles at 0.43 %. Seen on a live
 *     stream, which is how this was found.
 *   - **Later damage is invisible.** After six hours a station has
 *     ~130 000 frames; a burst of fifty corrupted ones moves the
 *     cumulative rate to 0.04 %, far below whatever was banked early,
 *     so the maximum never notices. The bad ten minutes this metric
 *     exists to catch is exactly what it would miss.
 *
 * So the rate is measured **per interval** -- errors and frames since
 * the previous judged sample -- and the worst of those is kept.
 *
 * The size is set by the warn threshold, not by taste: at 2000 frames a
 * single corrupted frame is 0.0005, which is **below** @ref
 * SR_CRC_WARN. Any smaller and one stray error would raise a warning
 * about a rate, when what happened was an event. Two errors reach the
 * warn level, twenty reach the bad one.
 *
 * An interval short of this is not judged and not discarded: it keeps
 * accumulating until there are enough frames to say something. A slow
 * station is judged over a longer stretch rather than never.
 */
#define SR_CRC_MIN_FRAMES          2000

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

/**
 * Seconds at the start of a session that are not a measurement of the
 * station.
 *
 * `sats_total` and `cnr_mean_all` describe the last five seconds, so a
 * sample taken while the first epoch is still arriving sees a partial
 * constellation — nine satellites where the station holds forty. Left
 * in, that single sample becomes the window's minimum and the report
 * calls a healthy station UNSTABLE. Found on the first real run.
 *
 * Thirty seconds matches KPI 3's allowance for a 1005/1006 to appear,
 * which is the same idea: a station is not answerable for what it has
 * not had time to send.
 */
#define SR_WARMUP_S                30.0

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
    double   crc_worst;         /**< worst *interval* rate, not the mean */
    uint64_t crc_base_frames;   /**< frames at the last judged interval  */
    uint64_t crc_base_errors;   /**< errors at the same point            */
    bool     crc_have_base;
    int      crc_intervals;     /**< intervals judged; 0 = nothing yet   */
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
 * @param t_stream Seconds on the **stream's** clock, not the host's:
 *        @ref NsStatsSnapshot::stream_time_s.  Negative means the stream
 *        carries no epochs to measure with, and such a sample is not
 *        evidence -- the warm-up guard below discards it.
 */
void sr_feed(SrState *s, const NsStatsSnapshot *snap, double t_stream);

/** @brief Build the report from what has been fed so far. */
void sr_build(const SrState *s, StationReport *out);

/** @brief "STABLE", "DEGRADED", "UNSTABLE", "INSUFFICIENT EVIDENCE". */
const char *sr_verdict_name(int verdict);

/**
 * @brief Stable key for a metric: "availability", "integrity", ...
 *
 * These name fields in the published JSON, and Munin has no version
 * negotiation: renaming one silently orphans its RRD history.  Frozen,
 * like every field name the daemon emits (architecture.md §7.3).
 */
const char *sr_metric_key(int metric_id);

/**
 * @brief Decimal places @ref SrMetric::value is meaningful to.
 *
 * Satellites held is a **count**. Printed as `39.000` it invites the
 * reader to wonder what a thousandth of a satellite is, and it
 * contradicts its own detail line, which says "fewest held: 39".
 *
 * The rest are given the precision their detail already states — one
 * decimal for dB-Hz, two for ROTI — so the two halves of a row cannot
 * disagree about how well the thing is known.
 *
 * Here rather than in a frontend: the CLI, the GUI and the daemon's
 * JSON print the same number, and a rule kept in three places is a rule
 * kept in none.
 */
int sr_metric_decimals(int metric_id);

/**
 * @brief Schema version of @ref sr_to_json's output.
 *
 * Separate from @ref NS_STATS_SCHEMA_VERSION: a snapshot and a report
 * are different documents with different lifetimes, and tying them
 * together would force a bump on one for a change to the other.
 *
 * Emitted under the key `report_schema_version`, which is deliberately
 * *not* the snapshot's key name — see the note in the implementation.
 */
#define SR_JSON_SCHEMA_VERSION 1

/**
 * @brief Serialise a report as a single-line JSON object.
 *
 * Flat by design: every metric contributes `<key>_verdict`,
 * `<key>_value` and `<key>_detail` as top-level scalars, so the shell
 * plugin that already reads the snapshot can read this the same way
 * without becoming a JSON parser.
 *
 * A metric that is **unavailable** — live-only, built from a capture —
 * emits `null` for its verdict and value rather than a zero, so a reader
 * cannot mistake "cannot be measured here" for "measured, and fine".
 *
 * @param r          The report.
 * @param mountpoint Written as `"mountpoint"`; may be NULL.
 * @param out        Caller's buffer.
 * @param cap        Its size, including the NUL.
 * @return As snprintf: the length the output would have had.  A value
 *         >= @p cap means it was truncated and must not be used.
 *         Negative on a NULL report.
 */
int sr_to_json(const StationReport *r, const char *mountpoint,
               char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* STATION_REPORT_H */
