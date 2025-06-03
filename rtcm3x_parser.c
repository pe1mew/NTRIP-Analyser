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
#include <math.h>
#include <inttypes.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
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

// Helper for extracting signed 38-bit values (two's complement)
int64_t extract_signed38(const unsigned char *buf, int start_bit) {
    uint64_t raw = get_bits(buf, start_bit, 38);
    if ((raw >> 37) & 1) {
        // Negative number
        return -((int64_t)((1ULL << 38) - raw));
    } else {
        return (int64_t)raw;
    }
}

    
void decode_rtcm_1005(const unsigned char *payload, int payload_len) {
    if (payload_len < 19) { // 12+12+6+1+1+1+1+38+1+1+38+2+38+2 = 200 bits = 25 bytes
        printf("Type 1005: Payload too short!\n");
        return;
    }
    int bit = 0;
    uint16_t msg_number = (uint16_t)get_bits(payload, bit, 12); bit += 12; // Should be 1006
    uint16_t ref_station_id = (uint16_t)get_bits(payload, bit, 12); bit += 12;
    uint8_t itrf_year = (uint8_t)get_bits(payload, bit, 6); bit += 6;
    uint8_t gps_ind = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    uint8_t glo_ind = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    uint8_t gal_ind = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    uint8_t ref_station_ind = (uint8_t)get_bits(payload, bit, 1); bit += 1;

    uint64_t raw_x = get_bits(payload, bit, 38); bit += 38;
    int64_t ecef_x = (raw_x & ((uint64_t)1 << 37)) ? (int64_t)(raw_x | (~((uint64_t)0x3FFFFFFFFF))) : (int64_t)raw_x;

    uint8_t osc_ind = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    bit += 1; // Reserved

    uint64_t raw_y = get_bits(payload, bit, 38); bit += 38;
    int64_t ecef_y = (raw_y & ((uint64_t)1 << 37)) ? (int64_t)(raw_y | (~((uint64_t)0x3FFFFFFFFF))) : (int64_t)raw_y;

    bit += 2; // Reserved

    uint64_t raw_z = get_bits(payload, bit, 38); bit += 38;
    int64_t ecef_z = (raw_z & ((uint64_t)1 << 37)) ? (int64_t)(raw_z | (~((uint64_t)0x3FFFFFFFFF))) : (int64_t)raw_z;

    bit += 2; // Reserved

    uint16_t antenna_height = (uint16_t)get_bits(payload, bit, 16); bit += 16;

    double x = ecef_x * 0.0001;
    double y = ecef_y * 0.0001;
    double z = ecef_z * 0.0001;
    double h = antenna_height * 0.0001;

    // ECEF to geodetic (WGS84)
    double a = 6378137.0;
    double e2 = 6.69437999014e-3;
    double lon = atan2(y, x);
    double p = sqrt(x * x + y * y);
    double lat = atan2(z, p * (1 - e2));
    double lat_prev;
    do {
        lat_prev = lat;
        double N = a / sqrt(1 - e2 * sin(lat) * sin(lat));
        lat = atan2(z + e2 * N * sin(lat), p);
    } while (fabs(lat - lat_prev) > 1e-11);

    double N = a / sqrt(1 - e2 * sin(lat) * sin(lat));
    double alt = p / cos(lat) - N + h;

    double lat_deg = lat * 180.0 / M_PI;
    double lon_deg = lon * 180.0 / M_PI;

    printf("RTCM 1005:\n");
    printf("  Message Number: %u\n", msg_number);
    printf("  Reference Station ID: %u\n", ref_station_id);
    printf("  ITRF Realization Year: %u\n", itrf_year);
    printf("  GPS: %u, GLONASS: %u, Galileo: %u\n", gps_ind, glo_ind, gal_ind);
    printf("  Reference Station Indicator: %u\n", ref_station_ind);
    printf("  ECEF X: %.4f m\n", x);
    printf("  ECEF Y: %.4f m\n", y);
    printf("  ECEF Z: %.4f m\n", z);
    printf("  Single Receiver Oscillator Indicator: %u\n", osc_ind);
    printf("WGS84 Lat: %.8f deg, Lon: %.8f deg, Alt: %.3f m\n", lat_deg, lon_deg, alt);
    printf("[Google Maps Link] https://maps.google.com/?q=%.8f,%.8f\n", lat_deg, lon_deg);

}

void decode_rtcm_1006(const unsigned char *payload, int payload_len) {
    if (payload_len < 21) { // 12+12+6+1+1+1+1+38+1+1+38+2+38+2+16 = 216 bits = 27 bytes
        printf("Type 1006: Payload too short!\n");
        return;
    }

    int bit = 0;
    uint16_t msg_number = (uint16_t)get_bits(payload, bit, 12); bit += 12; // Should be 1006
    uint16_t ref_station_id = (uint16_t)get_bits(payload, bit, 12); bit += 12;
    uint8_t itrf_year = (uint8_t)get_bits(payload, bit, 6); bit += 6;
    uint8_t gps_ind = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    uint8_t glo_ind = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    uint8_t gal_ind = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    uint8_t ref_station_ind = (uint8_t)get_bits(payload, bit, 1); bit += 1;

    uint64_t raw_x = get_bits(payload, bit, 38); bit += 38;
    int64_t ecef_x = (raw_x & ((uint64_t)1 << 37)) ? (int64_t)(raw_x | (~((uint64_t)0x3FFFFFFFFF))) : (int64_t)raw_x;

    uint8_t osc_ind = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    bit += 1; // Reserved

    uint64_t raw_y = get_bits(payload, bit, 38); bit += 38;
    int64_t ecef_y = (raw_y & ((uint64_t)1 << 37)) ? (int64_t)(raw_y | (~((uint64_t)0x3FFFFFFFFF))) : (int64_t)raw_y;

    bit += 2; // Reserved

    uint64_t raw_z = get_bits(payload, bit, 38); bit += 38;
    int64_t ecef_z = (raw_z & ((uint64_t)1 << 37)) ? (int64_t)(raw_z | (~((uint64_t)0x3FFFFFFFFF))) : (int64_t)raw_z;

    bit += 2; // Reserved

    uint16_t antenna_height = (uint16_t)get_bits(payload, bit, 16); bit += 16;

    double x = ecef_x * 0.0001;
    double y = ecef_y * 0.0001;
    double z = ecef_z * 0.0001;
    double h = antenna_height * 0.0001;

    // ECEF to geodetic (WGS84)
    double a = 6378137.0;
    double e2 = 6.69437999014e-3;
    double lon = atan2(y, x);
    double p = sqrt(x * x + y * y);
    double lat = atan2(z, p * (1 - e2));
    double lat_prev;
    do {
        lat_prev = lat;
        double N = a / sqrt(1 - e2 * sin(lat) * sin(lat));
        lat = atan2(z + e2 * N * sin(lat), p);
    } while (fabs(lat - lat_prev) > 1e-11);

    double N = a / sqrt(1 - e2 * sin(lat) * sin(lat));
    double alt = p / cos(lat) - N + h;

    double lat_deg = lat * 180.0 / M_PI;
    double lon_deg = lon * 180.0 / M_PI;

    printf("RTCM 1006:\n");
    printf("  Message Number: %u\n", msg_number);
    printf("  Reference Station ID: %u\n", ref_station_id);
    printf("  ITRF Realization Year: %u\n", itrf_year);
    printf("  GPS: %u, GLONASS: %u, Galileo: %u\n", gps_ind, glo_ind, gal_ind);
    printf("  Reference Station Indicator: %u\n", ref_station_ind);
    printf("  ECEF X: %.4f m\n", x);
    printf("  ECEF Y: %.4f m\n", y);
    printf("  ECEF Z: %.4f m\n", z);
    printf("  Antenna Height: %.4f m\n", h);
    printf("  Single Receiver Oscillator Indicator: %u\n", osc_ind);
    printf("WGS84 Lat: %.8f deg, Lon: %.8f deg, Alt: %.3f m\n", lat_deg, lon_deg, alt);
    printf("[Google Maps Link] https://maps.google.com/?q=%.8f,%.8f\n", lat_deg, lon_deg);
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

void decode_rtcm_1008(const unsigned char *payload, int payload_len) {
    int bit = 0;
    if (payload_len < 4) {
        printf("Type 1008: Payload too short!\n");
        return;
    }

    uint16_t msg_number = (uint16_t)get_bits(payload, bit, 12); bit += 12; // Should be 1008
    uint16_t ref_station_id = (uint16_t)get_bits(payload, bit, 12); bit += 12;

    uint8_t desc_len = (uint8_t)get_bits(payload, bit, 8); bit += 8;
    if (payload_len < 4 + desc_len) {
        printf("Type 1008: Payload too short for antenna descriptor!\n");
        return;
    }

    char descriptor[65] = {0};
    for (int i = 0; i < desc_len && i < 64; ++i) {
        descriptor[i] = (char)get_bits(payload, bit, 8); bit += 8;
    }
    descriptor[64] = '\0';

    uint8_t serial_len = (uint8_t)get_bits(payload, bit, 8); bit += 8;
    if (payload_len < 4 + desc_len + 1 + serial_len) {
        printf("Type 1008: Payload too short for antenna serial!\n");
        return;
    }

    char serial[65] = {0};
    for (int i = 0; i < serial_len && i < 64; ++i) {
        serial[i] = (char)get_bits(payload, bit, 8); bit += 8;
    }
    serial[64] = '\0';

    printf("RTCM 1008:\n");
    printf("  Message Number: %u\n", msg_number);
    printf("  Reference Station ID: %u\n", ref_station_id);
    printf("  Antenna Descriptor: %.*s\n", desc_len, descriptor);
    printf("  Antenna Serial Number: %.*s\n", serial_len, serial);
}

void decode_rtcm_1013(const unsigned char *payload, int payload_len) {
    int bit = 0;
    if (payload_len < 44) { // 12+12+128+8+128+8+8 = 304 bits = 38 bytes, but allow for some extra
        printf("Type 1013: Payload too short!\n");
        return;
    }

    uint16_t msg_number = (uint16_t)get_bits(payload, bit, 12); bit += 12; // Should be 1013
    uint16_t ref_station_id = (uint16_t)get_bits(payload, bit, 12); bit += 12;

    char aux_station_name[17] = {0};
    for (int i = 0; i < 16; ++i) {
        aux_station_name[i] = (char)get_bits(payload, bit, 8); bit += 8;
    }

    uint8_t aux_station_indicator = (uint8_t)get_bits(payload, bit, 8); bit += 8;

    char aux_station_provider[17] = {0};
    for (int i = 0; i < 16; ++i) {
        aux_station_provider[i] = (char)get_bits(payload, bit, 8); bit += 8;
    }

    uint8_t aux_station_setup_id = (uint8_t)get_bits(payload, bit, 8); bit += 8;
    uint8_t aux_station_interval = (uint8_t)get_bits(payload, bit, 8); bit += 8;

    printf("RTCM 1013 (Network Auxiliary Station Description):\n");
    printf("  Message Number: %u\n", msg_number);
    printf("  Reference Station ID: %u\n", ref_station_id);
    printf("  AUX Station Name: %.*s\n", 16, aux_station_name);
    printf("  AUX Station Indicator: %u\n", aux_station_indicator);
    printf("  AUX Station Provider: %.*s\n", 16, aux_station_provider);
    printf("  AUX Station Setup ID: %u\n", aux_station_setup_id);
    printf("  AUX Station Interval: %u s\n", aux_station_interval);
}

void decode_rtcm_1033(const unsigned char *payload, int payload_len) {
    int bit = 0;
    if (payload_len < 8) {
        printf("Type 1033: Payload too short!\n");
        return;
    }

    uint16_t msg_number = (uint16_t)get_bits(payload, bit, 12); bit += 12; // Should be 1033
    uint16_t ref_station_id = (uint16_t)get_bits(payload, bit, 12); bit += 12;

    uint8_t ant_desc_len = (uint8_t)get_bits(payload, bit, 8); bit += 8;
    char ant_desc[65] = {0};
    for (int i = 0; i < ant_desc_len && i < 64; ++i) {
        ant_desc[i] = (char)get_bits(payload, bit, 8); bit += 8;
    }
    ant_desc[64] = '\0';

    uint8_t ant_serial_len = (uint8_t)get_bits(payload, bit, 8); bit += 8;
    char ant_serial[65] = {0};
    for (int i = 0; i < ant_serial_len && i < 64; ++i) {
        ant_serial[i] = (char)get_bits(payload, bit, 8); bit += 8;
    }
    ant_serial[64] = '\0';

    uint8_t recv_type_len = (uint8_t)get_bits(payload, bit, 8); bit += 8;
    char recv_type[65] = {0};
    for (int i = 0; i < recv_type_len && i < 64; ++i) {
        recv_type[i] = (char)get_bits(payload, bit, 8); bit += 8;
    }
    recv_type[64] = '\0';

    uint8_t recv_serial_len = (uint8_t)get_bits(payload, bit, 8); bit += 8;
    char recv_serial[65] = {0};
    for (int i = 0; i < recv_serial_len && i < 64; ++i) {
        recv_serial[i] = (char)get_bits(payload, bit, 8); bit += 8;
    }
    recv_serial[64] = '\0';

    printf("RTCM 1033 (Receiver & Antenna Descriptor):\n");
    printf("  Message Number: %u\n", msg_number);
    printf("  Reference Station ID: %u\n", ref_station_id);
    printf("  Antenna Descriptor: %.*s\n", ant_desc_len, ant_desc);
    printf("  Antenna Serial Number: %.*s\n", ant_serial_len, ant_serial);
    printf("  Receiver Type: %.*s\n", recv_type_len, recv_type);
    printf("  Receiver Serial Number: %.*s\n", recv_serial_len, recv_serial);
}

void decode_rtcm_1045(const unsigned char *payload, int payload_len) {
    int bit = 0;
    if (payload_len < 48) { // Minimum length for RTCM 1045 (Galileo Ephemeris) is 48 bytes
        printf("Type 1045: Payload too short!\n");
        return;
    }

    uint16_t msg_number = (uint16_t)get_bits(payload, bit, 12); bit += 12; // Should be 1045
    uint8_t svid = (uint8_t)get_bits(payload, bit, 6); bit += 6;
    uint16_t week = (uint16_t)get_bits(payload, bit, 12); bit += 12;
    uint16_t iodnav = (uint16_t)get_bits(payload, bit, 10); bit += 10;
    uint8_t sisa = (uint8_t)get_bits(payload, bit, 8); bit += 8;
    int16_t idot = (int16_t)get_bits(payload, bit, 14); bit += 14;

    // Delta n (Mean Motion Difference)
    int16_t delta_n = (int16_t)get_bits(payload, bit, 16); bit += 16;
    // M0 (Mean Anomaly at Reference Time)
    int32_t m0 = (int32_t)get_bits(payload, bit, 32); bit += 32;
    // Eccentricity (e)
    uint32_t e = (uint32_t)get_bits(payload, bit, 32); bit += 32;
    // sqrtA (Square Root of Semi-major Axis)
    uint32_t sqrtA = (uint32_t)get_bits(payload, bit, 32); bit += 32;
    // Omega0 (Longitude of Ascending Node)
    int32_t omega0 = (int32_t)get_bits(payload, bit, 32); bit += 32;
    // i0 (Inclination Angle at Reference Time)
    int32_t i0 = (int32_t)get_bits(payload, bit, 32); bit += 32;
    // omega (Argument of Perigee)
    int32_t omega = (int32_t)get_bits(payload, bit, 32); bit += 32;
    // OmegaDot (Rate of Right Ascension)
    int16_t omega_dot = (int16_t)get_bits(payload, bit, 24); bit += 24;
    // Cuc
    int16_t cuc = (int16_t)get_bits(payload, bit, 16); bit += 16;
    // Cus
    int16_t cus = (int16_t)get_bits(payload, bit, 16); bit += 16;
    // Crc
    int16_t crc = (int16_t)get_bits(payload, bit, 16); bit += 16;
    // Crs
    int16_t crs = (int16_t)get_bits(payload, bit, 16); bit += 16;
    // Cic
    int16_t cic = (int16_t)get_bits(payload, bit, 16); bit += 16;
    // Cis
    int16_t cis = (int16_t)get_bits(payload, bit, 16); bit += 16;
    // Toe (Time of Ephemeris)
    uint16_t toe = (uint16_t)get_bits(payload, bit, 14); bit += 14;
    // BGD E5a/E1
    int16_t bgd_e5a_e1 = (int16_t)get_bits(payload, bit, 10); bit += 10;
    // BGD E5b/E1
    int16_t bgd_e5b_e1 = (int16_t)get_bits(payload, bit, 10); bit += 10;
    // Health/Status flags
    uint8_t health = (uint8_t)get_bits(payload, bit, 6); bit += 6;

    printf("RTCM 1045 (Galileo F/NAV Ephemeris):\n");
    printf("  Message Number: %u\n", msg_number);
    printf("  Satellite ID (SVID): %u\n", svid);
    printf("  Week Number: %u\n", week);
    printf("  IODnav: %u\n", iodnav);
    printf("  SISA: %u\n", sisa);
    printf("  IDOT: %d\n", idot);
    printf("  Delta n: %d\n", delta_n);
    printf("  M0: %d\n", m0);
    printf("  Eccentricity: %u\n", e);
    printf("  sqrtA: %u\n", sqrtA);
    printf("  Omega0: %d\n", omega0);
    printf("  i0: %d\n", i0);
    printf("  omega: %d\n", omega);
    printf("  OmegaDot: %d\n", omega_dot);
    printf("  Cuc: %d\n", cuc);
    printf("  Cus: %d\n", cus);
    printf("  Crc: %d\n", crc);
    printf("  Crs: %d\n", crs);
    printf("  Cic: %d\n", cic);
    printf("  Cis: %d\n", cis);
    printf("  Toe: %u\n", toe);
    printf("  BGD E5a/E1: %d\n", bgd_e5a_e1);
    printf("  BGD E5b/E1: %d\n", bgd_e5b_e1);
    printf("  Health/Status: %u\n", health);
}

void decode_rtcm_1230(const unsigned char *payload, int payload_len) {
    int bit = 0;
    if (payload_len < 4) {
        printf("Type 1230: Payload too short!\n");
        return;
    }

    uint16_t msg_number = (uint16_t)get_bits(payload, bit, 12); bit += 12; // Should be 1230
    uint16_t ref_station_id = (uint16_t)get_bits(payload, bit, 12); bit += 12;
    uint8_t num_sats = (uint8_t)get_bits(payload, bit, 6); bit += 6;

    printf("RTCM 1230 (GLONASS L1/L2 Code-Phase Biases):\n");
    printf("  Message Number: %u\n", msg_number);
    printf("  Reference Station ID: %u\n", ref_station_id);
    printf("  Number of Satellites: %u\n", num_sats);

    for (int i = 0; i < num_sats; ++i) {
        if ((bit + 22) > payload_len * 8) {
            printf("  [WARN] Not enough data for satellite %d\n", i + 1);
            break;
        }
        uint8_t sat_id = (uint8_t)get_bits(payload, bit, 6); bit += 6;
        int16_t bias = (int16_t)get_bits(payload, bit, 16); bit += 16;
        double bias_ns = bias * 0.01; // Convert to nanoseconds

        printf("    Satellite %d: Slot ID = %u, L1-L2 Code-Phase Bias = %.2f ns\n", i + 1, sat_id, bias_ns);
    }
}

void print_rtcm_1005_message_hex(const unsigned char *data, int length) {
    printf("Complete RTCM 1005 message (%d bytes):\n", length);
    for (int i = 0; i < length; ++i) {
        printf("%02X ", data[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    if (length % 16 != 0) printf("\n");
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
                print_rtcm_1005_message_hex(data, frame_len);
                printf("\nRTCM Message: Type = %d, Length = %d (Type 1005 detected)\n", msg_type, msg_length);
                decode_rtcm_1005(&data[3], msg_length);
            } else if (msg_type == 1006) {
                printf("\nRTCM Message: Type = %d, Length = %d (Type 1006 detected)\n", msg_type, msg_length);
                decode_rtcm_1006(&data[3], msg_length);
            } else if (msg_type == 1077) {
                printf("\nRTCM Message: Type = %d, Length = %d (Type 1077 detected)\n", msg_type, msg_length);
                decode_rtcm_1077(&data[3], msg_length);
            } else if (msg_type == 1087) {
                printf("\nRTCM Message: Type = %d, Length = %d (Type 1087 detected)\n", msg_type, msg_length);
                decode_rtcm_1087(&data[3], msg_length);
            } else if (msg_type == 1097) {
                printf("\nRTCM Message: Type = %d, Length = %d (Type 1097 detected)\n", msg_type, msg_length);
                decode_rtcm_1097(&data[3], msg_length);
            } else if (msg_type == 1117) {
                printf("\nRTCM Message: Type = %d, Length = %d (Type 1117 detected)\n", msg_type, msg_length);
                decode_rtcm_1117(&data[3], msg_length);
            } else if (msg_type == 1127) {
                printf("\nRTCM Message: Type = %d, Length = %d (Type 1127 detected)\n", msg_type, msg_length);
                decode_rtcm_1127(&data[3], msg_length);
            } else if (msg_type == 1137) {
                printf("\nRTCM Message: Type = %d, Length = %d (Type 1137 detected)\n", msg_type, msg_length);
                decode_rtcm_1137(&data[3], msg_length);
            } else if (msg_type == 1008) {
                printf("\nRTCM Message: Type = %d, Length = %d (Type 1008 detected)\n", msg_type, msg_length);
                decode_rtcm_1008(&data[3], msg_length);
            } else if (msg_type == 1013) {
                printf("\nRTCM Message: Type = %d, Length = %d (Type 1013 detected)\n", msg_type, msg_length);
                decode_rtcm_1013(&data[3], msg_length);
            } else if (msg_type == 1033) {
                printf("\nRTCM Message: Type = %d, Length = %d (Type 1033 detected)\n", msg_type, msg_length);
                decode_rtcm_1033(&data[3], msg_length);
            } else if (msg_type == 1045) {
                printf("\nRTCM Message: Type = %d, Length = %d (Type 1045 detected)\n", msg_type, msg_length);
                decode_rtcm_1045(&data[3], msg_length);
            } else if (msg_type == 1230) {
                printf("\nRTCM Message: Type = %d, Length = %d (Type 1230 detected)\n", msg_type, msg_length);
                decode_rtcm_1230(&data[3], msg_length);
            } else {
                if (length >= frame_len) {
                    if (crc_calc != crc_extracted) {
                        printf("\nRTCM Message: Type = %d, Length = %d, CRC = 0x%06X (CRC FAIL! Calculated: 0x%06X)\n", msg_type, msg_length, crc_extracted, crc_calc);
                    }
                } else {
                    printf("\nRTCM Message: Type = %d, Length = %d (frame incomplete)\n", msg_type, msg_length);
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


