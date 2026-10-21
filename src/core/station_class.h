/**
 * @file station_class.h
 * @brief Is this mountpoint a fixed base or a network service?
 *
 * The question decides how a moving reference position reads. A fixed
 * base whose broadcast position moves is a fault: corrections from it
 * are unreliable. A network service whose reference position moves is
 * doing its job -- a nearest-base service hands over as the rover
 * travels, a virtual reference station follows the rover by definition.
 * The same hand-over count says one thing or the other, never both.
 *
 * Decided here, once, for every frontend. Until 2026-10-14 the Win32
 * GUI and the Android app each carried their own copy of the rule, and
 * neither asked what the core's sourcetable classification already
 * knew: a tester's drive from the Netherlands to Switzerland on
 * Centipede's NEAR read "a fixed base should not move; corrections are
 * unreliable" for the one hand-over a nearest-base service owed him
 * (design/work-items/archive/handover-on-the-phone.md, H9).
 *
 * This is a classification, not a verdict on the station: it chooses
 * which sentence a hand-over gets, and the reason always states the
 * evidence.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
#ifndef STATION_CLASS_H
#define STATION_CLASS_H

#include <stdbool.h>
#include <stddef.h>

#include "core/sourcetable.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What kind of service a mountpoint is, as far as the evidence goes. */
typedef enum {
    STATION_SERVICE_UNKNOWN = 0,  /**< not enough evidence yet              */
    STATION_SERVICE_FIXED   = 1,  /**< one physical base at a fixed place   */
    STATION_SERVICE_NETWORK = 2,  /**< VRS, nearest-base, MAC: it moves     */
} StationService;

/** @brief Distance under which a reference position "sits on" the rover. */
/*
 * 150 m: comfortably above a virtual station's placement error, far
 * below the separation to any real base worth analysing. The GUI's
 * figure since 2026-08, moved here with the rule.
 */
#define STATION_CLASS_ON_ROVER_M 150.0

/**
 * @brief Classify a mountpoint from the evidence a run holds.
 *
 * The first rule that applies decides:
 * 1. The network-RTK gate test said GATED (the stream stopped when the
 *    GGA did): a network service.
 * 2. The sourcetable entry is a network service by @ref sourcetable_kind
 *    -- a keyword in its details, network field or name (`NEAR`,
 *    `NRBY_ADV`, `CORS_VRS`): a network service.
 * 3. The broadcast reference position sits within
 *    @ref STATION_CLASS_ON_ROVER_M of the position being sent: a network
 *    service -- a virtual station is placed at the rover.
 * 4. A reference position is known: a fixed base.
 * 5. Otherwise: unknown.
 *
 * Rules 1 and 2 outrank rule 3's absence: a nearest-base service puts
 * a real base kilometres from the rover, so distance alone calls it
 * fixed. A gate test that said NOT GATED is not used against rule 2: a
 * nearest-base service may keep streaming without a GGA.
 *
 * @param e        The mountpoint's sourcetable entry, or NULL when none
 *                 was found.
 * @param vrs_gate The gate test's result, a `VrsGate` value; 0 untested.
 * @param arp_valid Whether a broadcast reference position is known.
 * @param arp_lat,arp_lon The reference position, degrees.
 * @param gga_sent Whether a GGA position is being sent to the caster.
 * @param gga_lat,gga_lon The position sent, degrees.
 * @param why      [out] The evidence that decided, for display; may be NULL.
 * @param cap      Capacity of @p why.
 * @return The classification.
 */
StationService station_service(const SourcetableEntry *e, int vrs_gate,
                               bool arp_valid, double arp_lat, double arp_lon,
                               bool gga_sent, double gga_lat, double gga_lon,
                               char *why, size_t cap);

/** @brief "fixed", "network" or "unknown": the token the documents carry. */
const char *station_service_name(StationService s);

#ifdef __cplusplus
}
#endif

#endif /* STATION_CLASS_H */
