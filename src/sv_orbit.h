/**
 * @file sv_orbit.h
 * @brief Keplerian satellite-orbit propagator (ICD-GPS-200 §20.3.3.4.3).
 *
 * The same algorithm applies to GPS and Galileo with different
 * gravitational parameters; per-GNSS constants are selected internally
 * based on @c SvEphemeris::gnss_id.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#ifndef SV_ORBIT_H
#define SV_ORBIT_H

#include <stdbool.h>
#include "sv_ephemeris.h"

/**
 * @brief Propagate a broadcast ephemeris to ECEF position at GPS/Galileo time.
 *
 * Implements the standard Keplerian propagator (ICD-GPS-200 §20.3.3.4.3 /
 * Galileo OS-SIS-ICD §5.1.1).  Output is the satellite's WGS-84 ECEF
 * position in metres.  Validity-of-ephemeris is NOT checked here — the
 * caller should use @ref sv_eph_is_valid_at() first.
 *
 * @param eph    Ephemeris snapshot (must be non-NULL and have @c valid set).
 * @param week   GPS/Galileo week (no rollover) at propagation time.
 * @param tow_s  Seconds-of-week at propagation time.
 * @param x      [out] ECEF X (m), may be NULL.
 * @param y      [out] ECEF Y (m), may be NULL.
 * @param z      [out] ECEF Z (m), may be NULL.
 * @return true on success; false if @p eph is invalid or @c sqrt_a <= 0.
 */
bool kepler_to_ecef(const SvEphemeris *eph,
                    int week, double tow_s,
                    double *x, double *y, double *z);

#endif /* SV_ORBIT_H */
