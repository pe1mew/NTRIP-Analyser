/**
 * @file vrs_check.c
 * @brief VRS assertion set -- implementation.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * @copyright Apache License 2.0 with Commons Clause (see LICENSE for details)
 */

#include "core/vrs_check.h"
#include <string.h>
#include <math.h>

/** @brief Great-circle distance, kilometres. */
static double haversine_km(double lat1, double lon1, double lat2, double lon2)
{
    const double R = 6371.0, D = 3.14159265358979323846 / 180.0;
    double dla = (lat2 - lat1) * D, dlo = (lon2 - lon1) * D;
    double a = sin(dla / 2) * sin(dla / 2) +
               cos(lat1 * D) * cos(lat2 * D) * sin(dlo / 2) * sin(dlo / 2);
    return 2.0 * R * atan2(sqrt(a), sqrt(1.0 - a));
}

void vrs_run_start(VrsRun *run, double now)
{
    if (!run) return;
    memset(run, 0, sizeof(*run));
    run->t_start       = now;
    run->t_first_gga   = -1.0;
    run->t_first_frame = -1.0;
    run->t_disconnect  = -1.0;
    run->t_gate_start  = -1.0;
}

void vrs_note_gga(VrsRun *run, const NsStatsSnapshot *s, double now,
                  double lat, double lon)
{
    if (!run) return;
    if (run->t_first_gga < 0.0) {
        run->t_first_gga   = now;
        run->frames_at_gga = s ? s->frames_ok : 0;
    }
    run->gga_lat = lat;
    run->gga_lon = lon;
}

void vrs_begin_gate_test(VrsRun *run, double now)
{
    if (run && run->t_gate_start < 0.0) run->t_gate_start = now;
}

const char *vrs_gate_name(int gate)
{
    switch (gate) {
    case VRS_GATE_TESTING:   return "testing";
    case VRS_GATE_GATED:     return "GGA-gated (network service)";
    case VRS_GATE_NOT_GATED: return "not gated (fixed base?)";
    default:                 return "untested";
    }
}

void vrs_update(VrsRun *run, const NsStatsSnapshot *s, double now,
                VrsReport *out)
{
    if (!run || !s || !out) return;
    memset(out, 0, sizeof(*out));
    VrsResult *a = out->a;

    /* Track the connection edge and the first frame after the GGA. */
    if (s->connected) run->was_connected = true;
    else if (run->was_connected && run->t_disconnect < 0.0)
        run->t_disconnect = now;
    if (run->t_first_gga >= 0.0 && run->t_first_frame < 0.0 &&
        s->frames_ok > run->frames_at_gga)
        run->t_first_frame = now;

    double since_gga = (run->t_first_gga >= 0.0) ? now - run->t_first_gga
                                                 : -1.0;

    /* ── A1: GGA accepted ───────────────────────────────────────────── */
    a[0].label = "GGA accepted by caster";
    if (since_gga < 0.0) {
        a[0].verdict = KPI_PENDING;
        a[0].detail  = "No GGA sent yet";
    } else if (run->t_disconnect >= 0.0 &&
               run->t_disconnect - run->t_first_gga <= VRS_ACCEPT_S &&
               run->t_gate_start < 0.0) {
        a[0].verdict = KPI_FAIL;
        a[0].value   = run->t_disconnect - run->t_first_gga;
        a[0].detail  = "Caster dropped the stream on receiving the GGA";
    } else if (since_gga < VRS_ACCEPT_S) {
        a[0].verdict = KPI_PENDING;
        a[0].detail  = "Watching for a rejection";
    } else {
        a[0].verdict = KPI_PASS;
        a[0].value   = since_gga;
        a[0].detail  = "No disconnect within 5 s of the first GGA";
    }

    /* ── A2: RTCM starts after GGA ──────────────────────────────────── */
    a[1].label = "RTCM after GGA";
    if (since_gga < 0.0) {
        a[1].verdict = KPI_PENDING;
        a[1].detail  = "No GGA sent yet";
    } else if (run->t_first_frame >= 0.0) {
        double dt = run->t_first_frame - run->t_first_gga;
        a[1].value = dt;
        a[1].verdict = (dt <= VRS_RTCM_S) ? KPI_PASS : KPI_WARN;
        a[1].detail  = (dt <= VRS_RTCM_S)
                       ? "Corrections flowing within 10 s of the GGA"
                       : "Corrections started, but slower than 10 s";
    } else if (since_gga <= VRS_RTCM_S) {
        a[1].verdict = KPI_PENDING;
        a[1].detail  = "Waiting for the first frame";
    } else {
        a[1].verdict = KPI_FAIL;
        a[1].value   = since_gga;
        a[1].detail  = "No RTCM within 10 s of the GGA";
    }

    /* ── A3: ARP near the rover ─────────────────────────────────────── */
    a[2].label = "ARP near rover position";
    if (!s->arp_valid || since_gga < 0.0) {
        a[2].verdict = KPI_PENDING;
        a[2].detail  = "Needs a broadcast ARP and a sent GGA";
    } else {
        double km = haversine_km(run->gga_lat, run->gga_lon,
                                 s->arp_lat, s->arp_lon);
        a[2].value = km;
        if (km <= VRS_ARP_MAX_KM) {
            a[2].verdict = KPI_PASS;
            a[2].detail  = "Reference position within 50 km of the GGA";
        } else if (km <= 2.0 * VRS_ARP_MAX_KM) {
            a[2].verdict = KPI_WARN;
            a[2].detail  = "ARP 50-100 km away: nearest-station service?";
        } else {
            a[2].verdict = KPI_FAIL;
            a[2].detail  = "ARP implausibly far from the GGA position";
        }
    }

    /* ── A4: keep-alive holds ───────────────────────────────────────── */
    a[3].label = "Keep-alive holds";
    if (since_gga < 0.0) {
        a[3].verdict = KPI_PENDING;
        a[3].detail  = "No GGA sent yet";
    } else if (run->t_disconnect >= 0.0 && run->t_gate_start < 0.0) {
        a[3].verdict = KPI_FAIL;
        a[3].value   = run->t_disconnect - run->t_first_gga;
        a[3].detail  = "Stream dropped despite the GGA cadence";
    } else if (since_gga >= VRS_HOLD_S) {
        a[3].verdict = KPI_PASS;
        a[3].value   = since_gga;
        a[3].detail  = "Continuous for 60 s at the GGA cadence";
    } else {
        a[3].verdict = KPI_PENDING;
        a[3].value   = since_gga;
        a[3].detail  = "Holding; window not yet complete";
    }

    /* ── A5: gate classification ────────────────────────────────────── */
    a[4].label = "GGA gating";
    if (run->t_gate_start < 0.0) {
        out->gate    = VRS_GATE_UNTESTED;
        a[4].verdict = KPI_PENDING;
        a[4].detail  = "Gate test not started";
    } else if (run->t_disconnect >= 0.0 &&
               run->t_disconnect >= run->t_gate_start) {
        out->gate    = VRS_GATE_GATED;
        a[4].verdict = KPI_PASS;
        a[4].value   = run->t_disconnect - run->t_gate_start;
        a[4].detail  = "Stream dropped after GGA stopped: a live network service";
    } else if (now - run->t_gate_start > VRS_GATE_S) {
        /* Deliberately not a failure: a fixed base ignoring GGA is
         * behaving correctly for what it is.  The classification is the
         * result. */
        out->gate    = VRS_GATE_NOT_GATED;
        a[4].verdict = KPI_WARN;
        a[4].value   = now - run->t_gate_start;
        a[4].detail  = "Still streaming 90 s after GGA stopped: fixed base";
    } else {
        out->gate    = VRS_GATE_TESTING;
        a[4].verdict = KPI_PENDING;
        a[4].value   = now - run->t_gate_start;
        a[4].detail  = "GGA stopped; watching for the drop";
    }

    /* ── Roll-up ────────────────────────────────────────────────────── */
    out->failed = false;
    bool resolved = true;
    for (int i = 0; i < 4; i++) {
        if (a[i].verdict == KPI_FAIL) out->failed = true;
        if (a[i].verdict == KPI_PENDING) resolved = false;
    }
    out->complete = resolved &&
                    (run->t_gate_start < 0.0 || out->gate == VRS_GATE_GATED
                                             || out->gate == VRS_GATE_NOT_GATED);
}
