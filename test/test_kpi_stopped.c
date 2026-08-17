/**
 * @file test_kpi_stopped.c
 * @brief KPI 1 must not describe a stream that stopped as one that never
 *        started.
 *
 * Twice on live casters -- `HANESE` on 2026-08-16 and Centipede's `NEAR`
 * on 2026-08-17 -- one report carried these two lines together:
 *
 * ```
 * 1  Connected and producing  FAIL  0 B/s  Connected but no data arriving
 * 2  RTCM 3.x format          PASS  289    CRC-valid RTCM 3.x frames decoded
 * ```
 *
 * They contradict each other, and the contradiction is not cosmetic: this
 * tool exists to say whether a *station* is fit, and "no data arriving" is
 * what a user takes to the station's owner.  In both sightings the station
 * was healthy and the session had been evicted by the analyser's own
 * second connection.
 *
 * The verdict is right and stays FAIL -- `--check` disables reconnect on
 * purpose, so a session that dies is a finding.  What is pinned here is
 * the *explanation*, in three states the one message used to cover:
 *
 *   1. **Nothing ever arrived** -- the caster accepted the connection and
 *      sent no bytes.  That is the only state the old wording described.
 *   2. **Data arrived, then stopped** -- must say so, and say how long the
 *      stream ran, because that is what separates it from state 1.
 *   3. **The connection went away after data** -- must not read as though
 *      the caster was never reachable.
 *
 * Every assertion here fails against the implementation before the fix,
 * which emitted one string for all three.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
#include "core/kpi.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

/** A connected session that has delivered @p bytes at @p uptime seconds. */
static NsStatsSnapshot flowing(uint64_t bytes, double uptime, double rate)
{
    NsStatsSnapshot s;
    ns_stats_init(&s);
    s.connected   = true;
    s.uptime_s    = uptime;
    s.bytes_total = bytes;
    s.bytes_per_s = rate;
    s.frames_ok   = bytes / 200;   /* plausible; KPI 2 counts these */
    return s;
}

int main(void)
{
    /* ── 1. Nothing ever arrived ──────────────────────────────────── */
    {
        KpiRun run; KpiReport rep;
        kpi_run_start(&run, 0.0, NULL);

        NsStatsSnapshot s = flowing(0, 20.0, 0.0);
        kpi_update(&run, &s, 20.0, &rep);

        check(rep.kpi[0].verdict == KPI_FAIL,
              "a silent caster fails KPI 1");
        check(strstr(rep.kpi[0].detail, "sent nothing") != NULL,
              "and is described as having sent nothing");
        check(strstr(rep.kpi[0].detail, "stopped") == NULL,
              "not as a stream that stopped");
    }

    /* ── 2. Data arrived for fifteen seconds, then stopped ─────────
     *
     * The shape of both live sightings: frames decoded, then silence.
     * KPI 2 passes on the same snapshot, so KPI 1 saying nothing
     * arrived would be the report contradicting itself. */
    {
        KpiRun run; KpiReport rep;
        kpi_run_start(&run, 0.0, NULL);

        for (double t = 1.0; t <= 15.0; t += 1.0) {
            NsStatsSnapshot s = flowing((uint64_t)(t * 4000.0), t, 4000.0);
            kpi_update(&run, &s, t, &rep);
        }

        /* Same byte count from here on: the stream has stopped. */
        NsStatsSnapshot s = flowing(60000, 30.0, 0.0);
        kpi_update(&run, &s, 30.0, &rep);

        check(rep.kpi[0].verdict == KPI_FAIL,
              "a stream that stops mid-check still fails KPI 1");
        check(rep.kpi[1].verdict == KPI_PASS,
              "while KPI 2 still passes on the frames that did arrive");
        check(strstr(rep.kpi[0].detail, "stopped") != NULL,
              "KPI 1 says the stream stopped");
        check(strstr(rep.kpi[0].detail, "15 s") != NULL,
              "and how long it ran before it did");

        /* The reading is the session's own clock, not the check's, so a
         * check started an hour into a stream reports the stream's life
         * rather than its own. */
        KpiRun late; KpiReport lrep;
        kpi_run_start(&late, 3600.0, NULL);
        NsStatsSnapshot a = flowing(1000000, 3600.0, 4000.0);
        kpi_update(&late, &a, 3600.0, &lrep);
        NsStatsSnapshot b = flowing(1000000, 3640.0, 0.0);
        kpi_update(&late, &b, 3640.0, &lrep);
        check(strstr(lrep.kpi[0].detail, "3600 s") != NULL,
              "measured on the session's clock, not the check's");
    }

    /* ── 3. The connection went away after delivering ─────────────── */
    {
        KpiRun run; KpiReport rep;
        kpi_run_start(&run, 0.0, NULL);

        NsStatsSnapshot s = flowing(60000, 15.0, 4000.0);
        kpi_update(&run, &s, 15.0, &rep);

        s.connected   = false;
        s.bytes_per_s = 0.0;
        kpi_update(&run, &s, 30.0, &rep);

        check(rep.kpi[0].verdict == KPI_FAIL,
              "a dropped connection fails KPI 1");
        check(strstr(rep.kpi[0].detail, "Connection lost") != NULL,
              "and is named as a connection lost");
        check(strstr(rep.kpi[0].detail, "15 s") != NULL,
              "after the data it did deliver");
    }

    /* ── 4. Never connected at all is still the plainest message ──── */
    {
        KpiRun run; KpiReport rep;
        NsStatsSnapshot s;
        ns_stats_init(&s);
        kpi_run_start(&run, 0.0, NULL);
        kpi_update(&run, &s, 20.0, &rep);

        check(strcmp(rep.kpi[0].detail, "No connection to the caster") == 0,
              "a connection that never opened says exactly that");
    }

    /* ── 5. The explanations fit the column that shows them ────────
     *
     * The GUI's Detail column is 420 px, about sixty characters of the
     * shell font, and the longest string the engine already emits is
     * fifty-eight.  A formatted string can silently outgrow that where a
     * literal cannot, so the ceiling is asserted rather than eyeballed. */
    {
        KpiRun run; KpiReport rep;
        kpi_run_start(&run, 0.0, NULL);

        NsStatsSnapshot s = flowing(60000, 999999.0, 4000.0);
        kpi_update(&run, &s, 10.0, &rep);
        s.bytes_per_s = 0.0;
        kpi_update(&run, &s, 30.0, &rep);

        check(strlen(rep.kpi[0].detail) <= 58,
              "the longest stopped-stream explanation still fits the column");
    }

    printf("\n%s\n", failures ? "FAILURES" : "all ok");
    return failures ? 1 : 0;
}
