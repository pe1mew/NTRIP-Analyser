/**
 * @file ntrip_bridge.h
 * @brief The Android app's view of the C core -- deliberately JNI-free.
 *
 * Everything the phone needs from the shared C code is behind these six
 * functions, and none of them mentions a JNI type.  That is the point:
 * JNI glue cannot be compiled or run without an NDK, so anything that
 * lives there cannot be tested on a desktop.  Keeping the session
 * lifecycle, the KPI engine and the JSON assembly here means the part
 * that can carry bugs is plain C99 -- buildable and testable against a
 * live caster on any machine, with `jni_glue.c` reduced to mechanical
 * parameter marshalling.
 *
 * ## Threading
 *
 * One background thread owns a bridge instance: it calls
 * @ref bridge_open once, @ref bridge_pump in a loop, then
 * @ref bridge_close.  @ref bridge_snapshot_json may be called from that
 * same thread only.  On Android that thread is the foreground service's
 * pump loop, and the UI receives the JSON string, never the handle.
 *
 * ## The JSON contract (design-review D3)
 *
 * One string per poll carries both the statistics snapshot and the KPI
 * verdicts, so the UI never derives a verdict itself:
 *
 * ```json
 * {
 *   "stats":   { ... ns_stats_to_json() verbatim ... },
 *   "kpi": {
 *     "overall": 1, "overall_name": "STATION OK",
 *     "elapsed_s": 92.0, "sustained_s": 60.0,
 *     "items": [ { "verdict": 1, "verdict_name": "PASS",
 *                  "label": "...", "detail": "...", "value": 0.0 }, ... ]
 *   }
 * }
 * ```
 *
 * In watch mode a third block appears, carrying the long-run picture:
 *
 * ```json
 *   "watch": { "elapsed_s": 3600.0, "ok_s": 3540.0, "availability": 0.98,
 *              "streak_s": 900.0, "best_streak_s": 2400.0,
 *              "degradations": 2, "worst": 2, "worst_name": "CAUTION",
 *              "last_degrade_s": 2700.0 }
 * ```
 *
 * `schema_version` inside `stats` versions the whole document.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * @copyright Apache License 2.0 with Commons Clause (see LICENSE for details)
 */

#ifndef NTRIP_BRIDGE_H
#define NTRIP_BRIDGE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque bridge instance: one session plus one KPI run. */
typedef struct NtripBridge NtripBridge;

/**
 * @brief Open a session against a caster and start an acceptance run.
 *
 * Returns immediately; the connection is established by the first
 * @ref bridge_pump calls, so the caller is never blocked here.
 *
 * @param caster     Caster hostname.
 * @param port       Caster port.
 * @param mountpoint Mountpoint name.
 * @param user       Username; "" for none.
 * @param password   Password; "" for none.
 * @param lat        Rover latitude, degrees (used for GGA when enabled).
 * @param lon        Rover longitude, degrees.
 * @param send_gga   Send periodic GGA, for network-RTK mountpoints.
 * @param watch      Keep watching after a verdict is reached, and
 *                   accumulate long-run health (@ref KpiWatch) instead
 *                   of finishing.  A spot check grades a station; a
 *                   watch observes one.
 * @return Bridge handle, or NULL on allocation failure.
 */
NtripBridge *bridge_open(const char *caster, int port, const char *mountpoint,
                         const char *user, const char *password,
                         double lat, double lon, bool send_gga, bool watch);

/**
 * @brief Open a bridge that replays a captured `.rtcm3` file.
 *
 * The same acceptance run over recorded bytes -- how the app is
 * exercised without a caster, and how this module is tested.
 *
 * @param path  Capture to replay.
 * @param watch As for @ref bridge_open: soak a recording rather than
 *              grading it once.
 */
NtripBridge *bridge_open_file(const char *path, bool watch);

/**
 * @brief Service the stream and update the KPI verdicts.
 *
 * @param b          Bridge handle.
 * @param timeout_ms How long to wait for bytes before returning.
 * @param now_s      Caller's clock, seconds; only differences matter.
 * @return >=0 while the stream is alive, <0 once it has ended.
 */
int bridge_pump(NtripBridge *b, int timeout_ms, double now_s);

/**
 * @brief Serialise the snapshot and the KPI report as one JSON object.
 *
 * @param b   Bridge handle.
 * @param out Destination buffer.
 * @param cap Capacity; 16 kB is comfortable.
 * @return Bytes written excluding the terminator, or <0 on error.
 */
int bridge_snapshot_json(NtripBridge *b, char *out, size_t cap);

/**
 * @brief Fetch and parse a caster's sourcetable, as JSON.
 *
 * **Blocking**: this opens a connection and waits for the caster, so it
 * must not be called on a UI thread.
 *
 * Independent of any session -- a user browses a caster before deciding
 * what to check, so this takes credentials directly rather than needing
 * a bridge handle.
 *
 * ```json
 * {"entries":[{"mountpoint":"APEL00NLD0","identifier":"Apeldoorn",
 *              "format":"RTCM 3.3","nav_systems":"GPS+GLO+GAL+BDS",
 *              "country":"NLD","lat":52.23,"lon":5.94,
 *              "carrier":2,"nmea":false}, ...]}
 * ```
 *
 * @param caster   Caster hostname.
 * @param port     Caster port.
 * @param user     Username; "" for none.
 * @param password Password; "" for none.
 * @param out      Destination buffer.
 * @param cap      Capacity; a large caster needs 64 kB or more.
 * @return Bytes written excluding the terminator, or <0 on failure
 *         (unreachable caster, or a buffer too small for the table).
 */
int bridge_sourcetable_json(const char *caster, int port,
                            const char *user, const char *password,
                            char *out, size_t cap);

/** @brief Overall verdict as @ref KpiRunVerdict, for a cheap poll. */
int bridge_overall(const NtripBridge *b);

/**
 * @brief Attach an ephemeris side-stream, enabling the sky plot.
 *
 * The observation stream says *which* satellites are being tracked; it
 * cannot say *where* they are. Azimuth and elevation need broadcast
 * ephemerides, which casters publish on a separate mountpoint, so the
 * sky plot needs a second session -- exactly as the desktop's `--sky`
 * mode does.
 *
 * Optional: without it everything else still works and
 * @ref bridge_sky_rgb reports that it has nothing to draw.
 *
 * @param b          Bridge handle.
 * @param caster     Ephemeris caster hostname.
 * @param port       Ephemeris caster port.
 * @param mountpoint Ephemeris mountpoint, e.g. `BCEP00KAD0`.
 * @param user       Username; "" for none.
 * @param password   Password; "" for none.
 * @return true when the side-stream opened.
 */
bool bridge_open_eph(NtripBridge *b, const char *caster, int port,
                     const char *mountpoint,
                     const char *user, const char *password);

/**
 * @brief How many satellites currently have a usable ephemeris.
 *
 * Zero means the sky plot has nothing to place: either no ephemeris
 * stream is attached, or it has not yet delivered a full set.
 */
int bridge_eph_count(const NtripBridge *b);

/**
 * @brief Load orbits from a RINEX navigation file the user supplied.
 *
 * The app never downloads this file.  The user obtains it themselves and
 * so holds the relationship with the data provider, including its
 * licence and usage rules -- see `android/design/views.md`.
 *
 * @param b    Bridge handle (may be NULL: the cache is process-wide).
 * @param path Readable path to a RINEX 3 NAV file, already decompressed.
 * @return Records accepted, or <0 when the file could not be read.
 */
int bridge_load_rinex(NtripBridge *b, const char *path);

/**
 * @brief How many of the tracked satellites can currently be placed.
 *
 * The question the ephemeris policy turns on: with every tracked
 * satellite placed there is nothing to fetch, and a stream should not be
 * opened at all.
 *
 * @param b       Bridge handle.
 * @param tracked [out, optional] satellites the stream is carrying.
 * @return Satellites both tracked and holding a usable ephemeris.
 */
int bridge_placeable(const NtripBridge *b, int *tracked);

/** @brief Satellites holding an orbit, whether tracked or not. */
int bridge_eph_cached(const NtripBridge *b);

/**
 * @brief Seconds since the newest orbit in the cache was issued.
 *
 * Measured from the newest rather than the oldest: this answers "how
 * long since the app last learned anything about the orbits", which is
 * what decides whether to refill. The oldest entry answers a different
 * question and reads alarmingly for a cache that is mostly fresh.
 *
 * @return Age in seconds, or <0 when the cache is empty.
 */
double bridge_eph_age_s(const NtripBridge *b);

/** @brief Close the ephemeris side-stream, keeping what it delivered. */
void bridge_close_eph(NtripBridge *b);

/** @brief Frames seen on the ephemeris stream; -1 if no bridge.
 *  Distinguishes "stream not delivering" from "delivering but
 *  nothing decodable", which look identical from the count. */
int bridge_eph_frames(const NtripBridge *b);

/**
 * @brief Render the sky-coverage heatmap into an RGB buffer.
 *
 * Draws the same image the desktop's `--sky` mode saves, from the
 * sectors accumulated since the run began.
 *
 * @param b      Bridge handle.
 * @param rgb    [out] `width * height * 3` bytes, packed R,G,B.
 * @param width  At least 100.
 * @param height At least 100.
 * @return true when drawn; false when there is nothing to draw yet --
 *         no station position, or no ephemerides.
 */
bool bridge_sky_rgb(NtripBridge *b, unsigned char *rgb,
                    int width, int height);

/** @brief Close the session and free the bridge.  NULL-safe. */
void bridge_close(NtripBridge *b);

#ifdef __cplusplus
}
#endif

#endif /* NTRIP_BRIDGE_H */
