/**
 * @file rinex_nav.h
 * @brief Minimal RINEX 3 navigation file loader.
 *
 * Reads a RINEX 3.x mixed-GNSS navigation (".rnx" / ".nav" / "*MN.rnx")
 * file and populates the per-SV ephemeris cache (see sv_ephemeris.h)
 * the same way the dual-stream eph worker does.  Supports GPS, Galileo,
 * QZSS, BeiDou and NavIC/IRNSS as Keplerian records, and GLONASS as state
 * vectors.  SBAS records are recognised and skipped.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#ifndef RINEX_NAV_H
#define RINEX_NAV_H

#ifdef __cplusplus
extern "C" {
#endif

/** Per-GNSS load counts.  Index = gnss_id (1=GPS, 2=GLONASS, ...). */
#define RINEX_NAV_MAX_GNSS  8

/**
 * @brief Load broadcast ephemerides from a RINEX 3 NAV file.
 *
 * @param filename      Path to the file.
 * @param out_counts    Array of size RINEX_NAV_MAX_GNSS; on success each
 *                      slot is set to the number of ephemerides loaded
 *                      for that GNSS.  Pass NULL if you don't care.
 * @return Total number of ephemerides loaded, or -1 on I/O error.
 */
int rinex_nav_load(const char *filename, int *out_counts);

/**
 * @brief UTC epoch seconds of the newest record in the file last loaded.
 *
 * Zero when no file has been loaded, or none of its records parsed.
 *
 * Read from the records' own calendar dates, which is the only place a
 * *true* age can come from.  The cache cannot answer this: it stores
 * @c toe, and toe is not on one scale across systems -- GLONASS carries
 * Moscow seconds-of-day and nothing else, so an ephemeris a day old
 * wraps and reads as hours old.  A file whose newest record is thirty
 * hours old reported "4.5 h" that way, which is the opposite of what
 * such a file should tell a user: every record in it is outside the
 * validity window in @ref sv_eph_is_valid_at, so nothing can be placed
 * from it at all.
 */
long long rinex_nav_newest_utc(void);

#ifdef __cplusplus
}
#endif

#endif /* RINEX_NAV_H */
