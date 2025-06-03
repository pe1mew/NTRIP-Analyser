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
    char USERNAME[128];       /**< Username for HTTP Basic Authentication */
    char PASSWORD[128];       /**< Password for HTTP Basic Authentication */
    char AUTH_BASIC[256];     /**< Base64 encoded "username:password" for HTTP Basic Auth */
    double LATITUDE;          /**< Latitude for NTRIP connection (optional) */
    double LONGITUDE;         /**< Longitude for NTRIP connection (optional) */
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

/**
 * @brief Starts the NTRIP stream with a filter for specific RTCM message types.
 *
 * Connects to the NTRIP caster and mountpoint specified in the config, receives RTCM 3.x data,
 * and processes messages using the RTCM parser, but only for the message types specified in the filter.
 *
 * @param config Pointer to NTRIP_Config struct with connection details.
 * @param filter_list Array of RTCM message type IDs to filter (e.g., {1005, 1006, 1007}).
 * @param filter_count Number of message types in the filter_list.
 */
void start_ntrip_stream_with_filter(const NTRIP_Config *config, const int *filter_list, int filter_count);

/**
 * @brief Analyze RTCM message types for a given duration and print a summary table.
 *
 * Connects to the NTRIP caster and mountpoint specified in the config, receives RTCM 3.x data,
 * and analyzes the message types received for the specified duration.
 *
 * @param config Pointer to NTRIP_Config struct with connection details.
 * @param analysis_time Duration in seconds to analyze message types.
 */
void analyze_message_types(const NTRIP_Config *config, int analysis_time);

#ifdef __cplusplus
}
#endif

#endif // NTRIP_HANDLER_H