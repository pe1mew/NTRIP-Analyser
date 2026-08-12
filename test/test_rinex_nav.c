/**
 * @file test_rinex_nav.c
 * @brief Regression test for the RINEX 3 navigation loader.
 *
 * The bug this exists to prevent: RINEX 3.05 gives GLONASS a fourth
 * orbit line that 3.04 did not.  A loader that consumed a fixed three
 * left the fourth to be read as the next record, mistook it for an
 * unknown system, and skipped four more lines to "resync" -- consuming
 * good records instead.  The reader never came back into phase.  One
 * GLONASS satellite survived out of 279 in a daily file, and every
 * record after the GLONASS block was affected too.
 *
 * The shape of that failure is what the fixture is built to catch:
 * `test/data/mini_nav.rnx` holds one record per constellation with the
 * GLONASS records *between* the others, so a reader that loses phase on
 * them cannot parse what follows.  Records are verbatim from BKG's
 * daily broadcast file, with one GLONASS record shortened to the older
 * 3.04 shape -- both must load, since users import files of both
 * vintages.
 *
 * SBAS is in the fixture as well.  The loader is expected to recognise
 * and skip it, which is only meaningful if the record after it still
 * parses.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "core/rinex_nav.h"
#include "core/sv_ephemeris.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond, ...)                                            \
    do {                                                            \
        if (!(cond)) {                                              \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);           \
            printf(__VA_ARGS__);                                    \
            printf("\n");                                           \
            failures++;                                             \
        }                                                           \
    } while (0)

/* gnss ids, as core numbers them */
enum { GPS = 1, GLONASS = 2, GALILEO = 3, QZSS = 4, BEIDOU = 5, NAVIC = 7 };

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "test/data/mini_nav.rnx";

    sv_eph_init();
    int counts[RINEX_NAV_MAX_GNSS];
    memset(counts, 0, sizeof(counts));
    int total = rinex_nav_load(path, counts);

    printf("rinex_nav_load(%s) = %d\n", path, total);
    if (total < 0) {
        printf("  FAIL cannot read the fixture -- run from the repo root, "
               "or pass its path as argv[1]\n");
        return 1;
    }

    /* One record per constellation, and three for GLONASS: two in the
     * 3.05 shape back to back, as a real file has them, and one in the
     * 3.04 shape.  Consecutive 3.05 records are what turns a single
     * misread into a cascade -- the leftover line from the first is
     * mistaken for a record and the skip eats the second. */
    CHECK(counts[GPS]     == 1, "GPS: expected 1 record, got %d", counts[GPS]);
    CHECK(counts[GLONASS] == 3, "GLONASS: expected 3 records (two 3.05, one "
                                "3.04 shape), got %d", counts[GLONASS]);
    CHECK(counts[GALILEO] == 1, "Galileo: expected 1 record, got %d", counts[GALILEO]);
    CHECK(counts[QZSS]    == 1, "QZSS: expected 1 record, got %d", counts[QZSS]);
    CHECK(counts[BEIDOU]  == 1, "BeiDou: expected 1 record, got %d", counts[BEIDOU]);
    CHECK(counts[NAVIC]   == 1, "NavIC: expected 1 record, got %d", counts[NAVIC]);
    /* Nine records in the file, eight parsed: the SBAS one is skipped. */
    CHECK(total == 8, "expected 8 records in total, got %d", total);

    /* Phase: the constellations placed *after* the GLONASS and SBAS
     * records in the file are the ones a desync destroys.  Their
     * presence above is the real assertion; naming them here says why. */
    CHECK(counts[NAVIC] > 0 && counts[QZSS] > 0 && counts[BEIDOU] > 0,
          "records after the GLONASS/SBAS block were lost -- the reader "
          "lost phase");

    /* The records must reach the cache, not merely be counted. */
    const SvEphemeris *g = sv_eph_get(GPS, 1);
    CHECK(g != NULL, "GPS 1 is not in the cache");

    const SvEphemeris *r1 = sv_eph_get(GLONASS, 1);
    const SvEphemeris *r2 = sv_eph_get(GLONASS, 2);
    const SvEphemeris *r3 = sv_eph_get(GLONASS, 3);
    CHECK(r1 != NULL, "GLONASS 1 (3.05 shape) is not in the cache");
    CHECK(r2 != NULL, "GLONASS 2 (3.05 shape, after another) is not in the cache");
    CHECK(r3 != NULL, "GLONASS 3 (3.04 shape) is not in the cache");

    /* A GLONASS state vector is metres from km in the file; a satellite
     * is ~19 100 km up, so the magnitude catches both a missed unit
     * conversion and a field read from the wrong column. */
    if (r1) {
        double x = r1->glo_pos[0], y = r1->glo_pos[1], z = r1->glo_pos[2];
        double radius = 0.0;
        radius = x * x + y * y + z * z;
        radius = (radius > 0.0) ? radius : 0.0;
        /* compare squared, to keep <math.h> out of the assertion */
        CHECK(radius > 2.0e14 && radius < 9.0e14,
              "GLONASS 1 orbit radius is implausible: |p|^2 = %.3e "
              "(expected ~3.6e14 for a 19 100 km orbit)", radius);
        CHECK(r1->glo_tb_sod >= 0.0 && r1->glo_tb_sod < 86400.0,
              "GLONASS 1 reference epoch out of range: %.1f s", r1->glo_tb_sod);
    }

    /* Keplerian records carry a semi-major axis root of ~5153 (GPS) to
     * ~6493 (BeiDou GEO) m^(1/2); zero means the line offsets slipped. */
    if (g) {
        CHECK(g->sqrt_a > 3000.0 && g->sqrt_a < 8000.0,
              "GPS 1 sqrt(A) is implausible: %.1f", g->sqrt_a);
        CHECK(g->e >= 0.0 && g->e < 0.1,
              "GPS 1 eccentricity is implausible: %.6f", g->e);
    }

    /* SBAS is recognised and skipped, not stored. */
    int sbas = 0;
    for (int prn = 1; prn <= SV_EPH_MAX_SATS_PER_GNSS; prn++)
        if (sv_eph_get(6, prn)) sbas++;
    CHECK(sbas == 0, "SBAS records should be skipped, but %d reached the cache",
          sbas);

    /* A path that does not exist is an error, not an empty success:
     * the app tells the user their file was unreadable on this. */
    CHECK(rinex_nav_load("test/data/no-such-file.rnx", NULL) < 0,
          "a missing file should return < 0");

    if (failures == 0) printf("test_rinex_nav: OK\n");
    else               printf("test_rinex_nav: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
