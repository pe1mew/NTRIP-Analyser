#include "cli_help.h"
#include "config.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define CONF_KEY_WIDTH 14
#define CONF_VAL_WIDTH 26

void print_program_info(void) {
    printf(
        "NTRIP-Analyser - RTCM 3.x Stream Analyzer and NTRIP Client\n"
        "This program connects to NTRIP casters, retrieves mountpoint tables, and decodes RTCM 3.x streams.\n"
        "For usage instructions, run with -h or --help.\n"
        "\n"
        "Author: Remko Welling, PE1MEW\n"
        "License: Apache License 2.0 with Commons Clause\n"
        "GitHub: https://github.com/pe1mew/NTRIP-Analyser\n"
    );
}

void print_help(const char *progname) {
    printf("Usage: %s [options]\n", progname);
    printf("Options:\n");
    printf("  -c, --config [file]      Specify config file (default: config.json)\n");
    printf("  -m, --mounts             Show mountpoint list (sourcetable)\n");
    printf("  -r, --raw                Show mountpoint list in raw format (use with -m)\n");
    printf("  -d, --decode [types]     Start NTRIP stream (optionally filter message types, comma-separated)\n");
    printf("  -s, --sat [seconds]      Analyze unique satellites for N seconds (default: 60)\n");
    printf("  -t, --time [seconds]     Analyze message types for N seconds (default: 60)\n");
    printf("  -v, --verbose            Verbose output\n");
    printf("  -g, --generate           Generate default config.json\n");
    printf("  -i, --info               Show program info\n");
    printf("  -h, --help               Show this help message\n");
    printf("\nExamples:\n");
    printf("  %s -m                Show mountpoint list\n", progname);
    printf("  %s -m -r             Show mountpoint list in raw format\n", progname);
    printf("  %s -d 1004,1012      Start stream, filter for types 1004 and 1012\n", progname);
    printf("  %s -s 120            Analyze satellites for 120 seconds\n", progname);
    printf("\n");
}

void print_verbose_config(
    const NTRIP_Config *config,
    const char *config_filename,
    Operation operation
) {
    // Build dynamic border
    char conf_border[64];
    int conf_pos = 0;
    conf_border[conf_pos++] = '+';
    for (int i = 0; i < CONF_KEY_WIDTH+2; ++i) conf_border[conf_pos++] = '-';
    conf_border[conf_pos++] = '+';
    for (int i = 0; i < CONF_VAL_WIDTH+2; ++i) conf_border[conf_pos++] = '-';
    conf_border[conf_pos++] = '+';
    conf_border[conf_pos++] = '\0';

    printf("%s\n", conf_border);
    printf("| %-*s | %-*s |\n", CONF_KEY_WIDTH, "NTRIP-Analyser", CONF_VAL_WIDTH, "Configuration");
    printf("%s\n", conf_border);
    printf("| %-*s | %-*s |\n", CONF_KEY_WIDTH, "Config file", CONF_VAL_WIDTH, config_filename);
    printf("| %-*s | %-*s |\n", CONF_KEY_WIDTH, "NTRIP_CASTER", CONF_VAL_WIDTH, config->NTRIP_CASTER);
    printf("| %-*s | %-*d |\n", CONF_KEY_WIDTH, "NTRIP_PORT", CONF_VAL_WIDTH, config->NTRIP_PORT);
    printf("| %-*s | %-*s |\n", CONF_KEY_WIDTH, "MOUNTPOINT", CONF_VAL_WIDTH, config->MOUNTPOINT);
    printf("| %-*s | %-*s |\n", CONF_KEY_WIDTH, "USERNAME", CONF_VAL_WIDTH, config->USERNAME);
    printf("| %-*s | %-*s |\n", CONF_KEY_WIDTH, "PASSWORD", CONF_VAL_WIDTH, config->PASSWORD);
    printf("| %-*s | %-*.8f |\n", CONF_KEY_WIDTH, "LATITUDE", CONF_VAL_WIDTH, config->LATITUDE);
    printf("| %-*s | %-*.8f |\n", CONF_KEY_WIDTH, "LONGITUDE", CONF_VAL_WIDTH, config->LONGITUDE);
    printf("%s\n", conf_border);

    printf("[INFO] Action: ");
    switch (operation) {
        case OP_ANALYZE_TYPES:
            printf("Analyze message types\n");
            break;
        case OP_ANALYZE_SATS:
            printf("Analyze unique satellites\n");
            break;
        case OP_SHOW_MOUNT_RAW:
            printf("Show mountpoint list in raw format\n");
            break;
        case OP_SHOW_MOUNT_FORMATTED:
            printf("Show mountpoint list\n");
            break;
        case OP_DECODE_STREAM:
            printf("Start NTRIP stream\n");
            break;
        default:
            printf("No action specified\n");
    }
    printf("%s\n", conf_border);
}