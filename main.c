#define _WIN32_WINNT 0x0601 // Define Windows version (Windows 7 or higher)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <getopt.h>
#include <time.h>
#include <stdbool.h>
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
#define MAX_MSG_TYPES 4096

typedef struct {
    int count;
    double min_dt;
    double max_dt;
    double sum_dt;
    double last_time;
    bool seen;
} MsgStats;

bool show_mount_table = false; // Add this variable near the top with the other flags

int main(int argc, char *argv[]) {
    NTRIP_Config config;
    const char *config_filename = "config.json";
    int opt;
    bool analyze_types = false;
    int analysis_time = 60; // default to 60 seconds

    // If no arguments, enable message type analysis for 60 seconds
    if (argc == 1) {
        analyze_types = true;
    }

    while ((opt = getopt(argc, argv, "c:t:m")) != -1) {
        switch (opt) {
            case 'c':
                config_filename = optarg;
                break;
            case 't':
                analysis_time = atoi(optarg);
                if (analysis_time <= 0) analysis_time = 60;
                analyze_types = true; // Only analyze if -t is set
                break;
            case 'm':
                show_mount_table = true;
                break;
            default:
                fprintf(stderr, "Usage: %s [-c config_file] [-t seconds] [-m]\n", argv[0]);
                return 1;
        }
    }

    if (load_config(config_filename, &config) != 0) {
        fprintf(stderr, "Failed to load configuration.\n");
        return 1;
    }

    char auth[128];
    snprintf(auth, sizeof(auth), "%s:%s", config.USERNAME, config.PASSWORD);
    base64_encode(auth, config.AUTH_BASIC);

    if (analyze_types) {
        analyze_message_types(&config, analysis_time);
        return 0;
    }

    // === 1. Request and display mountpoint list ===
    if (show_mount_table) {
        printf("[DEBUG] Requesting mountpoint list (sourcetable)...\n");
        char *mount_table = receive_mount_table(&config);
        if (mount_table) {
            printf("%s\n", mount_table);
            free(mount_table);
        } else {
            fprintf(stderr, "[ERROR] Failed to retrieve mountpoint list.\n");
            return 1;
        }
    }

    // If only -m is set, exit after showing the table
    if (!analyze_types) {
        return 0;
    }


    // === 2. Start NTRIP stream from configured mountpoint ===
    printf("[DEBUG] Starting NTRIP stream from mountpoint '%s'...\n", config.MOUNTPOINT);
    start_ntrip_stream(&config);

    return 0;
}
