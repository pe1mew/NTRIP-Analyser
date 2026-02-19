/**
 * @file rtcm3x_parser.h
 * @brief API for RTCM 3.x Stream Analyzer and MSM4/MSM7 Decoding
 *
 * This header defines the API for parsing, analyzing, and decoding RTCM 3.x messages,
 * including MSM4 and MSM7 message types for multiple GNSS constellations (GPS, GLONASS, Galileo, QZSS, BeiDou, SBAS).
 * It provides utilities for bit extraction and CRC-24Q calculation.
 *
 * Project: NTRIP RTCM 3.x Stream Analyzer
 * @author Remko Welling, PE1MEW
 * @license Apache License 2.0 with Commons Clause (see LICENSE for details)
 *
 * ## Supported message types:
 *   - 1005: Stationary RTK Reference Station ARP (verified)
 *   - 1006: Stationary RTK Reference Station ARP with Height (verified)
 *   - 1007: Antenna Descriptor
 *   - 1008: Antenna Descriptor & Serial Number
 *   - 1012: GLONASS L1&L2 RTK Observables
 *   - 1013: System Parameters (GLONASS synchronization and message schedule)
 *   - 1019: GPS Ephemeris Data
 *   - *1020: GLONASS L1&L2 RTK Observables*
 *   - 1033: Receiver & Antenna Descriptor
 *   - *1042: GPS Code-Phase Biases*
 *   - *1044: GLONASS Code-Phase Biases*
 *   - 1045: SSR Messages
 *   - *1046: Galileo Code-Phase Biases*
 *   - 1074: MSM4 GPS
 *   - 1077: MSM7 GPS
 *   - 1084: MSM4 GLONASS
 *   - 1087: MSM7 GLONASS
 *   - 1094: MSM4 Galileo
 *   - 1097: MSM7 Galileo
 *   - *1107: SSR Orbit Correction, QZSS*
 *   - 1117: MSM7 QZSS
 *   - 1124: MSM4 QZSS
 *   - 1127: MSM7 BeiDou
 *   - 1137: MSM7 SBAS
 *   - 1230: GLONASS Code-Phase Biases
 *
 * ## Usage:
 *   - Use @ref analyze_rtcm_message to process a raw RTCM message buffer.
 *   - Use the decode_rtcm_xxxx() functions for message-specific decoding.
 *   - Use @ref get_bits for bitfield extraction and @ref crc24q for CRC checking.
 *
 * For more information, see the project README and LICENSE files.
 * 
 * @todo add scaling of extracted values to real-world units (e.g., meters, seconds).
 * @todo implement additional message types as needed: 1007,1020, 1042, 1044, 1046, 1107 
 */

#ifndef RTCM3X_PARSER_H
#define RTCM3X_PARSER_H

#include <stdint.h>
#include <stdbool.h>
#include "ntrip_handler.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Growable string buffer for capturing decode output.
 *
 * When passed to rtcm_set_output_buffer(), all decode_rtcm_*() printf
 * output is redirected into this buffer instead of stdout.
 */
typedef struct {
    char *buf;
    int   len;
    int   cap;
} RtcmStrBuf;

/** Initialise a string buffer with the given capacity. */
void rtcm_strbuf_init(RtcmStrBuf *sb, int initial_cap);

/** Free the string buffer memory. */
void rtcm_strbuf_free(RtcmStrBuf *sb);

/** Reset length to 0 (keeps allocated memory). */
void rtcm_strbuf_clear(RtcmStrBuf *sb);

/**
 * @brief Redirect all decode output to a string buffer.
 *
 * Pass NULL to restore output to stdout (the default).
 * Only the UI thread should call this; the worker thread always
 * calls analyze_rtcm_message() with suppress_output=true and
 * therefore never touches the buffer.
 */
void rtcm_set_output_buffer(RtcmStrBuf *sb);

/**
 * @brief Convert ECEF coordinates to geodetic (WGS84) latitude, longitude, altitude.
 * @param x ECEF X (meters)
 * @param y ECEF Y (meters)
 * @param z ECEF Z (meters)
 * @param h Antenna height (meters, optional, can be 0)
 * @param lat_deg [out] Latitude in degrees
 * @param lon_deg [out] Longitude in degrees
 * @param alt [out] Altitude in meters
 */
void ecef_to_geodetic(double x, double y, double z, double h, double *lat_deg, double *lon_deg, double *alt);

/**
 * @brief Extract bits from a buffer (big-endian, MSB first).
 *
 * Extracts a bitfield of length @p bit_len starting at @p start_bit from the buffer @p buf.
 *
 * @param buf        Pointer to the buffer.
 * @param start_bit  Start bit index (0 = first bit of buf[0]).
 * @param bit_len    Number of bits to extract.
 * @return Extracted bits as an unsigned 64-bit integer.
 */
uint64_t get_bits(const unsigned char *buf, int start_bit, int bit_len);

/**
 * @brief Calculate CRC-24Q for the given data.
 *
 * Computes the CRC-24Q checksum for the given data buffer.
 *
 * @param data   Pointer to input data buffer.
 * @param length Number of bytes in the buffer.
 * @return 24-bit CRC as uint32_t (lower 24 bits are valid).
 */
uint32_t crc24q(const uint8_t *data, size_t length);

/**
 * @brief Extract a signed 38-bit integer from a buffer.
 * 
 * Extracts a 38-bit signed integer value from the buffer and properly handles
 * two's complement negative numbers.
 *
 * @param buf        Pointer to the buffer.
 * @param start_bit  Start bit index (0 = first bit of buf[0]).
 * @return Extracted signed 38-bit integer as int64_t.
 */
int64_t extract_signed38(const unsigned char *buf, int start_bit);

/**
 * @brief Extract a signed N-bit integer from a buffer.
 * 
 * Generic function to extract signed integers of any bit length from the buffer,
 * with automatic sign extension for negative values.
 *
 * @param buf        Pointer to the buffer.
 * @param start_bit  Start bit index (0 = first bit of buf[0]).
 * @param bit_len    Number of bits to extract.
 * @return Extracted signed integer as int64_t.
 */
int64_t extract_signed(const unsigned char *buf, int start_bit, int bit_len);

/**
 * @brief Calculate the great-circle distance and heading between two WGS84 coordinates.
 *
 * Uses the Haversine formula to compute the shortest distance over the Earth's surface
 * between two latitude/longitude points, and computes the initial heading (bearing) from
 * the first point to the second.
 *
 * @param lat1        Latitude of the first point (degrees, WGS84)
 * @param lon1        Longitude of the first point (degrees, WGS84)
 * @param lat2        Latitude of the second point (degrees, WGS84)
 * @param lon2        Longitude of the second point (degrees, WGS84)
 * @param distance_km [out] Pointer to double to receive the distance in kilometers (may be NULL)
 * @param heading_deg [out] Pointer to double to receive the heading in degrees (may be NULL)
 *
 * @note The heading is the initial bearing from point 1 to point 2, in degrees clockwise from North.
 */
void calc_distance_heading(double lat1, double lon1, double lat2, double lon2, double *distance_km, double *heading_deg);

/**
 * @brief Analyze and print information about an RTCM message.
 *
 * Parses the provided RTCM message buffer, verifies its CRC, and decodes the message
 * if it is a supported type. Prints summary and decoding information to stdout unless
 * suppress_output is true.
 *
 * @param data            Pointer to the RTCM message buffer (should start with 0xD3 preamble).
 * @param length          Length of the buffer in bytes.
 * @param suppress_output If true, do not print any output.
 * @param config          Pointer to NTRIP_Config with rover coordinates for distance/heading calculations.
 * @return The RTCM message type as an integer if successfully parsed, or -1 on error or if not an RTCM message.
 */
int analyze_rtcm_message(const unsigned char *data, int length, bool suppress_output, const NTRIP_Config *config);

/**
 * @brief Decode and print the contents of an RTCM 3.x Type 1005 message (Stationary RTK Reference Station ARP).
 * 
 * Provides the Antenna Reference Point (ARP) position in ECEF coordinates for a stationary reference station.
 * Contains:
 * - Station ID
 * - ECEF X, Y, Z coordinates (high precision)
 * - Station type indicators
 * 
 * If rover coordinates are provided, also calculates distance and heading to the base station.
 * 
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 * @param config      Pointer to NTRIP_Config with rover coordinates for distance/heading calculations.
 */
void decode_rtcm_1005(const unsigned char *payload, int payload_len, const NTRIP_Config *config);

/**
 * @brief Decode and print the contents of an RTCM 3.x Type 1006 message (Stationary RTK Reference Station ARP with Height).
 * 
 * Extends Type 1005 with antenna height information.
 * Contains:
 * - Station ID  
 * - ECEF X, Y, Z coordinates (high precision)
 * - Station type indicators
 * - Antenna height above ARP marker
 * 
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 * @param config      Pointer to NTRIP_Config with rover coordinates for distance/heading calculations.
 */
void decode_rtcm_1006(const unsigned char *payload, int payload_len, const NTRIP_Config *config);

/**
 * @brief Decode RTCM 1019 message (GPS Ephemeris)
 *
 * This function decodes the RTCM 1019 message, which contains ephemeris data for GPS satellites.
 * The ephemeris data includes satellite position, velocity, and clock bias information,
 * which are essential for precise satellite navigation and timing.
 *
 * @param payload Pointer to the RTCM message payload
 * @param payload_len Length of the payload in bytes
 */
void decode_rtcm_1019(const unsigned char *payload, int payload_len);

/**
 * @brief Decode and print the contents of an RTCM 3.x Type 1077 message (MSM7 GPS).
 * 
 * MSM7 (Multiple Signal Message, Type 7) provides the highest resolution
 * GPS observations across multiple signal types (L1, L2, L5).
 * 
 * Displays:
 * - Reference Station ID and GPS epoch time (milliseconds)
 * - Multiple message flag, IODS, clock steering, external clock
 * - Divergence-free smoothing flag and smoothing interval
 * - Number of satellites, signals, and active cells
 * - Per-satellite data: rough range, extended info, phase-range rate
 * - Per-signal data: fine pseudorange, carrier phase, lock time, 
 *   half-cycle ambiguity, CNR (carrier-to-noise ratio), fine Doppler
 * 
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1077(const unsigned char *payload, int payload_len);

/**
 * @brief Decode and print the contents of an RTCM 3.x Type 1084 message (MSM4 GPS).
 *
 * Decodes and prints summary information for RTCM 1084 MSM4 (GPS) messages,
 * including satellite and signal masks, and the first few cell data.
 *
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1084(const unsigned char *payload, int payload_len);

/**
 * @brief Decode and print the contents of an RTCM 3.x Type 1087 message (MSM7 GLONASS).
 * 
 * MSM7 for GLONASS constellation providing full precision multi-signal observations
 * on L1 and L2 frequencies. GLONASS uses FDMA (Frequency Division Multiple Access),
 * so each satellite transmits on a different frequency channel.
 * 
 * Displays the same comprehensive MSM7 header and observation data as Type 1077,
 * but for GLONASS satellites (R01-R24).
 * 
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1087(const unsigned char *payload, int payload_len);

/**
 * @brief Decode and print the contents of an RTCM 3.x Type 1094 message (MSM4 GLONASS).
 *
 * Decodes and prints summary information for RTCM 1094 MSM4 (GLONASS) messages,
 * including satellite and signal masks, and the first few cell data.
 *
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1094(const unsigned char *payload, int payload_len);

/**
 * @brief Decode and print the contents of an RTCM 3.x Type 1097 message (MSM7 Galileo).
 * 
 * MSM7 for Galileo constellation (European GNSS) providing full precision observations
 * across E1, E5a, E5b, and E6 signals.
 * 
 * Displays the same comprehensive MSM7 header and observation data as Type 1077,
 * but for Galileo satellites (E01-E36).
 * 
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1097(const unsigned char *payload, int payload_len);

/**
 * @brief Decode and print the contents of an RTCM 3.x Type 1117 message (MSM7 QZSS).
 * 
 * MSM7 for the Quasi-Zenith Satellite System (QZSS - Japanese regional navigation system)
 * providing full precision multi-signal observations. QZSS uses GPS-compatible signals
 * and provides enhanced coverage over the Asia-Pacific region.
 * 
 * Displays the same comprehensive MSM7 header and observation data as Type 1077,
 * but for QZSS satellites (J01-J07).
 * 
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1117(const unsigned char *payload, int payload_len);

/**
 * @brief Decode and print the contents of an RTCM 3.x Type 1124 message (MSM4 QZSS).
 *
 * Decodes and prints summary information for RTCM 1124 MSM4 (QZSS) messages,
 * including satellite and signal masks, and the first few cell data.
 *
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1124(const unsigned char *payload, int payload_len);

/**
 * @brief Decode and print the contents of an RTCM 3.x Type 1127 message (MSM7 BeiDou).
 * 
 * MSM7 for BeiDou (BDS - Chinese navigation satellite system) constellation providing
 * full precision observations across B1, B2, and B3 signals.
 * 
 * Displays the same comprehensive MSM7 header and observation data as Type 1077,
 * but for BeiDou satellites (C01-C37).
 * 
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1127(const unsigned char *payload, int payload_len);

/**
 * @brief Decode and print the contents of an RTCM 3.x Type 1137 message (MSM7 SBAS).
 * 
 * MSM7 for Satellite-Based Augmentation Systems (SBAS) including WAAS (North America),
 * EGNOS (Europe), MSAS (Japan), and GAGAN (India). These provide atmospheric corrections
 * and integrity information.
 * 
 * Displays the same comprehensive MSM7 header and observation data as Type 1077,
 * but for SBAS satellites (S20-S58).
 * 
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1137(const unsigned char *payload, int payload_len);

/**
 * @brief Decode and print the contents of an RTCM 3.x Type 1007 message (Antenna Descriptor).
 * 
 * Provides antenna description and setup ID for the reference station.
 * Contains:
 * - Station ID
 * - Antenna descriptor (variable length string)
 * - Antenna setup ID
 * 
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1007(const unsigned char *payload, int payload_len);

/**
 * @brief Decode and print the contents of an RTCM 3.x Type 1008 message (Antenna Descriptor & Serial Number).
 * 
 * Extends Type 1007 with antenna serial number information.
 * Contains:
 * - Station ID
 * - Antenna descriptor (variable length string)
 * - Antenna setup ID
 * - Antenna serial number (variable length string)
 * 
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1008(const unsigned char *payload, int payload_len);

/**
 * @brief Decode and print the contents of an RTCM 3.x Type 1013 message (System Parameters).
 * 
 * This message provides system-level timing and synchronization parameters, including:
 * - Station ID
 * - Modified Julian Day (MJD) number for date reference
 * - Seconds of day for precise time-of-day
 * - Leap seconds (GPS/UTC offset)
 * - Synchronized message schedule (message types, sync flags, and transmission intervals)
 * 
 * Used primarily for GLONASS system synchronization in network RTK applications.
 * 
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1013(const unsigned char *payload, int payload_len);

/**
 * @brief Decode and print the contents of an RTCM 3.x Type 1033 message (Receiver & Antenna Descriptor).
 * 
 * Comprehensive station description including both receiver and antenna information.
 * Contains:
 * - Station ID
 * - Antenna descriptor and serial number
 * - Antenna setup ID
 * - Receiver descriptor, firmware version, and serial number
 * 
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1033(const unsigned char *payload, int payload_len);

/**
 * @brief Decode and print the contents of an RTCM 3.x Type 1045 message (Galileo Ephemeris).
 * 
 * Provides Galileo satellite ephemeris data including orbital parameters,
 * satellite health status, and clock correction information.
 * 
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1045(const unsigned char *payload, int payload_len);

/**
 * @brief Decode and print the contents of an RTCM 3.x Type 1230 message (GLONASS Code-Phase Biases).
 * 
 * Provides GLONASS satellite-specific code-phase bias information for
 * improved pseudorange and carrier-phase measurements.
 * Contains:
 * - Station ID
 * - Bias indicator flags
 * - L1 and L2 code-phase biases for each satellite
 * 
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1230(const unsigned char *payload, int payload_len);

/**
 * @brief Decode and print the contents of an RTCM 3.x Type 1012 message (GLONASS L1&L2 RTK Observables).
 * 
 * Contains GLONASS satellite observations on L1 and L2 frequencies.
 * Includes:
 * - Station ID and epoch time
 * - Number of satellites
 * - Pseudorange, carrier phase, and signal strength for each satellite
 * - Lock time indicators
 * 
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1012(const unsigned char *payload, int payload_len);

/**
 * @brief Decode RTCM 1074 MSM4 (GPS) message.
 *
 * Decodes and prints summary information for RTCM 1074 MSM4 (GPS) messages.
 *
 * @param payload Pointer to the RTCM message payload (after header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1074(const unsigned char *payload, int payload_len);

/**
 * @brief Generic decoder for RTCM MSM4 messages (shared for GPS, GLONASS, Galileo, QZSS, etc.).
 *
 * Decodes and displays MSM4 (Multiple Signal Message, Type 4) observations which provide
 * medium-resolution GNSS observations. MSM4 contains pseudorange, phase range, lock time,
 * half-cycle ambiguity, and CNR data with reduced bit precision compared to MSM7.
 * 
 * This generic function handles all GNSS constellations by using constellation-specific
 * scaling parameters passed as arguments.
 *
 * @param payload     Pointer to the message payload (after RTCM header).
 * @param payload_len Length of the payload in bytes.
 * @param gnss_name   String for GNSS name (e.g., "GPS", "GLONASS", "QZSS") for display.
 * @param msg_type    RTCM message type number (e.g., 1074, 1084, 1094, 1124) for display.
 * @param pr_bits     Number of bits for pseudorange field (e.g., 15 for GPS, 20 for QZSS).
 * @param ph_bits     Number of bits for phase range field (e.g., 22 for GPS, 24 for QZSS).
 * @param pr_scale    Scaling factor for pseudorange in meters (e.g., 0.02 for GPS, 0.1 for QZSS).
 * @param ph_scale    Scaling factor for phase range in meters (typically 0.0005 for all).
 * 
 * @note Only the first 5 cells are displayed to prevent excessive output. 
 *       Use suppress_output mode if you only need the message type.
 */
void decode_rtcm_msm4_generic(const unsigned char *payload, int payload_len,
                              const char *gnss_name, int msg_type,
                              int pr_bits, int ph_bits, double pr_scale, double ph_scale);

#ifdef __cplusplus
}
#endif

#endif // RTCM3X_PARSER_H