/**
 * @file test_eph_validity.c
 * @brief An ephemeris a day old must not pass for one an hour old.
 *
 * GLONASS broadcasts no week number: its reference epoch is Moscow
 * **seconds of day**, so the difference between "now" and a record's
 * epoch can only be computed modulo one day. A record from yesterday
 * morning therefore lands a few hours behind this morning and looked
 * current -- which is not a cosmetic fault, because the sky view would
 * place that satellite from a twenty-four-hour-old orbit and draw it
 * with the same confidence as a fresh one.
 *
 * Measured on the handset: a navigation file whose newest record was ten
 * hours old reported "newest orbit 58 min old".
 *
 * The fix is @c toe_utc, an absolute date filled in wherever one is
 * known -- which is any file. These cases pin both halves: the wrap
 * still governs a record with no absolute date (a live stream, seconds
 * old by construction), and the absolute date governs whenever it is
 * there.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
#include "core/sv_ephemeris.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

/* Moscow seconds-of-day for a Unix time, which is what GLONASS `toe`
 * holds. UTC + 3 h, wrapped into the day. */
static double moscow_sod(time_t t)
{
    double s = (double)(t % 86400) + 10800.0;
    while (s >= 86400.0) s -= 86400.0;
    return s;
}

int main(void)
{
    const time_t now = time(NULL);
    const double glo_now = moscow_sod(now);

    SvEphemeris e;

    /* ── 1. A GLONASS record from an hour ago: usable ─────────────── */
    memset(&e, 0, sizeof(e));
    e.gnss_id = 2;
    e.prn     = 1;
    e.valid   = true;
    e.toe     = moscow_sod(now - 3600);
    e.toe_utc = (double)(now - 3600);
    check(sv_eph_is_valid_at(&e, 0, glo_now),
          "GLONASS one hour old is valid");

    /* ── 2. The same record, but from yesterday ───────────────────── */
    /* Same seconds-of-day, so the wrapped difference is identical to
     * case 1 -- an hour. Only the absolute date can tell them apart, and
     * before it existed this case passed. */
    memset(&e, 0, sizeof(e));
    e.gnss_id = 2;
    e.prn     = 1;
    e.valid   = true;
    e.toe     = moscow_sod(now - 3600);          /* looks like an hour */
    e.toe_utc = (double)(now - 3600 - 86400);    /* is a day and an hour */
    check(!sv_eph_is_valid_at(&e, 0, glo_now),
          "GLONASS a day older, same time of day, is rejected");

    /* ── 3. No absolute date: the wrap still governs ──────────────── */
    /* This is the live-stream case. Nothing in a decoded RTCM 1020 says
     * which day it is for, and nothing needs to: it arrived seconds ago.
     */
    memset(&e, 0, sizeof(e));
    e.gnss_id = 2;
    e.prn     = 1;
    e.valid   = true;
    e.toe     = moscow_sod(now - 600);
    e.toe_utc = 0.0;
    check(sv_eph_is_valid_at(&e, 0, glo_now),
          "GLONASS with no absolute date falls back to the wrap");

    /* ── 4. GPS, where the wrap is a week ─────────────────────────── */
    /* A week is long enough that a day-old record is caught by the
     * wrapped difference alone -- but the absolute date must not break
     * the ordinary case. */
    const time_t GPS_EPOCH = 315964800;
    double gps_now = (double)(now - GPS_EPOCH) + 18.0;
    double tow_now = fmod(gps_now, 604800.0);

    memset(&e, 0, sizeof(e));
    e.gnss_id = 1;
    e.prn     = 5;
    e.valid   = true;
    e.toe     = fmod(tow_now - 3600.0 + 604800.0, 604800.0);
    e.toe_utc = (double)(now - 3600);
    check(sv_eph_is_valid_at(&e, 0, tow_now),
          "GPS one hour old is valid");

    memset(&e, 0, sizeof(e));
    e.gnss_id = 1;
    e.prn     = 5;
    e.valid   = true;
    e.toe     = fmod(tow_now - 6.0 * 3600.0 + 604800.0, 604800.0);
    e.toe_utc = (double)(now - 6 * 3600);
    check(!sv_eph_is_valid_at(&e, 0, tow_now),
          "GPS six hours old is rejected");

    printf("\n%s\n", failures ? "FAILED" : "all ephemeris validity cases pass");
    return failures ? 1 : 0;
}
