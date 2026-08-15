/**
 * @file ntrip_session.c
 * @brief The NTRIP stream session -- implementation.
 *
 * Structure:
 *   - platform socket helpers (the only platform-specific code here);
 *   - a framing state machine, written once;
 *   - statistics accumulation into an NsStatsSnapshot;
 *   - event dispatch to the caller.
 *
 * Frames are validated with `crc24q()` and their type read directly,
 * rather than by calling `analyze_rtcm_message()`.  That function decodes
 * and emits text, which a daemon does not want and a phone cannot
 * afford; a consumer that wants decoded output calls the decoders itself
 * from an NS_EV_FRAME handler.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "session/ntrip_session.h"
#include "core/version.h"
#include "core/rtcm3x_parser.h"   /* crc24q, msm_get_epoch, get_bits */
#include "core/nmea_parser.h"     /* GGA construction */
#include "core/sv_track.h"        /* satellites and C/N0 per constellation */
#include "core/iono.h"            /* ionospheric ROTI from dual-freq MSM7 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <errno.h>       /* why a capture write failed */

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET ns_sock_t;
  #define NS_INVALID_SOCK INVALID_SOCKET
#else
  #include <sys/socket.h>
  #include <sys/select.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  #include <errno.h>
  typedef int ns_sock_t;
  #define NS_INVALID_SOCK (-1)
  #define closesocket(s) close(s)
#endif

/** Largest RTCM 3.x frame: 3 header + 1023 payload + 3 CRC. */
#define NS_FRAME_MAX   1029
#define NS_RECV_BUF    8192
#define NS_HEADER_MAX  4096

/** How long an SV must be unseen before it leaves the tracked count. */
#define NS_SAT_STALE_S 5.0

/* ── Monotonic clock ──────────────────────────────────────────────── */

static double ns_now(void)
{
#ifdef _WIN32
    static LARGE_INTEGER freq;
    static int init = 0;
    LARGE_INTEGER now;
    if (!init) { QueryPerformanceFrequency(&freq); init = 1; }
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

/* ── Session state ───────────────────────────────────────────────── */

/** Longest capture path kept; longer ones are refused rather than cut. */
#define NS_CAP_PATH_MAX 512

/** How often a capture is flushed to the OS, in seconds.  A power cut
 *  or a kill then costs at most this much stream, and it is cheap: a
 *  frame arrives many times a second, a flush once. */
#define NS_CAP_FLUSH_S  1.0

/** Framing state machine positions. */
typedef enum {
    FR_SYNC = 0,   /**< hunting for the 0xD3 preamble */
    FR_HEAD,       /**< collecting the 3-byte header  */
    FR_BODY,       /**< collecting payload + CRC      */
} FrState;

struct NtripSession {
    NsOptions  opt;
    NsEventFn  cb;
    void      *user;

    /* Transport: exactly one of these is active. */
    ns_sock_t  sock;
    FILE      *file;
    bool       is_file;
    bool       own_file;   /* false when the caller owns it, e.g. stdin */

    bool       stopped;
    bool       ended;
    bool       connected;
    bool       header_done;
    bool       announced_streaming;
    bool       framing_enabled;    /**< ns_set_framing_enabled          */

    double     t0;             /**< session start (monotonic)          */
    double     t_start_unix;   /**< session start (wall clock)         */
    double     last_stats_t;
    double     last_gga_t;
    double     reconnect_at;   /**< monotonic time of the next attempt */
    int        backoff_s;

    /* Handshake accumulation. */
    char       header[NS_HEADER_MAX];
    int        header_len;
    NsHandshake handshake;

    /* Framing. */
    FrState        fr_state;
    unsigned char  frame[NS_FRAME_MAX];
    int            frame_pos;
    int            frame_len;

    /* Statistics. */
    NsStatsSnapshot stats;
    double     last_rate_t;
    uint64_t   last_rate_bytes;

    /* Capture-to-file.  Frames only: no handshake, nothing that failed
     * its CRC, none of the bytes between frames.  Kept here rather than
     * in NsStatsSnapshot because that structure is serialised to the
     * daemon's Munin output and the GUI's CSV export, and describes the
     * stream -- while this describes what we did with it. */
    FILE      *cap;
    char       cap_path[NS_CAP_PATH_MAX];
    uint64_t   cap_bytes;
    uint64_t   cap_frames;
    bool       cap_started;    /**< a capture was opened at some point */
    bool       cap_failed;     /**< a write failed; the session ended  */
    double     last_cap_flush_t;

    /* Which satellites the stream is currently carrying.  Lives here
     * rather than in a frontend so that every consumer -- the daemon's
     * snapshot, the GUI, Android -- reports the same numbers. */
    SvTrack    sv;

    /* Ionospheric phase arcs, for the same reason. */
    IonoState  iono;

    /* What the sourcetable promised, for the advertised-vs-observed
     * comparison.  Empty until a caller supplies it. */
    SourcetableType advertised[NS_MAX_TYPES];
    int             n_advertised;

    /* Per-type epoch tracking, so intervals are measured per epoch.
     * Indexed by the position in stats.types[]. */
    uint32_t   type_last_epoch[NS_MAX_TYPES];
    bool       type_has_epoch[NS_MAX_TYPES];
    double     type_last_time[NS_MAX_TYPES];
    double     type_sum_dt[NS_MAX_TYPES];
};

/* ── Event helpers ───────────────────────────────────────────────── */

static void emit(NtripSession *s, NsEvent *ev)
{
    if (!s->cb) return;
    ev->t_rel = ns_now() - s->t0;
    s->cb(ev, s->user);
}

static void emit_log(NtripSession *s, int level, const char *fmt, ...)
{
    if (!s->cb) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    NsEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = NS_EV_LOG;
    ev.u.log.level = level;
    ev.u.log.text  = buf;
    emit(s, &ev);
}

static void emit_end(NtripSession *s, int reason)
{
    if (s->ended) return;
    s->ended = true;
    s->stats.connected = false;

    NsEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = NS_EV_DISCONNECTED;
    ev.u.end.reason = reason;
    emit(s, &ev);
}

/* ── Capture ─────────────────────────────────────────────────────── */

/** @brief Close the capture file if one is open.  Emits nothing. */
static void cap_close(NtripSession *s)
{
    if (!s->cap) return;
    fclose(s->cap);
    s->cap = NULL;
}

/**
 * @brief Write one CRC-valid frame to the capture.
 *
 * @return true to carry on; false when the write failed, in which case
 *         the session has already been ended with NS_END_WRITE_ERROR.
 */
static bool cap_frame(NtripSession *s, const unsigned char *frame, int len)
{
    if (!s->cap) return true;

    /* A limit the caller set, so reaching it is the feature working:
     * close on a frame boundary -- never mid-frame, which would leave a
     * file no converter can read to its end -- and stream on. */
    if (s->opt.capture_max_bytes &&
        s->cap_bytes + (uint64_t)len > s->opt.capture_max_bytes) {
        cap_close(s);
        emit_log(s, NS_LOG_INFO,
                 "Capture reached its limit of %llu bytes: %s (%llu frames)",
                 (unsigned long long)s->opt.capture_max_bytes, s->cap_path,
                 (unsigned long long)s->cap_frames);
        return true;
    }

    size_t w = fwrite(frame, 1, (size_t)len, s->cap);
    if (w != (size_t)len) {
        /* The disk filled, or the volume went away.  Twenty hours of
         * capture that silently stopped at hour three is the outcome
         * this feature exists to make impossible, so a short write is
         * fatal to the session rather than a warning in a log nobody
         * reads until afterwards. */
        s->cap_bytes += (uint64_t)w;
        cap_close(s);
        s->cap_failed = true;
        emit_log(s, NS_LOG_ERROR,
                 "Capture write failed after %llu bytes: %s: %s",
                 (unsigned long long)s->cap_bytes, s->cap_path,
                 strerror(errno));
        emit_end(s, NS_END_WRITE_ERROR);
        return false;
    }

    s->cap_bytes  += (uint64_t)len;
    s->cap_frames += 1;
    return true;
}

/** @brief Flush the capture, at most once every NS_CAP_FLUSH_S. */
static void cap_maybe_flush(NtripSession *s, double now)
{
    if (!s->cap) return;
    if (now - s->last_cap_flush_t < NS_CAP_FLUSH_S) return;
    s->last_cap_flush_t = now;
    fflush(s->cap);
}

/* ── Socket helpers ──────────────────────────────────────────────── */

static bool sock_startup(void)
{
#ifdef _WIN32
    static bool done = false;
    if (done) return true;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
    done = true;
#endif
    return true;
}

/** @brief Connect to host:port.  Returns NS_INVALID_SOCK on failure. */
static ns_sock_t sock_connect(const char *host, int port)
{
    if (!sock_startup()) return NS_INVALID_SOCK;

    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;      /* IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res)
        return NS_INVALID_SOCK;

    ns_sock_t sk = NS_INVALID_SOCK;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        sk = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sk == NS_INVALID_SOCK) continue;
        if (connect(sk, ai->ai_addr, (int)ai->ai_addrlen) == 0) break;
        closesocket(sk);
        sk = NS_INVALID_SOCK;
    }
    freeaddrinfo(res);
    return sk;
}

/**
 * @brief Receive with a timeout.
 * @return >0 bytes, 0 on timeout, <0 on close or error.
 */
static int sock_recv(ns_sock_t sk, unsigned char *buf, int cap, int timeout_ms)
{
    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(sk, &rd);

    struct timeval tv;
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int r = select((int)sk + 1, &rd, NULL, NULL, &tv);
    if (r == 0)  return 0;
    if (r < 0)   return -1;

    int n = (int)recv(sk, (char *)buf, cap, 0);
    if (n == 0) return -1;      /* peer closed */
    if (n < 0)  return -1;
    return n;
}

/* ── Statistics ──────────────────────────────────────────────────── */

/** @brief Index of a type in stats.types[], creating it if needed. */
static int type_slot(NtripSession *s, int msg_type)
{
    for (int i = 0; i < s->stats.n_types; i++)
        if (s->stats.types[i].msg_type == msg_type) return i;

    if (s->stats.n_types >= NS_MAX_TYPES) {
        s->stats.types_truncated = true;
        return -1;
    }
    int i = s->stats.n_types;
    if (!ns_stats_type(&s->stats, msg_type)) return -1;
    s->type_has_epoch[i] = false;
    s->type_last_time[i] = 0.0;
    s->type_sum_dt[i]    = 0.0;
    return i;
}

/**
 * @brief Fold one accepted frame into the statistics.
 *
 * Interval statistics advance per **epoch**, not per frame.  MSM splits
 * one epoch across several frames of the same type when the observations
 * do not fit in one, and counting frames would report a base transmitting
 * at a multiple of its true rate.
 *
 * @return true if this frame began a new epoch.
 */
static bool stats_frame(NtripSession *s, int msg_type,
                        uint32_t epoch, bool has_epoch, double now)
{
    s->stats.frames_ok++;

    int i = type_slot(s, msg_type);
    if (i < 0) return true;

    NsTypeStats *t = &s->stats.types[i];
    t->frames++;

    bool new_epoch = !has_epoch
                     || !s->type_has_epoch[i]
                     || epoch != s->type_last_epoch[i];

    if (has_epoch) {
        s->type_last_epoch[i] = epoch;
        s->type_has_epoch[i]  = true;
    }

    if (new_epoch) {
        if (t->epochs > 0) {
            double dt = now - s->type_last_time[i];
            s->type_sum_dt[i] += dt;
            if (t->min_dt == 0.0 || dt < t->min_dt) t->min_dt = dt;
            if (dt > t->max_dt) t->max_dt = dt;
            t->avg_dt = s->type_sum_dt[i] / (double)t->epochs;
        }
        s->type_last_time[i] = now;
        t->epochs++;
    }
    return new_epoch;
}

/** @brief Refresh derived fields before publishing a snapshot. */
static void compare_advertised(NtripSession *s);

static void stats_refresh(NtripSession *s, double now)
{
    s->stats.uptime_s = now - s->t0;

    uint64_t checked = s->stats.frames_ok + s->stats.frames_crc_error;
    s->stats.crc_error_rate =
        checked ? (double)s->stats.frames_crc_error / (double)checked : 0.0;

    /* The window must span several update cycles of the stream.  RTCM
     * casters typically emit one burst per second; with a 0.5 s window a
     * measurement landing between bursts reads 0 B/s and one landing on
     * a burst reads double, so the published rate depends on window
     * phase.  2 s always covers at least two bursts. */
    double dt = now - s->last_rate_t;
    if (dt >= 2.0) {
        s->stats.bytes_per_s =
            (double)(s->stats.bytes_total - s->last_rate_bytes) / dt;
        s->last_rate_bytes = s->stats.bytes_total;
        s->last_rate_t     = now;
    }

    /* Recomputed rather than accumulated: a satellite leaving view has to
     * lower the count, and only a fresh sweep against the staleness
     * window can do that. */
    sv_track_summarise(&s->sv, now, SV_TRACK_STALE_S,
                       s->stats.gnss, NS_MAX_GNSS, &s->stats.n_gnss,
                       &s->stats.sats_total, &s->stats.cnr_mean_all);

    IonoSummary is;
    iono_summarise(&s->iono, now, &is);
    s->stats.iono_verdict       = is.verdict;
    s->stats.iono_roti_median   = is.roti_median;
    s->stats.iono_roti_max      = is.roti_max;
    s->stats.iono_sats_dualfreq = is.sats_dualfreq;
    s->stats.iono_slips         = is.slips_total;

    compare_advertised(s);
}

int ns_iono_view(const NtripSession *s, IonoSatView *out, int max_out)
{
    /* Implemented here rather than by handing the IonoState out: the
     * staleness test inside iono_sat_view() compares against the clock
     * iono_feed() was driven with, which is this session's ns_now() --
     * a caller using its own clock would misjudge staleness. */
    if (!s) return 0;
    return iono_sat_view(&s->iono, ns_now(), out, max_out);
}

static void maybe_emit_stats(NtripSession *s, double now)
{
    if (s->opt.stats_interval_s <= 0.0) return;
    if (now - s->last_stats_t < s->opt.stats_interval_s) return;
    s->last_stats_t = now;

    stats_refresh(s, now);

    NsEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = NS_EV_STATS;
    ev.u.stats = &s->stats;
    emit(s, &ev);
}

/* ── Framing ─────────────────────────────────────────────────────── */

/**
 * @brief Feed received bytes through the framing state machine.
 *
 * One implementation, replacing the ten that existed across the CLI,
 * the old handler and the GUI worker.
 */
static void feed(NtripSession *s, const unsigned char *data, int len)
{
    double now = ns_now();
    s->stats.bytes_total += (uint64_t)len;

    /* Raw first: format sniffing needs the bytes even when -- especially
     * when -- they will never assemble into an RTCM frame. */
    {
        NsEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.type       = NS_EV_RAW;
        ev.u.raw.data = data;
        ev.u.raw.len  = len;
        emit(s, &ev);
    }
    if (!s->framing_enabled) return;

    for (int i = 0; i < len; i++) {
        unsigned char b = data[i];

        switch (s->fr_state) {
        case FR_SYNC:
            if (b == 0xD3) {
                s->frame[0]   = b;
                s->frame_pos  = 1;
                s->fr_state   = FR_HEAD;
            }
            /* Bytes outside a frame are not counted as malformed: a
             * stream legitimately begins mid-frame, and NMEA or comments
             * between frames are common. */
            break;

        case FR_HEAD:
            s->frame[s->frame_pos++] = b;
            if (s->frame_pos == 3) {
                int payload = ((s->frame[1] & 0x03) << 8) | s->frame[2];
                s->frame_len = payload + 6;      /* preamble+len+payload+CRC */
                if (s->frame_len > NS_FRAME_MAX || payload == 0) {
                    s->stats.framing_resyncs++;
                    NsEvent ev;
                    memset(&ev, 0, sizeof(ev));
                    ev.type = NS_EV_FRAME_BAD;
                    ev.u.bad.reason = NS_BAD_LENGTH;
                    emit(s, &ev);
                    s->fr_state  = FR_SYNC;
                    s->frame_pos = 0;
                } else {
                    s->fr_state = FR_BODY;
                }
            }
            break;

        case FR_BODY:
            s->frame[s->frame_pos++] = b;
            if (s->frame_pos < s->frame_len) break;

            /* Complete frame: validate before believing anything in it. */
            {
                uint32_t want = ((uint32_t)s->frame[s->frame_len - 3] << 16) |
                                ((uint32_t)s->frame[s->frame_len - 2] << 8)  |
                                 (uint32_t)s->frame[s->frame_len - 1];
                uint32_t got = crc24q(s->frame, (size_t)(s->frame_len - 3));

                if (got != want) {
                    s->stats.frames_crc_error++;
                    NsEvent ev;
                    memset(&ev, 0, sizeof(ev));
                    ev.type = NS_EV_FRAME_BAD;
                    ev.u.bad.reason = NS_BAD_CRC;
                    emit(s, &ev);
                } else {
                    int payload_len = s->frame_len - 6;
                    int msg_type = (int)get_bits(s->frame + 3, 0, 12);

                    uint32_t epoch = 0;
                    bool has_epoch = msm_get_epoch(s->frame + 3, payload_len,
                                                   msg_type, &epoch) != 0;
                    bool new_epoch = stats_frame(s, msg_type, epoch,
                                                 has_epoch, now);

                    /* sv_track also reads the legacy observation
                     * messages; iono ignores everything but MSM6/7.
                     * Neither needs the caller to classify. */
                    sv_track_feed(&s->sv, s->frame + 3, payload_len,
                                  msg_type, now);
                    iono_feed(&s->iono, s->frame + 3, payload_len,
                              msg_type, now);

                    /* The broadcast reference position.  These snapshot
                     * fields existed since the schema was written but
                     * nothing filled them -- the daemon published
                     * arp_valid:false for every station until the KPI
                     * engine's first live run tripped over it. */
                    if (msg_type == 1005 || msg_type == 1006) {
                        RtcmArpInfo a;
                        if (rtcm_extract_arp_info(s->frame + 3, payload_len,
                                                  msg_type, &a)) {
                            s->stats.arp_valid = true;
                            s->stats.arp_lat = a.lat_deg;
                            s->stats.arp_lon = a.lon_deg;
                            s->stats.arp_alt = a.alt_m;
                            s->stats.arp_says_gps     = a.gps;
                            s->stats.arp_says_glonass = a.glonass;
                            s->stats.arp_says_galileo = a.galileo;
                        }
                    }

                    if (!s->announced_streaming) {
                        s->announced_streaming = true;
                        NsEvent ev;
                        memset(&ev, 0, sizeof(ev));
                        ev.type = NS_EV_STREAMING;
                        emit(s, &ev);
                    }

                    /* Capture before the event, so what a consumer is
                     * told about and what reaches the disk are the same
                     * frame -- and so a capture written by any frontend
                     * is byte-identical to one written by another. */
                    if (!cap_frame(s, s->frame, s->frame_len)) {
                        s->fr_state  = FR_SYNC;
                        s->frame_pos = 0;
                        return;              /* the session has ended */
                    }

                    NsEvent ev;
                    memset(&ev, 0, sizeof(ev));
                    ev.type = NS_EV_FRAME;
                    ev.u.frame.data      = s->frame;
                    ev.u.frame.len       = s->frame_len;
                    ev.u.frame.msg_type  = msg_type;
                    ev.u.frame.epoch     = epoch;
                    ev.u.frame.has_epoch = has_epoch;
                    ev.u.frame.new_epoch = new_epoch;
                    emit(s, &ev);
                }
            }
            s->fr_state  = FR_SYNC;
            s->frame_pos = 0;
            break;
        }
    }
}

/* ── Connection ──────────────────────────────────────────────────── */

static void do_connect(NtripSession *s)
{
    NsEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = NS_EV_CONNECTING;
    emit(s, &ev);

    s->sock = sock_connect(s->opt.config.NTRIP_CASTER, s->opt.config.NTRIP_PORT);
    if (s->sock == NS_INVALID_SOCK) {
        emit_log(s, NS_LOG_ERROR, "Cannot reach %s:%d",
                 s->opt.config.NTRIP_CASTER, s->opt.config.NTRIP_PORT);
        if (!s->opt.auto_reconnect) { emit_end(s, NS_END_NET_ERROR); return; }
        s->backoff_s = s->backoff_s ? s->backoff_s * 2 : 1;
        if (s->backoff_s > s->opt.reconnect_backoff_max_s)
            s->backoff_s = s->opt.reconnect_backoff_max_s;
        s->reconnect_at = ns_now() + s->backoff_s;
        return;
    }

    char req[1024];
    /* Every front end sets this.  The fallback is deliberately its own
     * name rather than the CLI's, so that a caller which forgot shows up
     * in the caster's log as a bug and not as a different artefact. */
    const char *agent = s->opt.user_agent ? s->opt.user_agent
                                          : NTRIP_USER_AGENT(NTRIP_ARTEFACT_LIB);
    int n = ns_proto_build_request(req, sizeof(req),
                                   s->opt.config.NTRIP_CASTER,
                                   s->opt.config.MOUNTPOINT,
                                   s->opt.config.USERNAME,
                                   s->opt.config.PASSWORD,
                                   agent);
    if (n <= 0 || (size_t)n >= sizeof(req)) {
        emit_log(s, NS_LOG_ERROR, "Request too long to build");
        closesocket(s->sock);
        s->sock = NS_INVALID_SOCK;
        emit_end(s, NS_END_NET_ERROR);
        return;
    }

    if (send(s->sock, req, n, 0) <= 0) {
        emit_log(s, NS_LOG_ERROR, "Failed to send the request");
        closesocket(s->sock);
        s->sock = NS_INVALID_SOCK;
        emit_end(s, NS_END_NET_ERROR);
        return;
    }

    s->connected   = true;
    s->header_len  = 0;
    s->header_done = false;
    s->backoff_s   = 0;
    s->stats.connected = true;
}

/**
 * @brief Accumulate response header bytes; returns payload offset or -1.
 *
 * The caster's reply is parsed from its status line only.  Searching the
 * whole header for "200" accepts a 404 whose Content-Length happens to be
 * 200, after which an HTML error page gets decoded as RTCM.
 */
static int take_header(NtripSession *s, const unsigned char *data, int len)
{
    int prev = s->header_len;

    int space = NS_HEADER_MAX - 1 - s->header_len;
    int take  = len < space ? len : space;
    memcpy(s->header + s->header_len, data, (size_t)take);
    s->header_len += take;
    s->header[s->header_len] = '\0';

    int end = ns_proto_header_end((const unsigned char *)s->header, s->header_len);
    if (end < 0) return -1;

    /* The caller needs the payload offset within ITS buffer, not within
     * the accumulated header.  When the header spans several recvs the
     * two differ by the bytes accumulated before this call; conflating
     * them either drops the first payload bytes or re-feeds header tail
     * as RTCM. */
    int off_in_buf = end - prev;
    if (off_in_buf < 0) off_in_buf = 0;   /* terminator straddled calls */

    s->header_done = true;
    ns_proto_parse_response(s->header, &s->handshake);

    s->stats.ntrip_version = s->handshake.version;
    s->stats.http_status   = s->handshake.status;
    /* snprintf rather than strncpy: the truncation is intended -- a
     * caster may announce a longer name than the field holds -- and
     * glibc's -Wstringop-truncation says so loudly for strncpy while
     * this form is both silent and always NUL-terminated. */
    snprintf(s->stats.caster_software, sizeof(s->stats.caster_software),
             "%s", s->handshake.server);

    NsEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = NS_EV_HANDSHAKE;
    ev.u.handshake = &s->handshake;
    emit(s, &ev);

    if (!s->handshake.valid || s->handshake.status != 200) {
        emit_log(s, NS_LOG_ERROR, "Caster rejected the request: %s",
                 s->handshake.status_line[0] ? s->handshake.status_line
                                             : "(unrecognised response)");
        closesocket(s->sock);
        s->sock = NS_INVALID_SOCK;
        s->connected = false;
        emit_end(s, NS_END_REJECTED);
        return -1;
    }
    if (s->handshake.version == NS_PROTO_V1) {
        emit_log(s, NS_LOG_INFO,
                 "Caster answered NTRIP 1.0 (ICY) although Ntrip/2.0 was requested");
    }

    /* Bytes beyond the terminator in this recv are already payload. */
    return off_in_buf;
}

/** @brief Send a GGA sentence, as VRS mountpoints require. */
static void maybe_send_gga(NtripSession *s, double now)
{
    if (!s->opt.send_gga || !s->connected || s->sock == NS_INVALID_SOCK) return;
    double iv = s->opt.gga_interval_s > 0.0 ? s->opt.gga_interval_s : 1.0;
    if (now - s->last_gga_t < iv) return;
    s->last_gga_t = now;

    char gga[256];
    create_gngga_sentence(s->opt.config.LATITUDE, s->opt.config.LONGITUDE, gga);

    size_t n = strlen(gga);
    if (n && gga[n - 1] != '\n') {
        char line[300];
        snprintf(line, sizeof(line), "%s\r\n", gga);
        send(s->sock, line, (int)strlen(line), 0);
    } else {
        send(s->sock, gga, (int)n, 0);
    }
}

/* ── Public API ──────────────────────────────────────────────────── */

void ns_options_default(NsOptions *opt)
{
    if (!opt) return;
    memset(opt, 0, sizeof(*opt));
    opt->stats_interval_s       = 1.0;
    opt->gga_interval_s         = 1.0;
    opt->reconnect_backoff_max_s = 60;
}

static NtripSession *alloc_session(const NsOptions *opt, NsEventFn cb, void *user)
{
    NtripSession *s = (NtripSession *)calloc(1, sizeof(NtripSession));
    if (!s) return NULL;

    if (opt) s->opt = *opt;
    else     ns_options_default(&s->opt);

    s->cb   = cb;
    s->user = user;
    s->sock = NS_INVALID_SOCK;
    s->framing_enabled = true;
    s->t0   = ns_now();
    s->t_start_unix  = (double)time(NULL);
    s->last_stats_t  = s->t0;
    s->last_gga_t    = 0.0;
    s->last_rate_t   = s->t0;

    ns_stats_init(&s->stats);
    sv_track_reset(&s->sv);
    s->stats.t_start_unix = s->t_start_unix;
    /* As above: intended truncation, stated in a form the compiler
     * does not have to guess about. */
    snprintf(s->stats.mountpoint, sizeof(s->stats.mountpoint), "%s",
             s->opt.config.MOUNTPOINT);
    snprintf(s->stats.caster, sizeof(s->stats.caster), "%s",
             s->opt.config.NTRIP_CASTER);
    return s;
}

/**
 * @brief Apply @ref NsOptions::capture_path, when one was given.
 *
 * A failure here ends the session before a byte is read.  An unattended
 * run whose whole purpose is the file must discover a bad path in its
 * first second rather than its twentieth hour.
 */
static void capture_from_options(NtripSession *s)
{
    if (!s->opt.capture_path || !s->opt.capture_path[0]) return;
    if (ns_capture_start(s, s->opt.capture_path) != 0) {
        /* The capture the caller asked for did not happen, which is the
         * same news to them as a write that failed later: the artefact
         * will not exist.  ns_capture_start() on its own does not set
         * this -- a GUI that offers a menu item can report a bad path
         * and carry on streaming. */
        s->cap_failed = true;
        emit_end(s, NS_END_WRITE_ERROR);
    }
}

NtripSession *ns_open(const NsOptions *opt, NsEventFn cb, void *user)
{
    NtripSession *s = alloc_session(opt, cb, user);
    if (!s) return NULL;
    s->is_file = false;
    capture_from_options(s);
    return s;
}

NtripSession *ns_open_stream(FILE *f, bool own, const NsOptions *opt,
                             NsEventFn cb, void *user)
{
    if (!f) return NULL;
    NtripSession *s = alloc_session(opt, cb, user);
    if (!s) return NULL;

    s->is_file  = true;
    s->file     = f;
    s->own_file = own;

    /* A capture has no handshake; it is payload from the first byte. */
    s->header_done = true;
    s->connected   = true;
    s->stats.connected = true;
    capture_from_options(s);
    return s;
}

NtripSession *ns_open_file(const char *path, const NsOptions *opt,
                           NsEventFn cb, void *user)
{
    if (!path) return NULL;

    FILE *f = fopen(path, "rb");
    if (!f) {
        /* Report through the session so the caller's error handling is
         * the same whether the failure is a bad path or a dead socket. */
        NtripSession *s = alloc_session(opt, cb, user);
        if (!s) return NULL;
        s->is_file = true;
        emit_log(s, NS_LOG_ERROR, "Cannot open capture: %s", path);
        emit_end(s, NS_END_NET_ERROR);
        return s;   /* caller still closes it */
    }
    return ns_open_stream(f, true, opt, cb, user);
}

int ns_pump(NtripSession *s, int timeout_ms)
{
    if (!s || s->ended) return -1;
    double now = ns_now();

    /* Once a pump rather than once a frame: a kill -9 or a power cut
     * then costs at most a second of stream, and the flush is invisible
     * beside the socket read it sits next to. */
    cap_maybe_flush(s, now);

    if (s->stopped) { emit_end(s, NS_END_STOPPED); return -1; }

    /* ── Replay ── */
    if (s->is_file) {
        if (!s->file) { emit_end(s, NS_END_EOF); return -1; }
        unsigned char buf[NS_RECV_BUF];
        size_t n = fread(buf, 1, sizeof(buf), s->file);
        if (n == 0) {
            stats_refresh(s, now);
            maybe_emit_stats(s, now);
            emit_end(s, NS_END_EOF);
            return -1;
        }
        feed(s, buf, (int)n);
        stats_refresh(s, now);
        maybe_emit_stats(s, now);
        return (int)n;
    }

    /* ── Live ── */
    if (!s->connected) {
        if (s->reconnect_at > 0.0 && now < s->reconnect_at) return 0;
        if (s->reconnect_at > 0.0) {
            s->stats.reconnects++;
            s->reconnect_at = 0.0;
        }
        do_connect(s);
        if (s->ended) return -1;
        if (!s->connected) return 0;
        now = ns_now();
    }

    maybe_send_gga(s, now);

    unsigned char buf[NS_RECV_BUF];
    int n = sock_recv(s->sock, buf, sizeof(buf), timeout_ms);
    if (n == 0) {
        stats_refresh(s, now);
        maybe_emit_stats(s, now);
        return 0;                    /* timeout: normal, keep pumping */
    }
    if (n < 0) {
        closesocket(s->sock);
        s->sock = NS_INVALID_SOCK;
        s->connected = false;
        s->stats.connected = false;
        emit_log(s, NS_LOG_WARN, "Connection closed by the caster");
        if (!s->opt.auto_reconnect) { emit_end(s, NS_END_EOF); return -1; }
        s->backoff_s = s->backoff_s ? s->backoff_s * 2 : 1;
        if (s->backoff_s > s->opt.reconnect_backoff_max_s)
            s->backoff_s = s->opt.reconnect_backoff_max_s;
        s->reconnect_at = now + s->backoff_s;
        return 0;
    }

    int off = 0;
    if (!s->header_done) {
        off = take_header(s, buf, n);
        if (s->ended) return -1;
        if (off < 0) return n;       /* header still incomplete */
    }
    if (off < n) feed(s, buf + off, n - off);

    stats_refresh(s, now);
    maybe_emit_stats(s, now);
    return n;
}

void ns_run(NtripSession *s, const volatile int *stop_flag)
{
    if (!s) return;
    while (!s->ended) {
        if (stop_flag && *stop_flag) { ns_stop(s); }
        if (ns_pump(s, 200) < 0) break;
    }
}

void ns_stop(NtripSession *s)      { if (s) s->stopped = true; }

void ns_close(NtripSession *s)
{
    if (!s) return;
    /* Before the socket: the capture is the artefact the run existed to
     * produce, and fclose() is what commits its tail to the disk. */
    cap_close(s);
    if (s->sock != NS_INVALID_SOCK) closesocket(s->sock);
    /* Only close what this session opened.  Closing a caller's handle --
     * stdin, above all -- would break the program around us. */
    if (s->file && s->own_file) fclose(s->file);
    free(s);
}

const NsStatsSnapshot *ns_stats(const NtripSession *s)
{
    return s ? &s->stats : NULL;
}

/* ── Capture, public ─────────────────────────────────────────────── */

int ns_capture_start(NtripSession *s, const char *path)
{
    if (!s || !path || !path[0]) return -1;

    if (s->cap) {
        emit_log(s, NS_LOG_WARN, "A capture is already running: %s",
                 s->cap_path);
        return -1;
    }
    if (strlen(path) >= sizeof(s->cap_path)) {
        emit_log(s, NS_LOG_ERROR, "Capture path is longer than %d characters",
                 (int)sizeof(s->cap_path) - 1);
        return -1;
    }

    /* Refuse to overwrite.  Deliberately unlike the sky PNG, which
     * overwrites by design: a PNG costs a minute to redraw, and a
     * capture can be a day of streaming that cannot be had again. */
    FILE *probe = fopen(path, "rb");
    if (probe) {
        fclose(probe);
        emit_log(s, NS_LOG_ERROR,
                 "Capture will not overwrite an existing file: %s", path);
        return -1;
    }

    /* Binary, and not negotiable.  On Windows a text-mode handle
     * translates CRLF pairs occurring inside RTCM payloads, and the
     * capture reads back as one frame instead of hundreds. */
    FILE *f = fopen(path, "wb");
    if (!f) {
        emit_log(s, NS_LOG_ERROR, "Cannot open capture for writing: %s: %s",
                 path, strerror(errno));
        return -1;
    }

    s->cap = f;
    strncpy(s->cap_path, path, sizeof(s->cap_path) - 1);
    s->cap_path[sizeof(s->cap_path) - 1] = '\0';
    s->cap_bytes        = 0;
    s->cap_frames       = 0;
    s->cap_started      = true;
    s->cap_failed       = false;
    s->last_cap_flush_t = ns_now();
    emit_log(s, NS_LOG_INFO, "Capturing frames to %s", path);
    return 0;
}

void ns_capture_stop(NtripSession *s)
{
    if (!s || !s->cap) return;
    cap_close(s);
    emit_log(s, NS_LOG_INFO,
             "Capture stopped: %llu bytes, %llu frames -> %s",
             (unsigned long long)s->cap_bytes,
             (unsigned long long)s->cap_frames, s->cap_path);
}

const char *ns_capture_status(const NtripSession *s,
                              uint64_t *bytes, uint64_t *frames)
{
    if (!s) return NULL;
    if (bytes)  *bytes  = s->cap_bytes;
    if (frames) *frames = s->cap_frames;
    return s->cap_started ? s->cap_path : NULL;
}

bool ns_capture_failed(const NtripSession *s)
{
    return s && s->cap_failed;
}

const NsHandshake *ns_handshake(const NtripSession *s)
{
    if (!s || !s->handshake.valid) return NULL;
    return &s->handshake;
}

bool ns_send_gga(NtripSession *s, double lat, double lon)
{
    if (!s || s->is_file || !s->connected || s->sock == NS_INVALID_SOCK)
        return false;

    char gga[256];
    create_gngga_sentence(lat, lon, gga);

    char line[300];
    size_t n = strlen(gga);
    if (n && gga[n - 1] != '\n')
        snprintf(line, sizeof(line), "%s\r\n", gga);
    else
        snprintf(line, sizeof(line), "%s", gga);

    return send(s->sock, line, (int)strlen(line), 0) > 0;
}

void ns_set_framing_enabled(NtripSession *s, bool enabled)
{
    if (s) s->framing_enabled = enabled;
}

double ns_uptime(const NtripSession *s)
{
    return s ? (ns_now() - s->t0) : 0.0;
}

int ns_sat_list(const NtripSession *s, SvTrackEntry *out, int max)
{
    if (!s) return 0;
    return sv_track_list(&s->sv, ns_now(), NS_SAT_STALE_S, out, max);
}

void ns_set_advertised(NtripSession *s, const SourcetableType *list, int n)
{
    if (!s) return;
    if (n > NS_MAX_TYPES) n = NS_MAX_TYPES;
    s->n_advertised = (list && n > 0) ? n : 0;
    if (s->n_advertised)
        memcpy(s->advertised, list, sizeof(SourcetableType) * (size_t)n);
    s->stats.advertised_known = s->n_advertised > 0;
    s->stats.advertised_count = s->n_advertised;
}

/**
 * @brief Compare what was promised against what arrived.
 *
 * Called from the statistics refresh, so the counts track the stream
 * rather than being computed once at the end.
 */
static void compare_advertised(NtripSession *s)
{
    s->stats.types_missing = 0;
    s->stats.types_offrate = 0;
    s->stats.types_extra   = 0;
    if (s->n_advertised <= 0) return;

    for (int a = 0; a < s->n_advertised; a++) {
        const SourcetableType *adv = &s->advertised[a];
        const NsTypeStats *seen = NULL;
        for (int i = 0; i < s->stats.n_types; i++)
            if (s->stats.types[i].msg_type == adv->type) {
                seen = &s->stats.types[i];
                break;
            }

        /* An observation message for a constellation with nothing in
         * view is not a broken promise: the station has nothing to put
         * in it.  APEL00NLD0 advertises 1117 across the Netherlands and
         * QZSS is visible from none of it, so counting that as missing
         * hard-failed a perfectly healthy base. */
        int adv_gnss = get_gnss_id_from_rtcm(adv->type);
        if (adv_gnss > 0) {
            int tracked_there = 0;
            for (int g = 0; g < s->stats.n_gnss; g++)
                if (s->stats.gnss[g].gnss_id == adv_gnss)
                    tracked_there = s->stats.gnss[g].sats_tracked;
            if (tracked_there == 0) continue;
        }

        if (!seen || seen->frames == 0) {
            /* Not arrived *yet* is not the same as not sent.  A type
             * promised every 15 s cannot be called missing after 20,
             * and an ephemeris type may legitimately take minutes, so
             * each is given several of its own advertised intervals
             * before it counts against the station.  Judging early
             * fails healthy stations, which is the fastest way to make
             * a check worth ignoring. */
            /* A type advertised without an interval promises no rate,
             * so lateness cannot be judged against it.  Ephemeris types
             * are exactly this case -- 1019, 1020, 1042, 1044-1046 are
             * routinely advertised bare and broadcast on a slow cycle,
             * a handful per minute.  They are given ten minutes, which
             * a ninety-second check never reaches and a long watch
             * does: the short run judges only what was promised with a
             * rate, which is all the sourcetable actually committed to. */
            double grace = (adv->interval_s > 0.0)
                           ? adv->interval_s * 3.0 : 600.0;
            if (grace < 30.0) grace = 30.0;
            if (s->stats.uptime_s >= grace) s->stats.types_missing++;
            continue;
        }

        /* Off-rate only where a rate was actually promised, and only
         * once enough epochs have passed to have measured one.  A 25%
         * tolerance keeps ordinary jitter from reading as a fault. */
        if (adv->interval_s > 0.0 && seen->epochs >= 3 && seen->avg_dt > 0.0) {
            double ratio = seen->avg_dt / adv->interval_s;
            if (ratio > 1.25 || ratio < 0.75) s->stats.types_offrate++;
        }
    }

    for (int i = 0; i < s->stats.n_types; i++) {
        bool promised = false;
        for (int a = 0; a < s->n_advertised; a++)
            if (s->advertised[a].type == s->stats.types[i].msg_type) {
                promised = true;
                break;
            }
        if (!promised) s->stats.types_extra++;
    }
}

void ns_set_advertised_gnss(NtripSession *s, unsigned mask)
{
    if (s) s->stats.advertised_gnss = mask;
}
