/**
 * @file test_ns_stats.c
 * @brief The snapshot's two serialisations must be readable by machines.
 *
 * `ns_stats_to_json()` feeds the daemon's status output, the GUI's export
 * and the Android bridge; `ns_stats_to_csv_row()` feeds a spreadsheet
 * hours or years later.  Both are written by hand, field by field, and
 * both fail in ways that no compiler and no eye reliably catches:
 *
 *   - **A missing separator.** `"advertised_gnss":5"types_missing":2`
 *     shipped, and is not JSON at all.  It was found by accident while
 *     adding a key beside it, which is one accident too many for a
 *     property a parser can decide in a millisecond.
 *   - **A column that drifts.** The CSV header and the row are two
 *     independent lists in two functions.  A field added to one and not
 *     the other silently shifts every column after it, and an archived
 *     file carries that shift for as long as it exists.
 *   - **Text from the network.** A caster's `Server:` header is
 *     arbitrary bytes.  A quote, a comma or a control character in it
 *     must not be able to end a JSON string or invent a CSV column.
 *
 * So this parses the output rather than inspecting it: a small strict
 * JSON reader, and an RFC 4180 field splitter.  Neither knows anything
 * about the snapshot's fields, which is the point -- they keep working
 * as fields are added.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
#include "core/ns_stats.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

/* ── A strict JSON reader ─────────────────────────────────────────────
 *
 * Enough of RFC 8259 to decide whether a document is well formed, and
 * nothing more: it does not build a tree, because the only question
 * asked here is whether a parser would accept what we emit.  A value
 * inside an object must be followed by ',' or '}', which is exactly the
 * rule the missing separator broke. */

#define MAX_KEYS 128
#define KEY_LEN   48

typedef struct {
    const char *p;
    const char *start;
    int         ok;
    const char *err;
    char        key[MAX_KEYS][KEY_LEN];  /* top-level keys, in order */
    int         n_keys;
} Jp;

static void jfail(Jp *j, const char *why)
{
    if (j->ok) { j->ok = 0; j->err = why; }
}

static void skip_ws(Jp *j)
{
    while (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')
        j->p++;
}

/** @brief Read a string literal; copies it to @p out when asked. */
static void jstring(Jp *j, char *out, size_t cap)
{
    size_t n = 0;
    if (*j->p != '"') { jfail(j, "expected a string"); return; }
    j->p++;
    while (*j->p && *j->p != '"') {
        unsigned char c = (unsigned char)*j->p;
        if (c < 0x20) { jfail(j, "a raw control character in a string"); return; }
        if (c == '\\') {
            j->p++;
            switch (*j->p) {
            case '"': case '\\': case '/': case 'b': case 'f':
            case 'n': case 'r':  case 't':
                break;
            case 'u':
                for (int i = 1; i <= 4; i++) {
                    char h = j->p[i];
                    int hex = (h >= '0' && h <= '9') || (h >= 'a' && h <= 'f')
                           || (h >= 'A' && h <= 'F');
                    if (!hex) { jfail(j, "a bad \\u escape"); return; }
                }
                j->p += 4;
                break;
            default:
                jfail(j, "an unknown escape");
                return;
            }
        }
        if (out && n + 1 < cap) out[n++] = *j->p;
        j->p++;
    }
    if (*j->p != '"') { jfail(j, "an unterminated string"); return; }
    j->p++;
    if (out && cap) out[n] = '\0';
}

static void jnumber(Jp *j)
{
    int digits = 0;
    if (*j->p == '-') j->p++;
    while (*j->p >= '0' && *j->p <= '9') { j->p++; digits++; }
    if (*j->p == '.') {
        j->p++;
        int frac = 0;
        while (*j->p >= '0' && *j->p <= '9') { j->p++; frac++; }
        if (!frac) { jfail(j, "a decimal point with no digits"); return; }
    }
    if (*j->p == 'e' || *j->p == 'E') {
        j->p++;
        if (*j->p == '+' || *j->p == '-') j->p++;
        int exp = 0;
        while (*j->p >= '0' && *j->p <= '9') { j->p++; exp++; }
        if (!exp) { jfail(j, "an exponent with no digits"); return; }
    }
    if (!digits) jfail(j, "a number with no digits");
}

static void jvalue(Jp *j, int depth);

static void jobject(Jp *j, int depth)
{
    j->p++;                                   /* '{' */
    skip_ws(j);
    if (*j->p == '}') { j->p++; return; }

    for (;;) {
        skip_ws(j);
        char name[KEY_LEN];
        jstring(j, name, sizeof(name));
        if (!j->ok) return;
        if (depth == 0 && j->n_keys < MAX_KEYS) {
            memcpy(j->key[j->n_keys], name, sizeof(name));
            j->n_keys++;
        }

        skip_ws(j);
        if (*j->p != ':') { jfail(j, "a key with no colon"); return; }
        j->p++;

        jvalue(j, depth + 1);
        if (!j->ok) return;

        skip_ws(j);
        if (*j->p == ',') { j->p++; continue; }
        if (*j->p == '}') { j->p++; return; }
        /* The defect this file exists for: a value followed by the next
         * key with nothing between them. */
        jfail(j, "a value not followed by ',' or '}'");
        return;
    }
}

static void jarray(Jp *j, int depth)
{
    j->p++;                                   /* '[' */
    skip_ws(j);
    if (*j->p == ']') { j->p++; return; }

    for (;;) {
        jvalue(j, depth + 1);
        if (!j->ok) return;
        skip_ws(j);
        if (*j->p == ',') { j->p++; continue; }
        if (*j->p == ']') { j->p++; return; }
        jfail(j, "an element not followed by ',' or ']'");
        return;
    }
}

static void jvalue(Jp *j, int depth)
{
    skip_ws(j);
    if (depth > 32) { jfail(j, "nested too deeply"); return; }

    switch (*j->p) {
    case '{': jobject(j, depth); return;
    case '[': jarray(j, depth);  return;
    case '"': jstring(j, NULL, 0); return;
    case 't': if (!strncmp(j->p, "true",  4)) { j->p += 4; return; } break;
    case 'f': if (!strncmp(j->p, "false", 5)) { j->p += 5; return; } break;
    case 'n': if (!strncmp(j->p, "null",  4)) { j->p += 4; return; } break;
    default:  jnumber(j); return;
    }
    jfail(j, "not a value");
}

/**
 * @brief Parse @p text; report the failure with its offset when it fails.
 * @return Number of top-level keys, or -1 if the document is malformed.
 */
static int json_check(const char *text, const char *what)
{
    Jp j;
    memset(&j, 0, sizeof(j));
    j.p = j.start = text;
    j.ok = 1;

    jvalue(&j, 0);
    skip_ws(&j);
    if (j.ok && *j.p) jfail(&j, "trailing bytes after the document");

    if (!j.ok) {
        long at = (long)(j.p - j.start);
        printf("FAIL: %s -- %s at offset %ld\n", what, j.err, at);
        /* The neighbourhood, which is what a reader needs to fix it. */
        long from = at > 30 ? at - 30 : 0;
        printf("      ...%.60s...\n", j.start + from);
        failures++;
        return -1;
    }
    printf("ok  : %s\n", what);
    return j.n_keys;
}

/** @brief Report any top-level key emitted twice. */
static void json_check_unique(const char *text, const char *what)
{
    Jp j;
    memset(&j, 0, sizeof(j));
    j.p = j.start = text;
    j.ok = 1;
    jvalue(&j, 0);
    if (!j.ok) return;                        /* already reported */

    const char *dup = NULL;
    for (int i = 0; i < j.n_keys && !dup; i++)
        for (int k = i + 1; k < j.n_keys && !dup; k++)
            if (!strcmp(j.key[i], j.key[k])) dup = j.key[i];

    check(dup == NULL, what);
    if (dup) printf("      duplicated key: %s\n", dup);
}

/* ── An RFC 4180 field splitter ──────────────────────────────────────
 * A comma inside a quoted field is data, not a separator.  Counting
 * commas would pass a row that a spreadsheet reads one column short. */
static int csv_fields(const char *row)
{
    int n = 1;
    int in_quotes = 0;
    for (const char *p = row; *p; p++) {
        if (*p == '"') {
            if (in_quotes && p[1] == '"') { p++; continue; }  /* escaped */
            in_quotes = !in_quotes;
        } else if (*p == ',' && !in_quotes) {
            n++;
        }
    }
    return in_quotes ? -1 : n;                /* -1: a field never closed */
}

/* ── Snapshots to serialise ──────────────────────────────────────────── */

/** @brief A snapshot with every table populated and hostile text in it. */
static void populate(NsStatsSnapshot *s)
{
    ns_stats_init(s);

    snprintf(s->mountpoint, sizeof(s->mountpoint), "HANESE");
    snprintf(s->caster, sizeof(s->caster), "caster.example.org");
    /* What a caster may actually answer with.  Every one of these
     * characters ends a string or a field if it is not escaped. */
    snprintf(s->caster_software, sizeof(s->caster_software),
             "BKG Prof\"essional, v2.1\\%c", 0x01);

    s->t_start_unix = 1755000000.0;
    s->uptime_s     = 3600.25;
    s->connected    = true;
    s->ntrip_version = NS_NTRIP_V2;
    s->http_status   = 200;
    s->reconnects    = 2;

    s->bytes_total      = 123456789ULL;
    s->bytes_per_s      = 1580.5;
    s->frames_ok        = 98765ULL;
    s->frames_crc_error = 3ULL;
    s->framing_resyncs  = 1ULL;
    s->crc_error_rate   = 0.0000304;

    s->advertised_known = true;
    s->advertised_count = 8;
    s->advertised_gnss  = 0x1F;
    s->types_missing    = 1;
    s->types_offrate    = 2;
    s->types_extra      = 3;

    for (int i = 0; i < 4; i++) {
        NsTypeStats *t = ns_stats_type(s, 1071 + i * 10);
        t->frames  = (uint64_t)(100 + i);
        t->epochs  = (uint64_t)(50 + i);
        t->min_dt  = 0.9;
        t->max_dt  = 15.779;
        t->avg_dt  = 1.0;
        t->advertised_interval = 1.0f;
        t->verdict = NS_VERDICT_OK;
    }
    for (int i = 0; i < 3; i++) {
        NsGnssStats *g = ns_stats_gnss(s, i + 1);
        g->sats_tracked = 10 + i;
        g->cnr_mean     = 45.5f;
        g->cnr_median   = 46.0f;
        g->cnr_min      = 30.0f;
        g->cnr_max      = 52.0f;
    }
    s->sats_total   = 39;
    s->cnr_mean_all = 45.7f;

    s->station_type = NS_STATION_FIXED;
    s->arp_valid    = true;
    s->arp_lat      = 52.0123456;
    s->arp_lon      = 5.9876543;
    s->arp_alt      = 43.21;

    s->iono_verdict       = 1;
    s->iono_roti_median   = 0.06f;
    s->iono_roti_max      = 0.4f;
    s->iono_sats_dualfreq = 21;
    s->iono_slips         = 4;

    s->stream_time_s = 3600.0;
}

int main(void)
{
    char json[16384], header[4096], row[4096];
    NsStatsSnapshot s;

    /* ── 1. A populated snapshot serialises to readable JSON ──────── */
    {
        populate(&s);
        int n = ns_stats_to_json(&s, json, sizeof(json));
        check(n > 0 && (size_t)n < sizeof(json),
              "a populated snapshot fits the buffer");
        json_check(json, "a populated snapshot is well-formed JSON");
        json_check_unique(json, "and no top-level key is emitted twice");
    }

    /* ── 2. So does an empty one ──────────────────────────────────── */
    {
        NsStatsSnapshot e;
        ns_stats_init(&e);
        ns_stats_to_json(&e, json, sizeof(json));
        json_check(json, "an initialised snapshot is well-formed JSON");

        /* Unmeasured is null, never 0: the distinction the whole
         * NS_UNSET convention exists to carry. */
        check(strstr(json, "\"latency_s\":null") != NULL,
              "an unmeasured latency serialises as null");
        check(strstr(json, "\"stream_time_s\":null") != NULL,
              "a stream with no clock serialises as null");
    }

    /* ── 3. The CSV header and the row describe the same columns ──── */
    {
        populate(&s);
        int nh = ns_stats_csv_header(header, sizeof(header));
        int nr = ns_stats_to_csv_row(&s, row, sizeof(row));
        check(nh > 0 && (size_t)nh < sizeof(header)
              && nr > 0 && (size_t)nr < sizeof(row),
              "the CSV header and row fit their buffers");

        int hc = csv_fields(header);
        int rc = csv_fields(row);
        check(hc > 20, "the header has columns to compare");
        check(rc == hc, "the row has exactly as many columns as the header");
        if (rc != hc) printf("      header %d, row %d\n", hc, rc);

        /* A quote and a comma arrived from the network and stayed
         * inside one field. */
        check(rc != -1, "a quoted field is closed");
    }

    /* ── 4. Unmeasured is an empty cell, not a zero ───────────────── */
    {
        NsStatsSnapshot e;
        ns_stats_init(&e);
        ns_stats_csv_header(header, sizeof(header));
        ns_stats_to_csv_row(&e, row, sizeof(row));
        check(csv_fields(row) == csv_fields(header),
              "an empty snapshot still fills every column");
        check(strstr(row, ",,") != NULL,
              "and an unmeasured value is an empty cell, not a 0");
    }

    /* ── 5. Truncation is reported, never silent ──────────────────── */
    {
        /* The snprintf contract: a return at or above the capacity means
         * the output is unusable, and the buffer is still terminated so
         * a caller that ignores the return does not read past it. */
        char tiny[24];
        populate(&s);

        memset(tiny, 'x', sizeof(tiny));
        int n = ns_stats_to_json(&s, tiny, sizeof(tiny));
        check((size_t)n >= sizeof(tiny), "a short JSON buffer reports the need");
        check(memchr(tiny, '\0', sizeof(tiny)) != NULL,
              "and the truncated JSON is still NUL-terminated");

        memset(tiny, 'x', sizeof(tiny));
        n = ns_stats_to_csv_row(&s, tiny, sizeof(tiny));
        check((size_t)n >= sizeof(tiny), "a short CSV buffer reports the need");
        check(memchr(tiny, '\0', sizeof(tiny)) != NULL,
              "and the truncated row is still NUL-terminated");

        memset(tiny, 'x', sizeof(tiny));
        n = ns_stats_csv_header(tiny, sizeof(tiny));
        check((size_t)n >= sizeof(tiny), "so does a short header buffer");
    }

    /* ── 6. A NULL snapshot is refused, not dereferenced ──────────── */
    {
        check(ns_stats_to_json(NULL, json, sizeof(json)) < 0,
              "serialising nothing is an error, not a crash");
        check(ns_stats_to_csv_row(NULL, row, sizeof(row)) < 0,
              "and the same for a row");
    }

    printf("\n%s\n", failures ? "FAILURES" : "all ns_stats serialisation cases pass");
    return failures ? 1 : 0;
}
