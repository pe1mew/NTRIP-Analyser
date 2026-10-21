/**
 * @file geo.h
 * @brief Distance and direction between two positions on the Earth.
 *
 * One implementation for every frontend. The GUI had two private copies
 * -- `haversine_km` for the sourcetable's distance column and
 * `geo_distance_m` for the ARP checks -- and the mountpoint map
 * (design/work-items/archive/mountpoint-map.md) would have needed a third on
 * the phone.
 *
 * **A sphere, not the ellipsoid.** Mean Earth radius, haversine
 * distance, initial great-circle bearing. Against the WGS-84 ellipsoid
 * the distance is off by well under one percent: enough to rank bases
 * by distance and to say which way one lies, not enough to survey with.
 * Nothing that needs better calls this.
 *
 * Core module: no I/O, no platform headers, no allocation.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * @copyright Apache License 2.0 with Commons Clause (see LICENSE for details)
 */

#ifndef GEO_H
#define GEO_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Mean Earth radius in metres (IUGG R1). */
#define GEO_EARTH_RADIUS_M 6371008.8

/**
 * @brief Great-circle distance between two positions.
 *
 * @param lat1,lon1 First position, degrees.
 * @param lat2,lon2 Second position, degrees.
 * @return Distance in metres.
 */
double geo_distance_m(double lat1, double lon1, double lat2, double lon2);

/**
 * @brief Initial bearing from the first position towards the second.
 *
 * The direction to set off in, clockwise from true north. Over the
 * distances between a site and its candidate bases it is also the
 * direction the base lies in, to within the drawing.
 *
 * @param lat1,lon1 From, degrees.
 * @param lat2,lon2 To, degrees.
 * @return Bearing in degrees, [0, 360). 0 when the two coincide, where
 *         no direction is defined.
 */
double geo_bearing_deg(double lat1, double lon1, double lat2, double lon2);

#ifdef __cplusplus
}
#endif

#endif /* GEO_H */
