#include "net/ntrip_handler.h"
#include "core/nmea_parser.h"
#include "core/rtcm3x_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #define CLOSESOCKET closesocket
    #define SOCKET_TYPE SOCKET
    #define SOCK_ERR(val) ((val) == INVALID_SOCKET)
    #define SOCK_CONN_ERR(val) ((val) == SOCKET_ERROR)
#else
    #include <netdb.h>
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <errno.h>
    #define CLOSESOCKET close
    #define SOCKET_TYPE int
    #define SOCK_ERR(val) ((val) < 0)
    #define SOCK_CONN_ERR(val) ((val) < 0)
    #define SOCKET_ERROR   -1
#endif

#ifdef _WIN32
#include <windows.h>
static inline double get_time_seconds() {
    static LARGE_INTEGER freq;
    static int freq_initialized = 0;
    LARGE_INTEGER now;
    if (!freq_initialized) {
        QueryPerformanceFrequency(&freq);
        freq_initialized = 1;
    }
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / freq.QuadPart;
}
#else
#include <time.h>
static inline double get_time_seconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}
#endif

/*

// Replace all time measurement code for -t option with get_time_seconds()
// For example, if you have something like:
double prev_time = 0, curr_time = 0;
// ...existing code...
curr_time = get_time_seconds();
double dt = curr_time - prev_time;
prev_time = curr_time;
// ...existing code...

*/

#define BUFFER_SIZE 4096
#define MAX_MSG_TYPES 4096

// Set satellites column width for 30 satellites, 3 chars per satellite (2 digits + space): 30*3 = 90
#define SAT_COL_WIDTH 60

typedef struct {
    int count;
    double min_dt;
    double max_dt;
    double sum_dt;
    double last_time;
    bool seen;
} MsgStats;

void base64_encode(const char *input, char *output) {
    const char *base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned char in[3];
    int i, j;
    int input_len = strlen(input);
    int output_index = 0;

    for (i = 0; i < input_len;) {
        int len = 0;
        for (j = 0; j < 3; j++) {
            if (i < input_len) in[j] = input[i++];
            else in[j] = 0, len++;
        }

        output[output_index++] = base64_chars[in[0] >> 2];
        output[output_index++] = base64_chars[((in[0] & 0x03) << 4) | (in[1] >> 4)];
        output[output_index++] = (len >= 2) ? '=' : base64_chars[((in[1] & 0x0f) << 2) | (in[2] >> 6)];
        output[output_index++] = (len >= 1) ? '=' : base64_chars[in[2] & 0x3f];
    }
    output[output_index] = '\0';
}

char* receive_mount_table(const NTRIP_Config *config) {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", WSAGetLastError());
        return NULL;
    }
#endif

    if (!config) {
        fprintf(stderr, "[ERROR] Config pointer is NULL.\n");
        return NULL; // -1
    }

    SOCKET_TYPE sock;
    struct sockaddr_in server;
    struct addrinfo hints, *result;
    char request[1024];
    char buffer[BUFFER_SIZE];
    char *mount_table = NULL;
    size_t mount_table_size = 0;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int gai_ret = getaddrinfo(config->NTRIP_CASTER, NULL, &hints, &result);
    if (gai_ret != 0) {
#ifdef _WIN32
        fprintf(stderr, "DNS lookup failed: %d\n", WSAGetLastError());
        WSACleanup();
#else
        fprintf(stderr, "DNS lookup failed: %s\n", gai_strerror(errno));
#endif
        return NULL; // -2
    }

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "Socket creation failed: %d\n", WSAGetLastError());
        freeaddrinfo(result);
        WSACleanup();
        return NULL; // -3
    }
#else
    if (sock < 0) {
        perror("Socket creation failed");
        freeaddrinfo(result);
        return NULL; // -3
    }
#endif

    server.sin_family = AF_INET;
    server.sin_port = htons(config->NTRIP_PORT);
    server.sin_addr = ((struct sockaddr_in *)result->ai_addr)->sin_addr;
    memset(&(server.sin_zero), 0, 8);

    freeaddrinfo(result);

    if (SOCK_CONN_ERR(connect(sock, (struct sockaddr *)&server, sizeof(struct sockaddr)))) {
        fprintf(stderr, "Connection failed\n");
        CLOSESOCKET(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return NULL; // -4
    }

    snprintf(request, sizeof(request),
             "GET / HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: NTRIP CClient/1.0\r\n"
             "Authorization: Basic %s\r\n"
             "\r\n",
             config->NTRIP_CASTER, config->AUTH_BASIC);

#ifdef _WIN32
    int sent = send(sock, request, strlen(request), 0);
    if (sent == SOCKET_ERROR) {
        fprintf(stderr, "[ERROR] Failed to send mountpoint list request: %d\n", WSAGetLastError());
        CLOSESOCKET(sock);
        WSACleanup();
        return NULL; // -5
    }
#else
    ssize_t sent = send(sock, request, strlen(request), 0);
    if (sent < 0) {
        perror("[ERROR] Failed to send mountpoint list request");
        CLOSESOCKET(sock);
        return NULL; // -5
    }
#endif

    int received;
    while (
#ifdef _WIN32
        (received = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0
#else
        (received = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0
#endif
    ) {
        buffer[received] = '\0';
        size_t old_size = mount_table_size;
        mount_table_size += received;
        char *new_table = realloc(mount_table, mount_table_size + 1);
        if (!new_table) {
            free(mount_table);
            fprintf(stderr, "[ERROR] Memory allocation failed for mount table.\n");
            CLOSESOCKET(sock);
#ifdef _WIN32
            WSACleanup();
#endif
            return NULL; // -6
        }
        mount_table = new_table;
        memcpy(mount_table + old_size, buffer, received + 1);
        if (strstr(buffer, "ENDSOURCETABLE")) {
            break;
        }
    }
    if (mount_table)
        mount_table[mount_table_size] = '\0';

    CLOSESOCKET(sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return mount_table; // 0 (success)
}

void extract_satellites(const unsigned char *data, int len, int msg_type, SatStatsSummary *summary) {
    int prns[MAX_SATS_PER_GNSS];
    int gnss_id = 0;
    int n = msm_extract_prns(data, len, msg_type, prns, MAX_SATS_PER_GNSS, &gnss_id);
    if (n <= 0 || !gnss_id) return;

    int idx = -1;
    for (int i = 0; i < summary->gnss_count; ++i) {
        if (summary->gnss[i].gnss_id == gnss_id) {
            idx = i;
            break;
        }
    }
    if (idx == -1 && summary->gnss_count < MAX_GNSS) {
        idx = summary->gnss_count++;
        summary->gnss[idx].gnss_id = gnss_id;
        memset(summary->gnss[idx].sat_seen, 0, sizeof(summary->gnss[idx].sat_seen));
        summary->gnss[idx].count = 0;
    }
    if (idx == -1) return;

    for (int i = 0; i < n; ++i) {
        int prn = prns[i];
        if (prn < 1 || prn > MAX_SATS_PER_GNSS) continue;
        if (!summary->gnss[idx].sat_seen[prn - 1]) {
            summary->gnss[idx].sat_seen[prn - 1] = 1;
            summary->gnss[idx].count++;
        }
    }
}

const char* rinex_id_from_gnss(int gnss_id, int prn, char *buf, size_t buflen) {
    // RINEX 3: G = GPS, R = GLONASS, E = Galileo, J = QZSS,
    //         C = BeiDou, S = SBAS, I = NavIC / IRNSS
    char sys = '?';
    switch (gnss_id) {
        case 1: sys = 'G'; break; // GPS
        case 2: sys = 'R'; break; // GLONASS
        case 3: sys = 'E'; break; // Galileo
        case 4: sys = 'J'; break; // QZSS
        case 5: sys = 'C'; break; // BeiDou
        case 6: sys = 'S'; break; // SBAS
        case 7: sys = 'I'; break; // NavIC / IRNSS
        default: sys = '?'; break;
    }
    snprintf(buf, buflen, "%c%02d", sys, prn);
    return buf;
}

const char* gnss_name_from_id(int gnss_id) {
    switch (gnss_id) {
        case 1: return "GPS";
        case 2: return "GLONASS";
        case 3: return "Galileo";
        case 4: return "QZSS";
        case 5: return "BeiDou";
        case 6: return "SBAS";
        case 7: return "NavIC";
        default: return "Unknown";
    }
}

/* Note: this mapping must agree with msm_extract_prns() in rtcm3x_parser.c,
 * which is what actually drives the Satellites tab today.  Previously this
 * function had SBAS mis-mapped to the 1130 range and lacked a NavIC entry. */
int get_gnss_id_from_rtcm(int msg_type) {
    if (msg_type >= 1070 && msg_type < 1080) return 1; // GPS
    if (msg_type >= 1080 && msg_type < 1090) return 2; // GLONASS
    if (msg_type >= 1090 && msg_type < 1100) return 3; // Galileo
    if (msg_type >= 1100 && msg_type < 1110) return 6; // SBAS
    if (msg_type >= 1110 && msg_type < 1120) return 4; // QZSS
    if (msg_type >= 1120 && msg_type < 1130) return 5; // BeiDou
    if (msg_type >= 1130 && msg_type < 1140) return 7; // NavIC / IRNSS
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────
 * Ephemeris-only NTRIP worker used by CLI `-s --sky` mode.
 *
 * Mirrors gui_thread.c WorkerOpenEphStream but reduced to the bare CLI
 * essentials: no UI messages, no log redirection -- just connect, parse
 * RTCM frames, dispatch eph-bearing messages (1019/1020/1041/1042/
 * 1044/1045/1046) into the per-SV ephemeris cache via the existing
 * decode_rtcm_* functions.  Polls @p stop_flag every recv() iteration
 * so it can exit promptly on Ctrl-C.
 * ───────────────────────────────────────────────────────────────────── */
