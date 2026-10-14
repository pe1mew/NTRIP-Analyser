/**
 * @file test_station_class.c
 * @brief Fixed base or network service, decided once for every frontend.
 *
 * The case that made this a core function: a tester drove from the
 * Netherlands to Switzerland on Centipede's NEAR. With the phone's
 * screen off the uplink kept his Dutch position; when he opened the
 * app in Switzerland the new position went out and NEAR handed over
 * once, to a base 47 km away. The app's own copy of the rule -- gate
 * test or 150 m -- called that a fixed base moving, "corrections are
 * unreliable", because it never asked the sourcetable, which names a
 * nearest-base service (handover-on-the-phone.md, H9).
 *
 * Every rule of station_service() is driven here, in the order it
 * decides, with positions worked from real places.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
#include "core/station_class.h"
#include "core/vrs_check.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

static SourcetableEntry entry(const char *mp, const char *details, const char *network)
{
    SourcetableEntry e;
    memset(&e, 0, sizeof(e));
    snprintf(e.mountpoint, sizeof(e.mountpoint), "%s", mp);
    snprintf(e.format, sizeof(e.format), "RTCM 3.2");
    snprintf(e.format_details, sizeof(e.format_details), "%s", details);
    snprintf(e.network, sizeof(e.network), "%s", network);
    e.solution = 0;                     /* "single base", as NEAR's says */
    return e;
}

int main(void)
{
    char why[160];

    /* RFSEE01 as registered, and two points measured from it. */
    const double arp_lat = 52.2115167, arp_lon = 5.9837064;
    const double near_lat = 52.2120, near_lon = 5.9837;     /* ~54 m north */
    const double far_lat  = 51.9877, far_lon  = 5.9515;     /* ~25 km south */

    /* ── 1. The tester's case: NEAR, base 47 km away, no gate test ─── */
    {
        SourcetableEntry e = entry("NEAR", "1004(1),1005(10),1077(1)", "Centipede");
        StationService s = station_service(&e, VRS_GATE_UNTESTED,
                                           true, arp_lat, arp_lon,
                                           true, far_lat, far_lon, why, sizeof(why));
        check(s == STATION_SERVICE_NETWORK,
              "NEAR with its base 25 km from the rover is a network service");
        check(strstr(why, "NEAR") != NULL,
              "and the reason names the sourcetable's evidence");
        printf("      why: %s\n", why);
    }

    /* ── 2. The gate test outranks everything ──────────────────────── */
    {
        SourcetableEntry e = entry("RFSEE01", "1005(10),1077(1)", "RFSee");
        check(station_service(&e, VRS_GATE_GATED, true, arp_lat, arp_lon,
                              true, far_lat, far_lon, why, sizeof(why))
                  == STATION_SERVICE_NETWORK &&
              strstr(why, "gate") != NULL,
              "a stream that stopped with the GGA is a network service");
        check(station_service(NULL, VRS_GATE_GATED, false, 0, 0, false, 0, 0,
                              why, sizeof(why)) == STATION_SERVICE_NETWORK,
              "even with no entry and no reference position yet");
    }

    /* ── 3. A keyword in the details or network field, as the GUI had ─ */
    {
        SourcetableEntry e = entry("CORS01", "1004(1) VRS", "NET");
        check(station_service(&e, VRS_GATE_UNTESTED, true, arp_lat, arp_lon,
                              true, far_lat, far_lon, why, sizeof(why))
                  == STATION_SERVICE_NETWORK,
              "a VRS keyword in the details is a network service");
    }

    /* ── 4. The reference position sitting on the rover ──────────── */
    {
        SourcetableEntry e = entry("MP", "1005(10),1077(1)", "NET");
        check(station_service(&e, VRS_GATE_UNTESTED, true, arp_lat, arp_lon,
                              true, near_lat, near_lon, why, sizeof(why))
                  == STATION_SERVICE_NETWORK &&
              strstr(why, " m from the position sent") != NULL,
              "a reference position ~54 m from the GGA is a network service");
        printf("      why: %s\n", why);
        check(station_service(&e, VRS_GATE_UNTESTED, true, arp_lat, arp_lon,
                              false, near_lat, near_lon, why, sizeof(why))
                  == STATION_SERVICE_FIXED,
              "but not when no GGA is sent: there is nothing to sit on");
        check(station_service(&e, VRS_GATE_UNTESTED, true, arp_lat, arp_lon,
                              true, 0.0, 0.0, why, sizeof(why))
                  == STATION_SERVICE_FIXED,
              "nor when the position sent is 0,0, the 'not set' convention");
    }

    /* ── 5. A fixed base, and not knowing ──────────────────────────── */
    {
        SourcetableEntry e = entry("RFSEE01", "1005(10),1077(1)", "RFSee");
        check(station_service(&e, VRS_GATE_NOT_GATED, true, arp_lat, arp_lon,
                              true, far_lat, far_lon, why, sizeof(why))
                  == STATION_SERVICE_FIXED,
              "a named base, 25 km from the rover, kept streaming: fixed");
        check(station_service(&e, VRS_GATE_UNTESTED, false, 0, 0,
                              true, far_lat, far_lon, why, sizeof(why))
                  == STATION_SERVICE_UNKNOWN,
              "no reference position and no other evidence: unknown");
        check(station_service(NULL, VRS_GATE_UNTESTED, true, arp_lat, arp_lon,
                              true, far_lat, far_lon, why, sizeof(why))
                  == STATION_SERVICE_FIXED,
              "no sourcetable entry: decided on the position alone");
    }

    /* ── 6. A nearest-base service that keeps streaming without GGA ── */
    {
        SourcetableEntry e = entry("NEAR4", "1005(10),1074(1)", "Centipede");
        check(station_service(&e, VRS_GATE_NOT_GATED, true, arp_lat, arp_lon,
                              true, far_lat, far_lon, why, sizeof(why))
                  == STATION_SERVICE_NETWORK,
              "NOT GATED does not overrule the sourcetable's network service");
    }

    check(strcmp(station_service_name(STATION_SERVICE_NETWORK), "network") == 0 &&
          strcmp(station_service_name(STATION_SERVICE_FIXED), "fixed") == 0 &&
          strcmp(station_service_name(STATION_SERVICE_UNKNOWN), "unknown") == 0,
          "the document tokens");
    check(station_service(NULL, 0, false, 0, 0, false, 0, 0, NULL, 0)
              == STATION_SERVICE_UNKNOWN,
          "a NULL reason buffer is allowed");

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
