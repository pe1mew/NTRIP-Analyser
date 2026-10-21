/**
 * @file test_sourcetable.c
 * @brief The sourcetable parser, what an entry says it is, and distance
 *        and direction on the Earth.
 *
 * `sourcetable_parse()` is shared by the GUI, the CLI and the Android
 * bridge and had never been tested: the only things that had exercised
 * it were live casters, which are polite. A sourcetable is text from a
 * server this program does not control, so the cases here are mostly
 * impolite ones -- records cut short, fields that hold the wrong thing,
 * positions that are not positions.
 *
 * Three things are pinned beside the parser
 * (design/work-items/archive/mountpoint-map.md, M1):
 *
 *   - The STR solution field is read strictly. An empty field read as
 *     "single base" would be a claim the caster never made.
 *   - Classification. The keyword rule moved here from the GUI, and its
 *     reason for being whole-word -- reading a fixed base as a network
 *     service suppresses the very position checks that would find its
 *     fault -- is pinned with the words that would break it.
 *   - Distance and bearing, against cases worked by hand rather than
 *     against the same formula written twice.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
#include "core/sourcetable.h"
#include "core/geo.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

static int near(double a, double b, double tol) { return fabs(a - b) <= tol; }

/** Parse a table expected to hold exactly one STR record. */
static int one(const char *raw, SourcetableEntry *e)
{
    memset(e, 0x5A, sizeof(*e));     /* garbage, so "left empty" is checked */
    return sourcetable_parse(raw, e, 1);
}

/* One degree of arc on the mean sphere: R * pi / 180. */
#define ONE_DEG_M (GEO_EARTH_RADIUS_M * M_PI / 180.0)

int main(void)
{
    SourcetableEntry e;

    /* ── 1. A complete record, as a caster sends it ───────────────── */
    {
        const char *raw =
            "CAS;caster.example;2101;Example;Op;0;NLD;52.00;6.00;0.0.0.0;0;http://x\r\n"
            "NET;EXNET;Op;B;N;http://x;none;none;none\r\n"
            "STR;APEL00NLD0;Apeldoorn;RTCM 3.3;1005(10),1077(1),1087(1);2;"
            "GPS+GLO+GAL+BDS;EXNET;NLD;52.2123;5.9654;0;0;Trimble;none;B;N;9600;\r\n"
            "ENDSOURCETABLE\r\n";

        check(sourcetable_parse(raw, NULL, 0) == 1,
              "count mode sees the one STR record and skips CAS, NET, END");
        check(one(raw, &e) == 1, "and parses it");
        check(strcmp(e.mountpoint, "APEL00NLD0") == 0 &&
              strcmp(e.identifier, "Apeldoorn") == 0 &&
              strcmp(e.format, "RTCM 3.3") == 0 &&
              strcmp(e.format_details, "1005(10),1077(1),1087(1)") == 0 &&
              strcmp(e.nav_systems, "GPS+GLO+GAL+BDS") == 0 &&
              strcmp(e.network, "EXNET") == 0 &&
              strcmp(e.country, "NLD") == 0,
              "text fields land in their places, CR trimmed");
        check(e.carrier == 2 && e.nmea == false, "carrier and nmea");
        check(e.latitude == 52.2123 && e.longitude == 5.9654,
              "position at full precision, not as a list shows it");
        check(e.solution == 0, "solution 0: a single base");
    }

    /* ── 2. The solution field is read strictly ───────────────────── */
    {
        static const struct { const char *field; int want; const char *what; } c[] = {
            { "1",    1, "\"1\" is a network" },
            { " 1 ",  1, "spaces around the digit are tolerated" },
            { "",    -1, "an empty field says nothing, not \"single base\"" },
            { "2",   -1, "a digit the format does not define" },
            { "1x",  -1, "a digit followed by anything else" },
            { "01",  -1, "a leading zero is not a stated value" },
            { "-1",  -1, "a negative number" },
            { "1                                        x", -1,
              "a field long enough to be truncated to a clean \"1\"" },
        };
        for (size_t i = 0; i < sizeof(c) / sizeof(c[0]); i++) {
            char raw[256];
            snprintf(raw, sizeof(raw),
                     "STR;M;I;RTCM 3.3;1005(10);2;GPS;N;NLD;52.1;6.1;1;%s;G;none;B;N;0;\n",
                     c[i].field);
            one(raw, &e);
            check(e.solution == c[i].want, c[i].what);
        }

        one("STR;M;I;RTCM 3.3;1005(10);2;GPS;N;NLD;52.1;6.1;1\n", &e);
        check(e.solution == -1 && e.nmea == true,
              "a record that ends before the solution field: -1, nmea still read");
    }

    /* ── 3. Records cut short, and tables that are not tidy ───────── */
    {
        check(one("STR;SHORT;Only an identifier\n", &e) == 1,
              "a record of three fields still yields an entry");
        check(strcmp(e.mountpoint, "SHORT") == 0 && e.format[0] == '\0' &&
              e.network[0] == '\0' && e.latitude == 0.0 && e.longitude == 0.0 &&
              e.solution == -1,
              "and what it lacks is empty, zero or -1 -- not the garbage before");

        check(one("STR;LAST;I;RTCM 3.3;;2;GPS;N;NLD;52.1;6.1;0;0", &e) == 1 &&
              strcmp(e.mountpoint, "LAST") == 0 && e.solution == 0,
              "the last line needs no newline");

        char longname[300];
        memset(longname, 'A', sizeof(longname));
        memcpy(longname, "STR;", 4);
        memcpy(longname + 250, ";I;F\n", 6);
        one(longname, &e);
        check(strlen(e.mountpoint) == sizeof(e.mountpoint) - 1,
              "an over-long mountpoint is truncated and terminated, not overrun");

        const char nul_inside[] = "STR;A;I\n\0STR;B;I\n";
        check(sourcetable_parse(nul_inside, NULL, 0) == 1,
              "an embedded NUL ends the table");

        const char *three = "STR;A;\nSTR;B;\nSTR;C;\n";
        SourcetableEntry two[2];
        check(sourcetable_parse(three, two, 2) == 2 &&
              strcmp(two[1].mountpoint, "B") == 0,
              "a full array stops the parse at its capacity");
        check(sourcetable_parse(three, NULL, 0) == 3,
              "while count mode reports the true total");

        check(sourcetable_parse(NULL, NULL, 0) == 0 &&
              sourcetable_parse("", NULL, 0) == 0 &&
              sourcetable_parse("STR;", NULL, 0) == 0,
              "NULL, empty, and a bare \"STR;\" yield nothing");

        one("STR;N;I;F;D;2;GPS;N;NLD;nan;inf;0;0\n", &e);
        check(!sourcetable_pos_usable(e.latitude, e.longitude),
              "\"nan\" and \"inf\" parse, and the position check refuses them");
    }

    /* ── 4. Which positions can be used ───────────────────────────── */
    {
        check(sourcetable_pos_usable(52.2, 5.9), "an ordinary position");
        check(!sourcetable_pos_usable(0.0, 0.0), "0,0 is \"not stated\"");
        check(sourcetable_pos_usable(0.0, 6.0) && sourcetable_pos_usable(52.0, 0.0),
              "but the equator and the prime meridian each alone are places");
        check(sourcetable_pos_usable(90.0, 180.0) && sourcetable_pos_usable(-90.0, -180.0),
              "the limits themselves are valid");
        check(!sourcetable_pos_usable(90.0001, 6.0) && !sourcetable_pos_usable(52.0, 180.0001) &&
              !sourcetable_pos_usable(-90.0001, 6.0) && !sourcetable_pos_usable(52.0, -180.0001),
              "just past them is not, and is refused rather than clamped");
        check(!sourcetable_pos_usable(1e308, 6.0) && !sourcetable_pos_usable(52.0, -1e308),
              "nor is a huge number");
        check(!sourcetable_pos_usable(nan(""), 6.0) && !sourcetable_pos_usable(52.0, INFINITY),
              "nor NaN or infinity");
    }

    /* ── 5. What an entry says it is ──────────────────────────────── */
    {
        char why[96];
        SourcetableEntry k;
        memset(&k, 0, sizeof(k));
        k.solution = -1;

        check(sourcetable_kind(&k, why, sizeof(why)) == ST_KIND_UNKNOWN &&
              strcmp(why, "sourcetable does not say") == 0,
              "no keyword, no solution field: unknown, and it says so");

        k.solution = 0;
        check(sourcetable_kind(&k, why, sizeof(why)) == ST_KIND_FIXED,
              "solution 0: fixed");
        k.solution = 1;
        check(sourcetable_kind(&k, why, sizeof(why)) == ST_KIND_UNKNOWN,
              "solution 1 alone is not believed: casters use it for products "
              "and physical stations (measured, M1)");

        /* Products: types advertised, none of them observations. */
        snprintf(k.format_details, sizeof(k.format_details),
                 "1019,1020,1042,1045,1046");
        check(sourcetable_kind(&k, why, sizeof(why)) == ST_KIND_PRODUCT,
              "an ephemeris stream is a product, whatever its solution field");
        snprintf(k.format_details, sizeof(k.format_details),
                 "1059(10),1060(10),1300(60),4076_023(10)");
        check(sourcetable_kind(&k, why, sizeof(why)) == ST_KIND_PRODUCT,
              "so is SSR, proprietary 4076 included");
        k.solution = 1;
        snprintf(k.format_details, sizeof(k.format_details),
                 "1006(10),1019,1020,1077(1),1087(1)");
        check(sourcetable_kind(&k, why, sizeof(why)) == ST_KIND_UNKNOWN,
              "one MSM among ephemerides makes it a station, not a product");
        k.solution = 0;
        snprintf(k.format_details, sizeof(k.format_details), "1004(1),1012(1),1006(10)");
        check(sourcetable_kind(&k, why, sizeof(why)) == ST_KIND_FIXED,
              "legacy observations count as observations");
        snprintf(k.format_details, sizeof(k.format_details), "1(1),3(10),18(1),19(1)");
        check(sourcetable_kind(&k, why, sizeof(why)) == ST_KIND_FIXED,
              "RTCM 2 numbers say nothing either way");
        snprintf(k.format_details, sizeof(k.format_details), "1078(1),1080(1),1138(1)");
        check(sourcetable_kind(&k, why, sizeof(why)) == ST_KIND_PRODUCT,
              "numbers beside the MSM ranges are not MSM");
        /* Both seen on real bases in the M1 sample. */
        snprintf(k.format_details, sizeof(k.format_details), "1005,1033");
        check(sourcetable_kind(&k, why, sizeof(why)) == ST_KIND_FIXED,
              "a base that lists only its position and antenna is still a base");
        snprintf(k.format_details, sizeof(k.format_details), "1230(10)");
        check(sourcetable_kind(&k, why, sizeof(why)) == ST_KIND_FIXED,
              "so is one that lists only its GLONASS biases");
        k.format_details[0] = '\0';

        /* The mountpoint name, where services are numbered. */
        /* NRBY_* as onocoy lists them (2026-10-13): nearest-base
         * services with no position, which the solution field calls
         * single bases. */
        static const char *const svc[] = { "NEAR", "NEAR4", "CORS_VRS", "vrs3_rtcm32",
                                           "NRBY_ADV", "NRBY_B12", "NRBY_B15" };
        for (size_t i = 0; i < sizeof(svc) / sizeof(svc[0]); i++) {
            char what[96];
            snprintf(k.mountpoint, sizeof(k.mountpoint), "%s", svc[i]);
            snprintf(what, sizeof(what), "mountpoint \"%s\" is a network service", svc[i]);
            check(sourcetable_kind(&k, why, sizeof(why)) == ST_KIND_NETWORK, what);
        }
        /* Real station names from the M1 sample that contain a keyword's
         * letters: a town, and a generated id. */
        static const char *const stn[] = { "MACO", "TMAC", "KENMACMAC1", "NEARBY" };
        for (size_t i = 0; i < sizeof(stn) / sizeof(stn[0]); i++) {
            char what[96];
            snprintf(k.mountpoint, sizeof(k.mountpoint), "%s", stn[i]);
            snprintf(what, sizeof(what), "mountpoint \"%s\" is not", stn[i]);
            check(sourcetable_kind(&k, why, sizeof(why)) == ST_KIND_FIXED, what);
        }
        check(sourcetable_network_keyword("NEAR4") == NULL,
              "while in free text a trailing digit still breaks the word, as before");
        k.mountpoint[0] = '\0';

        k.solution = 0;
        snprintf(k.format_details, sizeof(k.format_details), "1004(1),1012(1) VRS");
        check(sourcetable_kind(&k, why, sizeof(why)) == ST_KIND_NETWORK &&
              strcmp(why, "sourcetable names \"VRS\"") == 0,
              "a keyword outranks a solution field that says single base");

        k.format_details[0] = '\0';
        snprintf(k.network, sizeof(k.network), "net-near");
        check(sourcetable_kind(&k, why, sizeof(why)) == ST_KIND_NETWORK,
              "in the network field, in lower case, after a hyphen");

        static const char *const not_kw[] = { "LINEAR", "MACHINE", "VRSX", "XFKP", "SSR2",
                                              "NRBYX" };
        for (size_t i = 0; i < sizeof(not_kw) / sizeof(not_kw[0]); i++) {
            char what[96];
            snprintf(what, sizeof(what), "\"%s\" is not a keyword", not_kw[i]);
            check(sourcetable_network_keyword(not_kw[i]) == NULL, what);
        }
        static const char *const kw[] = { "VRS", "1077(1) MAC", "Net;NEAR;", "fkp", "(IMAX)", "SSR" };
        for (size_t i = 0; i < sizeof(kw) / sizeof(kw[0]); i++) {
            char what[96];
            snprintf(what, sizeof(what), "\"%s\" carries a keyword", kw[i]);
            check(sourcetable_network_keyword(kw[i]) != NULL, what);
        }
        check(sourcetable_network_keyword("Nearest;") == NULL,
              "\"Nearest\" is a word of its own, not NEAR");

        check(sourcetable_network_keyword(NULL) == NULL &&
              sourcetable_network_keyword("") == NULL,
              "NULL and empty text carry no keyword");
        check(sourcetable_kind(NULL, why, sizeof(why)) == ST_KIND_UNKNOWN,
              "a NULL entry is unknown");
        check(sourcetable_kind(&k, NULL, 0) == ST_KIND_NETWORK,
              "the reason is optional");
        char tiny[1];
        check(sourcetable_kind(&k, tiny, sizeof(tiny)) == ST_KIND_NETWORK && tiny[0] == '\0',
              "a one-byte reason buffer is terminated, not overrun");
    }

    /* ── 6. Distance and bearing, worked by hand ──────────────────── */
    {
        check(near(geo_distance_m(52.0, 6.0, 53.0, 6.0), ONE_DEG_M, 0.01) &&
              near(geo_bearing_deg(52.0, 6.0, 53.0, 6.0), 0.0, 1e-9),
              "one degree due north: R*pi/180, bearing 0");
        check(near(geo_bearing_deg(53.0, 6.0, 52.0, 6.0), 180.0, 1e-9),
              "due south: 180");
        check(near(geo_distance_m(0.0, 6.0, 0.0, 7.0), ONE_DEG_M, 0.01) &&
              near(geo_bearing_deg(0.0, 6.0, 0.0, 7.0), 90.0, 1e-9) &&
              near(geo_bearing_deg(0.0, 7.0, 0.0, 6.0), 270.0, 1e-9),
              "along the equator: one degree, east 90, west 270");
        check(near(geo_distance_m(0.0, 179.5, 0.0, -179.5), ONE_DEG_M, 0.01) &&
              near(geo_bearing_deg(0.0, 179.5, 0.0, -179.5), 90.0, 1e-9),
              "across the antimeridian: one degree east, not 359 west");

        /* Haversine is ill-conditioned here: a is within an ulp of 1, and
         * sqrt(1 - a) turns that ulp into ~1e-8 rad. Measured 0.13 m
         * short on the second pair (gcc -O2) -- noise at 20 000 km, and
         * what matters is that it never becomes NaN. */
        double half = M_PI * GEO_EARTH_RADIUS_M;
        double d1 = geo_distance_m(0.0, 0.0, 0.0, 180.0);
        double d2 = geo_distance_m(10.0, 20.0, -10.0, -160.0);
        check(near(d1, half, 1.0) && near(d2, half, 1.0) && !isnan(d2),
              "antipodal pairs: half the circumference to a metre, never NaN");

        check(geo_distance_m(52.2, 5.9, 52.2, 5.9) == 0.0 &&
              geo_bearing_deg(52.2, 5.9, 52.2, 5.9) == 0.0,
              "a point to itself: no distance, bearing 0");

        double b = geo_bearing_deg(52.0, 6.0, 52.0 + 1e-12, 6.0 - 1e-9);
        check(b >= 0.0 && b < 360.0, "a bearing just west of north stays below 360");

        check(near(geo_distance_m(52.0, 6.0, 51.0, 4.0),
                   geo_distance_m(51.0, 4.0, 52.0, 6.0), 1e-6),
              "distance is symmetric");
    }

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
