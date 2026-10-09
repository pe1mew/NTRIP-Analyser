/**
 * @file sv_state.c
 * @brief Position, velocity and clock, declared in sv_state.h.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "core/sv_state.h"
#include "core/sv_orbit.h"   /* sv_to_ecef: the propagator this builds on */

#include <math.h>
#include <string.h>

/** Speed of light, the value every GNSS interface control document
 *  fixes by definition. */
#define SV_C 299792458.0

/** Earth rotation rate, rad/s.  GPS, Galileo, QZSS and BeiDou use the
 *  WGS-84/GTRF value; GLONASS's PZ-90 differs in the last digits, which
 *  is 2 mm over a 70 ms flight and still worth having right. */
#define SV_OMEGA_E     7.2921151467e-5
#define SV_OMEGA_E_GLO 7.292115e-5

/** Half the central-difference step for velocity, seconds.
 *
 * The plan allows a numerical derivative rather than an analytic one,
 * and half a second is the balance point: a Keplerian orbit is smooth
 * over seconds, so truncation error is micrometres per second, while a
 * step small enough to lose precision in the differencing is far below
 * this.  The test proves it against the analytic derivative. */
#define SV_VEL_H 0.5

/** @brief Seconds between two times of week, folded across the rollover. */
static double tow_diff(double a, double b)
{
    double d = a - b;
    if (d >  302400.0) d -= 604800.0;
    if (d < -302400.0) d += 604800.0;
    return d;
}

/** @brief Seconds between two GLONASS times of day, folded at midnight. */
static double tod_diff(double a, double b)
{
    double d = a - b;
    if (d >  43200.0) d -= 86400.0;
    if (d < -43200.0) d += 86400.0;
    return d;
}

bool sv_state_at(const SvEphemeris *eph, int week, double tow_s,
                 SvState *out)
{
    if (!out) return false;
    memset(out, 0, sizeof *out);
    if (!eph || !eph->valid) return false;

    double p0[3], pm[3], pp[3];
    if (!sv_to_ecef(eph, week, tow_s, &p0[0], &p0[1], &p0[2])) return false;

    /* Velocity by central difference of the propagator that is already
     * trusted for the sky plot, rather than a second implementation of
     * the same orbit that could disagree with it. */
    if (!sv_to_ecef(eph, week, tow_s - SV_VEL_H, &pm[0], &pm[1], &pm[2]) ||
        !sv_to_ecef(eph, week, tow_s + SV_VEL_H, &pp[0], &pp[1], &pp[2]))
        return false;

    for (int i = 0; i < 3; i++) {
        out->pos[i] = p0[i];
        out->vel[i] = (pp[i] - pm[i]) / (2.0 * SV_VEL_H);
    }

    if (eph->gnss_id == 2) {
        /* GLONASS broadcasts tau_n and gamma_n instead of a polynomial.
         * The sign convention is the system's own: the correction is
         * -tau_n + gamma_n (t - tb). */
        double dt = tod_diff(tow_s, eph->glo_tb_sod);
        out->clock_s    = -eph->glo_tau_n + eph->glo_gamma_n * dt;
        out->clock_rate =  eph->glo_gamma_n;
        out->has_clock  = true;
    } else {
        double dt = tow_diff(tow_s, eph->toc);
        out->clock_s    = eph->af0 + eph->af1 * dt + eph->af2 * dt * dt;
        out->clock_rate = eph->af1 + 2.0 * eph->af2 * dt;

        /* The relativistic correction, as -2 (r . v) / c^2 rather than
         * from the eccentric anomaly: identical in value, and it needs
         * nothing the propagator does not already hand back.  Tens of
         * nanoseconds, which is tens of metres of range. */
        double rv = out->pos[0] * out->vel[0]
                  + out->pos[1] * out->vel[1]
                  + out->pos[2] * out->vel[2];
        out->clock_s += -2.0 * rv / (SV_C * SV_C);
        out->has_clock = true;
    }

    out->valid = true;
    return true;
}

void sv_state_derotate(double pos[3], double flight_s, int gnss_id)
{
    if (!pos) return;
    const double w = (gnss_id == 2) ? SV_OMEGA_E_GLO : SV_OMEGA_E;
    const double a = w * flight_s;        /* radians the Earth turned */
    const double c = cos(a), s = sin(a);
    const double x = pos[0], y = pos[1];
    pos[0] =  c * x + s * y;
    pos[1] = -s * x + c * y;
    /* z is the rotation axis and does not move. */
}
