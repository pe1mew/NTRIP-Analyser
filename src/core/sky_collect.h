/**
 * @file sky_collect.h
 * @brief CLI sky-heatmap data collector.
 *
 * Accumulates observed/expected counts per sector of the polar sky grid
 * (mirroring gui_state.h SKY_N_EL_BANDS / sky_az_bins_per_band[]).  Each
 * incoming MSM4/5/6/7 RTCM frame is fed in via sky_collect_feed_msm(),
 * which walks all cached ephemerides for that GNSS, propagates them to
 * the station ARP frame, and bumps the corresponding sector counter.
 *
 * No GUI / no threading state of its own.  Intended to be driven from
 * src/main.c when `-s --sky` is set.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#ifndef SKY_COLLECT_H
#define SKY_COLLECT_H

#include "core/sky_render.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reset all sector counters to zero.
 *
 * @param sectors Pointer to a SkyRenderSector grid of
 *   SKY_RENDER_N_EL_BANDS * SKY_RENDER_MAX_AZ_BINS slots.
 */
void sky_collect_reset(SkyRenderSector *sectors);

/**
 * @brief Update sector counters from one parsed MSM RTCM frame.
 *
 * Mirrors the obs-worker sky-update block in gui_thread.c: for every SV
 * in the same GNSS as this MSM that has a valid cached ephemeris and
 * lands above the horizon, increment that sector's `expected` counter
 * (and `observed` too if the SV's PRN was in the MSM sat mask).
 *
 * @param sectors      Sector grid as for sky_collect_reset().
 * @param payload      Pointer to the RTCM frame payload (after 3-byte header).
 * @param payload_len  Payload length in bytes.
 * @param msg_type     RTCM message type (must be 1074..1137 with subtype 4..7).
 * @param sx,sy,sz     Station ARP position in ECEF metres.
 * @return number of SVs that contributed an above-horizon update, or 0 if
 *   the frame was ignored (not an MSM4..7, bad station ARP, no eph).
 */
/**
 * @brief Azimuth and elevation of one satellite, from its cached orbit.
 *
 * The question the sky view asks per satellite, answered from the same
 * ephemeris cache and the same clock the coverage heatmap uses -- so the
 * two views cannot place the same satellite differently.
 *
 * Independent of where the observer's phone is: this is the satellite as
 * seen from the *station*, which is what the plot claims to show.
 *
 * @param gnss_id  RTCM constellation id (1 GPS, 2 GLONASS, ...).
 * @param prn      Satellite number within that constellation.
 * @param sx,sy,sz Station ARP in ECEF metres.
 * @param az_deg   [out] Azimuth, degrees clockwise from north.
 * @param el_deg   [out] Elevation, degrees; may be negative (below the
 *                 horizon), which the caller is left to judge.
 * @return true when an orbit was available and valid for now.
 */
/**
 * @brief GPS week and seconds-of-week for the host clock, now.
 *
 * Exposed because judging an ephemeris means comparing it against the
 * same clock the placement code uses. A caller computing its own drifts
 * from this one -- and a validity test that disagrees with the placement
 * test is worse than none.
 */
void sky_get_gps_time_now(int *week, double *tow_s);

/**
 * @brief Moscow seconds-of-day for the host clock, now.
 *
 * GLONASS ephemerides carry no week and reference Moscow time-of-day, so
 * this is the scale their @c toe lives on.
 */
double sky_get_glo_tod_now(void);

bool sky_azel_for_sat(int gnss_id, int prn,
                      double sx, double sy, double sz,
                      double *az_deg, double *el_deg);

int sky_collect_feed_msm(SkyRenderSector *sectors,
                         const unsigned char *payload, int payload_len,
                         int msg_type,
                         double sx, double sy, double sz);

#ifdef __cplusplus
}
#endif

#endif /* SKY_COLLECT_H */
