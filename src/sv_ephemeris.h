/**
 * @file sv_ephemeris.h
 * @brief Per-SV Keplerian broadcast-ephemeris cache.
 *
 * One slot per (gnss_id, prn).  Populated by the RTCM 1019/1045/1046
 * decoders as ephemerides arrive on the NTRIP stream, and consumed by
 * sv_orbit.c when computing satellite az/el for the Sky Plot window.
 *
 * Threading: the cache is intended to be used from a single thread
 * (the worker thread that drives the parser).  The UI thread's
 * re-decode-for-display path also writes to it, but races are benign —
 * the cache is for display, not for navigation.  Worst case: one update
 * cycle of slightly torn data, immediately overwritten by the next frame.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#ifndef SV_EPHEMERIS_H
#define SV_EPHEMERIS_H

#include <stdint.h>
#include <stdbool.h>

#define SV_EPH_MAX_GNSS          8
#define SV_EPH_MAX_SATS_PER_GNSS 64

/**
 * @brief Keplerian broadcast ephemeris, scaled to SI units.
 *
 * Shared by GPS (RTCM 1019) and Galileo F/NAV (1045) / I/NAV (1046).
 * Per-GNSS gravitational and rotation constants are applied at
 * propagation time in sv_orbit.c, not stored here.
 */
typedef struct {
    int      gnss_id;        /**< 1 = GPS, 3 = Galileo */
    int      prn;            /**< 1..SV_EPH_MAX_SATS_PER_GNSS */
    int      iode_iodnav;    /**< GPS IODE or Galileo IODnav */
    int      week;           /**< system week (10-bit GPS or 12-bit Galileo) */
    double   toe;            /**< time of ephemeris, seconds of week */
    double   toc;            /**< time of clock, seconds of week */
    double   sqrt_a;         /**< sqrt of semi-major axis, m^0.5 */
    double   e;              /**< eccentricity (dimensionless) */
    double   i0;             /**< inclination at toe, rad */
    double   omega0;         /**< RAAN at toe, rad */
    double   omega;          /**< argument of perigee, rad */
    double   m0;             /**< mean anomaly at toe, rad */
    double   delta_n;        /**< mean motion correction, rad/s */
    double   idot;           /**< rate of inclination, rad/s */
    double   omega_dot;      /**< rate of RAAN, rad/s */
    double   cuc, cus;       /**< argument-of-latitude corrections, rad */
    double   crc, crs;       /**< radial corrections, m */
    double   cic, cis;       /**< inclination corrections, rad */
    double   af0, af1, af2;  /**< SV clock correction polynomial */
    int      health;         /**< 0 = healthy */
    bool     valid;          /**< true once populated */

    /* ── GLONASS-only state-vector form (gnss_id == 2) ──────────────── */
    /* GLONASS broadcasts position + velocity in PZ-90 (~WGS-84) at the
     * reference epoch tb, plus luni-solar perturbation acceleration.
     * Propagation is by numerical integration of the 6-state ODE — see
     * glonass_to_ecef() in sv_orbit.c. */
    double   glo_pos[3];     /**< X, Y, Z in metres at tb (PZ-90) */
    double   glo_vel[3];     /**< Vx, Vy, Vz in m/s at tb */
    double   glo_acc[3];     /**< Luni-solar accel in m/s^2 (constants per eph) */
    double   glo_tb_sod;     /**< Reference time in Moscow seconds-of-day */
    int      glo_freq_chan;  /**< FDMA channel number, -7 .. +13 */
} SvEphemeris;

/** @brief Zero the entire cache.  Optional — static storage is already zeroed. */
void sv_eph_init(void);

/** @brief Insert or replace the slot for (eph->gnss_id, eph->prn). */
void sv_eph_store(const SvEphemeris *eph);

/**
 * @brief Look up the cached ephemeris for (gnss_id, prn).
 * @return Pointer to the cached slot, or NULL if no ephemeris stored.
 */
const SvEphemeris* sv_eph_get(int gnss_id, int prn);

/**
 * @brief Test whether @p eph is usable for propagation at @p week / @p tow_s.
 *
 * GPS broadcast ephemerides are nominally valid for ~2 h; Galileo for ~10 min.
 * We allow some grace (4 h GPS, 30 min Galileo) to keep the sky plot useful
 * even when ephemeris updates lag.
 */
bool sv_eph_is_valid_at(const SvEphemeris *eph, int week, double tow_s);

#endif /* SV_EPHEMERIS_H */
