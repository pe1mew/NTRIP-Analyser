/**
 * @file test_observables.c
 * @brief The MSM observables reader: ranges known by construction.
 *
 * `msm_extract_obs` exists so that something can solve with the stream's
 * pseudoranges and phases (`design/work-items/station-self-position.md`
 * P1).  Every number it produces is scaled from a bit field, and a wrong
 * scale or a missed block still yields a plausible-looking float, so
 * this test builds frames whose values it computes independently and
 * asserts three things a mistake cannot satisfy:
 *
 *  - the **absolute range band**, 18 000-27 000 km.  Keeping the fine
 *    field without the satellite's rough range -- the shape of the bug
 *    in the message-detail decoder -- lands near zero and fails here
 *    however right the scale is;
 *  - the **exact metre value**, recomputed in this file from RTCM
 *    10403.3's scales rather than read from the code under test;
 *  - **what a message cannot carry**: MSM4 and MSM6 have no phase rate,
 *    and a cell must say so rather than report a velocity of zero.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "core/rtcm3x_parser.h"
#include "core/ns_obs.h"

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

/** @brief Write @p nbits of @p value at @p pos, MSB first. */
static void put_bits(unsigned char *buf, int pos, int nbits, unsigned long value)
{
    for (int i = 0; i < nbits; i++) {
        int bit = pos + i;
        unsigned long v = (value >> (nbits - 1 - i)) & 1UL;
        if (v) buf[bit >> 3] |= (unsigned char)(0x80u >> (bit & 7));
        else   buf[bit >> 3] &= (unsigned char)~(0x80u >> (bit & 7));
    }
}

/** The layout, restated so the test does not read it from the code it
 *  tests. */
typedef struct {
    int    sat_bits, pr_bits, ph_bits, lock_bits, cnr_bits;
    double pr_scale_ms, ph_scale_ms;   /* RTCM 10403.3 */
    int    has_rate;
} Layout;

static Layout layout_for(int msm)
{
    const double p24 = 1.0 / 16777216.0;     /* 2^-24 */
    const double p29 = 1.0 / 536870912.0;    /* 2^-29 */
    const double p31 = 1.0 / 2147483648.0;   /* 2^-31 */
    switch (msm) {
    case 4:  return (Layout){18, 15, 22, 4,  6,  p24, p29, 0};
    case 5:  return (Layout){36, 15, 22, 4,  6,  p24, p29, 1};
    case 6:  return (Layout){18, 20, 24, 10, 10, p29, p31, 0};
    default: return (Layout){36, 20, 24, 10, 10, p29, p31, 1};   /* 7 */
    }
}

#define MS_TO_M 299792.458

/** What one built cell holds, before the reader sees it. */
typedef struct {
    long rough_int;     /* whole milliseconds, 255 marks invalid   */
    long rough_mod;     /* 1/1024 ms                               */
    long fine_pr;       /* signed, in the layout's pr_bits         */
    long fine_ph;       /* signed, in the layout's ph_bits         */
    long rough_rate;    /* whole m/s, MSM5/7 only                  */
    long fine_rate;     /* 0.0001 m/s, MSM5/7 only                 */
} Built;

/** Two satellites (PRN 3, PRN 9), one signal each: one cell per
 *  satellite, so a misread of either block is visible. */
static int build_msm(int msm, int msg_type, const Built b[2],
                     unsigned char *out, int out_cap)
{
    const Layout L = layout_for(msm);
    const int num_sats = 2, num_sigs = 1, num_cells = 2;

    memset(out, 0, (size_t)out_cap);

    put_bits(out, 0, 12, (unsigned long)msg_type);
    put_bits(out, 12, 12, 1234);          /* reference station id */
    put_bits(out, 24, 30, 86400000UL);    /* epoch time           */

    put_bits(out, 73 + 2, 1, 1);          /* PRN 3  */
    put_bits(out, 73 + 8, 1, 1);          /* PRN 9  */
    put_bits(out, 137 + 0, 1, 1);         /* signal id 1 */

    const int cell_mask_start = 169;
    for (int i = 0; i < num_sats * num_sigs; i++)
        put_bits(out, cell_mask_start + i, 1, 1);

    /* Satellite block, field by field across satellites. */
    int p = cell_mask_start + num_sats * num_sigs;
    for (int s = 0; s < num_sats; s++) { put_bits(out, p, 8, (unsigned long)b[s].rough_int); p += 8; }
    if (L.sat_bits == 36)
        for (int s = 0; s < num_sats; s++) { put_bits(out, p, 4, 0); p += 4; }
    for (int s = 0; s < num_sats; s++) { put_bits(out, p, 10, (unsigned long)b[s].rough_mod); p += 10; }
    if (L.sat_bits == 36)
        for (int s = 0; s < num_sats; s++) { put_bits(out, p, 14, (unsigned long)b[s].rough_rate); p += 14; }

    /* Signal block, field by field across cells. */
    for (int c = 0; c < num_cells; c++) { put_bits(out, p, L.pr_bits, (unsigned long)b[c].fine_pr); p += L.pr_bits; }
    for (int c = 0; c < num_cells; c++) { put_bits(out, p, L.ph_bits, (unsigned long)b[c].fine_ph); p += L.ph_bits; }
    for (int c = 0; c < num_cells; c++) { put_bits(out, p, L.lock_bits, 7); p += L.lock_bits; }
    for (int c = 0; c < num_cells; c++) { put_bits(out, p, 1, 0); p += 1; }
    for (int c = 0; c < num_cells; c++) { put_bits(out, p, L.cnr_bits, (msm >= 6) ? 700 : 44); p += L.cnr_bits; }
    if (L.has_rate)
        for (int c = 0; c < num_cells; c++) { put_bits(out, p, 15, (unsigned long)b[c].fine_rate); p += 15; }

    return (p + 7) / 8;
}

/** @brief One MSM family, end to end, against values computed here. */
static void case_msm(int msm, int msg_type, const char *name)
{
    const Layout L = layout_for(msm);

    /* 75 ms and 80 ms of travel: 22 484 km and 23 983 km, both inside
     * the band a real GNSS range occupies. */
    Built b[2];
    memset(b, 0, sizeof b);
    b[0].rough_int = 75;  b[0].rough_mod = 512;  b[0].fine_pr = 123;  b[0].fine_ph = -456;
    b[1].rough_int = 80;  b[1].rough_mod = 0;    b[1].fine_pr = -321; b[1].fine_ph = 654;
    if (L.has_rate) {
        b[0].rough_rate = 3;    b[0].fine_rate = 1500;    /* 3.15 m/s  */
        b[1].rough_rate = -2;   b[1].fine_rate = -2500;   /* -2.25 m/s */
    }

    unsigned char payload[256];
    int len = build_msm(msm, msg_type, b, payload, (int)sizeof payload);

    NsObsCell cells[8];
    int gnss = 0, total = 0;
    int n = msm_extract_obs(payload, len, msg_type, cells, 8, &gnss, &total);

    printf("%s (type %d, %d bytes): %d cells, gnss %d\n", name, msg_type, len, n, gnss);

    CHECK(n == 2, "%s: expected 2 cells, got %d", name, n);
    CHECK(total == 2, "%s: expected 2 cells present, got %d", name, total);
    if (n != 2) return;

    for (int i = 0; i < 2; i++) {
        const double rough_ms = (double)b[i].rough_int + (double)b[i].rough_mod / 1024.0;
        const double want_pr  = (rough_ms + (double)b[i].fine_pr * L.pr_scale_ms) * MS_TO_M;
        const double want_ph  = (rough_ms + (double)b[i].fine_ph * L.ph_scale_ms) * MS_TO_M;

        CHECK(fabs(cells[i].pseudorange_m - want_pr) < 1e-6,
              "%s cell %d: pseudorange %.6f, expected %.6f",
              name, i, cells[i].pseudorange_m, want_pr);
        CHECK(fabs(cells[i].phase_m - want_ph) < 1e-6,
              "%s cell %d: phase %.6f, expected %.6f",
              name, i, cells[i].phase_m, want_ph);

        /* The band no fine-only reading can reach. */
        CHECK(cells[i].pseudorange_m > 18.0e6 && cells[i].pseudorange_m < 27.0e6,
              "%s cell %d: %.1f m is outside a GNSS range; rough part lost?",
              name, i, cells[i].pseudorange_m);

        CHECK((cells[i].flags & NS_OBS_HAS_PHASE) != 0,
              "%s cell %d: phase flag missing", name, i);

        if (L.has_rate) {
            const double want_rate = (double)b[i].rough_rate
                                     + (double)b[i].fine_rate * 0.0001;
            CHECK((cells[i].flags & NS_OBS_HAS_RATE) != 0,
                  "%s cell %d: rate flag missing on an MSM that carries one", name, i);
            CHECK(fabs(cells[i].rate_ms - want_rate) < 1e-9,
                  "%s cell %d: rate %.6f, expected %.6f",
                  name, i, cells[i].rate_ms, want_rate);
        } else {
            CHECK((cells[i].flags & NS_OBS_HAS_RATE) == 0,
                  "%s cell %d: claims a phase rate this message cannot carry", name, i);
            CHECK(cells[i].rate_ms == 0.0,
                  "%s cell %d: rate %.6f where the message has none",
                  name, i, cells[i].rate_ms);
        }
    }

    CHECK(cells[0].prn == 3 && cells[1].prn == 9,
          "%s: expected PRN 3 and 9, got %d and %d", name, cells[0].prn, cells[1].prn);
    printf("  PRN %d %.3f m, PRN %d %.3f m\n",
           cells[0].prn, cells[0].pseudorange_m,
           cells[1].prn, cells[1].pseudorange_m);
}

/** @brief Invalid fields, and a satellite without a range. */
static void case_invalid(void)
{
    Built b[2];
    memset(b, 0, sizeof b);
    b[0].rough_int = 255;                      /* no range for PRN 3     */
    b[1].rough_int = 78; b[1].rough_mod = 100;
    b[1].fine_pr   = 50;
    b[1].fine_ph   = -(1L << 23);              /* invalid phase, MSM7    */

    unsigned char payload[256];
    int len = build_msm(7, 1077, b, payload, (int)sizeof payload);

    NsObsCell cells[8];
    int total = 0;
    int n = msm_extract_obs(payload, len, 1077, cells, 8, NULL, &total);

    printf("invalid fields: %d cells kept of %d present\n", n, total);
    CHECK(total == 2, "expected 2 cells present, got %d", total);
    CHECK(n == 1, "expected the rangeless satellite dropped, got %d cells", n);
    if (n != 1) return;
    CHECK(cells[0].prn == 9, "expected PRN 9 to survive, got %d", cells[0].prn);
    CHECK((cells[0].flags & NS_OBS_HAS_PHASE) == 0,
          "an invalid phase must not be reported as a phase");
    CHECK(cells[0].phase_m == 0.0, "invalid phase left a value behind");
    CHECK(cells[0].pseudorange_m > 18.0e6 && cells[0].pseudorange_m < 27.0e6,
          "surviving cell has an implausible range %.1f", cells[0].pseudorange_m);
}

/** @brief A caller with less room than the message has cells. */
static void case_capacity(void)
{
    Built b[2];
    memset(b, 0, sizeof b);
    b[0].rough_int = 75; b[1].rough_int = 80;

    unsigned char payload[256];
    int len = build_msm(7, 1077, b, payload, (int)sizeof payload);

    NsObsCell one[1];
    int total = 0;
    int n = msm_extract_obs(payload, len, 1077, one, 1, NULL, &total);

    printf("capacity: wrote %d of %d\n", n, total);
    CHECK(n == 1, "expected 1 cell written, got %d", n);
    CHECK(total == 2, "expected the caller to learn 2 were present, got %d", total);
}

/** @brief MSM1-3 carry no usable set and must be refused, not guessed. */
static void case_refused(void)
{
    unsigned char payload[64];
    memset(payload, 0, sizeof payload);
    put_bits(payload, 0, 12, 1071);

    NsObsCell cells[4];
    int n = msm_extract_obs(payload, (int)sizeof payload, 1071, cells, 4, NULL, NULL);
    CHECK(n == 0, "MSM1 must yield nothing, got %d cells", n);

    n = msm_extract_obs(NULL, 10, 1077, cells, 4, NULL, NULL);
    CHECK(n == 0, "a null payload must yield nothing, got %d", n);
}

/** @brief The epoch store: appending, overflowing, counting satellites. */
static void case_epoch_store(void)
{
    NsObsEpoch e;
    ns_obs_reset(&e, 86400000UL);
    CHECK(e.n == 0 && e.dropped == 0 && e.open,
          "a reset epoch must be empty and open");

    NsObsCell c;
    memset(&c, 0, sizeof c);
    c.gnss_id = 1; c.prn = 5; c.sig_id = 1;
    CHECK(ns_obs_add(&e, &c), "the first cell must fit");
    c.sig_id = 2;                         /* same satellite, second signal */
    CHECK(ns_obs_add(&e, &c), "the second signal must fit");
    c.gnss_id = 3; c.prn = 11;
    CHECK(ns_obs_add(&e, &c), "a Galileo cell must fit");

    CHECK(e.n == 3, "expected 3 cells, got %d", e.n);
    CHECK(ns_obs_sat_count(&e) == 2,
          "two satellites across three cells, got %d", ns_obs_sat_count(&e));
    CHECK((e.gnss_seen & (1u << 1)) && (e.gnss_seen & (1u << 3)),
          "both systems must be recorded in gnss_seen");

    /* Fill it, then prove the overflow is counted rather than lost. */
    while (e.n < NS_OBS_MAX_CELLS) {
        c.prn = (uint8_t)(1 + e.n % 60);
        ns_obs_add(&e, &c);
    }
    CHECK(!ns_obs_add(&e, &c), "a full epoch must refuse");
    CHECK(e.dropped == 1, "the refusal must be counted, got %d", e.dropped);
    CHECK(e.n == NS_OBS_MAX_CELLS, "a refusal must not grow the set");

    ns_obs_reset(&e, 1000);
    CHECK(e.n == 0 && e.dropped == 0 && e.epoch_ms == 1000,
          "reset must clear the count, the drops and the epoch");
}

int main(void)
{
    printf("== MSM observables ==\n");

    case_msm(4, 1074, "MSM4 GPS");
    case_msm(5, 1085, "MSM5 GLONASS");
    case_msm(6, 1096, "MSM6 Galileo");
    case_msm(7, 1077, "MSM7 GPS");

    printf("\n== invalid fields ==\n");
    case_invalid();

    printf("\n== capacity ==\n");
    case_capacity();

    printf("\n== refused ==\n");
    case_refused();

    printf("\n== the epoch store ==\n");
    case_epoch_store();

    printf("\n%s\n", failures ? "FAILED" : "all observables checks passed");
    return failures ? 1 : 0;
}
