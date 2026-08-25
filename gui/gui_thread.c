/**
 * @file gui_thread.c
 * @brief Worker thread entry points for NTRIP-Analyser GUI.
 *
 * Each function runs on a background thread and communicates results
 * back to the UI thread via PostMessage with WM_APP+n messages.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "resource.h"
#include "gui_state.h"
#include "core/rtcm3x_parser.h"
#include "core/nmea_parser.h"
#include "core/sv_ephemeris.h"
#include "core/sv_orbit.h"
#include "session/ntrip_session.h"
#include "core/sourcetable.h"
#include "core/version.h"
#include "gui_check_window.h"
#include "gui_report_window.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>    /* getenv, for the temporary capture comparison */
#include <time.h>
#include <ctype.h>

/**
 * @brief Coarse GPS time of day for sky-plot orbit propagation.
 *
 * The sky plot only needs second-level precision (an error of 10 s shifts
 * an SV by ~37 km along its orbit, well below sky-plot pixel resolution),
 * so we ignore leap seconds and simply derive GPS week + ToW from the
 * system clock using the GPS epoch (1980-01-06 00:00:00 UTC).
 */
static void sky_get_gps_time(int *week, double *tow_s)
{
    const time_t GPS_EPOCH_UNIX = 315964800;   /* 1980-01-06 UTC */
    time_t now = time(NULL);
    double delta = (double)(now - GPS_EPOCH_UNIX);
    int w = (int)(delta / 604800.0);
    if (week)  *week  = w;
    if (tow_s) *tow_s = delta - (double)w * 604800.0;
}

/**
 * @brief Moscow seconds-of-day for GLONASS orbit propagation.
 *
 * GLONASS time = UTC + 3 h (no leap-second offset vs UTC).  We use the
 * system clock directly, add 10800 seconds, and wrap into [0, 86400).
 */
static double sky_get_glo_tod(void)
{
    time_t now = time(NULL);
    double utc_sod = (double)(now % 86400);
    double msk = utc_sod + 10800.0;
    while (msk >= 86400.0) msk -= 86400.0;
    while (msk <    0.0)   msk += 86400.0;
    return msk;
}

/* stristr() now lives in gui_parsers.c -- the station classifier in
 * gui_events.c needs it too. */

/* ── Get Mountpoints worker ──────────────────────────────── */

DWORD WINAPI WorkerGetMountpoints(LPVOID param)
{
    AppState *state = (AppState *)param;

    char *mount_table = receive_mount_table(&state->config,
                            NTRIP_USER_AGENT(NTRIP_ARTEFACT_GUI));

    /* Post result to UI thread: wParam=0 success, 1 error; lParam=heap string */
    PostMessage(state->hMain, WM_APP_MOUNT_RESULT,
                (WPARAM)(mount_table ? 0 : 1),
                (LPARAM)mount_table);

    return 0;
}

/* ── Open Stream worker (session-layer adapter) ───────────────────────
 *
 * The transport half of the old worker -- DNS, socket, request, header
 * parse, RTCM framing, CRC -- now lives in src/session/ntrip_session.c,
 * shared with the CLI-to-be and the monitoring service.  What remains
 * here is what is genuinely the GUI's: format sniffing for non-RTCM
 * streams, capture-to-disk, per-type statistics posting, satellite and
 * sky updates, detail-window frames, and the interactive GGA logic the
 * VRS test features drive.
 *
 * The worker thread owns the pump; every callback below runs on this
 * thread, exactly as the old inline code did, so the threading contract
 * with the UI (PostMessage everything) is unchanged.
 */

/* ── Stream format IDs (matches gui_state.h / gui_events.c) ── */
#define FMT_NONE     0
#define FMT_RTCM3    1
#define FMT_UBX      2
#define FMT_SBF      3
#define FMT_RT27     4
#define FMT_LB2      5
#define FMT_UNKNOWN  6

/** @brief Per-stream context shared by the session event handlers. */
typedef struct {
    AppState     *state;
    NtripSession *sess;
    int  detected_format;
    bool decode_active;      /* true once a supported format is confirmed */
    bool unsupported_logged;
    bool first_data_check;   /* true until the first data byte is checked */
    long frames;             /* accepted frames, for the replay summary */
} ObsCtx;

/**
 * @brief Phase 1 -- format detection over raw payload bytes.
 *
 * Ported verbatim from the old recv loop.  Identifies the stream format
 * from sync/header bytes; RTCM 3.x is confirmed later by a successful
 * frame decode.  Distinctive 2-byte sync patterns (scanned anywhere):
 * Septentrio SBF 0x24 0x40, UBX 0xB5 0x62.  Weaker single-byte patterns
 * (RT27 0x10, LB2 0x01) are checked only at the very first data byte
 * after the HTTP header, to avoid false positives inside RTCM payload.
 */
static void ObsSniffFormat(ObsCtx *c, const unsigned char *data, int n)
{
    AppState *state = c->state;

    if (c->detected_format == FMT_NONE && n > 0) {
        /* RTCM 3.x first: scan for the 0xD3 preamble with a plausible
         * 10-bit length.  A much stronger signal than the 2-byte syncs,
         * pre-empting `$@` or 0xB5 0x62 inside an RTCM payload. */
        int rtcm_pos = -1;
        for (int j = 0; j + 2 < n; j++) {
            if (data[j] == 0xD3 && (data[j + 1] & 0xFC) == 0x00) {
                int rtcm_len = ((data[j + 1] & 0x03) << 8) | data[j + 2];
                if (rtcm_len >= 2 && rtcm_len <= 1023) {
                    rtcm_pos = j;
                    break;
                }
            }
        }
        if (rtcm_pos >= 0) {
            c->detected_format = FMT_RTCM3;
            /* The session's framer confirms via CRC. */
        }
        if (c->detected_format == FMT_NONE) {
            for (int j = 0; j < n - 1; j++) {
                if (data[j] == 0x24 && data[j + 1] == 0x40) {
                    c->detected_format = FMT_SBF;
                    break;
                }
                if (data[j] == 0xB5 && data[j + 1] == 0x62) {
                    c->detected_format = FMT_UBX;
                    break;
                }
            }
        }

        if (c->detected_format == FMT_NONE && c->first_data_check) {
            unsigned char b0 = data[0];
            if (b0 == 0x10 && n >= 4 &&
                data[1] != 0x10 && data[1] != 0x03) {
                c->detected_format = FMT_RT27;
            } else if (b0 == 0x01 && n >= 3 &&
                       data[1] <= 0x80 && data[1] > 0 && data[2] < 0x40) {
                c->detected_format = FMT_LB2;
            }
            c->first_data_check = false;
        }

        if (c->detected_format != FMT_NONE) {
            InterlockedExchange(&state->streamFormat, c->detected_format);
            PostMessage(state->hMain, WM_APP_STREAM_INFO, 0, 0);

            switch (c->detected_format) {
            case FMT_RTCM3:
                c->decode_active = true;
                printf("[INFO] RTCM 3.x stream detected -- decoding active\n");
                fflush(stdout);
                break;
            default:
                /* Future decoders: add case FMT_xxx: decode_active = true; */
                c->decode_active = false;
                break;
            }
        }
    }

    if (c->detected_format != FMT_NONE && !c->decode_active &&
        !c->unsupported_logged) {
        const char *name = "Unknown";
        switch (c->detected_format) {
        case FMT_UBX:  name = "UBX (u-blox)";   break;
        case FMT_SBF:  name = "Septentrio SBF"; break;
        case FMT_RT27: name = "Trimble RT27";   break;
        case FMT_LB2:  name = "Leica LB2";      break;
        }
        printf("[INFO] %s stream detected -- decoding not yet supported\n", name);
        fflush(stdout);
        c->unsupported_logged = true;

        /* Keep receiving so the byte counter and rate stay live, but do
         * not run RTCM framing over non-RTCM bytes: arbitrary binary
         * contains 0xD3 often enough to inflate the CRC-error counters
         * of a stream that is not malfunctioning. */
        ns_set_framing_enabled(c->sess, false);
    }
}

/**
 * @brief One CRC-valid RTCM frame: everything the GUI shows per frame.
 *
 * Ported verbatim from the old worker: capture dump, per-type epoch
 * statistics + WM_APP_STAT_UPDATE, satellite extraction, sky updates,
 * and the raw frame for the detail windows.
 */
static void ObsProcessFrame(ObsCtx *c, const unsigned char *frame,
                            int frame_len, int msg_type)
{
    AppState *state = c->state;

    if (msg_type <= 0 || msg_type >= GUI_MAX_MSG_TYPES) return;

    /* Decode for the log, exactly as before.  The session has already
     * CRC-validated the frame; this call is for the decoders' printf
     * output, which the log tab shows. */
    analyze_rtcm_message(frame, frame_len, true, &state->config);

    /* The capture used to be written here, frame by frame.  The session
     * layer writes it now -- see CaptureService in the pump loop. */

    /* Confirm RTCM 3.x on the first successful frame. */
    if (c->detected_format == FMT_NONE) {
        c->detected_format = FMT_RTCM3;
        c->decode_active   = true;
        InterlockedExchange(&state->streamFormat, FMT_RTCM3);
        PostMessage(state->hMain, WM_APP_STREAM_INFO, 0, 0);
        printf("[INFO] RTCM 3.x stream confirmed -- decoding active\n");
        fflush(stdout);
    }

    /* Per-type statistics, sampled per EPOCH, not per frame: an MSM type
     * split across frames would otherwise read as sending at a multiple
     * of its true rate. */
    double now = gui_get_time_seconds();
    GuiMsgStat *s = &state->msgStats[msg_type];

    int msg_length = ((frame[1] & 0x03) << 8) | frame[2];

    uint32_t epoch = 0;
    bool has_epoch = msm_get_epoch(frame + 3, msg_length,
                                   msg_type, &epoch) != 0;
    bool new_epoch = !has_epoch
                     || !s->has_epoch
                     || epoch != s->last_epoch;

    if (has_epoch) {
        s->last_epoch = epoch;
        s->has_epoch  = true;
    }

    if (new_epoch) {
        if (!s->seen) {
            s->seen = true;
            s->last_time = now;
            s->min_dt = s->max_dt = s->sum_dt = 0.0;
        } else {
            double dt = now - s->last_time;
            s->last_time = now;
            s->sum_dt += dt;
            if (dt < s->min_dt || s->min_dt == 0.0) s->min_dt = dt;
            if (dt > s->max_dt) s->max_dt = dt;
        }
        s->epochs++;
    }
    s->count++;

    PostMessage(state->hMain, WM_APP_STAT_UPDATE,
                (WPARAM)msg_type, (LPARAM)s->count);

    /* Satellite info from MSM messages. */
    extract_satellites(frame + 3, msg_length, msg_type, &state->satStats);

    /* Satellites in view and their C/N0, from the session's own tracker.
     * stats_refresh() runs on every ns_pump(), so this is at most one
     * pump old.  Taken here rather than recomputed in the GUI so that
     * the Satellites tab, the daemon's Munin graphs and Android all
     * report the same numbers. */
    {
        const NsStatsSnapshot *snap = ns_stats(c->sess);
        if (snap) {
            memcpy(state->gnssStats, snap->gnss, sizeof(state->gnssStats));
            state->nGnssStats = snap->n_gnss;
        }
        state->nIonoView = ns_iono_view(c->sess, state->ionoView,
                                        (int)(sizeof(state->ionoView) /
                                              sizeof(state->ionoView[0])));
    }
    PostMessage(state->hMain, WM_APP_SAT_UPDATE, 0, 0);

    /* ── Sky-plot update for MSM frames ─────────────────────────
     * Needs a station ARP and a valid ephemeris per SV.  Falls back to
     * the configured rover lat/lon when no 1005/1006 has arrived; there
     * is no ephemeris fallback, but an empty update is still posted so
     * the sky window's status line refreshes. */
    {
        bool   arp_valid = false;
        double sx = 0, sy = 0, sz = 0;
        rtcm_get_station_arp(&arp_valid, &sx, &sy, &sz, NULL, NULL, NULL);

        if (!arp_valid &&
            (state->config.LATITUDE != 0.0 ||
             state->config.LONGITUDE != 0.0)) {
            geodetic_to_ecef(state->config.LATITUDE,
                             state->config.LONGITUDE,
                             0.0, &sx, &sy, &sz);
            arp_valid = true;
        }

        int prns[64];
        float cnr_prns[64];
        int gnss_id = 0;
        int n_prns = arp_valid
            ? msm_extract_prns(frame + 3, msg_length, msg_type, prns,
                               (int)(sizeof(prns) / sizeof(prns[0])),
                               &gnss_id)
            : 0;

        int   cnr_n = 0;
        int   cnr_prn_list[64];
        if (n_prns > 0) {
            cnr_n = msm_extract_cnr(frame + 3, msg_length, msg_type,
                                    cnr_prn_list, cnr_prns, 64, NULL);
            msm_update_per_band_cnr(frame + 3, msg_length, msg_type);
        }
        if (cnr_n == 0) {
            for (int i = 0; i < 64; i++) cnr_prns[i] = 0.0f;
        }

        float cnr_by_prn[SV_EPH_MAX_SATS_PER_GNSS + 1];
        for (int i = 0; i <= SV_EPH_MAX_SATS_PER_GNSS; i++)
            cnr_by_prn[i] = 0.0f;
        for (int i = 0; i < cnr_n; i++) {
            int p = cnr_prn_list[i];
            if (p >= 1 && p <= SV_EPH_MAX_SATS_PER_GNSS)
                cnr_by_prn[p] = cnr_prns[i];
        }

        int upd_count = 0;
        SkySatUpdate *upd = NULL;

        if (n_prns > 0 &&
            (gnss_id == 1 || gnss_id == 2 || gnss_id == 3 ||
             gnss_id == 4 || gnss_id == 5 || gnss_id == 7)) {
            int    gps_week;
            double gps_tow;
            sky_get_gps_time(&gps_week, &gps_tow);
            double glo_tod = sky_get_glo_tod();
            double t_prop  = (gnss_id == 2) ? glo_tod : gps_tow;

            uint64_t obs_mask = 0;
            for (int i = 0; i < n_prns; i++) {
                int p = prns[i];
                if (p >= 1 && p <= 64) obs_mask |= 1ULL << (p - 1);
            }

            upd = (SkySatUpdate *)HeapAlloc(
                GetProcessHeap(), 0,
                sizeof(SkySatUpdate) * (size_t)SV_EPH_MAX_SATS_PER_GNSS);

            if (upd) {
                for (int p = 1; p <= SV_EPH_MAX_SATS_PER_GNSS; p++) {
                    const SvEphemeris *eph = sv_eph_get(gnss_id, p);
                    if (!eph) continue;
                    if (!sv_eph_is_valid_at(eph, gps_week, t_prop)) continue;

                    double svx, svy, svz;
                    if (!sv_to_ecef(eph, gps_week, t_prop, &svx, &svy, &svz))
                        continue;

                    double az_d, el_d;
                    azel_from_ecef(sx, sy, sz, svx, svy, svz, &az_d, &el_d);
                    if (el_d <= 0.0) continue;

                    int observed_flag = (p >= 1 && p <= 64)
                        ? ((obs_mask >> (p - 1)) & 1ULL) ? 1 : 0
                        : 0;

                    float cnr_dbhz = 0.0f;
                    if (observed_flag &&
                        p >= 1 && p <= SV_EPH_MAX_SATS_PER_GNSS)
                        cnr_dbhz = cnr_by_prn[p];

                    upd[upd_count].gnss_id       = gnss_id;
                    upd[upd_count].prn           = p;
                    upd[upd_count].az_deg        = (float)az_d;
                    upd[upd_count].el_deg        = (float)el_d;
                    upd[upd_count].cnr_dbhz      = cnr_dbhz;
                    /* ROTI from the per-SV view refreshed above; linear
                     * scan, but nIonoView is at most a few dozen. */
                    upd[upd_count].roti = -1.0f;
                    for (int vi = 0; vi < state->nIonoView; vi++) {
                        if (state->ionoView[vi].gnss_id == gnss_id &&
                            state->ionoView[vi].prn == p) {
                            upd[upd_count].roti = state->ionoView[vi].roti;
                            break;
                        }
                    }
                    upd[upd_count].observed_flag = observed_flag;
                    upd_count++;
                }
            }
        }

        if (!PostMessage(state->hMain, WM_APP_SKY_UPDATE,
                         (WPARAM)upd_count, (LPARAM)upd)) {
            if (upd) HeapFree(GetProcessHeap(), 0, upd);
        }
    }

    /* Raw frame to the UI thread for detail windows. */
    RtcmRawMsg *raw = (RtcmRawMsg *)HeapAlloc(
        GetProcessHeap(), 0, sizeof(RtcmRawMsg));
    if (raw) {
        raw->msg_type = msg_type;
        raw->length   = frame_len;
        memcpy(raw->data, frame, frame_len);
        if (!PostMessage(state->hMain, WM_APP_MSG_RAW,
                         (WPARAM)msg_type, (LPARAM)raw)) {
            HeapFree(GetProcessHeap(), 0, raw);
        }
    }

    c->frames++;
}

/** @brief Session events → the GUI's counters, log lines and messages. */
static void ObsOnEvent(const NsEvent *ev, void *user)
{
    ObsCtx   *c     = (ObsCtx *)user;
    AppState *state = c->state;

    switch (ev->type) {
    case NS_EV_HANDSHAKE: {
        /* Keep the GUI's copy for the Stream Health tab. */
        state->handshake = *ev->u.handshake;

        printf("[INFO] Connected to %s:%d/%s\n",
               state->config.NTRIP_CASTER, state->config.NTRIP_PORT,
               state->config.MOUNTPOINT);
        printf("[INFO] Caster handshake (NTRIP %s):\n",
               state->handshake.version == NS_PROTO_V1 ? "1.0" : "2.0");
        for (const char *hp = state->handshake.raw; *hp; ) {
            const char *eol = hp;
            while (*eol && *eol != '\r' && *eol != '\n') eol++;
            if (eol > hp) printf("  %.*s\n", (int)(eol - hp), hp);
            while (*eol == '\r' || *eol == '\n') eol++;
            hp = eol;
        }
        /* The version mismatch is not remarked on here: the session says
         * it already, from the code that read the answer, and two
         * wordings of one fact is one wording too many. */
        if (state->handshake.status == 200)
            printf("[INFO] Stream started\n");
        fflush(stdout);

        /* Initial GGA, now that the caster has accepted us -- unless the
         * user disabled auto-send via the Tools menu (used to verify
         * GGA-gated VRS behaviour). */
        if (state->handshake.status == 200 && state->ggaSendEnabled) {
            double lat = state->ggaOverrideValid ? state->ggaOverrideLat
                                                 : state->config.LATITUDE;
            double lon = state->ggaOverrideValid ? state->ggaOverrideLon
                                                 : state->config.LONGITUDE;
            state->ggaCurrentLat = lat;
            state->ggaCurrentLon = lon;
            if (ns_send_gga(c->sess, lat, lon)) {
                CheckNoteGga(state, lat, lon);
                printf("[GGA] Sent initial GGA\n");
                fflush(stdout);
                InterlockedIncrement(&state->ggaSendCount);
                InterlockedExchange(&state->ggaLastSendUnix,
                                    (LONG)time(NULL));
            }
        }
        break;
    }

    case NS_EV_RAW:
        /* All payload bytes count toward the rate display, whatever the
         * format turns out to be. */
        if (ev->u.raw.len > 0)
            InterlockedExchangeAdd(&state->streamBytes, ev->u.raw.len);
        ObsSniffFormat(c, ev->u.raw.data, ev->u.raw.len);
        break;

    case NS_EV_FRAME:
        InterlockedIncrement(&state->healthFramesOk);
        ObsProcessFrame(c, ev->u.frame.data, ev->u.frame.len,
                        ev->u.frame.msg_type);
        break;

    case NS_EV_FRAME_BAD:
        switch (ev->u.bad.reason) {
        case NS_BAD_CRC:       InterlockedIncrement(&state->healthCrcErrors); break;
        case NS_BAD_LENGTH:    InterlockedIncrement(&state->healthResyncs);   break;
        }
        break;

    case NS_EV_STATS:
        /* Kept whole so File > Export Statistics writes the same record
         * the daemon publishes, through the same serialisers. */
        if (ev->u.stats) {
            state->lastStats = *ev->u.stats;
            state->haveStats = TRUE;
            /* A station check in progress advances here rather than on a
             * window timer: the data is what justifies a verdict, so the
             * KPI clock steps once per snapshot -- never twice, never
             * skipping one. */
            CheckOnStats(state, ev->u.stats);
            /* Tier 2 advances from the same event and for the same
             * reason. It paces itself on the stream clock inside the
             * snapshot, so a replay running at disk speed accumulates
             * the window the capture holds rather than the seconds the
             * disk took. */
            ReportOnStats(state, ev->u.stats);
        }
        break;

    case NS_EV_LOG:
        printf("[%s] %s\n",
               ev->u.log.level == NS_LOG_ERROR ? "ERROR" :
               ev->u.log.level == NS_LOG_WARN  ? "WARN"  : "INFO",
               ev->u.log.text);
        fflush(stdout);
        break;

    default:
        break;
    }
}

/**
 * @brief Carry out whatever the capture menu asked for, and report back.
 *
 * The session writes the capture file, and the session belongs to this
 * thread -- so the menu leaves a request behind and the pump loop acts
 * on it between pumps, exactly as the GGA uplink is driven.  Doing it
 * from the UI thread instead would mean two threads inside one session
 * while frames are being written, which no lock in this program covers.
 *
 * Called once per pump, so it must stay cheap: one critical section and,
 * when nothing was asked, one status read.
 */
static void CaptureService(AppState *state, NtripSession *sess)
{
    int  req = 0;
    char path[MAX_PATH] = "";

    if (!state->csRtcmDumpInit) return;

    EnterCriticalSection(&state->csRtcmDump);
    req = state->captureReq;
    state->captureReq = 0;
    if (req == 1) {
        strncpy(path, state->captureReqPath, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    }
    LeaveCriticalSection(&state->csRtcmDump);

    if (req == 1) {
        /* A refusal -- the path exists, or is unwritable -- has already
         * been logged by the session as an NS_EV_LOG error, so all that
         * is left here is not to claim a capture that did not start. */
        if (ns_capture_start(sess, path) == 0) {
            EnterCriticalSection(&state->csRtcmDump);
            state->captureActive = TRUE;
            strncpy(state->capturePath, path,
                    sizeof(state->capturePath) - 1);
            state->capturePath[sizeof(state->capturePath) - 1] = '\0';
            state->captureBytes = 0;
            LeaveCriticalSection(&state->csRtcmDump);
            /* Nothing logged here: the session has already said
             * "Capturing frames to <path>", from the code that opened
             * the file.  Saying it twice invites the two to disagree. */
        } else {
            EnterCriticalSection(&state->csRtcmDump);
            state->captureActive = FALSE;
            LeaveCriticalSection(&state->csRtcmDump);
        }
        return;
    }

    if (req == 2) {
        ns_capture_stop(sess);   /* logs the totals itself, as it closes */
        uint64_t bytes = 0;
        ns_capture_status(sess, &bytes, NULL);
        EnterCriticalSection(&state->csRtcmDump);
        state->captureActive = FALSE;
        state->captureBytes  = (unsigned long)bytes;
        LeaveCriticalSection(&state->csRtcmDump);
        return;
    }

    /* No request: publish the running total, which is all the UI reads. */
    if (state->captureActive) {
        uint64_t bytes = 0;
        ns_capture_status(sess, &bytes, NULL);
        EnterCriticalSection(&state->csRtcmDump);
        state->captureBytes = (unsigned long)bytes;
        LeaveCriticalSection(&state->csRtcmDump);
    }
}

/**
 * @brief Close the books on a capture the stream ended under.
 *
 * @ref ns_close flushes the file on its own; what it cannot do is tell
 * the menu, which would otherwise still offer Stop Capture for a
 * capture that is over.
 */
static void CaptureFinish(AppState *state, NtripSession *sess)
{
    if (!state->csRtcmDumpInit) return;

    BOOL was_active;
    EnterCriticalSection(&state->csRtcmDump);
    was_active = state->captureActive;
    state->captureActive = FALSE;
    LeaveCriticalSection(&state->csRtcmDump);
    if (!was_active) return;

    /* The one thing the session cannot say, because it does not know it:
     * that nobody asked for this stop.  The totals follow from
     * @ref ns_capture_stop, so they are not repeated here. */
    printf("[INFO] Stream ended -- closing the RTCM capture\n");
    fflush(stdout);

    ns_capture_stop(sess);
    uint64_t bytes = 0;
    ns_capture_status(sess, &bytes, NULL);
    EnterCriticalSection(&state->csRtcmDump);
    state->captureBytes = (unsigned long)bytes;
    LeaveCriticalSection(&state->csRtcmDump);
}

DWORD WINAPI WorkerOpenStream(LPVOID param)
{
    AppState *state = (AppState *)param;

    /* ── Fetch the sourcetable if we do not have this mountpoint ──────
     * The Format/Details fields are only populated when the user fetched
     * the mountpoint list first.  Connecting to a typed-in mountpoint
     * would otherwise leave nothing to compare observed message types
     * against, so pull the sourcetable here -- on the worker, before the
     * stream connect, so the UI never blocks.  A caster that refuses is
     * not an error: advValid simply stays FALSE and the Status column
     * reads "unknown". */
    if (state->sourceDetails[0] == '\0') {
        printf("[INFO] No sourcetable entry for this mountpoint; fetching...\n");
        fflush(stdout);

        char *table = receive_mount_table(&state->config,
                          NTRIP_USER_AGENT(NTRIP_ARTEFACT_GUI));
        if (table) {
            if (SourcetableFindMountpoint(table, state->config.MOUNTPOINT,
                                          state->sourceFormat,
                                          sizeof(state->sourceFormat),
                                          state->sourceDetails,
                                          sizeof(state->sourceDetails),
                                          state->sourceNav,
                                          sizeof(state->sourceNav))) {
                state->advCount = ParseAdvertisedTypes(state->sourceDetails,
                                                       state->advInterval);
                state->advValid = (state->advCount > 0);
                state->advAutoFetched = TRUE;
                printf("[INFO] Sourcetable: mountpoint advertises %d type(s): %s\n",
                       state->advCount, state->sourceDetails);
            } else {
                printf("[WARN] Mountpoint not listed in the caster's sourcetable; "
                       "no advertised-vs-observed comparison available.\n");
            }
            /* Hand the whole table to the UI to fill the mountpoint list
             * rather than throwing it away.  The UI thread frees it. */
            PostMessage(state->hMain, WM_APP_SOURCETABLE, 0, (LPARAM)table);
        } else {
            printf("[WARN] Could not fetch sourcetable; no advertised-vs-observed "
                   "comparison available.\n");
        }
        fflush(stdout);
    }

    ObsCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.state            = state;
    ctx.detected_format  = FMT_NONE;
    ctx.first_data_check = true;

    /* ── Pre-seed format from the sourcetable ─────────────────────────
     * RAW streams (RT27, LB2) are wrapped inside RTCM 3.x framing, so
     * byte-level detection alone would mis-identify them as RTCM. */
    {
        const char *fmt = state->sourceFormat;
        const char *det = state->sourceDetails;

        printf("[INFO] Sourcetable -- Format: \"%s\", Details: \"%s\"\n", fmt, det);
        fflush(stdout);

        #define CONTAINS_CI(haystack, needle) (stristr((haystack), (needle)) != NULL)

        if (CONTAINS_CI(fmt, "RT27") || CONTAINS_CI(det, "RT27")) {
            ctx.detected_format = FMT_RT27;
            ctx.decode_active = true;
            InterlockedExchange(&state->streamFormat, FMT_RT27);
            PostMessage(state->hMain, WM_APP_STREAM_INFO, 0, 0);
            printf("[INFO] RAW Trimble RT27 stream (RTCM framing) -- decoding active\n");
            fflush(stdout);
        } else if (CONTAINS_CI(fmt, "LB2") || CONTAINS_CI(det, "LB2")) {
            ctx.detected_format = FMT_LB2;
            ctx.decode_active = true;
            InterlockedExchange(&state->streamFormat, FMT_LB2);
            PostMessage(state->hMain, WM_APP_STREAM_INFO, 0, 0);
            printf("[INFO] RAW Leica LB2 stream (RTCM framing) -- decoding active\n");
            fflush(stdout);
        } else if (CONTAINS_CI(fmt, "SBF") || CONTAINS_CI(det, "SBF") ||
                   CONTAINS_CI(fmt, "Septentrio")) {
            ctx.detected_format = FMT_SBF;
            InterlockedExchange(&state->streamFormat, FMT_SBF);
            PostMessage(state->hMain, WM_APP_STREAM_INFO, 0, 0);
            printf("[INFO] Septentrio SBF stream detected\n");
            fflush(stdout);
        } else if (CONTAINS_CI(fmt, "UBX") || CONTAINS_CI(det, "UBX") ||
                   CONTAINS_CI(fmt, "BINEX")) {
            ctx.detected_format = FMT_UBX;
            InterlockedExchange(&state->streamFormat, FMT_UBX);
            PostMessage(state->hMain, WM_APP_STREAM_INFO, 0, 0);
            printf("[INFO] UBX stream detected\n");
            fflush(stdout);
        }
        /* else: RTCM or unknown — byte-level + frame decode identify it */

        #undef CONTAINS_CI
    }

    /* ── Open the session ───────────────────────────────────────────── */
    InterlockedExchange(&state->streamBytes, 0);
    if (ctx.detected_format == FMT_NONE)
        InterlockedExchange(&state->streamFormat, 0);

    NsOptions opt;
    ns_options_default(&opt);
    opt.config           = state->config;
    /* The GUI keeps its own per-message statistics, but takes the
     * session's snapshot once a second so Export Statistics has
     * something to write. */
    opt.stats_interval_s = 1.0;
    opt.send_gga         = false;  /* GGA is driven interactively below */
    opt.auto_reconnect   = state->autoReconnect ? true : false;
    opt.user_agent       = NTRIP_USER_AGENT(NTRIP_ARTEFACT_GUI);

    NtripSession *sess = ns_open(&opt, ObsOnEvent, &ctx);
    if (!sess) {
        printf("[ERROR] Out of memory opening the stream session\n");
        fflush(stdout);
        PostMessage(state->hMain, WM_APP_STREAM_DONE, 0, 0);
        return 1;
    }
    ctx.sess = sess;

    /* A new stream is a new window of evidence: carrying the last
     * station's worst CRC rate into this one would be a verdict about
     * neither of them. */
    ReportReset(state, FALSE);

    /* ── Hand the session what the mountpoint advertises ──────────────
     * KPI 8 compares promise against delivery, and it reads the promise
     * from the session -- as the CLI and the Android bridge already do.
     * Without this the station check would evaluate against an empty
     * advertisement and pass everything.
     *
     * Constellations come from the STR nav-system field, not from the
     * 1005/1006 indicator bits: those cover GPS, GLONASS and Galileo
     * only, so a BeiDou-capable base cannot declare it there. */
    if (state->sourceDetails[0]) {
        SourcetableType adv[64];
        int nt = sourcetable_parse_types(state->sourceDetails, adv,
                                         (int)(sizeof(adv) / sizeof(adv[0])));
        if (nt > 0) ns_set_advertised(sess, adv, nt);
    }
    if (state->sourceNav[0])
        ns_set_advertised_gnss(sess, sourcetable_navsys_mask(state->sourceNav));

    /* Detected-but-undecoded formats bypass the RTCM framer entirely. */
    if (ctx.detected_format != FMT_NONE && !ctx.decode_active)
        ns_set_framing_enabled(sess, false);

    /* ── Pump until stopped ─────────────────────────────────────────── */
    time_t last_gga_time = time(NULL);

    while (!state->bStopRequested) {
        /* Periodic GGA resend (every 5 seconds).  Re-reads the override
         * and pause state each tick so the user can toggle auto-send or
         * push a position-shift test mid-session. */
        time_t now_t = time(NULL);
        if (state->ggaSendEnabled && (now_t - last_gga_time >= 5)) {
            double cur_lat = state->ggaOverrideValid ? state->ggaOverrideLat
                                                     : state->config.LATITUDE;
            double cur_lon = state->ggaOverrideValid ? state->ggaOverrideLon
                                                     : state->config.LONGITUDE;
            if (cur_lat != state->ggaCurrentLat ||
                cur_lon != state->ggaCurrentLon) {
                state->ggaCurrentLat = cur_lat;
                state->ggaCurrentLon = cur_lon;
                printf("[GGA] Position changed -> %.6f, %.6f\n",
                       cur_lat, cur_lon);
                fflush(stdout);
            }
            if (ns_send_gga(sess, cur_lat, cur_lon)) {
                CheckNoteGga(state, cur_lat, cur_lon);
                /* Not logged: an uplink every few seconds says the same
                 * thing forever and buries the events that differ.  The
                 * count and the time of the last one are on the Stream
                 * Health tab, and a change of position still logs. */
                InterlockedIncrement(&state->ggaSendCount);
                InterlockedExchange(&state->ggaLastSendUnix,
                                    (LONG)time(NULL));
            }
            last_gga_time = now_t;
        }

        CaptureService(state, sess);

        if (ns_pump(sess, 200) < 0)
            break;
    }

    /* ns_close flushes an open capture, but the menu's own state has to
     * be told, or "Stop Capture" would still offer itself afterwards. */
    CaptureFinish(state, sess);

    ns_close(sess);

    printf("\n[INFO] Stream worker finished\n");
    fflush(stdout);

    PostMessage(state->hMain, WM_APP_STREAM_DONE, 0, 0);
    return 0;
}

/**
 * @brief Post a log line to the UI thread, bypassing the stdout pipe.
 *
 * The stdout->pipe->LogPumpTimer chain depends on WM_TIMER (low priority).
 * Worker threads can post lines directly into the UI's message queue so
 * they show up reliably even when the queue is busy with WM_APP_* updates.
 */
static void eph_log(AppState *state, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;

    size_t copy_sz = (size_t)n + 1;
    char *dup = (char *)HeapAlloc(GetProcessHeap(), 0, copy_sz);
    if (!dup) return;
    memcpy(dup, buf, copy_sz);

    if (!PostMessage(state->hMain, WM_APP_LOG_LINE, 0, (LPARAM)dup)) {
        HeapFree(GetProcessHeap(), 0, dup);
    }
}

/* ── Ephemeris-only stream worker (session-layer adapter) ────────────────
 *
 * Runs in parallel with WorkerOpenStream when EPH_MOUNTPOINT is configured.
 * Connects to a separate caster (typically a public ephemeris service such
 * as BKG BCEP00BKG0 or Kadaster BCEP00KAD0) and feeds only the ephemeris
 * cache by calling the decode_rtcm_10xx ephemeris decoders directly.
 *
 * Deliberately does NOT:
 *   - send GGA (eph streams don't need a rover position),
 *   - process 1005/1006 (would overwrite the obs caster's ARP),
 *   - update msgStats / satStats / Msg Stats ListView,
 *   - post WM_APP_MSG_RAW (detail-window machinery is for the obs stream),
 *   - touch the byte-rate / status-bar counters.
 *
 * Lifetime is controlled via state->bStopRequestedEph.
 */

/** @brief Context for the ephemeris session's event handler. */
typedef struct {
    AppState *state;
    int       eph_count;   /* ephemerides cached so far */
} EphCtx;

/** @brief Ephemeris session events: cache eph frames, log via eph_log. */
static void EphOnEvent(const NsEvent *ev, void *user)
{
    EphCtx   *c     = (EphCtx *)user;
    AppState *state = c->state;

    switch (ev->type) {
    case NS_EV_HANDSHAKE:
        if (ev->u.handshake->status == 200) {
            eph_log(state,
                    "[EPH] Stream accepted by %s/%s -- decoding ephemerides\r\n",
                    state->config.EPH_CASTER,
                    state->config.EPH_MOUNTPOINT);
        }
        /* Rejection detail arrives via NS_EV_LOG; the session then ends. */
        break;

    case NS_EV_FRAME: {
        const unsigned char *frame = ev->u.frame.data;
        int payload_len = ev->u.frame.len - 6;

        /* Everything else is silently dropped -- including 1005/1006,
         * which must not overwrite the obs caster's ARP. */
        c->eph_count += rtcm_decode_eph(frame + 3, payload_len,
                                        ev->u.frame.msg_type);
        break;
    }

    case NS_EV_LOG:
        eph_log(state, "[EPH] %s\r\n", ev->u.log.text);
        break;

    default:
        break;
    }
}

DWORD WINAPI WorkerOpenEphStream(LPVOID param)
{
    AppState *state = (AppState *)param;

    /* Beacon: first thing every worker invocation does, even before the
     * gate check.  Posted via WM_APP_LOG_LINE (not printf) because the
     * stdout pipe pump can be starved by high message-queue traffic. */
    eph_log(state,
            "[EPH] Worker entered: caster=\"%s\" port=%d mp=\"%s\"\r\n",
            state->config.EPH_CASTER,
            state->config.EPH_PORT,
            state->config.EPH_MOUNTPOINT);

    if (state->config.EPH_MOUNTPOINT[0] == '\0' ||
        state->config.EPH_CASTER[0]     == '\0') {
        eph_log(state, "[EPH] Disabled (no caster/mountpoint configured)\r\n");
        return 0;
    }

    /* The session reads the primary connection fields, so map the EPH_*
     * block onto a private copy of the config. */
    EphCtx ctx;
    ctx.state     = state;
    ctx.eph_count = 0;

    NsOptions opt;
    ns_options_default(&opt);
    opt.config = state->config;
    strncpy(opt.config.NTRIP_CASTER, state->config.EPH_CASTER,
            sizeof(opt.config.NTRIP_CASTER) - 1);
    opt.config.NTRIP_PORT = state->config.EPH_PORT;
    opt.config.TLS        = state->config.EPH_TLS;  /* its own caster */
    strncpy(opt.config.MOUNTPOINT, state->config.EPH_MOUNTPOINT,
            sizeof(opt.config.MOUNTPOINT) - 1);
    strncpy(opt.config.USERNAME, state->config.EPH_USERNAME,
            sizeof(opt.config.USERNAME) - 1);
    strncpy(opt.config.PASSWORD, state->config.EPH_PASSWORD,
            sizeof(opt.config.PASSWORD) - 1);
    opt.stats_interval_s = 0.0;
    opt.send_gga         = false;
    opt.auto_reconnect   = false;
    opt.user_agent       = NTRIP_USER_AGENT(NTRIP_ARTEFACT_GUI);

    NtripSession *sess = ns_open(&opt, EphOnEvent, &ctx);
    if (!sess) {
        eph_log(state, "[EPH] Out of memory opening the eph session\r\n");
        return 1;
    }

    eph_log(state, "[EPH] Connecting to %s:%d/%s...\r\n",
            state->config.EPH_CASTER, state->config.EPH_PORT,
            state->config.EPH_MOUNTPOINT);

    while (!state->bStopRequestedEph) {
        if (ns_pump(sess, 200) < 0)
            break;
    }

    ns_close(sess);

    eph_log(state, "[EPH] Stream worker finished (%d ephemerides processed)\r\n",
            ctx.eph_count);

    return 0;
}

/* ── RTCM replay worker (session-layer adapter) ──────────────────────────
 *
 * Replays a .rtcm3 capture through ns_open_file(), which feeds the frames
 * through the identical framing, validation and event path as a live
 * stream -- and through the same ObsOnEvent handler as the live worker,
 * so replay and live analysis cannot diverge.  Frames arrive as fast as
 * disk and CPU allow; there is no pacing, because for analysis the point
 * is to get the whole picture into the UI immediately.
 *
 * Lifetime is shared with WorkerOpenStream via hWorkerThread /
 * bWorkerRunning / bStopRequested so Close Stream stops replay.  The eph
 * worker is not started during replay, and no capture is started for
 * you -- though one asked for from the menu is honoured.
 */
DWORD WINAPI WorkerReplayRtcm(LPVOID param)
{
    AppState *state = (AppState *)param;

    printf("[INFO] Replay: opening %s\n", state->replayPath);
    fflush(stdout);

    ObsCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.state           = state;
    ctx.detected_format = FMT_RTCM3;   /* a capture is RTCM by definition */
    ctx.decode_active   = true;

    /* Status-bar label, even though no caster is involved. */
    InterlockedExchange(&state->streamFormat, FMT_RTCM3);
    PostMessage(state->hMain, WM_APP_STREAM_INFO, 0, 0);

    NsOptions opt;
    ns_options_default(&opt);
    opt.config           = state->config;
    /* Once a second *of stream*, not of wall clock: the session paces
     * this event on the observation clock, so a six-hour capture read in
     * a moment still yields six hours of samples. Off, the stability
     * window would see nothing at all from a replay. */
    opt.stats_interval_s = 1.0;

    NtripSession *sess = ns_open_file(state->replayPath, &opt,
                                      ObsOnEvent, &ctx);
    if (!sess) {
        printf("[ERROR] Replay: out of memory\n");
        fflush(stdout);
        PostMessage(state->hMain, WM_APP_STREAM_DONE, 0, 0);
        return 1;
    }
    ctx.sess = sess;

    /* From a capture: availability is reported unavailable rather than
     * as a clean zero, because a file holds no arrival times. */
    ReportReset(state, TRUE);

    while (!state->bStopRequested) {
        /* Capture during a replay is not started for you, but it is
         * allowed: the menu is live while a replay runs, and re-writing
         * a capture through the framer is the identity function this
         * program's first test is built on.  Serviced here so the menu
         * does not silently do nothing. */
        CaptureService(state, sess);

        if (ns_pump(sess, 0) < 0)
            break;
    }

    CaptureFinish(state, sess);

    unsigned long long total = ns_stats(sess)->bytes_total;
    ns_close(sess);

    printf("\n[INFO] Replay finished: %ld frames, %llu bytes from %s\n",
           ctx.frames, total, state->replayPath);
    fflush(stdout);

    PostMessage(state->hMain, WM_APP_STREAM_DONE, 0, 0);
    return 0;
}
