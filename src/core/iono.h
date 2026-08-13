/**
 * @file iono.h
 * @brief Ionospheric disturbance monitoring from dual-frequency MSM7.
 *
 * Measures how *unsettled* the ionosphere is above a base station, as
 * ROTI -- the Rate Of TEC Index -- in TECU per minute.
 *
 * ## Why ROTI and not TEC
 *
 * The geometry-free combination of two carrier phases,
 *
 *     L_gf = phase(f1) - phase(f2)      [metres]
 *
 * cancels geometry, satellite and receiver clocks, and the troposphere,
 * because all of those are identical on both frequencies.  What remains
 * is the ionospheric delay difference plus a **constant** carrier-phase
 * ambiguity that no stream can resolve.
 *
 * Differencing L_gf between epochs cancels that ambiguity exactly. The
 * rate of change is therefore measurable without any calibration, while
 * absolute TEC is not: it would need code levelling (several TECU of
 * noise) plus satellite and receiver differential code biases, which an
 * NTRIP stream does not carry.  So this module reports rates, and states
 * slant TEC only as a value relative to the start of each arc.
 *
 * ## What it cannot tell you
 *
 * RTK is degraded by the ionospheric *gradient between base and rover*,
 * and a single station cannot measure a gradient -- it observes only its
 * own pierce points.  High ROTI at a base is a well-correlated warning
 * that gradients are likely, not a measurement of one.  Anything derived
 * here is labelled accordingly.
 *
 * ## Requirements
 *
 * MSM6 or MSM7 -- the extended-resolution messages -- with two or more
 * frequencies per satellite.  Both carry DF406, the 24-bit fine phase
 * range this is built on; MSM7 adds a Doppler that is not used here.
 * MSM4 and MSM5 do not qualify: their phase range is DF401, 22 bits at
 * a coarser scale.  GLONASS is excluded outright whatever the message:
 * it is FDMA, so each satellite's frequency depends on a channel number
 * that only the 1020 ephemeris carries.
 *
 * Core module: no I/O, no platform headers, no allocation.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * @copyright Apache License 2.0 with Commons Clause (see LICENSE for details)
 */

#ifndef IONO_H
#define IONO_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** gnss_id 1..7; index 0 unused so the id indexes the array directly. */
#define IONO_MAX_GNSS   8

/** The MSM satellite mask is 64 bits, so a PRN never exceeds 64. */
#define IONO_MAX_PRN    64

/**
 * Interval over which one rate-of-TEC sample is formed, seconds.
 *
 * **Not the stream's epoch rate, and this matters more than anything
 * else here.**  The geometry-free combination scales about 7.8 TECU per
 * metre, so 2 mm of carrier-phase noise is ~0.022 TECU. Differenced over
 * one second that is 1.3 TECU/min of pure noise -- larger than most real
 * ionospheric signal, and measured as exactly that when this module
 * first differenced consecutive epochs.  Over 30 s the same noise gives
 * 0.044 TECU/min, comfortably below the signal.
 *
 * 30 s is also the interval in the standard definition (Pi et al.,
 * 1997), for this reason.
 */
#define IONO_ROT_DT_S   30.0

/**
 * ROT samples held per satellite arc.
 *
 * ROTI is conventionally computed over five minutes, which at
 * @ref IONO_ROT_DT_S spacing is ten samples.
 */
#define IONO_ROT_WINDOW 10

/**
 * Seconds without an observation after which an arc is abandoned.
 *
 * A gap longer than this makes the epoch difference meaningless -- the
 * ionosphere will have moved, and any cycle slip in the gap is
 * undetectable -- so the arc restarts rather than producing one enormous
 * false ROT.
 */
#define IONO_ARC_GAP_S  30.0

/** ROTI thresholds, TECU/min, for the reported verdict. */
#define IONO_ROTI_UNSETTLED 0.5
#define IONO_ROTI_DISTURBED 1.0

/** @brief Overall assessment derived from the median ROTI. */
typedef enum {
    IONO_UNKNOWN = 0,   /**< not enough dual-frequency data yet     */
    IONO_QUIET,         /**< below @ref IONO_ROTI_UNSETTLED         */
    IONO_UNSETTLED,     /**< between the two thresholds             */
    IONO_DISTURBED,     /**< above @ref IONO_ROTI_DISTURBED         */
} IonoVerdict;

/**
 * @struct IonoArc
 * @brief One satellite's continuous dual-frequency phase arc.
 *
 * An arc runs from the first usable epoch until a cycle slip or a gap
 * breaks it, at which point the ambiguity changes and the arc restarts.
 */
typedef struct {
    bool   active;          /**< an arc is established                     */
    int    sig_a, sig_b;    /**< signal-mask indices of the pair in use    */
    double f_a, f_b;        /**< their frequencies, Hz                     */
    double last_gf_m;       /**< previous geometry-free value, metres      */
    double last_t;          /**< observation epoch of the previous sample  */
    double seen_at;         /**< caller's clock then, for staleness only    */
    double rot_ref_gf;      /**< geometry-free value at the last ROT sample */
    double rot_ref_t;       /**< and its epoch; ROT spans IONO_ROT_DT_S     */
    int    last_lock;       /**< previous lock-time indicator              */
    double arc_start_t;     /**< when the current arc began                */
    double stec_rel;        /**< slant TEC since arc start, TECU           */
    float  rot[IONO_ROT_WINDOW];  /**< recent ROT samples, TECU/min        */
    int    n_rot;           /**< samples held, up to IONO_ROT_WINDOW       */
    int    rot_head;        /**< ring insertion point                      */
    int    slips;           /**< arcs broken on this satellite this session*/
} IonoArc;

/**
 * @struct IonoState
 * @brief Arc state for every satellite of every constellation.
 *
 * Plain value type: copy it, embed it, zero it.  No cleanup needed.
 */
typedef struct {
    IonoArc arc[IONO_MAX_GNSS][IONO_MAX_PRN];
    int     frames_used;    /**< MSM7 frames that yielded a usable pair    */
    int     frames_seen;    /**< MSM7 frames offered                       */
} IonoState;

/** @brief One satellite's current ionospheric measurement, for display. */
typedef struct {
    int   gnss_id;
    int   prn;
    float roti;             /**< TECU/min; -1 when not yet computable      */
    float stec_rel;         /**< TECU since arc start                      */
    float arc_len_s;        /**< how long the current arc has run          */
    int   slips;
    const char *sig_a;      /**< signal labels of the pair, e.g. "L1C"     */
    const char *sig_b;
} IonoSatView;

/**
 * @struct IonoSummary
 * @brief Point-in-time roll-up, for the health row and the snapshot.
 */
typedef struct {
    int         verdict;        /**< @ref IonoVerdict                      */
    float       roti_median;    /**< across satellites; -1 if unknown      */
    float       roti_max;       /**< worst satellite; -1 if unknown        */
    int         sats_dualfreq;  /**< satellites with a usable pair         */
    int         arcs_active;
    int         slips_total;
} IonoSummary;

/** @brief Clear all arc state.  Call once per session. */
void iono_reset(IonoState *st);

/**
 * @brief Take one RTCM frame and update the arcs it carries.
 *
 * Frames that are not MSM7, and constellations without two usable
 * frequencies, are ignored -- so this can be called for every frame
 * without the caller classifying it.
 *
 * @param st          Arc state to update.
 * @param payload     Frame payload, i.e. the frame **after** its 3-byte header.
 * @param payload_len Payload length in bytes.
 * @param msg_type    RTCM message number.
 * @param now         Current time, seconds, same clock as @ref iono_summarise.
 * @return Number of satellites whose arcs were updated; 0 if the frame
 *         was unusable.
 */
int iono_feed(IonoState *st, const unsigned char *payload, int payload_len,
              int msg_type, double now);

/**
 * @brief Roll the arcs up into a single assessment.
 *
 * Uses the **median** ROTI rather than the mean: a single satellite at
 * low elevation, or one recovering from a slip, produces an outlier that
 * would drag a mean into a false alarm.
 *
 * @param st  Arc state to read.
 * @param now Current time, same clock as @ref iono_feed.
 * @param out [out] Roll-up; zeroed by this call.
 */
void iono_summarise(const IonoState *st, double now, IonoSummary *out);

/**
 * @brief List the satellites currently carrying a measurement.
 *
 * Ordered by constellation then PRN, so a redrawn table does not jump
 * about between refreshes.
 *
 * @param st      Arc state to read.
 * @param now     Current time, same clock as @ref iono_feed.
 * @param out     [out] Destination array.
 * @param max_out Capacity of @p out.
 * @return Entries written.
 */
int iono_sat_view(const IonoState *st, double now,
                  IonoSatView *out, int max_out);

/** @brief Short name for a verdict, e.g. "QUIET". */
const char *iono_verdict_name(int verdict);

#ifdef __cplusplus
}
#endif

#endif /* IONO_H */
