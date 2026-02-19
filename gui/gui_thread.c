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

#include <stdio.h>
#include <ctype.h>

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
            /* Scan for distinctive 2-byte sync patterns anywhere */
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

                    /* Post raw RTCM frame for detail window decode */
                    if (state->hDetailWnds[msg_type] != NULL) {
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

