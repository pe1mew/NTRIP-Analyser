/**
 * @file test_station_report.c
 * @brief The tier-2 report: what it says, and what it refuses to say.
 *
 * Three properties matter more than any individual threshold, and each
 * is a decision that would otherwise erode:
 *
 *   1. **"Not enough evidence yet" is a verdict**, not a placeholder. A
 *      report over ninety seconds must say so rather than grade an hour's
 *      question on a minute's data — the mistake tier 1 made three times
 *      before its sustain window was added.
 *   2. **Live-only metrics are absent from a replay, not zero.** A
 *      capture holds no arrival times and never drops, so a reconnect
 *      count derived from one would be an invention.
 *   3. **Windows are stream time.** The same snapshots fed at any speed
 *      must produce the identical report, which is what makes an
 *      archived capture a record.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
#include "core/station_report.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

/** A healthy station: plenty of satellites, clean frames, quiet sky. */
static NsStatsSnapshot healthy(void)
{
    NsStatsSnapshot s;
    ns_stats_init(&s);
    s.reconnects       = 0;
    s.crc_error_rate   = 0.0;
    s.cnr_mean_all     = 45.0f;
    s.sats_total       = 38;
    s.iono_roti_median = 0.2f;
    s.types_offrate    = 0;
    return s;
}

/** Feed `n` snapshots at `step` seconds of stream time apart. */
static void feed_run(SrState *st, NsStatsSnapshot *s, int n, double step)
{
    for (int i = 0; i < n; i++) sr_feed(st, s, i * step);
}

int main(void)
{
    SrState st;
    StationReport r;

    /* ── 1. Too little evidence is its own answer ─────────────────── */
    {
        NsStatsSnapshot s = healthy();
        sr_reset(&st, false);
        feed_run(&st, &s, 5, 10.0);          /* 40 s, 5 samples */
        sr_build(&st, &r);

        check(r.overall == SR_INSUFFICIENT,
              "ninety seconds of evidence yields no verdict");
        check(strstr(r.headline, "INSUFFICIENT") != NULL,
              "and the headline says so in those words");
        check(strcmp(sr_verdict_name(r.overall), "INSUFFICIENT EVIDENCE") == 0,
              "which is never one of tier 1's words");
    }

    /* ── 2. A long, clean run is STABLE ───────────────────────────── */
    {
        NsStatsSnapshot s = healthy();
        sr_reset(&st, false);
        feed_run(&st, &s, 60, 60.0);         /* 59 min, 60 samples */
        sr_build(&st, &r);

        check(r.overall == SR_STABLE, "an hour of clean data is STABLE");
        check(r.window_s > SR_MIN_WINDOW_S, "the window is stream time");
        check(r.metric[SR_AVAILABILITY].available,
              "availability is reported for a live session");
    }

    /* ── 3. Each metric can carry the verdict alone ───────────────── */
    {
        NsStatsSnapshot s = healthy();
        s.crc_error_rate = 0.02;             /* twice the bad threshold */
        sr_reset(&st, false);
        feed_run(&st, &s, 60, 60.0);
        sr_build(&st, &r);
        check(r.metric[SR_INTEGRITY].verdict == SR_UNSTABLE,
              "a 2 % CRC rate is UNSTABLE");
        check(r.overall == SR_UNSTABLE, "and it carries the roll-up");
        check(strstr(r.headline, "Frame integrity") != NULL,
              "the headline names the culprit, not just the word");

        s = healthy();
        s.sats_total = 12;
        sr_reset(&st, false);
        feed_run(&st, &s, 60, 60.0);
        sr_build(&st, &r);
        check(r.metric[SR_SATELLITES].verdict == SR_UNSTABLE,
              "twelve satellites is UNSTABLE");

        s = healthy();
        s.iono_roti_median = 1.5f;
        sr_reset(&st, false);
        feed_run(&st, &s, 60, 60.0);
        sr_build(&st, &r);
        check(r.metric[SR_IONOSPHERE].verdict == SR_UNSTABLE,
              "a disturbed ionosphere is UNSTABLE");
    }

    /* ── 4. A falling signal is caught by its fall, not its level ─── */
    {
        NsStatsSnapshot s = healthy();
        sr_reset(&st, false);
        for (int i = 0; i < 60; i++) {
            s.cnr_mean_all = (i < 30) ? 48.0f : 41.0f;   /* a 7 dB step */
            sr_feed(&st, &s, i * 60.0);
        }
        sr_build(&st, &r);
        check(r.metric[SR_SIGNAL].verdict == SR_UNSTABLE,
              "a 7 dB fall in C/N0 is UNSTABLE though 41 dB-Hz is a fine level");
    }

    /* ── 5. Live-only metrics are absent from a replay, never zero ── */
    {
        NsStatsSnapshot s = healthy();
        sr_reset(&st, true);                 /* from a capture */
        feed_run(&st, &s, 60, 60.0);
        sr_build(&st, &r);

        check(!r.metric[SR_AVAILABILITY].available,
              "availability is unavailable from a capture");
        check(r.metric[SR_AVAILABILITY].live_only,
              "and is marked live-only");
        check(strstr(r.metric[SR_AVAILABILITY].detail, "capture") != NULL,
              "the report says why rather than showing a clean zero");
        check(r.overall == SR_STABLE,
              "an unavailable metric does not drag the roll-up down");
    }

    /* ── 6. Replay speed cannot change the report ─────────────────── */
    {
        NsStatsSnapshot s = healthy();
        s.crc_error_rate = 0.002;            /* enough to be DEGRADED */
        StationReport live, fast;

        sr_reset(&st, false);
        feed_run(&st, &s, 60, 60.0);         /* an hour, in real time */
        sr_build(&st, &live);

        sr_reset(&st, false);
        feed_run(&st, &s, 60, 60.0);         /* the same stream clock,
                                              * whatever the wall clock
                                              * did while replaying */
        sr_build(&st, &fast);

        check(live.overall == fast.overall
              && live.window_s == fast.window_s
              && strcmp(live.headline, fast.headline) == 0,
              "the same stream produces the same report at any speed");
        check(live.overall == SR_DEGRADED,
              "0.2 % CRC is DEGRADED -- worth looking at, not yet unusable");
    }

    /* ── 7. No C/N0 in the stream is not a silent antenna ─────────── */
    {
        NsStatsSnapshot s = healthy();
        s.cnr_mean_all = 0.0f;               /* MSM1-3 carry none */
        sr_reset(&st, false);
        feed_run(&st, &s, 60, 60.0);
        sr_build(&st, &r);
        check(r.metric[SR_SIGNAL].verdict == SR_INSUFFICIENT,
              "a stream without C/N0 is unjudged, not condemned");
        check(strstr(r.metric[SR_SIGNAL].detail, "MSM1-3") != NULL,
              "and the report says which property of the stream caused it");
    }

    printf("\n%s\n", failures ? "FAILURES" : "all station-report cases pass");
    return failures ? 1 : 0;
}
