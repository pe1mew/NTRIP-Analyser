#include <stdio.h>
#include "rtcm3x_parser.h"

// RTCM CRC-24Q lookup table (polynomial 0x1864CFB)
static const uint32_t crc24q_table[256] = {
    0x000000,0x864CFB,0x8AD50D,0x0C99F6,0x93E6E1,0x15AA1A,0x1973EC,0x9F3F17,
    0xA18139,0x27CDC2,0x2B1434,0xAD58CF,0x3267D8,0xB42B23,0xB8F2D5,0x3EBE2E,
    0xC54E89,0x430272,0x4FDB84,0xC9977F,0x56A868,0xD0E493,0xDC3D65,0x5A719E,
    0x64CFB0,0xE2834B,0xEE5ABD,0x681646,0xF72951,0x7165AA,0x7DBC5C,0xFBF0A7,
    0x0C5D13,0x8A11E8,0x86C81E,0x0084E5,0x9FBBF2,0x19F709,0x152EFF,0x936204,
    0xADDC2A,0x2B90D1,0x274927,0xA105DC,0x3E3ACB,0xB87630,0xB4AFC6,0x32E33D,
    0xC9139A,0x4F5F61,0x438697,0xC5CA6C,0x5AF57B,0xDCB980,0xD06076,0x562C8D,
    0x6892A3,0xEEDE58,0xE207AE,0x644B55,0xFB7442,0x7D38B9,0x71E14F,0xF7ADB4,
    0x18BA26,0x9EF6DD,0x922F2B,0x1463D0,0x8B5CC7,0x0D103C,0x01C9CA,0x878531,
    0xB93B1F,0x3F77E4,0x33AE12,0xB5E2E9,0x2ADDFF,0xAC9104,0xA048F2,0x260409,
    0xDDF4AE,0x5BB855,0x5761A3,0xD12D58,0x4E124F,0xC85EB4,0xC48742,0x42CBB9,
    0x7C7597,0xFA396C,0xF6E09A,0x70AC61,0xEF9376,0x69DF8D,0x65067B,0xE34A80,
    0x14E734,0x92ABCF,0x9E7239,0x183EC2,0x8701D5,0x014D2E,0x0D94D8,0x8BD823,
    0xB5660D,0x332AF6,0x3FF300,0xB9BFFB,0x2680EC,0xA0CC17,0xAC15E1,0x2A591A,
    0xD1A9BD,0x57E546,0x5B3CB0,0xDD704B,0x424F5C,0xC403A7,0xC8DA51,0x4E96AA,
    0x702884,0xF6647F,0xFABD89,0x7CF172,0xE3CE65,0x65829E,0x695B68,0xEF1793,
    0x30644C,0xB62CB7,0xBAF541,0x3CB9BA,0xA386AD,0x25CA56,0x2913A0,0xAF5F5B,
    0x91E175,0x17AD8E,0x1B7478,0x9D3883,0x020794,0x844B6F,0x889299,0x0EDE62,
    0xF52EC5,0x73623E,0x7FBBC8,0xF9F733,0x66C824,0xE084DF,0xEC5D29,0x6A11D2,
    0x54AFEC,0xD2E317,0xDE3AE1,0x58761A,0xC7490D,0x4105F6,0x4DDC00,0xCB90FB,
    0x3C3D4F,0xBA71B4,0xB6A842,0x30E4B9,0xAFDBAE,0x299755,0x254EA3,0xA30258,
    0x9DBC76,0x1BF08D,0x17297B,0x916580,0x0E5A97,0x88166C,0x84CF9A,0x028361,
    0xF973C6,0x7F3F3D,0x73E6CB,0xF5AA30,0x6A9527,0xECD9DC,0xE0002A,0x664CD1,
    0x58F2FF,0xDEBE04,0xD267F2,0x542B09,0xCB141E,0x4D58E5,0x418113,0xC7CDE8,
    0x20105C,0xA65CA7,0xAA8551,0x2CC9AA,0xB3F6BD,0x35BA46,0x3963B0,0xBF2F4B,
    0x819165,0x07DD9E,0x0B0468,0x8D4893,0x127784,0x943B7F,0x98E289,0x1EAE72,
    0xE55ED5,0x63122E,0x6FCBD8,0xE98723,0x76B834,0xF0F4CF,0xFC2D39,0x7A61C2,
    0x44DFEC,0xC29317,0xCE4AE1,0x48061A,0xD7390D,0x5175F6,0x5DAC00,0xDBE0FB,
    0x2C4D4F,0xAA01B4,0xA6D842,0x2094B9,0xBFABAE,0x39E755,0x353EA3,0xB37258,
    0x8DCC76,0x0B808D,0x07597B,0x811580,0x1E2A97,0x98666C,0x94BF9A,0x12F361,
    0xE903C6,0x6F4F3D,0x6396CB,0xE5DA30,0x7AE527,0xFCA9DC,0xF0702A,0x763CD1,
    0x48A2FF,0xCEE604,0xC23FF2,0x447309,0xDB4C1E,0x5D00E5,0x51D913,0xD795E8
};

// CRC-24Q calculation
static uint32_t crc24q(const unsigned char *buf, int len) {
    uint32_t crc = 0;
    for (int i = 0; i < len; ++i) {
        crc = ((crc << 8) & 0xFFFFFF) ^ crc24q_table[((crc >> 16) ^ buf[i]) & 0xFF];
    }
    return crc & 0xFFFFFF;
}

// Helper to extract bits from a buffer (big-endian, MSB first)
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
    // payload points to data[3] in the RTCM frame (after header)
    // payload_len should be at least 12 bytes for 1005
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

    // Header
    uint16_t ref_station_id = (uint16_t)get_bits(payload, bit, 12); bit += 12;
    uint32_t epoch_time = (uint32_t)get_bits(payload, bit, 30); bit += 30;
    uint8_t mm_flag = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    uint8_t iods = (uint8_t)get_bits(payload, bit, 3); bit += 3;
    bit += 7; // reserved
    uint8_t clk_steering = (uint8_t)get_bits(payload, bit, 2); bit += 2;
    uint8_t ext_clk = (uint8_t)get_bits(payload, bit, 2); bit += 2;
    uint8_t df_smoothing = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    uint8_t smoothing_int = (uint8_t)get_bits(payload, bit, 3); bit += 3;

    // Satellite mask (64 bits)
    uint64_t sat_mask = get_bits(payload, bit, 64); bit += 64;
    // Signal mask (32 bits)
    uint32_t sig_mask = (uint32_t)get_bits(payload, bit, 32); bit += 32;

    // Count satellites and signals
    int num_sats = 0, num_sigs = 0;
    for (int i = 0; i < 64; ++i) if ((sat_mask >> (63 - i)) & 1) num_sats++;
    for (int i = 0; i < 32; ++i) if ((sig_mask >> (31 - i)) & 1) num_sigs++;

    int num_cells = 0;
    // Cell mask (num_sats * num_sigs bits)
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

    // For brevity, print only the first few cells' data
    for (int cell = 0; cell < num_cells && cell < 5; ++cell) {
        int32_t pseudorange = (int32_t)get_bits(payload, bit, 20); bit += 20;
        int32_t phaserange = (int32_t)get_bits(payload, bit, 24); bit += 24;
        uint8_t lock = (uint8_t)get_bits(payload, bit, 7); bit += 7;
        uint8_t half_cycle = (uint8_t)get_bits(payload, bit, 1); bit += 1;
        uint8_t cnr = (uint8_t)get_bits(payload, bit, 8); bit += 8;
        int16_t phaserate = (int16_t)get_bits(payload, bit, 15); bit += 15;

        // Sign extension for signed fields
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

// Generic MSM7 decoder for other GNSS (structure is the same as 1077, only GNSS type differs)
static void decode_rtcm_msm7(const unsigned char *payload, int payload_len, const char *gnss_name, int msg_type) {
    int bit = 0;
    if (payload_len < 20) {
        printf("Type %d: Payload too short!\n", msg_type);
        return;
    }

    // Header
    uint16_t ref_station_id = (uint16_t)get_bits(payload, bit, 12); bit += 12;
    uint32_t epoch_time = (uint32_t)get_bits(payload, bit, 30); bit += 30;
    uint8_t mm_flag = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    uint8_t iods = (uint8_t)get_bits(payload, bit, 3); bit += 3;
    bit += 7; // reserved
    uint8_t clk_steering = (uint8_t)get_bits(payload, bit, 2); bit += 2;
    uint8_t ext_clk = (uint8_t)get_bits(payload, bit, 2); bit += 2;
    uint8_t df_smoothing = (uint8_t)get_bits(payload, bit, 1); bit += 1;
    uint8_t smoothing_int = (uint8_t)get_bits(payload, bit, 3); bit += 3;

    // Satellite mask (64 bits)
    uint64_t sat_mask = get_bits(payload, bit, 64); bit += 64;
    // Signal mask (32 bits)
    uint32_t sig_mask = (uint32_t)get_bits(payload, bit, 32); bit += 32;

    // Count satellites and signals
    int num_sats = 0, num_sigs = 0;
    for (int i = 0; i < 64; ++i) if ((sat_mask >> (63 - i)) & 1) num_sats++;
    for (int i = 0; i < 32; ++i) if ((sig_mask >> (31 - i)) & 1) num_sigs++;

    int num_cells = 0;
    // Cell mask (num_sats * num_sigs bits)
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

    // For brevity, print only the first few cells' data
    for (int cell = 0; cell < num_cells && cell < 5; ++cell) {
        int32_t pseudorange = (int32_t)get_bits(payload, bit, 20); bit += 20;
        int32_t phaserange = (int32_t)get_bits(payload, bit, 24); bit += 24;
        uint8_t lock = (uint8_t)get_bits(payload, bit, 7); bit += 7;
        uint8_t half_cycle = (uint8_t)get_bits(payload, bit, 1); bit += 1;
        uint8_t cnr = (uint8_t)get_bits(payload, bit, 8); bit += 8;
        int16_t phaserate = (int16_t)get_bits(payload, bit, 15); bit += 15;

        // Sign extension for signed fields
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

void analyze_rtcm_message(const unsigned char *data, int length) {
    if (length < 6) return;

    if (data[0] == 0xD3) {
        int msg_length = ((data[1] & 0x03) << 8) | data[2];
        int msg_type = ((data[3] << 4) | (data[4] >> 4)) & 0x0FFF;

        // CRC is the last 3 bytes of the frame
        uint32_t crc_extracted = 0;
        int frame_len = msg_length + 6;
        if (length >= frame_len) {
            crc_extracted = (data[3 + msg_length] << 16) |
                            (data[3 + msg_length + 1] << 8) |
                            (data[3 + msg_length + 2]);
        }

        // Calculate CRC over header + payload (excluding CRC itself)
        // uint32_t crc_calc = 0;
        // if (length >= frame_len) {
        //     crc_calc = crc24q(data, 3 + msg_length);
        // }

        // --- CRC check commented for later work ---
        /*
        if (length >= frame_len) {
            printf("  CRC extracted:  0x%06X\n", crc_extracted);
            printf("  CRC calculated: 0x%06X\n", crc_calc);
            printf("  CRC check:      %s\n", (crc_calc == crc_extracted) ? "OK" : "FAIL");
        }
        */

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
                printf("RTCM Message: Type = %d, Length = %d, CRC = 0x%06X\n", msg_type, msg_length, crc_extracted);
                // CRC check is commented out for later work
                // if (crc_calc == crc_extracted) {
                //     printf("(CRC OK)\n");
                // } else {
                //     printf("(CRC FAIL! Calculated: 0x%06X)\n", crc_calc);
                // }
            } else {
                printf("RTCM Message: Type = %d, Length = %d (frame incomplete)\n", msg_type, msg_length);
            }
        }

        // Print CRC check result for all message types (commented for later)
        /*
        if (length >= frame_len) {
            printf("  CRC extracted:  0x%06X\n", crc_extracted);
            printf("  CRC calculated: 0x%06X\n", crc_calc);
            printf("  CRC check:      %s\n", (crc_calc == crc_extracted) ? "OK" : "FAIL");
        }
        */
    } else {
        printf("Non-RTCM or malformed data (first bytes): ");
        for (int i = 0; i < length && i < 16; ++i) {
            printf("%02X ", data[i]);
        }
        printf("\n");
    }
}


