/**
 * @file sourcetable.c
 * @brief Sourcetable parsing -- implementation.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * @copyright Apache License 2.0 with Commons Clause (see LICENSE for details)
 */

#include "core/sourcetable.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#  define strncasecmp _strnicmp
#endif

/**
 * @brief Copy field @p idx of a semicolon-separated record into @p dst.
 *
 * @return Pointer to the field within @p line, or NULL when the record
 *         ends before that field.
 */
static const char *field(const char *line, const char *end, int idx,
                         char *dst, size_t cap)
{
    const char *p = line;
    for (int i = 0; i < idx; i++) {
        const char *semi = memchr(p, ';', (size_t)(end - p));
        if (!semi) { if (dst && cap) dst[0] = '\0'; return NULL; }
        p = semi + 1;
    }
    const char *semi = memchr(p, ';', (size_t)(end - p));
    const char *stop = semi ? semi : end;

    if (dst && cap) {
        size_t n = (size_t)(stop - p);
        if (n >= cap) n = cap - 1;
        memcpy(dst, p, n);
        dst[n] = '\0';
    }
    return p;
}

/** @brief Field @p idx as a double; 0.0 when absent or unparsable. */
static double field_num(const char *line, const char *end, int idx)
{
    char buf[32];
    if (!field(line, end, idx, buf, sizeof(buf))) return 0.0;
    return atof(buf);
}

/**
 * @brief STR field 12 as 0 or 1; -1 when absent or anything else.
 *
 * Strict where the other numeric fields are lenient: atof() would read
 * "1x" as 1 and an empty field as 0, and an empty field read as "single
 * base" is a claim the caster never made.
 */
static int field_solution(const char *line, const char *end)
{
    /* Wide enough that truncation cannot cut "1   junk" down to a
     * clean "1   ": a field longer than this is rejected outright. */
    char buf[32];
    const char *f = field(line, end, 12, buf, sizeof(buf));
    if (!f) return -1;
    const char *semi = memchr(f, ';', (size_t)(end - f));
    if ((size_t)((semi ? semi : end) - f) >= sizeof(buf)) return -1;

    const char *s = buf;
    while (*s == ' ') s++;
    if ((s[0] != '0' && s[0] != '1')) return -1;
    const char *t = s + 1;
    while (*t == ' ') t++;
    return *t ? -1 : s[0] - '0';
}

int sourcetable_parse(const char *raw, SourcetableEntry *out, int max)
{
    if (!raw) return 0;

    int n = 0;
    const char *p = raw;

    while (*p) {
        const char *nl = strchr(p, '\n');
        const char *end = nl ? nl : p + strlen(p);

        /* Trim a trailing CR: casters send CRLF. */
        const char *line_end = end;
        if (line_end > p && line_end[-1] == '\r') line_end--;

        if ((size_t)(line_end - p) > 4 && strncmp(p, "STR;", 4) == 0) {
            if (!out) {
                n++;
            } else if (n < max) {
                SourcetableEntry *e = &out[n];
                memset(e, 0, sizeof(*e));

                field(p, line_end, 1, e->mountpoint,     sizeof(e->mountpoint));
                field(p, line_end, 2, e->identifier,     sizeof(e->identifier));
                field(p, line_end, 3, e->format,         sizeof(e->format));
                field(p, line_end, 4, e->format_details, sizeof(e->format_details));
                field(p, line_end, 6, e->nav_systems,    sizeof(e->nav_systems));
                field(p, line_end, 7, e->network,        sizeof(e->network));
                field(p, line_end, 8, e->country,        sizeof(e->country));

                e->carrier   = (int)field_num(p, line_end, 5);
                e->latitude  = field_num(p, line_end, 9);
                e->longitude = field_num(p, line_end, 10);
                e->nmea      = field_num(p, line_end, 11) != 0.0;
                e->solution  = field_solution(p, line_end);

                n++;
            } else {
                break;      /* caller's array is full */
            }
        }

        if (!nl) break;
        p = nl + 1;
    }
    return n;
}

int sourcetable_parse_types(const char *details, SourcetableType *out, int max)
{
    if (!details) return 0;

    int n = 0;
    const char *p = details;

    while (*p) {
        while (*p && (*p == ',' || *p == ' ')) p++;
        if (!*p) break;

        char *end = NULL;
        long type = strtol(p, &end, 10);
        if (end == p) {                 /* not a number: skip this token */
            while (*p && *p != ',') p++;
            continue;
        }
        p = end;

        double interval = 0.0;
        if (*p == '(') {
            p++;
            interval = strtod(p, &end);
            if (end != p) p = end;
            while (*p && *p != ')' && *p != ',') p++;
            if (*p == ')') p++;
        }

        if (type >= 1000 && type <= 4095) {
            if (!out) {
                n++;
            } else if (n < max) {
                out[n].type = (int)type;
                out[n].interval_s = interval;
                n++;
            } else {
                break;
            }
        }
    }
    return n;
}

unsigned sourcetable_navsys_mask(const char *navsys)
{
    if (!navsys) return 0;

    /* Casters spell these several ways; match the prefixes that
     * actually appear in the wild rather than an exhaustive table. */
    static const struct { const char *tag; int gnss; } names[] = {
        { "GPS",     1 }, { "GLO",     2 }, { "GAL",     3 },
        { "QZS",     4 }, { "BDS",     5 }, { "BEIDOU",  5 },
        { "COMPASS", 5 }, { "SBAS",    6 }, { "IRNSS",   7 },
        { "NAVIC",   7 },
    };

    unsigned mask = 0;
    for (const char *p = navsys; *p; ) {
        while (*p == '+' || *p == ' ' || *p == ',') p++;
        if (!*p) break;

        for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
            size_t n = strlen(names[i].tag);
            if (strncasecmp(p, names[i].tag, n) == 0) {
                mask |= 1u << names[i].gnss;
                break;
            }
        }
        while (*p && *p != '+' && *p != ' ' && *p != ',') p++;
    }
    return mask;
}

/* Network-service keywords as they appear in the format-details and
 * network fields, and (rule 2 of sourcetable_kind) in a mountpoint's
 * name. NRBY is onocoy's: NRBY_ADV, NRBY_B12 and NRBY_B15 are
 * nearest-base services, which their solution field calls single bases
 * (sourcetable fetched 2026-10-13; mountpoint-map.md). */
static const char *const k_network_keywords[] = {
    "VRS", "MAC", "NEAR", "NRBY", "FKP", "IMAX", "SSR",
};

/**
 * @brief @p token in @p hay as a whole word, ignoring case.
 *
 * @param digits_after Let digits follow the word: mountpoint names
 *        number their services (`NEAR4`), where free text does not.
 */
static bool contains_token_ci(const char *hay, const char *token, bool digits_after)
{
    size_t tlen = strlen(token);
    for (const char *p = hay; *p; p++) {
        if (strncasecmp(p, token, tlen) != 0) continue;

        unsigned char before = (p == hay) ? '\0' : (unsigned char)p[-1];
        unsigned char after  = (unsigned char)p[tlen];
        bool lhs_ok = (before == '\0') || !isalnum(before);
        bool rhs_ok = (after  == '\0') ||
                      (digits_after ? !isalpha(after) : !isalnum(after));
        if (lhs_ok && rhs_ok) return true;
    }
    return false;
}

/** @brief The first network keyword in @p text, or NULL. */
static const char *find_keyword(const char *text, bool digits_after)
{
    if (!text) return NULL;
    for (size_t i = 0; i < sizeof(k_network_keywords) / sizeof(k_network_keywords[0]); i++)
        if (contains_token_ci(text, k_network_keywords[i], digits_after))
            return k_network_keywords[i];
    return NULL;
}

/** @brief An RTCM 3 message that carries observations. */
static bool is_observation_type(int t)
{
    if ((t >= 1001 && t <= 1004) || (t >= 1009 && t <= 1012)) return true;
    /* MSM1-7 for GPS through NavIC: 1071-1077 ... 1131-1137. */
    return t >= 1071 && t <= 1137 && t % 10 >= 1 && t % 10 <= 7;
}

/**
 * @brief An RTCM 3 message only a station sends: its position (1005,
 *        1006), its antenna and receiver (1007, 1008, 1033), its GLONASS
 *        biases (1230).
 */
static bool is_station_type(int t)
{
    return (t >= 1005 && t <= 1008) || t == 1033 || t == 1230;
}

/**
 * @brief Advertises types, and none of them is from a station.
 *
 * A station is known by its observations *or* by the messages that
 * describe it: the M1 sample held 26 real bases whose operators listed
 * only `1005,1033` or `1230`, and calling those products would be
 * wrong. What is left -- ephemerides, SSR, proprietary blocks -- is a
 * product.
 *
 * Nothing advertised -- or only RTCM 2 numbers, which the type parser
 * does not keep -- says nothing, and is not a product.
 */
static bool advertises_no_station(const char *details)
{
    SourcetableType t[64];
    int n = sourcetable_parse_types(details, t, 64);
    if (n <= 0) return false;
    for (int i = 0; i < n; i++)
        if (is_observation_type(t[i].type) || is_station_type(t[i].type))
            return false;
    /* A list longer than the buffer is judged by its first 64: a
     * station type beyond that is possible, but the most any entry
     * advertised in the M1 sample was 22. */
    return true;
}

const char *sourcetable_network_keyword(const char *text)
{
    return find_keyword(text, false);
}

SourcetableKind sourcetable_kind(const SourcetableEntry *e, char *why, size_t cap)
{
    char scratch[1];
    if (!why || !cap) { why = scratch; cap = sizeof(scratch); }
    why[0] = '\0';

    if (!e) return ST_KIND_UNKNOWN;

    const char *kw = sourcetable_network_keyword(e->format_details);
    if (!kw) kw = sourcetable_network_keyword(e->network);
    if (kw) {
        snprintf(why, cap, "sourcetable names \"%s\"", kw);
        return ST_KIND_NETWORK;
    }
    kw = find_keyword(e->mountpoint, true);
    if (kw) {
        snprintf(why, cap, "mountpoint name says \"%s\"", kw);
        return ST_KIND_NETWORK;
    }
    if (advertises_no_station(e->format_details)) {
        snprintf(why, cap, "advertises no station messages");
        return ST_KIND_PRODUCT;
    }
    if (e->solution == 0) {
        snprintf(why, cap, "sourcetable states a single base");
        return ST_KIND_FIXED;
    }
    if (e->solution == 1) {
        /* Measured: casters mark products and physical stations this
         * way, and no network service among those seen. */
        snprintf(why, cap, "sourcetable states a network solution, "
                           "but names no network service");
        return ST_KIND_UNKNOWN;
    }
    snprintf(why, cap, "sourcetable does not say");
    return ST_KIND_UNKNOWN;
}

bool sourcetable_pos_usable(double lat, double lon)
{
    if (!isfinite(lat) || !isfinite(lon)) return false;
    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) return false;
    return !(lat == 0.0 && lon == 0.0);
}
