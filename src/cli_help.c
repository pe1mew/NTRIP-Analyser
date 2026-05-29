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
    printf("  -S, --sky                Sky-heatmap mode: collect obs + ephemerides until\n");
    printf("                           Ctrl-C, then save YYYYMMDDHHmmss_ARP-EPG.png.\n");
    printf("                           Requires either an EPH_CASTER block in the config\n");
    printf("                           file, or -R / --RINEX.\n");
    printf("  -R, --RINEX <file>       RINEX 3 NAV file to preload ephemerides from before\n");
    printf("                           the live EPH stream takes over (use with -S/--sky).\n");
    printf("      --duration <sec>     Auto-stop --sky mode after N seconds and save normally.\n");
    printf("                           Useful for unattended / cron usage.\n");
    printf("  -o, --output <path>      Write the --sky PNG to this path instead of the default\n");
    printf("                           timestamped name (overwrites if it exists).\n");
    printf("      --no-progress        Never print the per-second status line in --sky mode.\n");
    printf("                           (Useful when -q is not enough.)\n");
    printf("      --json               Emit per-tick status as one JSON object per line on\n");
    printf("                           stderr instead of the human-readable line; final\n");
    printf("                           {\"event\":\"stop\",\"reason\":...,\"saved\":...} on exit.\n");
    printf("      --rtcm-stdin         Read obs RTCM bytes from stdin instead of opening\n");
    printf("                           the NTRIP socket.  Auto-stops at EOF.  Lets you do\n");
    printf("                           offline replay:  --sky --rtcm-stdin -R nav.rnx <cap.rtcm3\n");
    printf("  -q, --quiet              Suppress informational chatter.  Errors still go to\n");
    printf("                           stderr; the saved PNG path is still printed to stdout.\n");
    printf("  -v, --verbose            Verbose output (overrides decoder mute in --sky mode).\n");
    printf("\nConfig field overrides (precedence: CLI > env var > config file):\n");
    printf("      --caster <host>      Override NTRIP_CASTER         (env: NTRIP_CASTER)\n");
    printf("      --port <num>         Override NTRIP_PORT           (env: NTRIP_PORT)\n");
    printf("      --mountpoint <name>  Override MOUNTPOINT           (env: NTRIP_MOUNTPOINT)\n");
    printf("      --user <name>        Override USERNAME             (env: NTRIP_USERNAME)\n");
    printf("      --password <pwd>     Override PASSWORD             (env: NTRIP_PASSWORD)\n");
    printf("      --eph-caster <host>  Override EPH_CASTER           (env: NTRIP_EPH_CASTER)\n");
    printf("      --eph-port <num>     Override EPH_PORT             (env: NTRIP_EPH_PORT)\n");
    printf("      --eph-mountpoint <name>\n");
    printf("                           Override EPH_MOUNTPOINT       (env: NTRIP_EPH_MOUNTPOINT)\n");
    printf("      --eph-user <name>    Override EPH_USERNAME         (env: NTRIP_EPH_USERNAME)\n");
    printf("      --eph-password <pwd> Override EPH_PASSWORD         (env: NTRIP_EPH_PASSWORD)\n");
    printf("      --check-config       Validate config (apply overrides + env, DNS-resolve\n");
    printf("                           casters), print resolved values, then exit.  Useful\n");
    printf("                           as a CI fail-fast check.\n");
    printf("\nOther:\n");
    printf("  -g, --generate           Generate default config.json\n");
    printf("  -i, --info               Show program info\n");
    printf("      --version            Print version (ntrip-analyser X.Y.Z) and exit\n");
    printf("  -h, --help               Show this help message\n");
    printf("\nExamples:\n");
    printf("  %s -m                            Show mountpoint list\n", progname);
    printf("  %s -m -r                         Show mountpoint list in raw format\n", progname);
    printf("  %s -d 1004,1012                  Start stream, filter for types 1004 and 1012\n", progname);
    printf("  %s -s 120                        Analyze satellites for 120 seconds\n", progname);
    printf("  %s -S                            Collect sky-heatmap, save PNG on Ctrl-C\n", progname);
    printf("  %s -S -R brdc.rnx                ... with a RINEX preload\n", progname);
    printf("  %s -S -R brdc.rnx -v             ... and verbose tick / EPH logging\n", progname);
    printf("  %s -S --duration 300 -o sky.png -q\n", progname);
    printf("                                   5-min unattended capture; script-friendly.\n");
    printf("                                   Stdout will contain only 'sky.png'.\n");
    printf("  %s --check-config                Dry-run config validation (DNS, fields).\n", progname);
    printf("  %s --caster a.b.c -m -q          List mountpoints from a one-off caster.\n", progname);
    printf("  NTRIP_PASSWORD=$SECRET %s -m     Credentials via env, no config file edit.\n", progname);
    printf("\nKey bindings in --sky mode:\n");
    printf("  Ctrl-C                   Stop collection, save PNG, exit (0).\n");
    printf("  Ctrl-A                   Abort immediately, do NOT save PNG, exit (5).\n");
    printf("\nExit codes:\n");
    printf("  0   Success\n");
    printf("  1   Generic / runtime / connect failure\n");
    printf("  2   Bad command-line arguments\n");
    printf("  3   Could not open or parse config file\n");
    printf("  4   --sky pre-flight: no ephemeris source configured\n");
    printf("  5   Aborted by user (Ctrl-A)\n");
    printf("\n");
}

void print_verbose_config(
    const NTRIP_Config *config,
    const char *config_filename,
    Operation operation
) {
    /* Verbose config dump is informational, so it writes to stderr --
     * keeps stdout reserved for the actual data of the chosen action
     * (sourcetable, decoded RTCM, sky-PNG path, etc.). */

    /* Dynamic border that fits CONF_KEY_WIDTH + CONF_VAL_WIDTH chars. */
    char conf_border[80];
    int conf_pos = 0;
    conf_border[conf_pos++] = '+';
    for (int i = 0; i < CONF_KEY_WIDTH + 2; ++i) conf_border[conf_pos++] = '-';
    conf_border[conf_pos++] = '+';
    for (int i = 0; i < CONF_VAL_WIDTH + 2; ++i) conf_border[conf_pos++] = '-';
    conf_border[conf_pos++] = '+';
    conf_border[conf_pos++] = '\0';

    /* Mask password fields the same way --check-config does. */
    const char *pwd_obs = config->PASSWORD[0]     ? "(set)" : "(empty)";
    const char *pwd_eph = config->EPH_PASSWORD[0] ? "(set)" : "(empty)";

    bool have_eph = config->EPH_CASTER[0]
                 && config->EPH_PORT > 0
                 && config->EPH_MOUNTPOINT[0];

    fprintf(stderr, "%s\n", conf_border);
    fprintf(stderr, "| %-*s | %-*s |\n",
            CONF_KEY_WIDTH, "NTRIP-Analyser",
            CONF_VAL_WIDTH, "Configuration");
    fprintf(stderr, "%s\n", conf_border);
    fprintf(stderr, "| %-*s | %-*s |\n",
            CONF_KEY_WIDTH, "Config file",
            CONF_VAL_WIDTH, config_filename);
    fprintf(stderr, "%s\n", conf_border);

    /* ── Observation stream block ── */
    fprintf(stderr, "| %-*s | %-*s |\n",
            CONF_KEY_WIDTH, "[Obs stream]", CONF_VAL_WIDTH, "");
    fprintf(stderr, "| %-*s | %-*s |\n",
            CONF_KEY_WIDTH, "NTRIP_CASTER",
            CONF_VAL_WIDTH, config->NTRIP_CASTER);
    fprintf(stderr, "| %-*s | %-*d |\n",
            CONF_KEY_WIDTH, "NTRIP_PORT",
            CONF_VAL_WIDTH, config->NTRIP_PORT);
    fprintf(stderr, "| %-*s | %-*s |\n",
            CONF_KEY_WIDTH, "MOUNTPOINT",
            CONF_VAL_WIDTH, config->MOUNTPOINT);
    fprintf(stderr, "| %-*s | %-*s |\n",
            CONF_KEY_WIDTH, "USERNAME",
            CONF_VAL_WIDTH, config->USERNAME);
    fprintf(stderr, "| %-*s | %-*s |\n",
            CONF_KEY_WIDTH, "PASSWORD",
            CONF_VAL_WIDTH, pwd_obs);
    fprintf(stderr, "| %-*s | %-*.8f |\n",
            CONF_KEY_WIDTH, "LATITUDE",
            CONF_VAL_WIDTH, config->LATITUDE);
    fprintf(stderr, "| %-*s | %-*.8f |\n",
            CONF_KEY_WIDTH, "LONGITUDE",
            CONF_VAL_WIDTH, config->LONGITUDE);

    /* ── Ephemeris stream block (always shown for completeness; an
     * unconfigured EPH stream lands in the rows as (none) / 0). ── */
    fprintf(stderr, "%s\n", conf_border);
    fprintf(stderr, "| %-*s | %-*s |\n",
            CONF_KEY_WIDTH, "[Eph stream]",
            CONF_VAL_WIDTH, have_eph ? "configured" : "(not configured)");
    fprintf(stderr, "| %-*s | %-*s |\n",
            CONF_KEY_WIDTH, "EPH_CASTER",
            CONF_VAL_WIDTH, config->EPH_CASTER[0] ? config->EPH_CASTER : "(none)");
    fprintf(stderr, "| %-*s | %-*d |\n",
            CONF_KEY_WIDTH, "EPH_PORT",
            CONF_VAL_WIDTH, config->EPH_PORT);
    fprintf(stderr, "| %-*s | %-*s |\n",
            CONF_KEY_WIDTH, "EPH_MOUNTPOINT",
            CONF_VAL_WIDTH, config->EPH_MOUNTPOINT[0] ? config->EPH_MOUNTPOINT : "(none)");
    fprintf(stderr, "| %-*s | %-*s |\n",
            CONF_KEY_WIDTH, "EPH_USERNAME",
            CONF_VAL_WIDTH, config->EPH_USERNAME);
    fprintf(stderr, "| %-*s | %-*s |\n",
            CONF_KEY_WIDTH, "EPH_PASSWORD",
            CONF_VAL_WIDTH, pwd_eph);
    fprintf(stderr, "%s\n", conf_border);

    fprintf(stderr, "[INFO] Action: ");
    switch (operation) {
        case OP_ANALYZE_TYPES:
            fprintf(stderr, "Analyze message types\n");
            break;
        case OP_ANALYZE_SATS:
            fprintf(stderr, "Analyze unique satellites\n");
            break;
        case OP_SHOW_MOUNT_RAW:
            fprintf(stderr, "Show mountpoint list in raw format\n");
            break;
        case OP_SHOW_MOUNT_FORMATTED:
            fprintf(stderr, "Show mountpoint list\n");
            break;
        case OP_DECODE_STREAM:
            fprintf(stderr, "Start NTRIP stream\n");
            break;
        case OP_SKY_HEATMAP:
            fprintf(stderr, "Sky-heatmap collection (Ctrl-C to save PNG)\n");
            break;
        default:
            fprintf(stderr, "No action specified\n");
    }
    fprintf(stderr, "%s\n", conf_border);
}