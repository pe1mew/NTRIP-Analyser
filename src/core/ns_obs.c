/**
 * @file ns_obs.c
 * @brief The epoch store declared in ns_obs.h -- three small operations
 *        over a fixed array, kept in core so every frontend shares them.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "core/ns_obs.h"

#include <string.h>

void ns_obs_reset(NsObsEpoch *e, uint32_t epoch_ms)
{
    if (!e) return;
    /* Only the header is cleared: the cell array is written before it is
     * read, and zeroing 7 KB per epoch on a handset buys nothing. */
    e->epoch_ms  = epoch_ms;
    e->gnss_seen = 0;
    e->n         = 0;
    e->dropped   = 0;
    e->open      = true;
    e->tow_gps_ms = -1.0;
    memset(e->sys_epoch, 0, sizeof e->sys_epoch);
}

bool ns_obs_add(NsObsEpoch *e, const NsObsCell *c)
{
    if (!e || !c) return false;
    if (e->n >= NS_OBS_MAX_CELLS) {
        e->dropped++;
        return false;
    }
    e->cell[e->n++] = *c;
    if (c->gnss_id < 32) e->gnss_seen |= (uint32_t)1u << c->gnss_id;
    return true;
}

int ns_obs_sat_count(const NsObsEpoch *e)
{
    if (!e) return 0;

    /* A satellite appears once per signal, so the cells are not the
     * count. Systems are 1..7 and PRNs 1..64 in every MSM, which fits a
     * bitmap small enough to live on the stack. */
    uint64_t seen[8];
    memset(seen, 0, sizeof seen);

    int sats = 0;
    for (int i = 0; i < e->n; i++) {
        int g = e->cell[i].gnss_id;
        int p = e->cell[i].prn;
        if (g < 1 || g > 7 || p < 1 || p > 64) continue;
        uint64_t bit = (uint64_t)1u << (p - 1);
        if (seen[g] & bit) continue;
        seen[g] |= bit;
        sats++;
    }
    return sats;
}
