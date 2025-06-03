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
 * @author Remko Welling, PE1MEW
 * @license Apache License 2.0 with Commons Clause (see LICENSE for details)
 *
 * ## Usage:
 *   - Fill an NTRIP_Config struct with connection and authentication details.
 *   - Use base64_encode() to prepare the AUTH_BASIC field.
 *   - Use receive_mount_table() to fetch the sourcetable from the caster.
 *   - Use start_ntrip_stream() or start_ntrip_stream_with_filter() to connect and process RTCM data.
 *
 * For more information, see the project README and LICENSE files.
 */

#include <stddef.h>
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
 *
 * Fields:
 *   - NTRIP_CASTER: Hostname or IP address of the NTRIP caster
 *   - NTRIP_PORT:   TCP port of the NTRIP caster
 *   - MOUNTPOINT:   Mountpoint string to request from the caster
 *   - USERNAME:     Username for HTTP Basic Authentication
 *   - PASSWORD:     Password for HTTP Basic Authentication
 *   - AUTH_BASIC:   Base64 encoded "username:password" for HTTP Basic Auth
 *   - LATITUDE:     Latitude for NTRIP connection (optional)
 *   - LONGITUDE:    Longitude for NTRIP connection (optional)
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
 * @def MAX_GNSS
 * @brief Maximum number of GNSS systems supported in statistics.
 *
 * Used as the size of the GNSS array in SatStatsSummary.
 */
#define MAX_GNSS 8

/**
 * @def MAX_SATS_PER_GNSS
 * @brief Maximum number of satellites per GNSS system supported in statistics.
 *
 * Used as the size of the sat_seen array in GnssSatStats.
 */
#define MAX_SATS_PER_GNSS 64

/**
 * @struct GnssSatStats
 * @brief Holds statistics for satellites seen for a single GNSS constellation.
 *
 * This structure contains statistics for one GNSS system, including which satellites have been seen.
 *
 * Fields:
 *   - gnss_id:   GNSS system ID (1=GPS, 2=GLONASS, 3=Galileo, 4=QZSS, 5=BeiDou, 6=SBAS, etc.)
 *   - sat_seen:  Array indicating which satellites have been seen (1 if seen, 0 if not)
 *   - count:     Number of unique satellites seen for this GNSS
 */
typedef struct {
    int gnss_id;
    int sat_seen[MAX_SATS_PER_GNSS]; ///< 1 if seen, 0 if not
    int count; 
} GnssSatStats;

/**
 * @struct SatStatsSummary
 * @brief Holds summary statistics for all GNSS constellations in the stream.
 *
 * Fields:
 *   - gnss:       Array of GnssSatStats, one per GNSS system
 *   - gnss_count: Number of GNSS systems present in the summary
 */
typedef struct {
    GnssSatStats gnss[MAX_GNSS];
    int gnss_count;
} SatStatsSummary;

/**
 * @brief Encode a string to Base64 for HTTP Basic Authentication.
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

// Returns GNSS system ID from RTCM message type
int get_gnss_id_from_rtcm(int msg_type); // Only the declaration, not the definition

// Extracts satellites from MSM7/RTCM message into summary
void extract_satellites(const unsigned char *data, int len, int msg_type, SatStatsSummary *summary);

// Opens NTRIP stream and analyzes satellites for a period
void analyze_satellites_stream(const NTRIP_Config *config, int analysis_time);

/**
 * @brief Returns the GNSS system name string for a given GNSS ID.
 *
 * @param gnss_id GNSS system ID (1=GPS, 2=GLONASS, 3=Galileo, 4=QZSS, 5=BeiDou, 6=SBAS, etc.)
 * @return Pointer to a static string with the GNSS name (e.g., "GPS", "GLONASS").
 */
const char* gnss_name_from_id(int gnss_id);

/**
 * @brief Formats a RINEX satellite ID for a given GNSS and PRN.
 *
 * Converts the GNSS system ID and PRN number to a RINEX 3 satellite ID string (e.g., "G01", "R02").
 *
 * @param gnss_id GNSS system ID (1=GPS, 2=GLONASS, 3=Galileo, 4=QZSS, 5=BeiDou, 6=SBAS, etc.)
 * @param prn     Satellite number within the constellation (1-based).
 * @param buf     Output buffer for the RINEX ID string.
 * @param buflen  Size of the output buffer.
 * @return Pointer to the output buffer containing the RINEX ID string.
 */
const char* rinex_id_from_gnss(int gnss_id, int prn, char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif // NTRIP_HANDLER_H