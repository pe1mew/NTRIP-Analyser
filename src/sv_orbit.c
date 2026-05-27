/**
 * @file sv_orbit.c
 * @brief Keplerian satellite-orbit propagator.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "sv_orbit.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Per-GNSS gravitational parameter (mu) in m^3/s^2.
 *   GPS:     ICD-GPS-200, 3.986005e14
 *   Galileo: OS-SIS-ICD,  3.986004418e14
 * Earth rotation rate omega_e is the same WGS-84 value for both.
 */
static double get_mu(int gnss_id)
{
    switch (gnss_id) {
    case 3:  return 3.986004418e14;
    default: return 3.986005e14;
    }
}

static double get_omega_e(int gnss_id)
{
    (void)gnss_id;
    return 7.2921151467e-5;
}

bool kepler_to_ecef(const SvEphemeris *eph,
                    int week, double tow_s,
                    double *x, double *y, double *z)
{
    if (!eph || !eph->valid) return false;
    if (eph->sqrt_a <= 0.0)  return false;

    const double mu      = get_mu(eph->gnss_id);
    const double omega_e = get_omega_e(eph->gnss_id);

    const double a  = eph->sqrt_a * eph->sqrt_a;          /* semi-major axis */
    const double n0 = sqrt(mu / (a * a * a));              /* nominal mean motion */
    const double n  = n0 + eph->delta_n;                   /* corrected mean motion */

    /* Time from ephemeris epoch.  We deliberately ignore @p week — see the
     * note in sv_eph_is_valid_at(): RTCM 1019 truncates GPS week to 10 bits
     * so comparing weeks across formats is fragile, while ephemeris validity
     * is always <4 h (well under half a week).  Wrapping the ToW delta to
     * [-302400, +302400] is sufficient. */
    (void)week;
    double tk = tow_s - eph->toe;
    if (tk >  302400.0) tk -= 604800.0;
    if (tk < -302400.0) tk += 604800.0;

    /* Mean anomaly */
    const double Mk = eph->m0 + n * tk;

    /* Solve Kepler's equation Ek - e*sin(Ek) = Mk by Newton iteration */
    double Ek = Mk;
    for (int i = 0; i < 30; i++) {
        double f  = Ek - eph->e * sin(Ek) - Mk;
        double fp = 1.0 - eph->e * cos(Ek);
        double d  = f / fp;
        Ek -= d;
        if (fabs(d) < 1e-13) break;
    }

    /* True anomaly */
    const double sinEk = sin(Ek), cosEk = cos(Ek);
    const double sqrt_1_e2 = sqrt(1.0 - eph->e * eph->e);
    const double nu = atan2(sqrt_1_e2 * sinEk, cosEk - eph->e);

    /* Argument of latitude */
    const double phi = nu + eph->omega;

    /* Second-harmonic corrections */
    const double s2phi = sin(2.0 * phi), c2phi = cos(2.0 * phi);
    const double du = eph->cus * s2phi + eph->cuc * c2phi;
    const double dr = eph->crs * s2phi + eph->crc * c2phi;
    const double di = eph->cis * s2phi + eph->cic * c2phi;

    const double uk = phi + du;
    const double rk = a * (1.0 - eph->e * cosEk) + dr;
    const double ik = eph->i0 + di + eph->idot * tk;

    /* In-orbital-plane coordinates */
    const double xp = rk * cos(uk);
    const double yp = rk * sin(uk);

    /* Corrected longitude of the ascending node (Earth-fixed) */
    const double Omk = eph->omega0
                     + (eph->omega_dot - omega_e) * tk
                     - omega_e * eph->toe;

    const double sin_Omk = sin(Omk), cos_Omk = cos(Omk);
    const double sin_ik  = sin(ik),  cos_ik  = cos(ik);

    if (x) *x = xp * cos_Omk - yp * cos_ik * sin_Omk;
    if (y) *y = xp * sin_Omk + yp * cos_ik * cos_Omk;
    if (z) *z = yp * sin_ik;

    return true;
}
