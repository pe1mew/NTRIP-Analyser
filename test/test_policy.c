/**
 * @file test_policy.c
 * @brief Thresholds as data: the defaults are the constants, and a
 *        policy actually decides the verdict.
 *
 * Phase 1 of design/work-items/thresholds-track.md moved every threshold
 * out of a `#define` used at the point of comparison and into a policy
 * structure carried by the run. That change is supposed to be invisible,
 * and "supposed to be invisible" is the most dangerous kind of change
 * there is: nothing about reading the code afterwards reveals a
 * threshold that was mistyped into its new home, or one that was added
 * to the struct and never filled.
 *
 * So three properties, each of which fails loudly rather than quietly:
 *
 *   1. **Every default equals the constant it replaced.** A wrong digit
 *      here would move a verdict for every user of every program.
 *   2. **No policy behaves exactly like the defaults.** `NULL` is what
 *      all callers pass today, so if the two ever diverged, every
 *      existing frontend would be judging by something else.
 *   3. **A policy that differs changes the verdict.** Without this the
 *      first two could both pass on an engine that reads the struct and
 *      ignores it -- which is precisely the failure a refactor like this
 *      invites.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
#include "core/kpi.h"
#include "core/iono.h"
#include "core/station_report.h"
#include "core/thresholds.h"
#include "core/vrs_check.h"

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
    s.cnr_mean_all     = 45.0f;
    s.sats_total       = 38;
    s.iono_roti_median = 0.2f;
    return s;
}

int main(void)
{
    /* ── 1. The defaults are the documented constants ─────────────── */
    {
        SrPolicy p;
        sr_policy_defaults(&p);

        check(p.reconnects_warn_per_h == SR_RECONNECTS_WARN_PER_H &&
              p.reconnects_bad_per_h  == SR_RECONNECTS_BAD_PER_H,
              "tier 2 availability defaults match their constants");
        check(p.integrity_warn_pct == SR_INTEGRITY_WARN_PCT &&
              p.integrity_bad_pct  == SR_INTEGRITY_BAD_PCT  &&
              p.integrity_window_s == SR_INTEGRITY_WINDOW_S,
              "so do frame integrity's, including its window");
        check(p.cnr_drop_warn == SR_CNR_DROP_WARN &&
              p.cnr_drop_bad  == SR_CNR_DROP_BAD,
              "so does the C/N0 fall");
        check(p.sats_warn == SR_SATS_WARN && p.sats_bad == SR_SATS_BAD,
              "so do the satellite counts");
        check(p.roti_warn == IONO_ROTI_UNSETTLED &&
              p.roti_bad  == IONO_ROTI_DISTURBED,
              "and the ionosphere still uses iono.h's own scale");
        check(p.offrate_warn == SR_OFFRATE_WARN &&
              p.offrate_bad  == SR_OFFRATE_BAD,
              "so does the delivery rate");
        check(p.warmup_s     == SR_WARMUP_S &&
              p.min_window_s == SR_MIN_WINDOW_S &&
              p.min_samples  == SR_MIN_SAMPLES,
              "and the evidence rules, which are not station thresholds "
              "but decide whether anything is said at all");

        KpiPolicy k;
        kpi_policy_defaults(&k);
        check(k.sustain_s       == KPI_SUSTAIN_S &&
              k.min_bytes_per_s == KPI_MIN_BYTES_PER_S &&
              k.arp_deadline_s  == KPI_ARP_DEADLINE_S &&
              k.msm_max_dt_s    == KPI_MSM_MAX_DT_S,
              "tier 1's first four defaults match their constants");
        check(k.min_cnr_median     == KPI_MIN_CNR_MEDIAN &&
              k.min_integrity_pct  == KPI_MIN_INTEGRITY_PCT &&
              k.bad_integrity_pct  == KPI_BAD_INTEGRITY_PCT &&
              k.integrity_window_s == KPI_INTEGRITY_WINDOW_S,
              "and so do C/N0 and integrity");

        static const int expect[8] = KPI_EXPECT_SATS;
        int same = (k.expect_unknown == KPI_EXPECT_UNKNOWN);
        for (int i = 0; i < 8; i++) if (k.expect_sats[i] != expect[i]) same = 0;
        check(same, "the per-constellation table is copied entire");
    }

    /* ── 2. No policy is the default policy ───────────────────────── */
    {
        /* What every caller passes today. If these ever diverged, every
         * frontend would silently be judging by something else. */
        NsStatsSnapshot s = healthy();
        SrPolicy def;
        sr_policy_defaults(&def);

        SrState a, b;
        StationReport ra, rb;
        sr_reset(&a, false, NULL);
        sr_reset(&b, false, &def);
        for (int i = 0; i < 60; i++) {
            sr_feed(&a, &s, 60.0 + i * 60.0);
            sr_feed(&b, &s, 60.0 + i * 60.0);
        }
        sr_build(&a, &ra);
        sr_build(&b, &rb);

        int identical = (ra.overall == rb.overall) &&
                        (ra.window_s == rb.window_s) &&
                        (ra.samples == rb.samples) &&
                        strcmp(ra.headline, rb.headline) == 0;
        for (int i = 0; i < SR_METRIC_COUNT; i++)
            if (ra.metric[i].verdict != rb.metric[i].verdict ||
                ra.metric[i].value   != rb.metric[i].value   ||
                ra.metric[i].limit   != rb.metric[i].limit   ||
                strcmp(ra.metric[i].detail, rb.metric[i].detail) != 0)
                identical = 0;
        check(identical,
              "a report with no policy is the report with the default one");

        KpiPolicy kdef;
        kpi_policy_defaults(&kdef);
        KpiRun run_a, run_b;
        KpiReport rep_a, rep_b;
        kpi_run_start(&run_a, 0.0, NULL);
        kpi_run_start(&run_b, 0.0, &kdef);
        for (int i = 0; i < 40; i++) {
            kpi_update(&run_a, &s, (double)i * 5.0, &rep_a);
            kpi_update(&run_b, &s, (double)i * 5.0, &rep_b);
        }
        int same = (rep_a.overall == rep_b.overall);
        for (int i = 0; i < KPI_COUNT; i++)
            if (rep_a.kpi[i].verdict != rep_b.kpi[i].verdict ||
                rep_a.kpi[i].value   != rep_b.kpi[i].value   ||
                rep_a.kpi[i].limit   != rep_b.kpi[i].limit)
                same = 0;
        check(same, "and a check with no policy is the check with it");
    }

    /* ── 3. A policy that differs is a verdict that differs ────────── */
    {
        /* The property the first two cannot show: that the engine reads
         * the struct rather than merely storing it. A station holding 38
         * satellites is comfortably STABLE by default; against a network
         * that wants 60, it is not. */
        NsStatsSnapshot s = healthy();
        SrPolicy strict;
        sr_policy_defaults(&strict);
        strict.sats_warn = 60;
        strict.sats_bad  = 40;

        SrState st;
        StationReport r;
        sr_reset(&st, false, NULL);
        for (int i = 0; i < 60; i++) sr_feed(&st, &s, 60.0 + i * 60.0);
        sr_build(&st, &r);
        check(r.metric[SR_SATELLITES].verdict == SR_STABLE,
              "38 satellites is STABLE by the built-in standard");

        sr_reset(&st, false, &strict);
        for (int i = 0; i < 60; i++) sr_feed(&st, &s, 60.0 + i * 60.0);
        sr_build(&st, &r);
        check(r.metric[SR_SATELLITES].verdict == SR_UNSTABLE,
              "and UNSTABLE against a policy that wants sixty");
        check(r.metric[SR_SATELLITES].limit == 60.0,
              "the row reports the limit it was actually held to");

        /* And the other direction, which is the point of the feature: a
         * station failing a standard aimed at survey work can be judged
         * by one aimed at its own purpose. */
        SrPolicy lenient;
        sr_policy_defaults(&lenient);
        lenient.sats_warn = 20;
        lenient.sats_bad  = 10;

        NsStatsSnapshot thin = healthy();
        thin.sats_total = 22;

        sr_reset(&st, false, NULL);
        for (int i = 0; i < 60; i++) sr_feed(&st, &thin, 60.0 + i * 60.0);
        sr_build(&st, &r);
        check(r.metric[SR_SATELLITES].verdict == SR_DEGRADED,
              "22 satellites is DEGRADED by the built-in standard");

        sr_reset(&st, false, &lenient);
        for (int i = 0; i < 60; i++) sr_feed(&st, &thin, 60.0 + i * 60.0);
        sr_build(&st, &r);
        check(r.metric[SR_SATELLITES].verdict == SR_STABLE,
              "and STABLE against a policy written for that station");
    }

    /* ── 4. Tier 1 reads its policy too ───────────────────────────── */
    {
        NsStatsSnapshot s = healthy();
        s.connected    = true;
        s.bytes_per_s  = 1500.0;
        s.frames_ok    = 5000;
        s.arp_valid    = true;
        s.arp_lat      = 52.0;
        s.gnss[0].gnss_id      = 1;
        s.gnss[0].sats_tracked = 38;
        s.gnss[0].cnr_median   = 45.0f;
        s.n_gnss       = 1;

        KpiPolicy strict;
        kpi_policy_defaults(&strict);
        strict.min_cnr_median = 50.0;   /* above what this station gives */

        KpiRun run;
        KpiReport rep;
        kpi_run_start(&run, 0.0, NULL);
        for (int i = 0; i < 40; i++) kpi_update(&run, &s, (double)i * 5.0, &rep);
        check(rep.kpi[5].verdict == KPI_PASS,
              "45 dB-Hz passes the built-in C/N0 floor");

        kpi_run_start(&run, 0.0, &strict);
        for (int i = 0; i < 40; i++) kpi_update(&run, &s, (double)i * 5.0, &rep);
        check(rep.kpi[5].verdict != KPI_PASS,
              "and does not pass a policy that asks for fifty");
        check(rep.kpi[5].limit == 50.0,
              "the check reports the floor it was actually held to");
    }

    /* ── 5. A file is an overlay, not a replacement ───────────────── */
    {
        Thresholds t;
        char err[256];
        thresholds_defaults(&t);

        const char *doc =
            "{ \"schema_version\": 1, \"name\": \"hobby base\","
            "  \"tier1\": { \"min_cnr_median\": 36.0,"
            "               \"expect_sats\": { \"gps\": 6 } },"
            "  \"tier2\": { \"sats_warn\": 20, \"sats_bad\": 10 } }";

        check(thresholds_parse(&t, doc, err, sizeof(err)),
              "a partial policy is accepted");
        check(t.kpi.min_cnr_median == 36.0 && t.sr.sats_warn == 20 &&
              t.sr.sats_bad == 10,
              "what it names is applied");
        check(t.kpi.expect_sats[1] == 6 && t.kpi.expect_sats[5] == 8,
              "expect_sats is per constellation: GPS changed, BeiDou did not");
        check(t.sr.cnr_drop_warn == SR_CNR_DROP_WARN &&
              t.kpi.min_bytes_per_s == KPI_MIN_BYTES_PER_S &&
              t.sr.min_window_s == SR_MIN_WINDOW_S,
              "everything it does not name keeps its default -- a file that "
              "had to restate them would rot as thresholds are added");
        check(strcmp(t.name, "hobby base") == 0 && t.loaded,
              "the policy carries its name, so a verdict can cite it");

        /* Provenance, field by field: without it a screen cannot say
         * which numbers were the user's and which were the build's. */
        int set_count = 0, unset_count = 0;
        for (int i = 0; i < thresholds_field_count(); i++) {
            if (thresholds_is_set(&t, i)) set_count++;
            else                          unset_count++;
        }
        check(set_count == 3 && unset_count == thresholds_field_count() - 3,
              "exactly the three named scalars are marked as coming from "
              "the file");
    }

    /* ── 6. A bad policy changes nothing at all ───────────────────── */
    {
        /* The promise that makes refusal safe: a half-applied standard
         * is one no verdict can be attributed to, so a rejected file
         * must leave the previous policy exactly as it was. */
        Thresholds t, before;
        char err[256];
        thresholds_defaults(&t);
        t.sr.sats_warn = 30;                 /* something to preserve */
        before = t;

        struct { const char *doc; const char *why; } bad[] = {
            { "{ \"tier2\": { \"sats_warn\": 10, \"sats_bad\": 30 } }",
              "a warn level on the wrong side of its bad level" },
            { "{ \"tier2\": { \"min_window_s\": 60 } }",
              "a window below the evidence six metrics need" },
            { "{ \"tier2\": { \"sats_warn\": \"lots\" } }",
              "a value that is not a number" },
            { "{ \"tier1\": { \"min_cnr_median\": -5 } }",
              "a figure outside the range that could describe a stream" },
            { "{ \"schema_version\": 99 }",
              "a schema this build does not understand" },
            { "not json at all",
              "a document that is not JSON" },
        };
        for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
            err[0] = '\0';
            bool ok = thresholds_parse(&t, bad[i].doc, err, sizeof(err));
            check(!ok, bad[i].why);
            check(err[0] != '\0',
                  "  ...and the refusal says which field, not just 'invalid'");
            check(memcmp(&t, &before, sizeof(t)) == 0,
                  "  ...and nothing was applied");
        }
    }

    /* ── 7. The fingerprint distinguishes what the name cannot ────── */
    {
        /* Two people may both call their policy "survey". Only the
         * numbers decide whether their verdicts are comparable. */
        Thresholds a, b;
        char err[256], fa[16], fb[16];

        thresholds_defaults(&a);
        thresholds_defaults(&b);
        thresholds_fingerprint(&a, fa, sizeof(fa));
        thresholds_fingerprint(&b, fb, sizeof(fb));
        check(strcmp(fa, fb) == 0,
              "identical policies fingerprint identically");

        check(thresholds_parse(&b, "{ \"name\": \"survey\" }", err, sizeof(err)),
              "a policy that renames but changes no value is accepted");
        thresholds_fingerprint(&b, fb, sizeof(fb));
        check(strcmp(fa, fb) == 0,
              "and does not change the fingerprint -- the name is not the "
              "standard");

        check(thresholds_parse(&b, "{ \"tier2\": { \"sats_warn\": 26 } }",
                               err, sizeof(err)),
              "a policy that changes one value is accepted");
        thresholds_fingerprint(&b, fb, sizeof(fb));
        check(strcmp(fa, fb) != 0,
              "and does change the fingerprint, so two reports cannot claim "
              "the same standard while using different numbers");
    }

    /* ── 8. The VRS assertions take a policy too ──────────────────── */
    {
        /* Decision 4: the least likely of these thresholds to need
         * changing, and overridable anyway -- "unlikely to be wrong" is
         * not a reason to make something unarguable. */
        Thresholds t;
        char err[256];
        thresholds_defaults(&t);

        VrsPolicy def;
        vrs_policy_defaults(&def);
        check(def.accept_s   == VRS_ACCEPT_S &&
              def.rtcm_s     == VRS_RTCM_S &&
              def.arp_max_km == VRS_ARP_MAX_KM &&
              def.hold_s     == VRS_HOLD_S &&
              def.gate_s     == VRS_GATE_S,
              "the five network-RTK deadlines match their constants");

        check(thresholds_parse(&t,
                  "{ \"vrs\": { \"rtcm_s\": 25.0, \"gate_s\": 180 } }",
                  err, sizeof(err)),
              "a policy may carry a vrs section");
        check(t.vrs.rtcm_s == 25.0 && t.vrs.gate_s == 180.0,
              "and the deadlines it names are applied");
        check(t.vrs.accept_s   == VRS_ACCEPT_S &&
              t.vrs.arp_max_km == VRS_ARP_MAX_KM &&
              t.vrs.hold_s     == VRS_HOLD_S,
              "while the three it does not name keep their defaults");

        /* And the engine reads them rather than merely storing them: a
         * caster silent for twenty seconds fails a ten-second deadline
         * and is still pending against a twenty-five-second one. */
        NsStatsSnapshot s = healthy();
        s.connected = true;
        s.frames_ok = 0;

        VrsRun run;
        VrsReport rep;
        VrsPolicy slow;
        vrs_policy_defaults(&slow);
        slow.rtcm_s = 25.0;

        vrs_run_start(&run, 0.0, NULL);
        vrs_note_gga(&run, &s, 0.0, 52.0, 6.0);
        vrs_update(&run, &s, 20.0, &rep);
        check(rep.a[1].verdict == KPI_FAIL,
              "no corrections after 20 s fails the built-in 10 s deadline");

        vrs_run_start(&run, 0.0, &slow);
        vrs_note_gga(&run, &s, 0.0, 52.0, 6.0);
        vrs_update(&run, &s, 20.0, &rep);
        check(rep.a[1].verdict == KPI_PENDING,
              "and is still pending against a policy that allows 25 s");
    }

    printf("\n%s\n", failures ? "FAILURES" : "all policy cases pass");
    return failures ? 1 : 0;
}
