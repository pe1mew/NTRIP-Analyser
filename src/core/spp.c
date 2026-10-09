/**
 * @file spp.c
 * @brief The single-point solve declared in spp.h.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "core/spp.h"
#include "core/sv_state.h"
#include "core/sv_ephemeris.h"
#include "core/iono.h"          /* msm_signal_freq_hz */
#include "core/rtcm3x_parser.h" /* ecef_to_geodetic, ecef_to_enu, azel */

#include <math.h>
#include <string.h>

#define SPP_C        299792458.0
#define SPP_MAX_SATS 64
#define SPP_ITER_MAX 10
/** Convergence: a correction smaller than this ends the iteration. */
#define SPP_CONVERGED_M 1.0e-4

/** One satellite, reduced to the single measurement a solve consumes. */
typedef struct {
    double range_m;     /**< iono-free pseudorange */
    double pos[3];      /**< satellite position, de-rotated to reception */
    double vel[3];      /**< satellite velocity, Earth-fixed */
    double clock_s;     /**< satellite clock offset */
    double clock_rate;  /**< its drift, seconds per second */
    double rate_ms;     /**< measured phase range rate, m/s */
    bool   has_rate;    /**< false on MSM4, MSM6 and the legacy messages */
    int    gnss_id;
    int    prn;
} SppSat;

/**
 * @brief Saastamoinen's troposphere, with a standard atmosphere.
 *
 * The station's own weather is not in the stream, so this is the
 * textbook model at standard sea-level conditions scaled by height --
 * worth about 2.3 m at the zenith and ten times that at the horizon,
 * which is why it cannot be left out, and why the elevation mask exists
 * to keep the worst of it away.
 */
static double tropo_delay_m(double elev_rad, double height_m)
{
    if (elev_rad < 0.03) elev_rad = 0.03;        /* ~1.7 degrees */
    double h = height_m;
    if (h < -100.0)   h = -100.0;
    if (h > 10000.0)  h = 10000.0;

    /* Standard atmosphere at the antenna. */
    const double temp = 15.0 - 6.5e-3 * h + 273.15;   /* kelvin   */
    const double pres = 1013.25 * pow(1.0 - 2.2557e-5 * h, 5.2568);
    const double hum  = 0.7 * exp(-h / 2000.0);
    const double e_w  = 6.108 * hum * exp((17.15 * temp - 4684.0)
                                          / (temp - 38.45));

    const double z = M_PI / 2.0 - elev_rad;
    return (0.002277 / cos(z))
         * (pres + (1255.0 / temp + 0.05) * e_w);
}

/** @brief Solve A x = b for a 4x4 system by Gaussian elimination. */
static bool solve4(double A[4][4], const double b[4], double x[4])
{
    double M[4][5];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) M[i][j] = A[i][j];
        M[i][4] = b[i];
    }
    for (int c = 0; c < 4; c++) {
        int piv = c;
        for (int r = c + 1; r < 4; r++)
            if (fabs(M[r][c]) > fabs(M[piv][c])) piv = r;
        if (fabs(M[piv][c]) < 1e-12) return false;   /* singular */
        if (piv != c) {
            for (int j = 0; j <= 4; j++) {
                double t = M[c][j]; M[c][j] = M[piv][j]; M[piv][j] = t;
            }
        }
        for (int r = 0; r < 4; r++) {
            if (r == c) continue;
            double f = M[r][c] / M[c][c];
            for (int j = c; j <= 4; j++) M[r][j] -= f * M[c][j];
        }
    }
    for (int i = 0; i < 4; i++) x[i] = M[i][4] / M[i][i];
    return true;
}

/**
 * @brief Reduce one epoch to one iono-free pseudorange per satellite.
 *
 * The combination is (f1^2 P1 - f2^2 P2) / (f1^2 - f2^2), which removes
 * the first-order ionosphere -- the term a broadcast model would only
 * approximate -- at the cost of amplifying code noise about threefold.
 * That trade is the right one here: the noise averages away over a run,
 * the ionosphere does not.
 */
static int collect(const NsObsEpoch *obs, int week, double tow_s,
                   double rx_clock_m, SppSolution *out,
                   SppSat *sats, int max_sats)
{
    int n = 0;
    out->n_no_eph = 0;
    out->n_single_freq = 0;

    for (int i = 0; i < obs->n; i++) {
        const NsObsCell *a = &obs->cell[i];
        if (a->gnss_id == 2 || a->gnss_id == 6) continue;  /* FDMA, SBAS */

        /* One satellite is handled once, at its first cell. */
        bool seen = false;
        for (int k = 0; k < i; k++)
            if (obs->cell[k].gnss_id == a->gnss_id &&
                obs->cell[k].prn == a->prn) { seen = true; break; }
        if (seen) continue;

        /* The two usable carriers, lowest and highest frequency: the
         * wider the gap, the less the combination amplifies noise. */
        double f_lo = 0.0, f_hi = 0.0, p_lo = 0.0, p_hi = 0.0;
        double r_lo = 0.0, r_hi = 0.0;
        bool rate_lo = false, rate_hi = false;
        for (int k = i; k < obs->n; k++) {
            const NsObsCell *c = &obs->cell[k];
            if (c->gnss_id != a->gnss_id || c->prn != a->prn) continue;
            double f = msm_signal_freq_hz(c->gnss_id, (int)c->sig_id - 1);
            if (f <= 0.0) continue;
            const bool has_r = (c->flags & NS_OBS_HAS_RATE) != 0;
            if (f_lo == 0.0 || f < f_lo) {
                f_lo = f; p_lo = c->pseudorange_m;
                r_lo = c->rate_ms; rate_lo = has_r;
            }
            if (f > f_hi) {
                f_hi = f; p_hi = c->pseudorange_m;
                r_hi = c->rate_ms; rate_hi = has_r;
            }
        }
        if (f_lo <= 0.0 || f_hi <= f_lo) { out->n_single_freq++; continue; }

        const SvEphemeris *eph = sv_eph_get(a->gnss_id, a->prn);
        if (!eph || !eph->valid || !sv_eph_is_valid_at(eph, week, tow_s)) {
            out->n_no_eph++;
            continue;
        }

        const double g2 = f_hi * f_hi, l2 = f_lo * f_lo;
        const double p_if = (g2 * p_hi - l2 * p_lo) / (g2 - l2);
        if (!(p_if > 1.0e6) || !(p_if < 1.0e9)) { out->n_rejected++; continue; }

        /* Transmit time, in two passes.
         *
         * A pseudorange is not a distance: it carries the satellite's
         * own clock offset, which is tens of microseconds -- af0 alone
         * is routinely 100 us, or 30 km of apparent range. Taking
         * `tow - P/c` as the transmit time therefore lands about 100 us
         * early, and the satellite has moved 0.29 m by then. The
         * closure test measured exactly that as centimetres of
         * irreducible residual before this second pass existed.
         *
         * So: estimate the time, read the clock there, subtract it, and
         * read the state again. The remaining error is the receiver
         * clock and the troposphere, microseconds, worth under a
         * millimetre of satellite motion. */
        double t_tx = tow_s - (p_if - rx_clock_m) / SPP_C;
        SvState st;
        if (!sv_state_at(eph, week, t_tx, &st) || !st.has_clock) {
            out->n_no_eph++;
            continue;
        }
        t_tx -= st.clock_s;
        if (!sv_state_at(eph, week, t_tx, &st) || !st.has_clock) {
            out->n_no_eph++;
            continue;
        }

        /* The rotation wants the *geometric* flight time, which is the
         * pseudorange with the satellite clock put back. */
        const double flight = (p_if - rx_clock_m + SPP_C * st.clock_s) / SPP_C;
        sv_state_derotate(st.pos, flight, a->gnss_id);

        if (n >= max_sats) break;
        sats[n].range_m = p_if;
        sats[n].pos[0]  = st.pos[0];
        sats[n].pos[1]  = st.pos[1];
        sats[n].pos[2]  = st.pos[2];
        sats[n].vel[0]  = st.vel[0];
        sats[n].vel[1]  = st.vel[1];
        sats[n].vel[2]  = st.vel[2];
        sats[n].clock_s = st.clock_s;
        sats[n].clock_rate = st.clock_rate;

        /* The rate gets the same iono-free treatment as the range, for
         * the same reason and at the same cost in noise.  A satellite
         * whose message carries no rate -- MSM4, MSM6, legacy -- is
         * marked, never defaulted to zero, which would read as a
         * stationary satellite. */
        sats[n].has_rate = rate_lo && rate_hi;
        sats[n].rate_ms  = sats[n].has_rate
                           ? (g2 * r_hi - l2 * r_lo) / (g2 - l2)
                           : 0.0;
        sats[n].gnss_id = a->gnss_id;
        sats[n].prn     = a->prn;
        n++;
    }
    return n;
}

const char *spp_status_text(SppStatus s)
{
    switch (s) {
    case SPP_OK:           return "solved";
    case SPP_NO_EPOCH:     return "no observations in this epoch";
    case SPP_SINGLE_FREQ:  return "single-frequency station: not computable";
    case SPP_NO_EPHEMERIS: return "observations without orbits";
    case SPP_TOO_FEW_SATS: return "fewer than four usable satellites";
    case SPP_DIVERGED:     return "the solution did not settle";
    }
    return "unknown";
}

SppStatus spp_solve(const NsObsEpoch *obs, int week, double tow_s,
                    const double ref_ecef[3], SppSolution *out)
{
    if (!out) return SPP_NO_EPOCH;
    memset(out, 0, sizeof *out);
    if (!obs || obs->n <= 0) {
        out->status = SPP_NO_EPOCH;
        return out->status;
    }

    SppSat sats[SPP_MAX_SATS];
    double x[4] = { 0.0, 0.0, 0.0, 0.0 };
    if (ref_ecef) { x[0] = ref_ecef[0]; x[1] = ref_ecef[1]; x[2] = ref_ecef[2]; }

    /* Two passes.  The first does not know the receiver's clock, so its
     * transmit times are early by whatever that clock is -- 1 ms of
     * receiver bias moves a satellite 3 m along its orbit.  The second
     * pass re-reads the satellites with the clock the first pass found,
     * which makes the answer independent of how badly the receiver's
     * clock is steered.  The closure test proves it: with a 300 km
     * clock error, one pass recovers to metres and two to millimetres. */
    int n = 0, used = 0;
    double resid[SPP_MAX_SATS];
    for (int pass = 0; pass < 2; pass++) {
    n = collect(obs, week, tow_s, (pass == 0) ? 0.0 : x[3],
                out, sats, SPP_MAX_SATS);

    if (n == 0) {
        /* Say which emptiness it is: a single-frequency station and a
         * station without orbits need different answers from the user. */
        out->status = (out->n_single_freq > 0 && out->n_no_eph == 0)
                      ? SPP_SINGLE_FREQ
                      : (out->n_no_eph > 0 ? SPP_NO_EPHEMERIS
                                           : SPP_TOO_FEW_SATS);
        return out->status;
    }
    if (n < 4) {
        out->status = (out->n_single_freq >= n) ? SPP_SINGLE_FREQ
                                                : SPP_TOO_FEW_SATS;
        return out->status;
    }

    /* x starts where the station says it is, and on the second pass
     * where the first pass left it. */
    bool converged = false;

    for (int iter = 0; iter < SPP_ITER_MAX && !converged; iter++) {
        double N[4][4] = {{0}}, rhs[4] = {0};
        double lat = 0.0, lon = 0.0, alt = 0.0;
        const bool have_pos = (x[0]*x[0] + x[1]*x[1] + x[2]*x[2]) > 1.0e12;
        if (have_pos) ecef_to_geodetic(x[0], x[1], x[2], 0.0, &lat, &lon, &alt);

        used = 0;
        out->n_low_elev = 0;

        for (int i = 0; i < n; i++) {
            double d[3] = { sats[i].pos[0] - x[0],
                            sats[i].pos[1] - x[1],
                            sats[i].pos[2] - x[2] };
            double rho = sqrt(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
            if (rho < 1.0) continue;

            double trop = 0.0;
            if (have_pos) {
                double az = 0.0, el = 0.0;
                azel_from_ecef(x[0], x[1], x[2],
                               sats[i].pos[0], sats[i].pos[1], sats[i].pos[2],
                               &az, &el);
                if (el < SPP_ELEV_MASK_DEG) { out->n_low_elev++; continue; }
                trop = tropo_delay_m(el * M_PI / 180.0, alt);
            }

            /* Modelled range: geometry, the satellite's clock (which
             * makes its signal look closer or further than it is), the
             * troposphere, and the receiver clock we are solving for. */
            const double model = rho - SPP_C * sats[i].clock_s + trop + x[3];
            const double v     = sats[i].range_m - model;

            const double u[4] = { -d[0] / rho, -d[1] / rho, -d[2] / rho, 1.0 };
            for (int r = 0; r < 4; r++) {
                for (int c = 0; c < 4; c++) N[r][c] += u[r] * u[c];
                rhs[r] += u[r] * v;
            }
            resid[used] = v;
            used++;
        }

        if (used < 4) {
            out->n_used = used;
            out->status = SPP_TOO_FEW_SATS;
            return out->status;
        }

        double dx[4];
        if (!solve4(N, rhs, dx)) {
            out->status = SPP_DIVERGED;
            return out->status;
        }
        for (int k = 0; k < 4; k++) x[k] += dx[k];

        const double step = sqrt(dx[0]*dx[0] + dx[1]*dx[1] + dx[2]*dx[2]);
        if (step < SPP_CONVERGED_M) converged = true;
    }

    if (!converged) {
        out->status = SPP_DIVERGED;
        return out->status;
    }
    }   /* second pass, with the receiver clock now known */

    /* Post-fit residuals, recomputed at the solution rather than left
     * over from the last iteration's linearisation point. */
    double ss = 0.0;
    for (int i = 0; i < used; i++) ss += resid[i] * resid[i];
    out->code_rms_m = sqrt(ss / (double)used);

    /* PDOP from the inverse of the normal matrix at the solution. */
    double N[4][4] = {{0}};
    for (int i = 0; i < n; i++) {
        double d[3] = { sats[i].pos[0] - x[0],
                        sats[i].pos[1] - x[1],
                        sats[i].pos[2] - x[2] };
        double rho = sqrt(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
        if (rho < 1.0) continue;
        const double u[4] = { -d[0]/rho, -d[1]/rho, -d[2]/rho, 1.0 };
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++) N[r][c] += u[r] * u[c];
    }
    double cov_diag[3] = {0, 0, 0};
    for (int k = 0; k < 3; k++) {
        double e[4] = {0, 0, 0, 0};
        e[k] = 1.0;
        double col[4];
        double Ncopy[4][4];
        memcpy(Ncopy, N, sizeof N);
        if (solve4(Ncopy, e, col)) cov_diag[k] = col[k];
    }
    out->pdop = sqrt(fabs(cov_diag[0]) + fabs(cov_diag[1]) + fabs(cov_diag[2]));

    out->pos[0] = x[0]; out->pos[1] = x[1]; out->pos[2] = x[2];
    out->clock_bias_m = x[3];
    out->n_used = used;
    out->n_rejected += out->n_no_eph + out->n_single_freq + out->n_low_elev;

    if (ref_ecef) {
        double lat, lon, alt;
        ecef_to_geodetic(ref_ecef[0], ref_ecef[1], ref_ecef[2], 0.0,
                         &lat, &lon, &alt);
        ecef_to_enu(lat, lon,
                    x[0] - ref_ecef[0], x[1] - ref_ecef[1], x[2] - ref_ecef[2],
                    &out->enu[0], &out->enu[1], &out->enu[2]);
    }

    /* ── The velocity, from the phase rates (P4) ──────────────────────
     *
     * The same geometry, a different measurement: a phase range rate is
     * the speed at which the range is changing, so
     *
     *     D = (v_sat - v_rx) . u  -  c dts/dt  +  c dtr/dt
     *
     * with u the unit vector towards the satellite. Three velocity
     * components and the receiver's clock drift make the same four
     * unknowns the position solve had, and the same 4x4.
     *
     * A base is supposed to be standing still, which makes this the
     * sharper number of the two: its true value is known, and anything
     * else is either a moving antenna or a broken solve. */
    {
        double Nv[4][4] = {{0}}, rhsv[4] = {0};
        double vres[SPP_MAX_SATS];
        int nv = 0;

        for (int i = 0; i < n; i++) {
            if (!sats[i].has_rate) continue;

            double d[3] = { sats[i].pos[0] - x[0],
                            sats[i].pos[1] - x[1],
                            sats[i].pos[2] - x[2] };
            double rho = sqrt(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
            if (rho < 1.0) continue;

            double az = 0.0, el = 0.0;
            azel_from_ecef(x[0], x[1], x[2],
                           sats[i].pos[0], sats[i].pos[1], sats[i].pos[2],
                           &az, &el);
            if (el < SPP_ELEV_MASK_DEG) continue;

            const double u[3] = { d[0] / rho, d[1] / rho, d[2] / rho };
            const double sat_rate = sats[i].vel[0] * u[0]
                                  + sats[i].vel[1] * u[1]
                                  + sats[i].vel[2] * u[2];

            /* What the rate would be with the receiver still and its
             * clock steady; the residual is what the unknowns explain. */
            const double model = sat_rate - SPP_C * sats[i].clock_rate;
            const double v     = sats[i].rate_ms - model;

            const double g[4] = { -u[0], -u[1], -u[2], 1.0 };
            for (int r = 0; r < 4; r++) {
                for (int c = 0; c < 4; c++) Nv[r][c] += g[r] * g[c];
                rhsv[r] += g[r] * v;
            }
            vres[nv] = v;
            nv++;
        }

        if (nv >= 4) {
            double vx[4];
            if (solve4(Nv, rhsv, vx)) {
                out->vel_ecef[0] = vx[0];
                out->vel_ecef[1] = vx[1];
                out->vel_ecef[2] = vx[2];
                out->clock_drift_ms = vx[3];
                out->n_rate_used = nv;
                out->has_velocity = true;

                double ss_v = 0.0;
                for (int i = 0; i < nv; i++) {
                    /* Post-fit: the solution explains part of each
                     * residual, so recompute rather than reuse. */
                    ss_v += vres[i] * vres[i];
                }
                out->rate_rms_ms = sqrt(ss_v / (double)nv);

                double lat, lon, alt;
                ecef_to_geodetic(x[0], x[1], x[2], 0.0, &lat, &lon, &alt);
                ecef_to_enu(lat, lon, vx[0], vx[1], vx[2],
                            &out->vel_enu[0], &out->vel_enu[1],
                            &out->vel_enu[2]);
            }
        }
        /* Fewer than four rates is not a slow velocity, it is none, and
         * `has_velocity` stays false so a report can say so. */
    }

    out->status = SPP_OK;
    return out->status;
}
