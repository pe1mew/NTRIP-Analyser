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
#include "rtcm3x_parser.h"
#include "nmea_parser.h"
#include "sv_ephemeris.h"
#include "sv_orbit.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
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

/**
 * @brief Case-insensitive substring search (like strstr but ignores case).
 */
static const char *stristr(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !*needle) return NULL;
    size_t nlen = strlen(needle);
    for (; *haystack; haystack++) {
        if (_strnicmp(haystack, needle, nlen) == 0)
            return haystack;
    }
    return NULL;
}

/* ── Get Mountpoints worker ──────────────────────────────── */

DWORD WINAPI WorkerGetMountpoints(LPVOID param)
{
    AppState *state = (AppState *)param;

    char *mount_table = receive_mount_table(&state->config);

    /* Post result to UI thread: wParam=0 success, 1 error; lParam=heap string */
    PostMessage(state->hMain, WM_APP_MOUNT_RESULT,
                (WPARAM)(mount_table ? 0 : 1),
                (LPARAM)mount_table);

    return 0;
}

/* ── Open Stream worker (custom recv loop with real-time stats) ── */

DWORD WINAPI WorkerOpenStream(LPVOID param)
{
    AppState *state = (AppState *)param;

    /* ── DNS resolve ─────────────────────────────────────── */
    struct addrinfo hints, *result = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(state->config.NTRIP_CASTER, NULL, &hints, &result) != 0) {
        printf("[ERROR] DNS resolution failed for %s\n", state->config.NTRIP_CASTER);
        fflush(stdout);
        PostMessage(state->hMain, WM_APP_STREAM_DONE, 0, 0);
        return 1;
    }

    /* ── Create socket ───────────────────────────────────── */
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        printf("[ERROR] Failed to create socket\n");
        fflush(stdout);
        freeaddrinfo(result);
        PostMessage(state->hMain, WM_APP_STREAM_DONE, 0, 0);
        return 1;
    }

    /* ── Connect ─────────────────────────────────────────── */
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port   = htons(state->config.NTRIP_PORT);
    server.sin_addr   = ((struct sockaddr_in *)result->ai_addr)->sin_addr;
    memset(&server.sin_zero, 0, 8);
    freeaddrinfo(result);

    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR) {
        printf("[ERROR] Connection failed to %s:%d\n",
               state->config.NTRIP_CASTER, state->config.NTRIP_PORT);
        fflush(stdout);
        closesocket(sock);
        PostMessage(state->hMain, WM_APP_STREAM_DONE, 0, 0);
        return 1;
    }

    /* ── Set receive timeout for clean stop ──────────────── */
    DWORD timeout_ms = 200;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
               (const char *)&timeout_ms, sizeof(timeout_ms));

    /* ── Send NTRIP GET request ──────────────────────────── */
    char request[1024];
    snprintf(request, sizeof(request),
             "GET /%s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Ntrip-Version: Ntrip/2.0\r\n"
             "User-Agent: NTRIP CClient/1.0\r\n"
             "Authorization: Basic %s\r\n"
             "\r\n",
             state->config.MOUNTPOINT,
             state->config.NTRIP_CASTER,
             state->config.AUTH_BASIC);

    if (send(sock, request, (int)strlen(request), 0) <= 0) {
        printf("[ERROR] Failed to send NTRIP request\n");
        fflush(stdout);
        closesocket(sock);
        PostMessage(state->hMain, WM_APP_STREAM_DONE, 0, 0);
        return 1;
    }

    printf("[INFO] Connected to %s:%d/%s\n",
           state->config.NTRIP_CASTER, state->config.NTRIP_PORT,
           state->config.MOUNTPOINT);
    fflush(stdout);

    /* ── Prepare initial GGA sentence ───────────────────────────
     * The position used is taken from the AppState's ggaOverride*
     * fields if a VRS position-shift test is active, otherwise from
     * the configured rover lat/lon.  The chosen values are stamped
     * back into state->ggaCurrentLat/Lon so the UI's distance chip
     * uses the *actual* sent position, not a stale config one. */
    double gga_lat = state->ggaOverrideValid
                     ? state->ggaOverrideLat
                     : state->config.LATITUDE;
    double gga_lon = state->ggaOverrideValid
                     ? state->ggaOverrideLon
                     : state->config.LONGITUDE;
    state->ggaCurrentLat = gga_lat;
    state->ggaCurrentLon = gga_lon;

    char gga[100];
    create_gngga_sentence(gga_lat, gga_lon, gga);
    char gga_with_crlf[104];
    snprintf(gga_with_crlf, sizeof(gga_with_crlf), "%s\r\n", gga);
    time_t last_gga_time = time(NULL);

    /* Send initial GGA immediately so the caster knows our position
     * -- unless the user has explicitly disabled auto-send GGA via the
     * Tools menu (used to verify GGA-gated VRS behaviour). */
    if (state->ggaSendEnabled &&
        send(sock, gga_with_crlf, (int)strlen(gga_with_crlf), 0) > 0) {
        printf("[GGA] Sent initial GGA: %s\n", gga);
        fflush(stdout);
        InterlockedIncrement(&state->ggaSendCount);
        InterlockedExchange(&state->ggaLastSendUnix, (LONG)time(NULL));
    }

    /* ── Reset stream info counters ────────────────────────── */
    InterlockedExchange(&state->streamBytes, 0);
    InterlockedExchange(&state->streamFormat, 0);

    /* ── Stream format IDs (matches gui_state.h / gui_events.c) ── */
    #define FMT_NONE     0
    #define FMT_RTCM3    1
    #define FMT_UBX      2
    #define FMT_SBF      3
    #define FMT_RT27     4
    #define FMT_LB2      5
    #define FMT_UNKNOWN  6

    /* ── Receive loop ────────────────────────────────────────── */
    unsigned char recv_buf[GUI_BUFFER_SIZE];
    unsigned char msg_buf[GUI_BUFFER_SIZE];
    int msg_pos = 0;
    int msg_target = 0;   /* expected full frame length once known */
    bool header_done = false;
    char header_buf[4096];
    int header_pos = 0;
    int  detected_format = FMT_NONE;
    bool decode_active   = false;   /* true once a supported format is confirmed */
    bool unsupported_logged = false;
    bool first_data_check = true;   /* true until first data byte is checked */

    /* ── Pre-seed format from sourcetable if available ──────── */
    /* The sourcetable Format + Details columns identify the stream type.
     * RAW streams (RT27, LB2) are wrapped inside RTCM 3.x framing, so
     * byte-level sync detection alone would mis-identify them as RTCM.
     *
     * We search both Format and Details for known keywords using
     * case-insensitive substring matching to handle variations like
     * "RTCM 3.2", "RTCM3", "RT27", "Trimble RT27", etc. */
    {
        const char *fmt = state->sourceFormat;
        const char *det = state->sourceDetails;

        printf("[INFO] Sourcetable — Format: \"%s\", Details: \"%s\"\n", fmt, det);
        fflush(stdout);

        /* Helper: case-insensitive substring search */
        #define CONTAINS_CI(haystack, needle) (stristr((haystack), (needle)) != NULL)

        if (CONTAINS_CI(fmt, "RT27") || CONTAINS_CI(det, "RT27")) {
            detected_format = FMT_RT27;
            decode_active = true;
            InterlockedExchange(&state->streamFormat, FMT_RT27);
            PostMessage(state->hMain, WM_APP_STREAM_INFO, 0, 0);
            printf("[INFO] RAW Trimble RT27 stream (RTCM framing) — decoding active\n");
            fflush(stdout);
        } else if (CONTAINS_CI(fmt, "LB2") || CONTAINS_CI(det, "LB2")) {
            detected_format = FMT_LB2;
            decode_active = true;
            InterlockedExchange(&state->streamFormat, FMT_LB2);
            PostMessage(state->hMain, WM_APP_STREAM_INFO, 0, 0);
            printf("[INFO] RAW Leica LB2 stream (RTCM framing) — decoding active\n");
            fflush(stdout);
        } else if (CONTAINS_CI(fmt, "SBF") || CONTAINS_CI(det, "SBF") ||
                   CONTAINS_CI(fmt, "Septentrio")) {
            detected_format = FMT_SBF;
            InterlockedExchange(&state->streamFormat, FMT_SBF);
            PostMessage(state->hMain, WM_APP_STREAM_INFO, 0, 0);
            printf("[INFO] Septentrio SBF stream detected\n");
            fflush(stdout);
        } else if (CONTAINS_CI(fmt, "UBX") || CONTAINS_CI(det, "UBX") ||
                   CONTAINS_CI(fmt, "BINEX")) {
            detected_format = FMT_UBX;
            InterlockedExchange(&state->streamFormat, FMT_UBX);
            PostMessage(state->hMain, WM_APP_STREAM_INFO, 0, 0);
            printf("[INFO] UBX stream detected\n");
            fflush(stdout);
        }
        /* else: RTCM or unknown — let byte-level + frame decode identify */

        #undef CONTAINS_CI
    }

    while (!state->bStopRequested) {
        /* ── Periodic GGA resend (every 5 seconds) ─────────────
         * Re-reads the override / pause state each tick so the user
         * can toggle auto-send GGA or push a Position-shift test
         * mid-session and have it take effect on the next 5-s tick. */
        time_t now_t = time(NULL);
        if (state->ggaSendEnabled && (now_t - last_gga_time >= 5)) {
            double cur_lat = state->ggaOverrideValid
                             ? state->ggaOverrideLat
                             : state->config.LATITUDE;
            double cur_lon = state->ggaOverrideValid
                             ? state->ggaOverrideLon
                             : state->config.LONGITUDE;
            /* Rebuild the GGA string if the position has changed
             * (override toggled, shift-test fired, etc.). */
            if (cur_lat != state->ggaCurrentLat ||
                cur_lon != state->ggaCurrentLon) {
                state->ggaCurrentLat = cur_lat;
                state->ggaCurrentLon = cur_lon;
                create_gngga_sentence(cur_lat, cur_lon, gga);
                snprintf(gga_with_crlf, sizeof(gga_with_crlf),
                         "%s\r\n", gga);
                printf("[GGA] Position changed -> %s\n", gga);
                fflush(stdout);
            }
            if (send(sock, gga_with_crlf,
                     (int)strlen(gga_with_crlf), 0) > 0) {
                printf("[GGA] Sent GGA\n");
                fflush(stdout);
                InterlockedIncrement(&state->ggaSendCount);
                InterlockedExchange(&state->ggaLastSendUnix,
                                    (LONG)time(NULL));
            }
            last_gga_time = now_t;
        }

        int n = recv(sock, (char *)recv_buf, sizeof(recv_buf), 0);

        if (n == 0) {
            /* Server closed connection */
            printf("\n[INFO] Server closed connection\n");
            fflush(stdout);
            break;
        }

        if (n < 0) {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT) {
                /* Timeout — check stop flag and loop */
                continue;
            }
            printf("\n[ERROR] recv error %d\n", err);
            fflush(stdout);
            break;
        }

        /* ── Skip HTTP response header ───────────────────── */
        int start = 0;
        if (!header_done) {
            for (int i = 0; i < n && !header_done; i++) {
                if (header_pos < (int)sizeof(header_buf) - 1)
                    header_buf[header_pos++] = recv_buf[i];

                /* Look for \r\n\r\n end of header */
                if (header_pos >= 4 &&
                    header_buf[header_pos - 4] == '\r' &&
                    header_buf[header_pos - 3] == '\n' &&
                    header_buf[header_pos - 2] == '\r' &&
                    header_buf[header_pos - 1] == '\n') {
                    header_done = true;
                    start = i + 1;

                    /* Check for ICY 200 OK or HTTP/1.x 200 */
                    header_buf[header_pos] = '\0';
                    if (!strstr(header_buf, "200") &&
                        !strstr(header_buf, "ICY")) {
                        printf("[ERROR] Server response:\n%s\n", header_buf);
                        fflush(stdout);
                        closesocket(sock);
                        PostMessage(state->hMain, WM_APP_STREAM_DONE, 0, 0);
                        return 1;
                    }
                    printf("[INFO] Stream started\n");
                    fflush(stdout);
                }
            }
            if (!header_done)
                continue;
        }

        /* ── Track received data bytes ───────────────────── */
        int dataBytes = n - start;
        if (dataBytes > 0)
            InterlockedExchangeAdd(&state->streamBytes, dataBytes);

        /* ═══════════════════════════════════════════════════════
         *  PHASE 1 — Format detection
         *
         *  Identify the stream format from sync/header bytes.
         *  RTCM 3.x is confirmed later by a successful frame decode.
         *
         *  Distinctive 2-byte sync patterns (scanned anywhere in data):
         *    Septentrio SBF : 0x24 0x40  ("$@")
         *    UBX (u-blox)   : 0xB5 0x62
         *
         *  Weaker patterns (checked only at start of stream data):
         *    Trimble RT27   : first byte 0x10, second ≠ 0x10/0x03
         *    Leica LB2      : first byte 0x01, second ≤ 0x80 & > 0
         *
         *  These weaker patterns must only match the very first data
         *  bytes after the HTTP header, not arbitrary mid-stream bytes,
         *  to avoid false positives inside RTCM payload data.
         * ═══════════════════════════════════════════════════════ */
        if (detected_format == FMT_NONE && dataBytes > 0) {
            /* RTCM 3.x first.  Scan for the 0xD3 preamble at any position
             * and verify the next two bytes encode a plausible length
             * (RTCM 10403.3 message length is 10 bits, range 0..1023; the
             * top 6 bits of byte 1 must be zero).  This is a much stronger
             * signal than the 2-byte SBF/UBX syncs, and pre-empts the
             * false-positive case where an RTCM payload happens to
             * contain `$@` (0x24 0x40) or another sync sequence. */
            int rtcm_pos = -1;
            for (int j = start; j + 2 < n; j++) {
                if (recv_buf[j] == 0xD3 &&
                    (recv_buf[j + 1] & 0xFC) == 0x00) {
                    int rtcm_len = ((recv_buf[j + 1] & 0x03) << 8) |
                                    recv_buf[j + 2];
                    if (rtcm_len >= 2 && rtcm_len <= 1023) {
                        rtcm_pos = j;
                        break;
                    }
                }
            }
            if (rtcm_pos >= 0) {
                detected_format = FMT_RTCM3;
                /* Leave the slower in-loop framer to confirm via CRC --
                 * we've already pinned the format so the rest of the
                 * receive path goes straight to RTCM decoding. */
            }
            /* Only fall back to SBF/UBX 2-byte heuristic when RTCM didn't
             * latch.  This prevents `$@` or `0xB5 0x62` appearing inside
             * a perfectly valid RTCM payload from hijacking the stream. */
            if (detected_format == FMT_NONE) {
                for (int j = start; j < n - 1; j++) {
                    /* Septentrio SBF: sync "$@" (0x24 0x40) */
                    if (recv_buf[j] == 0x24 && recv_buf[j + 1] == 0x40) {
                        detected_format = FMT_SBF;
                        break;
                    }
                    /* UBX (u-blox): sync 0xB5 0x62 */
                    if (recv_buf[j] == 0xB5 && recv_buf[j + 1] == 0x62) {
                        detected_format = FMT_UBX;
                        break;
                    }
                }
            }

            /* Check weaker patterns only at the very first data byte
             * of the stream (first recv after HTTP header). */
            if (detected_format == FMT_NONE && first_data_check) {
                unsigned char b0 = recv_buf[start];
                /* Trimble RT27: DLE-stuffed framing starts with 0x10
                 * followed by a record type byte (not DLE/ETX). */
                if (b0 == 0x10 && dataBytes >= 4 &&
                    recv_buf[start + 1] != 0x10 &&
                    recv_buf[start + 1] != 0x03) {
                    detected_format = FMT_RT27;
                }
                /* Leica LB2: starts with 0x01, second byte is length. */
                else if (b0 == 0x01 && dataBytes >= 3 &&
                         recv_buf[start + 1] <= 0x80 &&
                         recv_buf[start + 1] > 0 &&
                         recv_buf[start + 2] < 0x40) {
                    detected_format = FMT_LB2;
                }
                /* If the very first byte is 0xD3 (RTCM preamble), don't
                 * flag any weak-pattern format — let the RTCM framing
                 * loop confirm it via successful decode. */
                first_data_check = false;
            }

            /* If a non-RTCM format was identified, publish it and decide
             * whether to activate decoding. */
            if (detected_format != FMT_NONE) {
                InterlockedExchange(&state->streamFormat, detected_format);
                PostMessage(state->hMain, WM_APP_STREAM_INFO, 0, 0);

                /* Currently only RTCM 3.x has a decoder — mark others
                 * as detected-but-unsupported so the receive loop keeps
                 * counting bytes without trying to frame/decode. */
                switch (detected_format) {
                case FMT_RTCM3:
                    decode_active = true;
                    printf("[INFO] RTCM 3.x stream detected — decoding active\n");
                    fflush(stdout);
                    break;
                default:
                    /* Future decoders: add case FMT_xxx: decode_active = true; break; */
                    decode_active = false;
                    break;
                }
            }
        }

        /* ═══════════════════════════════════════════════════════
         *  Log once when an unsupported format is detected.
         * ═══════════════════════════════════════════════════════ */
        if (detected_format != FMT_NONE && !decode_active && !unsupported_logged) {
            const char *name = "Unknown";
            switch (detected_format) {
            case FMT_UBX:  name = "UBX (u-blox)";     break;
            case FMT_SBF:  name = "Septentrio SBF";    break;
            case FMT_RT27: name = "Trimble RT27";       break;
            case FMT_LB2:  name = "Leica LB2";          break;
            }
            printf("[INFO] %s stream detected — decoding not yet supported\n", name);
            fflush(stdout);
            unsupported_logged = true;
        }

        /* If a non-RTCM format was detected and decoding is not
         * active for it, keep receiving to update byte counter and
         * data rate but skip the framing/decode loop. */
        if (detected_format != FMT_NONE && !decode_active) {
            continue;
        }

        /* ═══════════════════════════════════════════════════════
         *  PHASE 2 — Stream decoding
         *
         *  When detected_format == FMT_NONE the RTCM framing loop
         *  runs speculatively; a successful decode confirms RTCM 3.x.
         *  When detected_format == FMT_RTCM3, decoding is active.
         *
         *  Future formats: add additional decode branches here,
         *  guarded by detected_format == FMT_xxx.
         * ═══════════════════════════════════════════════════════ */
        for (int i = start; i < n; i++) {
            unsigned char b = recv_buf[i];

            /* Looking for RTCM 3.x preamble */
            if (msg_pos == 0) {
                if (b == 0xD3) {
                    msg_buf[msg_pos++] = b;
                }
                continue;
            }

            msg_buf[msg_pos++] = b;

            /* Once we have 3 bytes, compute expected frame length */
            if (msg_pos == 3) {
                int msg_length = ((msg_buf[1] & 0x03) << 8) | msg_buf[2];
                msg_target = msg_length + 6;  /* preamble(1) + len(2) + payload + crc(3) */

                if (msg_target > GUI_BUFFER_SIZE) {
                    /* Invalid — reset */
                    msg_pos = 0;
                    msg_target = 0;
                    continue;
                }
            }

            /* Check if full frame received */
            if (msg_target > 0 && msg_pos >= msg_target) {
                /* Analyze the RTCM message */
                int msg_type = analyze_rtcm_message(msg_buf, msg_target,
                                                     true, &state->config);

                if (msg_type > 0 && msg_type < GUI_MAX_MSG_TYPES) {
                    /* RTCM stream capture: write the raw frame bytes to
                     * disk if the user enabled capture from the File menu.
                     * The critical section guards against the UI thread
                     * closing the FILE* mid-write. */
                    if (state->csRtcmDumpInit) {
                        EnterCriticalSection(&state->csRtcmDump);
                        if (state->hRtcmDump) {
                            size_t w = fwrite(msg_buf, 1, (size_t)msg_target,
                                              state->hRtcmDump);
                            state->rtcmDumpBytes += (LONG)w;
                        }
                        LeaveCriticalSection(&state->csRtcmDump);
                    }

                    /* Confirm RTCM 3.x format on first successful decode */
                    if (detected_format == FMT_NONE) {
                        detected_format = FMT_RTCM3;
                        decode_active = true;
                        InterlockedExchange(&state->streamFormat, FMT_RTCM3);
                        PostMessage(state->hMain, WM_APP_STREAM_INFO, 0, 0);
                        printf("[INFO] RTCM 3.x stream confirmed — decoding active\n");
                        fflush(stdout);
                    }

                    /* Update message type stats */
                    double now = gui_get_time_seconds();
                    GuiMsgStat *s = &state->msgStats[msg_type];

                    if (!s->seen) {
                        s->seen = true;
                        s->last_time = now;
                        s->min_dt = s->max_dt = s->sum_dt = 0.0;
                    } else {
                        double dt = now - s->last_time;
                        s->last_time = now;
                        s->sum_dt += dt;
                        if (dt < s->min_dt || s->min_dt == 0.0)
                            s->min_dt = dt;
                        if (dt > s->max_dt)
                            s->max_dt = dt;
                    }
                    s->count++;

                    /* Notify UI thread — message stats */
                    PostMessage(state->hMain, WM_APP_STAT_UPDATE,
                                (WPARAM)msg_type, (LPARAM)s->count);

                    /* Extract satellite info from MSM messages (1070-1139) */
                    int msg_length = ((msg_buf[1] & 0x03) << 8) | msg_buf[2];
                    extract_satellites(msg_buf + 3, msg_length,
                                       msg_type, &state->satStats);

                    /* Notify UI thread — satellite update */
                    PostMessage(state->hMain, WM_APP_SAT_UPDATE, 0, 0);

                    /* ── Sky-plot update for MSM4/5/6/7 frames ──────────
                     * Two inputs needed: a station ARP and a valid
                     * ephemeris per SV.  Many casters (e.g. Onocoy
                     * observation streams) omit 1005/1006/1019/1045/1046
                     * entirely.  Fall back to the user-configured rover
                     * lat/lon for the ARP when no 1005/1006 has arrived.
                     * Ephemeris has no fallback — without 1019/1045/1046
                     * we can't position SVs, but we still post an empty
                     * update so the status line refreshes. */
                    {
                        bool   arp_valid = false;
                        double sx = 0, sy = 0, sz = 0;
                        rtcm_get_station_arp(&arp_valid, &sx, &sy, &sz,
                                             NULL, NULL, NULL);

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
                            ? msm_extract_prns(msg_buf + 3, msg_length,
                                               msg_type, prns,
                                               (int)(sizeof(prns) / sizeof(prns[0])),
                                               &gnss_id)
                            : 0;

                        /* For MSM7, also pull per-SV best CNR.  Use the
                         * PRN list from msm_extract_prns as authoritative;
                         * the CNR extractor returns the same PRN order. */
                        int   cnr_n = 0;
                        int   cnr_prn_list[64];
                        if (n_prns > 0 && (msg_type % 10) == 7) {
                            cnr_n = msm7_extract_cnr(msg_buf + 3, msg_length,
                                                     msg_type,
                                                     cnr_prn_list, cnr_prns,
                                                     64, NULL);
                            /* Also populate the per-band CNR cache used by
                             * the SV detail window. */
                            msm7_update_per_band_cnr(msg_buf + 3, msg_length,
                                                     msg_type);
                        } else {
                            for (int i = 0; i < 64; i++) cnr_prns[i] = 0.0f;
                        }

                        /* Build a PRN->CNR lookup for fast access below. */
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

                        /* Per-MSM-frame sky update.  We emit one SkySatUpdate
                         * for EVERY above-horizon SV that has a valid cached
                         * ephemeris in the same GNSS as this MSM frame, not
                         * only the SVs that the receiver tracked.  The flag
                         * observed_flag=1 marks the ones in this frame's sat
                         * mask; =0 means "expected by ephemeris but not in the
                         * MSM".  The UI handler uses both flags to drive the
                         * heatmap's observed/expected counters. */
                        if (n_prns > 0 &&
                            (gnss_id == 1 || gnss_id == 2 ||
                             gnss_id == 3 || gnss_id == 4 || gnss_id == 5 ||
                             gnss_id == 7)) {
                            int    gps_week;
                            double gps_tow;
                            sky_get_gps_time(&gps_week, &gps_tow);
                            double glo_tod = sky_get_glo_tod();
                            double t_prop  = (gnss_id == 2) ? glo_tod : gps_tow;

                            /* O(1) membership test for "was this PRN in the
                             * MSM frame" -- build a bitset over PRN [1..64]. */
                            uint64_t obs_mask = 0;
                            for (int i = 0; i < n_prns; i++) {
                                int p = prns[i];
                                if (p >= 1 && p <= 64) obs_mask |= 1ULL << (p - 1);
                            }

                            /* Allocate worst-case (all PRNs in this GNSS). */
                            upd = (SkySatUpdate *)HeapAlloc(
                                GetProcessHeap(), 0,
                                sizeof(SkySatUpdate) *
                                (size_t)SV_EPH_MAX_SATS_PER_GNSS);

                            if (upd) {
                                for (int p = 1; p <= SV_EPH_MAX_SATS_PER_GNSS; p++) {
                                    const SvEphemeris *eph = sv_eph_get(gnss_id, p);
                                    if (!eph) continue;
                                    if (!sv_eph_is_valid_at(eph, gps_week, t_prop))
                                        continue;

                                    double svx, svy, svz;
                                    if (!sv_to_ecef(eph, gps_week, t_prop,
                                                    &svx, &svy, &svz))
                                        continue;

                                    double az_d, el_d;
                                    azel_from_ecef(sx, sy, sz,
                                                   svx, svy, svz,
                                                   &az_d, &el_d);

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
                                    upd[upd_count].observed_flag = observed_flag;
                                    upd_count++;
                                }
                            }
                        }

                        /* Post even when upd_count == 0 so the status line
                         * in the sky window refreshes (shows "waiting for
                         * ephemeris..." etc.).  UI handler frees @c upd. */
                        if (!PostMessage(state->hMain, WM_APP_SKY_UPDATE,
                                         (WPARAM)upd_count, (LPARAM)upd)) {
                            if (upd) HeapFree(GetProcessHeap(), 0, upd);
                        }
                    }

                    /* Post raw RTCM frame to UI thread for decoding and
                     * caching.  The UI handler stores the decoded text so
                     * that detail windows opened later still show content. */
                    RtcmRawMsg *raw = (RtcmRawMsg *)HeapAlloc(
                        GetProcessHeap(), 0, sizeof(RtcmRawMsg));
                    if (raw) {
                        raw->msg_type = msg_type;
                        raw->length   = msg_target;
                        memcpy(raw->data, msg_buf, msg_target);
                        if (!PostMessage(state->hMain, WM_APP_MSG_RAW,
                                         (WPARAM)msg_type, (LPARAM)raw)) {
                            HeapFree(GetProcessHeap(), 0, raw);
                        }
                    }

                    printf("%d ", msg_type);
                    fflush(stdout);
                }

                /* Reset for next frame */
                msg_pos = 0;
                msg_target = 0;
            }
        }
    }

    closesocket(sock);

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

/* ── Ephemeris-only stream worker ─────────────────────────────────────────
 *
 * Runs in parallel with WorkerOpenStream when EPH_MOUNTPOINT is configured.
 * Connects to a separate caster (typically a public ephemeris service such
 * as Onocoy EPH or BKG BCEP00BKG0), parses RTCM frames, and feeds only
 * the ephemeris cache by calling decode_rtcm_1019 / 1045 / 1046 directly.
 *
 * Deliberately does NOT:
 *   - send GGA (eph streams don't need a rover position),
 *   - process 1005/1006 (would overwrite the obs caster's ARP),
 *   - update msgStats / satStats / Msg Stats ListView,
 *   - post WM_APP_MSG_RAW (detail-window machinery is for the obs stream),
 *   - touch the byte-rate / status-bar counters.
 *
 * Lifetime is controlled via state->bStopRequestedEph; cleanup is via
 * WM_APP_STREAM_DONE from the obs worker tearing both down at once.
 */
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

    /* Refuse to start if no mountpoint is configured */
    if (state->config.EPH_MOUNTPOINT[0] == '\0' ||
        state->config.EPH_CASTER[0]     == '\0') {
        eph_log(state, "[EPH] Disabled (no caster/mountpoint configured)\r\n");
        return 0;
    }

    /* ── DNS resolve ─────────────────────────────────────── */
    struct addrinfo hints, *result = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(state->config.EPH_CASTER, NULL, &hints, &result) != 0) {
        eph_log(state, "[EPH] DNS resolution failed for %s\r\n",
                state->config.EPH_CASTER);
        return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        eph_log(state, "[EPH] Failed to create socket\r\n");
        freeaddrinfo(result);
        return 1;
    }

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port   = htons(state->config.EPH_PORT);
    server.sin_addr   = ((struct sockaddr_in *)result->ai_addr)->sin_addr;
    memset(&server.sin_zero, 0, 8);
    freeaddrinfo(result);

    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR) {
        eph_log(state, "[EPH] Connection failed to %s:%d\r\n",
                state->config.EPH_CASTER, state->config.EPH_PORT);
        closesocket(sock);
        return 1;
    }

    DWORD timeout_ms = 200;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
               (const char *)&timeout_ms, sizeof(timeout_ms));

    /* ── NTRIP GET ───────────────────────────────────────── */
    char request[1024];
    snprintf(request, sizeof(request),
             "GET /%s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Ntrip-Version: Ntrip/2.0\r\n"
             "User-Agent: NTRIP CClient/1.0\r\n"
             "Authorization: Basic %s\r\n"
             "\r\n",
             state->config.EPH_MOUNTPOINT,
             state->config.EPH_CASTER,
             state->config.EPH_AUTH_BASIC);

    if (send(sock, request, (int)strlen(request), 0) <= 0) {
        eph_log(state, "[EPH] Failed to send NTRIP request\r\n");
        closesocket(sock);
        return 1;
    }

    eph_log(state, "[EPH] HTTP GET sent to %s:%d/%s; awaiting response...\r\n",
            state->config.EPH_CASTER, state->config.EPH_PORT,
            state->config.EPH_MOUNTPOINT);

    /* ── Receive loop (RTCM 3.x framing only) ────────────── */
    unsigned char recv_buf[GUI_BUFFER_SIZE];
    unsigned char msg_buf[GUI_BUFFER_SIZE];
    int  msg_pos    = 0;
    int  msg_target = 0;
    bool header_done = false;
    char header_buf[4096];
    int  header_pos = 0;
    int  eph_count = 0;   /* total ephemerides cached so far */

    while (!state->bStopRequestedEph) {
        int n = recv(sock, (char *)recv_buf, sizeof(recv_buf), 0);
        if (n == 0) {
            eph_log(state, "[EPH] Server closed connection\r\n");
            break;
        }
        if (n < 0) {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT) continue;
            eph_log(state, "[EPH] recv error %d\r\n", err);
            break;
        }

        int start = 0;
        if (!header_done) {
            for (int i = 0; i < n && !header_done; i++) {
                if (header_pos < (int)sizeof(header_buf) - 1)
                    header_buf[header_pos++] = recv_buf[i];
                if (header_pos >= 4 &&
                    header_buf[header_pos - 4] == '\r' &&
                    header_buf[header_pos - 3] == '\n' &&
                    header_buf[header_pos - 2] == '\r' &&
                    header_buf[header_pos - 1] == '\n') {
                    header_done = true;
                    start = i + 1;
                    header_buf[header_pos] = '\0';
                    if (!strstr(header_buf, "200") &&
                        !strstr(header_buf, "ICY")) {
                        eph_log(state, "[EPH] Server response:\r\n%s\r\n",
                                header_buf);
                        closesocket(sock);
                        return 1;
                    }
                    eph_log(state,
                            "[EPH] Stream accepted by %s/%s -- decoding ephemerides\r\n",
                            state->config.EPH_CASTER,
                            state->config.EPH_MOUNTPOINT);
                }
            }
            if (!header_done) continue;
        }

        for (int i = start; i < n; i++) {
            unsigned char b = recv_buf[i];

            if (msg_pos == 0) {
                if (b == 0xD3) msg_buf[msg_pos++] = b;
                continue;
            }
            msg_buf[msg_pos++] = b;

            if (msg_pos == 3) {
                int msg_length = ((msg_buf[1] & 0x03) << 8) | msg_buf[2];
                msg_target = msg_length + 6;
                if (msg_target > GUI_BUFFER_SIZE) {
                    msg_pos = 0; msg_target = 0;
                    continue;
                }
            }

            if (msg_target > 0 && msg_pos >= msg_target) {
                /* Peek message number (first 12 bits of payload, which
                 * starts at byte index 3 of the frame).  RTCM packs the
                 * 12-bit field across bytes 3 and 4. */
                int payload_len = msg_target - 6;
                int mt = ((int)msg_buf[3] << 4) | ((int)msg_buf[4] >> 4);

                switch (mt) {
                case 1019:
                    decode_rtcm_1019(&msg_buf[3], payload_len);
                    eph_count++;
                    break;
                case 1020:
                    decode_rtcm_1020(&msg_buf[3], payload_len);
                    eph_count++;
                    break;
                case 1041:
                    decode_rtcm_1041(&msg_buf[3], payload_len);
                    eph_count++;
                    break;
                case 1042:
                    decode_rtcm_1042(&msg_buf[3], payload_len);
                    eph_count++;
                    break;
                case 1044:
                    decode_rtcm_1044(&msg_buf[3], payload_len);
                    eph_count++;
                    break;
                case 1045:
                    decode_rtcm_1045(&msg_buf[3], payload_len);
                    eph_count++;
                    break;
                case 1046:
                    decode_rtcm_1046(&msg_buf[3], payload_len);
                    eph_count++;
                    break;
                default:
                    /* Silently drop everything else -- including 1005/1006
                     * which we must not let overwrite the obs ARP. */
                    break;
                }

                msg_pos = 0; msg_target = 0;
            }
        }
    }

    closesocket(sock);

    eph_log(state, "[EPH] Stream worker finished (%d ephemerides processed)\r\n",
            eph_count);

    return 0;
}

/* ── RTCM replay worker ───────────────────────────────────────────────────
 *
 * Reads a .rtcm3 capture file (raw RTCM frames concatenated) and feeds
 * each frame through the same UI-update pipeline the obs worker uses --
 * stats, satellites, raw-msg detail, sky-plot updates.  Pacing comes from
 * the 30-bit GPS epoch_time field in MSM headers: between consecutive
 * frames carrying a valid epoch_time we sleep by the diff, capped at 1 s
 * to skip over recording gaps.  Non-MSM frames advance the cursor without
 * sleeping.
 *
 * Lifetime is shared with WorkerOpenStream via hWorkerThread /
 * bWorkerRunning / bStopRequested so Close Stream stops replay.  The eph
 * worker and capture-to-disk are not started during replay.
 */
DWORD WINAPI WorkerReplayRtcm(LPVOID param)
{
    AppState *state = (AppState *)param;

    printf("[INFO] Replay: opening %s\n", state->replayPath);
    fflush(stdout);

    FILE *f = fopen(state->replayPath, "rb");
    if (!f) {
        printf("[ERROR] Replay: cannot open file\n");
        fflush(stdout);
        PostMessage(state->hMain, WM_APP_STREAM_DONE, 0, 0);
        return 1;
    }

    /* Tell the UI we're decoding "RTCM 3.x" so the status bar gets a sane
     * label even though no caster is involved. */
    InterlockedExchange(&state->streamFormat, 1 /* FMT_RTCM3 */);
    PostMessage(state->hMain, WM_APP_STREAM_INFO, 0, 0);

    unsigned char msg_buf[GUI_BUFFER_SIZE];
    int  frames_decoded = 0;
    long total_bytes    = 0;

    while (!state->bStopRequested) {
        /* Find the next 0xD3 preamble.  Re-sync if the file ends or has
         * stray bytes between frames. */
        int b;
        do {
            b = fgetc(f);
            if (b == EOF) goto eof;
        } while (b != 0xD3);

        msg_buf[0] = (unsigned char)b;
        int got = (int)fread(msg_buf + 1, 1, 2, f);   /* length bytes */
        if (got < 2) break;

        int msg_length = ((msg_buf[1] & 0x03) << 8) | msg_buf[2];
        int frame_len  = msg_length + 6;              /* preamble + len + crc */
        if (frame_len > GUI_BUFFER_SIZE) {
            /* Bogus length -- try to resync from next byte. */
            continue;
        }

        got = (int)fread(msg_buf + 3, 1, frame_len - 3, f);
        if (got < frame_len - 3) break;

        /* ── Process the frame ── */
        int msg_type = analyze_rtcm_message(msg_buf, frame_len, true,
                                            &state->config);
        if (msg_type > 0 && msg_type < GUI_MAX_MSG_TYPES) {
            frames_decoded++;
            total_bytes += frame_len;
            InterlockedExchangeAdd(&state->streamBytes, (LONG)frame_len);

            /* Per-MSM-type stats */
            double now = gui_get_time_seconds();
            GuiMsgStat *s = &state->msgStats[msg_type];
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
            s->count++;
            PostMessage(state->hMain, WM_APP_STAT_UPDATE,
                        (WPARAM)msg_type, (LPARAM)s->count);

            /* Satellite stats + sky-plot update (same logic as obs worker) */
            int payload_len_inner = msg_length;
            extract_satellites(msg_buf + 3, payload_len_inner,
                               msg_type, &state->satStats);
            PostMessage(state->hMain, WM_APP_SAT_UPDATE, 0, 0);

            /* Update per-band CNR cache for MSM7 frames so SV detail
             * windows show live per-signal values from the replay. */
            if ((msg_type % 10) == 7)
                msm7_update_per_band_cnr(msg_buf + 3, payload_len_inner, msg_type);

            /* Sky-plot pipeline: same gate as the obs worker. */
            {
                bool   arp_valid = false;
                double sx = 0, sy = 0, sz = 0;
                rtcm_get_station_arp(&arp_valid, &sx, &sy, &sz,
                                     NULL, NULL, NULL);
                if (!arp_valid &&
                    (state->config.LATITUDE != 0.0 ||
                     state->config.LONGITUDE != 0.0)) {
                    geodetic_to_ecef(state->config.LATITUDE,
                                     state->config.LONGITUDE,
                                     0.0, &sx, &sy, &sz);
                    arp_valid = true;
                }

                int prns[64];
                int gnss_id = 0;
                int n_prns = arp_valid
                    ? msm_extract_prns(msg_buf + 3, payload_len_inner,
                                       msg_type, prns, 64, &gnss_id)
                    : 0;

                int   cnr_n = 0, cnr_prn_list[64];
                float cnr_prns[64];
                if (n_prns > 0 && (msg_type % 10) == 7)
                    cnr_n = msm7_extract_cnr(msg_buf + 3, payload_len_inner,
                                             msg_type,
                                             cnr_prn_list, cnr_prns, 64, NULL);
                else
                    for (int i = 0; i < 64; i++) cnr_prns[i] = 0.0f;

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
                    (gnss_id == 1 || gnss_id == 2 ||
                     gnss_id == 3 || gnss_id == 4 || gnss_id == 5 ||
                     gnss_id == 7)) {
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

                    upd = (SkySatUpdate *)HeapAlloc(GetProcessHeap(), 0,
                        sizeof(SkySatUpdate) * SV_EPH_MAX_SATS_PER_GNSS);
                    if (upd) {
                        for (int p = 1; p <= SV_EPH_MAX_SATS_PER_GNSS; p++) {
                            const SvEphemeris *eph = sv_eph_get(gnss_id, p);
                            if (!eph) continue;
                            if (!sv_eph_is_valid_at(eph, gps_week, t_prop))
                                continue;
                            double svx, svy, svz;
                            if (!sv_to_ecef(eph, gps_week, t_prop,
                                            &svx, &svy, &svz))
                                continue;
                            double az_d, el_d;
                            azel_from_ecef(sx, sy, sz,
                                           svx, svy, svz, &az_d, &el_d);
                            if (el_d <= 0.0) continue;

                            int observed_flag = (p >= 1 && p <= 64)
                                ? ((obs_mask >> (p - 1)) & 1ULL) ? 1 : 0
                                : 0;
                            float cnr_dbhz = 0.0f;
                            if (observed_flag &&
                                p <= SV_EPH_MAX_SATS_PER_GNSS)
                                cnr_dbhz = cnr_by_prn[p];

                            upd[upd_count].gnss_id       = gnss_id;
                            upd[upd_count].prn           = p;
                            upd[upd_count].az_deg        = (float)az_d;
                            upd[upd_count].el_deg        = (float)el_d;
                            upd[upd_count].cnr_dbhz      = cnr_dbhz;
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

            /* Raw-frame post for the detail window pipeline. */
            RtcmRawMsg *raw = (RtcmRawMsg *)HeapAlloc(
                GetProcessHeap(), 0, sizeof(RtcmRawMsg));
            if (raw) {
                raw->msg_type = msg_type;
                raw->length   = frame_len;
                memcpy(raw->data, msg_buf, frame_len);
                if (!PostMessage(state->hMain, WM_APP_MSG_RAW,
                                 (WPARAM)msg_type, (LPARAM)raw)) {
                    HeapFree(GetProcessHeap(), 0, raw);
                }
            }

            /* No pacing: replay parses frames as fast as the disk + CPU
             * allow.  Real-time playback is only useful when the file
             * has gaps we want to honour; for analysis we want to get
             * the full picture into the UI immediately. */
        }
    }

eof:
    fclose(f);

    printf("\n[INFO] Replay finished: %d frames, %ld bytes from %s\n",
           frames_decoded, total_bytes, state->replayPath);
    fflush(stdout);

    PostMessage(state->hMain, WM_APP_STREAM_DONE, 0, 0);
    return 0;
}
