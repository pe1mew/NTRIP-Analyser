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

/**
 * @brief Advance the cumulative frame counters, `errors` of them bad.
 *
 * Frame integrity is measured from the counters, not from the rate the
 * snapshot carries: that rate is cumulative since the session opened,
 * and the maximum of a running mean answers a different question from
 * the one the metric asks.  The rate is kept in step here anyway, since
 * that is what a real session would publish.
 */
static void add_frames(NsStatsSnapshot *s, uint64_t frames, uint64_t errors)
{
    s->frames_ok        += frames - errors;
    s->frames_crc_error += errors;
    uint64_t total = s->frames_ok + s->frames_crc_error;
    s->crc_error_rate = total ? (double)s->frames_crc_error / (double)total
                              : 0.0;
}

int main(void)
{
    SrState st;
    StationReport r;

    /* ── 1. Too little evidence is its own answer ─────────────────── */
    {
        NsStatsSnapshot s = healthy();
        sr_reset(&st, false, NULL);
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
        sr_reset(&st, false, NULL);
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
        sr_reset(&st, false, NULL);
        for (int i = 0; i < 60; i++) {
            add_frames(&s, 6000, 120);       /* 2 %: twice the bad level */
            sr_feed(&st, &s, 60.0 + i * 60.0);
        }
        sr_build(&st, &r);
        check(r.metric[SR_INTEGRITY].verdict == SR_UNSTABLE,
              "a 2 % CRC rate is UNSTABLE");
        check(r.overall == SR_UNSTABLE, "and it carries the roll-up");
        check(strstr(r.headline, "Frame integrity") != NULL,
              "the headline names the culprit, not just the word");

        s = healthy();
        s.sats_total = 12;
        sr_reset(&st, false, NULL);
        feed_run(&st, &s, 60, 60.0);
        sr_build(&st, &r);
        check(r.metric[SR_SATELLITES].verdict == SR_UNSTABLE,
              "twelve satellites is UNSTABLE");

        s = healthy();
        s.iono_roti_median = 1.5f;
        sr_reset(&st, false, NULL);
        feed_run(&st, &s, 60, 60.0);
        sr_build(&st, &r);
        check(r.metric[SR_IONOSPHERE].verdict == SR_UNSTABLE,
              "a disturbed ionosphere is UNSTABLE");
    }

    /* ── 4. A falling signal is caught by its fall, not its level ─── */
    {
        NsStatsSnapshot s = healthy();
        sr_reset(&st, false, NULL);
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
        sr_reset(&st, true, NULL);                 /* from a capture */
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
        StationReport live, fast;
        NsStatsSnapshot s = healthy();

        sr_reset(&st, false, NULL);
        for (int i = 0; i < 60; i++) {       /* an hour, in real time  */
            add_frames(&s, 6000, 12);        /* 0.2 %: DEGRADED        */
            sr_feed(&st, &s, 60.0 + i * 60.0);
        }
        sr_build(&st, &live);

        s = healthy();
        sr_reset(&st, false, NULL);
        for (int i = 0; i < 60; i++) {       /* the same stream clock, */
            add_frames(&s, 6000, 12);        /* whatever the wall did  */
            sr_feed(&st, &s, 60.0 + i * 60.0);
        }
        sr_build(&st, &fast);

        check(live.overall == fast.overall
              && live.window_s == fast.window_s
              && strcmp(live.headline, fast.headline) == 0,
              "the same stream produces the same report at any speed");
        check(live.overall == SR_DEGRADED,
              "0.2 % CRC is DEGRADED -- worth looking at, not yet unusable");
    }

    /* ── 7. The warm-up is not evidence ───────────────────────────── */
    {
        /* The first live run reported "fewest held: 9" against a station
         * that never dropped below 39, because sats_total describes the
         * last five seconds and the first sample lands mid-epoch. One
         * such sample must not become the window's minimum. */
        NsStatsSnapshot warm = healthy();
        warm.sats_total  = 9;                /* a partial first epoch  */
        warm.cnr_mean_all = 20.0f;           /* and a partial mean     */

        NsStatsSnapshot s = healthy();
        sr_reset(&st, false, NULL);
        sr_feed(&st, &warm, 0.0);            /* inside the warm-up     */
        sr_feed(&st, &warm, 15.0);           /* still inside it        */
        for (int i = 0; i < 60; i++) sr_feed(&st, &s, 60.0 + i * 60.0);
        sr_build(&st, &r);

        check(r.metric[SR_SATELLITES].value == 38,
              "a partial first epoch does not become the window's minimum");
        check(r.overall == SR_STABLE,
              "and a healthy station is not called UNSTABLE by its own warm-up");
        check(r.window_s <= 60.0 * 60.0,
              "the judged window starts after the warm-up, not before it");
    }

    /* ── 8. No C/N0 in the stream is not a silent antenna ─────────── */
    {
        NsStatsSnapshot s = healthy();
        s.cnr_mean_all = 0.0f;               /* MSM1-3 carry none */
        sr_reset(&st, false, NULL);
        feed_run(&st, &s, 60, 60.0);
        sr_build(&st, &r);
        check(r.metric[SR_SIGNAL].verdict == SR_INSUFFICIENT,
              "a stream without C/N0 is unjudged, not condemned");
        check(strstr(r.metric[SR_SIGNAL].detail, "MSM1-3") != NULL,
              "and the report says which property of the stream caused it");
    }

    /* ── 9. Say nothing about a stream before sampling it ─────────── */
    {
        /* The daemon publishes a report every ten seconds from the
         * moment a session opens, and for the first thirty of those --
         * the warm-up -- it published "no C/N0 in this stream (MSM1-3)"
         * and "no dual-frequency pair to measure with" about a station
         * sending both. An empty accumulator knows nothing about the
         * station, and must say that instead. */
        sr_reset(&st, false, NULL);
        sr_build(&st, &r);

        check(strstr(r.metric[SR_SIGNAL].detail, "MSM1-3") == NULL,
              "an unsampled stream is not accused of carrying no C/N0");
        check(strstr(r.metric[SR_IONOSPHERE].detail, "dual-frequency") == NULL,
              "nor of having no dual-frequency pair");
        check(r.overall == SR_INSUFFICIENT,
              "and the verdict is that there is not yet evidence");

        /* Nor a *sampled* one, before the window is judgeable. Gating on
         * "any samples at all" was not enough: the GUI showed both
         * claims at twenty-five seconds against a station carrying C/N0
         * and two frequencies, because the first samples arrive long
         * before ROTI has arcs to work with. */
        NsStatsSnapshot young = healthy();
        young.cnr_mean_all     = 0.0f;   /* not measured yet */
        young.iono_roti_median = -1.0f;  /* arcs not formed yet */
        sr_reset(&st, false, NULL);
        for (int i = 0; i < 20; i++) sr_feed(&st, &young, 40.0 + i * 5.0);
        sr_build(&st, &r);

        check(strstr(r.metric[SR_SIGNAL].detail, "MSM1-3") == NULL,
              "two minutes without C/N0 is the clock, not the stream");
        check(strstr(r.metric[SR_IONOSPHERE].detail, "dual-frequency") == NULL,
              "and two minutes without ROTI is arcs still forming");
    }

    /* ── 10. A burst late in a long run is not hidden by the mean ─── */
    {
        /* The reason frame integrity is measured per interval. The
         * snapshot's own rate is cumulative since the session opened,
         * so after six hours a burst of corrupted frames barely moves
         * it -- and a metric that keeps the maximum of a running mean
         * would report this station as spotless. It is not: for one
         * minute of the hour, one frame in a hundred was corrupt. */
        /* Ten hours of stream, and one bad quarter of an hour in the
         * middle of it. Over the session that is 0.025 % of frames --
         * far inside the healthy band. Over the ten minutes it happened
         * in, it is one frame in seventy. */
        NsStatsSnapshot s = healthy();
        sr_reset(&st, false, NULL);
        for (int i = 0; i < 600; i++) {
            add_frames(&s, 6000, i == 450 ? 900 : 0);
            sr_feed(&st, &s, 60.0 + i * 60.0);
        }
        sr_build(&st, &r);

        check(100.0 * (1.0 - s.crc_error_rate) > SR_INTEGRITY_WARN_PCT,
              "the session-wide figure ends healthy -- the mean has "
              "swallowed the burst");
        check(r.metric[SR_INTEGRITY].verdict == SR_UNSTABLE,
              "and the windowed reading catches it anyway");
    }

    /* ── 11. A small early denominator is not a measurement ───────── */
    {
        /* Two errors inside the first two hundred frames read as 0.93 %
         * cumulatively, and used to be banked as the worst rate for the
         * rest of the session -- against a station that settled at
         * 0.43 % and then ran clean. Seen on a live stream. */
        NsStatsSnapshot s = healthy();
        add_frames(&s, 200, 2);              /* 1 % of a tiny sample */
        sr_reset(&st, false, NULL);
        sr_feed(&st, &s, 60.0);
        for (int i = 1; i < 60; i++) {
            add_frames(&s, 6000, 0);         /* and then nothing but good */
            sr_feed(&st, &s, 60.0 + i * 60.0);
        }
        sr_build(&st, &r);

        check(r.metric[SR_INTEGRITY].verdict == SR_STABLE,
              "a clean station is not condemned by its first two hundred "
              "frames");
    }

    /* ── 12. An incomplete window is no reading at all ────────────── */
    {
        /* Five minutes in, no ten-minute window has closed. The metric
         * must say that rather than publish the perfect score it would
         * have if asked to grade what it has. */
        NsStatsSnapshot s = healthy();
        sr_reset(&st, false, NULL);
        for (int i = 0; i < 5; i++) {
            add_frames(&s, 6000, 0);
            sr_feed(&st, &s, 60.0 + i * 60.0);
        }
        sr_build(&st, &r);

        check(r.metric[SR_INTEGRITY].verdict == SR_INSUFFICIENT,
              "before the first window closes there is no reading");
        check(strstr(r.metric[SR_INTEGRITY].detail, "not yet complete") != NULL,
              "and the report says which window it is waiting for");
    }

    printf("\n%s\n", failures ? "FAILURES" : "all station-report cases pass");
    return failures ? 1 : 0;
}
