#include "ntrip_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif
#include "rtcm3x_parser.h"
#include "lib/cJSON/cJSON.h"

#define BUFFER_SIZE 4096

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
    WSADATA wsaData;
    SOCKET sock;
    struct sockaddr_in server;
    struct addrinfo hints, *result;
    char request[512];
    char buffer[BUFFER_SIZE];
    char *mount_table = NULL;
    size_t mount_table_size = 0;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", WSAGetLastError());
        return NULL;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(config->NTRIP_CASTER, NULL, &hints, &result) != 0) {
        fprintf(stderr, "DNS lookup failed: %d\n", WSAGetLastError());
        WSACleanup();
        return NULL;
    }

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "Socket creation failed: %d\n", WSAGetLastError());
        freeaddrinfo(result);
        WSACleanup();
        return NULL;
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
        return NULL;
    }

    snprintf(request, sizeof(request),
             "GET / HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: NTRIP CClient/1.0\r\n"
             "Authorization: Basic %s\r\n"
             "\r\n",
             config->NTRIP_CASTER, config->AUTH_BASIC);

    int sent = send(sock, request, strlen(request), 0);
    if (sent == SOCKET_ERROR) {
        fprintf(stderr, "[ERROR] Failed to send mountpoint list request: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return NULL;
    }

    int received;
    while ((received = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[received] = '\0';
        size_t old_size = mount_table_size;
        mount_table_size += received;
        char *new_table = realloc(mount_table, mount_table_size + 1);
        if (!new_table) {
            free(mount_table);
            fprintf(stderr, "[ERROR] Memory allocation failed for mount table.\n");
            closesocket(sock);
            WSACleanup();
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

    closesocket(sock);
    WSACleanup();
    return mount_table;
}

void start_ntrip_stream(const NTRIP_Config *config) {
    WSADATA wsaData;
    SOCKET sock;
    struct sockaddr_in server;
    struct addrinfo hints, *result;
    char request[512];
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
    while ((received = recv(sock, buffer, sizeof(buffer), 0)) > 0) {
        if (!header_skipped) {
            buffer[received] = '\0';
            ptr = strstr(buffer, "\r\n\r\n");
            if (ptr) {
                int offset = (ptr - buffer) + 4;
                analyze_rtcm_message((unsigned char *)(buffer + offset), received - offset);
                header_skipped = 1;
            }
            continue;
        }

        unsigned char msg_buffer[BUFFER_SIZE];
        int msg_buffer_len = 0;

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

                analyze_rtcm_message(msg_buffer, full_frame);

                memmove(msg_buffer, msg_buffer + full_frame, msg_buffer_len - full_frame);
                msg_buffer_len -= full_frame;
            }
        }
    }

    closesocket(sock);
    WSACleanup();
}