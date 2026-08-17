/**
 * @file test_stream_clock.c
 * @brief Elapsed time as the data measures it, and its three sharp edges.
 *
 * `stream_time_s` exists so that a report over a replayed capture covers
 * the window the capture holds rather than the seconds the replay took.
 * That makes it the only clock in the codebase derived from the stream,
 * and every way it can be wrong is a way an archived capture can be
 * misjudged years after the station is gone:
 *
 *   1. **The constellations do not share a clock.**  GLONASS counts a
 *      day where GPS counts a week, and BeiDou's week is offset fourteen
 *      seconds from Galileo's.  Accumulating across two of them steps
 *      the clock at every alternation.
 *   2. **The field wraps** -- at a week, and for GLONASS at a day.  The
 *      same class of fault made a day-old GLONASS orbit read as an hour
 *      old, which is why it gets a case here before it gets a station.
 *   3. **A frame can arrive late**, and a step backwards is not a wrap.
 *
 * A dropout is deliberately none of those: it advances the clock,
 * because the epochs on either side say so.
 *
 * The frames are built rather than recorded, for the same reason as in
 * test_capture.c -- and because a week rollover is not something one
 * waits for.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
#include "session/ntrip_session.h"
#include "core/rtcm3x_parser.h"   /* crc24q */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

static void check_near(double got, double want, double tol, const char *what)
{
    int ok = fabs(got - want) <= tol;
    printf("%s: %s (got %.3f, wanted %.3f)\n", ok ? "ok  " : "FAIL",
           what, got, want);
    if (!ok) failures++;
}

/* ── Building RTCM with a chosen epoch ───────────────────────────────
 *
 * MSM and legacy observation headers agree on the first 54 bits:
 *   DF002 message number    12 @ 0
 *   DF003 reference station 12 @ 12
 *   epoch                   30 @ 24   (27 for legacy GLONASS)
 */

#define PAYLOAD_LEN 12

/** @brief Write one frame of @p msg_type carrying @p epoch.  Returns length. */
static int build_frame(unsigned char *out, int msg_type, uint32_t epoch)
{
    out[0] = 0xD3;
    out[1] = (unsigned char)((PAYLOAD_LEN >> 8) & 0x03);
    out[2] = (unsigned char)(PAYLOAD_LEN & 0xFF);

    unsigned char *p = out + 3;
    memset(p, 0, PAYLOAD_LEN);

    p[0] = (unsigned char)(msg_type >> 4);
    p[1] = (unsigned char)((msg_type & 0x0F) << 4);   /* station id 0 */
    p[2] = 0;

    epoch &= 0x3FFFFFFFu;                             /* 30 bits */
    p[3] = (unsigned char)((epoch >> 22) & 0xFF);
    p[4] = (unsigned char)((epoch >> 14) & 0xFF);
    p[5] = (unsigned char)((epoch >>  6) & 0xFF);
    p[6] = (unsigned char)((epoch & 0x3F) << 2);

    uint32_t crc = crc24q(out, (size_t)(3 + PAYLOAD_LEN));
    out[3 + PAYLOAD_LEN + 0] = (unsigned char)((crc >> 16) & 0xFF);
    out[3 + PAYLOAD_LEN + 1] = (unsigned char)((crc >>  8) & 0xFF);
    out[3 + PAYLOAD_LEN + 2] = (unsigned char)( crc        & 0xFF);
    return PAYLOAD_LEN + 6;
}

/** @brief One frame of a stream to build: a type and an epoch value. */
typedef struct { int msg_type; uint32_t epoch; } Beat;

static const char *SRC = "test_stream_clock_src.rtcm3";

/**
 * @brief Write @p n beats, replay them, and return the stream clock.
 *
 * Replay is the point: the file goes past as fast as the disk allows, so
 * a clock that agreed with the wall would report roughly zero for every
 * case below.
 */
static double replay(const Beat *beats, int n)
{
    FILE *f = fopen(SRC, "wb");
    if (!f) { check(0, "could not write the source stream"); return -99.0; }
    unsigned char frame[64];
    for (int i = 0; i < n; i++) {
        int len = build_frame(frame, beats[i].msg_type, beats[i].epoch);
        fwrite(frame, 1, (size_t)len, f);
    }
    fclose(f);

    NsOptions opt;
    ns_options_default(&opt);
    opt.stats_interval_s = 0.0;

    NtripSession *s = ns_open_file(SRC, &opt, NULL, NULL);
    if (!s) { check(0, "session could not be allocated"); return -99.0; }
    while (ns_pump(s, 0) >= 0) { /* to end of file */ }

    const NsStatsSnapshot *snap = ns_stats(s);
    double t = snap ? snap->stream_time_s : -99.0;
    ns_close(s);
    remove(SRC);
    return t;
}

/** @brief `n` beats of one type, `step_ms` apart, starting at `first`. */
static int fill_run(Beat *out, int at, int n, int msg_type,
                    uint32_t first, uint32_t step_ms)
{
    for (int i = 0; i < n; i++) {
        out[at + i].msg_type = msg_type;
        out[at + i].epoch    = first + (uint32_t)i * step_ms;
    }
    return at + n;
}

#define WEEK_MS 604800000u
#define DAY_MS   86400000u

int main(void)
{
    Beat b[64];
    int n;

    /* ── 1. A plain 1 Hz stream measures its own length ───────────── */
    {
        n = fill_run(b, 0, 11, 1077, 100000, 1000);
        check_near(replay(b, n), 10.0, 0.001,
                   "eleven epochs a second apart are ten seconds of stream");
    }

    /* ── 2. A dropout is stream time, not a fault to be smoothed ──── */
    {
        n = fill_run(b, 0, 5, 1077, 100000, 1000);
        n = fill_run(b, n, 5, 1077, 100000 + 4000 + 600000, 1000);
        check_near(replay(b, n), 608.0, 0.001,
                   "a ten-minute gap advances the clock by ten minutes");
    }

    /* ── 3. The week ends, and the clock does not go backwards ────── */
    {
        /* Three epochs before midnight Saturday, three after.  The last
         * of the first run is one second short of the boundary, so the
         * six of them span five seconds across it. */
        n = fill_run(b, 0, 3, 1077, WEEK_MS - 3000, 1000);
        n = fill_run(b, n, 3, 1077, 0, 1000);
        check_near(replay(b, n), 5.0, 0.001,
                   "a week rollover reads as one second, not minus a week");
    }

    /* ── 4. GLONASS wraps a day, and carries a day field above it ─── */
    {
        /* Day 3, the last two seconds of it, then day 4 from zero. The
         * 3-bit day advances too, which is why the clock reads only the
         * 27 bits below it -- read whole, this field would jump by
         * 134 217 728 counts at the boundary. */
        Beat g[6];
        int k = 0;
        for (int i = 0; i < 3; i++) {
            g[k].msg_type = 1087;
            g[k++].epoch  = (3u << 27) | (DAY_MS - 3000 + (uint32_t)i * 1000);
        }
        for (int i = 0; i < 3; i++) {
            g[k].msg_type = 1087;
            g[k++].epoch  = (4u << 27) | ((uint32_t)i * 1000);
        }
        check_near(replay(g, k), 5.0, 0.001,
                   "a GLONASS day rollover reads as one second");
    }

    /* ── 5. Two constellations, one clock ──────────────────────────
     *
     * BeiDou's week is offset fourteen seconds from GPS's. A clock that
     * accumulated across both would step by fourteen seconds at every
     * alternation -- 140 s of invented stream over these ten frames,
     * against ten seconds of real one. */
    {
        int k = 0;
        for (int i = 0; i < 11; i++) {
            b[k].msg_type = 1077;
            b[k++].epoch  = 100000 + (uint32_t)i * 1000;
            b[k].msg_type = 1127;                       /* BeiDou */
            b[k++].epoch  = 100000 - 14000 + (uint32_t)i * 1000;
        }
        check_near(replay(b, k), 10.0, 0.001,
                   "a mixed stream counts one constellation, not the sum");
    }

    /* ── 6. A late frame is not a wrap ─────────────────────────────── */
    {
        n = fill_run(b, 0, 6, 1077, 100000, 1000);
        b[3].epoch = 100000;              /* arrives three seconds late */
        check_near(replay(b, n), 5.0, 0.001,
                   "a frame out of order neither rewinds nor adds a week");
    }

    /* ── 7. Duplicate frames of one epoch are one epoch ────────────── */
    {
        int k = 0;
        for (int i = 0; i < 6; i++) {
            /* What a large constellation actually sends: one epoch
             * split across several frames of the same type. */
            b[k].msg_type = 1077;  b[k++].epoch = 100000 + (uint32_t)i * 1000;
            b[k].msg_type = 1077;  b[k++].epoch = 100000 + (uint32_t)i * 1000;
        }
        check_near(replay(b, k), 5.0, 0.001,
                   "an epoch split across frames advances the clock once");
    }

    /* ── 8. A stream with no epochs has no clock, and says so ─────── */
    {
        static const int quiet[] = { 1005, 1008, 1033, 1230, 1013 };
        for (int i = 0; i < 5; i++) {
            b[i].msg_type = quiet[i];
            b[i].epoch    = 0;
        }
        double t = replay(b, 5);
        check(t == NS_UNSET,
              "a stream of station messages reports no clock, not zero");
    }

    /* ── 9. A GLONASS-only lock gives way to a week-based one ─────── */
    {
        /* Fewer wraps is fewer chances to be wrong, so the clock trades
         * up when GPS appears. The switch costs the delta across it and
         * nothing else. */
        int k = 0;
        for (int i = 0; i < 3; i++) {
            b[k].msg_type = 1087;
            b[k++].epoch  = (1u << 27) | (10000 + (uint32_t)i * 1000);
        }
        for (int i = 0; i < 4; i++) {
            b[k].msg_type = 1077;
            b[k++].epoch  = 500000 + (uint32_t)i * 1000;
        }
        check_near(replay(b, k), 5.0, 0.001,
                   "two seconds of GLONASS plus three of GPS, and no jump "
                   "across the change of scale");
    }

    /* ── 10. Legacy observations have a clock too ──────────────────── */
    {
        n = fill_run(b, 0, 11, 1004, 100000, 1000);
        check_near(replay(b, n), 10.0, 0.001,
                   "a station still sending 1004 is measurable");
    }

    /* ── 11. A replay reports the intervals it recorded ────────────── */
    {
        /* Everything in this file arrives in microseconds, so measured
         * against the wall a capture's epochs are all simultaneous: the
         * message-type table read 0.000 s and the delivery-rate metric
         * was meaningless offline.  A replay is timed by the stream it
         * holds, so it reports the second between epochs that is
         * actually recorded in it. */
        n = fill_run(b, 0, 11, 1077, 100000, 1000);

        FILE *f = fopen(SRC, "wb");
        if (!f) { check(0, "could not write the source stream"); return 1; }
        unsigned char frame[64];
        for (int i = 0; i < n; i++) {
            int len = build_frame(frame, b[i].msg_type, b[i].epoch);
            fwrite(frame, 1, (size_t)len, f);
        }
        fclose(f);

        NsOptions opt;
        ns_options_default(&opt);
        opt.stats_interval_s = 0.0;

        NtripSession *s = ns_open_file(SRC, &opt, NULL, NULL);
        if (!s) { check(0, "session could not be allocated"); return 1; }
        while (ns_pump(s, 0) >= 0) { }

        const NsStatsSnapshot *snap = ns_stats(s);
        const NsTypeStats *t = (snap && snap->n_types > 0) ? &snap->types[0]
                                                           : NULL;
        check(t != NULL && t->epochs == 11, "the replay counted its epochs");
        if (t) {
            check_near(t->avg_dt, 1.0, 0.001,
                       "a replayed epoch interval is the recorded second");
            check_near(t->max_dt, 1.0, 0.001,
                       "and the widest gap is the one in the file");
        }
        ns_close(s);
        remove(SRC);
    }

    printf("\n%s\n", failures ? "FAILURES" : "all stream-clock cases pass");
    return failures ? 1 : 0;
}
