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

/**
 * @brief Print program information (name, author, license, repository).
 */
void print_program_info(void);

/**
 * @brief Print usage help for the command-line interface.
 * @param progname The name of the executable (typically argv[0]).
 */
void print_help(const char *progname);

#ifdef __cplusplus
}
#endif

#endif // CLI_HELP_H