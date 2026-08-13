/**
 * @file sv_track.h
 * @brief Which satellites a stream is currently carrying, and at what C/N0.
 *
 * A small per-(constellation, PRN) table fed from MSM frames.  It answers
 * the two questions a monitor asks continuously -- how many satellites is
 * this base seeing, and how strong are the signals -- without needing
 * ephemerides or a station position.
 *
 * That independence is the point.  The GUI's satellite view computes
 * azimuth and elevation, which requires a decoded ephemeris per SV *and*
 * a station ARP, so it goes blank whenever either is missing.  Satellite
 * count and C/N0 need neither: the observed PRNs come straight from the
 * MSM satellite mask and the C/N0 from the MSM signal block.  Keeping
 * this separate is what lets the monitoring daemon report both from the
 * moment the first frame arrives.
 *
 * **C/N0 comes from MSM4, 5, 6 and 7** -- every MSM that carries it.
 * MSM1-3 have no C/N0 field at all, and the legacy 1002/1004/1010/1012
 * are not read here yet (`design/legacy-observations.md`), so a stream
 * of either yields satellite counts with `cnr_mean` left at zero, which
 * @ref NsGnssStats documents as "no C/N0 available".
 *
 * Core module: no I/O, no platform headers, no allocation.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#ifndef SV_TRACK_H
#define SV_TRACK_H

#include <stdbool.h>
#include <stdint.h>
#include "core/ns_stats.h"

#ifdef __cplusplus
extern "C" {
#endif

/** gnss_id runs 1..7 (GPS, GLONASS, Galileo, QZSS, BeiDou, SBAS, NavIC);
 *  index 0 is left unused so the id indexes the array directly. */
#define SV_TRACK_MAX_GNSS 8

/** The MSM satellite mask is 64 bits wide, so a PRN never exceeds 64. */
#define SV_TRACK_MAX_PRN  64

/**
 * How long an observation stays "current", in seconds.
 *
 * MSM typically arrives at 1 Hz, so this tolerates a few missed epochs
 * without a satellite flickering out of the count. Too short and the
 * count oscillates with normal jitter; too long and a base that stops
 * transmitting appears healthy for as long as the window lasts.
 */
#define SV_TRACK_STALE_S  5.0

/** @brief One satellite's most recent observation. */
typedef struct {
    double last_seen;   /**< seconds, caller's clock; 0 = never observed */
    float  cnr_dbhz;    /**< last known C/N0; 0 = never carried one      */

    /**
     * Sum of C/N0 expressed as linear power, and the sample count.
     *
     * The mean must be taken over power, never over decibels.  C/N0 is
     * logarithmic, so averaging the dB values yields the geometric mean
     * of the powers, which is biased low -- and the bias grows with the
     * spread, exactly when the number matters.  A satellite that fades
     * and recovers would be reported as steadily poor rather than as
     * intermittently strong.  @ref sv_track_cnr_mean converts back.
     */
    double cnr_pow_sum;
    uint32_t cnr_samples;
} SvTrackSat;

/**
 * @struct SvTrack
 * @brief Observation table for every satellite of every constellation.
 *
 * Plain value type: copy it, embed it, zero it. No cleanup needed.
 */
typedef struct {
    SvTrackSat sat[SV_TRACK_MAX_GNSS][SV_TRACK_MAX_PRN];
} SvTrack;

/** @brief Clear every observation. Call once per session. */
void sv_track_reset(SvTrack *t);

/**
 * @brief Record the satellites carried by one RTCM frame.
 *
 * Non-MSM message types are ignored, so this can be called for every
 * frame without the caller classifying it first.
 *
 * A frame that carries no C/N0 (MSM4/5/6) updates the "seen" timestamps
 * but leaves each satellite's last known C/N0 intact. Letting an
 * interleaved MSM4 frame zero the value would make signal strength drop
 * out on any base that transmits both.
 *
 * @param t           Table to update.
 * @param payload     Frame payload, i.e. the frame **after** its 3-byte header.
 * @param payload_len Payload length in bytes.
 * @param msg_type    RTCM message number.
 * @param now         Current time, seconds, same clock as @ref sv_track_summarise.
 * @return Number of satellites observed in this frame; 0 if not MSM.
 */
int sv_track_feed(SvTrack *t, const unsigned char *payload, int payload_len,
                  int msg_type, double now);

/**
 * @brief Summarise everything observed within @p window_s into snapshot form.
 *
 * Fills the per-constellation array in the order the constellations are
 * indexed, skipping any with no current satellites, so a GPS-only base
 * reports one entry rather than seven empty ones.
 *
 * @param t              Table to read.
 * @param now            Current time, same clock as @ref sv_track_feed.
 * @param window_s       Staleness window; see @ref SV_TRACK_STALE_S.
 * @param gnss_out       [out] Per-constellation summaries.
 * @param max_gnss       Capacity of @p gnss_out.
 * @param n_gnss_out     [out] Entries written.
 * @param sats_total_out [out] Satellites across all constellations.
 * @param cnr_mean_out   [out] Mean C/N0 over every satellite reporting one;
 *                       0 when the stream carries no C/N0 at all.
 */
/**
 * @brief One satellite, as the frontends want to draw it.
 *
 * Positions are not here: an observation stream never carries them, so
 * azimuth and elevation come from whatever source the caller has (the
 * phone's own GNSS, an ephemeris stream, a RINEX file) and are joined
 * to this list by (@ref gnss_id, @ref prn).
 */
typedef struct {
    int   gnss_id;      /**< RTCM constellation numbering              */
    int   prn;
    float cnr_dbhz;     /**< most recent C/N0; 0 when never carried    */
    float cnr_mean;     /**< session mean, averaged in power           */
    uint32_t samples;   /**< C/N0 samples behind @ref cnr_mean         */
    double last_seen;   /**< caller's clock                            */
} SvTrackEntry;

/**
 * @brief Mean C/N0 of one satellite over the session, in dB-Hz.
 *
 * Converts the accumulated power back to decibels.  Returns 0 when the
 * satellite has never carried a C/N0 (MSM4/5/6 do not).
 */
float sv_track_cnr_mean(const SvTrackSat *sat);

/**
 * @brief List the satellites seen within @p window_s of @p now.
 *
 * @param t        Tracker.
 * @param now      Current time, caller's clock.
 * @param window_s Staleness window; older satellites are omitted.
 * @param out      [out] Destination array; NULL to count only.
 * @param max      Capacity of @p out.
 * @return Number written, or the total when @p out is NULL.
 */
int sv_track_list(const SvTrack *t, double now, double window_s,
                  SvTrackEntry *out, int max);

void sv_track_summarise(const SvTrack *t, double now, double window_s,
                        NsGnssStats *gnss_out, int max_gnss, int *n_gnss_out,
                        int *sats_total_out, float *cnr_mean_out);

#ifdef __cplusplus
}
#endif

#endif /* SV_TRACK_H */
