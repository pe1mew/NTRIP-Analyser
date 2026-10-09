/**
 * @file test_selfpos_session.c
 * @brief Does a closed epoch reach the self-position solve at all?
 *
 * Every other test of this feature starts below the session.
 * `test_spp.c` hands `spp_solve()` an epoch it built itself;
 * `test_observables.c` hands `msm_extract_obs()` a payload it built
 * itself; `test_station_report.c` and `test_ns_stats.c` hand the report
 * and the serialisers snapshots they filled themselves. All of them
 * passed while the feature could not run at all: `selfpos_solve()` was
 * defined in `ntrip_session.c` and **never called**, because the return
 * value of `obs_feed()` -- the one that says an epoch just closed --
 * was discarded at the call site. It shipped through P5 and P6, and was
 * found by looking at the GUI window on a live stream.
 *
 * Nothing above the solver was wrong, which is exactly why nothing
 * caught it: every surface handled "nothing solved" correctly and said
 * so politely. What was missing was a test that drives the **session**,
 * the way a stream does, and asks whether the solve ran.
 *
 * So this test replays RTCM through `ns_open_file()` -- the same framing
 * and the same decode path a live caster feeds -- and asks only that.
 * Not whether the position is right: that is `test_spp.c`'s question,
 * against geometry it controls. Here the question is whether the epoch
 * gets there.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "session/ntrip_session.h"
#include "core/rtcm3x_parser.h"   /* crc24q */
#include "core/spp.h"             /* SppStatus */

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

/* ── Building a stream ───────────────────────────────────────────────
 * The bit layout is restated here rather than read from the code under
 * test, as test_observables.c does and for the same reason. */

static void put_bits(unsigned char *buf, int pos, int nbits,
                     unsigned long value)
{
    for (int i = 0; i < nbits; i++) {
        int bit = pos + i;
        unsigned long v = (value >> (nbits - 1 - i)) & 1UL;
        if (v) buf[bit >> 3] |= (unsigned char)(0x80u >> (bit & 7));
        else   buf[bit >> 3] &= (unsigned char)~(0x80u >> (bit & 7));
    }
}

/**
 * @brief One MSM7 GPS message: two satellites, one signal each.
 *
 * Deliberately thin. Two satellites on one frequency cannot produce a
 * position, and that is fine -- the solver must still be *reached* and
 * must still say which of its reasons applies. A richer constellation
 * would test the arithmetic, which is tested elsewhere.
 *
 * The multiple-message bit (DF393, bit 54) is left clear, so this frame
 * closes the epoch -- which is the condition the call site under test
 * is supposed to act on.
 */
static int build_msm7_gps(unsigned char *out, int out_cap,
                          unsigned long epoch_ms, int more)
{
    const int num_sats = 2, num_sigs = 1, num_cells = 2;
    memset(out, 0, (size_t)out_cap);

    put_bits(out, 0, 12, 1077);            /* GPS MSM7              */
    put_bits(out, 12, 12, 1234);           /* reference station id  */
    put_bits(out, 24, 30, epoch_ms);       /* GPS epoch, ms of week */
    /* DF393: clear closes the epoch here, set says another frame of
     * the same epoch follows.  The control case sets it on every
     * frame, so the epoch never closes and the solve must never run. */
    put_bits(out, 54, 1, more ? 1UL : 0UL);

    put_bits(out, 73 + 2, 1, 1);           /* PRN 3 */
    put_bits(out, 73 + 8, 1, 1);           /* PRN 9 */
    put_bits(out, 137 + 0, 1, 1);          /* signal id 1 */

    const int cell_mask_start = 169;
    for (int i = 0; i < num_sats * num_sigs; i++)
        put_bits(out, cell_mask_start + i, 1, 1);

    /* Satellite block: 75 ms and 80 ms of travel, both inside the band
     * a real GNSS range occupies. MSM7 carries the extended fields. */
    int p = cell_mask_start + num_sats * num_sigs;
    put_bits(out, p, 8, 75); p += 8;
    put_bits(out, p, 8, 80); p += 8;
    for (int s = 0; s < num_sats; s++) { put_bits(out, p, 4, 0); p += 4; }
    put_bits(out, p, 10, 512); p += 10;
    put_bits(out, p, 10, 0);   p += 10;
    for (int s = 0; s < num_sats; s++) { put_bits(out, p, 14, 0); p += 14; }

    /* Signal block, field by field across the cells. */
    for (int c = 0; c < num_cells; c++) { put_bits(out, p, 20, 100); p += 20; }
    for (int c = 0; c < num_cells; c++) { put_bits(out, p, 24, 100); p += 24; }
    for (int c = 0; c < num_cells; c++) { put_bits(out, p, 10, 7);   p += 10; }
    for (int c = 0; c < num_cells; c++) { put_bits(out, p, 1, 0);    p += 1;  }
    for (int c = 0; c < num_cells; c++) { put_bits(out, p, 10, 700); p += 10; }
    for (int c = 0; c < num_cells; c++) { put_bits(out, p, 15, 0);   p += 15; }

    return (p + 7) / 8;
}

/** @brief Wrap a payload in the RTCM 3 frame with its CRC. */
static int build_frame(unsigned char *out, const unsigned char *payload,
                       int payload_len)
{
    out[0] = 0xD3;
    out[1] = (unsigned char)((payload_len >> 8) & 0x03);
    out[2] = (unsigned char)(payload_len & 0xFF);
    memcpy(out + 3, payload, (size_t)payload_len);
    uint32_t crc = crc24q(out, (size_t)(3 + payload_len));
    out[3 + payload_len + 0] = (unsigned char)((crc >> 16) & 0xFF);
    out[3 + payload_len + 1] = (unsigned char)((crc >>  8) & 0xFF);
    out[3 + payload_len + 2] = (unsigned char)( crc        & 0xFF);
    return 3 + payload_len + 3;
}

/**
 * @brief Write @p n MSM7 epochs to a replayable capture.
 *
 * With @p more set, every frame claims another frame of the same epoch
 * follows, so no epoch ever closes.
 */
static int write_capture(const char *path, int n, int more)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    for (int i = 0; i < n; i++) {
        unsigned char payload[512], frame[560];
        int plen = build_msm7_gps(payload, (int)sizeof payload,
                                  86400000UL + (unsigned long)i * 1000UL,
                                  more);
        int flen = build_frame(frame, payload, plen);
        fwrite(frame, 1, (size_t)flen, f);
    }
    fclose(f);
    return 1;
}

/* ── Replaying it ────────────────────────────────────────────────────── */

typedef struct {
    int             stats_seen;
    NsStatsSnapshot last;
} Seen;

static void on_event(const NsEvent *ev, void *user)
{
    Seen *s = (Seen *)user;
    if (ev->type == NS_EV_STATS && ev->u.stats) {
        s->last = *ev->u.stats;
        s->stats_seen++;
    }
}

static int replay(const char *path, Seen *seen)
{
    NsOptions opt;
    ns_options_default(&opt);
    opt.stats_interval_s = 0.001;   /* a snapshot per pump, near enough */

    memset(seen, 0, sizeof(*seen));
    NtripSession *s = ns_open_file(path, &opt, on_event, seen);
    if (!s) return 0;
    while (ns_pump(s, 0) >= 0) { /* to end of file */ }
    ns_close(s);
    return 1;
}

int main(void)
{
    const char *obs_cap = "test_selfpos_obs.rtcm3";
    const char *dry_cap = "test_selfpos_dry.rtcm3";
    remove(obs_cap);
    remove(dry_cap);

    /* ── 1. A closed epoch reaches the solver ─────────────────────── */
    {
        Seen seen;
        check(write_capture(obs_cap, 20, 0), "built a capture of MSM7 epochs");
        check(replay(obs_cap, &seen), "replayed it through a session");
        check(seen.stats_seen > 0, "the session published snapshots");

        /* The whole point.  SPP_NO_EPOCH is the initial value of the
         * field: seeing it after twenty epochs of observations means
         * the solve was never run, which is exactly how the feature
         * shipped. Which *other* status it is does not matter here --
         * two satellites on one frequency cannot produce a position,
         * and the solver is entitled to say so. */
        check(seen.last.selfpos_status != SPP_NO_EPOCH,
              "an epoch that closed was handed to the self-position solve");
        printf("      status after replay: %d (%s)\n",
               seen.last.selfpos_status,
               spp_status_text((SppStatus)seen.last.selfpos_status));

        /* And it did not invent a position out of two satellites. */
        check(seen.last.selfpos_status != 0,
              "two satellites on one frequency do not produce a solution");
    }

    /* ── 2. An epoch that never closes does not ───────────────────── */
    {
        /* The control, and one bit away from the case above: the same
         * message type, the same path, the same observations, with
         * DF393 set on every frame so the epoch is never complete.
         * Without it the first case would pass just as well against a
         * solve run on every frame regardless of the call-site
         * condition it is there to test.
         *
         * A first attempt used a stream with no observations at all.
         * It proved nothing: with no epochs there is no stream clock,
         * so the session published **no snapshots**, and the test was
         * reading a zeroed struct -- which happens to be SPP_OK. */
        Seen seen;
        check(write_capture(dry_cap, 20, 1),
              "built a capture whose epochs never close");
        check(replay(dry_cap, &seen), "replayed that too");
        check(seen.stats_seen > 0,
              "the session published snapshots, so there is an answer to read");
        check(seen.last.selfpos_status == SPP_NO_EPOCH,
              "an epoch still open is not handed to the solve");
    }

    remove(obs_cap);
    remove(dry_cap);

    printf("\n%s\n", failures ? "FAILURES"
                              : "all self-position session cases pass");
    return failures ? 1 : 0;
}
