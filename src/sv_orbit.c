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
 *   GPS:     ICD-GPS-200,     3.986005e14
 *   Galileo: OS-SIS-ICD,      3.986004418e14
 *   QZSS:    IS-QZSS-PNT-004, 3.986005e14   (GPS-compatible)
 *   BeiDou:  BDS-SIS-ICD,     3.986004418e14
 */
static double get_mu(int gnss_id)
{
    switch (gnss_id) {
    case 3:  return 3.986004418e14;   /* Galileo */
    case 4:  return 3.986005e14;       /* QZSS */
    case 5:  return 3.986004418e14;    /* BeiDou */
    default: return 3.986005e14;       /* GPS */
    }
}

/* Earth rotation rate omega_e in rad/s.
 *   GPS / Galileo / QZSS: 7.2921151467e-5
 *   BeiDou (CGCS2000):   7.292115e-5  (truncated to 7 sig figs in BDS-SIS-ICD)
 */
static double get_omega_e(int gnss_id)
{
    switch (gnss_id) {
    case 5:  return 7.292115e-5;       /* BeiDou */
    default: return 7.2921151467e-5;   /* GPS, Galileo, QZSS */
    }
}

/* ── GLONASS numerical propagator ─────────────────────────────────────────
 *
 * GLONASS ephemeris is a position-velocity state vector at a reference
 * epoch (tb, Moscow seconds of day) plus a luni-solar acceleration that
 * stays constant for the eph validity window.  The orbit is integrated
 * via RK4 with a 60-second step on the 6-state ODE:
 *
 *     dr/dt = v
 *     dv/dt = -mu*r/|r|^3 * (1 + 1.5*J2*(Re/r)^2 * (1 - 5*(Z/r)^2))   (for x,y)
 *             -mu*r/|r|^3 * (1 + 1.5*J2*(Re/r)^2 * (3 - 5*(Z/r)^2))   (for z)
 *             + a_luni_solar
 *
 * Earth rotation is folded in afterwards by rotating the integrated PZ-90
 * inertial position by -omega_e * (t - tb) around Z.  PZ-90 -> WGS-84
 * datum offset is ~50 cm, ignored for sky-plot purposes.
 */
#define GLO_MU      3.9860044e14
#define GLO_J2      1.0826257e-3
#define GLO_RE      6378136.0
#define GLO_OMEGA_E 7.292115e-5

static void glo_deriv(const double y[6], const double accel_ls[3], double dy[6])
{
    double x = y[0], yy = y[1], z = y[2];
    double vx = y[3], vy = y[4], vz = y[5];

    double r2 = x*x + yy*yy + z*z;
    double r  = sqrt(r2);
    double r3 = r2 * r;
    double mu_r3 = GLO_MU / r3;

    double re_r2 = (GLO_RE / r) * (GLO_RE / r);
    double z_r2  = (z / r) * (z / r);
    double j2_factor = 1.5 * GLO_J2 * re_r2;
    double fac_xy = mu_r3 * (1.0 + j2_factor * (1.0 - 5.0 * z_r2));
    double fac_z  = mu_r3 * (1.0 + j2_factor * (3.0 - 5.0 * z_r2));

    dy[0] = vx;
    dy[1] = vy;
    dy[2] = vz;
    dy[3] = -fac_xy * x + accel_ls[0];
    dy[4] = -fac_xy * yy + accel_ls[1];
    dy[5] = -fac_z  * z  + accel_ls[2];
}

static void glo_rk4_step(double y[6], const double accel_ls[3], double h)
{
    double k1[6], k2[6], k3[6], k4[6], yt[6];
    int i;

    glo_deriv(y, accel_ls, k1);
    for (i = 0; i < 6; i++) yt[i] = y[i] + 0.5 * h * k1[i];
    glo_deriv(yt, accel_ls, k2);
    for (i = 0; i < 6; i++) yt[i] = y[i] + 0.5 * h * k2[i];
    glo_deriv(yt, accel_ls, k3);
    for (i = 0; i < 6; i++) yt[i] = y[i] +       h * k3[i];
    glo_deriv(yt, accel_ls, k4);

    for (i = 0; i < 6; i++)
        y[i] += (h / 6.0) * (k1[i] + 2.0 * k2[i] + 2.0 * k3[i] + k4[i]);
}

bool glonass_to_ecef(const SvEphemeris *eph, double glo_tod_s,
                     double *x, double *y, double *z)
{
    if (!eph || !eph->valid || eph->gnss_id != 2) return false;

    /* Time from tb, wrapped to half-day boundary so we don't try to
     * integrate across a midnight wrap.  GLONASS ephemerides are valid
     * for ~30 min, far less than 12 h. */
    double dt = glo_tod_s - eph->glo_tb_sod;
    if (dt >  43200.0) dt -= 86400.0;
    if (dt < -43200.0) dt += 86400.0;

    /* Initial state at tb.
     *
     * GLONASS broadcasts (pos, vel, acc) in PZ-90 -- the Earth-FIXED
     * rotating frame.  Our orbit ODE integrates in an INERTIAL frame
     * (gravity + J2 + luni-solar, no Coriolis / centrifugal).  At the
     * reference instant tb the two frames are spatially co-aligned, so
     * the position vector is identical; but the velocity differs by the
     * rotational term  v_inertial = v_pz90 + omega x r:
     *
     *     v_inertial_x = v_pz90_x - omega_e * y_pz90
     *     v_inertial_y = v_pz90_y + omega_e * x_pz90
     *     v_inertial_z = v_pz90_z
     *
     * Skipping this conversion (treating v_pz90 as inertial) used to give
     * an r(tb)-dependent error that, combined with the double-buffer
     * cache alternating between consecutive rebroadcasts (each with a
     * slightly different r(tb)), showed up as the per-tick zig-zag in
     * the GLONASS sky-plot trails.  With the cross product applied,
     * propagating eph_A and eph_B from the same orbit gives the same
     * result and the trails are smooth. */
    double s[6];
    s[0] = eph->glo_pos[0];
    s[1] = eph->glo_pos[1];
    s[2] = eph->glo_pos[2];
    s[3] = eph->glo_vel[0] - GLO_OMEGA_E * eph->glo_pos[1];
    s[4] = eph->glo_vel[1] + GLO_OMEGA_E * eph->glo_pos[0];
    s[5] = eph->glo_vel[2];

    /* RK4 integration with adaptive last step.  Step direction follows
     * sign(dt). */
    double step = (dt >= 0.0) ? 60.0 : -60.0;
    double t_remain = dt;
    while (fabs(t_remain) > 1e-9) {
        double h = (fabs(t_remain) < fabs(step)) ? t_remain : step;
        glo_rk4_step(s, eph->glo_acc, h);
        t_remain -= h;
    }

    /* Rotate the inertial result by -omega_e * dt around Z to land in the
     * Earth-fixed frame at time t.  (The state vector at tb is defined in
     * the rotating frame at the instant tb; for purposes of comparing to
     * the station ARP — also in the rotating frame at "now" — we need to
     * rotate by the Earth-rotation between tb and now.) */
    double theta = GLO_OMEGA_E * dt;
    double ct = cos(theta), st = sin(theta);
    double rx =  ct * s[0] + st * s[1];
    double ry = -st * s[0] + ct * s[1];
    double rz =  s[2];

    if (x) *x = rx;
    if (y) *y = ry;
    if (z) *z = rz;
    return true;
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

bool sv_to_ecef(const SvEphemeris *eph,
                int week, double tow_s,
                double *x, double *y, double *z)
{
    if (!eph || !eph->valid) return false;
    if (eph->gnss_id == 2) {
        /* tow_s is treated as Moscow seconds-of-day for GLONASS;
         * the caller converts GPS ToW -> GLO ToD before calling. */
        return glonass_to_ecef(eph, tow_s, x, y, z);
    }
    return kepler_to_ecef(eph, week, tow_s, x, y, z);
}
