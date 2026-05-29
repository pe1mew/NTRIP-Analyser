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
 *   - 1029: Unicode Text String
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
 * @brief Retrieve the most recently decoded reference-station ARP.
 *
 * Returns the ECEF coordinates published in the latest RTCM 1005 or 1006
 * message processed by @ref analyze_rtcm_message, along with the equivalent
 * WGS-84 geodetic position.  Used by the GUI worker thread to compute
 * satellite az/el for the Sky Plot.
 *
 * @param valid   [out] true if a 1005/1006 has been seen this session, may be NULL.
 * @param x,y,z   [out] ECEF metres, may be NULL.
 * @param lat_deg [out] WGS-84 latitude in degrees, may be NULL.
 * @param lon_deg [out] WGS-84 longitude in degrees, may be NULL.
 * @param alt_m   [out] WGS-84 altitude in metres, may be NULL.
 */
void rtcm_get_station_arp(bool *valid,
                          double *x, double *y, double *z,
                          double *lat_deg, double *lon_deg, double *alt_m);

/**
 * @brief Extract the list of satellites tracked in a single MSM4/5/6/7 frame.
 *
 * Reads the 64-bit satellite mask at the correct RTCM 10403.3 offset
 * (bit 73 of the payload) and emits PRN numbers (1-based) for every set
 * bit.  Unlike @ref extract_satellites in ntrip_handler.c, this does
 * not aggregate across frames — each call returns the PRNs visible
 * in this single epoch.
 *
 * @param payload     RTCM payload (starting at message-number bit).
 * @param payload_len Length of the payload in bytes.
 * @param msg_type    RTCM MSM message type (1074..1137).
 * @param prns_out    Output array, will be filled with PRNs.
 * @param max_prns    Capacity of @p prns_out.
 * @param gnss_id_out [out, optional] GNSS ID (1=GPS, 3=Galileo, ...).
 * @return Number of PRNs written to @p prns_out, or 0 on error.
 */
int msm_extract_prns(const unsigned char *payload, int payload_len,
                     int msg_type, int *prns_out, int max_prns,
                     int *gnss_id_out);

/**
 * @brief Extract per-SV best CNR from an MSM7 frame.
 *
 * Walks the per-cell block of an MSM7 payload and emits one (PRN, CNR)
 * pair per tracked satellite, where CNR is the maximum across the
 * satellite's signal cells.  CNR units are dB-Hz (10-bit field with
 * 0.0625 dB-Hz LSB).  Returns 0 for non-MSM7 message types.
 *
 * @param payload     RTCM payload (starting at message-number bit).
 * @param payload_len Payload length in bytes.
 * @param msg_type    RTCM MSM7 message type (1077/1087/1097/1117/1127/1137).
 * @param prns_out    Output PRNs.
 * @param cnr_out     Output CNRs (dB-Hz), parallel to @p prns_out.
 * @param max_prns    Capacity of both arrays.
 * @param gnss_id_out [out, optional] GNSS ID (1=GPS, 2=GLONASS, etc.).
 * @return Number of (PRN, CNR) pairs written, or 0 on error / non-MSM7.
 */
int msm7_extract_cnr(const unsigned char *payload, int payload_len,
                     int msg_type,
                     int *prns_out, float *cnr_out, int max_prns,
                     int *gnss_id_out);

/**
 * @brief Update the per-(GNSS, PRN, signal) CNR cache from an MSM7 frame.
 *
 * Walks the per-cell block and writes each cell's CNR into the cache slot
 * indexed by the satellite's PRN and the signal's bit position in the
 * signal mask.  Rows for satellites in this frame are first zeroed so
 * dropped signals don't linger between frames.
 *
 * @param payload     MSM7 RTCM payload (starting at message-number bit).
 * @param payload_len Length in bytes.
 * @param msg_type    1077 / 1087 / 1097 / 1117 / 1127 / 1137.
 */
void msm7_update_per_band_cnr(const unsigned char *payload, int payload_len,
                              int msg_type);

/**
 * @brief Read per-band CNR for one SV from the cache.
 *
 * @param gnss_id  GNSS ID (1=GPS, 2=GLONASS, 3=Galileo, 4=QZSS, 5=BeiDou, ...).
 * @param prn      PRN within the constellation, 1-based.
 * @param out_cnr  Output array of 32 floats in dB-Hz; 0.0 means "no obs
 *                 for that signal index".  Index is the 0-based MSB-first
 *                 position in the RTCM signal mask.
 */
void get_sv_per_band_cnr(int gnss_id, int prn, float out_cnr[32]);

/**
 * @brief Return a short label for a signal-mask bit position.
 *
 * Uses RTCM 10403.3 signal mask tables per GNSS to map sig_idx
 * (0-based, MSB-first) to a short string like "L1C" / "E5I" / "B2I".
 * Returns "S<N>" for reserved or unmapped bits (N = RTCM 1-based ID).
 */
const char *msm_signal_label(int gnss_id, int sig_idx);

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
 * @brief Convert WGS-84 geodetic (lat, lon, alt) coordinates to ECEF.
 *
 * The inverse of @ref ecef_to_geodetic.  Used by the Sky Plot when no
 * RTCM 1005/1006 has been received and we need to fall back to a
 * user-supplied position (e.g. the rover's lat/lon from the config).
 *
 * @param lat_deg Latitude (degrees, WGS-84).
 * @param lon_deg Longitude (degrees, WGS-84).
 * @param alt_m   Altitude above the ellipsoid (metres).  Pass 0 if unknown.
 * @param x,y,z   [out] ECEF coordinates (metres), may be NULL.
 */
void geodetic_to_ecef(double lat_deg, double lon_deg, double alt_m,
                      double *x, double *y, double *z);

/**
 * @brief Rotate an ECEF delta vector into the local ENU (East/North/Up) frame.
 *
 * Used together with @ref enu_to_azel to compute satellite azimuth/elevation
 * relative to a reference station whose geodetic position is known.
 *
 * @param lat_deg Station latitude  (degrees, WGS84).
 * @param lon_deg Station longitude (degrees, WGS84).
 * @param dx      X component of (sv - station) ECEF vector, in meters.
 * @param dy      Y component of (sv - station) ECEF vector, in meters.
 * @param dz      Z component of (sv - station) ECEF vector, in meters.
 * @param e       [out] East  component (meters), may be NULL.
 * @param n       [out] North component (meters), may be NULL.
 * @param u       [out] Up    component (meters), may be NULL.
 */
void ecef_to_enu(double lat_deg, double lon_deg,
                 double dx, double dy, double dz,
                 double *e, double *n, double *u);

/**
 * @brief Convert a local ENU vector to azimuth (clockwise from N) and elevation.
 *
 * @param e       East  component (meters).
 * @param n       North component (meters).
 * @param u       Up    component (meters).
 * @param az_deg  [out] Azimuth in degrees, 0..360 (0 = north, 90 = east), may be NULL.
 * @param el_deg  [out] Elevation in degrees, -90..+90, may be NULL.
 */
void enu_to_azel(double e, double n, double u,
                 double *az_deg, double *el_deg);

/**
 * @brief Convenience wrapper: compute satellite azimuth/elevation from raw ECEF.
 *
 * Combines @ref ecef_to_geodetic, @ref ecef_to_enu, and @ref enu_to_azel.
 *
 * @param sta_x   Station ECEF X (meters).
 * @param sta_y   Station ECEF Y (meters).
 * @param sta_z   Station ECEF Z (meters).
 * @param sv_x    Satellite ECEF X (meters).
 * @param sv_y    Satellite ECEF Y (meters).
 * @param sv_z    Satellite ECEF Z (meters).
 * @param az_deg  [out] Azimuth in degrees, 0..360, may be NULL.
 * @param el_deg  [out] Elevation in degrees, -90..+90, may be NULL.
 */
void azel_from_ecef(double sta_x, double sta_y, double sta_z,
                    double sv_x,  double sv_y,  double sv_z,
                    double *az_deg, double *el_deg);

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
 * @brief Decode and print the contents of an RTCM 3.x Type 1029 message (Unicode Text String).
 *
 * Broadcasts a UTF-8 encoded free-text message from the reference station.
 * Commonly used for station identification, operator notices, or service metadata.
 * Contains:
 * - Reference Station ID
 * - MJD (date) and UTC seconds-of-day (time)
 * - Byte count and Unicode character count
 * - UTF-8 encoded text string (up to 127 bytes / 255 characters)
 *
 * @param payload     Pointer to the message payload (after RTCM header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1029(const unsigned char *payload, int payload_len);

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
 * @brief Decode and print an RTCM 3.x Type 1020 message (GLONASS Ephemeris).
 *
 * Per RTCM 10403.3.  GLONASS ephemeris is a position + velocity state
 * vector in PZ-90 (~WGS-84) at the reference epoch tb (Moscow seconds-
 * of-day), with a broadcast luni-solar acceleration.  Stored in the
 * cache with gnss_id = 2 and propagated by glonass_to_ecef() via
 * numerical integration -- NOT Kepler.
 *
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1020(const unsigned char *payload, int payload_len);

/**
 * @brief Decode and print an RTCM 3.x Type 1041 message (NavIC / IRNSS Ephemeris).
 *
 * Per RTCM 10403.3 Amendment 2 (Jan 2018), Table 3.5-104.  NavIC uses
 * GPS-style Keplerian elements with the same gravitational parameter and
 * Earth-rotation rate as GPS, but a distinct field layout and a couple of
 * scale-factor differences:
 *   - Cuc/Cus/Cic/Cis are 15 bits  signed at LSB = 2^-28 rad (vs 16 bits at 2^-29 for GPS)
 *   - Crc/Crs are 15 bits signed at LSB = 2^-4 m (vs 16 bits at 2^-5 for GPS)
 *   - IDOT and Delta_n share a 14- / 22-bit signed encoding at 2^-43 / 2^-41 semi-circles/s
 *   - 10-bit NavIC week (since 1999-08-22 UTC), TGD is 8 bits at 2^-31 s
 * Stores the result in the per-SV cache with gnss_id = 7, where the
 * existing kepler_to_ecef() propagator uses it without modification.
 *
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1041(const unsigned char *payload, int payload_len);

/**
 * @brief Decode and print an RTCM 3.x Type 1042 message (BeiDou D1 Ephemeris).
 *
 * Per RTCM 10403.3 Table 3.5-31.  Uses BeiDou-specific scale factors
 * (2^3 s for toc/toe; 2^-6 m for Crs/Crc; 2^-31 rad for the C-harmonics)
 * and TGDs in 0.1 ns LSB.  Populates the per-SV ephemeris cache with
 * gnss_id = 5.
 *
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1042(const unsigned char *payload, int payload_len);

/**
 * @brief Decode and print an RTCM 3.x Type 1044 message (QZSS Ephemeris).
 *
 * Per RTCM 10403.3 Table 3.5-32.  QZSS is GPS-compatible (Keplerian model,
 * GPS-identical scale factors and time scale), but the RTCM field ORDER
 * differs from 1019: SVID is 4 bits, toc immediately follows it, and
 * Week / URA / Code-on-L2 / Health / TGD / IODC / Fit Interval appear at
 * the tail.  Populates the cache with gnss_id = 4.
 *
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1044(const unsigned char *payload, int payload_len);

/**
 * @brief Decode and print an RTCM 3.x Type 1045 message (Galileo F/NAV Ephemeris).
 *
 * Galileo F/NAV (Free Navigation) ephemerides broadcast on E5a-I.  Contains
 * the same Keplerian orbital block as RTCM 1046 (I/NAV) plus a single BGD
 * (E1-E5a) and E5a signal/data validity flags.  On a valid frame the
 * decoded ephemeris is also written to the per-SV cache for use by
 * sv_orbit.c::kepler_to_ecef.
 *
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1045(const unsigned char *payload, int payload_len);

/**
 * @brief Decode and print an RTCM 3.x Type 1046 message (Galileo I/NAV Ephemeris).
 *
 * Galileo I/NAV (Integrity Navigation) ephemerides broadcast on E1-B and
 * E5b-I.  Same Keplerian orbital block as RTCM 1045, with two BGDs
 * (E1-E5a and E1-E5b) and per-signal SHS/DVS health flags for E5b and E1-B.
 * Populates the per-SV ephemeris cache.
 *
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1046(const unsigned char *payload, int payload_len);

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