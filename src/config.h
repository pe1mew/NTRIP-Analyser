/**
 * @file config.h
 * @brief Configuration loader for NTRIP RTCM 3.x Stream Analyzer
 *
 * Provides functions to load NTRIP configuration from a JSON file into an NTRIP_Config struct.
 *
 * Project: NTRIP RTCM 3.x Stream Analyzer
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause (see LICENSE for details)
 */

#ifndef CONFIG_H
#define CONFIG_H

#include "ntrip_handler.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Load NTRIP configuration from a JSON file.
 *
 * Reads the specified JSON file and fills the provided NTRIP_Config structure
 * with the parsed configuration values.
 *
 * @param filename Path to the JSON configuration file.
 * @param config Pointer to an NTRIP_Config struct to be filled.
 * @return 0 on success, -1 on error.
 */
int load_config(const char *filename, NTRIP_Config *config);

#ifdef __cplusplus
}
#endif

#endif // CONFIG_H