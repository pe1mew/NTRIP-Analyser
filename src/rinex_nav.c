/**
 * @file rinex_nav.c
 * @brief Minimal RINEX 3 navigation file loader.
 *
 * Reads broadcast ephemerides from a RINEX 3 NAV file and pushes them
 * into the per-SV ephemeris cache (sv_ephemeris.h) so the Sky Plot can
 * propagate them without an NTRIP eph stream.
 *
 * Record format follows RINEX 3.x:
 *   - GPS / Galileo / QZSS / BeiDou (G, E, J, C): 8 lines, Keplerian.
 *   - GLONASS (R)                              : 4 lines, state vector.
 *   - SBAS (S), IRNSS (I)                       : ignored.
 *
 * Each data line carries up to 4 floats in Fortran "1.234D+05" notation;
 * we substitute 'D' -> 'E' before calling atof.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "rinex_nav.h"
#include "sv_ephemeris.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ── Helpers ──────────────────────────────────────────────────────────── */

/* Parse a 19-character Fortran "D" float starting at @p src.  Substitutes
 * the 'D' exponent with 'E' so atof can read it.  Empty / whitespace
 * fields return 0.0. */
static double rnx_float(const char *src, int len)
{
    char buf[24];
    if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;
    memcpy(buf, src, len);
    buf[len] = '\0';

    /* Trim trailing whitespace */
    while (len > 0 && isspace((unsigned char)buf[len - 1])) buf[--len] = '\0';
    /* Check for empty after trim */
    int has_nonspace = 0;
    for (int i = 0; i < len; i++)
        if (!isspace((unsigned char)buf[i])) { has_nonspace = 1; break; }
    if (!has_nonspace) return 0.0;

    /* Substitute Fortran exponent character */
    for (int i = 0; i < len; i++) {
        if (buf[i] == 'D' || buf[i] == 'd') buf[i] = 'E';
    }
    return atof(buf);
}

/* Read up to 4 floats from a RINEX data line.  Data fields start at
 * column 4 (after a leading "    " indent), each 19 chars wide.
 * If the line is short, missing fields are returned as 0. */
static void rnx_read_4(const char *line, double out[4])
{
    int len = (int)strlen(line);
    for (int i = 0; i < 4; i++) {
        int start = 4 + i * 19;
        if (start >= len) {
            out[i] = 0.0;
            continue;
        }
        int field_len = (start + 19 <= len) ? 19 : (len - start);
        out[i] = rnx_float(line + start, field_len);
    }
}

/* Strip the trailing CR/LF from a line read with fgets. */
static void rnx_chomp(char *line)
{
    size_t n = strlen(line);
    while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';
}

/* Convert a (year, month, day, hour, min, sec) UTC datetime into the
 * GPS week / seconds-of-week pair (no leap-second correction; sky plot
 * doesn't need it).  We only need the seconds-of-week for our TOW-only
 * validity logic; week is returned for completeness. */
static void rnx_to_gps_tow(int year, int month, int day,
                           int hour, int min, double sec,
                           int *out_week, double *out_tow)
{
    /* Days since Unix epoch via mktime (UTC).  Then offset to GPS epoch. */
    struct tm tm_u;
    memset(&tm_u, 0, sizeof(tm_u));
    tm_u.tm_year = year - 1900;
    tm_u.tm_mon  = month - 1;
    tm_u.tm_mday = day;
    tm_u.tm_hour = hour;
    tm_u.tm_min  = min;
    tm_u.tm_sec  = (int)sec;
    /* mktime treats tm as local time -- use _mkgmtime on Windows for UTC. */
#if defined(_WIN32)
    time_t unix_sec = _mkgmtime(&tm_u);
#else
    time_t unix_sec = timegm(&tm_u);
#endif
    if (unix_sec < 0) {
        if (out_week) *out_week = 0;
        if (out_tow)  *out_tow  = 0.0;
        return;
    }
    const time_t GPS_EPOCH_UNIX = 315964800; /* 1980-01-06 UTC */
    double gps_sec = (double)(unix_sec - GPS_EPOCH_UNIX) + (sec - (int)sec);
    int week = (int)(gps_sec / 604800.0);
    if (out_week) *out_week = week;
    if (out_tow)  *out_tow  = gps_sec - (double)week * 604800.0;
}

/* Moscow seconds-of-day from a UTC date+time (UTC + 3h, no leap secs). */
static double rnx_to_glo_tod(int hour, int min, double sec)
{
    double sod = (double)hour * 3600.0 + (double)min * 60.0 + sec + 10800.0;
    while (sod >= 86400.0) sod -= 86400.0;
    while (sod <    0.0)   sod += 86400.0;
    return sod;
}

/* ── Per-GNSS record parsers ──────────────────────────────────────────── */

/* Common Keplerian record: GPS (G), Galileo (E), QZSS (J), BeiDou (C).
 * Records are 8 lines (the first one being the sat-id/date/clock line).
 * Returns 1 if a SvEphemeris was stored, 0 otherwise. */
static int parse_keplerian_record(int gnss_id, int prn,
                                  int year, int month, int day,
                                  int hour, int min, double sec,
                                  double af0, double af1, double af2,
                                  const char *L1, const char *L2,
                                  const char *L3, const char *L4,
                                  const char *L5, const char *L6,
                                  const char *L7)
{
    double v1[4], v2[4], v3[4], v4[4], v5[4], v6[4];
    rnx_read_4(L1, v1);  /* IODE/IODnav/AODE, Crs, Δn, M0 */
    rnx_read_4(L2, v2);  /* Cuc, e, Cus, sqrt(A) */
    rnx_read_4(L3, v3);  /* toe, Cic, OMEGA0, Cis */
    rnx_read_4(L4, v4);  /* i0, Crc, omega, OMEGADOT */
    rnx_read_4(L5, v5);  /* IDOT, codes/sources, GPS-week, L2P/spare */
    rnx_read_4(L6, v6);  /* SV accuracy, SV health, TGD, IODC/BGD */
    (void)L7;            /* tx-time and friends — not used for the sky plot */

    SvEphemeris eph;
    memset(&eph, 0, sizeof(eph));
    eph.gnss_id     = gnss_id;
    eph.prn         = prn;
    eph.iode_iodnav = (int)v1[0];
    eph.week        = (int)v5[2];

    eph.crs       = v1[1];
    eph.delta_n   = v1[2];
    eph.m0        = v1[3];
    eph.cuc       = v2[0];
    eph.e         = v2[1];
    eph.cus       = v2[2];
    eph.sqrt_a    = v2[3];
    eph.toe       = v3[0];
    eph.cic       = v3[1];
    eph.omega0    = v3[2];
    eph.cis       = v3[3];
    eph.i0        = v4[0];
    eph.crc       = v4[1];
    eph.omega     = v4[2];
    eph.omega_dot = v4[3];
    eph.idot      = v5[0];

    eph.af0 = af0;
    eph.af1 = af1;
    eph.af2 = af2;

    /* Approximate toc from the date line.  Cache uses TOW-only matching
     * so the precision required is just within the validity window. */
    double toc_tow;
    rnx_to_gps_tow(year, month, day, hour, min, sec, NULL, &toc_tow);
    eph.toc = toc_tow;

    eph.health = (int)v6[1];

    sv_eph_store(&eph);
    return 1;
}

/* GLONASS record: 4 lines.  First line carries clock_bias + gamma_n +
 * message frame time.  Lines 1-3 are position/velocity/acceleration in
 * km / km/s / km/s^2, plus health, freq channel, age_of_oper_info. */
static int parse_glonass_record(int prn,
                                int year, int month, int day,
                                int hour, int min, double sec,
                                double clock_bias, double gamma_n,
                                double frame_time,
                                const char *L1, const char *L2,
                                const char *L3)
{
    (void)clock_bias; (void)gamma_n; (void)frame_time;

    double v1[4], v2[4], v3[4];
    rnx_read_4(L1, v1);   /* X, Vx, Ax, health */
    rnx_read_4(L2, v2);   /* Y, Vy, Ay, freq channel */
    rnx_read_4(L3, v3);   /* Z, Vz, Az, age of operation info */

    SvEphemeris eph;
    memset(&eph, 0, sizeof(eph));
    eph.gnss_id      = 2;             /* GLONASS */
    eph.prn          = prn;
    eph.iode_iodnav  = 0;
    eph.week         = 0;

    /* km / km/s / km/s^2 -> m / m/s / m/s^2 */
    eph.glo_pos[0]   = v1[0] * 1000.0;
    eph.glo_vel[0]   = v1[1] * 1000.0;
    eph.glo_acc[0]   = v1[2] * 1000.0;

    eph.glo_pos[1]   = v2[0] * 1000.0;
    eph.glo_vel[1]   = v2[1] * 1000.0;
    eph.glo_acc[1]   = v2[2] * 1000.0;

    eph.glo_pos[2]   = v3[0] * 1000.0;
    eph.glo_vel[2]   = v3[1] * 1000.0;
    eph.glo_acc[2]   = v3[2] * 1000.0;

    eph.glo_freq_chan = (int)v2[3];
    eph.health        = (int)v1[3];

    /* Reference epoch tb: convert the UTC datetime in the record header
     * to Moscow seconds-of-day, matching what kepler_to_ecef / glonass
     * propagator expect via the half-day-wrap validity logic. */
    eph.glo_tb_sod = rnx_to_glo_tod(hour, min, sec);
    eph.toe        = eph.glo_tb_sod;
    eph.toc        = eph.glo_tb_sod;

    sv_eph_store(&eph);
    (void)year; (void)month; (void)day;     /* date not needed by sky plot */
    return 1;
}

/* ── Top-level loader ─────────────────────────────────────────────────── */

int rinex_nav_load(const char *filename, int *out_counts)
{
    int counts[RINEX_NAV_MAX_GNSS] = { 0 };
    if (!filename) return -1;

    FILE *f = fopen(filename, "r");
    if (!f) return -1;

    char line[256];

    /* Skip header */
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "END OF HEADER")) break;
    }

    /* Read records */
    while (fgets(line, sizeof(line), f)) {
        rnx_chomp(line);

        /* Need at least a sat-id + date in the first 22 characters */
        if (strlen(line) < 22) continue;

        char sys = line[0];
        if (sys != 'G' && sys != 'R' && sys != 'E' &&
            sys != 'J' && sys != 'C') {
            /* Skip the entire record.  For S/I and friends we don't know
             * the line count without spec lookup, so consume a default 4
             * follow-up lines and resync.  Worst case: a small drift that
             * a later sat-id line corrects on its own. */
            for (int i = 0; i < 4; i++)
                if (!fgets(line, sizeof(line), f)) break;
            continue;
        }

        int prn   = atoi(line + 1);
        int year  = atoi(line + 4);
        int month = atoi(line + 9);
        int day   = atoi(line + 12);
        int hour  = atoi(line + 15);
        int min   = atoi(line + 18);
        double sec = atof(line + 21);

        double clock0[4];
        rnx_read_4(line, clock0);   /* fields beyond char 23: af0, af1, af2 */
        double af0 = clock0[1];
        double af1 = clock0[2];
        double af2 = clock0[3];

        if (sys == 'R') {
            char L1[256] = "", L2[256] = "", L3[256] = "";
            if (!fgets(L1, sizeof(L1), f)) break;
            rnx_chomp(L1);
            if (!fgets(L2, sizeof(L2), f)) break;
            rnx_chomp(L2);
            if (!fgets(L3, sizeof(L3), f)) break;
            rnx_chomp(L3);
            if (parse_glonass_record(prn, year, month, day, hour, min, sec,
                                     af0, af1, af2,   /* RINEX names: tau, gamma, msg-frame-time */
                                     L1, L2, L3)) {
                counts[2]++;
            }
        } else {
            char L1[256]="", L2[256]="", L3[256]="", L4[256]="",
                 L5[256]="", L6[256]="", L7[256]="";
            if (!fgets(L1, sizeof(L1), f)) break;
            rnx_chomp(L1);
            if (!fgets(L2, sizeof(L2), f)) break;
            rnx_chomp(L2);
            if (!fgets(L3, sizeof(L3), f)) break;
            rnx_chomp(L3);
            if (!fgets(L4, sizeof(L4), f)) break;
            rnx_chomp(L4);
            if (!fgets(L5, sizeof(L5), f)) break;
            rnx_chomp(L5);
            if (!fgets(L6, sizeof(L6), f)) break;
            rnx_chomp(L6);
            if (!fgets(L7, sizeof(L7), f)) break;
            rnx_chomp(L7);

            int gnss_id;
            switch (sys) {
            case 'G': gnss_id = 1; break;
            case 'E': gnss_id = 3; break;
            case 'J': gnss_id = 4; break;
            case 'C': gnss_id = 5; break;
            default:  gnss_id = 0; break;
            }
            if (gnss_id > 0 &&
                parse_keplerian_record(gnss_id, prn,
                                       year, month, day, hour, min, sec,
                                       af0, af1, af2,
                                       L1, L2, L3, L4, L5, L6, L7)) {
                if (gnss_id < RINEX_NAV_MAX_GNSS) counts[gnss_id]++;
            }
        }
    }

    fclose(f);

    int total = 0;
    for (int i = 0; i < RINEX_NAV_MAX_GNSS; i++) {
        if (out_counts) out_counts[i] = counts[i];
        total += counts[i];
    }
    return total;
}
