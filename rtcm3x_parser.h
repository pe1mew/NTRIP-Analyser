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