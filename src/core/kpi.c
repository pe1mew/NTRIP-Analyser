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

/** @brief Does types[] carry an MSM of @p gnss at KPI_MSM_MAX_DT_S or faster? */
static bool msm_flowing(const NsStatsSnapshot *s, int lo, int hi, double *dt_out)
{
    for (int i = 0; i < s->n_types; i++) {
        const NsTypeStats *t = &s->types[i];
        if (t->msg_type < lo || t->msg_type > hi) continue;
        if ((t->msg_type % 10) < 4 || (t->msg_type % 10) > 7) continue;
        if (t->epochs < 3) continue;          /* one epoch has no rate yet */
        if (t->avg_dt > 0.0 && t->avg_dt <= KPI_MSM_MAX_DT_S) {
            if (dt_out) *dt_out = t->avg_dt;
            return true;
        }
    }
    return false;
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

    /* ── 4: multi-GNSS MSM flowing ──────────────────────────────────── */
    k[3].label = "Multi-GNSS observations";
    double dt_gps = 0.0, dt_gal = 0.0;
    bool gps = msm_flowing(s, 1070, 1079, &dt_gps);
    bool gal = msm_flowing(s, 1090, 1099, &dt_gal);
    k[3].value = (gps ? 1.0 : 0.0) + (gal ? 2.0 : 0.0);
    if (gps && gal) {
        k[3].verdict = KPI_PASS;
        k[3].detail  = "GPS and Galileo MSM at 0.5 Hz or faster";
    } else if (out->elapsed_s < 15.0) {
        k[3].verdict = KPI_PENDING;
        k[3].detail  = "Waiting for MSM epochs to establish a rate";
    } else if (gps || gal) {
        k[3].verdict = KPI_WARN;
        k[3].detail  = gps ? "GPS flowing, Galileo missing or slow"
                           : "Galileo flowing, GPS missing or slow";
    } else {
        k[3].verdict = KPI_FAIL;
        k[3].detail  = "Neither GPS nor Galileo MSM at rate";
    }

    /* ── 5: satellite count ─────────────────────────────────────────── */
    k[4].label = "Satellites in view";
    k[4].value = (double)s->sats_total;
    if (out->elapsed_s < 10.0 && s->sats_total == 0) {
        k[4].verdict = KPI_PENDING;
        k[4].detail  = "Counting";
    } else if (s->sats_total >= KPI_MIN_SATS) {
        k[4].verdict = KPI_PASS;
        k[4].detail  = "At or above the 25-SV threshold";
    } else if (s->sats_total >= KPI_MIN_SATS / 2) {
        k[4].verdict = KPI_WARN;
        k[4].detail  = "Below 25 SVs -- obstruction or partial tracking?";
    } else {
        k[4].verdict = KPI_FAIL;
        k[4].detail  = "Fewer than half the expected satellites";
    }

    /* ── 6: median C/N0 ─────────────────────────────────────────────── */
    k[5].label = "Median C/N0";
    int cnr_sats = 0;
    double med = cnr_median_weighted(s, &cnr_sats);
    k[5].value = med;
    if (cnr_sats == 0) {
        /* MSM4/5/6 carry no C/N0.  That is the stream's shape, not a
         * station fault, so it reads as a caution rather than a fail. */
        k[5].verdict = (out->elapsed_s < 15.0) ? KPI_PENDING : KPI_WARN;
        k[5].detail  = "No C/N0 in this stream (MSM7 required)";
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
    (void)dt_gps; (void)dt_gal;
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
