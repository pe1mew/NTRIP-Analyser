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

/** @brief Overall verdict as @ref KpiRunVerdict, for a cheap poll. */
int bridge_overall(const NtripBridge *b);

/** @brief Close the session and free the bridge.  NULL-safe. */
void bridge_close(NtripBridge *b);

#ifdef __cplusplus
}
#endif

#endif /* NTRIP_BRIDGE_H */
