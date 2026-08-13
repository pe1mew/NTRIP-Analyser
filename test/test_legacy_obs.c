/**
 * @file test_legacy_obs.c
 * @brief Pins the layout of the legacy observation messages.
 *
 * 1001-1004 and 1009-1012 are fixed-layout: a header, then one record
 * per satellite. Get a width wrong and the reader still returns
 * satellites and still returns C/N0 -- just the wrong ones, from the
 * middle of a pseudorange. Nothing downstream can tell, which is why
 * these frames are built with values no other reading could produce.
 *
 * The same layout was confirmed against a live station that sends both
 * generations at once (rtk2go.com/Mirmenhof: 10 of 10 GPS and 8 of 8
 * GLONASS satellites matching its MSM6, every C/N0 within the 0.25 dB-Hz
 * quantisation). This file is the half of that evidence which does not
 * depend on someone else's station still being up.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
#include "core/rtcm3x_parser.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

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

/** The layout under test, restated rather than read from the code. */
typedef struct {
    int hdr, rec, nsat_off, cnr1, cnr2;
} Layout;

static Layout layout_for(int msg_type)
{
    switch (msg_type) {
    case 1002: return (Layout){64,  74, 55, 66,  -1};
    case 1003: return (Layout){64, 101, 55, -1,  -1};
    case 1004: return (Layout){64, 125, 55, 66, 117};
    case 1010: return (Layout){61,  79, 52, 71,  -1};
    case 1012: return (Layout){61, 130, 52, 71, 122};
    default:   return (Layout){0, 0, 0, -1, -1};
    }
}

/**
 * @brief Build one legacy observation frame.
 *
 * Two satellites, each with an L1 and (where the message has one) an L2
 * C/N0, and every other field filled with a pattern -- so a reader that
 * lands on the wrong offset picks up something obviously wrong rather
 * than a convenient zero.
 */
static int build(int msg_type, const int prn[2],
                 const unsigned long l1[2], const unsigned long l2[2],
                 unsigned char *out)
{
    const Layout L = layout_for(msg_type);
    memset(out, 0, 128);

    /* Everything ahead of the satellite records, pattern-filled. */
    for (int b = 0; b + 8 <= L.hdr; b += 8) put_bits(out, b, 8, 0xA5u);
    put_bits(out, 0, 12, (unsigned long)msg_type);
    put_bits(out, L.nsat_off, 5, 2);              /* two satellites */

    for (int s = 0; s < 2; s++) {
        int base = L.hdr + s * L.rec;
        for (int b = 0; b + 8 <= L.rec; b += 8) put_bits(out, base + b, 8, 0x5Au);
        put_bits(out, base, 6, (unsigned long)prn[s]);
        if (L.cnr1 >= 0) put_bits(out, base + L.cnr1, 8, l1[s]);
        if (L.cnr2 >= 0) put_bits(out, base + L.cnr2, 8, l2[s]);
    }
    return (L.hdr + 2 * L.rec + 7) / 8;
}

static void case_legacy(int msg_type, const char *name, int expect_gnss,
                        float expect_a, float expect_b)
{
    const int prn[2] = { 3, 21 };
    const unsigned long l1[2] = { 178, 150 };   /* 44.50 and 37.50 dB-Hz */
    const unsigned long l2[2] = { 120, 199 };   /* 30.00 and 49.75 dB-Hz */

    unsigned char payload[128];
    int len = build(msg_type, prn, l1, l2, payload);

    int   prns[8];
    float cnr[8];
    int   gnss = 0;
    int n = rtcm_legacy_extract(payload, len, msg_type, prns, cnr, 8, &gnss);

    printf("%s (type %d, %d bytes): %d satellites, gnss %d\n",
           name, msg_type, len, n, gnss);

    CHECK(n == 2, "%s: expected 2 satellites, got %d", name, n);
    if (n != 2) return;
    CHECK(gnss == expect_gnss, "%s: expected gnss %d, got %d",
          name, expect_gnss, gnss);
    CHECK(prns[0] == 3 && prns[1] == 21,
          "%s: expected satellites 3 and 21, got %d and %d",
          name, prns[0], prns[1]);
    CHECK(fabsf(cnr[0] - expect_a) < 0.001f,
          "%s: satellite 3 expected %.2f dB-Hz, got %.2f", name, expect_a, cnr[0]);
    CHECK(fabsf(cnr[1] - expect_b) < 0.001f,
          "%s: satellite 21 expected %.2f dB-Hz, got %.2f", name, expect_b, cnr[1]);

    printf("  %.2f and %.2f dB-Hz\n", cnr[0], cnr[1]);
}

int main(void)
{
    printf("== legacy observation layout ==\n");

    /* 1002 and 1010 carry L1 only, so the L1 value stands. */
    case_legacy(1002, "1002 GPS L1",      1, 44.50f, 37.50f);
    case_legacy(1010, "1010 GLONASS L1",  2, 44.50f, 37.50f);

    /* 1004 and 1012 carry both bands, and the stronger one represents
     * the satellite: 44.50 beats 30.00, and 49.75 beats 37.50. */
    case_legacy(1004, "1004 GPS L1+L2",     1, 44.50f, 49.75f);
    case_legacy(1012, "1012 GLONASS L1+L2", 2, 44.50f, 49.75f);

    /* 1003 has no C/N0 field at all.  Its satellites still count: a
     * satellite observed is worth reporting even when its signal
     * strength is not on offer. */
    case_legacy(1003, "1003 GPS, no C/N0",  1, 0.0f, 0.0f);

    /* An MSM is not this reader's business. */
    {
        unsigned char payload[128];
        const int prn[2] = { 3, 21 };
        const unsigned long z[2] = { 0, 0 };
        int len = build(1004, prn, z, z, payload);
        int prns[8]; float cnr[8];
        int n = rtcm_legacy_extract(payload, len, 1077, prns, cnr, 8, NULL);
        CHECK(n == 0, "MSM7: expected refusal, got %d satellites", n);
        printf("1077 (an MSM): refused, as it is not a legacy message\n");
    }

    if (failures) {
        printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nAll legacy observation layout checks passed\n");
    return 0;
}
