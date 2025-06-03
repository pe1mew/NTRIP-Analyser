#include "ntrip_handler.h"
#include "rtcm3x_parser.h"
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
#define CLOSESOCKET close
#define SOCKET_TYPE int
#define SOCK_ERR(val) ((val) < 0)
#define SOCK_CONN_ERR(val) ((val) < 0)
#endif

#define BUFFER_SIZE 4096
#define MAX_MSG_TYPES 4096

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
        fprintf(stderr, "DNS lookup failed: %s\n", gai_strerror(gai_ret));
#endif
        return NULL;
    }

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (SOCK_ERR(sock)) {
        fprintf(stderr, "Socket creation failed\n");
        freeaddrinfo(result);
#ifdef _WIN32
        WSACleanup();
#endif
        return NULL;
    }

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
        return NULL;
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
        return NULL;
    }
#else
    ssize_t sent = send(sock, request, strlen(request), 0);
    if (sent < 0) {
        perror("[ERROR] Failed to send mountpoint list request");
        CLOSESOCKET(sock);
        return NULL;
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
            return NULL;
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
    return mount_table;
}

void start_ntrip_stream(const NTRIP_Config *config) {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", WSAGetLastError());
        return;
    }
#endif

    SOCKET_TYPE sock;
    struct sockaddr_in server;
    struct addrinfo hints, *result;
    char request[1024];
    char buffer[BUFFER_SIZE];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int gai_ret = getaddrinfo(config->NTRIP_CASTER, NULL, &hints, &result);
    if (gai_ret != 0) {
#ifdef _WIN32
        fprintf(stderr, "DNS lookup failed: %d\n", WSAGetLastError());
        WSACleanup();
#else
        fprintf(stderr, "DNS lookup failed: %s\n", gai_strerror(gai_ret));
#endif
        return;
    }

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (SOCK_ERR(sock)) {
        fprintf(stderr, "Socket creation failed\n");
        freeaddrinfo(result);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

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
        return;
    }

    snprintf(request, sizeof(request),
             "GET /%s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Ntrip-Version: Ntrip/2.0\r\n"
             "User-Agent: NTRIP CClient/1.0\r\n"
             "Authorization: Basic %s\r\n"
             "\r\n",
             config->MOUNTPOINT, config->NTRIP_CASTER, config->AUTH_BASIC);

#ifdef _WIN32
    int sent = send(sock, request, strlen(request), 0);
    if (sent == SOCKET_ERROR) {
        fprintf(stderr, "[ERROR] Failed to send NTRIP stream request: %d\n", WSAGetLastError());
        CLOSESOCKET(sock);
        WSACleanup();
        return;
    }
#else
    ssize_t sent = send(sock, request, strlen(request), 0);
    if (sent < 0) {
        perror("[ERROR] Failed to send NTRIP stream request");
        CLOSESOCKET(sock);
        return;
    }
#endif

    int received;
    char *ptr;
    int header_skipped = 0;
    unsigned char msg_buffer[BUFFER_SIZE];
    int msg_buffer_len = 0;

    while (1) {
#ifdef _WIN32
        received = recv(sock, buffer, sizeof(buffer), 0);
#else
        received = recv(sock, buffer, sizeof(buffer), 0);
#endif
        if (received <= 0) break;

        if (!header_skipped) {
            buffer[received] = '\0';
            ptr = strstr(buffer, "\r\n\r\n");
            if (ptr) {
                int offset = (ptr - buffer) + 4;
                memmove(buffer, buffer + offset, received - offset);
                received -= offset;
                header_skipped = 1;
            } else {
                continue;
            }
        }

        int buf_pos = 0;
        while (buf_pos < received) {
            if (msg_buffer_len == 0) {
                while (buf_pos < received && (unsigned char)buffer[buf_pos] != 0xD3) {
                    buf_pos++;
                }
                if (buf_pos >= received) break;
            }
            int to_copy = received - buf_pos;
            if (msg_buffer_len + to_copy > BUFFER_SIZE) to_copy = BUFFER_SIZE - msg_buffer_len;
            memcpy(msg_buffer + msg_buffer_len, buffer + buf_pos, to_copy);
            msg_buffer_len += to_copy;
            buf_pos += to_copy;

            while (msg_buffer_len >= 3) {
                if (msg_buffer[0] != 0xD3) {
                    memmove(msg_buffer, msg_buffer + 1, --msg_buffer_len);
                    continue;
                }
                int msg_length = ((msg_buffer[1] & 0x03) << 8) | msg_buffer[2];
                int full_frame = msg_length + 6;
                if (msg_buffer_len < full_frame) break;

                // Only decode and print, do not collect stats or print table
                analyze_rtcm_message(msg_buffer, full_frame, false);

                memmove(msg_buffer, msg_buffer + full_frame, msg_buffer_len - full_frame);
                msg_buffer_len -= full_frame;
            }
        }
    }

    CLOSESOCKET(sock);
#ifdef _WIN32
    WSACleanup();
#endif
}

void start_ntrip_stream_with_filter(const NTRIP_Config *config, const int *filter_list, int filter_count) {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", WSAGetLastError());
        return;
    }
#endif

    SOCKET_TYPE sock;
    struct sockaddr_in server;
    struct addrinfo hints, *result;
    char request[1024]; // Increased from 512 to 1024
    char buffer[BUFFER_SIZE];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int gai_ret = getaddrinfo(config->NTRIP_CASTER, NULL, &hints, &result);
    if (gai_ret != 0) {
#ifdef _WIN32
        fprintf(stderr, "DNS lookup failed: %d\n", WSAGetLastError());
        WSACleanup();
#else
        fprintf(stderr, "DNS lookup failed: %s\n", gai_strerror(gai_ret));
#endif
        return;
    }

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (SOCK_ERR(sock)) {
        fprintf(stderr, "Socket creation failed\n");
        freeaddrinfo(result);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

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
        return;
    }

    snprintf(request, sizeof(request),
             "GET /%s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Ntrip-Version: Ntrip/2.0\r\n"
             "User-Agent: NTRIP CClient/1.0\r\n"
             "Authorization: Basic %s\r\n"
             "\r\n",
             config->MOUNTPOINT, config->NTRIP_CASTER, config->AUTH_BASIC);

#ifdef _WIN32
    int sent = send(sock, request, strlen(request), 0);
    if (sent == SOCKET_ERROR) {
        fprintf(stderr, "[ERROR] Failed to send NTRIP stream request: %d\n", WSAGetLastError());
        CLOSESOCKET(sock);
        WSACleanup();
        return;
    }
#else
    ssize_t sent = send(sock, request, strlen(request), 0);
    if (sent < 0) {
        perror("[ERROR] Failed to send NTRIP stream request");
        CLOSESOCKET(sock);
        return;
    }
#endif

    int received;
    char *ptr;
    int header_skipped = 0;
    unsigned char msg_buffer[BUFFER_SIZE];
    int msg_buffer_len = 0;

    while (1) {
        received = recv(sock, buffer, sizeof(buffer), 0);
        if (received <= 0) break;

        if (!header_skipped) {
            buffer[received] = '\0';
            ptr = strstr(buffer, "\r\n\r\n");
            if (ptr) {
                int offset = (ptr - buffer) + 4;
                memmove(buffer, buffer + offset, received - offset);
                received -= offset;
                header_skipped = 1;
            } else {
                continue;
            }
        }

        int buf_pos = 0;
        while (buf_pos < received) {
            if (msg_buffer_len == 0) {
                while (buf_pos < received && (unsigned char)buffer[buf_pos] != 0xD3) {
                    buf_pos++;
                }
                if (buf_pos >= received) break;
            }
            int to_copy = received - buf_pos;
            if (msg_buffer_len + to_copy > BUFFER_SIZE) to_copy = BUFFER_SIZE - msg_buffer_len;
            memcpy(msg_buffer + msg_buffer_len, buffer + buf_pos, to_copy);
            msg_buffer_len += to_copy;
            buf_pos += to_copy;

            while (msg_buffer_len >= 3) {
                if (msg_buffer[0] != 0xD3) {
                    memmove(msg_buffer, msg_buffer + 1, --msg_buffer_len);
                    continue;
                }
                int msg_length = ((msg_buffer[1] & 0x03) << 8) | msg_buffer[2];
                int full_frame = msg_length + 6;
                if (msg_buffer_len < full_frame) break;

                // Always analyze first to get the message type (no output)
                int msg_type = analyze_rtcm_message(msg_buffer, full_frame, true);

                int in_filter = 0;
                if (filter_count == 0) {
                    // No filter: print all messages
                    analyze_rtcm_message(msg_buffer, full_frame, false);
                } else {
                    // Only print if in filter_list, else print "."
                    for (int i = 0; i < filter_count; ++i) {
                        if (msg_type == filter_list[i]) {
                            analyze_rtcm_message(msg_buffer, full_frame, false);
                            in_filter = 1;
                            break;
                        }
                    }
                    if (!in_filter) {
                        printf(".");
                        fflush(stdout);
                    }
                }

                memmove(msg_buffer, msg_buffer + full_frame, msg_buffer_len - full_frame);
                msg_buffer_len -= full_frame;
            }
        }
    }

    CLOSESOCKET(sock);
#ifdef _WIN32
    WSACleanup();
#endif
}

void analyze_message_types(const NTRIP_Config *config, int analysis_time) {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", WSAGetLastError());
        return;
    }
#endif

    SOCKET_TYPE sock;
    struct sockaddr_in server;
    struct addrinfo hints, *result;
    char request[1024]; // Increased from 512 to 1024
    char buffer[BUFFER_SIZE];

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", WSAGetLastError());
        return;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(config->NTRIP_CASTER, NULL, &hints, &result) != 0) {
        fprintf(stderr, "DNS lookup failed: %d\n", WSAGetLastError());
        WSACleanup();
        return;
    }

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "Socket creation failed: %d\n", WSAGetLastError());
        freeaddrinfo(result);
        WSACleanup();
        return;
    }

    server.sin_family = AF_INET;
    server.sin_port = htons(config->NTRIP_PORT);
    server.sin_addr = ((struct sockaddr_in *)result->ai_addr)->sin_addr;
    memset(&(server.sin_zero), 0, 8);

    freeaddrinfo(result);

    if (connect(sock, (struct sockaddr *)&server, sizeof(struct sockaddr)) == SOCKET_ERROR) {
        fprintf(stderr, "Connection failed: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return;
    }

    snprintf(request, sizeof(request),
             "GET /%s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Ntrip-Version: Ntrip/2.0\r\n"
             "User-Agent: NTRIP CClient/1.0\r\n"
             "Authorization: Basic %s\r\n"
             "\r\n",
             config->MOUNTPOINT, config->NTRIP_CASTER, config->AUTH_BASIC);

    int sent = send(sock, request, strlen(request), 0);
    if (sent == SOCKET_ERROR) {
        fprintf(stderr, "[ERROR] Failed to send NTRIP stream request: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return;
    }

    int received;
    char *ptr;
    int header_skipped = 0;
    unsigned char msg_buffer[BUFFER_SIZE];
    int msg_buffer_len = 0;

    MsgStats stats[MAX_MSG_TYPES] = {0};
    double start_time = (double)clock() / CLOCKS_PER_SEC;

    printf("[INFO] Analyzing message types for %d seconds...\n", analysis_time);

    while (((double)clock() / CLOCKS_PER_SEC) - start_time < (double)analysis_time) {
        received = recv(sock, buffer, sizeof(buffer), 0);
        if (received <= 0) break;

        if (!header_skipped) {
            buffer[received] = '\0';
            ptr = strstr(buffer, "\r\n\r\n");
            if (ptr) {
                int offset = (ptr - buffer) + 4;
                memmove(buffer, buffer + offset, received - offset);
                received -= offset;
                header_skipped = 1;
            } else {
                continue;
            }
        }

        int buf_pos = 0;
        while (buf_pos < received) {
            if (msg_buffer_len == 0) {
                while (buf_pos < received && (unsigned char)buffer[buf_pos] != 0xD3) {
                    buf_pos++;
                }
                if (buf_pos >= received) break;
            }
            int to_copy = received - buf_pos;
            if (msg_buffer_len + to_copy > BUFFER_SIZE) to_copy = BUFFER_SIZE - msg_buffer_len;
            memcpy(msg_buffer + msg_buffer_len, buffer + buf_pos, to_copy);
            msg_buffer_len += to_copy;
            buf_pos += to_copy;

            while (msg_buffer_len >= 3) {
                if (msg_buffer[0] != 0xD3) {
                    memmove(msg_buffer, msg_buffer + 1, --msg_buffer_len);
                    continue;
                }
                int msg_length = ((msg_buffer[1] & 0x03) << 8) | msg_buffer[2];
                int full_frame = msg_length + 6;
                if (msg_buffer_len < full_frame) break;

                double now = (double)clock() / CLOCKS_PER_SEC;
                int msg_type = analyze_rtcm_message(msg_buffer, full_frame, true);
                if (msg_type > 0 && msg_type < MAX_MSG_TYPES) {
                    printf("%d ", msg_type); // Print message number in sequence
                    fflush(stdout);

                    MsgStats *s = &stats[msg_type];
                    if (!s->seen) {
                        s->seen = true;
                        s->last_time = now;
                        s->min_dt = s->max_dt = s->sum_dt = 0.0;
                    } else {
                        double dt = now - s->last_time;
                        s->last_time = now;
                        s->sum_dt += dt;
                        s->min_dt = (dt < s->min_dt || s->min_dt == 0.0) ? dt : s->min_dt;
                        s->max_dt = dt > s->max_dt ? dt : s->max_dt;
                    }
                    s->count++;
                }

                memmove(msg_buffer, msg_buffer + full_frame, msg_buffer_len - full_frame);
                msg_buffer_len -= full_frame;
            }
        }
    }

    closesocket(sock);
    WSACleanup();

    // Print statistics as a table
    printf("\n[INFO] Message type analysis complete. Statistics:\n");
    printf("+-------------+-------+---------------+---------------+---------------+\n");
    printf("| MessageType | Count |  Min-DT (S)   |  Max-DT (S)   |  Avg-DT (S)   |\n");
    printf("+-------------+-------+---------------+---------------+---------------+\n");
    for (int i = 1; i < MAX_MSG_TYPES; i++) {
        if (stats[i].seen && stats[i].count > 0) {
            double avg_dt = stats[i].count > 0 ? stats[i].sum_dt / stats[i].count : 0.0;
            printf("| %-11d | %5d | %13.3f | %13.3f | %13.3f |\n",
                   i, stats[i].count, stats[i].min_dt, stats[i].max_dt, avg_dt);
        }
    }
    printf("+-------------+-------+---------------+---------------+---------------+\n");
}

void extract_satellites(const unsigned char *data, int len, int msg_type, SatStatsSummary *summary) {
    int gnss_id = get_gnss_id_from_rtcm(msg_type);
    if (!gnss_id) return;

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

    int bit = 24;
    /* int n_sat = (int)get_bits(data, bit, 6); */ bit += 6; // Remove unused variable warning
    bit += 1 + 3; // skip other MSM header fields

    for (int s = 1; s <= 64 && s <= MAX_SATS_PER_GNSS; ++s) {
        if (get_bits(data, bit, 1)) {
            if (!summary->gnss[idx].sat_seen[s-1]) {
                summary->gnss[idx].sat_seen[s-1] = 1;
                summary->gnss[idx].count++;
            }
        }
        bit++;
    }
}

const char* rinex_id_from_gnss(int gnss_id, int prn, char *buf, size_t buflen) {
    // RINEX 3: G = GPS, R = GLONASS, E = Galileo, J = QZSS, C = BeiDou, S = SBAS
    char sys = '?';
    switch (gnss_id) {
        case 1: sys = 'G'; break; // GPS
        case 2: sys = 'R'; break; // GLONASS
        case 3: sys = 'E'; break; // Galileo
        case 4: sys = 'J'; break; // QZSS
        case 5: sys = 'C'; break; // BeiDou
        case 6: sys = 'S'; break; // SBAS
        default: sys = '?'; break;
    }
    snprintf(buf, buflen, "%c%02d", sys, prn);
    return buf;
}

void analyze_satellites_stream(const NTRIP_Config *config, int analysis_time) {
    printf("Opening NTRIP stream and analyzing satellites for %d seconds...\n", analysis_time);
    SatStatsSummary summary = {0};

    SOCKET_TYPE sock;
    struct sockaddr_in server;
    struct addrinfo hints, *result;
    char request[1024];
    char buffer[BUFFER_SIZE];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int gai_ret = getaddrinfo(config->NTRIP_CASTER, NULL, &hints, &result);
    if (gai_ret != 0) {
        fprintf(stderr, "DNS lookup failed\n");
        return;
    }

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (SOCK_ERR(sock)) {
        fprintf(stderr, "Socket creation failed\n");
        freeaddrinfo(result);
        return;
    }

    server.sin_family = AF_INET;
    server.sin_port = htons(config->NTRIP_PORT);
    server.sin_addr = ((struct sockaddr_in *)result->ai_addr)->sin_addr;
    memset(&(server.sin_zero), 0, 8);

    freeaddrinfo(result);

    if (SOCK_CONN_ERR(connect(sock, (struct sockaddr *)&server, sizeof(struct sockaddr)))) {
        fprintf(stderr, "Connection failed\n");
        CLOSESOCKET(sock);
        return;
    }

    snprintf(request, sizeof(request),
             "GET /%s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Ntrip-Version: Ntrip/2.0\r\n"
             "User-Agent: NTRIP CClient/1.0\r\n"
             "Authorization: Basic %s\r\n"
             "\r\n",
             config->MOUNTPOINT, config->NTRIP_CASTER, config->AUTH_BASIC);

    send(sock, request, strlen(request), 0);

    int received;
    int header_skipped = 0;
    unsigned char msg_buffer[BUFFER_SIZE];
    int msg_buffer_len = 0;

    time_t start_time = time(NULL);

    while (difftime(time(NULL), start_time) < analysis_time) {
        received = recv(sock, buffer, sizeof(buffer), 0);
        if (received <= 0) break;

        if (!header_skipped) {
            buffer[received] = '\0';
            char *ptr = strstr(buffer, "\r\n\r\n");
            if (ptr) {
                int offset = (ptr - buffer) + 4;
                memmove(buffer, buffer + offset, received - offset);
                received -= offset;
                header_skipped = 1;
            } else {
                continue;
            }
        }

        int buf_pos = 0;
        while (buf_pos < received) {
            if (msg_buffer_len == 0) {
                while (buf_pos < received && (unsigned char)buffer[buf_pos] != 0xD3) {
                    buf_pos++;
                }
                if (buf_pos >= received) break;
            }
            int to_copy = received - buf_pos;
            if (msg_buffer_len + to_copy > BUFFER_SIZE) to_copy = BUFFER_SIZE - msg_buffer_len;
            memcpy(msg_buffer + msg_buffer_len, buffer + buf_pos, to_copy);
            msg_buffer_len += to_copy;
            buf_pos += to_copy;

            while (msg_buffer_len >= 3) {
                if (msg_buffer[0] != 0xD3) {
                    memmove(msg_buffer, msg_buffer + 1, --msg_buffer_len);
                    continue;
                }
                int msg_length = ((msg_buffer[1] & 0x03) << 8) | msg_buffer[2];
                int full_frame = msg_length + 6;
                if (msg_buffer_len < full_frame) break;

                int msg_type = (msg_buffer[3] << 4) | (msg_buffer[4] >> 4);

                extract_satellites(msg_buffer + 3, msg_length, msg_type, &summary);

                // Count unique satellites after this message
                int total_unique = 0;
                for (int i = 0; i < summary.gnss_count; ++i) {
                    total_unique += summary.gnss[i].count;
                }
                printf("%d ", total_unique);

                memmove(msg_buffer, msg_buffer + full_frame, msg_buffer_len - full_frame);
                msg_buffer_len -= full_frame;
            }
        }
    }

    CLOSESOCKET(sock);


    // Set satellites column width for 30 satellites, 3 chars per satellite (2 digits + space): 30*3 = 90
    #define SAT_COL_WIDTH 60

    // Calculate total unique satellites
    int total_unique = 0;
    for (int i = 0; i < summary.gnss_count; ++i) {
        total_unique += summary.gnss[i].count;
    }

    // Print dynamic table border
    char border[256];
    int pos = 0;
    border[pos++] = '+';
    for (int i = 0; i < 11; ++i) border[pos++] = '-'; // GNSS col
    border[pos++] = '+';
    for (int i = 0; i < 12; ++i) border[pos++] = '-'; // #Sats Seen col
    border[pos++] = '+';
    for (int i = 0; i < SAT_COL_WIDTH+1; ++i) border[pos++] = '-'; // Satellites col
    border[pos++] = '+';
    border[pos++] = '\0';

    // printf("\nTotal unique satellites seen: %d\n", total_unique);
    printf("\nGNSS systems and satellites seen:\n");
    printf("%s\n", border);
    printf("|   GNSS    | #Sats Seen | Satellites%*s|\n", SAT_COL_WIDTH - 10, ""); // 10 = strlen("Satellites")
    printf("%s\n", border);
    for (int i = 0; i < summary.gnss_count; ++i) {
        // Prepare satellite list as a string, 2 chars per satellite, with space between
        char sat_list[MAX_SATS_PER_GNSS * 4] = "";
        int pos = 0;
        int first = 1;
        char idbuf[8];
        for (int s = 0; s < MAX_SATS_PER_GNSS; ++s) {
            if (summary.gnss[i].sat_seen[s]) {
                const char *rinex = rinex_id_from_gnss(summary.gnss[i].gnss_id, s + 1, idbuf, sizeof(idbuf));
                pos += snprintf(sat_list + pos, sizeof(sat_list) - pos, "%s%s", first ? "" : " ", rinex);
                first = 0;
            }
        }
        if (pos == 0) snprintf(sat_list, sizeof(sat_list), "None");

        // Word-wrap the satellite list
        int len = strlen(sat_list);
        int offset = 0;
        int first_line = 1;
        while (offset < len) {
            char line_buf[SAT_COL_WIDTH + 1];
            int copy_len = (len - offset > SAT_COL_WIDTH) ? SAT_COL_WIDTH : (len - offset);
            strncpy(line_buf, sat_list + offset, copy_len);
            line_buf[copy_len] = '\0';

            if (first_line) {
                printf("| %-9s | %10d | %-*s|\n",
                       gnss_name_from_id(summary.gnss[i].gnss_id),
                       summary.gnss[i].count,
                       SAT_COL_WIDTH, line_buf);
                first_line = 0;
            } else {
                printf("| %-9s | %10s | %-*s|\n",
                       "", "", SAT_COL_WIDTH, line_buf);
            }
            offset += copy_len;
            // Skip leading spaces on next line
            while (sat_list[offset] == ' ') offset++;
        }
    }
    printf("%s\n", border);
    printf("| Total     | %10d | %-*s|\n", total_unique, SAT_COL_WIDTH, ""); // Print total at the end
    printf("%s\n", border);
}

const char* gnss_name_from_id(int gnss_id) {
    switch (gnss_id) {
        case 1: return "GPS";
        case 2: return "GLONASS";
        case 3: return "Galileo";
        case 4: return "QZSS";
        case 5: return "BeiDou";
        case 6: return "SBAS";
        default: return "Unknown";
    }
}

int get_gnss_id_from_rtcm(int msg_type) {
    if (msg_type >= 1070 && msg_type < 1080) return 1; // GPS
    if (msg_type >= 1080 && msg_type < 1090) return 2; // GLONASS
    if (msg_type >= 1090 && msg_type < 1100) return 3; // Galileo
    if (msg_type >= 1110 && msg_type < 1120) return 4; // QZSS
    if (msg_type >= 1120 && msg_type < 1130) return 5; // BeiDou
    if (msg_type >= 1130 && msg_type < 1140) return 6; // SBAS
    return 0;
}