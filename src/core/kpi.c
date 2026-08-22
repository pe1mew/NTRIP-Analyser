/**
 * @file kpi.c
 * @brief Station acceptance test -- implementation.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * @copyright Apache License 2.0 with Commons Clause (see LICENSE for details)
 */

#include "core/kpi.h"
#include "core/ns_failure.h"
#include <stdio.h>     /* snprintf, into caller buffers only -- no I/O */
#include <string.h>

void kpi_policy_defaults(KpiPolicy *p)
{
    if (!p) return;
    memset(p, 0, sizeof(*p));

    static const int expect[8] = KPI_EXPECT_SATS;
    for (int i = 0; i < 8; i++) p->expect_sats[i] = expect[i];

    p->sustain_s          = KPI_SUSTAIN_S;
    p->min_bytes_per_s    = KPI_MIN_BYTES_PER_S;
    p->arp_deadline_s     = KPI_ARP_DEADLINE_S;
    p->msm_max_dt_s       = KPI_MSM_MAX_DT_S;
    p->expect_unknown     = KPI_EXPECT_UNKNOWN;
    p->min_cnr_median     = KPI_MIN_CNR_MEDIAN;
    p->min_integrity_pct  = KPI_MIN_INTEGRITY_PCT;
    p->bad_integrity_pct  = KPI_BAD_INTEGRITY_PCT;
    p->integrity_window_s = KPI_INTEGRITY_WINDOW_S;
}

void kpi_run_start(KpiRun *run, double now, const KpiPolicy *pol)
{
    if (!run) return;
    memset(run, 0, sizeof(*run));
    run->t_start        = now;
    run->stable_since   = -1.0;
    run->stable_verdict = KPI_RUN_RUNNING;

    /* A zeroed policy would pass everything, so an absent one means the
     * built-in thresholds rather than none at all. */
    if (pol) run->pol = *pol;
    else     kpi_policy_defaults(&run->pol);
}

int kpi_value_decimals(int kpi_index)
{
    switch (kpi_index) {
    case 5:  return 1;   /* median C/N0, dB-Hz                         */
    case 6:  return 3;   /* integrity: 99.743 % against a 99.9 % limit */
    default: return 0;   /* bytes/s, frames, satellites, types: counts */
    }
}

const char *kpi_value_unit(int kpi_index)
{
    switch (kpi_index) {
    case 0:  return "B/s";     /* throughput                            */
    case 5:  return "dB-Hz";   /* median C/N0                           */
    case 6:  return "%";       /* frames passing CRC                    */
    default: return "";        /* frames, satellites, types: counts     */
    }
}

const char *kpi_limit_text(const KpiResult *k, int kpi_index,
                           char *out, size_t cap)
{
    if (!out || cap == 0) return out;
    out[0] = '\0';
    if (!k || k->limit_dir == KPI_LIMIT_NONE) return out;

    const char *unit = kpi_value_unit(kpi_index);
    snprintf(out, cap, "%s %.*f%s%s",
             k->limit_dir == KPI_LIMIT_MIN ? "min" : "max",
             kpi_value_decimals(kpi_index), k->limit,
             *unit ? " " : "", unit);
    return out;
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
 * @param max_dt  Slowest acceptable epoch interval, from the run's
 *                policy rather than the macro: this is one of the
 *                figures a user may disagree with.
 * @param at_rate [out] true when something in range meets @p max_dt.
 * @return true when any observation message for @p gnss was seen at all.
 */
static bool obs_present(const NsStatsSnapshot *s, int gnss, double max_dt,
                        bool *at_rate)
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
        if (t->avg_dt > 0.0 && t->avg_dt <= max_dt && at_rate)
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

/**
 * @brief Write one of KPI 1's numbered explanations into the run.
 *
 * A `%.0f` will happily write three hundred digits for a nonsense
 * double, so the seconds become a clamped whole number first: the
 * sentence is then at most six digits wider than its format, which is a
 * width the compiler can check rather than one a reader has to trust.
 * @p fmt therefore takes `%ld`, not `%f`.
 *
 * @return the run's buffer, so it can be assigned inline.
 */
static const char *detail_secs(KpiRun *run, const char *fmt, double secs)
{
    long n = 0;                                   /* !(>0) catches NaN */
    if (secs > 0.0) n = (secs > 999999.0) ? 999999L : (long)secs;
    snprintf(run->detail1, sizeof run->detail1, fmt, n);
    return run->detail1;
}

void kpi_update(KpiRun *run, const NsStatsSnapshot *s, double now,
                KpiReport *out)
{
    if (!run || !s || !out) return;
    memset(out, 0, sizeof(*out));
    out->elapsed_s = now - run->t_start;

    KpiResult *k = out->kpi;

    /* When data was last seen, in the session's own clock.  A stream
     * that delivered and then stopped and a stream that never delivered
     * are the same instant on the throughput meter and entirely
     * different findings, and only this tells them apart. */
    if (s->bytes_total > run->bytes_seen) {
        run->bytes_seen = s->bytes_total;
        run->bytes_up_s = s->uptime_s;
    }

    /* ── 1: connected and producing ─────────────────────────────────── */
    k[0].label = "Connected and producing";
    k[0].value = s->bytes_per_s;
    k[0].limit = run->pol.min_bytes_per_s;
    k[0].limit_dir = KPI_LIMIT_MIN;
    if (!s->connected) {
        k[0].verdict = (out->elapsed_s < 10.0) ? KPI_PENDING : KPI_FAIL;
        /* Which refusal it was, when the session knows (3.7.0). "No
         * connection to the caster" is true of a wrong host, a wrong
         * port, a wrong password and a mountpoint that does not exist
         * -- four different things to go and fix, behind one sentence
         * that named none of them.  The verdict is unchanged: this
         * says *why* it failed, not *whether* it did, so every exit
         * code and every consumer of the verdict vocabulary is
         * untouched.
         *
         * The pointer is into the snapshot, which outlives this report
         * for the same reason the label strings do: both are read by
         * the caller before it pumps again. */
        k[0].detail  = s->bytes_total
            ? detail_secs(run, "Connection lost after %ld s of data",
                          run->bytes_up_s)
            : (s->failure != NS_FAIL_NONE
                   ? ns_failure_short((NsFailure)s->failure)
                   : "No connection to the caster");
    } else if (out->elapsed_s < 10.0) {
        k[0].verdict = KPI_PENDING;
        k[0].detail  = "Measuring throughput";
    } else if (s->bytes_per_s >= run->pol.min_bytes_per_s) {
        k[0].verdict = KPI_PASS;
        k[0].detail  = "Authenticated, connected, data flowing";
    } else if (s->bytes_per_s > 0.0) {
        k[0].verdict = KPI_WARN;
        k[0].detail  = "Connected, but throughput is below the minimum";
    } else if (s->bytes_total) {
        /* The verdict stands -- a session that stops mid-check has
         * failed the check -- but the explanation must not read as
         * though nothing was ever delivered, which contradicts KPI 2
         * counting the frames that were. */
        k[0].verdict = KPI_FAIL;
        k[0].detail  = detail_secs(run,
            "Data arrived for %ld s, then the stream stopped",
            run->bytes_up_s);
    } else {
        k[0].verdict = KPI_FAIL;
        k[0].detail  = "Connected, but the caster has sent nothing";
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
        k[2].verdict = (out->elapsed_s < run->pol.arp_deadline_s)
                       ? KPI_PENDING : KPI_FAIL;
        k[2].detail  = "No RTCM 1005/1006 within the allowance";
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
        if (!obs_present(s, g, run->pol.msm_max_dt_s, &at_rate)) continue;
        streaming++;
        if (at_rate) at_rate_n++;
    }
    k[3].value = (double)at_rate_n;
    if (streaming > 0 && at_rate_n == streaming) {
        k[3].verdict = KPI_PASS;
        k[3].detail  = "Every constellation streaming at or above the minimum rate";
    } else if (out->elapsed_s < 15.0) {
        k[3].verdict = KPI_PENDING;
        k[3].detail  = "Waiting for epochs to establish a rate";
    } else if (at_rate_n > 0) {
        k[3].verdict = KPI_WARN;
        k[3].detail  = "Some constellations slower than the minimum rate";
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
        const int *expect = run->pol.expect_sats;
        int want = 0;
        for (int g = 1; g <= 7; g++) {
            bool judged_advertised = (s->advertised_gnss != 0);
            bool counted = judged_advertised
                           ? ((s->advertised_gnss & (1u << g)) != 0)
                           : obs_present(s, g, run->pol.msm_max_dt_s, NULL);
            if (counted) want += expect[g];
        }
        if (want <= 0) want = run->pol.expect_unknown;

        /* The number this station was actually held to, which differs
         * with the constellations it streams -- a GPS+GLONASS base is
         * not asked for what a five-system one delivers. Showing the
         * flat table value instead would misstate the test. */
        k[4].limit     = (double)want;
        k[4].limit_dir = KPI_LIMIT_MIN;

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
    k[5].limit = run->pol.min_cnr_median;
    k[5].limit_dir = KPI_LIMIT_MIN;
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
    } else if (med >= run->pol.min_cnr_median) {
        k[5].verdict = KPI_PASS;
        k[5].detail  = "Antenna and LNA chain healthy";
    } else if (med >= run->pol.min_cnr_median * 0.9) {
        k[5].verdict = KPI_WARN;
        k[5].detail  = "Median C/N0 just below the floor";
    } else {
        k[5].verdict = KPI_FAIL;
        k[5].detail  = "Median C/N0 well below the floor";
    }

    /* ── 7: frame integrity ─────────────────────────────────────────
     *
     * The share of frames passing CRC over the last KPI_SUSTAIN_S of
     * stream, not over the session. The session figure dilutes: a burst
     * in the first seconds is washed out by every clean second after
     * it, and one late in a long run is washed out by everything
     * before. Either way the verdict stops being able to move, which is
     * what the sustain clock is meant to be timing. */
    k[6].label = "Frame integrity (CRC)";
    {
        uint64_t frames = s->frames_ok + s->frames_crc_error;
        uint64_t errors = s->frames_crc_error;

        if (!run->crc_have_base ||
            frames < run->crc_base_frames || errors < run->crc_base_errors) {
            run->crc_base_frames = frames;
            run->crc_base_errors = errors;
            run->crc_base_t      = now;
            run->crc_have_base   = true;
        } else if (now - run->crc_base_t >= run->pol.integrity_window_s) {
            uint64_t d_frames = frames - run->crc_base_frames;
            if (d_frames >= 100) {      /* a rate needs a denominator */
                uint64_t d_errors = errors - run->crc_base_errors;
                run->crc_pct = 100.0 * (double)(d_frames - d_errors)
                                     / (double)d_frames;
                run->crc_have_pct = true;
                run->crc_base_frames = frames;
                run->crc_base_errors = errors;
                run->crc_base_t      = now;
            }
            /* Too few frames in a whole window: leave the base alone so
             * the next one covers longer, rather than judging a rate on
             * a handful of frames. */
        }

        /* The details describe the same side of the measurement the
         * value does -- frames *passing* -- so a reader is not asked to
         * invert one against the other. The field's own term for the
         * complement is "CRC error rate"; it is not BER, and not FER,
         * which names frames that never arrived rather than frames that
         * arrived broken. */
        k[6].value     = run->crc_have_pct ? run->crc_pct : 100.0;
        k[6].limit     = run->pol.min_integrity_pct;
        k[6].limit_dir = KPI_LIMIT_MIN;
        if (!run->crc_have_pct) {
            k[6].verdict = KPI_PENDING;
            k[6].detail  = "Too few frames to judge integrity yet";
        } else if (run->crc_pct >= run->pol.min_integrity_pct) {
            k[6].verdict = KPI_PASS;
            k[6].detail  = "Frames passing CRC at or above the minimum";
        } else if (run->crc_pct >= run->pol.bad_integrity_pct) {
            k[6].verdict = KPI_WARN;
            k[6].detail  = "Below the minimum share passing: elevated CRC errors";
        } else {
            k[6].verdict = KPI_FAIL;
            k[6].detail  = "Far below the minimum: the link is corrupting frames";
        }
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
                    out->sustained_s >= run->pol.sustain_s);

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
