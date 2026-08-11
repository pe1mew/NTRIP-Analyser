/**
 * @file sv_track.c
 * @brief Satellite observation tracking -- implementation.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "core/sv_track.h"
#include "core/rtcm3x_parser.h"

#include <string.h>

/* MSM message numbers, matching the range msm_get_epoch() accepts. */
#define MSM_TYPE_MIN 1071
#define MSM_TYPE_MAX 1137

void sv_track_reset(SvTrack *t)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
}

int sv_track_feed(SvTrack *t, const unsigned char *payload, int payload_len,
                  int msg_type, double now)
{
    if (!t || !payload) return 0;
    if (msg_type < MSM_TYPE_MIN || msg_type > MSM_TYPE_MAX) return 0;

    int prns[SV_TRACK_MAX_PRN];
    int gnss_id = 0;
    int n = msm_extract_prns(payload, payload_len, msg_type,
                             prns, SV_TRACK_MAX_PRN, &gnss_id);
    if (n <= 0) return 0;
    if (gnss_id < 1 || gnss_id >= SV_TRACK_MAX_GNSS) return 0;

    for (int i = 0; i < n; i++) {
        int p = prns[i];
        if (p < 1 || p > SV_TRACK_MAX_PRN) continue;
        t->sat[gnss_id][p - 1].last_seen = now;
    }

    /* Only MSM7 carries the extended C/N0 field.  For every other MSM the
     * satellites above stay current while their last known C/N0 is left
     * untouched -- zeroing it here would make signal strength collapse on
     * any base interleaving MSM4 with MSM7. */
    if ((msg_type % 10) == 7) {
        int   cnr_prns[SV_TRACK_MAX_PRN];
        float cnr_vals[SV_TRACK_MAX_PRN];
        int   cnr_gnss = 0;
        int   cn = msm7_extract_cnr(payload, payload_len, msg_type,
                                    cnr_prns, cnr_vals, SV_TRACK_MAX_PRN,
                                    &cnr_gnss);
        if (cnr_gnss >= 1 && cnr_gnss < SV_TRACK_MAX_GNSS) {
            for (int i = 0; i < cn; i++) {
                int p = cnr_prns[i];
                if (p < 1 || p > SV_TRACK_MAX_PRN) continue;
                if (cnr_vals[i] > 0.0f)
                    t->sat[cnr_gnss][p - 1].cnr_dbhz = cnr_vals[i];
            }
        }
    }

    return n;
}

/** @brief Median of @p n floats, sorting @p v in place. */
static float median_inplace(float *v, int n)
{
    if (n <= 0) return 0.0f;
    /* Insertion sort: n is at most SV_TRACK_MAX_PRN and typically ~10. */
    for (int i = 1; i < n; i++) {
        float key = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > key) { v[j + 1] = v[j]; j--; }
        v[j + 1] = key;
    }
    return (n & 1) ? v[n / 2]
                   : (v[n / 2 - 1] + v[n / 2]) * 0.5f;
}

void sv_track_summarise(const SvTrack *t, double now, double window_s,
                        NsGnssStats *gnss_out, int max_gnss, int *n_gnss_out,
                        int *sats_total_out, float *cnr_mean_out)
{
    int    n_gnss     = 0;
    int    sats_total = 0;
    double cnr_sum    = 0.0;
    int    cnr_n      = 0;

    if (!t) {
        if (n_gnss_out)     *n_gnss_out     = 0;
        if (sats_total_out) *sats_total_out = 0;
        if (cnr_mean_out)   *cnr_mean_out   = 0.0f;
        return;
    }

    for (int g = 1; g < SV_TRACK_MAX_GNSS; g++) {
        float  cnrs[SV_TRACK_MAX_PRN];
        int    cnr_count = 0;
        int    tracked   = 0;
        double sum       = 0.0;
        float  lo = 0.0f, hi = 0.0f;

        for (int p = 0; p < SV_TRACK_MAX_PRN; p++) {
            const SvTrackSat *sv = &t->sat[g][p];
            if (sv->last_seen <= 0.0) continue;
            if ((now - sv->last_seen) > window_s) continue;
            tracked++;

            if (sv->cnr_dbhz > 0.0f) {
                if (cnr_count == 0) { lo = hi = sv->cnr_dbhz; }
                else {
                    if (sv->cnr_dbhz < lo) lo = sv->cnr_dbhz;
                    if (sv->cnr_dbhz > hi) hi = sv->cnr_dbhz;
                }
                cnrs[cnr_count++] = sv->cnr_dbhz;
                sum += sv->cnr_dbhz;
            }
        }

        if (tracked == 0) continue;      /* skip constellations not in view */

        sats_total += tracked;
        cnr_sum    += sum;
        cnr_n      += cnr_count;

        if (gnss_out && n_gnss < max_gnss) {
            NsGnssStats *o = &gnss_out[n_gnss++];
            memset(o, 0, sizeof(*o));
            o->gnss_id      = g;
            o->sats_tracked = tracked;
            if (cnr_count > 0) {
                o->cnr_mean   = (float)(sum / (double)cnr_count);
                o->cnr_min    = lo;
                o->cnr_max    = hi;
                o->cnr_median = median_inplace(cnrs, cnr_count);
            }
        }
    }

    if (n_gnss_out)     *n_gnss_out     = n_gnss;
    if (sats_total_out) *sats_total_out = sats_total;
    if (cnr_mean_out)
        *cnr_mean_out = cnr_n ? (float)(cnr_sum / (double)cnr_n) : 0.0f;
}
