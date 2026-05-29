/**
 * @file sky_collect.c
 * @brief CLI sky-heatmap data collector.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "sky_collect.h"
#include "sky_render.h"
#include "rtcm3x_parser.h"
#include "sv_ephemeris.h"
#include "sv_orbit.h"

#include <math.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ── Time helpers (mirror gui_thread.c sky_get_gps_time / sky_get_glo_tod) */
static void sky_get_gps_time_now(int *week, double *tow_s)
{
    const time_t GPS_EPOCH_UNIX = 315964800;   /* 1980-01-06 UTC */
    time_t now = time(NULL);
    double delta = (double)(now - GPS_EPOCH_UNIX);
    int w = (int)(delta / 604800.0);
    if (week)  *week  = w;
    if (tow_s) *tow_s = delta - (double)w * 604800.0;
}

static double sky_get_glo_tod_now(void)
{
    time_t now = time(NULL);
    double utc_sod = (double)(now % 86400);
    double msk = utc_sod + 10800.0;
    while (msk >= 86400.0) msk -= 86400.0;
    while (msk <    0.0)   msk += 86400.0;
    return msk;
}

/* ── Geometry helpers (mirror gui_state.h SKY_N_EL_BANDS layout) ─────── */
static void azel_to_sector(double az_deg, double el_deg,
                           int *band_out, int *bin_out)
{
    int band = (int)(el_deg / 10.0);
    if (band < 0) band = 0;
    if (band >= SKY_RENDER_N_EL_BANDS) band = SKY_RENDER_N_EL_BANDS - 1;
    int n_bins = sky_render_az_bins_per_band[band];
    int bin = 0;
    if (n_bins > 1) {
        if (az_deg < 0.0)   az_deg += 360.0;
        if (az_deg >= 360.0) az_deg -= 360.0;
        bin = (int)(az_deg * (double)n_bins / 360.0);
        if (bin < 0) bin = 0;
        if (bin >= n_bins) bin = n_bins - 1;
    }
    if (band_out) *band_out = band;
    if (bin_out)  *bin_out  = bin;
}

/* ── Public API ──────────────────────────────────────────────────────── */
void sky_collect_reset(SkyRenderSector *sectors)
{
    if (!sectors) return;
    memset(sectors, 0,
           sizeof(SkyRenderSector) *
           SKY_RENDER_N_EL_BANDS * SKY_RENDER_MAX_AZ_BINS);
}

int sky_collect_feed_msm(SkyRenderSector *sectors,
                         const unsigned char *payload, int payload_len,
                         int msg_type,
                         double sx, double sy, double sz)
{
    if (!sectors || !payload || payload_len < 14) return 0;
    int subtype = msg_type % 10;
    if (msg_type < 1070 || msg_type > 1139 || subtype < 4 || subtype > 7) return 0;

    int prns[64];
    int gnss_id = 0;
    int n_prns = msm_extract_prns(payload, payload_len, msg_type,
                                  prns, 64, &gnss_id);
    if (n_prns <= 0 || gnss_id == 0) return 0;

    /* The GUI gates the sky update to gnss_id in {1,2,3,4,5,7}; NavIC (7)
     * shares the GPS-style Keplerian propagator.  Anything else either
     * has no ephemeris support yet or is intentionally excluded. */
    if (!(gnss_id == 1 || gnss_id == 2 || gnss_id == 3 ||
          gnss_id == 4 || gnss_id == 5 || gnss_id == 7))
        return 0;

    int gps_week;
    double gps_tow;
    sky_get_gps_time_now(&gps_week, &gps_tow);
    double glo_tod = sky_get_glo_tod_now();
    double t_prop  = (gnss_id == 2) ? glo_tod : gps_tow;

    /* O(1) "was this PRN observed this frame?" test. */
    uint64_t obs_mask = 0;
    for (int i = 0; i < n_prns; i++) {
        int p = prns[i];
        if (p >= 1 && p <= 64) obs_mask |= (uint64_t)1 << (p - 1);
    }

    int contributed = 0;
    for (int p = 1; p <= SV_EPH_MAX_SATS_PER_GNSS; p++) {
        const SvEphemeris *eph = sv_eph_get(gnss_id, p);
        if (!eph) continue;
        if (!sv_eph_is_valid_at(eph, gps_week, t_prop)) continue;

        double svx, svy, svz;
        if (!sv_to_ecef(eph, gps_week, t_prop, &svx, &svy, &svz)) continue;

        double az_d, el_d;
        azel_from_ecef(sx, sy, sz, svx, svy, svz, &az_d, &el_d);
        if (el_d <= 0.0) continue;

        int band, bin;
        azel_to_sector(az_d, el_d, &band, &bin);
        SkyRenderSector *s = &sectors[band * SKY_RENDER_MAX_AZ_BINS + bin];
        s->expected++;
        if (p >= 1 && p <= 64 && ((obs_mask >> (p - 1)) & 1ULL))
            s->observed++;
        contributed++;
    }
    return contributed;
}
