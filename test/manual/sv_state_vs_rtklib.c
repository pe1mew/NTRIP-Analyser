/**
 * @file sv_state_vs_rtklib.c
 * @brief The manual check P2 names: our satellite state against RTKLIB's.
 *
 * `test_sv_state.c` checks the propagator against physics -- vis-viva,
 * angular momentum, the size of the relativistic term -- which catches
 * a wrong frame or a dropped term but cannot catch an error both
 * implementations of Kepler would share.  This feeds the *same*
 * ephemeris to RTKLIB's `eph2pos` and to `sv_state_at` and prints the
 * difference.
 *
 * It is **not** part of the suite: RTKLIB is not a dependency of this
 * project and is not installed on the CI runner.  It is a bench tool,
 * run by hand when the propagator changes, in the spirit of
 * `cli-track.md` V5's CSRS-PPP submission.  See `docs/RUNBOOK.md` for
 * the build line.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "core/sv_state.h"
#include "core/sv_ephemeris.h"

#include "rtklib.h"        /* RTKLIB 2.4.3; -I its src directory */

#include <math.h>
#include <stdio.h>
#include <string.h>

/* One orbit, written twice: once as this project stores it, once as
 * RTKLIB does.  Every number appears in both, so a difference in the
 * output is a difference in the arithmetic, not in the input. */
#define E_WEEK   2300
#define E_TOE    345600.0
#define E_SQRTA  5153.65
#define E_ECC    0.011
#define E_I0     0.9617
#define E_OMG0   1.0
#define E_OMG    0.5
#define E_M0     0.3
#define E_DELN   4.9e-9
#define E_IDOT   1.0e-10
#define E_OMGD  -8.1e-9
#define E_CUC    1.2e-6
#define E_CUS    3.4e-6
#define E_CRC    230.0
#define E_CRS    -45.0
#define E_CIC    -1.1e-7
#define E_CIS    2.2e-7
#define E_AF0    1.234e-4
#define E_AF1    3.0e-12
#define E_AF2    0.0

static SvEphemeris ours(void)
{
    SvEphemeris e;
    memset(&e, 0, sizeof e);
    e.gnss_id = 1; e.prn = 7; e.week = E_WEEK;
    e.toe = E_TOE; e.toc = E_TOE;
    e.sqrt_a = E_SQRTA; e.e = E_ECC; e.i0 = E_I0;
    e.omega0 = E_OMG0; e.omega = E_OMG; e.m0 = E_M0;
    e.delta_n = E_DELN; e.idot = E_IDOT; e.omega_dot = E_OMGD;
    e.cuc = E_CUC; e.cus = E_CUS; e.crc = E_CRC; e.crs = E_CRS;
    e.cic = E_CIC; e.cis = E_CIS;
    e.af0 = E_AF0; e.af1 = E_AF1; e.af2 = E_AF2;
    e.health = 0; e.valid = true;
    return e;
}

static eph_t theirs(void)
{
    eph_t e;
    memset(&e, 0, sizeof e);
    e.sat  = 7;                      /* GPS PRN 7 */
    e.week = E_WEEK;
    e.toe  = gpst2time(E_WEEK, E_TOE);
    e.toc  = gpst2time(E_WEEK, E_TOE);
    /* RTKLIB keeps toe twice: as an absolute time, and as seconds of
     * week in `toes`, which is the one the longitude-of-node term uses.
     * Leaving it zero drops -omge * toe and rotates the whole orbit by
     * 25 radians -- 1 800 km of apparent disagreement, all of it this
     * file's fault, on the first run of this comparison. */
    e.toes = E_TOE;
    e.A    = E_SQRTA * E_SQRTA;
    e.e    = E_ECC;  e.i0   = E_I0;
    e.OMG0 = E_OMG0; e.omg  = E_OMG; e.M0 = E_M0;
    e.deln = E_DELN; e.idot = E_IDOT; e.OMGd = E_OMGD;
    e.cuc  = E_CUC;  e.cus  = E_CUS;
    e.crc  = E_CRC;  e.crs  = E_CRS;
    e.cic  = E_CIC;  e.cis  = E_CIS;
    e.f0   = E_AF0;  e.f1   = E_AF1; e.f2 = E_AF2;
    e.svh  = 0;
    return e;
}

int main(void)
{
    SvEphemeris mine = ours();
    eph_t       rt   = theirs();

    printf("offset  |dpos| m   dclock ns   (ours vs RTKLIB eph2pos)\n");
    printf("--------+-----------+-----------\n");

    double worst_pos = 0.0, worst_clk = 0.0;

    /* Across a whole fit interval, because an error in a rate term is
     * invisible at toe and grows with time. */
    for (double dt = -7200.0; dt <= 7200.0 + 1e-9; dt += 900.0) {
        const double tow = E_TOE + dt;

        SvState s;
        if (!sv_state_at(&mine, E_WEEK, tow, &s)) {
            printf("%+7.0f  REFUSED\n", dt);
            continue;
        }

        double rs[6] = {0}, dts[2] = {0}, var = 0.0;
        eph2pos(gpst2time(E_WEEK, tow), &rt, rs, dts, &var);

        const double dp = sqrt((s.pos[0] - rs[0]) * (s.pos[0] - rs[0])
                             + (s.pos[1] - rs[1]) * (s.pos[1] - rs[1])
                             + (s.pos[2] - rs[2]) * (s.pos[2] - rs[2]));
        const double dc = (s.clock_s - dts[0]) * 1e9;

        if (dp > worst_pos) worst_pos = dp;
        if (fabs(dc) > fabs(worst_clk)) worst_clk = dc;

        printf("%+7.0f  %9.4f   %9.4f\n", dt, dp, dc);
    }

    printf("--------+-----------+-----------\n");
    printf("worst   %9.4f m %7.4f ns\n", worst_pos, worst_clk);
    printf("\n%s\n",
           (worst_pos < 0.01 && fabs(worst_clk) < 0.1)
           ? "AGREES: within 1 cm and 0.1 ns across the fit interval"
           : "DIFFERS: investigate before trusting anything built on this");
    return (worst_pos < 0.01 && fabs(worst_clk) < 0.1) ? 0 : 1;
}
