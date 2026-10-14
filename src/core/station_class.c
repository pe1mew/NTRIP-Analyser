/**
 * @file station_class.c
 * @brief Fixed base or network service -- implementation.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
#include "core/station_class.h"
#include "core/vrs_check.h"
#include "core/geo.h"

#include <stdio.h>

StationService station_service(const SourcetableEntry *e, int vrs_gate,
                               bool arp_valid, double arp_lat, double arp_lon,
                               bool gga_sent, double gga_lat, double gga_lon,
                               char *why, size_t cap)
{
    char scratch[160];
    if (!why || !cap) { why = scratch; cap = sizeof(scratch); }
    why[0] = '\0';

    if (vrs_gate == VRS_GATE_GATED) {
        snprintf(why, cap, "the stream stopped when the GGA did (gate test)");
        return STATION_SERVICE_NETWORK;
    }

    if (e) {
        char kwhy[128];
        if (sourcetable_kind(e, kwhy, sizeof(kwhy)) == ST_KIND_NETWORK) {
            snprintf(why, cap, "sourcetable: %s", kwhy);
            return STATION_SERVICE_NETWORK;
        }
    }

    if (!arp_valid) {
        snprintf(why, cap, "no reference position (1005/1006) yet");
        return STATION_SERVICE_UNKNOWN;
    }

    /* 0,0 is "not set", as everywhere a position comes from the user. */
    if (gga_sent && sourcetable_pos_usable(gga_lat, gga_lon)) {
        double d = geo_distance_m(arp_lat, arp_lon, gga_lat, gga_lon);
        if (d < STATION_CLASS_ON_ROVER_M) {
            snprintf(why, cap,
                     "reference position sits %.0f m from the position sent", d);
            return STATION_SERVICE_NETWORK;
        }
    }

    snprintf(why, cap, "reference position independent of the position sent");
    return STATION_SERVICE_FIXED;
}

const char *station_service_name(StationService s)
{
    switch (s) {
        case STATION_SERVICE_FIXED:   return "fixed";
        case STATION_SERVICE_NETWORK: return "network";
        default:                      return "unknown";
    }
}
