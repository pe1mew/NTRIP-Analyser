/**
 * @file test_sourcetable_map.c
 * @brief The mountpoint map's data, and the token that comes back.
 *
 * design/work-items/mountpoint-map.md, M2. Two crossings of text from
 * outside the program:
 *
 *   - **Out.** A caster's sourcetable written as GeoJSON into a page the
 *     browser runs. Every case is parsed back with cJSON and compared
 *     field by field -- inspecting the text would pass output that no
 *     parser accepts. Beyond valid JSON, nothing may appear that closes
 *     a `<script>` block or ends a JavaScript line.
 *   - **Back.** Clipboard text read as a selection. Anything can write
 *     to a clipboard, so every way a token could misrepresent itself is
 *     tried, and a refusal must say which.
 *
 * The real sourcetables of the M1 sample (8471 records) held one
 * non-ASCII name, valid UTF-8, and no control characters or HTML-special
 * characters at all. These cases are for the caster that is not polite.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
#include "core/sourcetable_map.h"
#include "core/geo.h"
#include "cJSON.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

static SourcetableEntry entry(const char *mp, double lat, double lon)
{
    SourcetableEntry e;
    memset(&e, 0, sizeof(e));
    snprintf(e.mountpoint, sizeof(e.mountpoint), "%s", mp);
    snprintf(e.identifier, sizeof(e.identifier), "%s", mp);
    snprintf(e.format, sizeof(e.format), "RTCM 3.3");
    snprintf(e.format_details, sizeof(e.format_details), "1005(30),1077(1)");
    snprintf(e.nav_systems, sizeof(e.nav_systems), "GPS+GAL");
    snprintf(e.network, sizeof(e.network), "NET");
    snprintf(e.country, sizeof(e.country), "NLD");
    e.latitude = lat;
    e.longitude = lon;
    e.solution = 0;
    return e;
}

/** The whole document, allocated; NULL if the two calls disagree. */
static char *geojson(const SourcetableEntry *e, int n, const char *caster, int port,
                     double olat, double olon)
{
    long need = sourcetable_to_geojson(e, n, caster, port, olat, olon, NULL, 0);
    if (need < 0) return NULL;
    char *buf = (char *)malloc((size_t)need + 1);
    long got = sourcetable_to_geojson(e, n, caster, port, olat, olon, buf, (size_t)need + 1);
    if (got != need || strlen(buf) != (size_t)need) { free(buf); return NULL; }
    return buf;
}

static const char *prop(const cJSON *feature, const char *key)
{
    const cJSON *p = cJSON_GetObjectItemCaseSensitive(feature, "properties");
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(p, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

static int str_is(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }

/** Nothing a <script> block or a JS line could trip on. */
static int script_safe(const char *s)
{
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p < 0x20 || *p == 0x7F || *p == '<' || *p == '>' || *p == '&') return 0;
        if (p[0] == 0xE2 && p[1] == 0x80 && (p[2] == 0xA8 || p[2] == 0xA9)) return 0;
    }
    return 1;
}

int main(void)
{
    /* RFSEE01 and HANESE as registered; 24.98 km apart (M1, live). */
    const double rf_lat = 52.2115167, rf_lon = 5.9837064;
    const double ha_lat = 51.987737,  ha_lon = 5.951523;

    /* ── 1. Ordinary entries, and the ones that cannot be drawn ────── */
    {
        SourcetableEntry e[3] = {
            entry("RFSEE01", rf_lat, rf_lon),
            entry("NOWHERE", 0.0, 0.0),
            entry("NONSENSE", 400.0, 5.0),
        };
        char *doc = geojson(e, 3, "rfsee.net", 2101, ha_lat, ha_lon);
        cJSON *j = doc ? cJSON_Parse(doc) : NULL;
        check(j != NULL, "the document parses");

        const cJSON *feats = cJSON_GetObjectItemCaseSensitive(j, "features");
        check(str_is(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(j, "type")),
                     "FeatureCollection") &&
              cJSON_GetArraySize(feats) == 1,
              "one feature: the two unusable positions are not drawn");
        check(cJSON_GetObjectItemCaseSensitive(j, "total")->valueint == 3 &&
              cJSON_GetObjectItemCaseSensitive(j, "omitted")->valueint == 2,
              "but they are counted: total 3, omitted 2");
        check(str_is(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(j, "caster")),
                     "rfsee.net") &&
              cJSON_GetObjectItemCaseSensitive(j, "port")->valueint == 2101,
              "the header names the caster and port");

        const cJSON *f = cJSON_GetArrayItem(feats, 0);
        const cJSON *c = cJSON_GetObjectItemCaseSensitive(
            cJSON_GetObjectItemCaseSensitive(f, "geometry"), "coordinates");
        check(fabs(cJSON_GetArrayItem(c, 0)->valuedouble - rf_lon) < 1e-7 &&
              fabs(cJSON_GetArrayItem(c, 1)->valuedouble - rf_lat) < 1e-7,
              "coordinates are [lon, lat], as GeoJSON orders them, to 1e-7 degree");
        check(str_is(prop(f, "mountpoint"), "RFSEE01") && str_is(prop(f, "kind"), "fixed") &&
              str_is(prop(f, "kind_why"), "sourcetable states a single base"),
              "mountpoint, kind and its reason");

        char tok[ST_TOKEN_MAX + 1];
        sourcetable_make_token("rfsee.net", 2101, "RFSEE01", tok, sizeof(tok));
        check(str_is(prop(f, "token"), tok), "the feature carries the token the map will copy");

        const cJSON *p = cJSON_GetObjectItemCaseSensitive(f, "properties");
        double d = cJSON_GetObjectItemCaseSensitive(p, "distance_m")->valuedouble;
        double b = cJSON_GetObjectItemCaseSensitive(p, "bearing_deg")->valuedouble;
        check(fabs(d - geo_distance_m(ha_lat, ha_lon, rf_lat, rf_lon)) <= 0.05 &&
              fabs(d - 24980.0) < 10.0,
              "distance from the origin: HANESE to RFSEE01, the 24.98 km seen live");
        check(b > 0.0 && b < 10.0, "bearing: RFSEE01 lies just east of north of HANESE");

        cJSON_Delete(j);
        free(doc);
    }

    /* ── 2. No origin, and no entries ─────────────────────────────── */
    {
        SourcetableEntry e = entry("RFSEE01", rf_lat, rf_lon);
        char *doc = geojson(&e, 1, "rfsee.net", 2101, 0.0, 0.0);
        cJSON *j = doc ? cJSON_Parse(doc) : NULL;
        const cJSON *p = cJSON_GetObjectItemCaseSensitive(
            cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(j, "features"), 0), "properties");
        check(j && cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(j, "origin")) &&
              cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(p, "distance_m")) &&
              cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(p, "bearing_deg")),
              "origin 0,0 is no origin: distance and bearing are null, not measured from 0N 0E");
        cJSON_Delete(j);
        free(doc);

        doc = geojson(NULL, 0, "rfsee.net", 2101, 0.0, 0.0);
        j = doc ? cJSON_Parse(doc) : NULL;
        check(j && cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(j, "features")) == 0 &&
              cJSON_GetObjectItemCaseSensitive(j, "total")->valueint == 0,
              "an empty sourcetable is an empty collection, still valid");
        cJSON_Delete(j);
        free(doc);

        check(sourcetable_to_geojson(NULL, 2, "x", 1, 0, 0, NULL, 0) == -1,
              "entries NULL with n > 0 is refused");

        /* Each kind as the core decides it, from M1's real cases. */
        SourcetableEntry k[3] = {
            entry("NEAR4", 45.0, 4.0),
            entry("BCEP00KAD0", 52.0, 5.0),
            entry("GRAS00FRA0", 43.75, 6.92),
        };
        snprintf(k[1].format_details, sizeof(k[1].format_details), "1019,1020,1042,1045,1046");
        k[2].solution = 1;
        doc = geojson(k, 3, "c", 2101, 0.0, 0.0);
        j = doc ? cJSON_Parse(doc) : NULL;
        const cJSON *fs = cJSON_GetObjectItemCaseSensitive(j, "features");
        check(str_is(prop(cJSON_GetArrayItem(fs, 0), "kind"), "network") &&
              str_is(prop(cJSON_GetArrayItem(fs, 1), "kind"), "product") &&
              str_is(prop(cJSON_GetArrayItem(fs, 2), "kind"), "unknown") &&
              str_is(prop(cJSON_GetArrayItem(fs, 0), "kind_why"), "mountpoint name says \"NEAR\""),
              "kinds are written as the core decides them: network, product, unknown");
        cJSON_Delete(j);
        free(doc);
    }

    /* ── 3. A hostile caster ──────────────────────────────────────── */
    {
        SourcetableEntry e = entry("</script><script>alert(1)</script>", rf_lat, rf_lon);
        snprintf(e.identifier,  sizeof(e.identifier),  "a\"b\\c'd");
        snprintf(e.network,     sizeof(e.network),     "line\xE2\x80\xA8" "break\xE2\x80\xA9");
        snprintf(e.country,     sizeof(e.country),     "N\x01\x1F\x7F");
        snprintf(e.nav_systems, sizeof(e.nav_systems), "bad\xC3\x28 \xFF \xE0\x80\xAF \xED\xA0\x80");
        snprintf(e.format,      sizeof(e.format),      "&amp;<!--");

        char *doc = geojson(&e, 1, "evil\"caster<", 2101, 0.0, 0.0);
        check(doc && script_safe(doc),
              "no < > & , control character, DEL or U+2028/9 appears literally");
        cJSON *j = doc ? cJSON_Parse(doc) : NULL;
        check(j != NULL, "and it is still valid JSON");

        const cJSON *f = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(j, "features"), 0);
        check(str_is(prop(f, "mountpoint"), "</script><script>alert(1)</script>"),
              "the mountpoint survives as text, every character intact");
        check(str_is(prop(f, "identifier"), "a\"b\\c'd"), "quotes and backslashes round-trip");
        check(str_is(prop(f, "network"), "line\xE2\x80\xA8" "break\xE2\x80\xA9"),
              "U+2028 and U+2029 round-trip through their escapes");
        check(str_is(prop(f, "country"), "N\x01\x1F\x7F"), "control characters round-trip");
        check(str_is(prop(f, "format"), "&amp;<!--"), "& < ! - round-trip");
        check(str_is(prop(f, "nav_systems"),
                     "bad\xEF\xBF\xBD( \xEF\xBF\xBD \xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD"
                     " \xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD"),
              "invalid UTF-8 -- a broken pair, a lone 0xFF, an overlong '/', a "
              "surrogate -- becomes U+FFFD byte for byte, never a guess");
        check(str_is(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(j, "caster")),
                     "evil\"caster<"),
              "the caster in the header is escaped too");
        cJSON_Delete(j);
        free(doc);

        SourcetableEntry ok = entry("TOMB", 44.5, 0.4);
        snprintf(ok.identifier, sizeof(ok.identifier), "Tombeb\xC5\x93uf");
        doc = geojson(&ok, 1, "caster.centipede.fr", 2101, 0.0, 0.0);
        j = doc ? cJSON_Parse(doc) : NULL;
        check(doc && strstr(doc, "Tombeb\xC5\x93uf") != NULL &&
              str_is(prop(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(j, "features"), 0),
                          "identifier"), "Tombeb\xC5\x93uf"),
              "valid UTF-8 -- the one non-ASCII name in the M1 sample -- passes unchanged");
        cJSON_Delete(j);
        free(doc);
    }

    /* ── 4. All or nothing when the buffer is short ───────────────── */
    {
        SourcetableEntry e[2] = { entry("RFSEE01", rf_lat, rf_lon), entry("HANESE", ha_lat, ha_lon) };
        long need = sourcetable_to_geojson(e, 2, "rfsee.net", 2101, 0, 0, NULL, 0);
        char *buf = (char *)malloc((size_t)need + 1);
        int all_empty = 1;
        for (long cap = 1; cap <= need; cap++) {
            memset(buf, 'x', (size_t)need + 1);
            long r = sourcetable_to_geojson(e, 2, "rfsee.net", 2101, 0, 0, buf, (size_t)cap);
            if (r != need || buf[0] != '\0') { all_empty = 0; break; }
        }
        check(need > 0 && all_empty,
              "every capacity from 1 to the length itself: the full length returned, "
              "an empty string written -- never a truncated document");
        long r = sourcetable_to_geojson(e, 2, "rfsee.net", 2101, 0, 0, buf, (size_t)need + 1);
        cJSON *j = cJSON_Parse(buf);
        check(r == need && (long)strlen(buf) == need && j != NULL,
              "one byte more, and the whole document is there and parses");
        cJSON_Delete(j);
        free(buf);
    }

    /* ── 5. Tokens: made, and read back ───────────────────────────── */
    {
        char tok[ST_TOKEN_MAX + 1];
        int len = sourcetable_make_token("rfsee.net", 2101, "RFSEE01", tok, sizeof(tok));
        check(len > 0 && str_is(tok, "ntrip-analyser:mount=rfsee.net:2101/RFSEE01"),
              "an ordinary name needs no encoding");

        SourcetableEntry table[4] = {
            entry("HANESE", ha_lat, ha_lon), entry("RFSEE01", rf_lat, rf_lon),
            entry("a/b:c d%", 52, 6), entry("TWICE", 52, 6),
        };
        int m = -9;
        check(sourcetable_match_token(tok, (size_t)len, "rfsee.net", 2101, table, 4, &m) == 1 && m == 1,
              "it selects its row, and only that one");

        char padded[ST_TOKEN_MAX + 8];
        snprintf(padded, sizeof(padded), "  %s\r\n", tok);
        check(sourcetable_match_token(padded, strlen(padded), "rfsee.net", 2101, table, 4, NULL) == 1,
              "surrounding white space, a copied newline included, is ignored");
        check(sourcetable_match_token(tok, (size_t)len, "RFSee.NET", 2101, table, 4, NULL) == 1,
              "the caster matches ignoring case, as host names do");

        len = sourcetable_make_token("rfsee.net", 2101, "a/b:c d%", tok, sizeof(tok));
        check(len > 0 && str_is(tok, "ntrip-analyser:mount=rfsee.net:2101/a%2Fb%3Ac%20d%25"),
              "a name with / : space and % is percent-encoded");
        check(sourcetable_match_token(tok, (size_t)len, "rfsee.net", 2101, table, 4, NULL) == 2,
              "and finds its row again -- writer and reader agree");

        len = sourcetable_make_token("[::1]", 2101, "RFSEE01", tok, sizeof(tok));
        check(len > 0 && sourcetable_match_token(tok, (size_t)len, "[::1]", 2101, table, 4, NULL) == 1,
              "an IPv6 caster's colons are encoded, not read as the port separator");

        check(sourcetable_make_token("rfsee.net", 0, "X", tok, sizeof(tok)) == -1 &&
              sourcetable_make_token("rfsee.net", 65536, "X", tok, sizeof(tok)) == -1 &&
              sourcetable_make_token("", 2101, "X", tok, sizeof(tok)) == -1 &&
              sourcetable_make_token("rfsee.net", 2101, "", tok, sizeof(tok)) == -1,
              "no token for port 0, port 65536, an empty caster or an empty name");
        check(sourcetable_make_token("rfsee.net", 2101, "RFSEE01", tok, 20) == -1 && tok[0] == '\0',
              "no token when it does not fit, and an empty string rather than half of one");
    }

    /* ── 6. Tokens: every refusal, and its reason ─────────────────── */
    {
        SourcetableEntry table[4] = {
            entry("HANESE", ha_lat, ha_lon), entry("RFSEE01", rf_lat, rf_lon),
            entry("TWICE", 52, 6), entry("TWICE", 53, 6),
        };
        static const struct { const char *text; int want; const char *what; } c[] = {
            { "ntrip-analyser:mount=rfsee.net:2102/RFSEE01", ST_TOKEN_OTHER_CASTER, "the wrong port" },
            { "ntrip-analyser:mount=caster.example:2101/RFSEE01", ST_TOKEN_OTHER_CASTER,
              "another caster -- a page left open from the last sourcetable" },
            { "ntrip-analyser:mount=rfsee.net:2101/MISSING", ST_TOKEN_NOT_LISTED, "a name not in the table" },
            { "ntrip-analyser:mount=rfsee.net:2101/rfsee01", ST_TOKEN_NOT_LISTED,
              "a name that differs in case: the token is made from the exact entry" },
            { "ntrip-analyser:mount=rfsee.net:2101/TWICE", ST_TOKEN_AMBIGUOUS,
              "a name listed twice: refused, never resolved to the first" },
            { "ntrip-analyser:mount=rfsee.net:2101", ST_TOKEN_MALFORMED, "no '/'" },
            { "ntrip-analyser:mount=rfsee.net/RFSEE01", ST_TOKEN_MALFORMED, "no ':'" },
            { "ntrip-analyser:mount=rfsee.net:2101/", ST_TOKEN_MALFORMED, "an empty name" },
            { "ntrip-analyser:mount=:2101/RFSEE01", ST_TOKEN_MALFORMED, "an empty caster" },
            { "ntrip-analyser:mount=rfsee.net:/RFSEE01", ST_TOKEN_MALFORMED, "no port" },
            { "ntrip-analyser:mount=rfsee.net:21a1/RFSEE01", ST_TOKEN_MALFORMED, "a port with a letter" },
            { "ntrip-analyser:mount=rfsee.net:0/RFSEE01", ST_TOKEN_MALFORMED, "port 0" },
            { "ntrip-analyser:mount=rfsee.net:70000/RFSEE01", ST_TOKEN_MALFORMED, "port 70000" },
            { "ntrip-analyser:mount=rfsee.net:2101/a/b", ST_TOKEN_MALFORMED,
              "a bare '/' in the name: not read as \"a\", nor as \"a/b\"" },
            { "ntrip-analyser:mount=rfsee.net:2101/RF%G1", ST_TOKEN_MALFORMED, "a broken escape" },
            { "ntrip-analyser:mount=rfsee.net:2101/RF%4", ST_TOKEN_MALFORMED, "a truncated escape" },
            { "ntrip-analyser:mount=rfsee.net:2101/RF%00EE01", ST_TOKEN_MALFORMED, "an encoded NUL" },
            { "ntrip-analyser:mount=rfsee.net:2101/"
              "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
              ST_TOKEN_MALFORMED, "a name longer than any entry can hold" },
            { "hello world", ST_TOKEN_NONE, "ordinary text is not a token, and is not an event" },
            { "", ST_TOKEN_NONE, "an empty clipboard" },
            { "ntrip-analyser:mount", ST_TOKEN_NONE, "the prefix, cut short" },
            { "xntrip-analyser:mount=rfsee.net:2101/RFSEE01", ST_TOKEN_NONE,
              "the prefix somewhere other than the start" },
        };
        for (size_t i = 0; i < sizeof(c) / sizeof(c[0]); i++) {
            int r = sourcetable_match_token(c[i].text, strlen(c[i].text),
                                            "rfsee.net", 2101, table, 4, NULL);
            char what[160];
            snprintf(what, sizeof(what), "%s (got %d)", c[i].what, r);
            check(r == c[i].want, what);
        }

        int m = 0;
        sourcetable_match_token("ntrip-analyser:mount=rfsee.net:2101/TWICE", 41,
                                "rfsee.net", 2101, table, 4, &m);
        check(m == 2, "an ambiguous token reports how many rows matched, for the log");

        const char nul[] = "ntrip-analyser:mount=rfsee.net:2101/RFS\0EE01";
        check(sourcetable_match_token(nul, sizeof(nul) - 1, "rfsee.net", 2101, table, 4, NULL)
                  == ST_TOKEN_MALFORMED,
              "a NUL inside the clipboard text is malformed, not a shorter name");

        size_t big = 64 * 1024;
        char *huge = (char *)malloc(big);
        memset(huge, 'A', big);
        memcpy(huge, ST_TOKEN_PREFIX, strlen(ST_TOKEN_PREFIX));
        check(sourcetable_match_token(huge, big, "rfsee.net", 2101, table, 4, NULL)
                  == ST_TOKEN_MALFORMED,
              "64 KB that starts like a token is refused");

        /* The case that needs the limit: valid in every other respect,
         * so only the length can refuse it. 64 KB of 'A' is malformed
         * anyway -- it has no ':' -- and a mutation run showed that case
         * passing with the limit deleted. */
        memset(huge, ' ', big);
        memcpy(huge, "ntrip-analyser:mount=rfsee.net:2101/RFSEE01", 43);
        check(sourcetable_match_token(huge, ST_TOKEN_MAX + 1, "rfsee.net", 2101, table, 4, NULL)
                  == ST_TOKEN_MALFORMED &&
              sourcetable_match_token(huge, ST_TOKEN_MAX, "rfsee.net", 2101, table, 4, NULL) == 1,
              "a valid token padded past ST_TOKEN_MAX is refused; at the limit it is read");
        free(huge);

        check(sourcetable_match_token(NULL, 0, "rfsee.net", 2101, table, 4, NULL) == ST_TOKEN_NONE &&
              sourcetable_match_token("ntrip-analyser:mount=rfsee.net:2101/RFSEE01", 43,
                                      NULL, 2101, table, 4, NULL) == ST_TOKEN_OTHER_CASTER &&
              sourcetable_match_token("ntrip-analyser:mount=rfsee.net:2101/RFSEE01", 43,
                                      "rfsee.net", 2101, NULL, 0, NULL) == ST_TOKEN_NOT_LISTED,
              "NULL text, no caster loaded, no sourcetable loaded: each refused by name");
    }

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
