/**
 * @file sv_ephemeris.c
 * @brief Per-SV Keplerian broadcast-ephemeris cache.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "sv_ephemeris.h"

#include <math.h>
#include <string.h>

/* Static storage is zero-initialised at program start, so sv_eph_init()
 * is purely defensive (and useful between repeated stream sessions). */
static SvEphemeris g_cache[SV_EPH_MAX_GNSS][SV_EPH_MAX_SATS_PER_GNSS];

void sv_eph_init(void)
{
    memset(g_cache, 0, sizeof(g_cache));
}

void sv_eph_store(const SvEphemeris *eph)
{
    if (!eph) return;
    if (eph->gnss_id < 0 || eph->gnss_id >= SV_EPH_MAX_GNSS) return;
    if (eph->prn < 1 || eph->prn > SV_EPH_MAX_SATS_PER_GNSS) return;

    g_cache[eph->gnss_id][eph->prn - 1] = *eph;
    g_cache[eph->gnss_id][eph->prn - 1].valid = true;
}

const SvEphemeris* sv_eph_get(int gnss_id, int prn)
{
    if (gnss_id < 0 || gnss_id >= SV_EPH_MAX_GNSS) return NULL;
    if (prn < 1 || prn > SV_EPH_MAX_SATS_PER_GNSS) return NULL;

    const SvEphemeris *slot = &g_cache[gnss_id][prn - 1];
    return slot->valid ? slot : NULL;
}

bool sv_eph_is_valid_at(const SvEphemeris *eph, int week, double tow_s)
{
    if (!eph || !eph->valid) return false;

    /* Week-of-broadcast is unreliable across systems:
     *   - RTCM 1019 stores GPS week as a 10-bit field (mod 1024); the host
     *     clock yields the full GPS week.  Subtracting them produces a
     *     1024-week offset.
     *   - Galileo's 12-bit week is fine but mixing conventions is fragile.
     *
     * Since broadcast ephemerides are valid for <4 h -- much less than half
     * a week (302400 s) -- we can ignore @p week entirely and compute the
     * offset modulo one week with a half-week wrap.
     *
     * GLONASS is the exception: @c eph->toe holds Moscow seconds-of-day
     * (0..86400), so we wrap on the half-day boundary instead. */
    (void)week;
    double wrap, half_wrap;
    if (eph->gnss_id == 2) {
        wrap      = 86400.0;
        half_wrap = 43200.0;
    } else {
        wrap      = 604800.0;
        half_wrap = 302400.0;
    }
    double dt = tow_s - eph->toe;
    if (dt >  half_wrap) dt -= wrap;
    if (dt < -half_wrap) dt += wrap;
    double abs_dt = fabs(dt);

    double max_dt;
    switch (eph->gnss_id) {
    case 1:  max_dt = 4.0 * 3600.0; break;  /* GPS:     2 h nominal, 4 h grace */
    case 2:  max_dt = 1.0 * 3600.0; break;  /* GLONASS: 30 min nominal, 1 h grace */
    case 3:  max_dt = 30.0 * 60.0;  break;  /* Galileo: 10 min nominal, 30 min grace */
    case 4:  max_dt = 4.0 * 3600.0; break;  /* QZSS:    2 h nominal, 4 h grace (GPS-like) */
    case 5:  max_dt = 6.0 * 3600.0; break;  /* BeiDou:  ~1 h nominal, 6 h grace */
    default: max_dt = 2.0 * 3600.0;
    }
    return abs_dt <= max_dt;
}
