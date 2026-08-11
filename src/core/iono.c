/**
 * @file iono.c
 * @brief Ionospheric disturbance monitoring -- implementation.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * @copyright Apache License 2.0 with Commons Clause (see LICENSE for details)
 */

#include "core/iono.h"
#include "core/rtcm3x_parser.h"

#include <string.h>
#include <math.h>

/* Speed of light, m/s.  RTCM ranges are expressed in milliseconds of
 * travel time, so metres = ms * (C / 1000). */
#define IONO_C 299792458.0

/* 40.3 m*Hz^2 per electron/m^2 is the first-order ionospheric constant;
 * one TECU is 1e16 electrons/m^2. */
#define IONO_K 40.3
#define IONO_TECU 1.0e16

/* ── Signal frequencies, indexed exactly as the MSM signal mask ───────
 *
 * The index is the 0-based, MSB-first bit position used throughout the
 * parser, so these tables line up entry-for-entry with the label tables
 * behind msm_signal_label().  0 means "not a usable carrier here".
 *
 * GLONASS is deliberately absent: it is FDMA, so a satellite's frequency
 * depends on a channel number carried only in the 1020 ephemeris, and a
 * single table cannot express it.
 */
#define MHZ(x) ((x) * 1.0e6)

static const double FREQ_GPS[32] = {
    0,        MHZ(1575.42), MHZ(1575.42), MHZ(1575.42), 0, 0, 0, 0,
    MHZ(1227.60), MHZ(1227.60), MHZ(1227.60), 0, 0, 0, MHZ(1227.60), MHZ(1227.60),
    MHZ(1227.60), 0, 0, 0, 0, MHZ(1176.45), MHZ(1176.45), MHZ(1176.45),
    0, 0, 0, 0, 0, MHZ(1575.42), MHZ(1575.42), MHZ(1575.42)
};
static const double FREQ_GAL[32] = {
    0,        MHZ(1575.42), MHZ(1575.42), MHZ(1575.42), MHZ(1575.42), MHZ(1575.42), 0, 0,
    MHZ(1278.75), MHZ(1278.75), MHZ(1278.75), MHZ(1278.75), MHZ(1278.75), 0, MHZ(1207.14), MHZ(1207.14),
    MHZ(1207.14), 0, MHZ(1191.795), MHZ(1191.795), MHZ(1191.795), MHZ(1176.45), MHZ(1176.45), MHZ(1176.45),
    0, 0, 0, 0, 0, 0, 0, 0
};
static const double FREQ_QZS[32] = {
    0,        MHZ(1575.42), 0, 0, 0, 0, 0, 0,
    MHZ(1227.60), 0, 0, 0, 0, 0, MHZ(1227.60), MHZ(1227.60),
    MHZ(1227.60), 0, 0, 0, 0, MHZ(1176.45), MHZ(1176.45), MHZ(1176.45),
    0, 0, 0, 0, 0, MHZ(1575.42), MHZ(1575.42), MHZ(1575.42)
};
static const double FREQ_BDS[32] = {
    0,        MHZ(1561.098), MHZ(1561.098), MHZ(1561.098), 0, 0, 0, 0,
    MHZ(1268.52), MHZ(1268.52), MHZ(1268.52), 0, 0, MHZ(1207.14), MHZ(1207.14), MHZ(1207.14),
    0, 0, 0, 0, 0, MHZ(1176.45), MHZ(1176.45), MHZ(1176.45),
    MHZ(1207.14), MHZ(1207.14), MHZ(1207.14), MHZ(1191.795), MHZ(1191.795), MHZ(1575.42), MHZ(1575.42), MHZ(1575.42)
};
static const double FREQ_NAVIC[32] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, MHZ(1176.45), MHZ(1176.45), MHZ(1176.45),
    0, 0, 0, 0, 0, 0, 0, 0
};

/** @brief Carrier frequency for a signal-mask index, or 0 if unusable. */
static double sig_freq(int gnss_id, int sig_idx)
{
    if (sig_idx < 0 || sig_idx >= 32) return 0.0;
    switch (gnss_id) {
    case 1: return FREQ_GPS[sig_idx];
    case 3: return FREQ_GAL[sig_idx];
    case 4: return FREQ_QZS[sig_idx];
    case 5: return FREQ_BDS[sig_idx];
    case 7: return FREQ_NAVIC[sig_idx];
    default: return 0.0;   /* 2 = GLONASS (FDMA), 6 = SBAS */
    }
}

void iono_reset(IonoState *st)
{
    if (st) memset(st, 0, sizeof(*st));
}

const char *iono_verdict_name(int verdict)
{
    switch (verdict) {
    case IONO_QUIET:     return "QUIET";
    case IONO_UNSETTLED: return "UNSETTLED";
    case IONO_DISTURBED: return "DISTURBED";
    default:             return "UNKNOWN";
    }
}

/**
 * @brief Convert a geometry-free phase difference to slant TEC.
 *
 * L_gf = K * TEC * (1/fa^2 - 1/fb^2), so TEC follows by division. The
 * sign convention does not matter here: only rates are reported, and a
 * consistent sign is enough for those.
 */
static double gf_to_tecu(double gf_m, double fa, double fb)
{
    double inv = 1.0 / (fa * fa) - 1.0 / (fb * fb);
    if (fabs(inv) < 1e-30) return 0.0;
    return gf_m / (IONO_K * inv) / IONO_TECU;
}

/** @brief Push one ROT sample into an arc's ring buffer. */
static void arc_push_rot(IonoArc *a, double rot)
{
    a->rot[a->rot_head] = (float)rot;
    a->rot_head = (a->rot_head + 1) % IONO_ROT_WINDOW;
    if (a->n_rot < IONO_ROT_WINDOW) a->n_rot++;
}

/** @brief Sample standard deviation of an arc's ROT ring, TECU/min. */
static double arc_roti(const IonoArc *a)
{
    if (a->n_rot < 4) return -1.0;       /* too few to mean anything */
    double sum = 0.0;
    for (int i = 0; i < a->n_rot; i++) sum += a->rot[i];
    double mean = sum / a->n_rot;
    double acc = 0.0;
    for (int i = 0; i < a->n_rot; i++) {
        double d = a->rot[i] - mean;
        acc += d * d;
    }
    return sqrt(acc / (a->n_rot - 1));
}

int iono_feed(IonoState *st, const unsigned char *payload, int payload_len,
              int msg_type, double now)
{
    if (!st || !payload) return 0;
    if (msg_type < 1071 || msg_type > 1137 || (msg_type % 10) != 7) return 0;

    int gnss_id;
    if      (msg_type < 1080) gnss_id = 1;
    else if (msg_type < 1090) gnss_id = 2;
    else if (msg_type < 1100) gnss_id = 3;
    else if (msg_type < 1110) gnss_id = 6;
    else if (msg_type < 1120) gnss_id = 4;
    else if (msg_type < 1130) gnss_id = 5;
    else                      gnss_id = 7;
    if (gnss_id >= IONO_MAX_GNSS) return 0;

    st->frames_seen++;

    const int total_bits = payload_len * 8;
    if (total_bits < 169) return 0;

    /* Time the observations by the GNSS epoch in the frame, not by when
     * the bytes arrived.
     *
     * Two reasons.  A satellite is observed once per frame of its own
     * type, and a stream interleaves six of them, so arrival time spaces
     * one satellite's observations several times further apart than they
     * really are -- which scales every rate down by that factor.  And
     * network jitter would otherwise be injected straight into a metric
     * whose whole purpose is to detect small, fast variation.
     *
     * The epoch field is milliseconds (of week for GPS/Galileo/BeiDou,
     * of day for GLONASS, which is excluded here anyway).  Only
     * differences are used, so the origin does not matter. */
    uint32_t epoch_ms = 0;
    double t_obs = now;
    if (msm_get_epoch(payload, payload_len, msg_type, &epoch_ms))
        t_obs = (double)epoch_ms / 1000.0;

    /* MSM header: 64-bit satellite mask at 73, 32-bit signal mask at 137,
     * then the cell mask, one bit per (satellite, signal). */
    int sat_prns[64], num_sats = 0;
    for (int i = 0; i < 64; i++)
        if (get_bits(payload, 73 + i, 1)) {
            if (num_sats < 64) sat_prns[num_sats++] = i + 1;
        }

    int sig_idx_list[32], num_sigs = 0;
    for (int i = 0; i < 32; i++)
        if (get_bits(payload, 137 + i, 1)) {
            if (num_sigs < 32) sig_idx_list[num_sigs++] = i;
        }
    if (num_sats == 0 || num_sigs < 2) return 0;

    const int cell_mask_start = 169;
    const int cell_bits = num_sats * num_sigs;
    if (cell_mask_start + cell_bits > total_bits) return 0;

    /* Satellite data: MSM7 carries rough range (8 + 4 + 10 bits) and
     * rough phase-range rate (14) per satellite.  None of it is needed
     * here -- being per-satellite, it is identical on both signals and
     * cancels in the geometry-free difference. */
    const int sat_block_bits = num_sats * (8 + 4 + 10 + 14);
    const int cell_block_start = cell_mask_start + cell_bits + sat_block_bits;

    int num_cells = 0;
    for (int i = 0; i < cell_bits; i++)
        if (get_bits(payload, cell_mask_start + i, 1)) num_cells++;
    if (num_cells == 0) return 0;

    /* Signal data is stored as parallel arrays across all cells, not as
     * contiguous per-cell blocks: every fine pseudorange, then every fine
     * phase range, and so on.  Reading it as per-cell blocks is the bug
     * that once made C/N0 report impossible values. */
    const int ph_array_start = cell_block_start + num_cells * 20;
    const int lock_array_start = ph_array_start + num_cells * 24;

    if (lock_array_start + num_cells * 10 > total_bits) return 0;

    /* Collect this frame's phase and lock time per (satellite, signal). */
    double ph_ms[64][32];
    int    lock[64][32];
    bool   have[64][32];
    memset(have, 0, sizeof(have));

    int cell_index = 0;
    for (int s = 0; s < num_sats; s++) {
        for (int sg = 0; sg < num_sigs; sg++) {
            if (!get_bits(payload, cell_mask_start + s * num_sigs + sg, 1))
                continue;
            int ci = cell_index++;

            int32_t raw = (int32_t)get_bits(payload, ph_array_start + ci * 24, 24);
            if (raw & 0x800000) raw -= 0x1000000;          /* sign-extend 24 bits */
            if (raw == -8388608) continue;                  /* DF406 invalid */

            /* DF406: fine phase range, 2^-31 milliseconds. */
            ph_ms[s][sg]  = (double)raw * (1.0 / 2147483648.0);
            lock[s][sg]   = (int)get_bits(payload, lock_array_start + ci * 10, 10);
            have[s][sg]   = true;
        }
    }

    int updated = 0;
    for (int s = 0; s < num_sats; s++) {
        int prn = sat_prns[s];
        if (prn < 1 || prn > IONO_MAX_PRN) continue;

        /* Choose the widest usable frequency separation available: the
         * larger the gap, the larger the geometry-free signal relative to
         * phase noise, so the resulting rate is the least noisy. */
        int best_a = -1, best_b = -1;
        double best_fa = 0, best_fb = 0, best_gap = 0;
        for (int i = 0; i < num_sigs; i++) {
            if (!have[s][i]) continue;
            double fi = sig_freq(gnss_id, sig_idx_list[i]);
            if (fi <= 0) continue;
            for (int j = i + 1; j < num_sigs; j++) {
                if (!have[s][j]) continue;
                double fj = sig_freq(gnss_id, sig_idx_list[j]);
                if (fj <= 0) continue;
                double gap = fabs(fi - fj);
                if (gap < 1.0e6) continue;      /* same band, no iono leverage */
                if (gap > best_gap) {
                    best_gap = gap;
                    best_a = i; best_b = j;
                    best_fa = fi; best_fb = fj;
                }
            }
        }
        if (best_a < 0) continue;

        IonoArc *a = &st->arc[gnss_id][prn - 1];

        double gf_m = (ph_ms[s][best_a] - ph_ms[s][best_b]) * (IONO_C / 1000.0);
        int    lk   = lock[s][best_a] < lock[s][best_b]
                          ? lock[s][best_a] : lock[s][best_b];

        /* An arc breaks when the pair changes, the lock-time indicator
         * falls (the receiver re-acquired, so the ambiguity moved), or
         * the gap is too long to difference across. */
        bool restart = !a->active
                       || a->sig_a != sig_idx_list[best_a]
                       || a->sig_b != sig_idx_list[best_b]
                       || lk < a->last_lock
                       || (t_obs - a->last_t) > IONO_ARC_GAP_S
                       || t_obs < a->last_t;          /* epoch rollover */

        if (restart) {
            if (a->active) a->slips++;
            a->active      = true;
            a->sig_a       = sig_idx_list[best_a];
            a->sig_b       = sig_idx_list[best_b];
            a->f_a         = best_fa;
            a->f_b         = best_fb;
            a->arc_start_t = t_obs;
            a->stec_rel    = 0.0;
            a->n_rot       = 0;
            a->rot_head    = 0;
            a->rot_ref_gf  = gf_m;
            a->rot_ref_t   = t_obs;
        } else {
            double dt = t_obs - a->last_t;
            if (dt > 0.05) {                    /* ignore duplicate epochs */
                /* Slant TEC relative to the arc start tracks every epoch:
                 * it is a level, so noise averages out of the picture. */
                a->stec_rel += gf_to_tecu(gf_m - a->last_gf_m,
                                          best_fa, best_fb);
                updated++;
            }

            /* A rate sample is formed only once per IONO_ROT_DT_S.  See
             * the constant's documentation: differencing every epoch
             * measures carrier-phase noise rather than the ionosphere. */
            double span = t_obs - a->rot_ref_t;
            if (span >= IONO_ROT_DT_S) {
                double dtec = gf_to_tecu(gf_m - a->rot_ref_gf,
                                         best_fa, best_fb);
                arc_push_rot(a, dtec / (span / 60.0));   /* TECU per minute */
                a->rot_ref_gf = gf_m;
                a->rot_ref_t  = t_obs;
            }
        }

        a->last_gf_m  = gf_m;
        a->last_t     = t_obs;
        a->seen_at    = now;
        a->last_lock  = lk;
    }

    if (updated) st->frames_used++;
    return updated;
}

/** @brief Insertion sort, ascending; n is small (satellites in view). */
static void sort_f(float *v, int n)
{
    for (int i = 1; i < n; i++) {
        float k = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > k) { v[j + 1] = v[j]; j--; }
        v[j + 1] = k;
    }
}

void iono_summarise(const IonoState *st, double now, IonoSummary *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->roti_median = -1.0f;
    out->roti_max    = -1.0f;
    if (!st) return;

    float vals[IONO_MAX_GNSS * IONO_MAX_PRN];
    int n = 0;

    for (int g = 1; g < IONO_MAX_GNSS; g++) {
        for (int p = 0; p < IONO_MAX_PRN; p++) {
            const IonoArc *a = &st->arc[g][p];
            if (!a->active) continue;
            out->slips_total += a->slips;
            if ((now - a->seen_at) > IONO_ARC_GAP_S) continue;
            out->arcs_active++;
            out->sats_dualfreq++;
            double r = arc_roti(a);
            if (r >= 0.0) vals[n++] = (float)r;
        }
    }

    if (n == 0) {
        out->verdict = IONO_UNKNOWN;
        return;
    }

    sort_f(vals, n);
    out->roti_median = (n & 1) ? vals[n / 2]
                               : (vals[n / 2 - 1] + vals[n / 2]) * 0.5f;
    out->roti_max = vals[n - 1];

    out->verdict = (out->roti_median >= IONO_ROTI_DISTURBED) ? IONO_DISTURBED
                 : (out->roti_median >= IONO_ROTI_UNSETTLED) ? IONO_UNSETTLED
                 : IONO_QUIET;
}

int iono_sat_view(const IonoState *st, double now,
                  IonoSatView *out, int max_out)
{
    if (!st || !out || max_out <= 0) return 0;
    int n = 0;
    for (int g = 1; g < IONO_MAX_GNSS && n < max_out; g++) {
        for (int p = 0; p < IONO_MAX_PRN && n < max_out; p++) {
            const IonoArc *a = &st->arc[g][p];
            if (!a->active || (now - a->seen_at) > IONO_ARC_GAP_S) continue;
            double r = arc_roti(a);
            IonoSatView *v = &out[n++];
            v->gnss_id  = g;
            v->prn      = p + 1;
            v->roti     = (r >= 0.0) ? (float)r : -1.0f;
            v->stec_rel = (float)a->stec_rel;
            v->arc_len_s = (float)(a->last_t - a->arc_start_t);
            v->slips    = a->slips;
            v->sig_a    = msm_signal_label(g, a->sig_a);
            v->sig_b    = msm_signal_label(g, a->sig_b);
        }
    }
    return n;
}
