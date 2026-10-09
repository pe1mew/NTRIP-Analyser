/**
 * @file test_sv_state.c
 * @brief Satellite position, velocity and clock, checked against physics
 *        rather than against the arithmetic that produced them.
 *
 * `sv_state_at` is the step every later number inherits
 * (`design/work-items/station-self-position.md` P2): a metre of error
 * here is a metre in each position solved from it, and nothing
 * downstream would say so.  Checking a propagator against itself proves
 * nothing, so these checks come from outside it:
 *
 *  - **vis-viva**, |v|^2 = mu (2/r - 1/a).  The velocity is a numerical
 *    derivative of the position; its magnitude must match a law that
 *    involves neither, only the orbit's own semi-major axis;
 *  - **angular momentum**, |r x v| = sqrt(mu a (1 - e^2)), which
 *    constrains the velocity's *direction* as well as its size;
 *  - **the orbit's shape**: radius and speed inside the band the
 *    constellation occupies, so a unit slip is visible at a glance;
 *  - **the relativistic term's size**, tens of nanoseconds, which is the
 *    one part of the clock that cannot come from the broadcast
 *    polynomial;
 *  - **the Earth-rotation correction**, against w r t computed here.
 *
 * A comparison against RTKLIB on the same navigation data is the
 * stronger check and is a manual step: see the work item's P2.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "core/sv_state.h"
#include "core/sv_ephemeris.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("  FAIL: ");                                            \
            printf(__VA_ARGS__);                                           \
            printf("\n");                                                  \
            failures++;                                                    \
        }                                                                  \
    } while (0)

#define MU_GPS 3.986005e14          /* WGS-84, m^3/s^2 */
#define C_M_S  299792458.0

static double norm3(const double v[3])
{
    return sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
}

static void cross3(const double a[3], const double b[3], double out[3])
{
    out[0] = a[1]*b[2] - a[2]*b[1];
    out[1] = a[2]*b[0] - a[0]*b[2];
    out[2] = a[0]*b[1] - a[1]*b[0];
}

/** A plausible GPS ephemeris: a real orbit's shape, round numbers. */
static SvEphemeris gps_eph(void)
{
    SvEphemeris e;
    memset(&e, 0, sizeof e);
    e.gnss_id = 1;
    e.prn     = 7;
    e.week    = 2300;
    e.toe     = 345600.0;            /* Thursday noon, seconds of week */
    e.toc     = 345600.0;
    e.sqrt_a  = 5153.65;             /* a = 26 560 km, the GPS value    */
    e.e       = 0.011;
    e.i0      = 0.9617;              /* 55 degrees                      */
    e.omega0  = 1.0;
    e.omega   = 0.5;
    e.m0      = 0.3;
    e.delta_n = 4.9e-9;
    e.idot    = 1.0e-10;
    e.omega_dot = -8.1e-9;
    e.af0     = 1.234e-4;            /* 123 us, a typical satellite     */
    e.af1     = 3.0e-12;
    e.af2     = 0.0;
    e.health  = 0;
    e.valid   = true;
    return e;
}

/** @brief The orbit's own laws, which the propagator must obey. */
static void case_gps_orbit(void)
{
    SvEphemeris e = gps_eph();
    const double a = e.sqrt_a * e.sqrt_a;

    /* Twenty minutes after toe: far enough that a frozen propagator or a
     * dropped rate term shows up. */
    SvState s;
    CHECK(sv_state_at(&e, e.week, e.toe + 1200.0, &s), "GPS state refused");
    if (!s.valid) return;

    const double r = norm3(s.pos);
    const double v = norm3(s.vel);

    printf("GPS: r %.1f km, v %.1f m/s (Earth-fixed), clock %.3f us\n",
           r / 1000.0, v, s.clock_s * 1e6);

    CHECK(r > 25.0e6 && r < 27.5e6,
          "radius %.1f km is outside the GPS band", r / 1000.0);
    CHECK(v > 2500.0 && v < 3500.0,
          "Earth-fixed speed %.1f m/s is outside the GPS band", v);

    /* The reported velocity is Earth-fixed; vis-viva and angular
     * momentum are inertial laws.  Adding w x r converts one to the
     * other, and is itself the check that the frame is what the header
     * says: get the frame wrong and both laws fail by 25%. */
    const double w = 7.2921151467e-5;
    const double vi[3] = { s.vel[0] - w * s.pos[1],
                           s.vel[1] + w * s.pos[0],
                           s.vel[2] };
    const double v_inertial = norm3(vi);

    const double v_visviva = sqrt(MU_GPS * (2.0 / r - 1.0 / a));
    CHECK(fabs(v_inertial - v_visviva) < 1.0,
          "inertial speed %.4f m/s against vis-viva %.4f m/s",
          v_inertial, v_visviva);

    /* Angular momentum constrains direction too: a velocity of the right
     * size pointing wrongly fails here. */
    double h[3];
    cross3(s.pos, vi, h);
    const double h_mag  = norm3(h);
    const double h_want = sqrt(MU_GPS * a * (1.0 - e.e * e.e));
    CHECK(fabs(h_mag - h_want) / h_want < 1e-3,
          "angular momentum %.4e against %.4e (%.3f%% off)",
          h_mag, h_want, 100.0 * fabs(h_mag - h_want) / h_want);
}

/** @brief The clock: polynomial, plus the piece physics adds. */
static void case_gps_clock(void)
{
    SvEphemeris e = gps_eph();

    SvState at_toc, later;
    CHECK(sv_state_at(&e, e.week, e.toc, &at_toc), "state at toc refused");
    CHECK(sv_state_at(&e, e.week, e.toc + 3600.0, &later), "state later refused");
    if (!at_toc.valid || !later.valid) return;

    CHECK(at_toc.has_clock, "a GPS ephemeris must yield a clock");

    /* At toc the polynomial is af0 exactly, so whatever remains is the
     * relativistic term -- which must be there, and must be small. */
    const double dtr = at_toc.clock_s - e.af0;
    printf("GPS clock: af0 %.3f us, relativistic %.2f ns\n",
           e.af0 * 1e6, dtr * 1e9);
    CHECK(fabs(dtr) > 1.0e-10,
          "no relativistic correction at all (%.3e s)", dtr);
    CHECK(fabs(dtr) < 5.0e-8,
          "relativistic correction %.3e s is too large to be real", dtr);
    CHECK(fabs(dtr) * C_M_S > 1.0,
          "the correction is worth less than a metre; check the formula");

    /* An hour on, the clock has moved by af1 * 3600 **plus** whatever the
     * relativistic term did, which for e = 0.011 swings by tens of
     * nanoseconds and dwarfs af1 over an hour.  Expecting af1 * 3600
     * alone was wrong by 11 ns on the first run: the eccentric orbit, not
     * the polynomial, is the larger term.
     *
     * The relativistic part is recomputed here from the states the call
     * returned -- the radial component of velocity, which is identical in
     * the Earth-fixed and inertial frames because w x r is perpendicular
     * to r. */
    const double dtr_at  = -2.0 * (at_toc.pos[0]*at_toc.vel[0]
                                 + at_toc.pos[1]*at_toc.vel[1]
                                 + at_toc.pos[2]*at_toc.vel[2]) / (C_M_S*C_M_S);
    const double dtr_lat = -2.0 * (later.pos[0]*later.vel[0]
                                 + later.pos[1]*later.vel[1]
                                 + later.pos[2]*later.vel[2]) / (C_M_S*C_M_S);
    const double drift = later.clock_s - at_toc.clock_s;
    const double want  = e.af1 * 3600.0 + (dtr_lat - dtr_at);
    CHECK(fabs(drift - want) < 1.0e-14,
          "clock moved %.6e s in an hour, expected %.6e", drift, want);
    CHECK(fabs(dtr_lat - dtr_at) > fabs(e.af1 * 3600.0),
          "the relativistic swing should dominate af1 over an hour; "
          "swing %.3e, af1 term %.3e", dtr_lat - dtr_at, e.af1 * 3600.0);
    CHECK(fabs(later.clock_rate - e.af1) < 1e-15,
          "clock rate %.4e, expected af1 %.4e", later.clock_rate, e.af1);
}

/** @brief GLONASS: a state vector, and the clock terms 1020 carries. */
static void case_glonass(void)
{
    SvEphemeris e;
    memset(&e, 0, sizeof e);
    e.gnss_id      = 2;
    e.prn          = 11;
    e.glo_tb_sod   = 43200.0;                 /* noon, Moscow */
    e.toe          = e.glo_tb_sod;
    e.toc          = e.glo_tb_sod;
    e.glo_pos[0]   = 10.0e6;                  /* a real-sized orbit */
    e.glo_pos[1]   = 15.0e6;
    e.glo_pos[2]   = 17.0e6;
    e.glo_vel[0]   = -2000.0;
    e.glo_vel[1]   =  2500.0;
    e.glo_vel[2]   = -1000.0;
    e.glo_tau_n    = 1.5e-4;                  /* 150 us */
    e.glo_gamma_n  = 2.0e-13;
    e.glo_freq_chan = 3;
    e.health       = 0;
    e.valid        = true;

    SvState s;
    CHECK(sv_state_at(&e, 0, e.glo_tb_sod, &s), "GLONASS state refused");
    if (!s.valid) return;

    const double r = norm3(s.pos);
    const double v = norm3(s.vel);
    printf("GLONASS: r %.1f km, v %.1f m/s, clock %.3f us\n",
           r / 1000.0, v, s.clock_s * 1e6);

    /* At tb the integrator has nothing to integrate, so the state is the
     * broadcast one: a check that the time handling has not shifted. */
    CHECK(fabs(r - norm3(e.glo_pos)) < 1.0,
          "at tb the position moved by %.3f m", fabs(r - norm3(e.glo_pos)));
    CHECK(fabs(v - norm3(e.glo_vel)) < 0.5,
          "at tb the speed differs by %.3f m/s", fabs(v - norm3(e.glo_vel)));

    /* The clock terms, which 1020 carried and nothing kept until P2. */
    CHECK(s.has_clock, "GLONASS must now yield a clock");
    CHECK(fabs(s.clock_s - (-e.glo_tau_n)) < 1e-12,
          "clock at tb is %.6e, expected -tau_n %.6e",
          s.clock_s, -e.glo_tau_n);

    SvState later;
    CHECK(sv_state_at(&e, 0, e.glo_tb_sod + 600.0, &later), "later refused");
    if (later.valid) {
        const double want = -e.glo_tau_n + e.glo_gamma_n * 600.0;
        CHECK(fabs(later.clock_s - want) < 1e-15,
              "clock after 600 s is %.6e, expected %.6e", later.clock_s, want);
    }
}

/** @brief The Earth turns while the signal flies. */
static void case_derotation(void)
{
    /* A satellite on the equator, 20 000 km up the x axis. */
    double pos[3] = { 26.0e6, 0.0, 0.0 };
    const double z_before = pos[2];

    double unmoved[3] = { pos[0], pos[1], pos[2] };
    sv_state_derotate(unmoved, 0.0, 1);
    CHECK(fabs(unmoved[0] - pos[0]) < 1e-9 && fabs(unmoved[1]) < 1e-9,
          "a zero flight time must not move the satellite");

    const double flight = 0.07;              /* 70 ms, a typical range */
    sv_state_derotate(pos, flight, 1);

    const double moved = sqrt((pos[0] - 26.0e6) * (pos[0] - 26.0e6)
                              + pos[1] * pos[1]);
    const double want  = 7.2921151467e-5 * 26.0e6 * flight;
    printf("derotation: %.2f m over %.0f ms\n", moved, flight * 1000.0);

    CHECK(fabs(moved - want) < 0.05,
          "rotated %.3f m, expected about %.3f m", moved, want);
    CHECK(pos[2] == z_before, "the rotation axis moved");
    CHECK(pos[1] < 0.0,
          "the correction turned the wrong way: the frame rotates with "
          "the Earth, so the satellite falls behind in y");
}

/** @brief What must be refused rather than guessed. */
static void case_refusals(void)
{
    SvState s;
    CHECK(!sv_state_at(NULL, 2300, 345600.0, &s), "a null ephemeris must refuse");

    SvEphemeris e = gps_eph();
    e.valid = false;
    CHECK(!sv_state_at(&e, e.week, e.toe, &s), "an invalid ephemeris must refuse");
    CHECK(!s.valid, "a refusal must leave the state invalid");

    e = gps_eph();
    CHECK(!sv_state_at(&e, e.week, e.toe, NULL), "a null output must refuse");
}

int main(void)
{
    printf("== satellite state ==\n");
    case_gps_orbit();
    case_gps_clock();
    case_glonass();
    case_derotation();
    case_refusals();

    if (failures) {
        printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nAll satellite-state checks passed\n");
    return 0;
}
