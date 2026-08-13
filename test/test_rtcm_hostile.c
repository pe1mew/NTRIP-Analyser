/**
 * @file test_rtcm_hostile.c
 * @brief Frames a hostile caster can send, and the decoders' response.
 *
 * Every byte the RTCM decoders see comes from a caster the user named
 * but nobody vouches for, and CRC-24Q is not a defence: it detects
 * corruption, and anyone constructing a frame on purpose simply
 * computes the right one. So the decoders must hold against a
 * *well-formed* frame that lies about its own contents.
 *
 * The case that failed before this test existed: message 1033 carries
 * four counted strings, each with an eight-bit length. A frame could
 * declare 255 characters four times over while carrying eight bytes of
 * payload, and the decoder read every one of them -- the copy was
 * capped at 64 characters, the *reads* were not bounded at all, so it
 * walked some 250 bytes past the frame and printed whatever it found as
 * an antenna descriptor.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "core/rtcm3x_parser.h"

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

/* Wrap a payload in a valid RTCM 3 frame: preamble, length, CRC-24Q.
 * The CRC is computed, not faked -- that is the point. */
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

/* Decode one frame with the output captured, so the test can assert on
 * what the decoder *said* rather than only that it returned. */
static int decode_capturing(const unsigned char *frame, int frame_len,
                            char *out, size_t cap)
{
    RtcmStrBuf sb;
    rtcm_strbuf_init(&sb, 4096);
    rtcm_set_output_buffer(&sb);
    int type = analyze_rtcm_message(frame, frame_len, false, NULL);
    rtcm_set_output_buffer(NULL);

    snprintf(out, cap, "%s", sb.buf ? sb.buf : "");
    rtcm_strbuf_free(&sb);
    return type;
}

/* A 1033 whose four length counters each claim 255 characters, in a
 * payload far too short to hold them. */
static void test_1033_lying_counters(void)
{
    unsigned char payload[8];
    memset(payload, 0, sizeof(payload));

    /* DF002 = 1033 in the first 12 bits, station id in the next 12. */
    payload[0] = 0x40;                    /* 1033 = 0x409 */
    payload[1] = 0x90;
    payload[2] = 0x00;
    /* First counter lands at bit 24: claim 255, and leave the rest of
     * the payload as further 0xFF counters. */
    memset(payload + 3, 0xFF, sizeof(payload) - 3);

    unsigned char frame[32];
    int frame_len = frame_up(frame, payload, (int)sizeof(payload));

    char out[8192];
    int type = decode_capturing(frame, frame_len, out, sizeof(out));
    CHECK(type == 1033, "expected the frame to be recognised as 1033, got %d",
          type);

    /* The decoder must refuse the frame rather than read past it. A
     * build that reads the declared 255 characters anyway prints an
     * "Antenna Descriptor:" line full of whatever was next in memory,
     * which is exactly what this asserts against. */
    CHECK(strstr(out, "ends mid-field") != NULL,
          "expected the payload to be rejected as truncated; got:\n%s", out);
    CHECK(strstr(out, "Antenna Descriptor") == NULL,
          "a descriptor was printed from a payload that cannot hold one:\n%s",
          out);
    printf("  1033 with four 255-byte counters in an 8-byte payload: refused\n");
}

/* A 1033 that ends exactly where a counted string begins. */
static void test_1033_truncated_at_counter(void)
{
    unsigned char payload[4];
    memset(payload, 0, sizeof(payload));
    payload[0] = 0x40;
    payload[1] = 0x90;
    payload[2] = 0x00;
    payload[3] = 0x20;                    /* a counter, then nothing */

    unsigned char frame[32];
    int frame_len = frame_up(frame, payload, (int)sizeof(payload));

    char out[8192];
    int type = decode_capturing(frame, frame_len, out, sizeof(out));
    CHECK(type == 1033, "expected 1033, got %d", type);
    CHECK(strstr(out, "Antenna Descriptor") == NULL,
          "a descriptor was printed from a payload that ends first:\n%s", out);
    printf("  1033 truncated at a counter: refused\n");
}

/* 1007 and 1008 carry counted strings too; they were already guarded,
 * so this pins that they stay guarded. */
static void test_1007_lying_counter(void)
{
    unsigned char payload[6];
    memset(payload, 0, sizeof(payload));
    payload[0] = 0x3E;                    /* 1007 = 0x3EF */
    payload[1] = 0xF0;
    payload[2] = 0x00;
    payload[3] = 0xFF;                    /* descriptor length 255 */

    unsigned char frame[32];
    int frame_len = frame_up(frame, payload, (int)sizeof(payload));

    char out[8192];
    int type = decode_capturing(frame, frame_len, out, sizeof(out));
    CHECK(type == 1007, "expected 1007, got %d", type);
    CHECK(strstr(out, "too short") != NULL,
          "expected 1007 to reject the counter; got:\n%s", out);
    printf("  1007 with a 255-byte counter in a 6-byte payload: refused\n");
}

/* A frame whose CRC does not match must not be decoded at all. */
static void test_bad_crc_is_rejected(void)
{
    unsigned char payload[8];
    memset(payload, 0, sizeof(payload));
    payload[0] = 0x40;
    payload[1] = 0x90;

    unsigned char frame[32];
    int frame_len = frame_up(frame, payload, (int)sizeof(payload));
    frame[frame_len - 1] ^= 0xFF;         /* corrupt the CRC */

    int type = analyze_rtcm_message(frame, frame_len, true, NULL);
    CHECK(type == 0, "a CRC failure must report 0, got %d", type);
    printf("  frame with a broken CRC: rejected\n");
}

int main(void)
{
    printf("hostile RTCM frames:\n");
    test_1033_lying_counters();
    test_1033_truncated_at_counter();
    test_1007_lying_counter();
    test_bad_crc_is_rejected();

    if (failures == 0) printf("test_rtcm_hostile: OK\n");
    else               printf("test_rtcm_hostile: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
