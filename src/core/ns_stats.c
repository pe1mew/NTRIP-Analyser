/**
 * @file ns_stats.c
 * @brief Statistics snapshot: construction and serialisation.
 *
 * Layer rules (design/architecture.md §2.2): no I/O, no printf, no
 * platform headers.  Serialisers write into a caller-supplied buffer and
 * report the space they needed, in the manner of snprintf.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "core/ns_stats.h"

#include <stdio.h>      /* vsnprintf into caller buffers only -- no I/O */
#include <stdarg.h>
#include <string.h>
#include <math.h>

/* ── Bounded output helper ────────────────────────────────────────────
 * Tracks the total length the output *would* have, so a truncated write
 * still reports the buffer size the caller should have supplied.  Once
 * the buffer is full it keeps counting but stops writing, which is what
 * makes the snprintf-style return contract work. */
typedef struct {
    char  *buf;
    size_t cap;
    size_t len;   /* bytes that would have been written, excluding NUL */
} NsOut;

static void out_init(NsOut *o, char *buf, size_t cap)
{
    o->buf = buf;
    o->cap = cap;
    o->len = 0;
    if (cap > 0) buf[0] = '\0';
}

static void out_ch(NsOut *o, char c)
{
    if (o->cap > 0 && o->len + 1 < o->cap) {
        o->buf[o->len] = c;
        o->buf[o->len + 1] = '\0';
    }
    o->len++;
}

static void out_str(NsOut *o, const char *s)
{
    for (; *s; s++) out_ch(o, *s);
}

static void out_fmt(NsOut *o, const char *fmt, ...)
{
    /* Format into a scratch buffer, then append through out_str so the
     * overflow accounting stays in one place.  256 is comfortably above
     * anything emitted here (a double plus a key name). */
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    tmp[sizeof(tmp) - 1] = '\0';
    out_str(o, tmp);
}

/**
 * @brief Append a JSON string literal, escaped.
 *
 * Escapes the two characters that would otherwise terminate or continue
 * the literal, plus the control characters below 0x20 that RFC 8259
 * forbids raw.  A caster's Server: header is arbitrary text from the
 * network, so this is not theoretical.
 */
static void out_json_str(NsOut *o, const char *s)
{
    out_ch(o, '"');
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"':  out_str(o, "\\\""); break;
        case '\\': out_str(o, "\\\\"); break;
        case '\b': out_str(o, "\\b");  break;
        case '\f': out_str(o, "\\f");  break;
        case '\n': out_str(o, "\\n");  break;
        case '\r': out_str(o, "\\r");  break;
        case '\t': out_str(o, "\\t");  break;
        default:
            if (c < 0x20) out_fmt(o, "\\u%04x", c);
            else          out_ch(o, (char)c);
            break;
        }
    }
    out_ch(o, '"');
}

/**
 * @brief Append a double as a JSON number, or `null` if not finite.
 *
 * JSON has no NaN or Infinity; emitting either produces a document that
 * strict parsers reject, which would break the Munin plugin at exactly
 * the moment something has gone wrong upstream.  NS_UNSET is likewise
 * reported as null so "not measured" is distinguishable from zero.
 */
static void out_json_num(NsOut *o, double v, int decimals)
{
    if (!isfinite(v) || v == NS_UNSET) { out_str(o, "null"); return; }
    out_fmt(o, "%.*f", decimals, v);
}

static void out_json_u64(NsOut *o, uint64_t v)
{
    out_fmt(o, "%llu", (unsigned long long)v);
}

static void out_key(NsOut *o, const char *k)
{
    out_json_str(o, k);
    out_ch(o, ':');
}

/* ── Public API ───────────────────────────────────────────────────── */

void ns_stats_init(NsStatsSnapshot *s)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->schema_version = NS_STATS_SCHEMA_VERSION;

    /* Distinguish "not measured" from "measured as zero".  Drift of 0 m
     * and no ARP at all are very different statements. */
    s->arp_drift_m           = NS_UNSET;
    s->sourcetable_offset_m  = NS_UNSET;
    s->latency_s             = NS_UNSET;
}

NsTypeStats *ns_stats_type(NsStatsSnapshot *s, int msg_type)
{
    if (!s) return NULL;
    for (int i = 0; i < s->n_types; i++)
        if (s->types[i].msg_type == msg_type) return &s->types[i];

    if (s->n_types >= NS_MAX_TYPES) {
        s->types_truncated = true;
        return NULL;
    }
    NsTypeStats *t = &s->types[s->n_types++];
    memset(t, 0, sizeof(*t));
    t->msg_type = msg_type;
    return t;
}

NsGnssStats *ns_stats_gnss(NsStatsSnapshot *s, int gnss_id)
{
    if (!s) return NULL;
    for (int i = 0; i < s->n_gnss; i++)
        if (s->gnss[i].gnss_id == gnss_id) return &s->gnss[i];

    if (s->n_gnss >= NS_MAX_GNSS) return NULL;
    NsGnssStats *g = &s->gnss[s->n_gnss++];
    memset(g, 0, sizeof(*g));
    g->gnss_id = gnss_id;
    return g;
}

const char *ns_verdict_name(int verdict)
{
    switch (verdict) {
    case NS_VERDICT_OK:      return "ok";
    case NS_VERDICT_MISSING: return "missing";
    case NS_VERDICT_RATE:    return "rate";
    case NS_VERDICT_EXTRA:   return "extra";
    default:                 return "unknown";
    }
}

const char *ns_station_type_name(int station_type)
{
    switch (station_type) {
    case NS_STATION_FIXED: return "fixed";
    case NS_STATION_VRS:   return "vrs";
    default:               return "unknown";
    }
}

int ns_stats_to_json(const NsStatsSnapshot *s, char *out, size_t cap)
{
    if (!s || (!out && cap > 0)) return -1;

    NsOut o;
    out_init(&o, out, cap);

    out_ch(&o, '{');

    out_key(&o, "schema_version"); out_fmt(&o, "%d", s->schema_version);

    out_str(&o, ",");
    out_key(&o, "mountpoint"); out_json_str(&o, s->mountpoint);
    out_str(&o, ",");
    out_key(&o, "caster"); out_json_str(&o, s->caster);
    out_str(&o, ",");
    out_key(&o, "t_start_unix"); out_json_num(&o, s->t_start_unix, 3);
    out_str(&o, ",");
    out_key(&o, "uptime_s"); out_json_num(&o, s->uptime_s, 3);

    /* Connection */
    out_str(&o, ",");
    out_key(&o, "connected"); out_str(&o, s->connected ? "true" : "false");
    out_str(&o, ",");
    out_key(&o, "ntrip_version"); out_fmt(&o, "%d", s->ntrip_version);
    out_str(&o, ",");
    out_key(&o, "http_status"); out_fmt(&o, "%d", s->http_status);
    out_str(&o, ",");
    out_key(&o, "caster_software"); out_json_str(&o, s->caster_software);
    out_str(&o, ",");
    out_key(&o, "reconnects"); out_fmt(&o, "%d", s->reconnects);

    /* Volume and integrity */
    out_str(&o, ",");
    out_key(&o, "bytes_total"); out_json_u64(&o, s->bytes_total);
    out_str(&o, ",");
    out_key(&o, "bytes_per_s"); out_json_num(&o, s->bytes_per_s, 1);
    out_str(&o, ",");
    out_key(&o, "frames_ok"); out_json_u64(&o, s->frames_ok);
    out_str(&o, ",");
    out_key(&o, "frames_crc_error"); out_json_u64(&o, s->frames_crc_error);
    out_str(&o, ",");
    out_key(&o, "frames_malformed"); out_json_u64(&o, s->frames_malformed);
    out_str(&o, ",");
    out_key(&o, "framing_resyncs"); out_json_u64(&o, s->framing_resyncs);
    out_str(&o, ",");
    out_key(&o, "crc_error_rate"); out_json_num(&o, s->crc_error_rate, 6);

    /* Advertised-versus-observed roll-up */
    out_str(&o, ",");
    out_key(&o, "advertised_known");
    out_str(&o, s->advertised_known ? "true" : "false");
    out_str(&o, ",");
    out_key(&o, "advertised_count"); out_fmt(&o, "%d", s->advertised_count);
    out_str(&o, ",");
    out_key(&o, "types_missing"); out_fmt(&o, "%d", s->types_missing);
    out_str(&o, ",");
    out_key(&o, "types_offrate"); out_fmt(&o, "%d", s->types_offrate);
    out_str(&o, ",");
    out_key(&o, "types_extra"); out_fmt(&o, "%d", s->types_extra);
    out_str(&o, ",");
    out_key(&o, "types_truncated");
    out_str(&o, s->types_truncated ? "true" : "false");

    /* Per message type */
    out_str(&o, ",");
    out_key(&o, "types");
    out_ch(&o, '[');
    for (int i = 0; i < s->n_types; i++) {
        const NsTypeStats *t = &s->types[i];
        if (i) out_ch(&o, ',');
        out_ch(&o, '{');
        out_key(&o, "type");   out_fmt(&o, "%d", t->msg_type);
        out_str(&o, ",");
        out_key(&o, "frames"); out_json_u64(&o, t->frames);
        out_str(&o, ",");
        out_key(&o, "epochs"); out_json_u64(&o, t->epochs);
        out_str(&o, ",");
        out_key(&o, "min_dt"); out_json_num(&o, t->min_dt, 3);
        out_str(&o, ",");
        out_key(&o, "max_dt"); out_json_num(&o, t->max_dt, 3);
        out_str(&o, ",");
        out_key(&o, "avg_dt"); out_json_num(&o, t->avg_dt, 3);
        out_str(&o, ",");
        out_key(&o, "advertised_interval");
        out_json_num(&o, t->advertised_interval, 3);
        out_str(&o, ",");
        out_key(&o, "verdict"); out_json_str(&o, ns_verdict_name(t->verdict));
        out_ch(&o, '}');
    }
    out_ch(&o, ']');

    /* Satellites */
    out_str(&o, ",");
    out_key(&o, "sats_total"); out_fmt(&o, "%d", s->sats_total);
    out_str(&o, ",");
    out_key(&o, "cnr_mean_all"); out_json_num(&o, s->cnr_mean_all, 2);
    out_str(&o, ",");
    out_key(&o, "gnss");
    out_ch(&o, '[');
    for (int i = 0; i < s->n_gnss; i++) {
        const NsGnssStats *g = &s->gnss[i];
        if (i) out_ch(&o, ',');
        out_ch(&o, '{');
        out_key(&o, "gnss_id");      out_fmt(&o, "%d", g->gnss_id);
        out_str(&o, ",");
        out_key(&o, "sats_tracked"); out_fmt(&o, "%d", g->sats_tracked);
        out_str(&o, ",");
        out_key(&o, "cnr_mean");     out_json_num(&o, g->cnr_mean, 2);
        out_str(&o, ",");
        out_key(&o, "cnr_median");   out_json_num(&o, g->cnr_median, 2);
        out_str(&o, ",");
        out_key(&o, "cnr_min");      out_json_num(&o, g->cnr_min, 2);
        out_str(&o, ",");
        out_key(&o, "cnr_max");      out_json_num(&o, g->cnr_max, 2);
        out_ch(&o, '}');
    }
    out_ch(&o, ']');

    /* Reference station */
    out_str(&o, ",");
    out_key(&o, "station_type");
    out_json_str(&o, ns_station_type_name(s->station_type));
    out_str(&o, ",");
    out_key(&o, "arp_valid"); out_str(&o, s->arp_valid ? "true" : "false");
    out_str(&o, ",");
    out_key(&o, "arp_lat"); out_json_num(&o, s->arp_valid ? s->arp_lat : NS_UNSET, 8);
    out_str(&o, ",");
    out_key(&o, "arp_lon"); out_json_num(&o, s->arp_valid ? s->arp_lon : NS_UNSET, 8);
    out_str(&o, ",");
    out_key(&o, "arp_alt"); out_json_num(&o, s->arp_valid ? s->arp_alt : NS_UNSET, 3);
    out_str(&o, ",");
    out_key(&o, "arp_drift_m"); out_json_num(&o, s->arp_drift_m, 3);
    out_str(&o, ",");
    out_key(&o, "arp_moves"); out_fmt(&o, "%d", s->arp_moves);
    out_str(&o, ",");
    out_key(&o, "sourcetable_offset_m");
    out_json_num(&o, s->sourcetable_offset_m, 1);

    /* Timeliness */
    out_str(&o, ",");
    out_key(&o, "latency_s"); out_json_num(&o, s->latency_s, 3);

    /* Ionosphere.  -1 means "not measurable yet"; serialised as null so
     * a consumer cannot mistake it for a real (and impossible) rate. */
    out_str(&o, ",");
    out_key(&o, "iono_verdict"); out_fmt(&o, "%d", s->iono_verdict);
    out_str(&o, ",");
    out_key(&o, "iono_roti_median");
    out_json_num(&o, s->iono_roti_median >= 0.0f ? s->iono_roti_median
                                                 : NS_UNSET, 3);
    out_str(&o, ",");
    out_key(&o, "iono_roti_max");
    out_json_num(&o, s->iono_roti_max >= 0.0f ? s->iono_roti_max
                                              : NS_UNSET, 3);
    out_str(&o, ",");
    out_key(&o, "iono_sats_dualfreq");
    out_fmt(&o, "%d", s->iono_sats_dualfreq);
    out_str(&o, ",");
    out_key(&o, "iono_slips"); out_fmt(&o, "%d", s->iono_slips);

    out_ch(&o, '}');
    return (int)o.len;
}

/* ── CSV ──────────────────────────────────────────────────────────────
 * Scalars only.  The per-type and per-GNSS tables are variable length and
 * do not belong in a fixed row; appending one row per interval gives a
 * time series a spreadsheet can plot directly. */

#define NS_CSV_COLUMNS \
    "t_start_unix,uptime_s,mountpoint,caster,connected,ntrip_version," \
    "http_status,caster_software,reconnects,bytes_total,bytes_per_s," \
    "frames_ok,frames_crc_error,frames_malformed,framing_resyncs," \
    "crc_error_rate,advertised_count,types_missing,types_offrate," \
    "types_extra,sats_total,cnr_mean_all,station_type,arp_valid," \
    "arp_lat,arp_lon,arp_alt,arp_drift_m,arp_moves," \
    "sourcetable_offset_m,latency_s,"     "iono_verdict,iono_roti_median,iono_roti_max,iono_sats_dualfreq,"     "iono_slips"

int ns_stats_csv_header(char *out, size_t cap)
{
    if (!out && cap > 0) return -1;
    NsOut o;
    out_init(&o, out, cap);
    out_str(&o, NS_CSV_COLUMNS);
    return (int)o.len;
}

/** @brief Append a CSV field, quoting and doubling quotes when needed. */
static void out_csv_str(NsOut *o, const char *s)
{
    bool needs_quote = false;
    for (const char *p = s; p && *p; p++) {
        if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r') {
            needs_quote = true;
            break;
        }
    }
    if (!needs_quote) { out_str(o, s ? s : ""); return; }

    out_ch(o, '"');
    for (const char *p = s; p && *p; p++) {
        if (*p == '"') out_ch(o, '"');   /* RFC 4180: double it */
        out_ch(o, *p);
    }
    out_ch(o, '"');
}

/** @brief Append a numeric CSV field, empty when not measured. */
static void out_csv_num(NsOut *o, double v, int decimals)
{
    if (!isfinite(v) || v == NS_UNSET) return;   /* empty cell */
    out_fmt(o, "%.*f", decimals, v);
}

int ns_stats_to_csv_row(const NsStatsSnapshot *s, char *out, size_t cap)
{
    if (!s || (!out && cap > 0)) return -1;

    NsOut o;
    out_init(&o, out, cap);

    out_csv_num(&o, s->t_start_unix, 3);          out_ch(&o, ',');
    out_csv_num(&o, s->uptime_s, 3);              out_ch(&o, ',');
    out_csv_str(&o, s->mountpoint);               out_ch(&o, ',');
    out_csv_str(&o, s->caster);                   out_ch(&o, ',');
    out_str(&o, s->connected ? "1" : "0");        out_ch(&o, ',');
    out_fmt(&o, "%d", s->ntrip_version);          out_ch(&o, ',');
    out_fmt(&o, "%d", s->http_status);            out_ch(&o, ',');
    out_csv_str(&o, s->caster_software);          out_ch(&o, ',');
    out_fmt(&o, "%d", s->reconnects);             out_ch(&o, ',');
    out_fmt(&o, "%llu", (unsigned long long)s->bytes_total);      out_ch(&o, ',');
    out_csv_num(&o, s->bytes_per_s, 1);           out_ch(&o, ',');
    out_fmt(&o, "%llu", (unsigned long long)s->frames_ok);        out_ch(&o, ',');
    out_fmt(&o, "%llu", (unsigned long long)s->frames_crc_error); out_ch(&o, ',');
    out_fmt(&o, "%llu", (unsigned long long)s->frames_malformed); out_ch(&o, ',');
    out_fmt(&o, "%llu", (unsigned long long)s->framing_resyncs);  out_ch(&o, ',');
    out_csv_num(&o, s->crc_error_rate, 6);        out_ch(&o, ',');
    out_fmt(&o, "%d", s->advertised_count);       out_ch(&o, ',');
    out_fmt(&o, "%d", s->types_missing);          out_ch(&o, ',');
    out_fmt(&o, "%d", s->types_offrate);          out_ch(&o, ',');
    out_fmt(&o, "%d", s->types_extra);            out_ch(&o, ',');
    out_fmt(&o, "%d", s->sats_total);             out_ch(&o, ',');
    out_csv_num(&o, s->cnr_mean_all, 2);          out_ch(&o, ',');
    out_csv_str(&o, ns_station_type_name(s->station_type)); out_ch(&o, ',');
    out_str(&o, s->arp_valid ? "1" : "0");        out_ch(&o, ',');
    out_csv_num(&o, s->arp_valid ? s->arp_lat : NS_UNSET, 8); out_ch(&o, ',');
    out_csv_num(&o, s->arp_valid ? s->arp_lon : NS_UNSET, 8); out_ch(&o, ',');
    out_csv_num(&o, s->arp_valid ? s->arp_alt : NS_UNSET, 3); out_ch(&o, ',');
    out_csv_num(&o, s->arp_drift_m, 3);           out_ch(&o, ',');
    out_fmt(&o, "%d", s->arp_moves);              out_ch(&o, ',');
    out_csv_num(&o, s->sourcetable_offset_m, 1);  out_ch(&o, ',');
    out_csv_num(&o, s->latency_s, 3);             out_ch(&o, ',');
    out_fmt(&o, "%d", s->iono_verdict);           out_ch(&o, ',');
    out_csv_num(&o, s->iono_roti_median >= 0.0f ? s->iono_roti_median
                                                : NS_UNSET, 3);
    out_ch(&o, ',');
    out_csv_num(&o, s->iono_roti_max >= 0.0f ? s->iono_roti_max
                                             : NS_UNSET, 3);
    out_ch(&o, ',');
    out_fmt(&o, "%d", s->iono_sats_dualfreq);     out_ch(&o, ',');
    out_fmt(&o, "%d", s->iono_slips);

    return (int)o.len;
}
