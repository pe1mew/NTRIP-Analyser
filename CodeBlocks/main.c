#define _WIN32_WINNT 0x0601 // Define Windows version (Windows 7 or higher)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

#define BUFFER_SIZE 4096

// Configuration variables
char NTRIP_CASTER[256];
int NTRIP_PORT;
char MOUNTPOINT[256];
char USERNAME[256];
char PASSWORD[256];

// Function prototypes
void base64_encode(const char *input, char *output);
void analyze_rtcm_message(const unsigned char *data, int length);
int load_config(const char *filename);

int main() {
    WSADATA wsaData;
    SOCKET sock;
    struct sockaddr_in server;
    struct addrinfo hints, *result;
    char request[512], auth[128], auth_encoded[256];
    char buffer[BUFFER_SIZE];

    // Load configuration from JSON file
    if (load_config("config.json") != 0) {
        fprintf(stderr, "Failed to load configuration.\n");
        return 1;
    }

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", WSAGetLastError());
        return 1;
    }

    // Resolve host
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(NTRIP_CASTER, NULL, &hints, &result) != 0) {
        fprintf(stderr, "DNS lookup failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "Socket creation failed: %d\n", WSAGetLastError());
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }

    // Set up the server address
    server.sin_family = AF_INET;
    server.sin_port = htons(NTRIP_PORT);
    server.sin_addr = ((struct sockaddr_in *)result->ai_addr)->sin_addr; // Use result from getaddrinfo
    memset(&(server.sin_zero), 0, 8);

    freeaddrinfo(result); // Free the memory allocated by getaddrinfo

    if (connect(sock, (struct sockaddr *)&server, sizeof(struct sockaddr)) == SOCKET_ERROR) {
        fprintf(stderr, "Connection failed: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    // Create basic auth string
    snprintf(auth, sizeof(auth), "%s:%s", USERNAME, PASSWORD);
    base64_encode(auth, auth_encoded);

    // Send request
    snprintf(request, sizeof(request),
             "GET /%s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Ntrip-Version: Ntrip/2.0\r\n"
             "User-Agent: NTRIP CClient/1.0\r\n"
             "Authorization: Basic %s\r\n"
             "\r\n",
             MOUNTPOINT, NTRIP_CASTER, auth_encoded);

    send(sock, request, strlen(request), 0);

    // Read and skip HTTP headers
    int received;
    char *ptr;
    while ((received = recv(sock, buffer, sizeof(buffer), 0)) > 0) {
        buffer[received] = '\0';
        ptr = strstr(buffer, "\r\n\r\n");
        if (ptr) {
            int offset = (ptr - buffer) + 4;
            analyze_rtcm_message((unsigned char *)(buffer + offset), received - offset);
            break;
        }
    }

    // Main loop to receive RTCM
    while ((received = recv(sock, buffer, sizeof(buffer), 0)) > 0) {
        analyze_rtcm_message((unsigned char *)buffer, received);
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}

// Function to load configuration from a JSON file
int load_config(const char *filename) {
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
    strcpy(NTRIP_CASTER, cJSON_GetObjectItem(json, "NTRIP_CASTER")->valuestring);
    NTRIP_PORT = cJSON_GetObjectItem(json, "NTRIP_PORT")->valueint;
    strcpy(MOUNTPOINT, cJSON_GetObjectItem(json, "MOUNTPOINT")->valuestring);
    strcpy(USERNAME, cJSON_GetObjectItem(json, "USERNAME")->valuestring);
    strcpy(PASSWORD, cJSON_GetObjectItem(json, "PASSWORD")->valuestring);

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

void analyze_rtcm_message(const unsigned char *data, int length) {
    if (length < 6) return;

    // RTCM message header typically starts with 0xD3
    if (data[0] == 0xD3) {
        int msg_length = ((data[1] & 0x03) << 8) | data[2];
        int msg_type = ((data[3] & 0xFC) >> 2);

        printf("RTCM Message: Type = %d, Length = %d\n", msg_type, msg_length);
    } else {
        printf("Unknown or malformed data\n");
    }
}
