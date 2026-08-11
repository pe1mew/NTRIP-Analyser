/**
 * @file ntrip_session.h
 * @brief One implementation of the NTRIP stream loop, for every frontend.
 *
 * Connect, reassemble RTCM frames, validate CRC-24Q, accumulate
 * statistics, and report through a callback.  The caller decides what to
 * do with the events: the CLI prints them, the GUI paints them, the
 * monitoring service counts them, the Android app evaluates them as KPIs.
 *
 * Before this existed the same loop was written out at ten separate
 * sites across `src/main.c`, `src/ntrip_handler.c` and
 * `gui/gui_thread.c`.  See design/architecture.md §1.1 and §3.
 *
 * ### Who owns the thread
 *
 * The caller does.  @ref ns_pump performs one iteration and returns, so
 * a GUI can drive it from a worker thread, a daemon can drive several
 * sessions from one loop, and a CLI can spin on it.  @ref ns_run is a
 * convenience wrapper for the last case.
 *
 * ### Live and replay are the same code path
 *
 * @ref ns_open_file replays a capture through the identical framing,
 * validation and statistics code, so offline analysis cannot drift away
 * from live behaviour.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#ifndef NTRIP_SESSION_H
#define NTRIP_SESSION_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>    /* FILE, for ns_open_stream() */
#include "core/iono.h"     /* IonoSatView, for ns_iono_view() */
#include <stddef.h>

#include "core/ns_stats.h"
#include "net/ntrip_proto.h"
#include "net/ntrip_handler.h"   /* NTRIP_Config */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque session handle. */
typedef struct NtripSession NtripSession;

/** @brief What happened. */
typedef enum {
    NS_EV_CONNECTING = 0, /**< a connection attempt started            */
    NS_EV_HANDSHAKE,      /**< the caster answered; handshake parsed   */
    NS_EV_STREAMING,      /**< first frame decoded; the stream is live */
    NS_EV_RAW,            /**< payload bytes, before framing           */
    NS_EV_FRAME,          /**< one RTCM frame with a valid CRC         */
    NS_EV_FRAME_BAD,      /**< a frame was rejected                    */
    NS_EV_STATS,          /**< periodic statistics snapshot            */
    NS_EV_DISCONNECTED,   /**< the session ended or dropped            */
    NS_EV_LOG,            /**< human-readable diagnostic               */
} NsEventType;

/** @brief Why a frame was rejected. */
typedef enum {
    NS_BAD_CRC = 1,       /**< complete frame, CRC-24Q mismatch        */
    NS_BAD_MALFORMED,     /**< bad preamble or runt frame              */
    NS_BAD_LENGTH,        /**< implausible length; framing re-acquired */
} NsBadReason;

/** @brief Why a session ended. */
typedef enum {
    NS_END_STOPPED = 0,   /**< the caller asked it to stop             */
    NS_END_EOF,           /**< the peer closed, or a capture ended     */
    NS_END_REJECTED,      /**< the caster refused the request          */
    NS_END_NET_ERROR,     /**< socket or DNS failure                   */
} NsEndReason;

/** @brief Severity of an @ref NS_EV_LOG event. */
typedef enum {
    NS_LOG_INFO = 0,
    NS_LOG_WARN,
    NS_LOG_ERROR,
} NsLogLevel;

/** @brief One event delivered to the caller's callback. */
typedef struct {
    NsEventType type;
    double      t_rel;                 /**< seconds since session start */
    union {
        /** NS_EV_RAW: every payload byte as received, before the RTCM
         *  framer sees it.  This is what lets a consumer recognise a
         *  non-RTCM stream (UBX, SBF, RT27, LB2): the session only
         *  understands RTCM framing, so on such a stream NS_EV_FRAME
         *  never fires and raw bytes are all there is. */
        struct { const unsigned char *data; int len; } raw;
        struct {
            const unsigned char *data; /**< whole frame, preamble to CRC */
            int      len;
            int      msg_type;
            uint32_t epoch;            /**< MSM epoch; 0 if not an MSM   */
            bool     has_epoch;
            bool     new_epoch;        /**< first frame of this epoch    */
        } frame;
        struct { int reason; } bad;    /**< @ref NsBadReason             */
        const NsHandshake     *handshake;
        const NsStatsSnapshot *stats;
        struct { int reason; } end;    /**< @ref NsEndReason             */
        struct { int level; const char *text; } log;
    } u;
} NsEvent;

/**
 * @brief Event sink.
 *
 * Called on the thread that invoked @ref ns_pump.  Pointers in the event
 * are valid only for the duration of the call; copy anything you keep.
 */
typedef void (*NsEventFn)(const NsEvent *ev, void *user);

/** @brief Session configuration. */
typedef struct {
    NTRIP_Config config;          /**< caster, port, mountpoint, credentials */
    double stats_interval_s;      /**< emit NS_EV_STATS this often; 0 = never */
    bool   send_gga;              /**< periodic GGA uplink, for VRS mountpoints */
    double gga_interval_s;        /**< default 1.0 when send_gga is set      */
    bool   auto_reconnect;        /**< reconnect with backoff after a drop   */
    int    reconnect_backoff_max_s;
    const char *user_agent;       /**< NULL selects a default                */
} NsOptions;

/** @brief Fill @p opt with defaults. */
void ns_options_default(NsOptions *opt);

/**
 * @brief Open a live session against a caster.
 *
 * Returns immediately; the connection is established by the first calls
 * to @ref ns_pump, so a caller is never blocked at construction.
 *
 * @return Session handle, or NULL on allocation failure.
 */
NtripSession *ns_open(const NsOptions *opt, NsEventFn cb, void *user);

/**
 * @brief Open a session that replays a captured `.rtcm3` file.
 *
 * Emits the same events as a live session, minus the handshake, so
 * anything that analyses a live stream analyses a capture unchanged.
 */
NtripSession *ns_open_file(const char *path, const NsOptions *opt,
                           NsEventFn cb, void *user);

/**
 * @brief Open a session that reads RTCM from an already-open stream.
 *
 * The general form of @ref ns_open_file, which is a thin wrapper around
 * it. Exists so a caller can feed the session from a handle it does not
 * own -- `stdin` above all, which is how a capture is piped in for
 * offline analysis and which no path/`fopen` API can express.
 *
 * @param f    Stream to read, positioned at the first RTCM byte. Should
 *             be in binary mode; on Windows a text-mode handle mangles
 *             CRLF byte pairs inside RTCM payloads.
 * @param own  true to `fclose` @p f in @ref ns_close. Pass **false** for
 *             `stdin` or any handle the caller reuses afterwards.
 * @param opt  Session options; the connection fields are ignored.
 * @param cb   Event callback.
 * @param user Opaque pointer passed to @p cb.
 * @return Session handle, or NULL on allocation failure.
 */
NtripSession *ns_open_stream(FILE *f, bool own, const NsOptions *opt,
                             NsEventFn cb, void *user);

/**
 * @brief Advance the session by one iteration.
 *
 * @param s          Session to advance.
 * @param timeout_ms Longest time to wait for input.  Ignored for replay.
 * @return >0 bytes consumed, 0 nothing available before the timeout,
 *         or <0 when the session has ended -- after which the caller
 *         should stop pumping and call @ref ns_close.
 */
int ns_pump(NtripSession *s, int timeout_ms);

/**
 * @brief Pump until the session ends or @p stop_flag becomes non-zero.
 *
 * Convenience for callers that own a whole thread, such as the CLI.
 *
 * @param s         Session to run.
 * @param stop_flag Polled each iteration; may be NULL to run until the
 *                  session ends on its own.
 */
void ns_run(NtripSession *s, const volatile int *stop_flag);

/** @brief Ask the session to end at the next @ref ns_pump. */
void ns_stop(NtripSession *s);

/** @brief Release the session.  Safe on NULL. */
void ns_close(NtripSession *s);

/**
 * @brief Current statistics.
 *
 * Valid until the next @ref ns_pump.  Callers that keep it should copy;
 * @ref NsStatsSnapshot is a plain value type.
 */
const NsStatsSnapshot *ns_stats(const NtripSession *s);

/**
 * @brief Per-satellite ionospheric measurements for display.
 *
 * Fills @p out from the session's arc state, judged for staleness
 * against the session's own clock -- which is why this exists instead of
 * exposing the raw @ref IonoState to callers with a different clock.
 *
 * @param s       Session to read.
 * @param out     [out] Destination array.
 * @param max_out Capacity of @p out.
 * @return Entries written; 0 when nothing is measurable yet.
 */
int ns_iono_view(const NtripSession *s, IonoSatView *out, int max_out);

/** @brief The caster's handshake, or NULL if not yet received. */
const NsHandshake *ns_handshake(const NtripSession *s);

/**
 * @brief Send one GGA sentence now, at the given position.
 *
 * For consumers that manage the uplink themselves -- the GUI's VRS test
 * features move the reported position and pause the uplink interactively,
 * a cadence the built-in @ref NsOptions::send_gga timer cannot express.
 * Use one mechanism or the other, not both.
 *
 * @return true if the sentence was handed to the socket.  Always false
 *         on a replay session, which has nothing to send to.
 */
bool ns_send_gga(NtripSession *s, double lat, double lon);

/**
 * @brief Enable or disable RTCM framing (default: enabled).
 *
 * A consumer that identifies the stream as a non-RTCM format (via
 * @ref NS_EV_RAW) disables framing so the payload is not fed through the
 * RTCM state machine -- arbitrary binary contains 0xD3 often enough that
 * leaving it on inflates the CRC-error counters of a stream that is not
 * malfunctioning.  Raw events and byte counting continue.
 */
void ns_set_framing_enabled(NtripSession *s, bool enabled);

/** @brief Seconds since the session was opened. */
double ns_uptime(const NtripSession *s);

#ifdef __cplusplus
}
#endif

#endif /* NTRIP_SESSION_H */
