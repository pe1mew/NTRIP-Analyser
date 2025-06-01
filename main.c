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
bool decode_stream = false;
int filter_list[MAX_MSG_TYPES] = {0};
int filter_count = 0;

int main(int argc, char *argv[]) {
    NTRIP_Config config;
    const char *config_filename = "config.json";
    int opt;
    bool analyze_types = false;
    int analysis_time = 60; // default to 60 seconds

    static struct option long_options[] = {
        {"config",    optional_argument, 0, 'c'},
        {"time",      optional_argument, 0, 't'},
        {"mounts",    no_argument,       0, 'm'},
        {"decode",    optional_argument, 0, 'd'},
        {"latitude",  required_argument, 0,  1 },
        {"lat",       required_argument, 0,  2 },
        {"longitude", required_argument, 0,  3 },
        {"lon",       required_argument, 0,  4 },
        {0, 0, 0, 0}
    };

    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "c::t::md::", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'c':
                config_filename = optarg ? optarg : "config.json";
                break;
            case 't':
                analyze_types = true;
                if (optarg) {
                    analysis_time = atoi(optarg);
                    if (analysis_time <= 0) analysis_time = 60;
                } else if (optind < argc && argv[optind] && argv[optind][0] != '-') {
                    // Handle -t 10 (where 10 is the next argv)
                    analysis_time = atoi(argv[optind]);
                    if (analysis_time <= 0) analysis_time = 60;
                    optind++; // Skip this argument
                } else {
                    analysis_time = 60;
                }
                break;
            case 'm':
                show_mount_table = true;
                break;
            case 'd':
                decode_stream = true;
                if (optarg) {
                    char *token = strtok(optarg, ", ");
                    while (token && filter_count < MAX_MSG_TYPES) {
                        filter_list[filter_count++] = atoi(token);
                        token = strtok(NULL, ", ");
                    }
                } else if (optind < argc && argv[optind] && argv[optind][0] != '-') {
                    // Handle -d 1005 (where 1005 is the next argv)
                    char *token = strtok(argv[optind], ", ");
                    while (token && filter_count < MAX_MSG_TYPES) {
                        filter_list[filter_count++] = atoi(token);
                        token = strtok(NULL, ", ");
                    }
                    optind++; // Skip this argument
                }
                break;
            case 1: // --latitude
            case 2: // --lat
                if (optarg) config.LATITUDE = atof(optarg);
                break;
            case 3: // --longitude
            case 4: // --lon
                if (optarg) config.LONGITUDE = atof(optarg);
                break;
            default:
                fprintf(stderr, "Usage: %s [-c[config_file]] [-t[seconds]] [-m] [-d[message_numbers]]\n", argv[0]);
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
        // If only -m is set, exit after showing the table
        if (!decode_stream) {
            return 0;
        }
    }

    // === 2. Start NTRIP stream from configured mountpoint ===
    if (decode_stream) {
        printf("[DEBUG] Starting NTRIP stream from mountpoint '%s'...\n", config.MOUNTPOINT);
        if (filter_count > 0) {
            printf("[DEBUG] Filter list: ");
            for (int i = 0; i < filter_count; ++i) {
                printf("%d ", filter_list[i]);
            }
            printf("\n");
        } else {
            printf("[DEBUG] No filter: all message types will be shown.\n");
        }
        start_ntrip_stream_with_filter(&config, filter_list, filter_count);
        return 0;
    }

    return 0;
}
