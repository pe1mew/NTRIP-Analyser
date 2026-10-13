/**
 * @file test_bridge_sourcetable.c
 * @brief The sourcetable as the Android app receives it.
 *
 * `bridge_sourcetable_to_json` is the document the phone's list and map
 * decode (mountpoint-map.md, M6a). Every figure in it about an entry --
 * kind, whether the position is usable, distance and bearing -- is the
 * core's, so the app computes none of them; this pins that the bridge
 * carries those figures faithfully, and that nothing a caster can put in
 * a record makes the document unreadable.
 *
 * Before M6a the bridge escaped only `"`, `\` and newline, and printed
 * the position with `%f`: a control character in a name, or a latitude
 * of "nan", produced text the app could not decode, which it reports as
 * an unreachable caster. Cases 3 and 4 are those two.
 *
 * Every document is parsed back with cJSON, as the app parses it with
 * kotlinx; no case inspects the text by eye.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
#include "ntrip_bridge.h"
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

#define CAP 65536
static char doc[CAP];

/** The document for @p raw, parsed; NULL when refused or unparseable. */
static cJSON *json_of(const char *raw, double olat, double olon)
{
    int n = bridge_sourcetable_to_json(raw, olat, olon, doc, sizeof(doc));
    if (n < 0) return NULL;
    if ((size_t)n != strlen(doc)) return NULL;
    return cJSON_Parse(doc);
}

static const cJSON *entry_at(const cJSON *root, int i)
{
    return cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root, "entries"), i);
}

static const cJSON *field(const cJSON *e, const char *key)
{
    return cJSON_GetObjectItemCaseSensitive(e, key);
}

static int str_is(const cJSON *v, const char *s)
{
    return cJSON_IsString(v) && strcmp(v->valuestring, s) == 0;
}

static int num_near(const cJSON *v, double want, double tol)
{
    return cJSON_IsNumber(v) && fabs(v->valuedouble - want) <= tol;
}

/* An ordinary fixed base, as test_sourcetable.c's first case has it. */
#define STR_APEL \
    "STR;APEL00NLD0;Apeldoorn;RTCM 3.3;1005(10),1077(1),1087(1);2;" \
    "GPS+GLO+GAL+BDS;EXNET;NLD;53.0000;5.0000;0;0;Trimble;none;B;N;9600;\r\n"

int main(void)
{
    /* ── 1. An ordinary entry, every field, from a hand-worked origin ── */
    {
        /* 52 N 5 E to 53 N 5 E: due north, one degree of a sphere of
         * radius 6371008.8 m, 2*pi*R/360 = 111195.08 m. (A first draft
         * of this test said 111194.93 -- the figure for 6371 km, not the
         * mean radius geo.h uses.) */
        cJSON *root = json_of(STR_APEL "ENDSOURCETABLE\r\n", 52.0, 5.0);
        const cJSON *e = entry_at(root, 0);
        check(root && e, "a one-entry table is a document with one entry");
        check(str_is(field(e, "mountpoint"), "APEL00NLD0") &&
              str_is(field(e, "identifier"), "Apeldoorn") &&
              str_is(field(e, "format"), "RTCM 3.3") &&
              str_is(field(e, "nav_systems"), "GPS+GLO+GAL+BDS") &&
              str_is(field(e, "network"), "EXNET") &&
              str_is(field(e, "country"), "NLD"),
              "the text fields arrive as the caster wrote them");
        check(num_near(field(e, "lat"), 53.0, 1e-9) &&
              num_near(field(e, "lon"), 5.0, 1e-9) &&
              cJSON_IsTrue(field(e, "usable")),
              "a real position is carried and marked usable");
        check(str_is(field(e, "kind"), "fixed") &&
              cJSON_IsString(field(e, "kind_why")) &&
              field(e, "kind_why")->valuestring[0] != '\0',
              "the kind is the core's, with its reason");
        const cJSON *d = field(e, "distance_m"), *b = field(e, "bearing_deg");
        check(num_near(d, 111195.08, 0.1) && num_near(b, 0.0, 0.05),
              "one degree due north: 111195.1 m at 0 degrees");
        if (!(num_near(d, 111195.08, 0.1) && num_near(b, 0.0, 0.05)))
            printf("      got distance %s, bearing %s\n",
                   cJSON_PrintUnformatted(d), cJSON_PrintUnformatted(b));
        cJSON_Delete(root);
    }

    /* ── 2. No origin: no distance, and the entry otherwise whole ───── */
    {
        static const double none[][2] = { { 0.0, 0.0 }, { NAN, NAN }, { 91.0, 5.0 } };
        int all = 1;
        for (size_t i = 0; i < sizeof(none) / sizeof(none[0]); i++) {
            cJSON *root = json_of(STR_APEL, none[i][0], none[i][1]);
            const cJSON *e = entry_at(root, 0);
            all = all && e &&
                  cJSON_IsNull(field(e, "distance_m")) &&
                  cJSON_IsNull(field(e, "bearing_deg")) &&
                  cJSON_IsTrue(field(e, "usable"));
            cJSON_Delete(root);
        }
        check(all, "an origin at 0,0, NaN or latitude 91 is none: distance and bearing null");
    }

    /* ── 3. Positions that are no place ─────────────────────────────── */
    {
        const char *raw =
            "STR;ZERO;I;RTCM 3.3;1005(10),1077(1);2;GPS;N;NLD;0;0;0;0\r\n"
            "STR;NANPOS;I;RTCM 3.3;1005(10),1077(1);2;GPS;N;NLD;nan;inf;0;0\r\n"
            "STR;FAR;I;RTCM 3.3;1005(10),1077(1);2;GPS;N;NLD;400;5;0;0\r\n";
        cJSON *root = json_of(raw, 52.0, 5.0);
        check(root != NULL,
              "a latitude of nan still gives a document the app can read");
        int all = 1;
        for (int i = 0; i < 3; i++) {
            const cJSON *e = entry_at(root, i);
            all = all && e &&
                  cJSON_IsFalse(field(e, "usable")) &&
                  num_near(field(e, "lat"), 0.0, 0.0) &&
                  num_near(field(e, "lon"), 0.0, 0.0) &&
                  cJSON_IsNull(field(e, "distance_m"));
        }
        check(all, "0,0, nan/inf and latitude 400: unusable, written 0,0, no distance");
        cJSON_Delete(root);
    }

    /* ── 4. Hostile text: still a document, and the text survives ───── */
    {
        const char *raw =
            "STR;Q\"B\\S;</script><img src=x onerror=1>;RTCM 3.3;1005(10),1077(1);2;GPS;N;NLD;52.1;6.1;0;0\r\n"
            "STR;CTRL\x01\x1f;bad \xff byte;RTCM 3.3;1005(10),1077(1);2;GPS;N;NLD;52.1;6.1;0;0\r\n"
            "STR;Tombeb\xc5\x93uf;caf\xc3\xa9;RTCM 3.3;1005(10),1077(1);2;GPS;N;FRA;44.4;0.6;0;0\r\n";
        int n = bridge_sourcetable_to_json(raw, 52.0, 5.0, doc, sizeof(doc));
        cJSON *root = n >= 0 ? cJSON_Parse(doc) : NULL;
        check(root != NULL, "quotes, backslashes, control bytes and invalid UTF-8 still parse");

        int raw_ctrl = 0, raw_lt = 0;
        for (const unsigned char *p = (const unsigned char *)doc; *p; p++) {
            if (*p < 0x20) raw_ctrl = 1;
            if (*p == '<') raw_lt = 1;
        }
        check(!raw_ctrl && !raw_lt,
              "no raw control character and no raw '<' in the document text");

        const cJSON *e0 = entry_at(root, 0), *e1 = entry_at(root, 1), *e2 = entry_at(root, 2);
        check(str_is(field(e0, "mountpoint"), "Q\"B\\S") &&
              str_is(field(e0, "identifier"), "</script><img src=x onerror=1>"),
              "quote, backslash and markup come back as the same text");
        check(str_is(field(e1, "mountpoint"), "CTRL\x01\x1f"),
              "control characters come back as themselves, escaped in transit");
        check(str_is(field(e1, "identifier"), "bad \xef\xbf\xbd byte"),
              "an invalid UTF-8 byte becomes U+FFFD, never a guess");
        check(str_is(field(e2, "mountpoint"), "Tombeb\xc5\x93uf") &&
              str_is(field(e2, "identifier"), "caf\xc3\xa9"),
              "real UTF-8 (Tombeb\xc5\x93uf, from the M1 sample) passes unchanged");
        cJSON_Delete(root);
    }

    /* ── 5. Refusals: nothing, or a document that does not fit ──────── */
    {
        check(bridge_sourcetable_to_json(NULL, 52.0, 5.0, doc, sizeof(doc)) < 0,
              "no table: refused");
        check(bridge_sourcetable_to_json("<html>401 Unauthorized</html>\r\n",
                                         52.0, 5.0, doc, sizeof(doc)) < 0,
              "an error page in place of a table: refused, as before M6a");

        int need = bridge_sourcetable_to_json(STR_APEL, 52.0, 5.0, doc, sizeof(doc));
        int cut_ok = need > 0;
        for (int cap = 32; cut_ok && cap <= need; cap++) {
            char small[CAP];
            memset(small, 'x', sizeof(small));
            int got = bridge_sourcetable_to_json(STR_APEL, 52.0, 5.0, small, (size_t)cap);
            cut_ok = got < 0 && small[0] == '\0';
        }
        check(cut_ok, "every buffer from 32 bytes to one short: refused and empty, never cut");
        check(bridge_sourcetable_to_json(STR_APEL, 52.0, 5.0, doc, (size_t)need + 1) == need,
              "and exactly enough fits");
    }

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
