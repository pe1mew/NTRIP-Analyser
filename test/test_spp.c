/**
 * @file test_spp.c
 * @brief The solver, checked by inverting a problem this file built.
 *
 * Physics bounds a propagator; they cannot bound a position. A solve
 * can be wrong by metres with every component looking sane, so the
 * check here is a **closure test**: put a receiver at a known point,
 * compute the pseudoranges that receiver would measure -- geometry,
 * satellite clocks, troposphere, a receiver clock bias -- hand them to
 * `spp_solve`, and demand the known point back.
 *
 * It catches what bounds cannot: a sign on the satellite clock, a
 * troposphere added where it should be subtracted, a frame left
 * un-rotated, a linearisation that converges to the wrong place.
 *
 * The troposphere is implemented again here rather than borrowed, so
 * the solver's copy is compared against a second one; the two agreeing
 * is the check, and a disagreement shows up as centimetres of recovery
 * error rather than as a passing test.
 *
 * What it is **not**: a check against real measurements. That is
 * `rnx2rtkp` on a capture, the manual step the work item's P3 names.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "core/spp.h"
#include "core/sv_state.h"
#include "core/sv_ephemeris.h"
#include "core/ns_obs.h"
#include "core/rtcm3x_parser.h"

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

#define C_M_S 299792458.0
#define WEEK  2300
#define TOW   345600.0

/* GPS signal-mask ids, 1-based as NsObsCell carries them: index 1 is
 * L1 C/A at 1575.42 MHz, index 8 is L2 at 1227.60. */
#define SIG_L1 2
#define SIG_L2 9

/** The troposphere again, independently of the solver's copy. */
static double tropo(double elev_rad, double h)
{
    if (elev_rad < 0.03) elev_rad = 0.03;
    const double temp = 15.0 - 6.5e-3 * h + 273.15;
    const double pres = 1013.25 * pow(1.0 - 2.2557e-5 * h, 5.2568);
    const double hum  = 0.7 * exp(-h / 2000.0);
    const double e_w  = 6.108 * hum * exp((17.15 * temp - 4684.0)
                                          / (temp - 38.45));
    const double z = M_PI / 2.0 - elev_rad;
    return (0.002277 / cos(z)) * (pres + (1255.0 / temp + 0.05) * e_w);
}

/** A GPS ephemeris with the orbit's real shape, placed by m0/omega0. */
static SvEphemeris eph_at(int prn, double m0, double omega0)
{
    SvEphemeris e;
    memset(&e, 0, sizeof e);
    e.gnss_id = 1;  e.prn = prn;  e.week = WEEK;
    e.toe = TOW;    e.toc = TOW;
    e.sqrt_a = 5153.65;  e.e = 0.004;  e.i0 = 0.9617;
    e.omega0 = omega0;   e.omega = 0.3;  e.m0 = m0;
    e.af0 = 1.0e-4 + prn * 1.0e-6;   /* each satellite its own clock */
    e.af1 = 2.0e-12;
    e.health = 0;  e.valid = true;
    return e;
}

/** @brief Add both carriers of one satellite, same range on each: the
 *         iono-free combination of two equal pseudoranges is that
 *         pseudorange, so the test needs no ionosphere to model. */
static void add_sat(NsObsEpoch *ep, int prn, double range_m)
{
    NsObsCell c;
    memset(&c, 0, sizeof c);
    c.gnss_id = 1;  c.prn = (uint8_t)prn;  c.flags = NS_OBS_HAS_PHASE;
    c.cnr_dbhz = 45.0f;
    c.pseudorange_m = range_m;
    c.sig_id = SIG_L1;  ns_obs_add(ep, &c);
    c.sig_id = SIG_L2;  ns_obs_add(ep, &c);
}

/**
 * @brief Build the epoch a receiver at @p rx would have measured.
 * @return Satellites written above the mask.
 */
static int build_epoch(NsObsEpoch *ep, const double rx[3], double bias_m,
                       int *prns, int max_prns)
{
    double lat, lon, alt;
    ecef_to_geodetic(rx[0], rx[1], rx[2], 0.0, &lat, &lon, &alt);

    ns_obs_reset(ep, 0);
    int n = 0;

    for (int k = 0; k < 10; k++) {
        const int prn = k + 1;
        const SvEphemeris *e = sv_eph_get(1, prn);
        if (!e) continue;

        /* Flight time by iteration, as the real thing does. */
        double flight = 0.075, rho = 0.0;
        SvState st;
        memset(&st, 0, sizeof st);
        for (int it = 0; it < 3; it++) {
            if (!sv_state_at(e, WEEK, TOW - flight, &st)) break;
            double p[3] = { st.pos[0], st.pos[1], st.pos[2] };
            sv_state_derotate(p, flight, 1);
            rho = sqrt((p[0]-rx[0])*(p[0]-rx[0]) + (p[1]-rx[1])*(p[1]-rx[1])
                     + (p[2]-rx[2])*(p[2]-rx[2]));
            flight = rho / C_M_S;
            st.pos[0] = p[0]; st.pos[1] = p[1]; st.pos[2] = p[2];
        }
        if (rho <= 0.0) continue;

        double az, el;
        azel_from_ecef(rx[0], rx[1], rx[2], st.pos[0], st.pos[1], st.pos[2],
                       &az, &el);
        if (el < 15.0) continue;      /* clear of the solver's 10 deg mask */

        /* Exactly the model spp.c inverts. */
        const double p_if = rho - C_M_S * st.clock_s
                          + tropo(el * M_PI / 180.0, alt) + bias_m;
        add_sat(ep, prn, p_if);
        if (n < max_prns) prns[n] = prn;
        n++;
    }
    return n;
}

/** @brief The closure test: known point in, known point out. */
static void case_recovers_the_truth(void)
{
    sv_eph_init();
    /* Ten satellites spread around the sky; those below the mask are
     * skipped by the builder, which is itself the geometry check. */
    for (int k = 0; k < 10; k++) {
        SvEphemeris e = eph_at(k + 1, 0.2 * k, 0.6 * k);
        sv_eph_store(&e);
    }

    double rx[3];
    geodetic_to_ecef(52.0, 6.0, 50.0, &rx[0], &rx[1], &rx[2]);
    const double bias = 1234.5;          /* receiver clock, metres */

    NsObsEpoch ep;
    int prns[16];
    const int n = build_epoch(&ep, rx, bias, prns, 16);
    printf("closure: %d satellites above the mask\n", n);
    CHECK(n >= 4, "need four satellites to solve, built %d", n);
    if (n < 4) return;

    SppSolution s;
    SppStatus st = spp_solve(&ep, WEEK, TOW, rx, &s);
    CHECK(st == SPP_OK, "solve failed: %s", spp_status_text(st));
    if (st != SPP_OK) return;

    const double derr = sqrt((s.pos[0]-rx[0])*(s.pos[0]-rx[0])
                           + (s.pos[1]-rx[1])*(s.pos[1]-rx[1])
                           + (s.pos[2]-rx[2])*(s.pos[2]-rx[2]));
    printf("  recovered to %.4f m, clock %.4f m (truth %.1f), rms %.4f, pdop %.2f, %d used\n",
           derr, s.clock_bias_m, bias, s.code_rms_m, s.pdop, s.n_used);

    CHECK(derr < 0.01, "recovered position is %.4f m from the truth", derr);
    CHECK(fabs(s.clock_bias_m - bias) < 0.01,
          "clock bias %.4f m, expected %.1f", s.clock_bias_m, bias);
    CHECK(s.code_rms_m < 0.01, "residual RMS %.4f m on noiseless data",
          s.code_rms_m);
    CHECK(s.n_used == n, "used %d satellites of %d", s.n_used, n);
    CHECK(s.pdop > 0.5 && s.pdop < 20.0, "implausible PDOP %.2f", s.pdop);
    CHECK(fabs(s.enu[0]) < 0.01 && fabs(s.enu[1]) < 0.01
          && fabs(s.enu[2]) < 0.01,
          "ENU against the truth should be zero, got %.4f %.4f %.4f",
          s.enu[0], s.enu[1], s.enu[2]);
}

/** @brief The same data, started from the Earth's centre. */
static void case_converges_from_nothing(void)
{
    double rx[3];
    geodetic_to_ecef(52.0, 6.0, 50.0, &rx[0], &rx[1], &rx[2]);

    NsObsEpoch ep;
    int prns[16];
    if (build_epoch(&ep, rx, 500.0, prns, 16) < 4) return;

    SppSolution s;
    SppStatus st = spp_solve(&ep, WEEK, TOW, NULL, &s);
    CHECK(st == SPP_OK, "solve from the centre failed: %s", spp_status_text(st));
    if (st != SPP_OK) return;

    const double derr = sqrt((s.pos[0]-rx[0])*(s.pos[0]-rx[0])
                           + (s.pos[1]-rx[1])*(s.pos[1]-rx[1])
                           + (s.pos[2]-rx[2])*(s.pos[2]-rx[2]));
    printf("from the centre: %.4f m\n", derr);
    CHECK(derr < 0.01, "converged %.4f m from the truth", derr);
}

/**
 * @brief A receiver clock a millisecond out, which is 300 km of range.
 *
 * The transmit time is derived from the pseudorange, so a badly steered
 * receiver clock moves every satellite along its orbit -- 1 ms is 3 m
 * of motion. A single-pass solver inherits that as metres of position
 * error; the second pass, which re-reads the satellites with the clock
 * the first pass found, is what makes this independent of the receiver.
 */
static void case_survives_a_bad_receiver_clock(void)
{
    double rx[3];
    geodetic_to_ecef(52.0, 6.0, 50.0, &rx[0], &rx[1], &rx[2]);
    const double bias = 299792.458;          /* exactly one millisecond */

    NsObsEpoch ep;
    int prns[16];
    if (build_epoch(&ep, rx, bias, prns, 16) < 4) return;

    SppSolution s;
    SppStatus st = spp_solve(&ep, WEEK, TOW, rx, &s);
    CHECK(st == SPP_OK, "solve failed with a 1 ms clock: %s",
          spp_status_text(st));
    if (st != SPP_OK) return;

    const double derr = sqrt((s.pos[0]-rx[0])*(s.pos[0]-rx[0])
                           + (s.pos[1]-rx[1])*(s.pos[1]-rx[1])
                           + (s.pos[2]-rx[2])*(s.pos[2]-rx[2]));
    printf("1 ms receiver clock: recovered to %.4f m, clock %.3f m\n",
           derr, s.clock_bias_m);
    CHECK(derr < 0.01,
          "a 1 ms receiver clock cost %.4f m of position", derr);
    CHECK(fabs(s.clock_bias_m - bias) < 0.01,
          "clock bias %.4f m, expected %.3f", s.clock_bias_m, bias);
}

/** @brief What must be refused, and named when refused. */
static void case_refusals(void)
{
    double rx[3];
    geodetic_to_ecef(52.0, 6.0, 50.0, &rx[0], &rx[1], &rx[2]);
    SppSolution s;

    /* Nothing at all. */
    NsObsEpoch empty;
    ns_obs_reset(&empty, 0);
    CHECK(spp_solve(&empty, WEEK, TOW, rx, &s) == SPP_NO_EPOCH,
          "an empty epoch must say so");

    /* One carrier per satellite: refused, never modelled. */
    NsObsEpoch single;
    ns_obs_reset(&single, 0);
    for (int prn = 1; prn <= 6; prn++) {
        NsObsCell c;
        memset(&c, 0, sizeof c);
        c.gnss_id = 1; c.prn = (uint8_t)prn; c.sig_id = SIG_L1;
        c.pseudorange_m = 22.0e6;
        ns_obs_add(&single, &c);
    }
    SppStatus st = spp_solve(&single, WEEK, TOW, rx, &s);
    CHECK(st == SPP_SINGLE_FREQ,
          "a single-frequency station must be refused, got '%s'",
          spp_status_text(st));
    CHECK(s.n_single_freq == 6, "expected 6 single-frequency drops, got %d",
          s.n_single_freq);

    /* Observations for satellites with no orbit. */
    NsObsEpoch orphan;
    ns_obs_reset(&orphan, 0);
    for (int prn = 20; prn <= 25; prn++) {
        NsObsCell c;
        memset(&c, 0, sizeof c);
        c.gnss_id = 1; c.prn = (uint8_t)prn;
        c.pseudorange_m = 22.0e6;
        c.sig_id = SIG_L1; ns_obs_add(&orphan, &c);
        c.sig_id = SIG_L2; ns_obs_add(&orphan, &c);
    }
    st = spp_solve(&orphan, WEEK, TOW, rx, &s);
    CHECK(st == SPP_NO_EPHEMERIS,
          "observations without orbits must say so, got '%s'",
          spp_status_text(st));

    /* GLONASS is excluded by design, however complete it looks. */
    NsObsEpoch glo;
    ns_obs_reset(&glo, 0);
    for (int prn = 1; prn <= 8; prn++) {
        NsObsCell c;
        memset(&c, 0, sizeof c);
        c.gnss_id = 2; c.prn = (uint8_t)prn;
        c.pseudorange_m = 22.0e6;
        c.sig_id = 2; ns_obs_add(&glo, &c);
        c.sig_id = 9; ns_obs_add(&glo, &c);
    }
    st = spp_solve(&glo, WEEK, TOW, rx, &s);
    CHECK(st != SPP_OK, "GLONASS alone must not produce a solution");
    CHECK(s.n_used == 0, "no GLONASS satellite may enter the solve, %d did",
          s.n_used);
}

int main(void)
{
    printf("== single-point solve ==\n");
    case_recovers_the_truth();
    case_converges_from_nothing();
    case_survives_a_bad_receiver_clock();
    case_refusals();

    if (failures) {
        printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nAll solver checks passed\n");
    return 0;
}
