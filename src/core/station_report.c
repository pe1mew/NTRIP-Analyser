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

void sr_reset(SrState *s, bool from_capture)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->from_capture = from_capture;
    /* Sentinels: "worst seen" starts at the best possible value, and the
     * minima start impossibly high, so the first sample sets them. */
    s->crc_worst  = 0.0;
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
    if (t_stream < SR_WARMUP_S) return;

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

    if (snap->crc_error_rate > s->crc_worst) s->crc_worst = snap->crc_error_rate;

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

    const bool enough = out->window_s >= SR_MIN_WINDOW_S
                     && s->samples    >= SR_MIN_SAMPLES;
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
                grade_up(per_h, SR_RECONNECTS_WARN_PER_H, SR_RECONNECTS_BAD_PER_H),
                true, true, "%.1f reconnects per hour", per_h);
    }

    /* ── Frame integrity: the worst rate the window saw, not its mean.
     * A mean hides a bad ten minutes inside a good six hours. ── */
    {
        SrMetric *m = &out->metric[SR_INTEGRITY];
        set(m, "Frame integrity", s->crc_worst * 100.0,
            enough ? grade_up(s->crc_worst, SR_CRC_WARN, SR_CRC_BAD)
                   : SR_INSUFFICIENT,
            false, true, "worst CRC error rate %.3f %%", s->crc_worst * 100.0);
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
                grade_up(drop, SR_CNR_DROP_WARN, SR_CNR_DROP_BAD), false, true,
                "fell %.1f dB-Hz from %.1f", drop, (double)s->cnr_best);
    }

    /* ── Satellites: the fewest held at any moment. ── */
    {
        int fewest = (s->sats_min == (1 << 30)) ? 0 : s->sats_min;
        SrMetric *m = &out->metric[SR_SATELLITES];
        set(m, "Satellites held", fewest,
            (enough && fewest > 0)
                ? grade_down(fewest, SR_SATS_WARN, SR_SATS_BAD)
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
                grade_up(s->roti_worst, IONO_ROTI_UNSETTLED, IONO_ROTI_DISTURBED),
                false, true, "worst median ROTI %.2f TECU/min",
                (double)s->roti_worst);
    }

    /* ── Delivery: how often an advertised type was arriving off-rate. ── */
    {
        double share = (s->samples > 0)
                     ? (double)s->offrate_samples / s->samples : 0.0;
        SrMetric *m = &out->metric[SR_DELIVERY];
        set(m, "Delivery rate", share * 100.0,
            enough ? grade_up(share, SR_OFFRATE_WARN, SR_OFFRATE_BAD)
                   : SR_INSUFFICIENT,
            false, true, "off-rate in %.0f %% of samples", share * 100.0);
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
                 out->window_s, SR_MIN_WINDOW_S, out->samples);
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
        if (m->available) o_num(&o, m->value, 3); else o_str(&o, "null");

        snprintf(key, sizeof(key), "%s_detail", k);
        o_key(&o, key);
        o_jstr(&o, m->detail);
    }

    o_ch(&o, '}');
    return (int)o.len;
}
