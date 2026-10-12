/**
 * @file test_station_arp.c
 * @brief The decoded station ARP belongs to one stream, not the process.
 *
 * The parser keeps the last 1005/1006 position process-wide, and until
 * rtcm_reset_station_arp() existed nothing ever forgot it. The GUI opens
 * stream after stream in one process, so the previous station's ARP was
 * read as the new one's until the new station's own 1005 arrived: RFSEE01
 * then HANESE showed "moved 1x, largest jump 24.98 km" -- the distance
 * between the two stations, not a movement of either (2026-10-11).
 *
 * Pinned here: a decode sets the ARP, a reset makes it invalid and leaves
 * no coordinates behind, and the next decode -- 1006 this time, the other
 * writer -- makes it valid again with the new station's position.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "core/rtcm3x_parser.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond, ...)                                            \
    do {                                                            \
        if (!(cond)) {                                              \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);           \
            printf(__VA_ARGS__);                                    \
            printf("\n");                                           \
            failures++;                                             \
        }                                                           \
    } while (0)

/* MSB-first bit writer, the order RTCM puts fields on the wire. */
static void put_bits(unsigned char *buf, int *bit, int n, uint64_t v)
{
    for (int i = n - 1; i >= 0; i--, (*bit)++) {
        if ((v >> i) & 1u)
            buf[*bit / 8] |= (unsigned char)(0x80u >> (*bit % 8));
    }
}

/* Wrap a payload in a valid RTCM 3 frame: preamble, length, CRC-24Q. */
static int frame_up(unsigned char *out, const unsigned char *payload, int len)
{
    out[0] = 0xD3;
    out[1] = (unsigned char)((len >> 8) & 0x03);
    out[2] = (unsigned char)(len & 0xFF);
    memcpy(out + 3, payload, (size_t)len);

    uint32_t crc = crc24q(out, (size_t)(3 + len));
    out[3 + len]     = (unsigned char)((crc >> 16) & 0xFF);
    out[3 + len + 1] = (unsigned char)((crc >> 8) & 0xFF);
    out[3 + len + 2] = (unsigned char)(crc & 0xFF);
    return len + 6;
}

/* A 1005 (msg 1005) or 1006 (msg 1006, antenna height 0) at an ECEF
 * position in metres, encoded at RTCM's 0.1 mm resolution. Returns the
 * frame length. */
static int build_arp_frame(unsigned char *frame, int msg,
                           double x, double y, double z)
{
    unsigned char p[21];
    memset(p, 0, sizeof(p));
    int bit = 0;
    const uint64_t m38 = ((uint64_t)1 << 38) - 1;

    put_bits(p, &bit, 12, (uint64_t)msg);
    put_bits(p, &bit, 12, 42);                 /* station id */
    put_bits(p, &bit, 6, 0);                   /* ITRF year */
    put_bits(p, &bit, 4, 0xE);                 /* GPS, GLO, GAL, ref-station */
    put_bits(p, &bit, 38, (uint64_t)llround(x * 10000.0) & m38);
    put_bits(p, &bit, 2, 0);                   /* oscillator, reserved */
    put_bits(p, &bit, 38, (uint64_t)llround(y * 10000.0) & m38);
    put_bits(p, &bit, 2, 0);                   /* quarter cycle */
    put_bits(p, &bit, 38, (uint64_t)llround(z * 10000.0) & m38);
    if (msg == 1006)
        put_bits(p, &bit, 16, 0);              /* antenna height */

    return frame_up(frame, p, (bit + 7) / 8);
}

/* Decode the way the GUI's display path does: with output on, which is
 * the only way the 1005/1006 decoders run at all -- suppress_output
 * skips them, and with them the ARP.  The text goes to a buffer. */
static int decode(const unsigned char *frame, int len)
{
    RtcmStrBuf sb;
    rtcm_strbuf_init(&sb, 4096);
    rtcm_set_output_buffer(&sb);
    int type = analyze_rtcm_message(frame, len, false, NULL);
    rtcm_set_output_buffer(NULL);
    rtcm_strbuf_free(&sb);
    return type;
}

static void check_arp(bool want_valid, double wx, double wy, double wz,
                      const char *what)
{
    bool   valid = !want_valid;
    double x = -1, y = -1, z = -1;
    rtcm_get_station_arp(&valid, &x, &y, &z, NULL, NULL, NULL);

    CHECK(valid == want_valid, "%s: valid is %d, expected %d",
          what, (int)valid, (int)want_valid);
    CHECK(fabs(x - wx) < 1e-3 && fabs(y - wy) < 1e-3 && fabs(z - wz) < 1e-3,
          "%s: ARP (%.4f, %.4f, %.4f), expected (%.4f, %.4f, %.4f)",
          what, x, y, z, wx, wy, wz);
    printf("  %s: %s\n", what, valid ? "valid" : "invalid");
}

int main(void)
{
    /* Two stations about 25 km apart, as RFSEE01 and HANESE are. */
    const double ax = 3906123.4567, ay = 458765.4321, az = 5024680.1357;
    const double bx = 3918000.0000, by = 437000.0000, bz = 5016000.0000;

    unsigned char frame[64];
    int len;

    printf("station ARP across streams:\n");

    check_arp(false, 0, 0, 0, "before any 1005");

    len = build_arp_frame(frame, 1005, ax, ay, az);
    CHECK(decode(frame, len) == 1005,
          "the 1005 frame did not decode as 1005");
    check_arp(true, ax, ay, az, "after station A's 1005");

    rtcm_reset_station_arp();
    check_arp(false, 0, 0, 0, "after reset");

    len = build_arp_frame(frame, 1006, bx, by, bz);
    CHECK(decode(frame, len) == 1006,
          "the 1006 frame did not decode as 1006");
    check_arp(true, bx, by, bz, "after station B's 1006");

    if (failures == 0) printf("test_station_arp: OK\n");
    else               printf("test_station_arp: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
