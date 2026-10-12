/**
 * @file sourcetable_map.c
 * @brief A sourcetable as map data, and the selection token --
 *        implementation.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * @copyright Apache License 2.0 with Commons Clause (see LICENSE for details)
 */

#include "core/sourcetable_map.h"
#include "core/geo.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#  define strcasecmp _stricmp
#else
#  include <strings.h>
#endif

/* ── Output that counts past its end ─────────────────────────────────── */

typedef struct {
    char  *buf;
    size_t cap;
    size_t len;     /* bytes the complete output needs, so far */
    bool   broken;  /* a formatted piece did not fit its scratch buffer */
} Out;

static void put(Out *o, const char *s, size_t n)
{
    if (o->buf && o->len < o->cap) {
        size_t room = o->cap - o->len;
        memcpy(o->buf + o->len, s, n < room ? n : room);
    }
    o->len += n;
}

static void put_s(Out *o, const char *s) { put(o, s, strlen(s)); }

/**
 * A short formatted piece: a number, an escape. Longer text goes
 * through put_s. A piece that would not fit is never written cut short
 * -- the first draft did exactly that, silently, and the feature header
 * came out as `[5.9"mountpoint"` -- it marks the output broken instead,
 * and the caller refuses the whole document.
 */
static void put_f(Out *o, const char *fmt, ...)
{
    char tmp[64];
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (k < 0 || (size_t)k >= sizeof(tmp)) { o->broken = true; return; }
    put(o, tmp, (size_t)k);
}

/**
 * Length of the valid UTF-8 sequence starting at @p p, or 0 when it is
 * not one: a stray continuation byte, a truncated sequence, an overlong
 * form, a surrogate, or a code point past U+10FFFF.
 */
static size_t utf8_len(const unsigned char *p)
{
    unsigned char c = p[0];
    if (c < 0x80) return 1;

    size_t n;
    unsigned cp;
    if      (c >= 0xC2 && c <= 0xDF) { n = 2; cp = c & 0x1Fu; }
    else if (c >= 0xE0 && c <= 0xEF) { n = 3; cp = c & 0x0Fu; }
    else if (c >= 0xF0 && c <= 0xF4) { n = 4; cp = c & 0x07u; }
    else return 0;

    for (size_t i = 1; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) return 0;   /* includes the terminator */
        cp = (cp << 6) | (p[i] & 0x3Fu);
    }
    if ((n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000)) return 0;
    if (cp >= 0xD800 && cp <= 0xDFFF) return 0;
    if (cp > 0x10FFFF) return 0;
    return n;
}

/**
 * A JSON string that is also safe inside an HTML `<script>` block.
 * Escaped: `"` and `\`, every control character and DEL, `<` `>` `&`
 * (so nothing can close the block or open a comment), and U+2028/2029
 * (line terminators to older JavaScript). Invalid UTF-8 becomes U+FFFD
 * one byte at a time.
 */
static void put_json_str(Out *o, const char *s)
{
    put(o, "\"", 1);
    const unsigned char *p = (const unsigned char *)(s ? s : "");
    while (*p) {
        unsigned char c = *p;
        if (c == '"')  { put(o, "\\\"", 2); p++; continue; }
        if (c == '\\') { put(o, "\\\\", 2); p++; continue; }
        if (c < 0x20 || c == 0x7F || c == '<' || c == '>' || c == '&') {
            put_f(o, "\\u%04x", c);
            p++;
            continue;
        }
        size_t n = utf8_len(p);
        if (n == 0) {
            put(o, "\\ufffd", 6);
            p++;
            continue;
        }
        if (n == 3 && p[0] == 0xE2 && p[1] == 0x80 && (p[2] == 0xA8 || p[2] == 0xA9)) {
            put(o, p[2] == 0xA8 ? "\\u2028" : "\\u2029", 6);
            p += 3;
            continue;
        }
        put(o, (const char *)p, n);
        p += n;
    }
    put(o, "\"", 1);
}

/* ── The token ───────────────────────────────────────────────────────── */

const char *sourcetable_kind_name(SourcetableKind kind)
{
    switch (kind) {
    case ST_KIND_FIXED:   return "fixed";
    case ST_KIND_NETWORK: return "network";
    case ST_KIND_PRODUCT: return "product";
    default:              return "unknown";
    }
}

static bool unreserved(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~';
}

static void put_pct(Out *o, const char *s)
{
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (unreserved(*p)) put(o, (const char *)p, 1);
        else                put_f(o, "%%%02X", *p);
    }
}

/** The token, written through @p o; false when it cannot be made. */
static bool put_token(Out *o, const char *caster, int port, const char *mountpoint)
{
    if (!caster || !*caster || !mountpoint || !*mountpoint) return false;
    if (port < 1 || port > 65535) return false;
    put_s(o, ST_TOKEN_PREFIX);
    put_pct(o, caster);
    put_f(o, ":%d/", port);
    put_pct(o, mountpoint);
    return true;
}

int sourcetable_make_token(const char *caster, int port, const char *mountpoint,
                           char *buf, size_t cap)
{
    if (buf && cap) buf[0] = '\0';
    if (!buf || !cap) return -1;

    Out o = { buf, cap, 0, false };
    if (!put_token(&o, caster, port, mountpoint) || o.broken || o.len >= cap) {
        buf[0] = '\0';
        return -1;
    }
    buf[o.len] = '\0';
    return (int)o.len;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/**
 * Percent-decode [s, end) into @p dst. False on a broken escape, a
 * decoded NUL, a reserved character left unencoded, or no room.
 */
static bool pct_decode(const char *s, const char *end, char *dst, size_t cap)
{
    size_t n = 0;
    while (s < end) {
        unsigned char c = (unsigned char)*s;
        if (c == '%') {
            if (end - s < 3) return false;
            int hi = hexval(s[1]), lo = hexval(s[2]);
            if (hi < 0 || lo < 0) return false;
            c = (unsigned char)(hi * 16 + lo);
            if (c == 0) return false;
            s += 3;
        } else if (unreserved(c)) {
            s++;
        } else {
            return false;   /* our writer never leaves these bare */
        }
        if (n + 1 >= cap) return false;
        dst[n++] = (char)c;
    }
    dst[n] = '\0';
    return n > 0;
}

static bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

int sourcetable_match_token(const char *text, size_t len,
                            const char *caster, int port,
                            const SourcetableEntry *entries, int n,
                            int *matches)
{
    if (matches) *matches = 0;
    if (!text) return ST_TOKEN_NONE;

    const char *s = text, *end = text + len;
    while (s < end && is_space(*s)) s++;
    while (end > s && is_space(end[-1])) end--;

    size_t plen = strlen(ST_TOKEN_PREFIX);
    if ((size_t)(end - s) < plen || memcmp(s, ST_TOKEN_PREFIX, plen) != 0)
        return ST_TOKEN_NONE;

    /* From here it claims to be ours, so every fault is reported. The
     * length is the raw clipboard text's, padding included. A NUL needs
     * no check of its own: it can be neither a digit of the port nor a
     * bare character pct_decode accepts. */
    if (len > ST_TOKEN_MAX) return ST_TOKEN_MALFORMED;
    s += plen;

    /* <caster>:<port>/<mountpoint>. Encoding leaves no ':' or '/' in
     * either name, so the first of each is the separator. */
    const char *colon = memchr(s, ':', (size_t)(end - s));
    if (!colon) return ST_TOKEN_MALFORMED;
    const char *slash = memchr(colon, '/', (size_t)(end - colon));
    if (!slash) return ST_TOKEN_MALFORMED;

    char want_caster[256], want_mount[sizeof(entries->mountpoint)];
    if (!pct_decode(s, colon, want_caster, sizeof(want_caster)))     return ST_TOKEN_MALFORMED;
    if (!pct_decode(slash + 1, end, want_mount, sizeof(want_mount))) return ST_TOKEN_MALFORMED;

    long want_port = 0;
    if (slash - colon < 2 || slash - colon > 6) return ST_TOKEN_MALFORMED;
    for (const char *p = colon + 1; p < slash; p++) {
        if (*p < '0' || *p > '9') return ST_TOKEN_MALFORMED;
        want_port = want_port * 10 + (*p - '0');
    }
    if (want_port < 1 || want_port > 65535) return ST_TOKEN_MALFORMED;

    if (!caster || strcasecmp(want_caster, caster) != 0 || want_port != port)
        return ST_TOKEN_OTHER_CASTER;

    int found = -1, count = 0;
    for (int i = 0; entries && i < n; i++) {
        if (strcmp(entries[i].mountpoint, want_mount) == 0) {
            if (found < 0) found = i;
            count++;
        }
    }
    if (matches) *matches = count;
    if (count == 0) return ST_TOKEN_NOT_LISTED;
    if (count > 1)  return ST_TOKEN_AMBIGUOUS;
    return found;
}

/* ── GeoJSON ─────────────────────────────────────────────────────────── */

static void put_prop_str(Out *o, const char *key, const char *val)
{
    put_json_str(o, key);
    put(o, ":", 1);
    put_json_str(o, val);
    put(o, ",", 1);
}

long sourcetable_to_geojson(const SourcetableEntry *entries, int n,
                            const char *caster, int port,
                            double origin_lat, double origin_lon,
                            char *buf, size_t cap)
{
    if (n < 0 || (n > 0 && !entries)) return -1;
    if (buf && cap) buf[0] = '\0';

    Out o = { buf, buf ? cap : 0, 0, false };
    bool has_origin = sourcetable_pos_usable(origin_lat, origin_lon);

    int omitted = 0;
    for (int i = 0; i < n; i++)
        if (!sourcetable_pos_usable(entries[i].latitude, entries[i].longitude))
            omitted++;

    put_s(&o, "{\"type\":\"FeatureCollection\",");
    put_prop_str(&o, "caster", caster);
    put_s(&o, "\"port\":");
    put_f(&o, "%d", port);
    put_s(&o, ",\"origin\":");
    if (has_origin) put_f(&o, "[%.7f,%.7f]", origin_lon, origin_lat);
    else            put_s(&o, "null");
    put_s(&o, ",\"total\":");
    put_f(&o, "%d", n);
    put_s(&o, ",\"omitted\":");
    put_f(&o, "%d", omitted);
    put_s(&o, ",\"features\":[");

    bool first = true;
    for (int i = 0; i < n; i++) {
        const SourcetableEntry *e = &entries[i];
        if (!sourcetable_pos_usable(e->latitude, e->longitude)) continue;

        if (!first) put(&o, ",", 1);
        first = false;

        put_s(&o, "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":");
        put_f(&o, "[%.7f,%.7f]", e->longitude, e->latitude);
        put_s(&o, "},\"properties\":{");

        put_prop_str(&o, "mountpoint",  e->mountpoint);
        put_prop_str(&o, "identifier",  e->identifier);
        put_prop_str(&o, "network",     e->network);
        put_prop_str(&o, "country",     e->country);
        put_prop_str(&o, "format",      e->format);
        put_prop_str(&o, "nav_systems", e->nav_systems);

        char why[96];
        SourcetableKind k = sourcetable_kind(e, why, sizeof(why));
        put_prop_str(&o, "kind",     sourcetable_kind_name(k));
        put_prop_str(&o, "kind_why", why);
        put_f(&o, "\"nmea\":%s,", e->nmea ? "true" : "false");

        /* The token is built here, through the same escaping as every
         * other string, so the page copies text it never assembled. */
        put_json_str(&o, "token");
        put(&o, ":", 1);
        char tok[ST_TOKEN_MAX + 1];
        Out t = { tok, sizeof(tok), 0, false };
        if (put_token(&t, caster, port, e->mountpoint) && !t.broken && t.len < sizeof(tok)) {
            tok[t.len] = '\0';
            put_json_str(&o, tok);
        } else {
            put_s(&o, "null");
        }

        if (has_origin) {
            put_s(&o, ",\"distance_m\":");
            put_f(&o, "%.1f", geo_distance_m(origin_lat, origin_lon, e->latitude, e->longitude));
            put_s(&o, ",\"bearing_deg\":");
            put_f(&o, "%.1f", geo_bearing_deg(origin_lat, origin_lon, e->latitude, e->longitude));
            put_s(&o, "}}");
        } else {
            put_s(&o, ",\"distance_m\":null,\"bearing_deg\":null}}");
        }
    }
    put_s(&o, "]}");

    if (o.broken) {
        if (buf && cap) buf[0] = '\0';
        return -1;
    }
    if (buf && cap) {
        if (o.len < cap) buf[o.len] = '\0';
        else             buf[0] = '\0';   /* all or nothing */
    }
    return (long)o.len;
}
