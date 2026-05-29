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

#include "sky_render.h"

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
int sky_collect_feed_msm(SkyRenderSector *sectors,
                         const unsigned char *payload, int payload_len,
                         int msg_type,
                         double sx, double sy, double sz);

#ifdef __cplusplus
}
#endif

#endif /* SKY_COLLECT_H */
