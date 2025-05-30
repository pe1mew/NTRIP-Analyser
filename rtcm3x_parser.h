/**
 * @file rtcm3x_parser.h
 * @brief RTCM 3.x Stream Analyzer - Function Prototypes and Documentation
 *
 * This header provides function prototypes for parsing, analyzing, and decoding RTCM 3.x messages,
 * including MSM7 message types for multiple GNSS constellations. It also includes bit extraction
 * and CRC-24Q calculation utilities.
 *
 * Project: NTRIP RTCM 3.x Stream Analyzer
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause (see LICENSE for details)
 * 
 * Supported message types:
 *   - 1005: Stationary RTK Reference Station ARP
 *   - 1077: MSM7 GPS
 *   - 1087: MSM7 GLONASS
 *   - 1097: MSM7 Galileo
 *   - 1117: MSM7 QZSS
 *   - 1127: MSM7 BeiDou
 *   - 1137: MSM7 SBAS
 *
 * For more information, see the project README and LICENSE files.
 */

#ifndef RTCM3X_PARSER_H
#define RTCM3X_PARSER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Extract bits from a buffer (big-endian, MSB first).
 * 
 * @param buf      Pointer to the buffer.
 * @param start_bit Start bit index (0 = first bit of buf[0]).
 * @param bit_len   Number of bits to extract.
 * @return Extracted bits as an unsigned 64-bit integer.
 */
uint64_t get_bits(const unsigned char *buf, int start_bit, int bit_len);

/**
 * @brief Calculate CRC-24Q for the given data.
 * 
 * @param data   Pointer to input data buffer.
 * @param length Number of bytes in the buffer.
 * @return 24-bit CRC as uint32_t (lower 24 bits are valid).
 */
uint32_t crc24q(const uint8_t *data, size_t length);

/**
 * @brief Analyze and print information about an RTCM message.
 * 
 * @param data   Pointer to the RTCM message buffer.
 * @param length Length of the buffer in bytes.
 */
void analyze_rtcm_message(const unsigned char *data, int length);

/**
 * @brief Decode and print the contents of an RTCM 3.x Type 1005 message.
 * 
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1005(const unsigned char *payload, int payload_len);

/**
 * @brief Decode and print the contents of an RTCM 3.x Type 1077 (MSM7 GPS) message.
 * 
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1077(const unsigned char *payload, int payload_len);

/**
 * @brief Decode and print the contents of an RTCM 3.x Type 1087 (MSM7 GLONASS) message.
 * 
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1087(const unsigned char *payload, int payload_len);

/**
 * @brief Decode and print the contents of an RTCM 3.x Type 1097 (MSM7 Galileo) message.
 * 
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1097(const unsigned char *payload, int payload_len);

/**
 * @brief Decode and print the contents of an RTCM 3.x Type 1117 (MSM7 QZSS) message.
 * 
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1117(const unsigned char *payload, int payload_len);

/**
 * @brief Decode and print the contents of an RTCM 3.x Type 1127 (MSM7 BeiDou) message.
 * 
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1127(const unsigned char *payload, int payload_len);

/**
 * @brief Decode and print the contents of an RTCM 3.x Type 1137 (MSM7 SBAS) message.
 * 
 * @param payload     Pointer to the message payload (after header).
 * @param payload_len Length of the payload in bytes.
 */
void decode_rtcm_1137(const unsigned char *payload, int payload_len);

#ifdef __cplusplus
}
#endif

#endif // RTCM3X_PARSER_H