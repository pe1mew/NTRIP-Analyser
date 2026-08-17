/**
 * @file kpi.c
 * @brief Station acceptance test -- implementation.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * @copyright Apache License 2.0 with Commons Clause (see LICENSE for details)
 */

#include "core/kpi.h"
#include <string.h>

void kpi_run_start(KpiRun *run, double now)
{
    if (!run) return;
    memset(run, 0, sizeof(*run));
    run->t_start        = now;
    run->stable_since   = -1.0;
    run->stable_verdict = KPI_RUN_RUNNING;
}

int kpi_value_decimals(int kpi_index)
{
    switch (kpi_index) {
    case 5:  return 1;   /* median C/N0, dB-Hz                         */
    case 6:  return 5;   /* a CRC rate: 0.00430 against a 0.001 limit  */
    default: return 0;   /* bytes/s, frames, satellites, types: counts */
    }
}

const char *kpi_verdict_name(int v)
{
    switch (v) {
    case KPI_PASS: return "PASS";
    case KPI_WARN: return "WARN";
    case KPI_FAIL: return "FAIL";
    default:       return "...";
    }
}

const char *kpi_run_verdict_name(int v)
{
    switch (v) {
    case KPI_RUN_OK:      return "STATION OK";
    case KPI_RUN_CAUTION: return "CAUTION";
    case KPI_RUN_FAILED:  return "FAILED";
    default:              return "RUNNING";
    }
}

/**
 * @brief Message-type ranges that carry observations for a constellation.
 *
 * MSM exists for all seven; the legacy observation messages exist only
 * for GPS and GLONASS, because there never were any others -- which is
 * exactly why a station sending them cannot be asked for Galileo.
 *
 * @param gnss    Core 1-based GNSS id.
 * @param lo, hi  MSM range, out.
 * @param llo, lhi  Legacy range, out; 0 when the constellation has none.
 */
static void obs_ranges(int gnss, int *lo, int *hi, int *llo, int *lhi)
{
    *lo = *hi = *llo = *lhi = 0;
    switch (gnss) {
    case 1: *lo = 1071; *hi = 1077; *llo = 1001; *lhi = 1004; break;  /* GPS */
    case 2: *lo = 1081; *hi = 1087; *llo = 1009; *lhi = 1012; break;  /* GLONASS */
    case 3: *lo = 1091; *hi = 1097; break;                            /* Galileo */
    case 4: *lo = 1111; *hi = 1117; break;                            /* QZSS */
    case 5: *lo = 1121; *hi = 1127; break;                            /* BeiDou */
    case 6: *lo = 1101; *hi = 1107; break;                            /* SBAS */
    case 7: *lo = 1131; *hi = 1137; break;                            /* NavIC */
    default: break;
    }
}

/**
 * @brief Is this constellation's observation stream present, and at rate?
 *
 * Counts legacy and MSM alike: what matters is that observations arrive
 * often enough to position with, not which generation of message
 * carries them.
 *
 * @param at_rate [out] true when something in range meets
 *                @ref KPI_MSM_MAX_DT_S.
 * @return true when any observation message for @p gnss was seen at all.
 */
static bool obs_present(const NsStatsSnapshot *s, int gnss, bool *at_rate)
{
    int lo, hi, llo, lhi;
    obs_ranges(gnss, &lo, &hi, &llo, &lhi);
    bool present = false;
    if (at_rate) *at_rate = false;

    for (int i = 0; i < s->n_types; i++) {
        const NsTypeStats *t = &s->types[i];
        int m = t->msg_type;
        /* Any MSM counts as observations flowing, including MSM1-3:
         * they carry satellites and ranges, only no C/N0. */
        bool in_msm = (lo && m >= lo && m <= hi &&
                       (m % 10) >= 1 && (m % 10) <= 7);
        bool in_leg = (llo && m >= llo && m <= lhi);
        if (!in_msm && !in_leg) continue;

        present = true;
        if (t->epochs < 3) continue;          /* one epoch has no rate yet */
        if (t->avg_dt > 0.0 && t->avg_dt <= KPI_MSM_MAX_DT_S && at_rate)
            *at_rate = true;
    }
    return present;
}

/**
 * @brief Median C/N0 across constellations, satellite-weighted.
 *
 * The snapshot carries a per-constellation median; the single figure
 * used here is those medians weighted by how many satellites each
 * constellation contributes.  Not identical to a true all-SV median,
 * but monotone with it, cheap, and derived only from published snapshot
 * fields -- which keeps this engine buildable against a JSON snapshot
 * too (design-review D3/D4).
 */
static double cnr_median_weighted(const NsStatsSnapshot *s, int *nsats_out)
{
    double acc = 0.0;
    int    n   = 0;
    for (int i = 0; i < s->n_gnss; i++) {
        if (s->gnss[i].cnr_median <= 0.0f) continue;
        acc += (double)s->gnss[i].cnr_median * s->gnss[i].sats_tracked;
        n   += s->gnss[i].sats_tracked;
    }
    if (nsats_out) *nsats_out = n;
    return n ? acc / n : 0.0;
}

void kpi_update(KpiRun *run, const NsStatsSnapshot *s, double now,
                KpiReport *out)
{
    if (!run || !s || !out) return;
    memset(out, 0, sizeof(*out));
    out->elapsed_s = now - run->t_start;

    KpiResult *k = out->kpi;

    /* ── 1: connected and producing ─────────────────────────────────── */
    k[0].label = "Connected and producing";
    k[0].value = s->bytes_per_s;
    if (!s->connected) {
        k[0].verdict = (out->elapsed_s < 10.0) ? KPI_PENDING : KPI_FAIL;
        k[0].detail  = "No connection to the caster";
    } else if (out->elapsed_s < 10.0) {
        k[0].verdict = KPI_PENDING;
        k[0].detail  = "Measuring throughput";
    } else if (s->bytes_per_s >= KPI_MIN_BYTES_PER_S) {
        k[0].verdict = KPI_PASS;
        k[0].detail  = "Authenticated, connected, data flowing";
    } else if (s->bytes_per_s > 0.0) {
        k[0].verdict = KPI_WARN;
        k[0].detail  = "Connected but throughput below 100 B/s";
    } else {
        k[0].verdict = KPI_FAIL;
        k[0].detail  = "Connected but no data arriving";
    }

    /* ── 2: format is RTCM 3.x ──────────────────────────────────────── */
    k[1].label = "RTCM 3.x format";
    k[1].value = (double)s->frames_ok;
    if (s->frames_ok > 0) {
        k[1].verdict = KPI_PASS;
        k[1].detail  = "CRC-valid RTCM 3.x frames decoded";
    } else {
        k[1].verdict = (out->elapsed_s < 10.0) ? KPI_PENDING : KPI_FAIL;
        k[1].detail  = "No valid RTCM 3.x frame yet -- wrong format?";
    }

    /* ── 3: ARP broadcast ───────────────────────────────────────────── */
    k[2].label = "Reference position (ARP)";
    k[2].value = s->arp_valid ? 1.0 : 0.0;
    if (s->arp_valid && (s->arp_lat != 0.0 || s->arp_lon != 0.0)) {
        run->arp_ever = true;
        k[2].verdict  = KPI_PASS;
        k[2].detail   = "1005/1006 received with non-zero coordinates";
    } else if (run->arp_ever) {
        k[2].verdict = KPI_WARN;    /* had one, currently unconfirmed */
        k[2].detail  = "ARP seen earlier this run but not confirmed now";
    } else {
        k[2].verdict = (out->elapsed_s < KPI_ARP_DEADLINE_S)
                       ? KPI_PENDING : KPI_FAIL;
        k[2].detail  = "No RTCM 1005/1006 within the 30 s allowance";
    }

    /* ── 4: observations flowing ────────────────────────────────────
     * Every constellation the station is streaming must stream at rate.
     * Not "GPS and Galileo": the legacy messages cannot express Galileo
     * at all, so demanding it failed an old station for something its
     * format forbids (design/legacy-observations.md).
     *
     * Whether an *advertised* constellation is missing altogether stays
     * KPI 8's question.  One fault should produce one failing verdict,
     * not two. */
    k[3].label = "Observations flowing";
    int streaming = 0, at_rate_n = 0;
    for (int g = 1; g <= 7; g++) {
        bool at_rate = false;
        if (!obs_present(s, g, &at_rate)) continue;
        streaming++;
        if (at_rate) at_rate_n++;
    }
    k[3].value = (double)at_rate_n;
    if (streaming > 0 && at_rate_n == streaming) {
        k[3].verdict = KPI_PASS;
        k[3].detail  = "Every constellation streaming at 0.5 Hz or faster";
    } else if (out->elapsed_s < 15.0) {
        k[3].verdict = KPI_PENDING;
        k[3].detail  = "Waiting for epochs to establish a rate";
    } else if (at_rate_n > 0) {
        k[3].verdict = KPI_WARN;
        k[3].detail  = "Some constellations slower than 0.5 Hz";
    } else {
        k[3].verdict = KPI_FAIL;
        k[3].detail  = "No observations arriving at rate";
    }

    /* ── 5: satellite count ─────────────────────────────────────────
     * Against what this station said it would deliver, not against a
     * flat number: a GPS+GLONASS station cannot reach 25 satellites
     * whatever its health, and failing it for that is failing it for
     * its age.
     *
     * The advertisement is the sourcetable's, as KPI 8 uses; failing
     * that, the constellations actually streaming, so a caster whose
     * sourcetable we could not read never costs the station a verdict. */
    k[4].label = "Satellites in view";
    k[4].value = (double)s->sats_total;
    {
        static const int expect[8] = KPI_EXPECT_SATS;
        int want = 0;
        for (int g = 1; g <= 7; g++) {
            bool judged_advertised = (s->advertised_gnss != 0);
            bool counted = judged_advertised
                           ? ((s->advertised_gnss & (1u << g)) != 0)
                           : obs_present(s, g, NULL);
            if (counted) want += expect[g];
        }
        if (want <= 0) want = KPI_EXPECT_UNKNOWN;

        if (out->elapsed_s < 10.0 && s->sats_total == 0) {
            k[4].verdict = KPI_PENDING;
            k[4].detail  = "Counting";
        } else if (s->sats_total >= want) {
            k[4].verdict = KPI_PASS;
            k[4].detail  = "At or above what this station advertises";
        } else if (s->sats_total >= want / 2) {
            k[4].verdict = KPI_WARN;
            k[4].detail  = "Below expectation -- obstruction or partial tracking?";
        } else {
            k[4].verdict = KPI_FAIL;
            k[4].detail  = "Fewer than half the expected satellites";
        }
    }

    /* ── 6: median C/N0 ─────────────────────────────────────────────── */
    k[5].label = "Median C/N0";
    int cnr_sats = 0;
    double med = cnr_median_weighted(s, &cnr_sats);
    k[5].value = med;
    if (cnr_sats == 0) {
        /* Every MSM that carries C/N0 is now read -- 4, 5, 6 and 7 --
         * so this branch means the stream carries none of them: MSM1-3,
         * which have no C/N0 field at all, or a station still sending
         * only the legacy 1002/1004/1010/1012, whose 8-bit C/N0 is not
         * read yet.  A caution rather than a fail, because it is not a
         * station fault; and worded as our limit rather than the
         * stream's shape, because on a legacy station it is ours. */
        k[5].verdict = (out->elapsed_s < 15.0) ? KPI_PENDING : KPI_WARN;
        k[5].detail  = "Not measured: C/N0 is read from MSM4, 5, 6 and 7";
    } else if (med >= KPI_MIN_CNR_MEDIAN) {
        k[5].verdict = KPI_PASS;
        k[5].detail  = "Antenna and LNA chain healthy";
    } else if (med >= KPI_MIN_CNR_MEDIAN * 0.9) {
        k[5].verdict = KPI_WARN;
        k[5].detail  = "Median C/N0 within 10% below threshold";
    } else {
        k[5].verdict = KPI_FAIL;
        k[5].detail  = "Median C/N0 well below 40 dB-Hz";
    }

    /* ── 7: CRC error rate ──────────────────────────────────────────── */
    k[6].label = "Frame integrity (CRC)";
    k[6].value = s->crc_error_rate;
    uint64_t checked = s->frames_ok + s->frames_crc_error;
    if (checked < 100) {
        k[6].verdict = KPI_PENDING;
        k[6].detail  = "Too few frames to judge a rate";
    } else if (s->crc_error_rate < KPI_MAX_CRC_RATE) {
        k[6].verdict = KPI_PASS;
        k[6].detail  = "Fewer than 1 error per 1000 frames";
    } else if (s->crc_error_rate < KPI_MAX_CRC_RATE * 10.0) {
        k[6].verdict = KPI_WARN;
        k[6].detail  = "Elevated CRC error rate";
    } else {
        k[6].verdict = KPI_FAIL;
        k[6].detail  = "Link is corrupting frames";
    }

    /* ── 8: advertised versus actual ────────────────────────────────── */
    k[7].label = "Advertised versus actual";
    if (!s->advertised_known) {
        /* No sourcetable entry: the promise is unknown, so nothing can
         * be judged.  Deliberately not a pass -- "we could not check"
         * and "we checked and it was fine" are different statements.
         *
         * But it must not stay PENDING for ever either: a run settles
         * only when nothing is pending, so a caster that publishes no
         * usable entry for this mountpoint left the check running with
         * no verdict and no timeout -- observed against a caster whose
         * table was larger than the client parsed.  After the same
         * allowance the other half of this KPI gets, it settles as a
         * caution: one of the eight claims could not be checked, so the
         * station is not certified OK, and the operator is told which
         * check was missing rather than watching a run that never
         * ends. */
        k[7].verdict = out->elapsed_s < 30.0 ? KPI_PENDING : KPI_WARN;
        k[7].detail  = "No sourcetable entry to compare against";
    } else if (out->elapsed_s < 30.0) {
        /* The session applies a per-type grace proportional to each
         * advertised interval; this floor keeps the KPI quiet until
         * even the fastest of them has had several chances. */
        k[7].verdict = KPI_PENDING;
        k[7].detail  = "Waiting for the advertised types to arrive";
    } else if (s->types_missing > 0) {
        /* A promise not kept.  A rover configured from this
         * sourcetable will not receive what it was told to expect, so
         * this fails rather than merely warns. */
        k[7].verdict = KPI_FAIL;
        k[7].value   = (double)s->types_missing;
        k[7].detail  = "Advertised message types are not being sent";
    } else {
        int obs = s->types_offrate + s->types_extra;

        /* Constellations are judged against the sourcetable's NavSys
         * field, which is the actual advertisement.  The 1005/1006
         * indicator bits are not used: they cover GPS, GLONASS and
         * Galileo only, so a station streaming BeiDou cannot declare it
         * there and would be faulted for a gap in the message format.
         *
         * Only the one direction is an observation.  Streaming a system
         * that was never advertised misleads anyone choosing the
         * mountpoint; advertising one that is not currently streamed is
         * ordinary -- QZSS is advertised across Europe and visible from
         * none of it. */
        int undeclared = 0;
        if (s->advertised_gnss) {
            for (int i = 0; i < s->n_gnss; i++) {
                if (s->gnss[i].sats_tracked <= 0) continue;
                unsigned bit = 1u << s->gnss[i].gnss_id;
                if (!(s->advertised_gnss & bit)) undeclared++;
            }
        }
        obs += undeclared;

        k[7].value = (double)obs;
        if (obs == 0) {
            k[7].verdict = KPI_PASS;
            k[7].detail  = "Delivers what the sourcetable advertises";
        } else {
            k[7].verdict = KPI_WARN;
            k[7].detail  = s->types_offrate > 0
                ? "Some types arrive off their advertised rate"
                : (s->types_extra > 0
                   ? "Sending types the sourcetable does not advertise"
                   : "Streaming a constellation the sourcetable omits");
        }
    }

    /* ── Roll-up, per the design's rule ─────────────────────────────── */
    bool all_pass = true, any_pending = false;
    bool hard_fail = false, any_warn = false, soft_fail = false;
    /* KPI 8 is hard on failure: a station that does not send what it
     * advertises breaks any rover configured from that advertisement.
     * Its softer findings are warnings, which the roll-up turns into
     * CAUTION rather than FAILED. */
    static const bool is_hard[KPI_COUNT] =
        { true, true, true, true, false, false, true, true };

    for (int i = 0; i < KPI_COUNT; i++) {
        switch (k[i].verdict) {
        case KPI_PASS: break;
        case KPI_PENDING: any_pending = true; all_pass = false; break;
        case KPI_WARN:    any_warn = true;    all_pass = false; break;
        case KPI_FAIL:
            all_pass = false;
            if (is_hard[i]) hard_fail = true; else soft_fail = true;
            break;
        }
    }

    /* The verdict this instant argues for, before any timing. */
    int candidate;
    if (hard_fail)                  candidate = KPI_RUN_FAILED;
    else if (soft_fail || any_warn) candidate = KPI_RUN_CAUTION;
    else if (all_pass)              candidate = KPI_RUN_OK;
    else                            candidate = KPI_RUN_RUNNING;

    /* Time how long that candidate has held.  A run still gathering
     * evidence does not start the clock; a station alternating between
     * OK and CAUTION restarts it, which is right -- neither verdict has
     * held. */
    if (candidate != run->stable_verdict) {
        run->stable_verdict = candidate;
        run->stable_since   = (candidate == KPI_RUN_RUNNING) ? -1.0 : now;
    }
    out->sustained_s = (run->stable_since >= 0.0) ? now - run->stable_since : 0.0;

    /* A failure is conclusive at once: nothing is learned by watching a
     * station keep failing for another minute.  OK and CAUTION are both
     * claims about steadiness, so both must hold the window. */
    out->settled = (candidate == KPI_RUN_FAILED) ||
                   (candidate != KPI_RUN_RUNNING &&
                    out->sustained_s >= KPI_SUSTAIN_S);

    /* OK is not claimed before it is earned; CAUTION is shown at once,
     * because a warning the user cannot see yet helps nobody. */
    if (candidate == KPI_RUN_OK && !out->settled)
        out->overall = KPI_RUN_RUNNING;
    else
        out->overall = candidate;

    (void)any_pending;
}

/* ── Watch mode ──────────────────────────────────────────────────────── */

void kpi_watch_start(KpiWatch *w, double now)
{
    if (!w) return;
    memset(w, 0, sizeof(*w));
    w->t_start        = now;
    w->last_t         = now;
    w->last_degrade_t = -1.0;
    w->worst          = KPI_RUN_RUNNING;
    w->last_overall   = KPI_RUN_RUNNING;
    w->started        = true;
}

void kpi_watch_update(KpiWatch *w, const KpiReport *rep, double now)
{
    if (!w || !rep || !w->started) return;

    double dt = now - w->last_t;
    if (dt < 0.0) dt = 0.0;          /* a clock that moved backwards */
    w->last_t = now;

    /* Warm-up is not behaviour: record nothing until the run has
     * actually reached a verdict once. */
    if (!w->armed) {
        w->warmup_s += dt;
        if (rep->overall == KPI_RUN_OK || rep->overall == KPI_RUN_FAILED) {
            w->armed        = true;
            w->worst        = rep->overall;
            w->last_overall = rep->overall;
        }
        return;
    }

    /* Attribute the interval to the state that was in force during it. */
    switch (w->last_overall) {
    case KPI_RUN_OK:      w->ok_s      += dt; w->streak_s += dt; break;
    case KPI_RUN_CAUTION: w->caution_s += dt; break;
    case KPI_RUN_FAILED:  w->failed_s  += dt; break;
    default:              w->warmup_s  += dt; break;
    }
    if (w->streak_s > w->best_streak_s) w->best_streak_s = w->streak_s;

    /* Worst is ordered by severity, not by enum value: RUNNING is not
     * worse than OK, it is merely undecided. */
    static const int severity[] = { 0, 1, 2, 3 };   /* RUNNING OK CAUTION FAILED */
    int v = rep->overall;
    if (v >= 0 && v <= KPI_RUN_FAILED &&
        severity[v] > severity[w->worst]) w->worst = v;

    if (w->last_overall == KPI_RUN_OK && v != KPI_RUN_OK) {
        w->degradations++;
        w->last_degrade_t = now - w->t_start;
        w->streak_s = 0.0;
    }
    w->last_overall = v;
}

double kpi_watch_elapsed(const KpiWatch *w, double now)
{
    return (w && w->started) ? now - w->t_start : 0.0;
}

double kpi_watch_availability(const KpiWatch *w)
{
    if (!w) return -1.0;
    double judged = w->ok_s + w->caution_s + w->failed_s;
    return (judged > 0.0) ? w->ok_s / judged : -1.0;
}
