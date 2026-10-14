/**
 * @file test_kpi_watch.c
 * @brief The watch accumulator, and the record it keeps of each drop.
 *
 * An 8-hour watch on the phone ended "2 degradations, the worst
 * FAILED", and nothing anywhere could say which check had failed or for
 * how long: the watch kept a count, the worst verdict and the time of
 * the last drop (station-self-position.md, P7). It now keeps each drop
 * -- when, how long, how bad, and which KPIs were failing, warning or
 * starting again -- and this is the first test the accumulator has had.
 *
 * Reports are built by hand: the watch reads only the roll-up and each
 * KPI's verdict, so nothing here needs a stream.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
#include "core/kpi.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

/** A report with every KPI passing and the given roll-up. */
static KpiReport report(int overall)
{
    KpiReport r;
    memset(&r, 0, sizeof r);
    for (int i = 0; i < KPI_COUNT; i++) r.kpi[i].verdict = KPI_PASS;
    r.overall = overall;
    return r;
}

/** KPI n, numbered as the screens number it. */
static unsigned bit(int n) { return 1u << (n - 1); }

static int near(double a, double b) { return fabs(a - b) < 1e-9; }

int main(void)
{
    KpiWatch w;
    KpiReport r;

    /* ── Warm-up is not a drop ───────────────────────────────────── */
    kpi_watch_start(&w, 1000.0);
    r = report(KPI_RUN_RUNNING);   kpi_watch_update(&w, &r, 1010.0);
    r = report(KPI_RUN_CAUTION);   kpi_watch_update(&w, &r, 1020.0);
    check(w.degradations == 0 && w.n_drops == 0,
          "CAUTION on the way up records no drop");
    r = report(KPI_RUN_OK);        kpi_watch_update(&w, &r, 1030.0);
    check(w.armed, "the first OK arms the watch");

    /* ── A FAILED spell: which KPIs, how long, how bad ───────────── */
    r = report(KPI_RUN_OK);        kpi_watch_update(&w, &r, 1100.0);
    r = report(KPI_RUN_FAILED);
    r.kpi[3].verdict = KPI_FAIL;   /* KPI 4 */
    r.kpi[1].verdict = KPI_WARN;   /* KPI 2 */
    kpi_watch_update(&w, &r, 1101.0);
    check(w.degradations == 1 && w.n_drops == 1, "leaving OK opens a drop");
    check(near(w.drops[0].t_s, 101.0), "dated on the watch's own clock");
    check(w.drops[0].dur_s < 0.0, "and open until OK returns");

    r = report(KPI_RUN_CAUTION);
    r.kpi[5].verdict = KPI_WARN;   /* KPI 6, later in the same spell */
    kpi_watch_update(&w, &r, 1104.0);
    r = report(KPI_RUN_OK);        kpi_watch_update(&w, &r, 1107.0);

    const KpiDrop *d = &w.drops[0];
    check(near(d->dur_s, 6.0), "closed at the first OK: 6 s out");
    check(d->worst == KPI_RUN_FAILED,
          "its worst is FAILED, not the CAUTION it ended on");
    check(d->fail_mask == bit(4), "KPI 4 named as failing");
    check(d->warn_mask == (bit(2) | bit(6)),
          "KPI 2 and KPI 6 named as warning: the union over the spell");
    check(d->pending_mask == 0, "and nothing restarting");
    check(near(w.last_degrade_t, 101.0), "last_degrade_t still agrees");

    /* ── A reconnect: every check starts again ──────────────────── */
    r = report(KPI_RUN_RUNNING);
    for (int i = 0; i < KPI_COUNT; i++) r.kpi[i].verdict = KPI_PENDING;
    kpi_watch_update(&w, &r, 1200.0);
    r = report(KPI_RUN_OK);        kpi_watch_update(&w, &r, 1290.0);
    d = &w.drops[1];
    check(w.n_drops == 2 && d->worst == KPI_RUN_RUNNING,
          "a reconnect is a drop whose worst is RUNNING, not FAILED");
    check(d->pending_mask == 0xFFu && d->fail_mask == 0,
          "every KPI restarting, none failing");
    check(near(d->dur_s, 90.0), "and as long as the run took to settle");

    /* ── Still out when read ─────────────────────────────────────── */
    r = report(KPI_RUN_CAUTION);
    r.kpi[6].verdict = KPI_WARN;
    kpi_watch_update(&w, &r, 1300.0);
    check(w.drops[2].dur_s < 0.0, "a spell still in progress has no duration");

    /* ── The ring keeps the last KPI_WATCH_DROPS ─────────────────── */
    double t = 1400.0;
    for (int k = 0; k < 10; k++) {
        r = report(KPI_RUN_OK);     kpi_watch_update(&w, &r, t);
        r = report(KPI_RUN_FAILED);
        r.kpi[0].verdict = KPI_FAIL;
        kpi_watch_update(&w, &r, t + 1.0);
        t += 10.0;
    }
    r = report(KPI_RUN_OK);        kpi_watch_update(&w, &r, t);
    check(w.degradations == 13, "the count keeps every drop");
    check(w.n_drops == KPI_WATCH_DROPS, "the detail keeps the last eight");
    check(near(w.drops[KPI_WATCH_DROPS - 1].t_s, (t - 10.0 + 1.0) - 1000.0),
          "the newest is last");
    check(near(w.drops[0].t_s, 1421.0 - 1000.0),
          "and the oldest kept is the sixth drop of thirteen, the first five gone");
    for (int k = 0; k < KPI_WATCH_DROPS; k++)
        if (w.drops[k].dur_s < 0.0 || w.drops[k].fail_mask != bit(1)) {
            check(0, "every kept drop closed, with KPI 1 failing");
            break;
        }

    if (failures) {
        printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nAll watch checks passed\n");
    return 0;
}
