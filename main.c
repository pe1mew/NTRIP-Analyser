#define _WIN32_WINNT 0x0601 // Define Windows version (Windows 7 or higher)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <getopt.h>
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
#include "ntrip_handler.h"
#include "config.h"

#define BUFFER_SIZE 4096

int main(int argc, char *argv[]) {
    NTRIP_Config config;
    const char *config_filename = "config.json";

    int opt;
    while ((opt = getopt(argc, argv, "c:")) != -1) {
        switch (opt) {
            case 'c':
                config_filename = optarg;
                break;
            default:
                fprintf(stderr, "Usage: %s [-c config_file]\n", argv[0]);
                return 1;
        }
    }

    // Load configuration from JSON file
    if (load_config(config_filename, &config) != 0) {
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
