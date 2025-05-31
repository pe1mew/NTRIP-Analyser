/**
 * @file rtcm3x_parser.c
 * @brief RTCM 3.x Stream Analyzer - Implementation
 *
 * This file contains the implementation for parsing, analyzing, and decoding RTCM 3.x messages,
 * including MSM7 message types for multiple GNSS constellations (GPS, GLONASS, Galileo, QZSS, BeiDou, SBAS).
 * It provides utilities for bit extraction and CRC-24Q calculation, as well as message-specific decoders.
 *
 * Project: NTRIP RTCM 3.x Stream Analyzer
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause (see LICENSE for details)
 *
 * For more information, see the project README and LICENSE files.
 */

#include <stdio.h>
#include "rtcm3x_parser.h"

uint32_t crc24q(const uint8_t *data, size_t length) {
    uint32_t crc = 0x000000;
    const uint32_t poly = 0x1864CFB;

    for (size_t i = 0; i < length; i++) {
        crc ^= ((uint32_t)data[i]) << 16;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x800000)
                crc = ((crc << 1) ^ poly) & 0xFFFFFF;
            else
                crc = (crc << 1) & 0xFFFFFF;
        }
    }
    return crc;
}

uint64_t get_bits(const unsigned char *buf, int start_bit, int bit_len) {
    uint64_t result = 0;
    for (int i = 0; i < bit_len; ++i) {
        int byte = (start_bit + i) / 8;
        int bit = 7 - ((start_bit + i) % 8);
        result = (result << 1) | ((buf[byte] >> bit) & 1);
    }
    return result;
}

void decode_rtcm_1005(const unsigned char *payload, int payload_len) {
    if (payload_len < 12) {
        printf("Type 1005: Payload too short!\n");
        return;
    }

    int bit = 0;
    uint16_t ref_station_id = (uint16_t)get_bits(payload, bit, 12); bit += 12;
    uint8_t itrf_year = (uint8_t)get_bits(payload, bit, 6); bit += 6;
    uint8_t gps_ind = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    uint8_t glo_ind = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    uint8_t gal_ind = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    uint8_t ref_station_ind = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    int64_t ecef_x = (int64_t)get_bits(payload, bit, 38); bit += 38;
    if (ecef_x & ((uint64_t)1 << 37)) ecef_x -= ((uint64_t)1 << 38); // sign extend
    uint8_t osc_ind = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    bit += 1; // reserved
    int64_t ecef_y = (int64_t)get_bits(payload, bit, 38); bit += 38;
    if (ecef_y & ((uint64_t)1 << 37)) ecef_y -= ((uint64_t)1 << 38);
    bit += 2; // reserved
    int64_t ecef_z = (int64_t)get_bits(payload, bit, 38); bit += 38;
    if (ecef_z & ((uint64_t)1 << 37)) ecef_z -= ((uint64_t)1 << 38);
    bit += 3; // reserved

    printf("RTCM 1005 Stationary RTK Reference Station ARP:\n");
    printf("  Reference Station ID: %u\n", ref_station_id);
    printf("  ITRF Realization Year: %u\n", itrf_year);
    printf("  GPS: %u, GLONASS: %u, Galileo: %u\n", gps_ind, glo_ind, gal_ind);
    printf("  Reference Station Indicator: %u\n", ref_station_ind);
    printf("  ECEF X: %.4f m\n", ecef_x * 0.0001);
    printf("  ECEF Y: %.4f m\n", ecef_y * 0.0001);
    printf("  ECEF Z: %.4f m\n", ecef_z * 0.0001);
    printf("  Single Receiver Oscillator Indicator: %u\n", osc_ind);
}

void decode_rtcm_1077(const unsigned char *payload, int payload_len) {
    int bit = 0;
    if (payload_len < 20) {
        printf("Type 1077: Payload too short!\n");
        return;
    }

    uint16_t ref_station_id = (uint16_t)get_bits(payload, bit, 12); bit += 12;
    uint32_t epoch_time = (uint32_t)get_bits(payload, bit, 30); bit += 30;
    uint8_t mm_flag = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    uint8_t iods = (uint8_t)get_bits(payload, bit, 3); bit += 3;
    bit += 7; // reserved
    uint8_t clk_steering = (uint8_t)get_bits(payload, bit, 2); bit += 2;
    uint8_t ext_clk = (uint8_t)get_bits(payload, bit, 2); bit += 2;
    uint8_t df_smoothing = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    uint8_t smoothing_int = (uint8_t)get_bits(payload, bit, 3); bit += 3;

    uint64_t sat_mask = get_bits(payload, bit, 64); bit += 64;
    uint32_t sig_mask = (uint32_t)get_bits(payload, bit, 32); bit += 32;

    int num_sats = 0, num_sigs = 0;
    for (int i = 0; i < 64; ++i) if ((sat_mask >> (63 - i)) & 1) num_sats++;
    for (int i = 0; i < 32; ++i) if ((sig_mask >> (31 - i)) & 1) num_sigs++;

    int num_cells = 0;
    for (int i = 0; i < num_sats * num_sigs; ++i)
        if (get_bits(payload, bit + i, 1)) num_cells++;
    bit += num_sats * num_sigs;

    printf("RTCM 1077 MSM7 (GPS Full Obs):\n");
    printf("  Reference Station ID: %u\n", ref_station_id);
    printf("  Epoch Time: %u ms\n", epoch_time);
    printf("  Multiple Message Flag: %u\n", mm_flag);
    printf("  IODS: %u\n", iods);
    printf("  Clock Steering: %u, Ext Clock: %u\n", clk_steering, ext_clk);
    printf("  Divergence-free Smoothing: %u, Smoothing Interval: %u\n", df_smoothing, smoothing_int);
    printf("  Satellites: %d, Signals: %d, Cells: %d\n", num_sats, num_sigs, num_cells);

    for (int cell = 0; cell < num_cells && cell < 5; ++cell) {
        int32_t pseudorange = (int32_t)get_bits(payload, bit, 20); bit += 20;
        int32_t phaserange = (int32_t)get_bits(payload, bit, 24); bit += 24;
        uint8_t lock = (uint8_t)get_bits(payload, bit, 7); bit += 7;
        uint8_t half_cycle = (uint8_t)get_bits(payload, bit, 1); bit += 1;
        uint8_t cnr = (uint8_t)get_bits(payload, bit, 8); bit += 8;
        int16_t phaserate = (int16_t)get_bits(payload, bit, 15); bit += 15;

        if (pseudorange & (1 << 19)) pseudorange -= (1 << 20);
        if (phaserange & (1 << 23)) phaserange -= (1 << 24);
        if (phaserate & (1 << 14)) phaserate -= (1 << 15);

        printf("  Cell %d: PR=%.4f m, PH=%.4f m, Lock=%u, Half=%u, CNR=%u dBHz, PHrate=%.4f m/s\n",
            cell + 1,
            pseudorange * 0.0001,
            phaserange * 0.0001,
            lock,
            half_cycle,
            cnr,
            phaserate * 0.0001
        );
    }
    if (num_cells > 5) printf("  ... (%d more cells not shown)\n", num_cells - 5);
}

static void decode_rtcm_msm7(const unsigned char *payload, int payload_len, const char *gnss_name, int msg_type) {
    int bit = 0;
    if (payload_len < 20) {
        printf("Type %d: Payload too short!\n", msg_type);
        return;
    }

    uint16_t ref_station_id = (uint16_t)get_bits(payload, bit, 12); bit += 12;
    uint32_t epoch_time = (uint32_t)get_bits(payload, bit, 30); bit += 30;
    uint8_t mm_flag = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    uint8_t iods = (uint8_t)get_bits(payload, bit, 3); bit += 3;
    bit += 7; // reserved
    uint8_t clk_steering = (uint8_t)get_bits(payload, bit, 2); bit += 2;
    uint8_t ext_clk = (uint8_t)get_bits(payload, bit, 2); bit += 2;
    uint8_t df_smoothing = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    uint8_t smoothing_int = (uint8_t)get_bits(payload, bit, 3); bit += 3;

    uint64_t sat_mask = get_bits(payload, bit, 64); bit += 64;
    uint32_t sig_mask = (uint32_t)get_bits(payload, bit, 32); bit += 32;

    int num_sats = 0, num_sigs = 0;
    for (int i = 0; i < 64; ++i) if ((sat_mask >> (63 - i)) & 1) num_sats++;
    for (int i = 0; i < 32; ++i) if ((sig_mask >> (31 - i)) & 1) num_sigs++;

    int num_cells = 0;
    for (int i = 0; i < num_sats * num_sigs; ++i)
        if (get_bits(payload, bit + i, 1)) num_cells++;
    bit += num_sats * num_sigs;

    printf("RTCM %d MSM7 (%s Full Obs):\n", msg_type, gnss_name);
    printf("  Reference Station ID: %u\n", ref_station_id);
    printf("  Epoch Time: %u ms\n", epoch_time);
    printf("  Multiple Message Flag: %u\n", mm_flag);
    printf("  IODS: %u\n", iods);
    printf("  Clock Steering: %u, Ext Clock: %u\n", clk_steering, ext_clk);
    printf("  Divergence-free Smoothing: %u, Smoothing Interval: %u\n", df_smoothing, smoothing_int);
    printf("  Satellites: %d, Signals: %d, Cells: %d\n", num_sats, num_sigs, num_cells);

    for (int cell = 0; cell < num_cells && cell < 5; ++cell) {
        int32_t pseudorange = (int32_t)get_bits(payload, bit, 20); bit += 20;
        int32_t phaserange = (int32_t)get_bits(payload, bit, 24); bit += 24;
        uint8_t lock = (uint8_t)get_bits(payload, bit, 7); bit += 7;
        uint8_t half_cycle = (uint8_t)get_bits(payload, bit, 1); bit += 1;
        uint8_t cnr = (uint8_t)get_bits(payload, bit, 8); bit += 8;
        int16_t phaserate = (int16_t)get_bits(payload, bit, 15); bit += 15;

        if (pseudorange & (1 << 19)) pseudorange -= (1 << 20);
        if (phaserange & (1 << 23)) phaserange -= (1 << 24);
        if (phaserate & (1 << 14)) phaserate -= (1 << 15);

        printf("  Cell %d: PR=%.4f m, PH=%.4f m, Lock=%u, Half=%u, CNR=%u dBHz, PHrate=%.4f m/s\n",
            cell + 1,
            pseudorange * 0.0001,
            phaserange * 0.0001,
            lock,
            half_cycle,
            cnr,
            phaserate * 0.0001
        );
    }
    if (num_cells > 5) printf("  ... (%d more cells not shown)\n", num_cells - 5);
}

void decode_rtcm_1087(const unsigned char *payload, int payload_len) {
    decode_rtcm_msm7(payload, payload_len, "GLONASS", 1087);
}

void decode_rtcm_1097(const unsigned char *payload, int payload_len) {
    decode_rtcm_msm7(payload, payload_len, "Galileo", 1097);
}

void decode_rtcm_1117(const unsigned char *payload, int payload_len) {
    decode_rtcm_msm7(payload, payload_len, "QZSS", 1117);
}

void decode_rtcm_1127(const unsigned char *payload, int payload_len) {
    decode_rtcm_msm7(payload, payload_len, "BeiDou", 1127);
}

void decode_rtcm_1137(const unsigned char *payload, int payload_len) {
    decode_rtcm_msm7(payload, payload_len, "SBAS", 1137);
}

int analyze_rtcm_message(const unsigned char *data, int length, bool suppress_output) {
    if (length < 6) return -1;

    if (data[0] == 0xD3) {
        int msg_length = ((data[1] & 0x03) << 8) | data[2];
        int msg_type = ((data[3] << 4) | (data[4] >> 4)) & 0x0FFF;

        uint32_t crc_extracted = 0;
        int frame_len = msg_length + 6;
        if (length >= frame_len) {
            crc_extracted = (data[3 + msg_length] << 16) |
                            (data[3 + msg_length + 1] << 8) |
                            (data[3 + msg_length + 2]);
        }

        uint32_t crc_calc = 0;
        if (length >= frame_len) {
            crc_calc = crc24q(data, 3 + msg_length);
        }

        if (!suppress_output) {
            if (msg_type == 1005) {
                printf("RTCM Message: Type = %d, Length = %d (Type 1005 detected)\n", msg_type, msg_length);
                decode_rtcm_1005(&data[3], msg_length);
            } else if (msg_type == 1077) {
                printf("RTCM Message: Type = %d, Length = %d (Type 1077 detected)\n", msg_type, msg_length);
                decode_rtcm_1077(&data[3], msg_length);
            } else if (msg_type == 1087) {
                printf("RTCM Message: Type = %d, Length = %d (Type 1087 detected)\n", msg_type, msg_length);
                decode_rtcm_1087(&data[3], msg_length);
            } else if (msg_type == 1097) {
                printf("RTCM Message: Type = %d, Length = %d (Type 1097 detected)\n", msg_type, msg_length);
                decode_rtcm_1097(&data[3], msg_length);
            } else if (msg_type == 1117) {
                printf("RTCM Message: Type = %d, Length = %d (Type 1117 detected)\n", msg_type, msg_length);
                decode_rtcm_1117(&data[3], msg_length);
            } else if (msg_type == 1127) {
                printf("RTCM Message: Type = %d, Length = %d (Type 1127 detected)\n", msg_type, msg_length);
                decode_rtcm_1127(&data[3], msg_length);
            } else if (msg_type == 1137) {
                printf("RTCM Message: Type = %d, Length = %d (Type 1137 detected)\n", msg_type, msg_length);
                decode_rtcm_1137(&data[3], msg_length);
            } else {
                if (length >= frame_len) {
                    if (crc_calc != crc_extracted) {
                        printf("RTCM Message: Type = %d, Length = %d, CRC = 0x%06X (CRC FAIL! Calculated: 0x%06X)\n", msg_type, msg_length, crc_extracted, crc_calc);
                    }
                } else {
                    printf("RTCM Message: Type = %d, Length = %d (frame incomplete)\n", msg_type, msg_length);
                }
            }

            if (length >= frame_len && crc_calc != crc_extracted) {
                printf("  CRC check: FAIL | extracted: 0x%06X | calculated: 0x%06X\n", crc_extracted, crc_calc);
            }
        }

        return msg_type;
    } else {
        if (!suppress_output) {
            printf("Non-RTCM or malformed data (first bytes): ");
            for (int i = 0; i < length && i < 16; ++i) {
                printf("%02X ", data[i]);
            }
            printf("\n");
        }
        return -1;
    }
}


