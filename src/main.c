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
#include "cJSON.h" // Include cJSON library
#include "rtcm3x_parser.h" // Include RTCM parser header
#include "ntrip_handler.h"
#include "config.h"
#include "cli_help.h"

#define BUFFER_SIZE 4096
#define MAX_MSG_TYPES 4096

// Define column widths for verbose printing
#define CONF_KEY_WIDTH 14
#define CONF_VAL_WIDTH 26

typedef struct {
    int count;
    double min_dt;
    double max_dt;
    double sum_dt;
    double last_time;
    bool seen;
} MsgStats;

bool verbose = false;
int filter_list[MAX_MSG_TYPES] = {0};
int filter_count = 0;

int main(int argc, char *argv[]) {
    NTRIP_Config config;
    const char *config_filename = "config.json";
    int opt;
    int analysis_time = 60; // default to 60 seconds
    Operation operation = OP_NONE;

    static struct option long_options[] = {
        {"config",    optional_argument, 0, 'c'},
        {"types",     optional_argument, 0, 't'},
        {"mounts",    no_argument,       0, 'm'},
        {"decode",    optional_argument, 0, 'd'},
        {"sat",       optional_argument, 0, 's'},
        {"raw",       no_argument,       0, 'r'},
        {"latitude",  required_argument, 0,  1 },
        {"lat",       required_argument, 0,  2 },
        {"longitude", required_argument, 0,  3 },
        {"lon",       required_argument, 0,  4 },
        {"verbose",   no_argument,       0, 'v'},
        {"generate",  no_argument,       0, 'g'},
        {"info",      no_argument,       0, 'i'},
        {0, 0, 0, 0}
    };

    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "c::t::md::vs::gir", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'c':
                if (optarg) {
                    config_filename = optarg;
                } else if (optind < argc && argv[optind] && argv[optind][0] != '-') {
                    config_filename = argv[optind];
                    optind++; // Skip this argument
                } else {
                    config_filename = "config.json";
                }
                break;
            case 't':
                operation = OP_ANALYZE_TYPES;
                if (optarg) {
                    analysis_time = atoi(optarg);
                    if (analysis_time <= 0) analysis_time = 60;
                } else if (optind < argc && argv[optind] && argv[optind][0] != '-') {
                    analysis_time = atoi(argv[optind]);
                    if (analysis_time <= 0) analysis_time = 60;
                    optind++; // Skip this argument
                } else {
                    analysis_time = 60;
                }
                break;
            case 'm':
                operation = OP_SHOW_MOUNT_FORMATTED;
                break;
            case 'r':
                operation = OP_SHOW_MOUNT_RAW;
                break;
            case 'd':
                operation = OP_DECODE_STREAM;
                if (optarg) {
                    char *token = strtok(optarg, ", ");
                    while (token && filter_count < MAX_MSG_TYPES) {
                        filter_list[filter_count++] = atoi(token);
                        token = strtok(NULL, ", ");
                    }
                } else if (optind < argc && argv[optind] && argv[optind][0] != '-') {
                    char *token = strtok(argv[optind], ", ");
                    while (token && filter_count < MAX_MSG_TYPES) {
                        filter_list[filter_count++] = atoi(token);
                        token = strtok(NULL, ", ");
                    }
                    optind++; // Skip this argument
                }
                break;
            case 's':
                operation = OP_ANALYZE_SATS;
                if (optarg) {
                    analysis_time = atoi(optarg);
                    if (analysis_time <= 0) analysis_time = 60;
                } else if (optind < argc && argv[optind] && argv[optind][0] != '-') {
                    analysis_time = atoi(argv[optind]);
                    if (analysis_time <= 0) analysis_time = 60;
                    optind++;
                } else {
                    analysis_time = 60;
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
            case 'v':
                verbose = true;
                break;
            case 'g':
                initialize_config("config.json");
                return 0;
            case 'i':
                print_program_info();
                return 0;
            case 'h':
                print_help(argv[0]);
                return 0;
            default:
                print_help(argv[0]);
                return 1;
        }
    }

    // If no arguments provided, force verbose to show "No action specified"
    if (argc == 1) {
        verbose = true;
    }

    if (load_config(config_filename, &config) != 0) {
        fprintf(stderr, "[ERROR] Could not open or parse config file: %s\n", config_filename);
        fprintf(stderr, "Aborting.\n");
        return 1;
    }

#ifdef _WIN32   // === Windows-specific: Initialize Winsock ===
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        fprintf(stderr, "[ERROR] WSAStartup failed.\n");
        return 1;
    }
#endif

    char auth[512];
    snprintf(auth, sizeof(auth), "%s:%s", config.USERNAME, config.PASSWORD);
    base64_encode(auth, config.AUTH_BASIC);

    // === Verbose reporting ===
    if (verbose) {
        print_verbose_config(
            &config,
            config_filename,
            operation
        );
    }

    // === 0. Analyze message types if requested ===
    if (operation == OP_ANALYZE_TYPES) {
        analyze_message_types(&config, analysis_time);
#ifdef _WIN32
        WSACleanup();
#endif
        return 0;
    }

    // === 1. Request and display mountpoint list ===
    if (operation == OP_SHOW_MOUNT_FORMATTED || operation == OP_SHOW_MOUNT_RAW) {
        printf("[DEBUG] Requesting mountpoint list (sourcetable)...\n");
        char *mount_table = receive_mount_table(&config);
        if (mount_table) {
            if (operation == OP_SHOW_MOUNT_RAW) {
                // Print raw mountpoint list
                printf("%s", mount_table);
            } else {
                // Print formatted mountpoint list (default behavior)
                printf("%s\n", mount_table);
            }
            free(mount_table);
        } else {
            fprintf(stderr, "[ERROR] Failed to retrieve mountpoint list.\n");
#ifdef _WIN32
            WSACleanup();
#endif
            return 1;
        }
        // If only -m or -r is set, exit after showing the table
        if (operation != OP_DECODE_STREAM) {
#ifdef _WIN32
            WSACleanup();
#endif
            return 0;
        }
    }

    // === 2. Start NTRIP stream from configured mountpoint ===
    if (operation == OP_DECODE_STREAM) {
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
        start_ntrip_stream_with_filter(&config, filter_list, filter_count, verbose);
#ifdef _WIN32
        WSACleanup();
#endif
        return 0;
    }

    if (operation == OP_ANALYZE_SATS) {
        analyze_satellites_stream(&config, analysis_time);
#ifdef _WIN32
        WSACleanup();
#endif
        return 0;
    }

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
