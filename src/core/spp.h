/**
 * @file spp.h
 * @brief Where the station's own data says the station is.
 *
 * One epoch of observables in, one position out, by least squares over
 * the iono-free combination: P3 of
 * `design/work-items/station-self-position.md`.
 *
 * **What the number is, and is not.** Broadcast ephemeris gives a
 * metre-level position, so the offset against the station's declared
 * reference is dominated by the solution's own error, not by the
 * antenna having moved. The scatter over time is the measurement; one
 * epoch's offset is noise with a number printed on it. Anything
 * displaying this must say so, and nothing here is a surveyed
 * coordinate: `docs/base-declaration.md` keeps that job, with CSRS-PPP
 * doing the work.
 *
 * **What it refuses rather than approximates:**
 * - a single-frequency station, because modelling the ionosphere
 *   instead would bias the answer by tens of metres while looking
 *   exactly as confident (decision 3 of the work item);
 * - **every constellation but GPS.** The argument was first written for
 *   GLONASS alone — FDMA inter-frequency biases that one receiver-clock
 *   unknown cannot absorb — and it holds for all of them: each system
 *   keeps its own time and reaches the receiver down its own hardware
 *   path, so each needs a clock unknown of its own. BeiDou brings a
 *   second fault with it, since BDT runs 14 s behind GPS and a 1042
 *   ephemeris dates its `toe` in BDT. Letting the lot in under one
 *   clock unknown was measured against a six-system station: an offset
 *   of 1.8 km, a worst residual of 2.6 km, an apparent motion of
 *   829 m/s. Multi-GNSS returns when there is a clock unknown per
 *   system;
 * - fewer than four usable satellites, which is not a weak solution but
 *   no solution.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
#ifndef SPP_H
#define SPP_H

#include <stdbool.h>

#include "core/ns_obs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Why a solve produced nothing, in the words a report will use. */
typedef enum {
    SPP_OK = 0,
    SPP_NO_EPOCH,        /**< no observables at all */
    SPP_SINGLE_FREQ,     /**< no satellite carried two usable carriers */
    SPP_NO_EPHEMERIS,    /**< observations without orbits */
    SPP_TOO_FEW_SATS,    /**< fewer than four after every rejection */
    SPP_DIVERGED         /**< the iteration did not settle */
} SppStatus;

/** One epoch's answer. */
typedef struct {
    double    pos[3];        /**< ECEF metres */
    double    enu[3];        /**< east, north, up against the reference */
    double    clock_bias_m;  /**< receiver clock offset, metres */
    double    code_rms_m;    /**< RMS of the post-fit residuals */
    double    pdop;          /**< position dilution of precision */
    int       n_used;        /**< satellites in the final solution */
    int       n_rejected;    /**< satellites dropped, with reasons counted
                                  below */
    int       n_no_eph;      /**< dropped: no ephemeris */
    int       n_single_freq; /**< dropped: only one usable carrier */
    int       n_low_elev;    /**< dropped: below the elevation mask */
    SppStatus status;

    /* The velocity half (P4).  A station is supposed to be standing
     * still, so this is the sharper of the two numbers: the position's
     * offset is dominated by the solution's own error, while the
     * velocity has a known true value of zero and anything else is a
     * finding. */
    bool      has_velocity;  /**< false when the stream carries no phase
                                  rates at all -- MSM4, MSM6 and the
                                  legacy messages do not, and a velocity
                                  of zero must never be invented for
                                  them */
    double    vel_ecef[3];   /**< metres per second, Earth-fixed */
    double    vel_enu[3];    /**< east, north, up */
    double    clock_drift_ms;/**< receiver clock drift, metres per second */
    double    rate_rms_ms;   /**< RMS of the post-fit rate residuals */
    int       n_rate_used;   /**< satellites carrying a usable rate */
    /**
     * The sign convention the station's phase-range rates were found to
     * use: +1 as RTCM 10403.3 defines them (positive while the range
     * grows), −1 reversed, 0 when it could not be decided.
     *
     * Decided from the data, not assumed, because a receiver in service
     * in the Netherlands in October 2026 encodes them reversed: every
     * satellite the exact negative of what its orbit predicts, which a
     * solver taking the field at its word turned into an antenna doing
     * 1.4 km/s. Both conventions are fitted and the one that fits is
     * kept; with five or more satellites the right one leaves
     * centimetres per second and the wrong one hundreds of metres.
     * With exactly four both fit perfectly, so it is 0 and the standard
     * sign is used.
     *
     * −1 is a finding about the station, not a correction to hide: a
     * rover that trusts these rates is being misled by them.
     */
    int       rate_sign;
} SppSolution;

/** Satellites below this are more troposphere than signal. */
#define SPP_ELEV_MASK_DEG 10.0

/**
 * @brief Solve one epoch for the station's own position.
 *
 * @param obs    The epoch, as the session accumulated it.
 * @param week   GPS week for the epoch.
 * @param tow_s  Seconds of week at reception.
 * @param ref_ecef Reference position: the broadcast ARP, or the
 *                 sourcetable's, and the report must say which. Used as
 *                 the iteration's starting point and as the origin of
 *                 @ref SppSolution::enu. NULL starts from the Earth's
 *                 centre, which costs an iteration or two.
 * @param out    [out] The solution; its `status` says why on failure.
 * @return The same status, so a caller may branch without reading
 *         @p out.
 */
SppStatus spp_solve(const NsObsEpoch *obs, int week, double tow_s,
                    const double ref_ecef[3], SppSolution *out);

/** @brief One line naming a status, for a report or a log. */
const char *spp_status_text(SppStatus s);

#ifdef __cplusplus
}
#endif

#endif /* SPP_H */
