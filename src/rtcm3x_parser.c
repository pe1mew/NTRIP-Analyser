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
#include <string.h>
#include <math.h>
#include "rtcm3x_parser.h"
#include "sv_ephemeris.h"

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
 * @brief Printf replacement that writes to g_rtcm_strbuf when set, otherwise outputs to stdout.
 * 
 * This function provides redirectable output for all RTCM decode functions. When a string buffer
 * is set via rtcm_set_output_buffer(), all output is captured into that buffer. Otherwise, 
 * output goes to stdout. The buffer automatically expands if needed.
 * 
 * @param fmt Printf-style format string.
 * @param ... Variable arguments matching the format string.
 * 
 * @note This is an internal function used by all decode_rtcm_*() functions.
 * @see rtcm_set_output_buffer()
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

int64_t extract_signed38(const unsigned char *buf, int start_bit) {
    uint64_t raw = get_bits(buf, start_bit, 38);
    if ((raw >> 37) & 1) {
        // Negative number
        return -((int64_t)((1ULL << 38) - raw));
    } else {
        return (int64_t)raw;
    }
}

int64_t extract_signed(const unsigned char *buf, int start_bit, int bit_len) {
    uint64_t val = get_bits(buf, start_bit, bit_len);
    // Sign-extend if needed
    if (val & ((uint64_t)1 << (bit_len - 1))) {
        val |= (~0ULL) << bit_len;
    }
    return (int64_t)val;
}

int msm_extract_prns(const unsigned char *payload, int payload_len,
                     int msg_type, int *prns_out, int max_prns,
                     int *gnss_id_out)
{
    if (!payload || payload_len < 14 || !prns_out || max_prns <= 0) return 0;

    /* MSM4/5/6/7 only — message numbers ending in 4..7 of any GNSS range */
    int subtype = msg_type % 10;
    if (msg_type < 1070 || msg_type > 1139 || subtype < 4 || subtype > 7) return 0;

    /* Map RTCM range to GNSS ID, matching get_gnss_id_from_rtcm() */
    int gnss_id;
    if      (msg_type >= 1070 && msg_type <  1080) gnss_id = 1;   /* GPS */
    else if (msg_type >= 1080 && msg_type <  1090) gnss_id = 2;   /* GLONASS */
    else if (msg_type >= 1090 && msg_type < 1100)  gnss_id = 3;   /* Galileo */
    else if (msg_type >= 1100 && msg_type < 1110)  gnss_id = 6;   /* SBAS */
    else if (msg_type >= 1110 && msg_type < 1120)  gnss_id = 4;   /* QZSS */
    else if (msg_type >= 1120 && msg_type < 1130)  gnss_id = 5;   /* BeiDou */
    else if (msg_type >= 1130 && msg_type < 1140)  gnss_id = 7;   /* NavIC */
    else return 0;
    if (gnss_id_out) *gnss_id_out = gnss_id;

    /* MSM header layout (RTCM 10403.3, fixed 169-bit header):
     *   12 msg_number, 12 ref_station_id, 30 epoch_time, 1 MM, 3 IODS,
     *    7 reserved, 2 clk_steering, 2 ext_clk, 1 smoothing, 3 smoothing_interval,
     *   64 satellite_mask, 32 signal_mask, ...cell_mask follows
     * Total before satellite_mask = 12+12+30+1+3+7+2+2+1+3 = 73 bits.
     */
    int bit = 73;
    if ((bit + 64) > payload_len * 8) return 0;

    uint64_t sat_mask = get_bits(payload, bit, 64);

    int count = 0;
    for (int i = 0; i < 64 && count < max_prns; i++) {
        /* MSB-first: bit (63-i) corresponds to satellite (i+1). */
        if ((sat_mask >> (63 - i)) & 1ULL) {
            prns_out[count++] = i + 1;
        }
    }
    return count;
}

/* ── Reference-station ARP cache ──────────────────────────────────────────
 * Populated by decode_rtcm_1005 / 1006 every time the station broadcasts
 * its antenna reference point.  Read by the GUI worker thread when
 * computing satellite az/el for the Sky Plot window.
 *
 * Single-threaded by convention: written and read on the worker.  The UI
 * thread's re-decode-for-display path also writes to it, but a torn read
 * is harmless for our purposes. */
static struct {
    bool   valid;
    double x, y, z;        /* ECEF metres */
    double lat_deg;
    double lon_deg;
    double alt_m;
} g_station_arp = { false, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };

void rtcm_get_station_arp(bool *valid,
                          double *x, double *y, double *z,
                          double *lat_deg, double *lon_deg, double *alt_m)
{
    if (valid)   *valid   = g_station_arp.valid;
    if (x)       *x       = g_station_arp.x;
    if (y)       *y       = g_station_arp.y;
    if (z)       *z       = g_station_arp.z;
    if (lat_deg) *lat_deg = g_station_arp.lat_deg;
    if (lon_deg) *lon_deg = g_station_arp.lon_deg;
    if (alt_m)   *alt_m   = g_station_arp.alt_m;
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

void geodetic_to_ecef(double lat_deg, double lon_deg, double alt_m,
                      double *x, double *y, double *z)
{
    const double a  = 6378137.0;
    const double e2 = 6.69437999014e-3;
    double lat = lat_deg * M_PI / 180.0;
    double lon = lon_deg * M_PI / 180.0;
    double sl  = sin(lat), cl  = cos(lat);
    double slo = sin(lon), clo = cos(lon);
    double N   = a / sqrt(1.0 - e2 * sl * sl);
    if (x) *x = (N + alt_m) * cl * clo;
    if (y) *y = (N + alt_m) * cl * slo;
    if (z) *z = (N * (1.0 - e2) + alt_m) * sl;
}

void ecef_to_enu(double lat_deg, double lon_deg,
                 double dx, double dy, double dz,
                 double *e, double *n, double *u)
{
    double lat = lat_deg * M_PI / 180.0;
    double lon = lon_deg * M_PI / 180.0;
    double sl  = sin(lat), cl  = cos(lat);
    double slo = sin(lon), clo = cos(lon);

    /* Rotation of an ECEF delta into the local ENU frame at (lat, lon). */
    if (e) *e = -slo * dx + clo * dy;
    if (n) *n = -sl * clo * dx - sl * slo * dy + cl * dz;
    if (u) *u =  cl * clo * dx + cl * slo * dy + sl * dz;
}

void enu_to_azel(double e, double n, double u,
                 double *az_deg, double *el_deg)
{
    double horiz = sqrt(e * e + n * n);
    double el = atan2(u, horiz);             /* -pi/2 .. +pi/2 */
    double az = atan2(e, n);                  /* -pi .. +pi, 0 = north, +E */
    if (az < 0) az += 2.0 * M_PI;             /* wrap to 0 .. 2*pi (clockwise from N) */

    if (el_deg) *el_deg = el * 180.0 / M_PI;
    if (az_deg) *az_deg = az * 180.0 / M_PI;
}

void azel_from_ecef(double sta_x, double sta_y, double sta_z,
                    double sv_x,  double sv_y,  double sv_z,
                    double *az_deg, double *el_deg)
{
    double lat, lon, alt;
    ecef_to_geodetic(sta_x, sta_y, sta_z, 0.0, &lat, &lon, &alt);

    double e, n, u;
    ecef_to_enu(lat, lon,
                sv_x - sta_x, sv_y - sta_y, sv_z - sta_z,
                &e, &n, &u);

    enu_to_azel(e, n, u, az_deg, el_deg);
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

    /* Cache station ARP for the Sky Plot az/el computation */
    g_station_arp.valid   = true;
    g_station_arp.x       = x;
    g_station_arp.y       = y;
    g_station_arp.z       = z;
    g_station_arp.lat_deg = lat_deg;
    g_station_arp.lon_deg = lon_deg;
    g_station_arp.alt_m   = alt;

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

    /* Cache station ARP for the Sky Plot az/el computation */
    g_station_arp.valid   = true;
    g_station_arp.x       = x;
    g_station_arp.y       = y;
    g_station_arp.z       = z;
    g_station_arp.lat_deg = lat_deg;
    g_station_arp.lon_deg = lon_deg;
    g_station_arp.alt_m   = alt;

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
 * Fully decodes the MSM7 (Multiple Signal Message, Type 7) structure per RTCM 3.3 specification.
 * MSM7 provides the highest resolution observations including pseudorange, carrier phase, 
 * Doppler, and signal strength measurements.
 * 
 * Structure: Header → Satellite data (per sat) → Signal/Cell data (per cell)
 *
 * MSM Header fields (displayed):
 *   - Reference Station ID        (12 bits)
 *   - Epoch Time                  (30 bits, GNSS time in milliseconds)
 *   - Multiple Message Flag       (1 bit)
 *   - Issue of Data Station       (3 bits, IODS)
 *   - Clock Steering              (2 bits)
 *   - External Clock              (2 bits)
 *   - Divergence-free Smoothing   (1 bit)
 *   - Smoothing Interval          (3 bits)
 *   - Satellite mask              (64 bits)
 *   - Signal mask                 (32 bits)
 *   - Cell mask                   (variable, num_sats × num_sigs bits)
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
 *
 * @param payload     Pointer to the message payload (after RTCM header).
 * @param payload_len Length of the payload in bytes.
 * @param gnss_name   Name of the GNSS constellation (e.g., "GPS", "GLONASS", "Galileo").
 * @param msg_type    RTCM message type number (e.g., 1077, 1087, 1097, etc.) for display purposes.
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

    rtcm_printf("  Reference Station ID  : %u\n", ref_station_id);
    rtcm_printf("  Epoch Time            : %u\n", epoch_time);
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

/**
 * @brief Internal wrapper for MSM7 decoding (GLONASS, Galileo, QZSS, BeiDou, SBAS).
 * 
 * This static helper function provides a common interface for all non-GPS MSM7 message types.
 * It wraps the full MSM7 decoder with constellation-specific parameters.
 * 
 * @param payload     Pointer to the message payload (after RTCM header).
 * @param payload_len Length of the payload in bytes.
 * @param gnss_name   Name of the GNSS constellation for display.
 * @param msg_type    RTCM message type number for display purposes.
 * 
 * @note This is an internal function used by decode_rtcm_1087(), decode_rtcm_1097(), 
 *       decode_rtcm_1117(), decode_rtcm_1127(), and decode_rtcm_1137().
 */
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
    if (payload_len < 8) { // Minimum size check (12+12+16+17+5+8 = 70 bits = 9 bytes minimum)
        printf("Type 1013: Payload too short!\n");
        return;
    }

    // DF002: Message Number (12 bits)
    uint16_t msg_number = (uint16_t)get_bits(payload, bit, 12); bit += 12;
    
    // DF003: Station ID (12 bits)
    uint16_t station_id = (uint16_t)get_bits(payload, bit, 12); bit += 12;
    
    // DF051: Modified Julian Day Number (16 bits)
    uint16_t mjd = (uint16_t)get_bits(payload, bit, 16); bit += 16;
    
    // DF052: Seconds of Day (17 bits)
    uint32_t seconds_of_day = (uint32_t)get_bits(payload, bit, 17); bit += 17;
    
    // DF053: Number of Messages (5 bits)
    uint8_t num_messages = (uint8_t)get_bits(payload, bit, 5); bit += 5;
    
    // DF054: Leap Seconds (8 bits)
    uint8_t leap_seconds = (uint8_t)get_bits(payload, bit, 8); bit += 8;

    printf("RTCM 1013 (System Parameters):\n");
    printf("  Message Number: %u\n", msg_number);
    printf("  Station ID: %u\n", station_id);
    printf("  Modified Julian Day (MJD): %u\n", mjd);
    printf("  Seconds of Day: %u (%.2f hours)\n", seconds_of_day, seconds_of_day / 3600.0);
    printf("  Leap Seconds: %u\n", leap_seconds);
    printf("  Number of Sync Messages: %u\n", num_messages);

    // Decode each message sync info
    for (int i = 0; i < num_messages; ++i) {
        if ((bit + 29) / 8 > payload_len) {
            printf("    Warning: Insufficient data for message %d\n", i + 1);
            break;
        }
        
        // DF055: Message Number (12 bits)
        uint16_t sync_msg_num = (uint16_t)get_bits(payload, bit, 12); bit += 12;
        
        // DF056: Sync Flag (1 bit)
        uint8_t sync_flag = (uint8_t)get_bits(payload, bit, 1); bit += 1;
        
        // DF057: Transmission Interval (16 bits)
        uint16_t interval = (uint16_t)get_bits(payload, bit, 16); bit += 16;
        
        printf("    Message %d: Type=%u, Sync=%s, Interval=%u\n", 
               i + 1, sync_msg_num, sync_flag ? "Yes" : "No", interval);
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

/**
 * @brief Read the common Galileo orbital block from an RTCM 1045/1046 payload.
 *
 * Both message types share the same 24 orbital + clock fields in the same
 * order; only the trailing BGD / health bits differ.  This helper:
 *   - extracts every field with the correct signedness,
 *   - applies the scaling factors specified in RTCM 10403.3 Table 3.5-22/23,
 *   - populates the orbital part of @p eph (gnss_id, prn, week, toe, toc,
 *     orbital elements, harmonic corrections, clock polynomial),
 *   - advances @p *bit by the consumed bit count.
 *
 * Caller sets eph->health afterwards based on the per-message trailing bits.
 *
 * @param p           Raw RTCM payload.
 * @param bit         [in/out] Bit cursor.
 * @param eph         [out] SvEphemeris to populate.
 * @param out_iodnav  [out, may be NULL] Raw IODnav for printing.
 * @param out_sisa    [out, may be NULL] Raw SISA index for printing.
 */
static void galileo_read_orbit_block(const unsigned char *p, int *bit,
                                     SvEphemeris *eph,
                                     uint32_t *out_iodnav, uint32_t *out_sisa)
{
    int b = *bit;
    uint32_t svid    = (uint32_t)get_bits(p, b, 6);          b += 6;
    uint32_t week    = (uint32_t)get_bits(p, b, 12);         b += 12;
    uint32_t iodnav  = (uint32_t)get_bits(p, b, 10);         b += 10;
    uint32_t sisa    = (uint32_t)get_bits(p, b, 8);          b += 8;
    int32_t  idot    = (int32_t) extract_signed(p, b, 14);   b += 14;
    uint32_t toc_raw = (uint32_t)get_bits(p, b, 14);         b += 14;
    int32_t  af2     = (int32_t) extract_signed(p, b, 6);    b += 6;
    int32_t  af1     = (int32_t) extract_signed(p, b, 21);   b += 21;
    int32_t  af0     = (int32_t) extract_signed(p, b, 31);   b += 31;
    int32_t  crs     = (int32_t) extract_signed(p, b, 16);   b += 16;
    int32_t  delta_n = (int32_t) extract_signed(p, b, 16);   b += 16;
    int32_t  m0      = (int32_t) extract_signed(p, b, 32);   b += 32;
    int32_t  cuc     = (int32_t) extract_signed(p, b, 16);   b += 16;
    uint64_t e_raw   =          get_bits(p, b, 32);          b += 32;
    int32_t  cus     = (int32_t) extract_signed(p, b, 16);   b += 16;
    uint64_t sqrtA   =          get_bits(p, b, 32);          b += 32;
    uint32_t toe_raw = (uint32_t)get_bits(p, b, 14);         b += 14;
    int32_t  cic     = (int32_t) extract_signed(p, b, 16);   b += 16;
    int32_t  omega0  = (int32_t) extract_signed(p, b, 32);   b += 32;
    int32_t  cis     = (int32_t) extract_signed(p, b, 16);   b += 16;
    int32_t  i0      = (int32_t) extract_signed(p, b, 32);   b += 32;
    int32_t  crc     = (int32_t) extract_signed(p, b, 16);   b += 16;
    int32_t  omega   = (int32_t) extract_signed(p, b, 32);   b += 32;
    int32_t  om_dot  = (int32_t) extract_signed(p, b, 24);   b += 24;
    *bit = b;

    memset(eph, 0, sizeof(*eph));
    eph->gnss_id      = 3;             /* Galileo */
    eph->prn          = (int)svid;
    eph->iode_iodnav  = (int)iodnav;
    eph->week         = (int)week;
    eph->toe          = (double)toe_raw * 60.0;
    eph->toc          = (double)toc_raw * 60.0;
    eph->sqrt_a       = (double)sqrtA   * pow(2.0, -19);
    eph->e            = (double)e_raw   * pow(2.0, -33);
    eph->i0           = (double)i0      * pow(2.0, -31) * M_PI;
    eph->omega0       = (double)omega0  * pow(2.0, -31) * M_PI;
    eph->omega        = (double)omega   * pow(2.0, -31) * M_PI;
    eph->m0           = (double)m0      * pow(2.0, -31) * M_PI;
    eph->delta_n      = (double)delta_n * pow(2.0, -43) * M_PI;
    eph->idot         = (double)idot    * pow(2.0, -43) * M_PI;
    eph->omega_dot    = (double)om_dot  * pow(2.0, -43) * M_PI;
    eph->cuc          = (double)cuc * pow(2.0, -29);
    eph->cus          = (double)cus * pow(2.0, -29);
    eph->crc          = (double)crc * pow(2.0, -5);
    eph->crs          = (double)crs * pow(2.0, -5);
    eph->cic          = (double)cic * pow(2.0, -29);
    eph->cis          = (double)cis * pow(2.0, -29);
    eph->af0          = (double)af0 * pow(2.0, -34);
    eph->af1          = (double)af1 * pow(2.0, -46);
    eph->af2          = (double)af2 * pow(2.0, -59);

    if (out_iodnav) *out_iodnav = iodnav;
    if (out_sisa)   *out_sisa   = sisa;
}

/**
 * @brief Print the shared orbital fields of a populated Galileo ephemeris.
 *
 * Used by both decode_rtcm_1045 and decode_rtcm_1046 to keep the output
 * consistent.  Per-message header / health lines are printed separately.
 */
static void galileo_print_orbit_block(const SvEphemeris *eph,
                                      uint32_t iodnav, uint32_t sisa)
{
    rtcm_printf("  SV: E%02d   Galileo Week: %d   IODnav: %u   SISA: %u\n",
                eph->prn, eph->week, iodnav, sisa);
    rtcm_printf("  toc: %.0f s of week   toe: %.0f s of week\n",
                eph->toc, eph->toe);
    rtcm_printf("  Clock: af0=%.6e s  af1=%.6e s/s  af2=%.6e s/s^2\n",
                eph->af0, eph->af1, eph->af2);
    rtcm_printf("  sqrt(A) = %.6f m^0.5    e = %.10g\n", eph->sqrt_a, eph->e);
    rtcm_printf("  i0      = %+.6f rad     OMEGA0 = %+.6f rad\n",
                eph->i0, eph->omega0);
    rtcm_printf("  omega   = %+.6f rad     M0     = %+.6f rad\n",
                eph->omega, eph->m0);
    rtcm_printf("  delta_n = %+.6e rad/s   idot   = %+.6e rad/s\n",
                eph->delta_n, eph->idot);
    rtcm_printf("  OMEGADOT = %+.6e rad/s\n", eph->omega_dot);
    rtcm_printf("  Harmonic: Cuc=%+.6e Cus=%+.6e Crc=%+.3f m Crs=%+.3f m\n",
                eph->cuc, eph->cus, eph->crc, eph->crs);
    rtcm_printf("            Cic=%+.6e Cis=%+.6e\n", eph->cic, eph->cis);
}

void decode_rtcm_1045(const unsigned char *payload, int payload_len) {
    /* RTCM 10403.3 Table 3.5-22 — Galileo F/NAV, 496 bits = 62 bytes. */
    if (!payload || payload_len < 62) {
        rtcm_printf("RTCM 1045: Payload too short (need 62 bytes, got %d)\n", payload_len);
        return;
    }

    int bit = 0;
    uint32_t msg_type = (uint32_t)get_bits(payload, bit, 12); bit += 12;
    if (msg_type != 1045) {
        rtcm_printf("[1045] Not a 1045 message (got %u)\n", msg_type);
        return;
    }

    SvEphemeris eph;
    uint32_t iodnav, sisa;
    galileo_read_orbit_block(payload, &bit, &eph, &iodnav, &sisa);

    /* Trailing F/NAV-specific fields */
    int32_t  bgd_e1_e5a = (int32_t) extract_signed(payload, bit, 10); bit += 10;
    uint32_t e5a_oshs   = (uint32_t)get_bits(payload, bit, 2);        bit += 2;
    uint32_t e5a_osdvs  = (uint32_t)get_bits(payload, bit, 1);        bit += 1;
    /* 7-bit reserved */

    double bgd_e1_e5a_s = (double)bgd_e1_e5a * pow(2.0, -32);

    /* Health: combine 2-bit OSHS and 1-bit OSDVS into a single field.
     * Bit 0 = OSHS LSB, bit 1 = OSHS MSB, bit 2 = OSDVS.  0 = nominal. */
    eph.health = (int)((e5a_osdvs << 2) | (e5a_oshs & 0x3));
    sv_eph_store(&eph);

    rtcm_printf("RTCM 1045 (Galileo F/NAV Ephemeris):\n");
    galileo_print_orbit_block(&eph, iodnav, sisa);
    rtcm_printf("  BGD E1-E5a = %.6e s   E5a OSHS=%u   E5a OSDVS=%u\n",
                bgd_e1_e5a_s, e5a_oshs, e5a_osdvs);
}

void decode_rtcm_1046(const unsigned char *payload, int payload_len) {
    /* RTCM 10403.3 Table 3.5-23 — Galileo I/NAV, 504 bits = 63 bytes. */
    if (!payload || payload_len < 63) {
        rtcm_printf("RTCM 1046: Payload too short (need 63 bytes, got %d)\n", payload_len);
        return;
    }

    int bit = 0;
    uint32_t msg_type = (uint32_t)get_bits(payload, bit, 12); bit += 12;
    if (msg_type != 1046) {
        rtcm_printf("[1046] Not a 1046 message (got %u)\n", msg_type);
        return;
    }

    SvEphemeris eph;
    uint32_t iodnav, sisa;
    galileo_read_orbit_block(payload, &bit, &eph, &iodnav, &sisa);

    /* Trailing I/NAV-specific fields */
    int32_t  bgd_e1_e5a = (int32_t) extract_signed(payload, bit, 10); bit += 10;
    int32_t  bgd_e1_e5b = (int32_t) extract_signed(payload, bit, 10); bit += 10;
    uint32_t e5b_shs    = (uint32_t)get_bits(payload, bit, 2);        bit += 2;
    uint32_t e5b_dvs    = (uint32_t)get_bits(payload, bit, 1);        bit += 1;
    uint32_t e1b_shs    = (uint32_t)get_bits(payload, bit, 2);        bit += 2;
    uint32_t e1b_dvs    = (uint32_t)get_bits(payload, bit, 1);        bit += 1;
    /* 2-bit reserved */

    double bgd_e1_e5a_s = (double)bgd_e1_e5a * pow(2.0, -32);
    double bgd_e1_e5b_s = (double)bgd_e1_e5b * pow(2.0, -32);

    /* Combine SHS/DVS bits into a single int — 0 = nominal.  Layout:
     * bits 0-1 = E5b SHS, bit 2 = E5b DVS, bits 3-4 = E1B SHS, bit 5 = E1B DVS. */
    eph.health = (int)((e1b_dvs << 5) | (e1b_shs << 3) |
                       (e5b_dvs << 2) | (e5b_shs & 0x3));
    sv_eph_store(&eph);

    rtcm_printf("RTCM 1046 (Galileo I/NAV Ephemeris):\n");
    galileo_print_orbit_block(&eph, iodnav, sisa);
    rtcm_printf("  BGD E1-E5a = %.6e s   BGD E1-E5b = %.6e s\n",
                bgd_e1_e5a_s, bgd_e1_e5b_s);
    rtcm_printf("  E5b SHS=%u DVS=%u   E1-B SHS=%u DVS=%u\n",
                e5b_shs, e5b_dvs, e1b_shs, e1b_dvs);
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
            } else if (msg_type == 1029) {
                rtcm_printf("\nRTCM Message: Type = %d, Length = %d (Type 1029 detected)\n", msg_type, msg_length);
                decode_rtcm_1029(&data[3], msg_length);
            } else if (msg_type == 1033) {
                rtcm_printf("\nRTCM Message: Type = %d, Length = %d (Type 1033 detected)\n", msg_type, msg_length);
                decode_rtcm_1033(&data[3], msg_length);
            } else if (msg_type == 1045) {
                rtcm_printf("\nRTCM Message: Type = %d, Length = %d (Type 1045 detected)\n", msg_type, msg_length);
                decode_rtcm_1045(&data[3], msg_length);
            } else if (msg_type == 1046) {
                rtcm_printf("\nRTCM Message: Type = %d, Length = %d (Type 1046 detected)\n", msg_type, msg_length);
                decode_rtcm_1046(&data[3], msg_length);
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

        /* Only return a valid msg_type when CRC passes.
         * This ensures callers (GUI stats, detail windows) only process
         * frames with verified integrity. */
        if (length >= (size_t)frame_len && crc_calc != crc_extracted) {
            return 0;
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
    /* RTCM 10403.3 Table 3.5-21 — GPS Ephemerides, 488 bits = 61 bytes. */
    if (!payload || payload_len < 61) {
        rtcm_printf("RTCM 1019: Payload too short (need 61 bytes, got %d)\n", payload_len);
        return;
    }

    int bit = 0;
    uint32_t msg_type = (uint32_t)get_bits(payload, bit, 12); bit += 12;
    if (msg_type != 1019) {
        rtcm_printf("[1019] Not a 1019 message (got %u)\n", msg_type);
        return;
    }

    /* ── Field extraction, in the order defined by the RTCM spec ── */
    uint32_t prn       = (uint32_t)get_bits(payload, bit, 6);          bit += 6;
    uint32_t gps_week  = (uint32_t)get_bits(payload, bit, 10);         bit += 10;
    uint32_t sv_acc    = (uint32_t)get_bits(payload, bit, 4);          bit += 4;
    uint32_t code_l2   = (uint32_t)get_bits(payload, bit, 2);          bit += 2;
    int32_t  idot      = (int32_t) extract_signed(payload, bit, 14);   bit += 14;
    uint32_t iode      = (uint32_t)get_bits(payload, bit, 8);          bit += 8;
    uint32_t toc_raw   = (uint32_t)get_bits(payload, bit, 16);         bit += 16;
    int32_t  af2       = (int32_t) extract_signed(payload, bit, 8);    bit += 8;
    int32_t  af1       = (int32_t) extract_signed(payload, bit, 16);   bit += 16;
    int32_t  af0       = (int32_t) extract_signed(payload, bit, 22);   bit += 22;
    uint32_t iodc      = (uint32_t)get_bits(payload, bit, 10);         bit += 10;
    int32_t  crs       = (int32_t) extract_signed(payload, bit, 16);   bit += 16;
    int32_t  delta_n   = (int32_t) extract_signed(payload, bit, 16);   bit += 16;
    int32_t  m0        = (int32_t) extract_signed(payload, bit, 32);   bit += 32;
    int32_t  cuc       = (int32_t) extract_signed(payload, bit, 16);   bit += 16;
    uint64_t e_raw     =          get_bits(payload, bit, 32);          bit += 32;
    int32_t  cus       = (int32_t) extract_signed(payload, bit, 16);   bit += 16;
    uint64_t sqrtA_raw =          get_bits(payload, bit, 32);          bit += 32;
    uint32_t toe_raw   = (uint32_t)get_bits(payload, bit, 16);         bit += 16;
    int32_t  cic       = (int32_t) extract_signed(payload, bit, 16);   bit += 16;
    int32_t  omega0    = (int32_t) extract_signed(payload, bit, 32);   bit += 32;
    int32_t  cis       = (int32_t) extract_signed(payload, bit, 16);   bit += 16;
    int32_t  i0        = (int32_t) extract_signed(payload, bit, 32);   bit += 32;
    int32_t  crc       = (int32_t) extract_signed(payload, bit, 16);   bit += 16;
    int32_t  omega     = (int32_t) extract_signed(payload, bit, 32);   bit += 32;
    int32_t  omega_dot = (int32_t) extract_signed(payload, bit, 24);   bit += 24;
    int32_t  tgd       = (int32_t) extract_signed(payload, bit, 8);    bit += 8;
    uint32_t health    = (uint32_t)get_bits(payload, bit, 6);          bit += 6;
    uint32_t l2p_flag  = (uint32_t)get_bits(payload, bit, 1);          bit += 1;
    uint32_t fit_flag  = (uint32_t)get_bits(payload, bit, 1);          bit += 1;

    /* ── Scaling.  "semi-circle" units (DF079, 087, 088, 095, 097, 099, 100)
     *    are converted to radians by multiplying the LSB by pi. ── */
    double idot_s      = (double)idot      * pow(2.0, -43) * M_PI;
    double toc_s       = (double)toc_raw   * 16.0;
    double af2_s       = (double)af2       * pow(2.0, -55);
    double af1_s       = (double)af1       * pow(2.0, -43);
    double af0_s       = (double)af0       * pow(2.0, -31);
    double crs_s       = (double)crs       * pow(2.0, -5);
    double delta_n_s   = (double)delta_n   * pow(2.0, -43) * M_PI;
    double m0_s        = (double)m0        * pow(2.0, -31) * M_PI;
    double cuc_s       = (double)cuc       * pow(2.0, -29);
    double e_s         = (double)e_raw     * pow(2.0, -33);
    double cus_s       = (double)cus       * pow(2.0, -29);
    double sqrtA_s     = (double)sqrtA_raw * pow(2.0, -19);
    double toe_s       = (double)toe_raw   * 16.0;
    double cic_s       = (double)cic       * pow(2.0, -29);
    double omega0_s    = (double)omega0    * pow(2.0, -31) * M_PI;
    double cis_s       = (double)cis       * pow(2.0, -29);
    double i0_s        = (double)i0        * pow(2.0, -31) * M_PI;
    double crc_s       = (double)crc       * pow(2.0, -5);
    double omega_s     = (double)omega     * pow(2.0, -31) * M_PI;
    double omega_dot_s = (double)omega_dot * pow(2.0, -43) * M_PI;
    double tgd_s       = (double)tgd       * pow(2.0, -31);

    /* ── Populate the central ephemeris cache used by the Sky Plot ── */
    SvEphemeris eph;
    memset(&eph, 0, sizeof(eph));
    eph.gnss_id      = 1;             /* GPS */
    eph.prn          = (int)prn;
    eph.iode_iodnav  = (int)iode;
    eph.week         = (int)gps_week; /* TODO: handle 10-bit rollover */
    eph.toe          = toe_s;
    eph.toc          = toc_s;
    eph.sqrt_a       = sqrtA_s;
    eph.e            = e_s;
    eph.i0           = i0_s;
    eph.omega0       = omega0_s;
    eph.omega        = omega_s;
    eph.m0           = m0_s;
    eph.delta_n      = delta_n_s;
    eph.idot         = idot_s;
    eph.omega_dot    = omega_dot_s;
    eph.cuc          = cuc_s;
    eph.cus          = cus_s;
    eph.crc          = crc_s;
    eph.crs          = crs_s;
    eph.cic          = cic_s;
    eph.cis          = cis_s;
    eph.af0          = af0_s;
    eph.af1          = af1_s;
    eph.af2          = af2_s;
    eph.health       = (int)health;
    sv_eph_store(&eph);

    /* ── Print summary ── */
    rtcm_printf("RTCM 1019 (GPS Ephemeris):\n");
    rtcm_printf("  SV: G%02u   GPS Week: %u (10-bit, no rollover)\n", prn, gps_week);
    rtcm_printf("  Health: %u   SV Accuracy (URA idx): %u   Code-on-L2: %u\n",
                health, sv_acc, code_l2);
    rtcm_printf("  IODE: %u   IODC: %u   L2P-flag: %u   Fit-flag: %u\n",
                iode, iodc, l2p_flag, fit_flag);
    rtcm_printf("  toc: %.0f s of week   toe: %.0f s of week\n", toc_s, toe_s);
    rtcm_printf("  Clock: af0=%.6e s  af1=%.6e s/s  af2=%.6e s/s^2  TGD=%.6e s\n",
                af0_s, af1_s, af2_s, tgd_s);
    rtcm_printf("  sqrt(A) = %.6f m^0.5    e = %.10g\n", sqrtA_s, e_s);
    rtcm_printf("  i0      = %+.6f rad     OMEGA0 = %+.6f rad\n", i0_s, omega0_s);
    rtcm_printf("  omega   = %+.6f rad     M0     = %+.6f rad\n", omega_s, m0_s);
    rtcm_printf("  delta_n = %+.6e rad/s   idot   = %+.6e rad/s\n", delta_n_s, idot_s);
    rtcm_printf("  OMEGADOT = %+.6e rad/s\n", omega_dot_s);
    rtcm_printf("  Harmonic: Cuc=%+.6e Cus=%+.6e Crc=%+.3f m Crs=%+.3f m\n",
                cuc_s, cus_s, crc_s, crs_s);
    rtcm_printf("            Cic=%+.6e Cis=%+.6e\n", cic_s, cis_s);
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

/**
 * @brief Decode RTCM 3.x Type 1029 — Unicode Text String.
 *
 * Fields (RTCM 10403.3):
 *   DF002  Message Number        12 bits
 *   DF003  Reference Station ID  12 bits
 *   DF051  MJD Number            16 bits
 *   DF052  UTC Seconds of Day    17 bits
 *   DF138  Byte Count            7 bits  (N, number of UTF-8 encoded bytes)
 *   DF029  Unicode Char Count    8 bits  (may differ from N for multibyte chars)
 *   DF139  UTF-8 Text            N×8 bits
 */
void decode_rtcm_1029(const unsigned char *payload, int payload_len) {
    /* Fixed header is 12+12+16+17+7+8 = 72 bits = 9 bytes */
    if (payload_len < 9) {
        rtcm_printf("RTCM 1029: Payload too short (%d bytes, need at least 9)!\n", payload_len);
        return;
    }

    int bit = 0;
    uint16_t msg_number      = (uint16_t)get_bits(payload, bit, 12); bit += 12;
    uint16_t ref_station_id  = (uint16_t)get_bits(payload, bit, 12); bit += 12;
    uint16_t mjd             = (uint16_t)get_bits(payload, bit, 16); bit += 16;
    uint32_t sod             = (uint32_t)get_bits(payload, bit, 17); bit += 17;
    uint8_t  n_bytes         = (uint8_t) get_bits(payload, bit,  7); bit +=  7;
    uint8_t  n_chars         = (uint8_t) get_bits(payload, bit,  8); bit +=  8;
    /* bit == 72 here */

    if (payload_len < 9 + n_bytes) {
        rtcm_printf("RTCM 1029: Payload too short for text "
                    "(%d bytes available, need %d)!\n",
                    payload_len, 9 + n_bytes);
        return;
    }

    /* Extract UTF-8 string bytes */
    char text[256] = {0};
    int  copy_len  = (n_bytes < (int)sizeof(text) - 1) ? n_bytes : (int)sizeof(text) - 1;
    for (int i = 0; i < copy_len; i++) {
        text[i] = (char)get_bits(payload, bit, 8); bit += 8;
    }
    text[copy_len] = '\0';

    /* Decompose seconds-of-day into HH:MM:SS */
    uint32_t hh = sod / 3600u;
    uint32_t mm = (sod % 3600u) / 60u;
    uint32_t ss = sod % 60u;

    rtcm_printf("RTCM 1029 (Unicode Text String):\n");
    rtcm_printf("  Message Number    : %u\n", msg_number);
    rtcm_printf("  Reference Station : %u\n", ref_station_id);
    rtcm_printf("  MJD               : %u\n", mjd);
    rtcm_printf("  UTC Time          : %02u:%02u:%02u\n", hh, mm, ss);
    rtcm_printf("  Byte count        : %u\n", n_bytes);
    rtcm_printf("  Unicode chars     : %u\n", n_chars);
    rtcm_printf("  Text              : %s\n", text);
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


