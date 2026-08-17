/**
 * @file station_report.c
 * @brief Tier 2 -- implementation.
 *
 * Accumulate, then judge.  Every metric here is derived from fields the
 * snapshot already carries, so the tier can be proved before any new
 * measurement is funded.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "core/station_report.h"
#include "core/iono.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void sr_policy_defaults(SrPolicy *p)
{
    if (!p) return;
    memset(p, 0, sizeof(*p));
    p->reconnects_warn_per_h = SR_RECONNECTS_WARN_PER_H;
    p->reconnects_bad_per_h  = SR_RECONNECTS_BAD_PER_H;
    p->integrity_warn_pct    = SR_INTEGRITY_WARN_PCT;
    p->integrity_bad_pct     = SR_INTEGRITY_BAD_PCT;
    p->integrity_window_s    = SR_INTEGRITY_WINDOW_S;
    p->cnr_drop_warn         = SR_CNR_DROP_WARN;
    p->cnr_drop_bad          = SR_CNR_DROP_BAD;
    p->sats_warn             = SR_SATS_WARN;
    p->sats_bad              = SR_SATS_BAD;
    p->roti_warn             = IONO_ROTI_UNSETTLED;
    p->roti_bad              = IONO_ROTI_DISTURBED;
    p->offrate_warn          = SR_OFFRATE_WARN;
    p->offrate_bad           = SR_OFFRATE_BAD;
    p->warmup_s              = SR_WARMUP_S;
    p->min_window_s          = SR_MIN_WINDOW_S;
    p->min_samples           = SR_MIN_SAMPLES;
}

void sr_reset(SrState *s, bool from_capture, const SrPolicy *pol)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->from_capture = from_capture;

    /* A zeroed policy would call every station stable, so an absent one
     * means the built-in thresholds rather than none at all. */
    if (pol) s->pol = *pol;
    else     sr_policy_defaults(&s->pol);
    /* Sentinels: "worst seen" starts at the best possible value, and the
     * minima start impossibly high, so the first sample sets them. */
    s->crc_worst_pct = 100.0;   /* lowered by the first judged window */
    s->cnr_best   = 0.0f;
    s->cnr_last   = 0.0f;
    s->sats_min   = 1 << 30;
    s->roti_worst = -1.0f;
}

void sr_feed(SrState *s, const NsStatsSnapshot *snap, double t_stream)
{
    if (!s || !snap) return;

    /* The warm-up is not evidence. A snapshot taken while the first
     * epoch is still arriving reports a partial constellation, and one
     * such sample is enough to make the window's minimum meaningless --
     * which is exactly what the first live run produced: "fewest held:
     * 9" from a station that never dropped below 39. */
    if (t_stream < s->pol.warmup_s) return;

    if (!s->started) {
        s->started          = true;
        s->t_first          = t_stream;
        s->reconnects_first = snap->reconnects;
    }
    /* Stream time only moves forward.  A replay driven faster than real
     * time still produces the same window, which is the property that
     * makes a captured session reproduce its report. */
    if (t_stream > s->t_last || s->samples == 0) s->t_last = t_stream;
    s->samples++;

    s->reconnects_last = snap->reconnects;

    /* Frame integrity over a window of stream time -- see
     * SR_INTEGRITY_WINDOW_S for why the snapshot's own cumulative rate
     * cannot answer the question this metric asks. */
    {
        uint64_t frames = snap->frames_ok + snap->frames_crc_error;
        uint64_t errors = snap->frames_crc_error;

        /* Counters only climb within a session. If they have gone
         * backwards the caller has handed us a different session, and
         * the honest response is to start again rather than to compute
         * a delta between two unrelated streams. */
        if (!s->crc_have_base ||
            frames < s->crc_base_frames || errors < s->crc_base_errors) {
            s->crc_base_frames = frames;
            s->crc_base_errors = errors;
            s->crc_base_t      = t_stream;
            s->crc_have_base   = true;
        } else if (t_stream - s->crc_base_t >= s->pol.integrity_window_s) {
            uint64_t d_frames = frames - s->crc_base_frames;
            if (d_frames > 0) {
                uint64_t d_errors = errors - s->crc_base_errors;
                double pct = 100.0 * (double)(d_frames - d_errors)
                                   / (double)d_frames;
                if (s->crc_windows == 0 || pct < s->crc_worst_pct)
                    s->crc_worst_pct = pct;
                s->crc_windows++;
            }
            /* A window with no frames at all is a dropout, which
             * availability reports; it is not an integrity reading. */
            s->crc_base_frames = frames;
            s->crc_base_errors = errors;
            s->crc_base_t      = t_stream;
        }
    }

    /* C/N0 of zero means "not measured" -- MSM1-3 carry none -- and must
     * not be mistaken for a silent antenna. */
    if (snap->cnr_mean_all > 0.0f) {
        if (snap->cnr_mean_all > s->cnr_best) s->cnr_best = snap->cnr_mean_all;
        s->cnr_last = snap->cnr_mean_all;
    }

    if (snap->sats_total > 0 && snap->sats_total < s->sats_min)
        s->sats_min = snap->sats_total;

    /* -1 until measurable: a station with no dual-frequency pair has no
     * ROTI, which is not the same as a quiet ionosphere. */
    if (snap->iono_roti_median >= 0.0f && snap->iono_roti_median > s->roti_worst)
        s->roti_worst = snap->iono_roti_median;

    if (snap->types_offrate > 0) s->offrate_samples++;
}

/** @brief Grade a rising quantity against a warn and a bad threshold. */
static int grade_up(double v, double warn, double bad)
{
    if (v >= bad)  return SR_UNSTABLE;
    if (v >= warn) return SR_DEGRADED;
    return SR_STABLE;
}

/** @brief Grade a falling quantity: fewer is worse. */
static int grade_down(double v, double warn, double bad)
{
    if (v <= bad)  return SR_UNSTABLE;
    if (v <= warn) return SR_DEGRADED;
    return SR_STABLE;
}

static void set(SrMetric *m, const char *label, double value, int verdict,
                bool live_only, bool available, const char *fmt, ...)
{
    va_list ap;
    m->label     = label;
    m->value     = value;
    m->verdict   = verdict;
    m->live_only = live_only;
    m->available = available;
    va_start(ap, fmt);
    vsnprintf(m->detail, sizeof(m->detail), fmt, ap);
    va_end(ap);
}

void sr_build(const SrState *s, StationReport *out)
{
    if (!s || !out) return;
    memset(out, 0, sizeof(*out));

    out->window_s     = (s->t_last > s->t_first) ? s->t_last - s->t_first : 0.0;
    out->samples      = s->samples;
    out->from_capture = s->from_capture;

    const bool enough = out->window_s >= s->pol.min_window_s
                     && s->samples    >= s->pol.min_samples;
    const double hours = out->window_s / 3600.0;

    /* ── Availability.  Live-only: a replay never drops, so reporting a
     * clean zero from a capture would be an invention. ── */
    {
        double per_h = (hours > 0.0)
            ? (s->reconnects_last - s->reconnects_first) / hours : 0.0;
        SrMetric *m = &out->metric[SR_AVAILABILITY];
        if (s->from_capture)
            set(m, "Availability", 0.0, SR_INSUFFICIENT, true, false,
                "not available from a capture -- it holds no arrival times");
        else if (!enough)
            set(m, "Availability", per_h, SR_INSUFFICIENT, true, true,
                "%d reconnect(s) so far", s->reconnects_last - s->reconnects_first);
        else
            set(m, "Availability", per_h,
                grade_up(per_h, s->pol.reconnects_warn_per_h,
                         s->pol.reconnects_bad_per_h),
                true, true, "%.1f reconnects per hour", per_h);
    }

    /* ── Frame integrity: the worst rate the window saw, not its mean.
     * A mean hides a bad ten minutes inside a good six hours. ── */
    {
        SrMetric *m = &out->metric[SR_INTEGRITY];
        /* No completed window means no reading -- not a reading of
         * zero, and not one of a hundred per cent either. */
        if (s->crc_windows == 0)
            set(m, "Frame integrity", 100.0, SR_INSUFFICIENT, false, true,
                "first %.0f min of stream not yet complete",
                s->pol.integrity_window_s / 60.0);
        else
            set(m, "Frame integrity", s->crc_worst_pct,
                enough ? grade_down(s->crc_worst_pct, s->pol.integrity_warn_pct,
                                    s->pol.integrity_bad_pct)
                       : SR_INSUFFICIENT,
                false, true, "worst %.3f %% of frames passed CRC in %.0f min",
                s->crc_worst_pct, s->pol.integrity_window_s / 60.0);
    }

    /* ── Signal: how far the mean C/N0 fell from the best this window
     * saw.  An absolute level says more about the site than the station;
     * a fall says the station changed. ── */
    {
        double drop = (s->cnr_best > 0.0f && s->cnr_last > 0.0f)
                    ? (double)(s->cnr_best - s->cnr_last) : 0.0;
        SrMetric *m = &out->metric[SR_SIGNAL];
        /* "This stream carries no C/N0" is a claim about the station,
         * and it may not be made until there is enough evidence to
         * judge anything at all.  Gated on `samples > 0` it was still
         * wrong: the GUI showed it at twenty-five seconds against a
         * station that carries plenty, because the first samples land
         * before the measurement has anything in it.  Ten minutes
         * without a single C/N0 is a property of the stream; twenty-five
         * seconds without one is a property of the clock. */
        if (!enough || s->cnr_best <= 0.0f)
            set(m, "Signal level", drop, SR_INSUFFICIENT, false, true,
                (enough && s->cnr_best <= 0.0f)
                    ? "no C/N0 in this stream (MSM1-3)" : "gathering");
        else
            set(m, "Signal level", drop,
                grade_up(drop, s->pol.cnr_drop_warn, s->pol.cnr_drop_bad),
                false, true,
                "fell %.1f dB-Hz from %.1f", drop, (double)s->cnr_best);
    }

    /* ── Satellites: the fewest held at any moment. ── */
    {
        int fewest = (s->sats_min == (1 << 30)) ? 0 : s->sats_min;
        SrMetric *m = &out->metric[SR_SATELLITES];
        set(m, "Satellites held", fewest,
            (enough && fewest > 0)
                ? grade_down(fewest, s->pol.sats_warn, s->pol.sats_bad)
                : SR_INSUFFICIENT,
            false, true, "fewest held: %d", fewest);
    }

    /* ── Ionosphere: the worst median ROTI, on iono.h's own scale. ── */
    {
        SrMetric *m = &out->metric[SR_IONOSPHERE];
        /* Same rule, and the same defect: ROTI needs phase arcs, which
         * take minutes to form even on a station streaming MSM7 on two
         * frequencies.  Before the window is judgeable this says
         * nothing about the stream. */
        if (!enough || s->roti_worst < 0.0f)
            set(m, "Ionosphere", 0.0, SR_INSUFFICIENT, false, true,
                (enough && s->roti_worst < 0.0f)
                    ? "no dual-frequency pair to measure with" : "gathering");
        else
            set(m, "Ionosphere", s->roti_worst,
                grade_up(s->roti_worst, s->pol.roti_warn, s->pol.roti_bad),
                false, true, "worst median ROTI %.2f TECU/min",
                (double)s->roti_worst);
    }

    /* ── Delivery: how often an advertised type was arriving off-rate. ── */
    {
        double share = (s->samples > 0)
                     ? (double)s->offrate_samples / s->samples : 0.0;
        SrMetric *m = &out->metric[SR_DELIVERY];
        set(m, "Delivery rate", share * 100.0,
            enough ? grade_up(share, s->pol.offrate_warn, s->pol.offrate_bad)
                   : SR_INSUFFICIENT,
            false, true, "off-rate in %.0f %% of samples", share * 100.0);
    }

    /* ── The figure each row was judged against ──────────────────────
     * In the units the row displays, so a screen can put it beside the
     * value and a reader can see for himself what "STABLE" was measured
     * against. Every one of these is a judgement rather than a fact --
     * see docs/thresholds.md -- which is exactly why it should not be
     * invisible. Indexed by name so reordering the enum cannot silently
     * pair a row with someone else's threshold. */
    {
        const struct { double limit; int dir; } lim[SR_METRIC_COUNT] = {
            [SR_AVAILABILITY] = { s->pol.reconnects_warn_per_h, SR_LIMIT_MAX },
            [SR_INTEGRITY]    = { s->pol.integrity_warn_pct,    SR_LIMIT_MIN },
            [SR_SIGNAL]       = { s->pol.cnr_drop_warn,         SR_LIMIT_MAX },
            [SR_SATELLITES]   = { s->pol.sats_warn,             SR_LIMIT_MIN },
            [SR_IONOSPHERE]   = { s->pol.roti_warn,             SR_LIMIT_MAX },
            /* The share is graded as a fraction and shown as a per
             * cent; the limit follows the display. */
            [SR_DELIVERY]     = { s->pol.offrate_warn * 100.0,  SR_LIMIT_MAX },
        };
        for (int i = 0; i < SR_METRIC_COUNT; i++) {
            out->metric[i].limit     = lim[i].limit;
            out->metric[i].limit_dir = lim[i].dir;
        }
    }

    /* ── Roll-up.  Insufficient evidence is a state, not a failure: it
     * says the window is too short to judge, which is true for the first
     * ten minutes of every run. ── */
    int worst = SR_STABLE;
    bool any_insufficient = false;
    const SrMetric *culprit = NULL;

    for (int i = 0; i < SR_METRIC_COUNT; i++) {
        const SrMetric *m = &out->metric[i];
        if (!m->available) continue;              /* live-only, offline */
        if (m->verdict == SR_INSUFFICIENT) { any_insufficient = true; continue; }
        if (m->verdict > worst) { worst = m->verdict; culprit = m; }
    }

    if (!enough) {
        out->overall = SR_INSUFFICIENT;
        snprintf(out->headline, sizeof(out->headline),
                 "INSUFFICIENT EVIDENCE -- %.0f s of %.0f needed, %d sample(s)",
                 out->window_s, s->pol.min_window_s, out->samples);
        return;
    }

    out->overall = worst;
    if (culprit)
        snprintf(out->headline, sizeof(out->headline),
                 "%s over %.1f h -- %s: %s", sr_verdict_name(worst),
                 hours, culprit->label, culprit->detail);
    else
        snprintf(out->headline, sizeof(out->headline),
                 "STABLE over %.1f h%s", hours,
                 any_insufficient ? " (some metrics still gathering)" : "");
}

const char *sr_verdict_name(int verdict)
{
    switch (verdict) {
    case SR_STABLE:   return "STABLE";
    case SR_DEGRADED: return "DEGRADED";
    case SR_UNSTABLE: return "UNSTABLE";
    default:          return "INSUFFICIENT EVIDENCE";
    }
}

int sr_metric_decimals(int metric_id)
{
    switch (metric_id) {
    case SR_SATELLITES:   return 0;   /* a count of satellites */
    case SR_DELIVERY:     return 0;   /* whole percent of samples */
    case SR_SIGNAL:       return 1;   /* dB-Hz, as the detail states  */
    case SR_AVAILABILITY: return 2;   /* reconnects per hour          */
    case SR_IONOSPHERE:   return 2;   /* TECU/min, as iono.h reports  */
    default:              return 3;   /* a CRC rate is small          */
    }
}

const char *sr_metric_unit(int metric_id)
{
    switch (metric_id) {
    case SR_AVAILABILITY: return "/h";        /* reconnections per hour */
    case SR_INTEGRITY:    return "%";         /* frames passing CRC     */
    case SR_SIGNAL:       return "dB-Hz";     /* the fall, not a level  */
    case SR_IONOSPHERE:   return "TECU/min";
    case SR_DELIVERY:     return "%";         /* of samples off-rate    */
    default:              return "";          /* satellites: a count    */
    }
}

const char *sr_metric_limit_text(const SrMetric *m, int metric_id,
                                 char *out, size_t cap)
{
    if (!out || cap == 0) return out;
    out[0] = '\0';
    if (!m || m->limit_dir == SR_LIMIT_NONE) return out;

    const char *unit = sr_metric_unit(metric_id);
    snprintf(out, cap, "%s %.*f%s%s",
             m->limit_dir == SR_LIMIT_MIN ? "min" : "max",
             sr_metric_decimals(metric_id), m->limit,
             *unit ? " " : "", unit);
    return out;
}

const char *sr_metric_key(int metric_id)
{
    switch (metric_id) {
    case SR_AVAILABILITY: return "availability";
    case SR_INTEGRITY:    return "integrity";
    case SR_SIGNAL:       return "signal";
    case SR_SATELLITES:   return "satellites";
    case SR_IONOSPHERE:   return "ionosphere";
    case SR_DELIVERY:     return "delivery";
    default:              return "unknown";
    }
}

/* ── JSON ─────────────────────────────────────────────────────────────
 * The same bounded-append shape as ns_stats.c, for the same reasons: a
 * truncated write still reports the buffer the caller should have
 * supplied, and core writes into that buffer rather than to a file. */

typedef struct {
    char  *buf;
    size_t cap;
    size_t len;      /* bytes that would have been written, less the NUL */
} SrOut;

static void o_ch(SrOut *o, char c)
{
    if (o->cap > 0 && o->len + 1 < o->cap) {
        o->buf[o->len] = c;
        o->buf[o->len + 1] = '\0';
    }
    o->len++;
}

static void o_str(SrOut *o, const char *s)
{
    for (; s && *s; s++) o_ch(o, *s);
}

static void o_fmt(SrOut *o, const char *fmt, ...)
{
    char tmp[192];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    tmp[sizeof(tmp) - 1] = '\0';
    o_str(o, tmp);
}

/**
 * @brief A JSON string literal, escaped.
 *
 * The details are our own text, but a mountpoint is caster-supplied and
 * may hold anything at all.
 */
static void o_jstr(SrOut *o, const char *s)
{
    o_ch(o, '"');
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"':  o_str(o, "\\\""); break;
        case '\\': o_str(o, "\\\\"); break;
        case '\b': o_str(o, "\\b");  break;
        case '\f': o_str(o, "\\f");  break;
        case '\n': o_str(o, "\\n");  break;
        case '\r': o_str(o, "\\r");  break;
        case '\t': o_str(o, "\\t");  break;
        default:
            if (c < 0x20) o_fmt(o, "\\u%04x", c);
            else          o_ch(o, (char)c);
            break;
        }
    }
    o_ch(o, '"');
}

/**
 * @brief Append `,"key":`.
 *
 * The separator lives with the key it precedes rather than after the
 * value before it, so it cannot be forgotten between two fields -- which
 * is precisely how the snapshot serialiser came to emit a document no
 * parser would accept.
 */
static void o_key(SrOut *o, const char *k)
{
    o_str(o, ",\"");
    o_str(o, k);
    o_str(o, "\":");
}

static void o_num(SrOut *o, double v, int decimals)
{
    if (!isfinite(v)) { o_str(o, "null"); return; }
    o_fmt(o, "%.*f", decimals, v);
}

int sr_to_json(const StationReport *r, const char *mountpoint,
               char *out, size_t cap)
{
    if (!r || (!out && cap > 0)) return -1;

    SrOut o;
    o.buf = out;
    o.cap = cap;
    o.len = 0;
    if (cap > 0) out[0] = '\0';

    /* `report_schema_version`, not `schema_version`, and the difference
     * is load-bearing.  The daemon writes reports beside snapshots in
     * one directory, and the Munin plugin finds snapshots by globbing
     * `*.json` and keeping whatever carries a `schema_version`.  A key
     * of that name here would make every report look like a snapshot to
     * a plugin older than it -- a phantom graph family per station, full
     * of undefined values, on any host where the daemon is updated
     * before the plugin.  With this name an old plugin skips the file,
     * and the new one identifies it positively. */
    o_str(&o, "{\"report_schema_version\":");
    o_fmt(&o, "%d", SR_JSON_SCHEMA_VERSION);

    o_key(&o, "mountpoint");   o_jstr(&o, mountpoint ? mountpoint : "");
    o_key(&o, "window_s");     o_num(&o, r->window_s, 3);
    o_key(&o, "samples");      o_fmt(&o, "%d", r->samples);
    o_key(&o, "from_capture"); o_str(&o, r->from_capture ? "true" : "false");
    o_key(&o, "overall");      o_fmt(&o, "%d", r->overall);
    o_key(&o, "overall_name"); o_jstr(&o, sr_verdict_name(r->overall));
    o_key(&o, "headline");     o_jstr(&o, r->headline);

    for (int i = 0; i < SR_METRIC_COUNT; i++) {
        const SrMetric *m = &r->metric[i];
        const char *k = sr_metric_key(i);
        char key[48];

        /* An unavailable metric is null, never 0.  A live-only figure
         * absent from a replay and a figure measured as zero are
         * different statements, and a monitoring graph that cannot tell
         * them apart draws the second when it means the first. */
        snprintf(key, sizeof(key), "%s_verdict", k);
        o_key(&o, key);
        if (m->available) o_fmt(&o, "%d", m->verdict); else o_str(&o, "null");

        snprintf(key, sizeof(key), "%s_value", k);
        o_key(&o, key);
        if (m->available) o_num(&o, m->value, sr_metric_decimals(i));
        else              o_str(&o, "null");

        snprintf(key, sizeof(key), "%s_detail", k);
        o_key(&o, key);
        o_jstr(&o, m->detail);
    }

    o_ch(&o, '}');
    return (int)o.len;
}
