/**
 * @file test_vrs.c
 * @brief The network-RTK assertions, driven through both endings.
 *
 * `vrs_check.c` shipped with `--check-vrs` and had no test at all: the
 * only thing that had ever exercised it was a live caster, which is the
 * one instrument a CI run does not have.  This drives the engine
 * through a gated service, a fixed base, and each way an assertion can
 * fail -- V1 of `design/work-items/vrs-on-the-phone.md`, written before
 * the phone becomes the engine's third caller.
 *
 * No sockets, deliberately.  The plan said "loopback", the harness
 * `test_failure.c` uses; reading the engine showed better.  `vrs_update`
 * takes a snapshot struct and the caller's clock and touches nothing
 * else, so a synthetic snapshot and a driven clock test exactly the
 * contract every frontend uses -- and a 60 s hold window or a 90 s gate
 * deadline costs nothing, because `now` is ours.  A loopback would have
 * re-tested the session layer (test_stall, test_failure) and made CI
 * wait out real minutes.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
#include "core/vrs_check.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

/* ── A stream that behaves as told ───────────────────────────────── */

/** The rover the GGA claims; the ARP sits ~7 km north of it. */
#define ROVER_LAT 52.0
#define ROVER_LON 5.0

static void snap_reset(NsStatsSnapshot *s)
{
    memset(s, 0, sizeof(*s));
    s->connected = true;
}

/** A caster that answered: frames counted, ARP broadcast @p km north. */
static void snap_streaming(NsStatsSnapshot *s, uint64_t frames, double km)
{
    snap_reset(s);
    s->frames_ok = frames;
    s->arp_valid = true;
    s->arp_lat   = ROVER_LAT + km / 111.0;   /* ~111 km per degree */
    s->arp_lon   = ROVER_LON;
}

/* ── The two endings ─────────────────────────────────────────────── */

/**
 * @brief A live network service: streams on GGA, drops when it stops.
 *
 * The happy path end to end, on the CLI's own cadence: GGA at t=0,
 * frames two seconds later, the hold window waited out, the gate test
 * entered, the drop seen.  Every assertion must resolve and the
 * classification must be GATED.
 */
static void case_gated(void)
{
    printf("-- a GGA-gated network service\n");
    VrsRun run; VrsReport r;
    NsStatsSnapshot s;

    vrs_run_start(&run, 0.0, NULL);

    /* Before any GGA: everything pending, nothing failed. */
    snap_reset(&s);
    vrs_update(&run, &s, 0.0, &r);
    check(r.a[0].verdict == KPI_PENDING && !r.failed && !r.complete,
          "before the first GGA every assertion is pending");

    /* GGA goes out; corrections start two seconds later. */
    vrs_note_gga(&run, &s, 0.0, ROVER_LAT, ROVER_LON);
    snap_streaming(&s, 100, 7.0);
    vrs_update(&run, &s, 2.0, &r);
    check(r.a[1].verdict == KPI_PASS, "A2 passes on frames inside the deadline");
    check(r.a[1].value == 2.0,        "A2 reports when the first frame came");
    check(r.a[0].verdict == KPI_PENDING,
          "A1 still pending inside the acceptance window");

    /* Past the acceptance window, still connected. */
    vrs_update(&run, &s, 6.0, &r);
    check(r.a[0].verdict == KPI_PASS, "A1 passes once the window closes clean");
    check(r.a[2].verdict == KPI_PASS, "A3 passes with the ARP 7 km away");
    check(r.a[3].verdict == KPI_PENDING, "A4 pending before the hold window");

    /* The hold window passes at the GGA cadence. */
    vrs_update(&run, &s, 61.0, &r);
    check(r.a[3].verdict == KPI_PASS, "A4 passes after the hold window");
    /* The header's contract: complete means A1..A4 resolved, and the
     * gate only if it was started.  A caller that never enters the
     * gate still gets a finished report -- the CLI's --check-vrs can
     * end on a failure before the gate, and "complete" must not hold
     * that report open for a test that will never run. */
    check(r.complete, "complete once A1..A4 resolve, before any gate");
    check(r.gate == VRS_GATE_UNTESTED, "the gate reads untested until entered");

    /* Gate test: GGA stops; the caster drops 40 s later. */
    vrs_begin_gate_test(&run, 61.0);
    vrs_update(&run, &s, 80.0, &r);
    check(r.gate == VRS_GATE_TESTING, "gate is testing while the stream holds");
    s.connected = false;
    vrs_update(&run, &s, 101.0, &r);
    check(r.gate == VRS_GATE_GATED, "the drop after GGA stopped means GATED");
    check(r.a[4].verdict == KPI_PASS, "A5 reads the drop as a pass");
    check(r.complete && !r.failed, "the run is complete and nothing failed");
    check(r.a[0].verdict == KPI_PASS && r.a[3].verdict == KPI_PASS,
          "the gate's own disconnect fails neither A1 nor A4");
}

/**
 * @brief A fixed base: ignores the GGA, never drops.
 *
 * A5's other branch, and the one a pass/fail mindset gets wrong: still
 * streaming past the deadline is a *classification* -- WARN, never
 * FAIL, `failed` false.
 */
static void case_fixed_base(void)
{
    printf("-- a fixed base that ignores GGA\n");
    VrsRun run; VrsReport r;
    NsStatsSnapshot s;

    vrs_run_start(&run, 0.0, NULL);
    snap_streaming(&s, 50, 3.0);
    vrs_note_gga(&run, &s, 0.0, ROVER_LAT, ROVER_LON);
    snap_streaming(&s, 500, 3.0);
    vrs_update(&run, &s, 61.0, &r);

    vrs_begin_gate_test(&run, 61.0);
    vrs_update(&run, &s, 130.0, &r);
    check(r.gate == VRS_GATE_TESTING, "still testing inside the gate deadline");
    vrs_update(&run, &s, 152.0, &r);
    check(r.gate == VRS_GATE_NOT_GATED,
          "streaming past the deadline means NOT gated");
    check(r.a[4].verdict == KPI_WARN,
          "a fixed base is a WARN classification, not a failure");
    check(r.complete && !r.failed,
          "the run completes without a failure: correct for what it is");
}

/* ── Each way in which an assertion fails ────────────────────────── */

static void case_failures(void)
{
    printf("-- the failure branches\n");
    VrsRun run; VrsReport r;
    NsStatsSnapshot s;

    /* A1: the caster drops the moment it sees the GGA.  The engine
     * sees the world only through updates, so the connection must be
     * shown to it once before the drop can be one -- exactly as a real
     * caller polls each snapshot. */
    vrs_run_start(&run, 0.0, NULL);
    snap_reset(&s);
    vrs_note_gga(&run, &s, 0.0, ROVER_LAT, ROVER_LON);
    vrs_update(&run, &s, 1.0, &r);
    s.connected = false;
    vrs_update(&run, &s, 3.0, &r);
    check(r.a[0].verdict == KPI_FAIL && r.failed,
          "A1 fails on a disconnect right after the GGA");

    /* A2: connected, but no correction ever comes. */
    vrs_run_start(&run, 0.0, NULL);
    snap_reset(&s);
    vrs_note_gga(&run, &s, 0.0, ROVER_LAT, ROVER_LON);
    vrs_update(&run, &s, 11.0, &r);
    check(r.a[1].verdict == KPI_FAIL && r.failed,
          "A2 fails when no RTCM arrives inside the deadline");

    /* A3: the broadcast ARP is nowhere near the claimed rover. */
    vrs_run_start(&run, 0.0, NULL);
    snap_streaming(&s, 10, 300.0);
    vrs_note_gga(&run, &s, 0.0, ROVER_LAT, ROVER_LON);
    vrs_update(&run, &s, 6.0, &r);
    check(r.a[2].verdict == KPI_FAIL,
          "A3 fails with the ARP 300 km from the GGA");

    /* ...but between one and two ceilings reads as nearest-station. */
    vrs_run_start(&run, 0.0, NULL);
    snap_streaming(&s, 10, 80.0);
    vrs_note_gga(&run, &s, 0.0, ROVER_LAT, ROVER_LON);
    vrs_update(&run, &s, 6.0, &r);
    check(r.a[2].verdict == KPI_WARN,
          "A3 warns between the ceiling and twice it: nearest-station");

    /* A4: the stream dies mid-hold with the GGA cadence kept. */
    vrs_run_start(&run, 0.0, NULL);
    snap_streaming(&s, 10, 5.0);
    vrs_note_gga(&run, &s, 0.0, ROVER_LAT, ROVER_LON);
    vrs_update(&run, &s, 20.0, &r);
    s.connected = false;
    vrs_update(&run, &s, 30.0, &r);
    check(r.a[3].verdict == KPI_FAIL && r.failed,
          "A4 fails on a drop inside the hold window");
}

/* ── The deadlines are policy, not constants ─────────────────────── */

static void case_policy(void)
{
    printf("-- the policy is honoured\n");
    VrsRun run; VrsReport r;
    NsStatsSnapshot s;

    /* A frame at 7 s: inside the default deadline, past a halved one.
     * This is the falsification the plan asks for, kept as a living
     * assertion rather than a one-off experiment. */
    VrsPolicy pol;
    vrs_policy_defaults(&pol);
    pol.rtcm_s = VRS_RTCM_S / 2.0;

    vrs_run_start(&run, 0.0, &pol);
    snap_reset(&s);
    vrs_note_gga(&run, &s, 0.0, ROVER_LAT, ROVER_LON);
    snap_streaming(&s, 10, 5.0);
    vrs_update(&run, &s, 7.0, &r);
    check(r.a[1].verdict == KPI_WARN,
          "a frame at 7 s is late against a 5 s policy");

    vrs_run_start(&run, 0.0, NULL);
    snap_reset(&s);
    vrs_note_gga(&run, &s, 0.0, ROVER_LAT, ROVER_LON);
    snap_streaming(&s, 10, 5.0);
    vrs_update(&run, &s, 7.0, &r);
    check(r.a[1].verdict == KPI_PASS,
          "the same frame is in time against the default 10 s");
}

int main(void)
{
    case_gated();
    case_fixed_base();
    case_failures();
    case_policy();

    printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
