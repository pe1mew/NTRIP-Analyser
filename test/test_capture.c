/**
 * @file test_capture.c
 * @brief A capture must be the frames, all of them, and nothing else.
 *
 * The session's capture exists so that a stream can be written to disk
 * for a converter to read hours later, and the whole value of it rests
 * on one property: what comes out is exactly the CRC-valid frames that
 * went in.  That is testable without a network, a caster or a config
 * file -- for an input which is already nothing but valid frames,
 * capture-of-replay is the identity function.
 *
 * The frames here are built rather than recorded.  A recorded capture
 * would tie the test to one station's coordinates in a public
 * repository, and would test less: built frames let a case put junk
 * between them, corrupt one deliberately, and pick sizes that land the
 * byte limit mid-frame.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
#include "session/ntrip_session.h"
#include "core/rtcm3x_parser.h"   /* crc24q */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

/* ── Building RTCM ───────────────────────────────────────────────── */

/**
 * @brief Write one well-formed RTCM 3 frame into @p out.
 *
 * Layout: 0xD3, six reserved bits and a ten-bit payload length, the
 * payload (whose first twelve bits are the message type), then CRC-24Q
 * over everything before it.
 *
 * @return The frame length in bytes.
 */
static int build_frame(unsigned char *out, int msg_type, int payload_len,
                       unsigned char seed)
{
    out[0] = 0xD3;
    out[1] = (unsigned char)((payload_len >> 8) & 0x03);
    out[2] = (unsigned char)(payload_len & 0xFF);

    unsigned char *p = out + 3;
    p[0] = (unsigned char)(msg_type >> 4);
    p[1] = (unsigned char)(((msg_type & 0x0F) << 4) | (seed & 0x0F));
    for (int i = 2; i < payload_len; i++)
        p[i] = (unsigned char)(seed + i);

    uint32_t crc = crc24q(out, (size_t)(3 + payload_len));
    out[3 + payload_len + 0] = (unsigned char)((crc >> 16) & 0xFF);
    out[3 + payload_len + 1] = (unsigned char)((crc >> 8)  & 0xFF);
    out[3 + payload_len + 2] = (unsigned char)( crc        & 0xFF);
    return payload_len + 6;
}

#define N_FRAMES 24

/** Sizes chosen to vary, and to make the byte-limit case land mid-frame. */
static int frame_len_for(int i) { return 10 + (i * 7) % 53; }
static int frame_type_for(int i)
{
    static const int types[] = { 1077, 1087, 1097, 1005, 1033, 1230 };
    return types[i % 6];
}

/** @brief Write @p n frames to @p path.  Returns total bytes. */
static long write_frames(const char *path, int n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    unsigned char frame[128];
    long total = 0;
    for (int i = 0; i < n; i++) {
        int len = build_frame(frame, frame_type_for(i), frame_len_for(i),
                              (unsigned char)i);
        fwrite(frame, 1, (size_t)len, f);
        total += len;
    }
    fclose(f);
    return total;
}

/** @brief Read a whole file.  Caller frees.  Length in @p len_out. */
static unsigned char *slurp(const char *path, long *len_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = (unsigned char *)malloc((size_t)(n > 0 ? n : 1));
    if (buf && n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (len_out) *len_out = n;
    return buf;
}

static int files_identical(const char *a, const char *b)
{
    long na = 0, nb = 0;
    unsigned char *pa = slurp(a, &na);
    unsigned char *pb = slurp(b, &nb);
    int same = pa && pb && na == nb && memcmp(pa, pb, (size_t)na) == 0;
    free(pa);
    free(pb);
    return same;
}

/* ── Driving a session ───────────────────────────────────────────── */

typedef struct {
    int frames;
    int end_reason;
    int ended;
} Seen;

static void on_event(const NsEvent *ev, void *user)
{
    Seen *s = (Seen *)user;
    if (ev->type == NS_EV_FRAME) s->frames++;
    if (ev->type == NS_EV_DISCONNECTED) {
        s->ended = 1;
        s->end_reason = ev->u.end.reason;
    }
}

/** @brief Replay @p in, capturing to @p out; fills @p seen. */
static void replay_capture(const char *in, const char *out,
                           uint64_t max_bytes, Seen *seen,
                           uint64_t *cap_bytes, uint64_t *cap_frames,
                           int *cap_failed)
{
    memset(seen, 0, sizeof(*seen));

    NsOptions opt;
    ns_options_default(&opt);
    opt.stats_interval_s   = 0.0;
    opt.capture_path       = out;
    opt.capture_max_bytes  = max_bytes;

    NtripSession *s = ns_open_file(in, &opt, on_event, seen);
    if (!s) { check(0, "session could not be allocated"); return; }

    while (ns_pump(s, 0) >= 0) { /* replay runs to end of file */ }

    if (cap_bytes || cap_frames) ns_capture_status(s, cap_bytes, cap_frames);
    if (cap_failed) *cap_failed = ns_capture_failed(s) ? 1 : 0;
    ns_close(s);
}

int main(void)
{
    const char *src   = "test_capture_src.rtcm3";
    const char *dirty = "test_capture_dirty.rtcm3";
    const char *out   = "test_capture_out.rtcm3";

    /* A capture refuses to overwrite, so a rerun must start clean. */
    remove(src); remove(dirty); remove(out);

    long src_bytes = write_frames(src, N_FRAMES);
    check(src_bytes > 0, "built a source capture of valid frames");

    /* ── 1. Capture of a replay is the identity ───────────────────── */
    {
        Seen seen;
        uint64_t bytes = 0, frames = 0;
        int failed = 1;
        replay_capture(src, out, 0, &seen, &bytes, &frames, &failed);

        check(seen.frames == N_FRAMES, "every frame reached the consumer");
        check(frames == (uint64_t)N_FRAMES, "every frame reached the disk");
        check(bytes == (uint64_t)src_bytes, "byte count matches the source");
        check(files_identical(src, out), "the capture is byte-identical");
        check(!failed, "no write failure was reported");
        check(seen.ended && seen.end_reason == NS_END_EOF,
              "the session ended at end of file");
        remove(out);
    }

    /* ── 2. Junk and a bad CRC are filtered out ───────────────────── */
    {
        /* What a real stream carries between frames: NMEA the analyser
         * does not frame, and the occasional corrupted frame. */
        FILE *f = fopen(dirty, "wb");
        check(f != NULL, "opened a dirty source for writing");
        if (f) {
            unsigned char frame[128];
            for (int i = 0; i < N_FRAMES; i++) {
                const char *junk = "$GPGGA,,,,,,0,,,,,,,,*66\r\n";
                fwrite(junk, 1, strlen(junk), f);

                int len = build_frame(frame, frame_type_for(i),
                                      frame_len_for(i), (unsigned char)i);
                if (i == 7) {
                    /* Corrupt the payload after the CRC was computed, so
                     * the frame is complete and wrong -- the case the
                     * capture must not pass on to a converter. */
                    unsigned char bad[128];
                    memcpy(bad, frame, (size_t)len);
                    bad[5] ^= 0xFF;
                    fwrite(bad, 1, (size_t)len, f);
                }
                fwrite(frame, 1, (size_t)len, f);
            }
            fclose(f);
        }

        Seen seen;
        uint64_t bytes = 0, frames = 0;
        replay_capture(dirty, out, 0, &seen, &bytes, &frames, NULL);

        check(frames == (uint64_t)N_FRAMES,
              "the corrupted frame and the NMEA were left out");
        check(files_identical(src, out),
              "capturing a dirty stream yields the clean one");
        remove(out);
    }

    /* ── 3. The byte limit stops on a frame boundary ──────────────── */
    {
        /* A limit that falls inside the fourth frame: the capture must
         * close before it, not halfway through it, or the file ends in
         * a fragment no converter can read. */
        long three = 0;
        for (int i = 0; i < 3; i++) three += frame_len_for(i) + 6;
        uint64_t limit = (uint64_t)(three + 4);

        Seen seen;
        uint64_t bytes = 0, frames = 0;
        int failed = 1;
        replay_capture(src, out, limit, &seen, &bytes, &frames, &failed);

        check(bytes == (uint64_t)three, "stopped exactly on a frame boundary");
        check(frames == 3, "kept the frames that fitted");
        check(!failed, "reaching the limit is not a failure");
        check(seen.frames == N_FRAMES,
              "the session kept streaming after the limit");
        check(seen.end_reason == NS_END_EOF,
              "and still ended at end of file");

        long n = 0;
        unsigned char *got = slurp(out, &n);
        check(n == three, "the file on disk is the size reported");
        free(got);
        remove(out);
    }

    /* ── 4. It will not overwrite an existing capture ─────────────── */
    {
        FILE *f = fopen(out, "wb");
        if (f) { fputs("a previous run", f); fclose(f); }

        Seen seen;
        int failed = 0;
        replay_capture(src, out, 0, &seen, NULL, NULL, &failed);

        check(failed, "refusing to overwrite is reported as a failure");
        check(seen.end_reason == NS_END_WRITE_ERROR,
              "the session ended NS_END_WRITE_ERROR");
        check(seen.frames == 0, "and it ended before reading any frame");

        long n = 0;
        unsigned char *got = slurp(out, &n);
        check(n == 14, "the existing file was left untouched");
        free(got);
        remove(out);
    }

    /* ── 5. An unopenable path fails at once ──────────────────────── */
    {
        Seen seen;
        int failed = 0;
        replay_capture(src, "no_such_directory_here/x.rtcm3", 0,
                       &seen, NULL, NULL, &failed);

        check(failed, "a bad path is a failure, not a warning");
        check(seen.frames == 0,
              "nothing was streamed -- the run stops in its first second");
    }

    remove(src);
    remove(dirty);

    printf("\n%s\n", failures ? "FAILURES" : "all capture cases pass");
    return failures ? 1 : 0;
}
