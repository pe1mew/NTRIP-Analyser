/**
 * @file sv_state.h
 * @brief Where a satellite was, how fast, and what its clock read.
 *
 * The sky plot needs a direction, so `sv_orbit.c` has long answered
 * *where*.  Solving for a receiver's own position needs three more
 * things at the instant the signal left the satellite: the position in
 * the frame the measurement is made in, the velocity, and the satellite
 * clock's offset from system time.  This module adds those, and is P2 of
 * `design/work-items/station-self-position.md`.
 *
 * **It is the step everything downstream inherits.** A metre of error
 * here is a metre of error in every position formed from it, silently,
 * so each quantity is checked against something that does not share its
 * arithmetic: the analytic velocity against a numerical derivative of
 * the existing propagator, the geometry against the orbit it must
 * describe, the clock against the magnitudes physics allows.
 *
 * **What it does not do.** No troposphere, no ionosphere, no antenna
 * phase centre, no group delay -- a dual-frequency iono-free combination
 * needs none of those from here, and P3 owns the ones it does need.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
#ifndef SV_STATE_H
#define SV_STATE_H

#include <stdbool.h>

#include "core/sv_ephemeris.h"

#ifdef __cplusplus
extern "C" {
#endif

/** What a satellite was doing when the signal left it. */
typedef struct {
    double pos[3];      /**< ECEF metres */
    double vel[3];      /**< ECEF metres per second, **Earth-fixed**: the
                             rate of change of the ECEF position, which is
                             what a velocity solve differences against a
                             receiver's own ECEF motion.  It is about
                             2.9 km/s for a GPS satellite where the
                             inertial speed is 3.9; the difference is
                             w x r, and a test applying vis-viva or
                             angular momentum must add it back */
    double clock_s;     /**< Clock offset from system time, seconds.
                             Positive means the satellite clock runs
                             ahead, so a measured pseudorange is short by
                             c * clock_s */
    double clock_rate;  /**< Rate of that offset, seconds per second */
    bool   has_clock;   /**< False when the ephemeris carries no clock,
                             which no solve may treat as zero */
    bool   valid;       /**< False when nothing could be computed */
} SvState;

/**
 * @brief Position, velocity and clock at one instant.
 *
 * @param eph    Ephemeris for the satellite.
 * @param week   GPS week of @p tow_s; ignored for GLONASS.
 * @param tow_s  Seconds of week, or Moscow seconds-of-day for GLONASS,
 *               exactly as @ref sv_to_ecef takes them.
 * @param out    [out] The state. Zeroed, with `valid` false, on refusal.
 * @return true when a position and velocity were computed. Check
 *         @ref SvState::has_clock separately: a GLONASS record that
 *         predates the clock terms has an orbit and no time.
 */
bool sv_state_at(const SvEphemeris *eph, int week, double tow_s,
                 SvState *out);

/**
 * @brief Rotate a position into the frame the receiver measured in.
 *
 * Earth turns while the signal flies, so a satellite position computed
 * at transmit time is expressed in a frame that has since moved: about
 * 30 metres at the equator over a 70 ms flight, which is thirty times
 * the accuracy a broadcast solution can otherwise reach.  The rotation
 * belongs to the geometry, not to the propagator, so it is applied here
 * rather than inside @ref sv_state_at.
 *
 * @param pos      [in,out] ECEF position at transmit time.
 * @param flight_s Signal flight time in seconds.
 * @param gnss_id  Constellation, for its own rotation rate.
 */
void sv_state_derotate(double pos[3], double flight_s, int gnss_id);

#ifdef __cplusplus
}
#endif

#endif /* SV_STATE_H */
