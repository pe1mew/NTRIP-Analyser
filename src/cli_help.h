/**
 * @file cli_help.h
 * @brief CLI help and program information functions for NTRIP-Analyser.
 *
 * Provides functions to print program information and usage help to the user.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling
 * @license Apache License 2.0 with Commons Clause (see LICENSE for details)
 */

#ifndef CLI_HELP_H
#define CLI_HELP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "config.h"
#include <stdbool.h>

/**
 * @enum Operation
 * @brief Enumeration of available operations for the NTRIP Analyser application.
 * 
 * Defines the various operational modes that the application can perform,
 * selected via command-line arguments.
 */
typedef enum {
    OP_NONE,                   /**< No operation selected (default/error state) */
    OP_ANALYZE_TYPES,          /**< Analyze and count RTCM message types in stream */
    OP_ANALYZE_SATS,           /**< Analyze satellite visibility and statistics */
    OP_SHOW_MOUNT_RAW,         /**< Display raw mountpoint source table from caster */
    OP_SHOW_MOUNT_FORMATTED,   /**< Display formatted mountpoint table from caster */
    OP_DECODE_STREAM           /**< Decode and display detailed RTCM message contents */
} Operation;

/**
 * @brief Print program information (name, author, license, repository).
 */
void print_program_info(void);

/**
 * @brief Print usage help for the command-line interface.
 * @param progname The name of the executable (typically argv[0]).
 */
void print_help(const char *progname);

/**
 * @brief Print the configuration settings in a verbose format.
 *
 * This function outputs the current NTRIP configuration settings to the
 * console in a human-readable format, including details about the mount
 * point table and stream decoding options.
 *
 * @param config A pointer to the NTRIP_Config structure containing the
 *               current configuration settings.
 * @param config_filename The name of the configuration file being used.
 * @param operation The operation mode for the NTRIP server (e.g., server,
 *                  client, or relay).
 */
void print_verbose_config(
    const NTRIP_Config *config,
    const char *config_filename,
    Operation operation
);

#ifdef __cplusplus
}
#endif

#endif // CLI_HELP_H