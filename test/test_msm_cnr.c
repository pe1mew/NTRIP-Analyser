/**
 * @file test_msm_cnr.c
 * @brief Pins the MSM cell layout that C/N0 is read from.
 *
 * The four MSMs that carry C/N0 place it at different offsets, and an
 * offset that is wrong by a few bits does not fail loudly: it returns a
 * plausible number from the middle of a phase range, and the tool then
 * reports a healthy antenna as weak, or the reverse. Nothing downstream
 * can tell the difference, which is exactly why this file exists.
 *
 * Each case builds a frame whose C/N0 values are known by construction,
 * then asks the reader for them back. MSM4, MSM5 and MSM7 also have live
 * stations behind them (Centipede NEAR4 and NEAR, rtk2go Mirmenhof for
 * MSM6); this is the part of the evidence that does not depend on a
 * third party's station still being up.
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

/** @brief Write @p nbits of @p value at @p pos, MSB first -- get_bits' mirror. */
static void put_bits(unsigned char *buf, int pos, int nbits, unsigned long value)
{
    for (int i = 0; i < nbits; i++) {
        int bit = pos + i;
        unsigned long v = (value >> (nbits - 1 - i)) & 1UL;
        if (v) buf[bit >> 3] |= (unsigned char)(0x80u >> (bit & 7));
        else   buf[bit >> 3] &= (unsigned char)~(0x80u >> (bit & 7));
    }
}

/** The layout under test, restated here so the test does not read it
 *  from the code it is testing. */
typedef struct {
    int   sat_bits, pr_bits, ph_bits, lock_bits, cnr_bits;
    float cnr_scale;
} Layout;

static Layout layout_for(int msm)
{
    switch (msm) {
    case 4:  return (Layout){18, 15, 22, 4,  6,  1.0f};
    case 5:  return (Layout){36, 15, 22, 4,  6,  1.0f};
    case 6:  return (Layout){18, 20, 24, 10, 10, 0.0625f};
    default: return (Layout){36, 20, 24, 10, 10, 0.0625f};   /* 7 */
    }
}

/**
 * @brief Build one MSM frame payload with the given per-cell C/N0 values.
 *
 * Two satellites (PRN 3 and PRN 9) and two signals, all four cells
 * present, so every field array has a known length and a misread offset
 * lands somewhere visible.
 *
 * @param msm      4, 5, 6 or 7.
 * @param msg_type The RTCM type to stamp in (1074, 1076, ...).
 * @param raw      Four raw C/N0 counts, cell order: (sat0,sig0),
 *                 (sat0,sig1), (sat1,sig0), (sat1,sig1).
 * @param out      Payload buffer, at least 128 bytes.
 * @return Payload length in bytes.
 */
static int build_msm(int msm, int msg_type, const unsigned long raw[4],
                     unsigned char *out)
{
    const Layout L = layout_for(msm);
    const int num_sats = 2, num_sigs = 2, num_cells = 4;

    memset(out, 0, 128);

    put_bits(out, 0, 12, (unsigned long)msg_type);
    put_bits(out, 12, 12, 1234);          /* reference station id       */
    put_bits(out, 24, 30, 86400000UL);    /* epoch time                 */
    /* bits 54..72 are flags the reader does not touch, left at zero.   */

    /* Satellite mask: PRN 3 and PRN 9, MSB-first from PRN 1 at bit 73. */
    put_bits(out, 73 + 2, 1, 1);
    put_bits(out, 73 + 8, 1, 1);

    /* Signal mask: signal ids 1 and 3, MSB-first from bit 137.         */
    put_bits(out, 137 + 0, 1, 1);
    put_bits(out, 137 + 2, 1, 1);

    /* Cell mask: every satellite carries every signal.                 */
    const int cell_mask_start = 169;
    for (int i = 0; i < num_sats * num_sigs; i++)
        put_bits(out, cell_mask_start + i, 1, 1);

    const int cell_block_start =
        cell_mask_start + num_sats * num_sigs + L.sat_bits * num_sats;

    /* Satellite block: rough ranges, deliberately non-zero so that a
     * reader which mistakes this block for signal data cannot pass by
     * reading zeros. */
    for (int s = 0; s < num_sats; s++)
        put_bits(out, cell_mask_start + num_sats * num_sigs + s * L.sat_bits,
                 8, 0x5Au);

    /* Signal arrays, field by field across all cells, in order:
     * pseudorange, phase range, lock, half-cycle, C/N0, [phase rate]. */
    int p = cell_block_start;
    for (int c = 0; c < num_cells; c++) { put_bits(out, p, L.pr_bits,   0x2AAAu); p += L.pr_bits; }
    for (int c = 0; c < num_cells; c++) { put_bits(out, p, L.ph_bits,   0x155555u); p += L.ph_bits; }
    for (int c = 0; c < num_cells; c++) { put_bits(out, p, L.lock_bits, 0x2Au); p += L.lock_bits; }
    for (int c = 0; c < num_cells; c++) { put_bits(out, p, 1,           1u); p += 1; }
    const int cnr_start = p;
    for (int c = 0; c < num_cells; c++) { put_bits(out, p, L.cnr_bits,  raw[c]); p += L.cnr_bits; }
    if (msm == 5 || msm == 7)
        for (int c = 0; c < num_cells; c++) { put_bits(out, p, 15, 0x1234u); p += 15; }

    (void)cnr_start;
    return (p + 7) / 8;
}

/** @brief One MSM family, end to end through the public reader. */
static void case_msm(int msm, int msg_type, const char *name,
                     const unsigned long raw[4],
                     float expect_prn3, float expect_prn9)
{
    unsigned char payload[128];
    int len = build_msm(msm, msg_type, raw, payload);

    int   prns[8];
    float cnr[8];
    int   gnss = 0;
    int n = msm_extract_cnr(payload, len, msg_type, prns, cnr, 8, &gnss);

    printf("%s (type %d, %d bytes): %d satellites, gnss %d\n",
           name, msg_type, len, n, gnss);

    CHECK(n == 2, "%s: expected 2 satellites, got %d", name, n);
    if (n != 2) return;

    CHECK(prns[0] == 3 && prns[1] == 9,
          "%s: expected PRN 3 and 9, got %d and %d", name, prns[0], prns[1]);

    /* The reader reports the strongest signal per satellite. */
    CHECK(fabsf(cnr[0] - expect_prn3) < 0.001f,
          "%s: PRN 3 expected %.4f dB-Hz, got %.4f", name, expect_prn3, cnr[0]);
    CHECK(fabsf(cnr[1] - expect_prn9) < 0.001f,
          "%s: PRN 9 expected %.4f dB-Hz, got %.4f", name, expect_prn9, cnr[1]);

    printf("  PRN 3 %.4f dB-Hz, PRN 9 %.4f dB-Hz\n", cnr[0], cnr[1]);
}

int main(void)
{
    printf("== MSM C/N0 layout ==\n");

    /* MSM4 and MSM5: six bits, whole dB-Hz. */
    const unsigned long raw6[4] = { 33, 45, 51, 20 };
    case_msm(4, 1074, "MSM4 GPS",     raw6, 45.0f, 51.0f);
    case_msm(5, 1085, "MSM5 GLONASS", raw6, 45.0f, 51.0f);

    /* MSM6 and MSM7: ten bits, sixteenths of a dB-Hz.  700/16 = 43.75,
     * 812/16 = 50.75 -- values that no six-bit reading could produce. */
    const unsigned long raw10[4] = { 528, 700, 812, 320 };
    case_msm(6, 1096, "MSM6 Galileo", raw10, 43.75f, 50.75f);
    case_msm(7, 1077, "MSM7 GPS",     raw10, 43.75f, 50.75f);

    /* MSM1-3 carry no C/N0, and the reader must refuse rather than
     * guess -- but their satellites are still there to be counted, in a
     * header field every MSM shares. Refusing both is what made a
     * working MSM3 station report zero satellites and fail. */
    {
        unsigned char payload[128];
        int len = build_msm(4, 1073, raw6, payload);

        int prns[8]; float cnr[8];
        int n = msm_extract_cnr(payload, len, 1073, prns, cnr, 8, NULL);
        CHECK(n == 0, "MSM3: expected C/N0 refusal, got %d satellites", n);

        int gnss = 0;
        int sats = msm_extract_prns(payload, len, 1073, prns, 8, &gnss);
        CHECK(sats == 2, "MSM3: expected 2 satellites, got %d", sats);
        CHECK(gnss == 1, "MSM3: expected GPS, got gnss %d", gnss);
        if (sats == 2)
            CHECK(prns[0] == 3 && prns[1] == 9,
                  "MSM3: expected PRN 3 and 9, got %d and %d", prns[0], prns[1]);

        printf("MSM3 (type 1073): %d satellites, and no C/N0 to report\n", sats);
    }

    /* The message-detail decoder's own arithmetic.
     *
     * It converts the fine fields to metres for the GUI's detail view
     * and the CLI's verbose output, and it used one scale for all three:
     * 0.0001, which belongs to the phase rate alone.  The pseudorange
     * column was 5.6x out and the phase column 1.4x, from the day the
     * decoder was written until 2026-10-09, because no measurement ever
     * read those columns -- only a person looking at a table.  This
     * check is that person.
     *
     * The expected strings are computed here from RTCM 10403.3's scales
     * rather than taken from the code under test, and formatted exactly
     * as the decoder formats them, so a wrong scale cannot produce a
     * matching substring. */
    {
        const unsigned long raw[4] = { 528, 700, 812, 320 };
        unsigned char payload[128];
        int len = build_msm(7, 1077, raw, payload);

        /* build_msm writes 0x2AAA into every fine pseudorange and
         * 0x155555 into every fine phase; both are positive in MSM7's
         * 20- and 24-bit fields. */
        const double pr_lsb_m = 299792.458 / 536870912.0;   /* 2^-29 ms */
        const double ph_lsb_m = 299792.458 / 2147483648.0;  /* 2^-31 ms */
        char want_pr[32], want_ph[32], wrong_pr[32];
        snprintf(want_pr,  sizeof want_pr,  "%+10.4f", 0x2AAA   * pr_lsb_m);
        snprintf(want_ph,  sizeof want_ph,  "%+11.4f", 0x155555 * ph_lsb_m);
        snprintf(wrong_pr, sizeof wrong_pr, "%+10.4f", 0x2AAA   * 0.0001);

        RtcmStrBuf sb;
        rtcm_strbuf_init(&sb, 4096);
        rtcm_set_output_buffer(&sb);
        decode_rtcm_1077(payload, len);
        rtcm_set_output_buffer(NULL);

        CHECK(sb.buf && strstr(sb.buf, want_pr) != NULL,
              "detail view: expected a fine pseudorange of '%s' in the table",
              want_pr);
        CHECK(sb.buf && strstr(sb.buf, want_ph) != NULL,
              "detail view: expected a fine phase of '%s' in the table",
              want_ph);
        CHECK(!sb.buf || strstr(sb.buf, wrong_pr) == NULL,
              "detail view: the phase-rate scale is back on the pseudorange ('%s')",
              wrong_pr);

        printf("MSM7 detail view: fine PR '%s', fine PH '%s'\n", want_pr, want_ph);
        rtcm_strbuf_free(&sb);
    }

    if (failures) {
        printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nAll MSM C/N0 layout checks passed\n");
    return 0;
}
