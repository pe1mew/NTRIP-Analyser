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
 * @brief One MSM7 message: two satellites, one or two signals each.
 *
 * Deliberately thin -- a richer constellation would test the
 * arithmetic, which is tested elsewhere. What matters here is which
 * *reason* the solver gives, because each reason proves how far the
 * observations got:
 *
 * - `num_sigs == 1` -> the satellites are refused as single-frequency,
 *   which proves only that cells were examined;
 * - `num_sigs == 2` -> they pass that gate and are refused for having
 *   no orbit, which proves they were examined **as usable
 *   satellites**. With no ephemerides stored, that is as far as any
 *   satellite can get here, and it is far enough to tell a surviving
 *   epoch from a discarded one.
 *
 * The signal mask bits are 0-based indices into the system's signal
 * table: bit 1 is GPS 1C and bit 9 is GPS 2W, two carriers far enough
 * apart for the iono-free combination the solver forms.
 *
 * DF393 (bit 54) set says another frame of the same epoch follows.
 */
static int build_msm7(unsigned char *out, int out_cap, int msg_type,
                      unsigned long epoch_ms, int more, int num_sigs)
{
    const int num_sats = 2;
    const int num_cells = num_sats * num_sigs;
    memset(out, 0, (size_t)out_cap);

    put_bits(out, 0, 12, (unsigned long)msg_type);
    put_bits(out, 12, 12, 1234);           /* reference station id  */
    put_bits(out, 24, 30, epoch_ms);       /* epoch, on its own scale */
    put_bits(out, 54, 1, more ? 1UL : 0UL);

    put_bits(out, 73 + 2, 1, 1);           /* PRN 3 */
    put_bits(out, 73 + 8, 1, 1);           /* PRN 9 */
    put_bits(out, 137 + 1, 1, 1);          /* signal 1C */
    if (num_sigs > 1)
        put_bits(out, 137 + 9, 1, 1);      /* signal 2W */

    const int cell_mask_start = 169;
    for (int i = 0; i < num_cells; i++)
        put_bits(out, cell_mask_start + i, 1, 1);

    /* Satellite block: 75 ms and 80 ms of travel, both inside the band
     * a real GNSS range occupies. MSM7 carries the extended fields. */
    int p = cell_mask_start + num_cells;
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
        /* With `more` set every frame carries the **same** epoch: one
         * set, never completed. Advancing the epoch instead would be a
         * base whose DF393 is stuck, and those must be solved -- each
         * new bundle completing the one before it. */
        const unsigned long epoch = more ? 86400000UL
                                         : 86400000UL + (unsigned long)i * 1000UL;
        int plen = build_msm7(payload, (int)sizeof payload, 1077,
                              epoch, more, 1);
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

/**
 * @brief A bundle where one constellation arrives in two frames.
 *
 * What a real station does, and what the multiple-message bit exists to
 * express. RFSEE01 sends BeiDou as 48 cells and then 5, and the epoch
 * rule treated the second frame as proof that a new bundle had begun:
 * it reset the set, discarding GPS, GLONASS, Galileo and BeiDou's own
 * first frame. The epoch that reached the solver held 6 cells of the
 * 128 that had arrived, not one of them GPS -- so a station streaming
 * ten GPS satellites reported *fewer than four usable satellites*.
 *
 * GPS first, then the same system twice, the second closing the set.
 */
static int write_split_capture(const char *path, int n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    for (int i = 0; i < n; i++) {
        const unsigned long epoch = 86400000UL + (unsigned long)i * 1000UL;
        /* GPS carries two signals, so it reaches the orbit lookup and
         * is refused there. BeiDou carries one and is refused earlier.
         * The two reasons are what let this case tell a surviving
         * epoch from a discarded one. */
        const struct { int type; int more; int sigs; } frames[] = {
            { 1077, 1, 2 },   /* GPS, dual frequency                 */
            { 1127, 1, 1 },   /* BeiDou, first frame                 */
            { 1127, 0, 1 },   /* BeiDou again, same epoch: closes it */
        };
        for (size_t k = 0; k < sizeof frames / sizeof frames[0]; k++) {
            unsigned char payload[512], frame[560];
            int plen = build_msm7(payload, (int)sizeof payload,
                                  frames[k].type, epoch, frames[k].more,
                                  frames[k].sigs);
            int flen = build_frame(frame, payload, plen);
            fwrite(frame, 1, (size_t)flen, f);
        }
    }
    fclose(f);
    return 1;
}

int main(void)
{
    const char *obs_cap = "test_selfpos_obs.rtcm3";
    const char *dry_cap = "test_selfpos_dry.rtcm3";
    const char *spl_cap = "test_selfpos_split.rtcm3";
    remove(obs_cap);
    remove(dry_cap);
    remove(spl_cap);

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

    /* ── 2. A set still being filled is not solved ────────────────── */
    {
        /* The control, and one bit away from the case above: the same
         * message type, the same path, the same observations, with
         * DF393 set on every frame **and one epoch only**, so the set
         * is still being filled when the stream ends. Without a
         * control the first case would pass just as well against a
         * solve run on every frame regardless.
         *
         * Two shapes were tried first and proved nothing, both because
         * **a snapshot is paced by the stream clock**:
         *
         * - a stream with no observations at all: no epochs, no clock,
         *   so the session published no snapshots and the test read a
         *   zeroed struct -- which happens to be `SPP_OK`;
         * - twenty frames of one epoch, never completed: the clock
         *   never advanced, so again no snapshots. (Twenty *different*
         *   epochs is not the control either -- that is a base whose
         *   DF393 is stuck, which case 4 says must be solved.)
         *
         * What works is a complete bundle followed by an incomplete
         * one. The first sets a status, the clock advances so there
         * are snapshots, and the trailing partial bundle -- a single
         * Galileo frame, single-frequency, never closed -- must leave
         * the answer alone. A solve that ran per frame would overwrite
         * it with `SPP_SINGLE_FREQ`. */
        Seen seen;
        FILE *f = fopen(dry_cap, "wb");
        check(f != NULL, "built a complete bundle, then an incomplete one");
        if (f) {
            unsigned char payload[512], frame[560];
            int plen, flen;
            /* Twenty complete bundles: GPS dual-frequency, then
             * Galileo clearing DF393. Twenty because a snapshot is
             * published on the stream's own clock, and two epochs do
             * not advance it far enough for one to be published at
             * all -- which is how the previous two attempts at this
             * control came to read a zeroed struct. */
            for (int i = 0; i < 20; i++) {
                const unsigned long epoch =
                    86400000UL + (unsigned long)i * 1000UL;
                plen = build_msm7(payload, (int)sizeof payload, 1077,
                                  epoch, 1, 2);
                flen = build_frame(frame, payload, plen);
                fwrite(frame, 1, (size_t)flen, f);
                plen = build_msm7(payload, (int)sizeof payload, 1097,
                                  epoch, 0, 1);
                flen = build_frame(frame, payload, plen);
                fwrite(frame, 1, (size_t)flen, f);
            }
            /* Incomplete, and the last thing in the stream. */
            plen = build_msm7(payload, (int)sizeof payload, 1097,
                              86420000UL, 1, 1);
            flen = build_frame(frame, payload, plen);
            fwrite(frame, 1, (size_t)flen, f);
            fclose(f);
        }
        check(replay(dry_cap, &seen), "replayed that too");
        check(seen.stats_seen > 0,
              "the session published snapshots, so there is an answer to read");
        printf("      status after a trailing partial bundle: %d (%s)\n",
               seen.last.selfpos_status,
               spp_status_text((SppStatus)seen.last.selfpos_status));
        check(seen.last.selfpos_status == SPP_NO_EPHEMERIS,
              "a set still being filled is not solved, and does not "
              "overwrite the answer from the one before it");
    }

    /* ── 3. A constellation split across two frames ───────────────── */
    {
        Seen seen;
        check(write_split_capture(spl_cap, 20),
              "built a capture where one system arrives in two frames");
        check(replay(spl_cap, &seen), "replayed it");

        /* The GPS satellites arrived in the first frame of the bundle.
         * If the split reset the set they are gone, and the solver --
         * seeing no GPS at all -- reports TOO_FEW_SATS without ever
         * examining one. If they survived, the solver examines them,
         * finds no orbits for them, and says *that* instead. The
         * difference between the two statuses is the whole defect. */
        printf("      status after a split bundle: %d (%s)\n",
               seen.last.selfpos_status,
               spp_status_text((SppStatus)seen.last.selfpos_status));
        check(seen.last.selfpos_status == SPP_NO_EPHEMERIS,
              "a system continued across frames does not discard the epoch");
    }

    /* ── 4. A base whose DF393 never clears ───────────────────────── */
    {
        /* RFSEE01, an hour after case 3 was written: NavIC dropped out
         * of view, and with it the only frame that had been clearing
         * the multiple-message bit. Every frame then said "more
         * follows", no set ever closed, and the window read "waiting
         * for the first observation epoch" through forty-one snapshots
         * of a stream carrying ten GPS satellites.
         *
         * Bundles of two systems, DF393 set on every frame, a new
         * epoch each second. The arrival of the next bundle is the only
         * evidence the last one is complete, and it has to be enough. */
        const char *stuck_cap = "test_selfpos_stuck.rtcm3";
        remove(stuck_cap);

        FILE *f = fopen(stuck_cap, "wb");
        check(f != NULL, "built a capture whose DF393 never clears");
        if (f) {
            for (int i = 0; i < 20; i++) {
                const unsigned long epoch =
                    86400000UL + (unsigned long)i * 1000UL;
                const struct { int type; int sigs; } frames[] = {
                    { 1077, 2 },     /* GPS, dual frequency */
                    { 1097, 1 },     /* Galileo             */
                };
                for (size_t k = 0; k < sizeof frames / sizeof frames[0]; k++) {
                    unsigned char payload[512], frame[560];
                    int plen = build_msm7(payload, (int)sizeof payload,
                                          frames[k].type, epoch,
                                          1 /* more: always set */,
                                          frames[k].sigs);
                    int flen = build_frame(frame, payload, plen);
                    fwrite(frame, 1, (size_t)flen, f);
                }
            }
            fclose(f);
        }

        Seen seen;
        check(replay(stuck_cap, &seen), "replayed it");
        printf("      status with DF393 stuck: %d (%s)\n",
               seen.last.selfpos_status,
               spp_status_text((SppStatus)seen.last.selfpos_status));
        check(seen.last.selfpos_status == SPP_NO_EPHEMERIS,
              "a bundle completed by the next one is still solved");
        remove(stuck_cap);
    }

    remove(obs_cap);
    remove(dry_cap);
    remove(spl_cap);

    printf("\n%s\n", failures ? "FAILURES"
                              : "all self-position session cases pass");
    return failures ? 1 : 0;
}
