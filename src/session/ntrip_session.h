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
#include "core/sv_track.h"
#include "core/sourcetable.h"
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

/**
 * @brief Why a frame was rejected.
 *
 * Two categories, and the pair is exhaustive by construction. A
 * `NS_BAD_MALFORMED` once sat between them and **nothing ever raised
 * it**: the framer treats a byte outside a frame as ordinary (a stream
 * legitimately begins mid-frame, and NMEA between frames is common) and
 * an implausible length as a re-sync. It was retired in 3.5.0 along with
 * the counter, the graph and the manual's row, because a monitoring
 * signal that cannot move is worse than an absent one.
 */
typedef enum {
    NS_BAD_CRC = 1,       /**< complete frame, CRC-24Q mismatch        */
    NS_BAD_LENGTH,        /**< implausible length; framing re-acquired */
} NsBadReason;

/** @brief Why a session ended. */
typedef enum {
    NS_END_STOPPED = 0,   /**< the caller asked it to stop             */
    NS_END_EOF,           /**< the peer closed, or a capture ended     */
    NS_END_REJECTED,      /**< the caster refused the request          */
    NS_END_NET_ERROR,     /**< socket or DNS failure                   */
    /** A capture write failed -- the disk is full, or the volume went
     *  away.  The only end reason that has nothing to do with the
     *  network: a session that is capturing can fail at the file even
     *  while the stream is perfectly healthy.  Ending is deliberate.
     *  An unattended run whose purpose is the file must not continue
     *  for another twenty hours writing nothing. */
    NS_END_WRITE_ERROR,
    /** The socket stayed open and the caster stopped sending.  Its own
     *  reason rather than @ref NS_END_EOF, because nothing closed and
     *  nothing failed: the connection is still there, and only the
     *  clock says it is dead.  A caller reporting why a session ended
     *  should be able to say which of the two happened. */
    NS_END_STALLED,
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

    /**
     * @brief Give up on a connected but silent socket after this long;
     *        0 = wait forever.  Default 60 s.
     *
     * A caster can stop sending without closing anything.  The socket
     * stays established, `recv` keeps returning "nothing yet", and a
     * session with no clock of its own will sit there reporting itself
     * connected for as long as the process lives -- which is how one
     * monitored stream delivered nothing for fourteen hours while every
     * status it published said it was fine.
     *
     * Silence is measured in bytes, not frames: a stream sending
     * something the framer cannot use is a different fault, and this
     * one must not be blamed for it.  The timer starts when the socket
     * is connected, so a caster that accepts and then never sends is
     * caught by the same rule.
     *
     * Expiry is treated exactly as a drop -- @ref auto_reconnect applies
     * -- and reported as @ref NS_END_STALLED.  The default is long
     * enough that no healthy mountpoint reaches it: a base station that
     * sends nothing at all for a minute has stopped.
     */
    double stall_timeout_s;

    /**
     * @brief Write every CRC-valid frame to this path; NULL = no capture.
     *
     * The convenience form of @ref ns_capture_start, applied at open.
     * What lands on disk is frames only: no handshake, nothing that
     * failed its CRC, and none of the bytes between frames.  A capture
     * is therefore clean input to a converter by construction, and one
     * made by any frontend is byte-identical to one made by another.
     *
     * The session refuses to overwrite an existing file.  A capture can
     * represent a day of streaming, which is not something to lose to a
     * repeated command.
     */
    const char *capture_path;

    /**
     * @brief Stop capturing once the file reaches this size; 0 = no limit.
     *
     * Reaching it is not an error: the file is closed at a frame
     * boundary and the session continues.  It exists because the machine
     * that captures for a day is often a Pi with a small card.
     */
    uint64_t    capture_max_bytes;
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
 * @brief Begin writing CRC-valid frames to @p path.
 *
 * The mid-session form of @ref NsOptions::capture_path, and the reason
 * both exist: a GUI starts and stops a capture from a menu while the
 * stream runs, which an option fixed at open cannot express.
 *
 * Refuses to overwrite an existing file, and refuses a second capture
 * over a running one.  Failures are reported as an @ref NS_EV_LOG at
 * @ref NS_LOG_ERROR, so a caller's error handling is the same here as
 * for a dead socket.
 *
 * @return 0 when capturing, non-zero when not.
 */
int ns_capture_start(NtripSession *s, const char *path);

/**
 * @brief Close the capture, flushing what is buffered.  Safe when idle.
 */
void ns_capture_stop(NtripSession *s);

/**
 * @brief What the capture has written.
 *
 * Deliberately not part of @ref NsStatsSnapshot: that structure is
 * serialised to the daemon's Munin output and the GUI's CSV export, and
 * a capture is not a property of the stream -- it is what we did with
 * it.
 *
 * @param bytes  Filled with the byte count when non-NULL.
 * @param frames Filled with the frame count when non-NULL.
 * @return The capture's path once one has been started (even after it
 *         closed), NULL when this session has never captured.
 */
const char *ns_capture_status(const NtripSession *s,
                              uint64_t *bytes, uint64_t *frames);

/**
 * @brief True when the capture the session was opened for did not happen.
 *
 * Covers both a @ref NsOptions::capture_path that could not be opened
 * and a write that failed later; either way the file the run existed to
 * produce is not there.  Deliberately **not** set by a failed
 * @ref ns_capture_start call, which is a menu item reporting a bad path
 * rather than a run losing its purpose.
 *
 * Distinguishes those from a capture that stopped because it reached
 * @ref NsOptions::capture_max_bytes, which is the feature working.
 */
bool ns_capture_failed(const NtripSession *s);

/**
 * @brief List the satellites the stream is currently carrying.
 *
 * Positions are not included: an observation stream never carries them.
 * The caller joins azimuth and elevation from its own source by
 * (gnss_id, prn) -- see `android/design/views.md`.
 *
 * @param s        Session.
 * @param out      [out] Destination; NULL to count only.
 * @param max      Capacity of @p out.
 * @return Number written, or the total when @p out is NULL.
 */
int ns_sat_list(const NtripSession *s, SvTrackEntry *out, int max);

/**
 * @brief Tell the session what the sourcetable advertises.
 *
 * Enables the advertised-versus-observed comparison, whose roll-up
 * appears in @ref NsStatsSnapshot as `types_missing`, `types_offrate`
 * and `types_extra`. Without it those stay zero and
 * `advertised_known` stays false, which the KPI reads as "cannot judge"
 * rather than "nothing wrong".
 *
 * The session copies the list; the caller may free it.
 *
 * @param s    Session.
 * @param list Advertised types and their promised intervals.
 * @param n    Number of entries.
 */
void ns_set_advertised(NtripSession *s, const SourcetableType *list, int n);

/**
 * @brief Tell the session which constellations the sourcetable claims.
 *
 * @param s    Session.
 * @param mask From @ref sourcetable_navsys_mask.
 */
void ns_set_advertised_gnss(NtripSession *s, unsigned mask);

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
