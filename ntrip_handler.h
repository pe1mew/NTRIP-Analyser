/**
 * @file ntrip_handler.h
 * @brief NTRIP client handler API for NTRIP RTCM 3.x Stream Analyzer
 *
 * This header defines the NTRIP_Config structure and declares functions for:
 *   - Base64 encoding for HTTP Basic Authentication
 *   - Receiving the NTRIP mountpoint table from a caster
 *   - Starting and handling an NTRIP RTCM 3.x data stream
 *
 * Project: NTRIP RTCM 3.x Stream Analyzer
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause (see LICENSE for details)
 *
 * Usage:
 *   - Fill an NTRIP_Config struct with connection and authentication details.
 *   - Use base64_encode() to prepare the AUTH_BASIC field.
 *   - Use receive_mount_table() to fetch the sourcetable from the caster.
 *   - Use start_ntrip_stream() to connect and process RTCM data.
 *
 * For more information, see the project README and LICENSE files.
 */

#ifndef NTRIP_HANDLER_H
#define NTRIP_HANDLER_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct NTRIP_Config
 * @brief Holds configuration and authentication details for an NTRIP connection.
 *
 * This structure contains all necessary fields for connecting to an NTRIP caster,
 * including server address, port, mountpoint, user credentials, and a precomputed
 * Base64-encoded authentication string.
 */
typedef struct {
    char NTRIP_CASTER[256];   /**< Hostname or IP address of the NTRIP caster */
    int  NTRIP_PORT;          /**< TCP port of the NTRIP caster */
    char MOUNTPOINT[256];     /**< Mountpoint string to request from the caster */
    char USERNAME[256];       /**< Username for HTTP Basic Authentication */
    char PASSWORD[256];       /**< Password for HTTP Basic Authentication */
    char AUTH_BASIC[256];     /**< Base64 encoded "username:password" for HTTP Basic Auth */
} NTRIP_Config;

/**
 * @brief Encode a string to Base64.
 *
 * Encodes the input string (typically "username:password") to Base64 for use in HTTP Basic Authentication.
 *
 * @param input  Null-terminated input string.
 * @param output Output buffer for Base64 string (must be large enough).
 */
void base64_encode(const char *input, char *output);

/**
 * @brief Receives the NTRIP mountpoint table (sourcetable) from the caster.
 *
 * Connects to the configured NTRIP caster and retrieves the mountpoint table.
 *
 * @param config Pointer to NTRIP_Config struct with connection details.
 * @return Pointer to the mount table string (must be freed by caller), or NULL on error.
 */
char* receive_mount_table(const NTRIP_Config *config);

/**
 * @brief Starts the NTRIP stream from the configured mountpoint and prints RTCM message types.
 *
 * Connects to the NTRIP caster and mountpoint specified in the config, receives RTCM 3.x data,
 * and processes messages using the RTCM parser.
 *
 * @param config Pointer to NTRIP_Config struct with connection details.
 */
void start_ntrip_stream(const NTRIP_Config *config);

#ifdef __cplusplus
}
#endif

#endif // NTRIP_HANDLER_H