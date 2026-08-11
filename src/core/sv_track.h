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
 * MSM satellite mask and the C/N0 from the MSM7 signal block.  Keeping
 * this separate is what lets the monitoring daemon report both from the
 * moment the first frame arrives.
 *
 * **C/N0 is MSM7-only.**  MSM4/5/6 carry no extended C/N0 field, so a
 * stream of those yields satellite counts with `cnr_mean` left at zero,
 * which @ref NsGnssStats documents as "no C/N0 available".
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
void sv_track_summarise(const SvTrack *t, double now, double window_s,
                        NsGnssStats *gnss_out, int max_gnss, int *n_gnss_out,
                        int *sats_total_out, float *cnr_mean_out);

#ifdef __cplusplus
}
#endif

#endif /* SV_TRACK_H */
