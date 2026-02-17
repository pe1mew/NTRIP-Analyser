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
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <math.h>
#include "rtcm3x_parser.h"

/* ── Redirectable output buffer for decode functions ──────── */

static RtcmStrBuf *g_rtcm_strbuf = NULL;

void rtcm_strbuf_init(RtcmStrBuf *sb, int initial_cap) {
    sb->buf = (char *)malloc(initial_cap);
    sb->len = 0;
    sb->cap = initial_cap;
    if (sb->buf) sb->buf[0] = '\0';
}

void rtcm_strbuf_free(RtcmStrBuf *sb) {
    free(sb->buf);
    sb->buf = NULL;
    sb->len = sb->cap = 0;
}

void rtcm_strbuf_clear(RtcmStrBuf *sb) {
    sb->len = 0;
    if (sb->buf) sb->buf[0] = '\0';
}

void rtcm_set_output_buffer(RtcmStrBuf *sb) {
    g_rtcm_strbuf = sb;
}

/**
 * @brief Printf replacement that writes to g_rtcm_strbuf when set,
 *        otherwise falls through to stdout.
 */
static void rtcm_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (g_rtcm_strbuf && g_rtcm_strbuf->buf) {
        int avail = g_rtcm_strbuf->cap - g_rtcm_strbuf->len;
        va_list ap2;
        va_copy(ap2, ap);
        int n = vsnprintf(g_rtcm_strbuf->buf + g_rtcm_strbuf->len, avail, fmt, ap2);
        va_end(ap2);
        if (n >= avail) {
            /* Grow buffer */
            int new_cap = g_rtcm_strbuf->cap * 2;
            if (new_cap < g_rtcm_strbuf->len + n + 1)
                new_cap = g_rtcm_strbuf->len + n + 1;
            char *tmp = (char *)realloc(g_rtcm_strbuf->buf, new_cap);
            if (tmp) {
                g_rtcm_strbuf->buf = tmp;
                g_rtcm_strbuf->cap = new_cap;
                vsnprintf(g_rtcm_strbuf->buf + g_rtcm_strbuf->len,
                          g_rtcm_strbuf->cap - g_rtcm_strbuf->len, fmt, ap);
            }
        }
        if (n > 0) g_rtcm_strbuf->len += n;
    } else {
        vprintf(fmt, ap);
    }
    va_end(ap);
}

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

// Helper to extract signed N-bit values
int64_t extract_signed(const unsigned char *buf, int start_bit, int bit_len) {
    uint64_t val = get_bits(buf, start_bit, bit_len);
    // Sign-extend if needed
    if (val & ((uint64_t)1 << (bit_len - 1))) {
        val |= (~0ULL) << bit_len;
    }
    return (int64_t)val;
}

void ecef_to_geodetic(double x, double y, double z, double h, double *lat_deg, double *lon_deg, double *alt) {
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
    if (alt) *alt = p / cos(lat) - N + h;
    if (lat_deg) *lat_deg = lat * 180.0 / M_PI;
    if (lon_deg) *lon_deg = lon * 180.0 / M_PI;
}

void calc_distance_heading(double lat1, double lon1, double lat2, double lon2, double *distance_km, double *heading_deg) {
    const double R = 6371.0; // Earth radius in km
    double phi1 = lat1 * M_PI / 180.0;
    double phi2 = lat2 * M_PI / 180.0;
    double dphi = (lat2 - lat1) * M_PI / 180.0;
    double dlambda = (lon2 - lon1) * M_PI / 180.0;

    double a = sin(dphi/2) * sin(dphi/2) +
               cos(phi1) * cos(phi2) *
               sin(dlambda/2) * sin(dlambda/2);
    double c = 2 * atan2(sqrt(a), sqrt(1-a));
    if (distance_km) *distance_km = R * c;

    // Heading calculation (bearing from point 1 to point 2)
    double y = sin(dlambda) * cos(phi2);
    double x = cos(phi1)*sin(phi2) - sin(phi1)*cos(phi2)*cos(dlambda);
    double theta = atan2(y, x);
    double bearing = fmod((theta * 180.0 / M_PI) + 360.0, 360.0);
    if (heading_deg) *heading_deg = bearing;
}

    
void decode_rtcm_1005(const unsigned char *payload, int payload_len, const NTRIP_Config *config) {
    if (payload_len < 19) { // 12+12+6+1+1+1+1+38+1+1+38+2+38+2 = 200 bits = 25 bytes
        rtcm_printf("Type 1005: Payload too short!\n");
        return;
    }
    int bit = 0;
    uint16_t msg_number = (uint16_t)get_bits(payload, bit, 12); bit += 12; // Should be 1005
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

    // 1005 does not have antenna height
    double x = ecef_x * 0.0001;
    double y = ecef_y * 0.0001;
    double z = ecef_z * 0.0001;
    double h = 0.0;

    double lat_deg, lon_deg, alt;
    ecef_to_geodetic(x, y, z, h, &lat_deg, &lon_deg, &alt);

    rtcm_printf("RTCM 1005:\n");
    rtcm_printf("  Message Number: %u\n", msg_number);
    rtcm_printf("  Reference Station ID: %u\n", ref_station_id);
    rtcm_printf("  ITRF Realization Year: %u\n", itrf_year);
    rtcm_printf("  GPS: %u, GLONASS: %u, Galileo: %u\n", gps_ind, glo_ind, gal_ind);
    rtcm_printf("  Reference Station Indicator: %u\n", ref_station_ind);
    rtcm_printf("  ECEF X: %.4f m\n", x);
    rtcm_printf("  ECEF Y: %.4f m\n", y);
    rtcm_printf("  ECEF Z: %.4f m\n", z);
    rtcm_printf("  Single Receiver Oscillator Indicator: %u\n", osc_ind);
    rtcm_printf("WGS84 Lat: %.8f deg, Lon: %.8f deg, Alt: %.3f m\n", lat_deg, lon_deg, alt);
    rtcm_printf("[Google Maps Link] https://maps.google.com/?q=%.8f,%.8f\n", lat_deg, lon_deg);

    // --- Distance and heading calculation ---
    if (config) {
        double distance_km = 0, heading_deg = 0;
        // Calculate distance and heading from rover (config) to base (RTCM 1005)
        calc_distance_heading(config->LATITUDE, config->LONGITUDE, lat_deg, lon_deg, &distance_km, &heading_deg);
        rtcm_printf("Distance to base (from rover): %.3f km, Heading: %.1f deg\n", distance_km, heading_deg);
    }
}

void decode_rtcm_1006(const unsigned char *payload, int payload_len, const NTRIP_Config *config) {
    if (payload_len < 21) { // 12+12+6+1+1+1+1+38+1+1+38+2+38+2+16 = 216 bits = 27 bytes
        rtcm_printf("Type 1006: Payload too short!\n");
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

    double lat_deg, lon_deg, alt;
    ecef_to_geodetic(x, y, z, h, &lat_deg, &lon_deg, &alt);

    rtcm_printf("RTCM 1006:\n");
    rtcm_printf("  Message Number: %u\n", msg_number);
    rtcm_printf("  Reference Station ID: %u\n", ref_station_id);
    rtcm_printf("  ITRF Realization Year: %u\n", itrf_year);
    rtcm_printf("  GPS: %u, GLONASS: %u, Galileo: %u\n", gps_ind, glo_ind, gal_ind);
    rtcm_printf("  Reference Station Indicator: %u\n", ref_station_ind);
    rtcm_printf("  ECEF X: %.4f m\n", x);
    rtcm_printf("  ECEF Y: %.4f m\n", y);
    rtcm_printf("  ECEF Z: %.4f m\n", z);
    rtcm_printf("  Antenna Height: %.4f m\n", h);
    rtcm_printf("  Single Receiver Oscillator Indicator: %u\n", osc_ind);
    rtcm_printf("WGS84 Lat: %.8f deg, Lon: %.8f deg, Alt: %.3f m\n", lat_deg, lon_deg, alt);
    rtcm_printf("[Google Maps Link] https://maps.google.com/?q=%.8f,%.8f\n", lat_deg, lon_deg);

    // --- Distance and heading calculation ---
    if (config) {
        double distance_km = 0, heading_deg = 0;
        // Calculate distance and heading from rover (config) to base (RTCM 1005)
        calc_distance_heading(config->LATITUDE, config->LONGITUDE, lat_deg, lon_deg, &distance_km, &heading_deg);
        rtcm_printf("Distance to base (from rover): %.3f km, Heading: %.1f deg\n", distance_km, heading_deg);
    }
}

/**
 * @brief Comprehensive MSM7 decoder used by 1077 and all other MSM7 types.
 *
 * Fully decodes the MSM7 structure per RTCM 3.3:
 *   Header  → Satellite data (per sat) → Signal/Cell data (per cell)
 *
 * Satellite data block (per satellite, in mask order):
 *   - Rough range integer         (8 bits, ms)
 *   - Extended satellite info     (4 bits)
 *   - Rough range modulo          (10 bits, ms/1024)
 *   - Rough phase-range rate      (14 bits signed, m/s)
 *
 * Signal/cell data block (per active cell):
 *   - Fine pseudorange            (20 bits signed, 2^-29 ms ≈ 0.0001 m)
 *   - Fine phase range            (24 bits signed, 2^-31 ms ≈ 0.0001 m)
 *   - Lock time indicator         (10 bits)
 *   - Half-cycle ambiguity        (1 bit)
 *   - CNR                         (10 bits, 0.0625 dB-Hz)
 *   - Fine phase-range rate       (15 bits signed, 0.0001 m/s)
 */
static void decode_rtcm_msm7_full(const unsigned char *payload, int payload_len,
                                   const char *gnss_name, int msg_type)
{
    int bit = 0;
    if (payload_len < 20) {
        rtcm_printf("Type %d: Payload too short!\n", msg_type);
        return;
    }

    /* ── MSM header ──────────────────────────────────────────── */
    uint16_t ref_station_id = (uint16_t)get_bits(payload, bit, 12); bit += 12;
    uint32_t epoch_time     = (uint32_t)get_bits(payload, bit, 30); bit += 30;
    uint8_t  mm_flag        = (uint8_t)get_bits(payload, bit, 1);  bit += 1;
    uint8_t  iods           = (uint8_t)get_bits(payload, bit, 3);  bit += 3;
    bit += 7; /* reserved */
    uint8_t  clk_steering   = (uint8_t)get_bits(payload, bit, 2);  bit += 2;
    uint8_t  ext_clk        = (uint8_t)get_bits(payload, bit, 2);  bit += 2;
    uint8_t  df_smoothing   = (uint8_t)get_bits(payload, bit, 1);  bit += 1;
    uint8_t  smoothing_int  = (uint8_t)get_bits(payload, bit, 3);  bit += 3;

    uint64_t sat_mask = get_bits(payload, bit, 64); bit += 64;
    uint32_t sig_mask = (uint32_t)get_bits(payload, bit, 32); bit += 32;

    /* Build satellite PRN list from mask */
    int sat_prns[64];
    int num_sats = 0;
    for (int i = 0; i < 64; ++i) {
        if ((sat_mask >> (63 - i)) & 1)
            sat_prns[num_sats++] = i + 1;   /* PRN = 1-based mask bit position */
    }

    /* Build signal ID list from mask */
    int sig_ids[32];
    int num_sigs = 0;
    for (int i = 0; i < 32; ++i) {
        if ((sig_mask >> (31 - i)) & 1)
            sig_ids[num_sigs++] = i + 1;
    }

    /* Cell mask: num_sats × num_sigs bit matrix */
    int cell_mask[64][32];
    int num_cells = 0;
    for (int s = 0; s < num_sats; s++) {
        for (int g = 0; g < num_sigs; g++) {
            cell_mask[s][g] = (int)get_bits(payload, bit, 1); bit += 1;
            if (cell_mask[s][g]) num_cells++;
        }
    }

    /* ── Print header ────────────────────────────────────────── */
    rtcm_printf("RTCM %d MSM7 (%s Full Pseudorange and PhaseRange plus CNR (high resolution))\n", msg_type, gnss_name);
    rtcm_printf("============================================================\n");
    rtcm_printf("\n");
    rtcm_printf("  Reference Station ID  : %u\n", ref_station_id);
    rtcm_printf("  Epoch Time            : %u ms (%.3f s)\n", epoch_time, epoch_time / 1000.0);
    rtcm_printf("  Multiple Message Flag : %u\n", mm_flag);
    rtcm_printf("  IODS                  : %u\n", iods);
    rtcm_printf("  Clock Steering        : %u\n", clk_steering);
    rtcm_printf("  External Clock        : %u\n", ext_clk);
    rtcm_printf("  Div-free Smoothing    : %u\n", df_smoothing);
    rtcm_printf("  Smoothing Interval    : %u\n", smoothing_int);
    rtcm_printf("  Satellites            : %d\n", num_sats);
    rtcm_printf("  Signals               : %d\n", num_sigs);
    rtcm_printf("  Cells                 : %d\n", num_cells);

    /* ── Satellite data block ────────────────────────────────── */
    int    rough_range_int[64];
    int    ext_info[64];
    int    rough_range_mod[64];
    int    rough_phrate[64];

    for (int s = 0; s < num_sats; s++) {
        rough_range_int[s] = (int)get_bits(payload, bit, 8); bit += 8;
    }
    for (int s = 0; s < num_sats; s++) {
        ext_info[s] = (int)get_bits(payload, bit, 4); bit += 4;
    }
    for (int s = 0; s < num_sats; s++) {
        rough_range_mod[s] = (int)get_bits(payload, bit, 10); bit += 10;
    }
    for (int s = 0; s < num_sats; s++) {
        int raw = (int)get_bits(payload, bit, 14); bit += 14;
        if (raw & (1 << 13)) raw -= (1 << 14);
        rough_phrate[s] = raw;
    }

    /* Print satellite summary */
    rtcm_printf("\n");
    rtcm_printf("  Satellite Data\n");
    rtcm_printf("  -------------------------------------------------------\n");
    rtcm_printf("  PRN   Range(ms)     ExtInfo  PhaseRate(m/s)\n");
    rtcm_printf("  -------------------------------------------------------\n");
    for (int s = 0; s < num_sats; s++) {
        double range_ms = rough_range_int[s] + rough_range_mod[s] / 1024.0;
        double phrate_ms = rough_phrate[s] * 1.0;
        rtcm_printf("  %c%02d   %10.4f     %2d       %8.1f\n",
                     gnss_name[0], sat_prns[s], range_ms, ext_info[s], phrate_ms);
    }

    /* ── Signal / cell data block ────────────────────────────── */
    /* Read all cell data arrays (MSM7 bit widths) */
    int32_t  fine_pr[256];
    int32_t  fine_ph[256];
    uint16_t lock_ind[256];
    uint8_t  half_cyc[256];
    uint16_t cnr_raw[256];
    int16_t  fine_phrate[256];

    int c = 0;
    /* Fine pseudoranges (20 bits signed each) */
    for (int s = 0; s < num_sats; s++)
        for (int g = 0; g < num_sigs; g++)
            if (cell_mask[s][g]) {
                int32_t v = (int32_t)get_bits(payload, bit, 20); bit += 20;
                if (v & (1 << 19)) v -= (1 << 20);
                fine_pr[c++] = v;
            }

    c = 0;
    /* Fine phase ranges (24 bits signed each) */
    for (int s = 0; s < num_sats; s++)
        for (int g = 0; g < num_sigs; g++)
            if (cell_mask[s][g]) {
                int32_t v = (int32_t)get_bits(payload, bit, 24); bit += 24;
                if (v & (1 << 23)) v -= (1 << 24);
                fine_ph[c++] = v;
            }

    c = 0;
    /* Lock time indicators (10 bits each) */
    for (int s = 0; s < num_sats; s++)
        for (int g = 0; g < num_sigs; g++)
            if (cell_mask[s][g]) {
                lock_ind[c++] = (uint16_t)get_bits(payload, bit, 10); bit += 10;
            }

    c = 0;
    /* Half-cycle ambiguity (1 bit each) */
    for (int s = 0; s < num_sats; s++)
        for (int g = 0; g < num_sigs; g++)
            if (cell_mask[s][g]) {
                half_cyc[c++] = (uint8_t)get_bits(payload, bit, 1); bit += 1;
            }

    c = 0;
    /* CNR (10 bits each, resolution 0.0625 dB-Hz) */
    for (int s = 0; s < num_sats; s++)
        for (int g = 0; g < num_sigs; g++)
            if (cell_mask[s][g]) {
                cnr_raw[c++] = (uint16_t)get_bits(payload, bit, 10); bit += 10;
            }

    c = 0;
    /* Fine phase-range rates (15 bits signed, 0.0001 m/s) */
    for (int s = 0; s < num_sats; s++)
        for (int g = 0; g < num_sigs; g++)
            if (cell_mask[s][g]) {
                int16_t v = (int16_t)get_bits(payload, bit, 15); bit += 15;
                if (v & (1 << 14)) v -= (1 << 15);
                fine_phrate[c++] = v;
            }

    /* ── Print signal data per satellite ─────────────────────── */
    rtcm_printf("\n");
    rtcm_printf("  Signal Data\n");
    rtcm_printf("  -------------------------------------------------------------------------------------\n");
    rtcm_printf("  PRN   Sig  Fine PR(m)   Fine PH(m)   Lock  HC  CNR(dB-Hz)  PHrate(m/s)\n");
    rtcm_printf("  -------------------------------------------------------------------------------------\n");

    c = 0;
    for (int s = 0; s < num_sats; s++) {
        for (int g = 0; g < num_sigs; g++) {
            if (cell_mask[s][g]) {
                double pr_m      = fine_pr[c]     * 0.0001;
                double ph_m      = fine_ph[c]     * 0.0001;
                double cnr_dbhz  = cnr_raw[c]     * 0.0625;
                double phrate_ms = fine_phrate[c]  * 0.0001;

                rtcm_printf("  %c%02d   S%02d  %+10.4f   %+11.4f   %4u   %u   %7.2f     %+8.4f\n",
                             gnss_name[0], sat_prns[s], sig_ids[g],
                             pr_m, ph_m, lock_ind[c], half_cyc[c],
                             cnr_dbhz, phrate_ms);
                c++;
            }
        }
    }
    rtcm_printf("  -------------------------------------------------------------------------------------\n");
}

void decode_rtcm_1077(const unsigned char *payload, int payload_len) {
    decode_rtcm_msm7_full(payload, payload_len, "GPS", 1077);
}

static void decode_rtcm_msm7(const unsigned char *payload, int payload_len, const char *gnss_name, int msg_type) {
    decode_rtcm_msm7_full(payload, payload_len, gnss_name, msg_type);
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

void decode_rtcm_1007(const unsigned char *payload, int payload_len) {
    int bit = 0;
    if (payload_len < 4) {
        rtcm_printf("Type 1007: Payload too short!\n");
        return;
    }

    uint16_t msg_number = (uint16_t)get_bits(payload, bit, 12); bit += 12; // Should be 1007
    uint16_t ref_station_id = (uint16_t)get_bits(payload, bit, 12); bit += 12;

    uint8_t desc_len = (uint8_t)get_bits(payload, bit, 8); bit += 8;
    if (payload_len < 4 + desc_len) {
        rtcm_printf("Type 1007: Payload too short for antenna descriptor!\n");
        return;
    }

    char descriptor[65] = {0};
    for (int i = 0; i < desc_len && i < 64; ++i) {
        descriptor[i] = (char)get_bits(payload, bit, 8); bit += 8;
    }
    descriptor[64] = '\0';

    uint8_t setup_id = (uint8_t)get_bits(payload, bit, 8); bit += 8;

    rtcm_printf("RTCM 1007:\n");
    rtcm_printf("  Message Number: %u\n", msg_number);
    rtcm_printf("  Reference Station ID: %u\n", ref_station_id);
    rtcm_printf("  Antenna Descriptor: %.*s\n", desc_len, descriptor);
    rtcm_printf("  Antenna Setup ID: %u\n", setup_id);
}

void decode_rtcm_1008(const unsigned char *payload, int payload_len) {
    int bit = 0;
    if (payload_len < 4) {
        rtcm_printf("Type 1008: Payload too short!\n");
        return;
    }

    uint16_t msg_number = (uint16_t)get_bits(payload, bit, 12); bit += 12; // Should be 1008
    uint16_t ref_station_id = (uint16_t)get_bits(payload, bit, 12); bit += 12;

    uint8_t desc_len = (uint8_t)get_bits(payload, bit, 8); bit += 8;
    if (payload_len < 4 + desc_len) {
        rtcm_printf("Type 1008: Payload too short for antenna descriptor!\n");
        return;
    }

    char descriptor[65] = {0};
    for (int i = 0; i < desc_len && i < 64; ++i) {
        descriptor[i] = (char)get_bits(payload, bit, 8); bit += 8;
    }
    descriptor[64] = '\0';

    uint8_t serial_len = (uint8_t)get_bits(payload, bit, 8); bit += 8;
    if (payload_len < 4 + desc_len + 1 + serial_len) {
        rtcm_printf("Type 1008: Payload too short for antenna serial!\n");
        return;
    }

    char serial[65] = {0};
    for (int i = 0; i < serial_len && i < 64; ++i) {
        serial[i] = (char)get_bits(payload, bit, 8); bit += 8;
    }
    serial[64] = '\0';

    rtcm_printf("RTCM 1008:\n");
    rtcm_printf("  Message Number: %u\n", msg_number);
    rtcm_printf("  Reference Station ID: %u\n", ref_station_id);
    rtcm_printf("  Antenna Descriptor: %.*s\n", desc_len, descriptor);
    rtcm_printf("  Antenna Serial Number: %.*s\n", serial_len, serial);
}

void decode_rtcm_1013(const unsigned char *payload, int payload_len) {
    int bit = 0;

    /* Header: 12+12+16+17+5 = 62 bits = 8 bytes minimum */
    if (payload_len < 8) {
        rtcm_printf("Type 1013: Payload too short (%d bytes)!\n", payload_len);
        return;
    }

    uint16_t msg_number      = (uint16_t)get_bits(payload, bit, 12); bit += 12;
    uint16_t ref_station_id  = (uint16_t)get_bits(payload, bit, 12); bit += 12;
    uint16_t mjd             = (uint16_t)get_bits(payload, bit, 16); bit += 16;
    uint32_t seconds_of_day  = (uint32_t)get_bits(payload, bit, 17); bit += 17;
    uint8_t  n_announcements = (uint8_t) get_bits(payload, bit,  5); bit += 5;

    /* Convert MJD + seconds to human-readable date/time */
    /* MJD epoch: November 17, 1858 (Julian day 2400000.5) */
    /* Convert MJD to calendar date using standard algorithm */
    long jd = (long)mjd + 2400001L;
    long l = jd + 68569L;
    long n_val = 4L * l / 146097L;
    l = l - (146097L * n_val + 3L) / 4L;
    long i_val = 4000L * (l + 1L) / 1461001L;
    l = l - 1461L * i_val / 4L + 31L;
    long j_val = 80L * l / 2447L;
    int day   = (int)(l - 2447L * j_val / 80L);
    l = j_val / 11L;
    int month = (int)(j_val + 2L - 12L * l);
    int year  = (int)(100L * (n_val - 49L) + i_val + l);

    int hours   = (int)(seconds_of_day / 3600);
    int minutes = (int)((seconds_of_day % 3600) / 60);
    int secs    = (int)(seconds_of_day % 60);

    rtcm_printf("=== RTCM 1013 — System Parameters ===\n\n");
    rtcm_printf("  Message Number      : %u\n", msg_number);
    rtcm_printf("  Reference Station ID: %u\n", ref_station_id);
    rtcm_printf("  Modified Julian Day : %u  (%04d-%02d-%02d)\n",
                mjd, year, month, day);
    rtcm_printf("  Seconds of Day      : %u  (%02d:%02d:%02d UTC)\n",
                seconds_of_day, hours, minutes, secs);
    rtcm_printf("  Message Announcements : %u\n\n", n_announcements);

    if (n_announcements > 0) {
        /* Each announcement: 12+1+16 = 29 bits */
        int bits_needed = bit + n_announcements * 29;
        if (bits_needed > payload_len * 8) {
            rtcm_printf("  [WARNING] Payload too short for %u announcements\n",
                        n_announcements);
            n_announcements = (uint8_t)((payload_len * 8 - bit) / 29);
        }

        rtcm_printf("  %-8s  %-6s  %s\n", "Msg ID", "Sync", "Interval (s)");
        rtcm_printf("  %-8s  %-6s  %s\n", "------", "----", "------------");

        for (int i = 0; i < n_announcements; i++) {
            uint16_t announced_id = (uint16_t)get_bits(payload, bit, 12); bit += 12;
            uint8_t  sync_flag    = (uint8_t) get_bits(payload, bit,  1); bit += 1;
            uint16_t interval_raw = (uint16_t)get_bits(payload, bit, 16); bit += 16;

            /* Interval is in 0.1 second units */
            double interval_sec = interval_raw * 0.1;

            rtcm_printf("  %-8u  %-6s  %.1f\n",
                        announced_id,
                        sync_flag ? "Yes" : "No",
                        interval_sec);
        }
    }
}

void decode_rtcm_1033(const unsigned char *payload, int payload_len) {
    int bit = 0;
    if (payload_len < 8) {
        rtcm_printf("Type 1033: Payload too short!\n");
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

    rtcm_printf("RTCM 1033 (Receiver & Antenna Descriptor):\n");
    rtcm_printf("  Message Number: %u\n", msg_number);
    rtcm_printf("  Reference Station ID: %u\n", ref_station_id);
    rtcm_printf("  Antenna Descriptor: %.*s\n", ant_desc_len, ant_desc);
    rtcm_printf("  Antenna Serial Number: %.*s\n", ant_serial_len, ant_serial);
    rtcm_printf("  Receiver Type: %.*s\n", recv_type_len, recv_type);
    rtcm_printf("  Receiver Serial Number: %.*s\n", recv_serial_len, recv_serial);
}

void decode_rtcm_1045(const unsigned char *payload, int payload_len) {
    int bit = 0;
    if (payload_len < 48) { // Minimum length for RTCM 1045 (Galileo Ephemeris) is 48 bytes
        rtcm_printf("Type 1045: Payload too short!\n");
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

    rtcm_printf("RTCM 1045 (Galileo F/NAV Ephemeris):\n");
    rtcm_printf("  Message Number: %u\n", msg_number);
    rtcm_printf("  Satellite ID (SVID): %u\n", svid);
    rtcm_printf("  Week Number: %u\n", week);
    rtcm_printf("  IODnav: %u\n", iodnav);
    rtcm_printf("  SISA: %u\n", sisa);
    rtcm_printf("  IDOT: %d\n", idot);
    rtcm_printf("  Delta n: %d\n", delta_n);
    rtcm_printf("  M0: %d\n", m0);
    rtcm_printf("  Eccentricity: %u\n", e);
    rtcm_printf("  sqrtA: %u\n", sqrtA);
    rtcm_printf("  Omega0: %d\n", omega0);
    rtcm_printf("  i0: %d\n", i0);
    rtcm_printf("  omega: %d\n", omega);
    rtcm_printf("  OmegaDot: %d\n", omega_dot);
    rtcm_printf("  Cuc: %d\n", cuc);
    rtcm_printf("  Cus: %d\n", cus);
    rtcm_printf("  Crc: %d\n", crc);
    rtcm_printf("  Crs: %d\n", crs);
    rtcm_printf("  Cic: %d\n", cic);
    rtcm_printf("  Cis: %d\n", cis);
    rtcm_printf("  Toe: %u\n", toe);
    rtcm_printf("  BGD E5a/E1: %d\n", bgd_e5a_e1);
    rtcm_printf("  BGD E5b/E1: %d\n", bgd_e5b_e1);
    rtcm_printf("  Health/Status: %u\n", health);
}

void decode_rtcm_1230(const unsigned char *payload, int payload_len) {
    int bit = 0;
    if (payload_len < 4) {
        rtcm_printf("Type 1230: Payload too short!\n");
        return;
    }

    uint16_t msg_number = (uint16_t)get_bits(payload, bit, 12); bit += 12; // Should be 1230
    uint16_t ref_station_id = (uint16_t)get_bits(payload, bit, 12); bit += 12;
    uint8_t num_sats = (uint8_t)get_bits(payload, bit, 6); bit += 6;

    rtcm_printf("RTCM 1230 (GLONASS L1/L2 Code-Phase Biases):\n");
    rtcm_printf("  Message Number: %u\n", msg_number);
    rtcm_printf("  Reference Station ID: %u\n", ref_station_id);
    rtcm_printf("  Number of Satellites: %u\n", num_sats);

    for (int i = 0; i < num_sats; ++i) {
        if ((bit + 22) > payload_len * 8) {
            rtcm_printf("  [WARN] Not enough data for satellite %d\n", i + 1);
            break;
        }
        uint8_t sat_id = (uint8_t)get_bits(payload, bit, 6); bit += 6;
        int16_t bias = (int16_t)get_bits(payload, bit, 16); bit += 16;
        double bias_ns = bias * 0.01; // Convert to nanoseconds

        rtcm_printf("    Satellite %d: Slot ID = %u, L1-L2 Code-Phase Bias = %.2f ns\n", i + 1, sat_id, bias_ns);
    }
}

void decode_rtcm_1012(const unsigned char *payload, int payload_len) {
    int bit = 0;
    int msg_type = (int)get_bits(payload, bit, 12); bit += 12;
    if (msg_type != 1012) {
        rtcm_printf("[1012] Not a 1012 message (got %d)\n", msg_type);
        return;
    }

    int ref_station_id = (int)get_bits(payload, bit, 12); bit += 12;
    int epoch_time = (int)get_bits(payload, bit, 27); bit += 27;
    int sync_gnss_flag = (int)get_bits(payload, bit, 1); bit += 1;
    int num_satellites = (int)get_bits(payload, bit, 6); bit += 6;
    int smoothing = (int)get_bits(payload, bit, 1); bit += 1;
    int smoothing_interval = (int)get_bits(payload, bit, 3); bit += 3;

    rtcm_printf("RTCM 1012 (GLONASS L1&L2 RTK Observables)\n");
    rtcm_printf("  Reference Station ID: %d\n", ref_station_id);
    rtcm_printf("  Epoch Time: %d\n", epoch_time);
    rtcm_printf("  Synchronous GNSS Flag: %d\n", sync_gnss_flag);
    rtcm_printf("  Number of GLONASS Satellites: %d\n", num_satellites);
    rtcm_printf("  Smoothing: %d\n", smoothing);
    rtcm_printf("  Smoothing Interval: %d\n", smoothing_interval);

    for (int i = 0; i < num_satellites; ++i) {
        int sat_id = (int)get_bits(payload, bit, 6); bit += 6;
        int l1_code_ind = (int)get_bits(payload, bit, 1); bit += 1;
        int l1_pseudorange = (int)get_bits(payload, bit, 25); bit += 25;
        int l1_phase_range = (int)get_bits(payload, bit, 20); bit += 20;
        int l1_lock_time = (int)get_bits(payload, bit, 7); bit += 7;
        int l1_ambiguity = (int)get_bits(payload, bit, 7); bit += 7;
        int l1_cnr = (int)get_bits(payload, bit, 8); bit += 8;

        int l2_code_ind = (int)get_bits(payload, bit, 2); bit += 2;
        int l2_pseudorange_diff = (int)get_bits(payload, bit, 14); bit += 14;
        int l2_phase_range_diff = (int)get_bits(payload, bit, 20); bit += 20;
        int l2_lock_time = (int)get_bits(payload, bit, 7); bit += 7;
        int l2_cnr = (int)get_bits(payload, bit, 8); bit += 8;

        rtcm_printf("  Satellite %d:\n", i + 1);
        rtcm_printf("    Satellite ID: %d\n", sat_id);
        rtcm_printf("    L1 Code Indicator: %d\n", l1_code_ind);
        rtcm_printf("    L1 Pseudorange: %d\n", l1_pseudorange);
        rtcm_printf("    L1 Phase Range: %d\n", l1_phase_range);
        rtcm_printf("    L1 Lock Time Indicator: %d\n", l1_lock_time);
        rtcm_printf("    L1 Ambiguity: %d\n", l1_ambiguity);
        rtcm_printf("    L1 CNR: %d\n", l1_cnr);
        rtcm_printf("    L2 Code Indicator: %d\n", l2_code_ind);
        rtcm_printf("    L2 Pseudorange Diff: %d\n", l2_pseudorange_diff);
        rtcm_printf("    L2 Phase Range Diff: %d\n", l2_phase_range_diff);
        rtcm_printf("    L2 Lock Time Indicator: %d\n", l2_lock_time);
        rtcm_printf("    L2 CNR: %d\n", l2_cnr);
    }
}

int analyze_rtcm_message(const unsigned char *data, int length, bool suppress_output,const NTRIP_Config *config) {
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
                rtcm_printf("\nRTCM Message: Type = %d, Length = %d (Type 1005 detected)\n", msg_type, msg_length);
                decode_rtcm_1005(&data[3], msg_length, config);
            } else if (msg_type == 1006) {
                rtcm_printf("\nRTCM Message: Type = %d, Length = %d (Type 1006 detected)\n", msg_type, msg_length);
                decode_rtcm_1006(&data[3], msg_length, config);
            } else if (msg_type == 1019) {
                rtcm_printf("\nRTCM Message: Type = %d, Length = %d (Type 1019 detected)\n", msg_type, msg_length);
                decode_rtcm_1019(&data[3], msg_length);
            } else if (msg_type == 1077) {
                rtcm_printf("\nRTCM Message: Type = %d, Length = %d (Type 1077 detected)\n", msg_type, msg_length);
                decode_rtcm_1077(&data[3], msg_length);
            } else if (msg_type == 1074) {
                rtcm_printf("\nRTCM Message: Type = %d, Length = %d (Type 1074 detected)\n", msg_type, msg_length);
                decode_rtcm_msm4_generic(&data[3], msg_length, "GPS", 1074, 15, 22, 0.02, 0.0005);
            } else if (msg_type == 1084) {
                rtcm_printf("\nRTCM Message: Type = %d, Length = %d (Type 1084 detected)\n", msg_type, msg_length);
                decode_rtcm_msm4_generic(&data[3], msg_length, "GLONASS", 1084, 15, 22, 0.02, 0.0005);
            } else if (msg_type == 1094) {
                rtcm_printf("\nRTCM Message: Type = %d, Length = %d (Type 1094 detected)\n", msg_type, msg_length);
                decode_rtcm_msm4_generic(&data[3], msg_length, "Galileo", 1094, 15, 22, 0.02, 0.0005);
            } else if (msg_type == 1124) {
                rtcm_printf("\nRTCM Message: Type = %d, Length = %d (Type 1124 detected)\n", msg_type, msg_length);
                decode_rtcm_msm4_generic(&data[3], msg_length, "QZSS", 1124, 20, 24, 0.1, 0.0005);
            } else if (msg_type == 1087) {
                rtcm_printf("\nRTCM Message: Type = %d, Length = %d (Type 1087 detected)\n", msg_type, msg_length);
                decode_rtcm_1087(&data[3], msg_length);
            } else if (msg_type == 1097) {
                rtcm_printf("\nRTCM Message: Type = %d, Length = %d (Type 1097 detected)\n", msg_type, msg_length);
                decode_rtcm_1097(&data[3], msg_length);
            } else if (msg_type == 1117) {
                rtcm_printf("\nRTCM Message: Type = %d, Length = %d (Type 1117 detected)\n", msg_type, msg_length);
                decode_rtcm_1117(&data[3], msg_length);
            } else if (msg_type == 1127) {
                rtcm_printf("\nRTCM Message: Type = %d, Length = %d (Type 1127 detected)\n", msg_type, msg_length);
                decode_rtcm_1127(&data[3], msg_length);
            } else if (msg_type == 1137) {
                rtcm_printf("\nRTCM Message: Type = %d, Length = %d (Type 1137 detected)\n", msg_type, msg_length);
                decode_rtcm_1137(&data[3], msg_length);
            } else if (msg_type == 1007) {
                rtcm_printf("\nRTCM Message: Type = %d, Length = %d (Type 1007 detected)\n", msg_type, msg_length);
                decode_rtcm_1007(&data[3], msg_length);
            } else if (msg_type == 1008) {
                rtcm_printf("\nRTCM Message: Type = %d, Length = %d (Type 1008 detected)\n", msg_type, msg_length);
                decode_rtcm_1008(&data[3], msg_length);
            } else if (msg_type == 1013) {
                rtcm_printf("\nRTCM Message: Type = %d, Length = %d (Type 1013 detected)\n", msg_type, msg_length);
                decode_rtcm_1013(&data[3], msg_length);
            } else if (msg_type == 1033) {
                rtcm_printf("\nRTCM Message: Type = %d, Length = %d (Type 1033 detected)\n", msg_type, msg_length);
                decode_rtcm_1033(&data[3], msg_length);
            } else if (msg_type == 1045) {
                rtcm_printf("\nRTCM Message: Type = %d, Length = %d (Type 1045 detected)\n", msg_type, msg_length);
                decode_rtcm_1045(&data[3], msg_length);
            } else if (msg_type == 1230) {
                rtcm_printf("\nRTCM Message: Type = %d, Length = %d (Type 1230 detected)\n", msg_type, msg_length);
                decode_rtcm_1230(&data[3], msg_length);
            } else if (msg_type == 1012) {
                rtcm_printf("\nRTCM Message: Type = %d, Length = %d (Type 1012 detected)\n", msg_type, msg_length);
                decode_rtcm_1012(&data[3], msg_length);
            } else {
                if (length >= frame_len) {
                    if (crc_calc != crc_extracted) {
                        rtcm_printf("\nRTCM Message: Type = %d, Length = %d, CRC = 0x%06X (CRC FAIL! Calculated: 0x%06X)\n", msg_type, msg_length, crc_extracted, crc_calc);
                    }
                } else {
                    rtcm_printf("\nRTCM Message: Type = %d, Length = %d (frame incomplete)\n", msg_type, msg_length);
                }
            }

            if (length >= frame_len && crc_calc != crc_extracted) {
                rtcm_printf("  CRC check: FAIL | extracted: 0x%06X | calculated: 0x%06X\n", crc_extracted, crc_calc);
            }
        }

        return msg_type;
    } else {
        if (!suppress_output) {
            rtcm_printf("Non-RTCM or malformed data (first bytes): ");
            for (int i = 0; i < length && i < 16; ++i) {
                rtcm_printf("%02X ", data[i]);
            }
            rtcm_printf("\n");
        }
        return -1;
    }
}

void decode_rtcm_1019(const unsigned char *payload, int payload_len) {
    if (!payload || payload_len < 51) {
        rtcm_printf("RTCM 1019: Payload too short\n");
        return;
    }

    int bit = 0;
    uint32_t msg_type = (uint32_t)get_bits(payload, bit, 12); bit += 12;
    if (msg_type != 1019) {
        rtcm_printf("[1019] Not a 1019 message (got %d)\n", msg_type);
        return;
    }

    uint32_t prn = (uint32_t)get_bits(payload, bit, 6); bit += 6;
    uint32_t gps_week = (uint32_t)get_bits(payload, bit, 10); bit += 10;
    uint32_t sv_accuracy = (uint32_t)get_bits(payload, bit, 4); bit += 4;
    uint32_t code_on_l2 = (uint32_t)get_bits(payload, bit, 2); bit += 2;
    int16_t idot = (int16_t)extract_signed(payload, bit, 14); bit += 14;
    uint32_t iode = (uint32_t)get_bits(payload, bit, 8); bit += 8;
    uint32_t toc = (uint32_t)get_bits(payload, bit, 16); bit += 16;
    int8_t af2 = (int8_t)extract_signed(payload, bit, 8); bit += 8;
    int16_t af1 = (int16_t)extract_signed(payload, bit, 16); bit += 16;
    int32_t af0 = (int32_t)extract_signed(payload, bit, 22); bit += 22;
    uint32_t iodc = (uint32_t)get_bits(payload, bit, 10); bit += 10;
    int16_t crs = (int16_t)extract_signed(payload, bit, 16); bit += 16;
    int16_t delta_n = (int16_t)extract_signed(payload, bit, 16); bit += 16;
    int32_t m0 = (int32_t)extract_signed(payload, bit, 32); bit += 32;
    int16_t cuc = (int16_t)extract_signed(payload, bit, 16); bit += 16;
    int16_t cus = (int16_t)extract_signed(payload, bit, 16); bit += 16;
    int16_t crc = (int16_t)extract_signed(payload, bit, 16); bit += 16;
    int16_t crs2 = (int16_t)extract_signed(payload, bit, 16); bit += 16;
    int16_t cic = (int16_t)extract_signed(payload, bit, 16); bit += 16;
    int16_t cis = (int16_t)extract_signed(payload, bit, 16); bit += 16;
    uint32_t e = (uint32_t)get_bits(payload, bit, 32); bit += 32;
    uint32_t sqrtA = (uint32_t)get_bits(payload, bit, 32); bit += 32;
    uint32_t toe = (uint32_t)get_bits(payload, bit, 16); bit += 16;
    uint8_t fit_flag = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    uint8_t aodo = (uint8_t)get_bits(payload, bit, 5); bit += 5;
    uint8_t health = (uint8_t)get_bits(payload, bit, 6); bit += 6;
    int8_t tgd = (int8_t)extract_signed(payload, bit, 8); bit += 8;
    uint32_t tx_time = (uint32_t)get_bits(payload, bit, 16); bit += 16;
    uint8_t reserved = (uint8_t)get_bits(payload, bit, 2); bit += 2;

    // Apply scaling
    double idot_s = idot * pow(2, -43) * M_PI; // semi-circles/sec to rad/sec
    double toc_s = toc * pow(2, 4);
    double af2_s = af2 * pow(2, -55);
    double af1_s = af1 * pow(2, -43);
    double af0_s = af0 * pow(2, -31);
    double crs_s = crs * pow(2, -5);
    double delta_n_s = delta_n * pow(2, -43) * M_PI; // semi-circles/sec to rad/sec
    double m0_s = m0 * pow(2, -31) * M_PI; // semi-circles to radians
    double cuc_s = cuc * pow(2, -29);
    double cus_s = cus * pow(2, -29);
    double crc_s = crc * pow(2, -5);
    double crs2_s = crs2 * pow(2, -5);
    double cic_s = cic * pow(2, -29);
    double cis_s = cis * pow(2, -29);
    double e_s = e * pow(2, -33);
    double sqrtA_s = sqrtA * pow(2, -19);
    double toe_s = toe * pow(2, 4);
    double tgd_s = tgd * pow(2, -31);
    double tx_time_s = tx_time * pow(2, 4);

    rtcm_printf("RTCM 1019 (GPS Ephemeris):\n");
    rtcm_printf("  PRN: %u\n", prn);
    rtcm_printf("  GPS Week: %u\n", gps_week);
    rtcm_printf("  SV Accuracy: %u\n", sv_accuracy);
    rtcm_printf("  Code on L2: %u\n", code_on_l2);
    rtcm_printf("  IDOT: %g rad/s\n", idot_s);
    rtcm_printf("  IODE: %u\n", iode);
    rtcm_printf("  toc: %.0f s\n", toc_s);
    rtcm_printf("  af2: %.12g s/s^2\n", af2_s);
    rtcm_printf("  af1: %.12g s/s\n", af1_s);
    rtcm_printf("  af0: %.12g s\n", af0_s);
    rtcm_printf("  IODC: %u\n", iodc);
    rtcm_printf("  crs: %.3f m\n", crs_s);
    rtcm_printf("  delta n: %.12g rad/s\n", delta_n_s);
    rtcm_printf("  M0: %.12g rad\n", m0_s);
    rtcm_printf("  cuc: %.12g rad\n", cuc_s);
    rtcm_printf("  cus: %.12g rad\n", cus_s);
    rtcm_printf("  crc: %.3f m\n", crc_s);
    rtcm_printf("  crs (2): %.3f m\n", crs2_s);
    rtcm_printf("  cic: %.12g rad\n", cic_s);
    rtcm_printf("  cis: %.12g rad\n", cis_s);
    rtcm_printf("  e: %.15g\n", e_s);
    rtcm_printf("  sqrtA: %.8f m^0.5\n", sqrtA_s);
    rtcm_printf("  toe: %.0f s\n", toe_s);
    rtcm_printf("  fit interval flag: %u\n", fit_flag);
    rtcm_printf("  AODO: %u\n", aodo);
    rtcm_printf("  GNSS health: %u\n", health);
    rtcm_printf("  TGD: %.12g s\n", tgd_s);
    rtcm_printf("  Transmission time: %.0f s\n", tx_time_s);
    rtcm_printf("  Reserved: %u\n", reserved);
}

void decode_rtcm_1094(const unsigned char *payload, int payload_len) {
    int bit = 0;
    if (payload_len < 20) {
        rtcm_printf("Type 1094: Payload too short!\n");
        return;
    }

    uint16_t ref_station_id = (uint16_t)get_bits(payload, bit, 12); bit += 12;
    uint32_t epoch_time = (uint32_t)get_bits(payload, bit, 30); bit += 30;
    uint8_t mm_flag = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    uint8_t iods = (uint8_t)get_bits(payload, bit, 3); bit += 3;
    bit += 7; // reserved
    uint8_t clk_steering = (uint8_t)get_bits(payload, bit, 2); bit += 2;
    uint8_t ext_clk = (uint8_t)get_bits(payload, bit, 2); bit += 2;
    uint8_t sync_gnss = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    uint8_t num_sats = (uint8_t)get_bits(payload, bit, 6); bit += 6;
    uint8_t num_sigs = (uint8_t)get_bits(payload, bit, 6); bit += 6;

    // Satellite mask (num_sats bits)
    uint64_t sat_mask = 0;
    for (int i = 0; i < num_sats; ++i) {
        sat_mask = (sat_mask << 1) | get_bits(payload, bit++, 1);
    }

    // Signal mask (num_sigs bits)
    uint32_t sig_mask = 0;
    for (int i = 0; i < num_sigs; ++i) {
        sig_mask = (sig_mask << 1) | get_bits(payload, bit++, 1);
    }

    // Cell mask (num_sats * num_sigs bits)
    int num_cells = 0;
    int cell_mask_start = bit;
    for (int i = 0; i < num_sats * num_sigs; ++i)
        if (get_bits(payload, cell_mask_start + i, 1)) num_cells++;
    bit += num_sats * num_sigs;

    rtcm_printf("RTCM 1094 MSM4 (GLONASS):\n");
    rtcm_printf("  Reference Station ID: %u\n", ref_station_id);
    rtcm_printf("  Epoch Time: %u ms\n", epoch_time);
    rtcm_printf("  Multiple Message Flag: %u\n", mm_flag);
    rtcm_printf("  IODS: %u\n", iods);
    rtcm_printf("  Clock Steering: %u, Ext Clock: %u\n", clk_steering, ext_clk);
    rtcm_printf("  Synchronous GNSS: %u\n", sync_gnss);
    rtcm_printf("  Satellites: %u, Signals: %u, Cells: %d\n", num_sats, num_sigs, num_cells);

    // MSM4: Only pseudorange, phase range, lock, half-cycle, CNR
    for (int cell = 0; cell < num_cells && cell < 5; ++cell) {
        int32_t pseudorange = (int32_t)get_bits(payload, bit, 15); bit += 15;
        int32_t phaserange = (int32_t)get_bits(payload, bit, 22); bit += 22;
        uint8_t lock = (uint8_t)get_bits(payload, bit, 4); bit += 4;
        uint8_t half_cycle = (uint8_t)get_bits(payload, bit, 1); bit += 1;
        uint8_t cnr = (uint8_t)get_bits(payload, bit, 6); bit += 6;

        if (pseudorange & (1 << 14)) pseudorange -= (1 << 15);
        if (phaserange & (1 << 21)) phaserange -= (1 << 22);

        rtcm_printf("  Cell %d: PR=%.4f m, PH=%.4f m, Lock=%u, Half=%u, CNR=%u dBHz\n",
            cell + 1,
            pseudorange * 0.02,   // MSM4 scaling for pseudorange
            phaserange * 0.0005,  // MSM4 scaling for phase range
            lock,
            half_cycle,
            cnr
        );
    }
    if (num_cells > 5) rtcm_printf("  ... (%d more cells not shown)\n", num_cells - 5);
}

void decode_rtcm_1084(const unsigned char *payload, int payload_len) {
    int bit = 0;
    if (payload_len < 20) {
        rtcm_printf("Type 1084: Payload too short!\n");
        return;
    }

    uint16_t ref_station_id = (uint16_t)get_bits(payload, bit, 12); bit += 12;
    uint32_t epoch_time = (uint32_t)get_bits(payload, bit, 30); bit += 30;
    uint8_t mm_flag = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    uint8_t iods = (uint8_t)get_bits(payload, bit, 3); bit += 3;
    bit += 7; // session transmission time (reserved)
    uint8_t clk_steering = (uint8_t)get_bits(payload, bit, 2); bit += 2;
    uint8_t ext_clk = (uint8_t)get_bits(payload, bit, 2); bit += 2;
    uint8_t df_smoothing = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    uint8_t smoothing_int = (uint8_t)get_bits(payload, bit, 3); bit += 3;

    uint64_t sat_mask = get_bits(payload, bit, 64); bit += 64;
    uint32_t sig_mask = (uint32_t)get_bits(payload, bit, 32); bit += 32;

    // Count satellites and signals
    int num_sats = 0, num_sigs = 0;
    for (int i = 0; i < 64; ++i) if ((sat_mask >> (63 - i)) & 1) num_sats++;
    for (int i = 0; i < 32; ++i) if ((sig_mask >> (31 - i)) & 1) num_sigs++;

    // Cell mask
    int num_cells = 0;
    int cell_mask_start = bit;
    for (int i = 0; i < num_sats * num_sigs; ++i)
        if (get_bits(payload, cell_mask_start + i, 1)) num_cells++;
    bit += num_sats * num_sigs;

    // Print header
    rtcm_printf("RTCM 1084 MSM4 (GPS):\n");
    rtcm_printf("  Reference Station ID: %u\n", ref_station_id);
    rtcm_printf("  Epoch Time: %u ms\n", epoch_time);
    rtcm_printf("  Multiple Message Flag: %u\n", mm_flag);
    rtcm_printf("  IODS: %u\n", iods);
    rtcm_printf("  Clock Steering: %u, Ext Clock: %u\n", clk_steering, ext_clk);
    rtcm_printf("  Divergence-free Smoothing: %u, Smoothing Interval: %u\n", df_smoothing, smoothing_int);
    rtcm_printf("  Satellites: %d, Signals: %d, Cells: %d\n", num_sats, num_sigs, num_cells);

    // Print rough range and extended info for each satellite
    rtcm_printf("  Satellite rough ranges and extended info:\n");
    int sat_idx = 0;
    for (int i = 0; i < 64; ++i) {
        if ((sat_mask >> (63 - i)) & 1) {
            if ((bit + 8 + 4) > payload_len * 8) {
                rtcm_printf("    [WARN] Not enough data for satellite %d\n", i + 1);
                break;
            }
            int rough_range = (int)get_bits(payload, bit, 8); bit += 8;
            int ext_info = (int)get_bits(payload, bit, 4); bit += 4;
            rtcm_printf("    PRN %2d: Rough Range = %3d, Extended Info = %2d\n", i + 1, rough_range, ext_info);
            sat_idx++;
        }
    }

    // MSM4: Fine pseudoranges, fine phases, lock, half-cycle, CNR
    for (int cell = 0; cell < num_cells && cell < 5; ++cell) {
        if ((bit + 15 + 22 + 4 + 1 + 6) > payload_len * 8) {
            rtcm_printf("  [WARN] Not enough data for cell %d\n", cell + 1);
            break;
        }
        int32_t pseudorange = (int32_t)get_bits(payload, bit, 15); bit += 15;
        int32_t phaserange = (int32_t)get_bits(payload, bit, 22); bit += 22;
        uint8_t lock = (uint8_t)get_bits(payload, bit, 4); bit += 4;
        uint8_t half_cycle = (uint8_t)get_bits(payload, bit, 1); bit += 1;
        uint8_t cnr = (uint8_t)get_bits(payload, bit, 6); bit += 6;

        if (pseudorange & (1 << 14)) pseudorange -= (1 << 15);
        if (phaserange & (1 << 21)) phaserange -= (1 << 22);

        rtcm_printf("  Cell %d: PR=%.4f m, PH=%.4f m, Lock=%u, Half=%u, CNR=%u dBHz\n",
            cell + 1,
            pseudorange * 0.02,   // MSM4 scaling for pseudorange
            phaserange * 0.0005,  // MSM4 scaling for phase range
            lock,
            half_cycle,
            cnr
        );
    }
    if (num_cells > 5) rtcm_printf("  ... (%d more cells not shown)\n", num_cells - 5);
}

void decode_rtcm_1074(const unsigned char *payload, int payload_len) {
    int bit = 0;
    if (payload_len < 20) {
        rtcm_printf("Type 1074: Payload too short!\n");
        return;
    }

    uint16_t ref_station_id = (uint16_t)get_bits(payload, bit, 12); bit += 12;
    uint32_t epoch_time = (uint32_t)get_bits(payload, bit, 30); bit += 30;
    uint8_t mm_flag = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    uint8_t iods = (uint8_t)get_bits(payload, bit, 3); bit += 3;
    bit += 7; // reserved/session transmission time
    uint8_t clk_steering = (uint8_t)get_bits(payload, bit, 2); bit += 2;
    uint8_t ext_clk = (uint8_t)get_bits(payload, bit, 2); bit += 2;
    uint8_t df_smoothing = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    uint8_t smoothing_int = (uint8_t)get_bits(payload, bit, 3); bit += 3;

    uint64_t sat_mask = get_bits(payload, bit, 64); bit += 64;
    uint32_t sig_mask = (uint32_t)get_bits(payload, bit, 32); bit += 32;

    // Count satellites and signals
    int num_sats = 0, num_sigs = 0;
    for (int i = 0; i < 64; ++i) if ((sat_mask >> (63 - i)) & 1) num_sats++;
    for (int i = 0; i < 32; ++i) if ((sig_mask >> (31 - i)) & 1) num_sigs++;

    // Cell mask
    int num_cells = 0;
    int cell_mask_start = bit;
    for (int i = 0; i < num_sats * num_sigs; ++i)
        if (get_bits(payload, cell_mask_start + i, 1)) num_cells++;
    bit += num_sats * num_sigs;

    // Satellite Data: Rough Range (8 bits), Extended Info (4 bits)
    for (int i = 0, sat_idx = 0; i < 64; ++i) {
        if ((sat_mask >> (63 - i)) & 1) {
            (void)get_bits(payload, bit, 8); bit += 8;   // Rough Range
            (void)get_bits(payload, bit, 4); bit += 4;   // Extended Info
            sat_idx++;
        }
    }

    rtcm_printf("RTCM 1074 MSM4 (GPS):\n");
    rtcm_printf("  Reference Station ID: %u\n", ref_station_id);
    rtcm_printf("  Epoch Time: %u ms\n", epoch_time);
    rtcm_printf("  Multiple Message Flag: %u\n", mm_flag);
    rtcm_printf("  IODS: %u\n", iods);
    rtcm_printf("  Clock Steering: %u, Ext Clock: %u\n", clk_steering, ext_clk);
    rtcm_printf("  Divergence-free Smoothing: %u, Smoothing Interval: %u\n", df_smoothing, smoothing_int);
    rtcm_printf("  Satellites: %d, Signals: %d, Cells: %d\n", num_sats, num_sigs, num_cells);

    // MSM4: Fine pseudoranges, fine phases, lock, half-cycle, CNR
    for (int cell = 0; cell < num_cells && cell < 5; ++cell) {
        int32_t pseudorange = (int32_t)get_bits(payload, bit, 15); bit += 15;
        int32_t phaserange = (int32_t)get_bits(payload, bit, 22); bit += 22;
        uint8_t lock = (uint8_t)get_bits(payload, bit, 4); bit += 4;
        uint8_t half_cycle = (uint8_t)get_bits(payload, bit, 1); bit += 1;
        uint8_t cnr = (uint8_t)get_bits(payload, bit, 6); bit += 6;

        if (pseudorange & (1 << 14)) pseudorange -= (1 << 15);
        if (phaserange & (1 << 21)) phaserange -= (1 << 22);

        rtcm_printf("  Cell %d: PR=%.4f m, PH=%.4f m, Lock=%u, Half=%u, CNR=%u dBHz\n",
            cell + 1,
            pseudorange * 0.02,   // MSM4 scaling for pseudorange
            phaserange * 0.0005,  // MSM4 scaling for phase range
            lock,
            half_cycle,
            cnr
        );
    }
    if (num_cells > 5) rtcm_printf("  ... (%d more cells not shown)\n", num_cells - 5);
}

void decode_rtcm_1124(const unsigned char *payload, int payload_len) {
    int bit =  0;
    if (payload_len < 20) {
        rtcm_printf("Type 1124: Payload too short!\n");
        return;
    }

    uint16_t ref_station_id = (uint16_t)get_bits(payload, bit, 12); bit += 12;
    uint32_t epoch_time = (uint32_t)get_bits(payload, bit, 30); bit += 30;
    uint8_t mm_flag = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    uint8_t iods = (uint8_t)get_bits(payload, bit, 3); bit += 3;
    bit += 7; // reserved/session transmission time
    uint8_t clk_steering = (uint8_t)get_bits(payload, bit, 2); bit += 2;
    uint8_t ext_clk = (uint8_t)get_bits(payload, bit, 2); bit += 2;
    uint8_t df_smoothing = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    uint8_t smoothing_int = (uint8_t)get_bits(payload, bit, 3); bit += 3;

    uint64_t sat_mask = get_bits(payload, bit, 64); bit += 64;
    uint32_t sig_mask = (uint32_t)get_bits(payload, bit, 32); bit += 32;

    // Count satellites and signals
    int num_sats = 0, num_sigs = 0;
    for (int i = 0; i < 64; ++i) if ((sat_mask >> (63 - i)) & 1) num_sats++;
    for (int i = 0; i < 32; ++i) if ((sig_mask >> (31 - i)) & 1) num_sigs++;

    // Cell mask
    int num_cells = 0;
    int cell_mask_start = bit;
    for (int i = 0; i < num_sats * num_sigs; ++i)
        if (get_bits(payload, cell_mask_start + i, 1)) num_cells++;
    bit += num_sats * num_sigs;

    // Satellite Data: Rough Range (8 bits), Extended Info (4 bits)
    for (int i = 0, sat_idx = 0; i < 64; ++i) {
        if ((sat_mask >> (63 - i)) & 1) {
            (void)get_bits(payload, bit, 8); bit += 8;   // Rough Range
            (void)get_bits(payload, bit, 4); bit += 4;   // Extended Info
            sat_idx++;
        }
    }

    rtcm_printf("RTCM 1124 MSM4 (QZSS):\n");
    rtcm_printf("  Reference Station ID: %u\n", ref_station_id);
    rtcm_printf("  Epoch Time: %u ms\n", epoch_time);
    rtcm_printf("  Multiple Message Flag: %u\n", mm_flag);
    rtcm_printf("  IODS: %u\n", iods);
    rtcm_printf("  Clock Steering: %u, Ext Clock: %u\n", clk_steering, ext_clk);
    rtcm_printf("  Divergence-free Smoothing: %u, Smoothing Interval: %u\n", df_smoothing, smoothing_int);
    rtcm_printf("  Satellites: %d, Signals: %d, Cells: %d\n", num_sats, num_sigs, num_cells);

    // Signal Data: For each cell (sat-sig pair in cell mask)
    for (int cell = 0; cell < num_cells && cell < 5; ++cell) {
        int32_t pseudorange = (int32_t)get_bits(payload, bit, 20); bit += 20;
        int32_t phaserange = (int32_t)get_bits(payload, bit, 24); bit += 24;
        uint8_t lock = (uint8_t)get_bits(payload, bit, 4); bit += 4;
        uint8_t half_cycle = (uint8_t)get_bits(payload, bit, 1); bit += 1;
        uint8_t cnr = (uint8_t)get_bits(payload, bit, 6); bit += 6;

        if (pseudorange & (1 << 19)) pseudorange -= (1 << 20);
        if (phaserange & (1 << 23)) phaserange -= (1 << 24);

        rtcm_printf("  Cell %d: PR=%.1f m, PH=%.4f m, Lock=%u, Half=%u, CNR=%u dBHz\n",
            cell + 1,
            pseudorange * 0.1,   // MSM4 scaling for pseudorange (0.1 m)
            phaserange * 0.0005, // MSM4 scaling for phase range (0.0005 m)
            lock,
            half_cycle,
            cnr
        );
    }
    if (num_cells > 5) rtcm_printf("  ... (%d more cells not shown)\n", num_cells - 5);
}

void decode_rtcm_msm4_generic(const unsigned char *payload, int payload_len,
                              const char *gnss_name, int msg_type,
                              int pr_bits, int ph_bits, double pr_scale, double ph_scale)
{
    int bit = 0;
    if (payload_len < 20) {
        rtcm_printf("Type %d: Payload too short!\n", msg_type);
        return;
    }

    uint16_t ref_station_id = (uint16_t)get_bits(payload, bit, 12); bit += 12;
    uint32_t epoch_time = (uint32_t)get_bits(payload, bit, 30); bit += 30;
    uint8_t mm_flag = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    uint8_t iods = (uint8_t)get_bits(payload, bit, 3); bit += 3;
    bit += 7; // reserved/session transmission time
    uint8_t clk_steering = (uint8_t)get_bits(payload, bit, 2); bit += 2;
    uint8_t ext_clk = (uint8_t)get_bits(payload, bit, 2); bit += 2;
    uint8_t df_smoothing = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    uint8_t smoothing_int = (uint8_t)get_bits(payload, bit, 3); bit += 3;

    uint64_t sat_mask = get_bits(payload, bit, 64); bit += 64;
    uint32_t sig_mask = (uint32_t)get_bits(payload, bit, 32); bit += 32;

    // Count satellites and signals
    int num_sats = 0, num_sigs = 0;
    for (int i = 0; i < 64; ++i) if ((sat_mask >> (63 - i)) & 1) num_sats++;
    for (int i = 0; i < 32; ++i) if ((sig_mask >> (31 - i)) & 1) num_sigs++;

    // Cell mask
    int num_cells = 0;
    int cell_mask_start = bit;
    for (int i = 0; i < num_sats * num_sigs; ++i)
        if (get_bits(payload, cell_mask_start + i, 1)) num_cells++;
    bit += num_sats * num_sigs;

    rtcm_printf("RTCM %d MSM4 (%s):\n", msg_type, gnss_name);
    rtcm_printf("  Reference Station ID: %u\n", ref_station_id);
    rtcm_printf("  Epoch Time: %u ms\n", epoch_time);
    rtcm_printf("  Multiple Message Flag: %u\n", mm_flag);
    rtcm_printf("  IODS: %u\n", iods);
    rtcm_printf("  Clock Steering: %u, Ext Clock: %u\n", clk_steering, ext_clk);
    rtcm_printf("  Divergence-free Smoothing: %u, Smoothing Interval: %u\n", df_smoothing, smoothing_int);
    rtcm_printf("  Satellites: %d, Signals: %d, Cells: %d\n", num_sats, num_sigs, num_cells);

    // Print rough range and extended info for each satellite
    rtcm_printf("  Satellite rough ranges and extended info:\n");
    int sat_idx = 0;
    for (int i = 0; i < 64; ++i) {
        if ((sat_mask >> (63 - i)) & 1) {
            if ((bit + 8 + 4) > payload_len * 8) {
                rtcm_printf("    [WARN] Not enough data for satellite %d\n", i + 1);
                break;
            }
            int rough_range = (int)get_bits(payload, bit, 8); bit += 8;
            int ext_info = (int)get_bits(payload, bit, 4); bit += 4;
            rtcm_printf("    PRN %2d: Rough Range = %3d, Extended Info = %2d\n", i + 1, rough_range, ext_info);
            sat_idx++;
        }
    }

    // MSM4: Fine pseudoranges, fine phases, lock, half-cycle, CNR
    for (int cell = 0; cell < num_cells && cell < 5; ++cell) {
        if ((bit + pr_bits + ph_bits + 4 + 1 + 6) > payload_len * 8) {
            rtcm_printf("  [WARN] Not enough data for cell %d\n", cell + 1);
            break;
        }
        int32_t pseudorange = (int32_t)get_bits(payload, bit, pr_bits); bit += pr_bits;
        int32_t phaserange = (int32_t)get_bits(payload, bit, ph_bits); bit += ph_bits;
        uint8_t lock = (uint8_t)get_bits(payload, bit, 4); bit += 4;
        uint8_t half_cycle = (uint8_t)get_bits(payload, bit, 1); bit += 1;
        uint8_t cnr = (uint8_t)get_bits(payload, bit, 6); bit += 6;

        // Sign extension for signed values
        if (pseudorange & (1 << (pr_bits - 1))) pseudorange -= (1 << pr_bits);
        if (phaserange & (1 << (ph_bits - 1))) phaserange -= (1 << ph_bits);

        rtcm_printf("  Cell %d: PR=%.4f m, PH=%.4f m, Lock=%u, Half=%u, CNR=%u dBHz\n",
            cell + 1,
            pseudorange * pr_scale,
            phaserange * ph_scale,
            lock,
            half_cycle,
            cnr
        );
    }
    if (num_cells > 5) rtcm_printf("  ... (%d more cells not shown)\n", num_cells - 5);
}


