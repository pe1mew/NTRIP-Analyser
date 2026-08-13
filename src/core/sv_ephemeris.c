/**
 * @file sv_ephemeris.c
 * @brief Per-SV broadcast-ephemeris cache (lock-free double-buffer).
 *
 * Each (gnss_id, prn) slot holds *two* SvEphemeris buffers plus an
 * atomic index that names the currently-active buffer.  Writers fill the
 * inactive buffer in place, issue a release-store of the active index,
 * and the swap is visible to readers atomically.  Readers do an
 * acquire-load of the active index, then read the named buffer -- with
 * no lock.  This eliminates the "torn struct" race that affected
 * GLONASS, where a 268-byte non-atomic struct copy from one thread
 * could be interleaved with a read from another, producing a
 * physically-inconsistent (position, velocity, acceleration) tuple and
 * hundred-kilometre propagation errors on the sky plot.
 *
 * Threading model assumed by this implementation:
 *   - ONE writer per slot at a time (per-PRN).  Multiple writers across
 *     different PRNs are fine.  Two writers per slot is the normal case
 *     on a station that broadcasts ephemerides on its observation stream
 *     as well as on a dedicated mountpoint: the obs stream decodes them
 *     too (rtcm_decode_eph), so the CLI's eph worker thread and its obs
 *     thread can both write, as can the GUI's eph worker and its UI
 *     thread (WM_APP_MSG_RAW -> analyze_rtcm_message).  In practice both
 *     decoders see the same broadcast and write the same values, so a
 *     multi-writer collision just wastes the work, not the correctness --
 *     and the double buffer means a reader never sees a torn struct
 *     either way.  Android pumps both sessions on one thread, so there
 *     the question does not arise.
 *
 * Atomic primitives use the GCC/Clang __atomic_* builtins so this file
 * compiles cleanly under both MinGW (GUI + CLI Windows) and Linux GCC
 * (CLI Linux) without needing C11's <stdatomic.h>.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "core/sv_ephemeris.h"

#include <math.h>
#include <string.h>

#define EPH_ATOMIC_LOAD(p)        __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define EPH_ATOMIC_STORE(p, v)    __atomic_store_n((p), (v), __ATOMIC_RELEASE)

typedef struct {
    SvEphemeris  bufs[2];     /**< double buffer; writer fills the inactive one */
    volatile int active;      /**< 0 or 1: index of the buffer readers should use */
} EphSlot;

/* Zero-initialised at program start (BSS) so sv_eph_init() is purely
 * defensive between repeated stream sessions. */
static EphSlot g_slots[SV_EPH_MAX_GNSS][SV_EPH_MAX_SATS_PER_GNSS];

void sv_eph_init(void)
{
    memset(g_slots, 0, sizeof(g_slots));
}

void sv_eph_store(const SvEphemeris *eph)
{
    if (!eph) return;
    if (eph->gnss_id < 0 || eph->gnss_id >= SV_EPH_MAX_GNSS) return;
    if (eph->prn < 1 || eph->prn > SV_EPH_MAX_SATS_PER_GNSS) return;

    EphSlot *slot = &g_slots[eph->gnss_id][eph->prn - 1];

    /* Find the inactive buffer.  Acquire-load is overkill for the writer
     * (we just need the value); use it anyway for symmetry with the
     * reader path. */
    int cur  = EPH_ATOMIC_LOAD(&slot->active);
    int next = 1 - cur;

    /* Write the full struct into the inactive buffer.  Non-atomic --
     * but readers won't see this until we publish the new index below. */
    slot->bufs[next] = *eph;
    slot->bufs[next].valid = true;

    /* Release-store of the active index publishes the new buffer.  All
     * the writes to bufs[next] above happen-before the swap is visible. */
    EPH_ATOMIC_STORE(&slot->active, next);
}

const SvEphemeris* sv_eph_get(int gnss_id, int prn)
{
    if (gnss_id < 0 || gnss_id >= SV_EPH_MAX_GNSS) return NULL;
    if (prn < 1 || prn > SV_EPH_MAX_SATS_PER_GNSS) return NULL;

    EphSlot *slot = &g_slots[gnss_id][prn - 1];

    /* Acquire-load: any read of bufs[idx] below sees the writer's
     * release-store from before its swap, i.e. a fully-written buffer. */
    int idx = EPH_ATOMIC_LOAD(&slot->active);
    const SvEphemeris *e = &slot->bufs[idx];
    return e->valid ? e : NULL;
}

int sv_eph_count(void)
{
    int n = 0;
    /* PRNs are 1-based: sv_eph_get rejects 0, and a loop ending at
     * MAX-1 never looks at the last satellite. */
    for (int g = 0; g < SV_EPH_MAX_GNSS; g++)
        for (int p = 1; p <= SV_EPH_MAX_SATS_PER_GNSS; p++)
            if (sv_eph_get(g, p)) n++;
    return n;
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

    /* Grace periods are deliberately generous: for sky-plot purposes
     * the orbit-extrapolation error after a few hours is still
     * sub-pixel (e.g. ~20 km for Galileo at 4 h = ~1 px on an 800-px
     * plot at 23 000 km).  Tight grace causes visible tracking gaps
     * when the upstream eph stream has even brief hiccups. */
    double max_dt;
    switch (eph->gnss_id) {
    case 1:  max_dt = 4.0 * 3600.0; break;  /* GPS:     2 h nominal, 4 h grace */
    /* GLONASS: 30 min nominal, 4 h grace.  Two hours looked prudent and
     * was not: a daily RINEX file is typically published a couple of
     * hours after its last record, so every GLONASS satellite in it fell
     * outside the window and the constellation vanished from the plot.
     * Measured against the file's own later records, the state-vector
     * propagator drifts 0.1 km at 2 h and 1.9 km at 6 h -- 0.006 deg as
     * seen from the ground, against markers about a degree wide. */
    case 2:  max_dt = 4.0 * 3600.0; break;
    case 3:  max_dt = 4.0 * 3600.0; break;  /* Galileo: 10 min nominal, 4 h grace */
    case 4:  max_dt = 4.0 * 3600.0; break;  /* QZSS:    2 h nominal, 4 h grace (GPS-like) */
    case 5:  max_dt = 6.0 * 3600.0; break;  /* BeiDou:  ~1 h nominal, 6 h grace */
    case 7:  max_dt = 4.0 * 3600.0; break;  /* NavIC:   2 h nominal, 4 h grace (GPS-like) */
    default: max_dt = 2.0 * 3600.0;
    }
    return abs_dt <= max_dt;
}
