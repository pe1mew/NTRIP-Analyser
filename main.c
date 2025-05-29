#define _WIN32_WINNT 0x0601 // Define Windows version (Windows 7 or higher)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h> // Required for getaddrinfo
#else
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif
#include "lib/cJSON/cJSON.h" // Include cJSON library
#include "rtcm3x_parser.h" // Include RTCM parser header

#define BUFFER_SIZE 4096

// Configuration struct
typedef struct {
    char NTRIP_CASTER[256];
    int NTRIP_PORT;
    char MOUNTPOINT[256];
    char USERNAME[256];
    char PASSWORD[256];
    char AUTH_BASIC[256]; // Base64 encoded "username:password"
} NTRIP_Config;

// Function prototypes
void base64_encode(const char *input, char *output);
void analyze_rtcm_message(const unsigned char *data, int length);
int load_config(const char *filename, NTRIP_Config *config);
char* receive_mount_table(const NTRIP_Config *config);
void start_ntrip_stream(const NTRIP_Config *config);

int main() {
    NTRIP_Config config;

    // Load configuration from JSON file
    if (load_config("config.json", &config) != 0) {
        fprintf(stderr, "Failed to load configuration.\n");
        return 1;
    }

    // Prepare basic auth string and store in config
    char auth[128];
    snprintf(auth, sizeof(auth), "%s:%s", config.USERNAME, config.PASSWORD);
    base64_encode(auth, config.AUTH_BASIC);

    // === 1. Request and display mountpoint list ===
    printf("[DEBUG] Requesting mountpoint list (sourcetable)...\n");
    char *mount_table = receive_mount_table(&config);
    if (mount_table) {
        printf("%s\n", mount_table);
        free(mount_table);
    } else {
        fprintf(stderr, "[ERROR] Failed to retrieve mountpoint list.\n");
        return 1;
    }

    // === 2. Start NTRIP stream from configured mountpoint ===
    printf("[DEBUG] Starting NTRIP stream from mountpoint '%s'...\n", config.MOUNTPOINT);
    start_ntrip_stream(&config);

    return 0;
}

// Receives the mount table as a string (caller must free)
char* receive_mount_table(const NTRIP_Config *config) {
    WSADATA wsaData;
    SOCKET sock;
    struct sockaddr_in server;
    struct addrinfo hints, *result;
    char request[512];
    char buffer[BUFFER_SIZE];
    char *mount_table = NULL;
    size_t mount_table_size = 0;

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", WSAGetLastError());
        return NULL;
    }

    // Resolve host
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(config->NTRIP_CASTER, NULL, &hints, &result) != 0) {
        fprintf(stderr, "DNS lookup failed: %d\n", WSAGetLastError());
        WSACleanup();
        return NULL;
    }

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "Socket creation failed: %d\n", WSAGetLastError());
        freeaddrinfo(result);
        WSACleanup();
        return NULL;
    }

    // Set up the server address
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

    // Prepare and send request using config->AUTH_BASIC
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

    // Receive the mount table
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

// Function to load configuration from a JSON file
int load_config(const char *filename, NTRIP_Config *config) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Failed to open config file");
        return -1;
    }

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *data = (char *)malloc(length + 1);
    if (!data) {
        perror("Failed to allocate memory for config file");
        fclose(file);
        return -1;
    }

    fread(data, 1, length, file);
    data[length] = '\0';
    fclose(file);

    cJSON *json = cJSON_Parse(data);
    free(data);

    if (!json) {
        fprintf(stderr, "Failed to parse JSON: %s\n", cJSON_GetErrorPtr());
        return -1;
    }

    // Extract configuration values
    strcpy(config->NTRIP_CASTER, cJSON_GetObjectItem(json, "NTRIP_CASTER")->valuestring);
    config->NTRIP_PORT = cJSON_GetObjectItem(json, "NTRIP_PORT")->valueint;
    strcpy(config->MOUNTPOINT, cJSON_GetObjectItem(json, "MOUNTPOINT")->valuestring);
    strcpy(config->USERNAME, cJSON_GetObjectItem(json, "USERNAME")->valuestring);
    strcpy(config->PASSWORD, cJSON_GetObjectItem(json, "PASSWORD")->valuestring);

    cJSON_Delete(json);
    return 0;
}

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

// Starts the NTRIP stream from the configured mountpoint and prints RTCM message types
void start_ntrip_stream(const NTRIP_Config *config) {
    WSADATA wsaData;
    SOCKET sock;
    struct sockaddr_in server;
    struct addrinfo hints, *result;
    char request[512];
    char buffer[BUFFER_SIZE];

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", WSAGetLastError());
        return;
    }

    // Resolve host
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(config->NTRIP_CASTER, NULL, &hints, &result) != 0) {
        fprintf(stderr, "DNS lookup failed: %d\n", WSAGetLastError());
        WSACleanup();
        return;
    }

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "Socket creation failed: %d\n", WSAGetLastError());
        freeaddrinfo(result);
        WSACleanup();
        return;
    }

    // Set up the server address
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

    // Prepare and send request for the RTCM stream
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

    // Read and skip HTTP headers
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
            // If we are not in the middle of a message, look for 0xD3
            if (msg_buffer_len == 0) {
                // Search for 0xD3 preamble
                while (buf_pos < received && (unsigned char)buffer[buf_pos] != 0xD3) {
                    buf_pos++;
                }
                if (buf_pos >= received) break; // No preamble found in this chunk
            }
            // Copy bytes to msg_buffer
            int to_copy = received - buf_pos;
            if (msg_buffer_len + to_copy > BUFFER_SIZE) to_copy = BUFFER_SIZE - msg_buffer_len;
            memcpy(msg_buffer + msg_buffer_len, buffer + buf_pos, to_copy);
            msg_buffer_len += to_copy;
            buf_pos += to_copy;

            // If we have at least 3 bytes, we can get the message length
            while (msg_buffer_len >= 3) {
                if (msg_buffer[0] != 0xD3) {
                    // Shift buffer until we find 0xD3
                    memmove(msg_buffer, msg_buffer + 1, --msg_buffer_len);
                    continue;
                }
                int msg_length = ((msg_buffer[1] & 0x03) << 8) | msg_buffer[2];
                int full_frame = msg_length + 6; // 3 header + msg + 3 CRC
                if (msg_buffer_len < full_frame) break; // Wait for more data

                // We have a full message, analyze it
                analyze_rtcm_message(msg_buffer, full_frame);

                // Remove this message from buffer
                memmove(msg_buffer, msg_buffer + full_frame, msg_buffer_len - full_frame);
                msg_buffer_len -= full_frame;
            }
        }
    }

    closesocket(sock);
    WSACleanup();
}
