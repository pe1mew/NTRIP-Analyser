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

    /* ── Receive loop with RTCM framing and stats ────────── */
    unsigned char recv_buf[GUI_BUFFER_SIZE];
    unsigned char msg_buf[GUI_BUFFER_SIZE];
    int msg_pos = 0;
    int msg_target = 0;   /* expected full frame length once known */
    bool header_done = false;
    char header_buf[4096];
    int header_pos = 0;
    bool format_detected = false;

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
        if (dataBytes > 0) {
            InterlockedExchangeAdd(&state->streamBytes, dataBytes);

            /* Scan for UBX sync (0xB5 0x62) in data before RTCM framing claims it */
            if (!format_detected) {
                for (int j = start; j < n - 1; j++) {
                    if (recv_buf[j] == 0xB5 && recv_buf[j + 1] == 0x62) {
                        InterlockedExchange(&state->streamFormat, 2);  /* UBX */
                        format_detected = true;
                        PostMessage(state->hMain, WM_APP_STREAM_INFO, 0, 0);
                        break;
                    }
                }
            }
        }

        /* ── Process RTCM data ───────────────────────────── */
        for (int i = start; i < n; i++) {
            unsigned char b = recv_buf[i];

            /* Looking for preamble */
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
                    /* Detect RTCM 3.x format on first successful decode */
                    if (!format_detected) {
                        InterlockedExchange(&state->streamFormat, 1);  /* RTCM 3.x */
                        format_detected = true;
                        PostMessage(state->hMain, WM_APP_STREAM_INFO, 0, 0);
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

