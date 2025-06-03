#include <stdio.h>
#include "cli_help.h"

void print_program_info(void) {
    printf(
        "NTRIP-Analyser - RTCM 3.x Stream Analyzer and NTRIP Client\n"
        "This program connects to NTRIP casters, retrieves mountpoint tables, and decodes or analyzes RTCM 3.x streams.\n"
        "For usage instructions, run with -h or --help.\n"
        "\n"
        "Author: Remko Welling, PE1MEW\n"
        "License: Apache License 2.0 with Commons Clause\n"
        "GitHub: https://github.com/pe1mew/NTRIP-Analyser\n"
    );
}

void print_help(const char *progname) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "Options:\n"
        "  -c, --config [file]      Specify config file (default: config.json)\n"
        "  -t, --time [seconds]     Analyze message types for N seconds (default: 60)\n"
        "  -m, --mounts             Show mountpoint (sourcetable) list and exit\n"
        "  -d, --decode [types]     Decode RTCM stream, optionally filter by comma-separated RTCM-message numbers (e.g. 1005,1074)\n"
        "  -v, --verbose            Print configuration and action details before running\n"
        "  -g, --generate           Create a template config.json and exit\n"
        "  -i, --info               Print program information and exit\n"
        "      --latitude [value]   Override latitude in json config-file\n"
        "      --lat [value]        Same as --latitude\n"
        "      --longitude [value]  Override longitude in json config-file\n"
        "      --lon [value]        Same as --longitude\n"
        "  -h, --help               Show this help message and exit\n"
        "\n"
        "Examples:\n"
        "  %s -m\n"
        "  %s -d 1005,1074\n"
        "  %s -c myconfig.json -d\n"
        "  %s -g\n"
        "  %s -i\n",
        progname, progname, progname, progname, progname, progname
    );
}