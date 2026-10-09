/**
 * @file ns_obs.h
 * @brief One epoch of raw observables, kept so that something can solve
 *        with them.
 *
 * The stream has always carried pseudoranges, carrier phases and phase
 * rates; nothing kept them.  They were decoded into the message-detail
 * text and dropped, which is why `design/kpi-candidates.md` records code
 * and phase RMS as "not computable": not absent from the wire, absent
 * from memory.  This is the structure that ends that, and
 * `design/work-items/station-self-position.md` P1 is why.
 *
 * **One epoch, never a history.** A base at 1 Hz would otherwise grow a
 * megabyte a minute, and the phone has to run this too (decision 8 of
 * the work item). Measured: a cell is 40 bytes and the set 7 704, one
 * instance per session. Cells beyond @ref NS_OBS_MAX_CELLS are counted in
 * @ref NsObsEpoch::dropped rather than silently lost: a solve over a
 * truncated epoch must be able to say so.
 *
 * **An epoch spans several frames.** A multi-constellation base sends
 * GPS, GLONASS, Galileo and BeiDou as separate messages for the same
 * instant, flagged by the MSM multiple-message bit, so the set
 * accumulates across frames and closes when that bit clears.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
#ifndef NS_OBS_H
#define NS_OBS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Cells retained per epoch. Sixty satellites across three signals fit;
 *  a four-constellation base streaming everything it has may not, and
 *  that is what @ref NsObsEpoch::dropped is for. */
#define NS_OBS_MAX_CELLS 192

/** What a cell actually carries. MSM4 and MSM6 have no phase rate, so a
 *  velocity cannot be formed from them; the absence is recorded rather
 *  than filled with zero, which would read as "not moving". */
enum {
    NS_OBS_HAS_PHASE  = 1u << 0,  /**< carrier phase present and valid   */
    NS_OBS_HAS_RATE   = 1u << 1,  /**< phase range rate present (MSM5/7) */
    NS_OBS_HALF_CYCLE = 1u << 2   /**< half-cycle ambiguity not resolved */
};

/** One satellite on one signal, at one epoch. */
typedef struct {
    uint8_t  gnss_id;        /**< 1 GPS, 2 GLONASS, 3 Galileo, 4 QZSS,
                                  5 BeiDou, 6 SBAS, 7 NavIC — the
                                  project's 1-based convention */
    uint8_t  prn;            /**< satellite number within the system */
    uint8_t  sig_id;         /**< 1-based MSM signal index */
    uint8_t  flags;          /**< NS_OBS_HAS_* */
    double   pseudorange_m;  /**< rough range and fine part, combined */
    double   phase_m;        /**< carrier phase as a range, metres */
    double   rate_ms;        /**< phase range rate, m/s; 0 unless HAS_RATE */
    float    cnr_dbhz;       /**< carrier-to-noise density */
    uint16_t lock;           /**< lock time indicator, as the message gives it */
} NsObsCell;

/** Every cell of one epoch, across constellations. */
typedef struct {
    uint32_t  epoch_ms;      /**< the frame's own epoch field */
    uint32_t  gnss_seen;     /**< bit (1 << gnss_id) per system contributing */
    int       n;             /**< cells held */
    int       dropped;       /**< cells that did not fit */
    bool      open;          /**< more frames expected for this epoch */
    NsObsCell cell[NS_OBS_MAX_CELLS];
} NsObsEpoch;

/**
 * @brief Begin a new epoch, discarding whatever the previous one held.
 *
 * @param e        Set to clear. Ignored when NULL.
 * @param epoch_ms The epoch field of the frame that opened it.
 */
void ns_obs_reset(NsObsEpoch *e, uint32_t epoch_ms);

/**
 * @brief Append one cell.
 *
 * @param e Set to append to.
 * @param c Cell to copy in.
 * @return true when stored; false when full, with @ref NsObsEpoch::dropped
 *         incremented. A caller that ignores the result loses nothing it
 *         can detect; the count is the honest record.
 */
bool ns_obs_add(NsObsEpoch *e, const NsObsCell *c);

/**
 * @brief Satellites represented, counting a satellite once however many
 *        signals it carries.
 *
 * @param e Set to count. NULL counts zero.
 * @return Distinct (gnss_id, prn) pairs.
 */
int ns_obs_sat_count(const NsObsEpoch *e);

#ifdef __cplusplus
}
#endif

#endif /* NS_OBS_H */
