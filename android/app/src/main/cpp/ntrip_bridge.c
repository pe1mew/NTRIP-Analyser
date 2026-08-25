/**
 * @file ntrip_bridge.c
 * @brief The Android app's view of the C core -- implementation.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * @copyright Apache License 2.0 with Commons Clause (see LICENSE for details)
 */

#include "ntrip_bridge.h"

#include "session/ntrip_session.h"
#include "core/kpi.h"
#include "core/vrs_check.h"
#include "core/station_report.h"
#include "core/sourcetable.h"
#include "core/rtcm3x_parser.h"  /* eph decoders, ARP, output sink */
#include "core/sky_collect.h"
#include "core/sky_render.h"
#include "core/sv_ephemeris.h"
#include "core/rinex_nav.h"
#include "net/ntrip_handler.h"   /* receive_mount_table */
#include "core/version.h"

#ifdef __ANDROID__
#include <android/log.h>
/* The C side prints to stderr, which on Android goes nowhere. KPI 8
 * depends entirely on this fetch, and when it fails the app can only
 * say "no sourcetable entry" -- true, and no help at all in finding
 * out why. These few lines are the difference between a diagnosis and
 * a guess. */
#define BLOG(...) __android_log_print(ANDROID_LOG_INFO, "ntrip_bridge", __VA_ARGS__)
#else
#define BLOG(...) ((void)0)
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

/** @brief How many sourcetable records to carry across the bridge. */
/*
 * A defensive ceiling, not an expected size: the table is counted first
 * and allocated to fit. 512 was neither -- caster.centipede.fr publishes
 * 1217 mountpoints and lists NEAR at 816, so the entry was never found,
 * KPI 8 could not judge, and the run sat PENDING for ever with no
 * verdict. A cap that silently drops the mountpoint you asked for is
 * worse than no cap at all.
 *
 * receive_mount_table() bounds the download at 4 MB; this bounds what
 * that can turn into in memory (~800 bytes an entry).
 */
#define BRIDGE_MAX_ENTRIES 4096

/**
 * @brief Parse a sourcetable into a right-sized array.
 *
 * @param raw    The table as received.
 * @param count  Out: entries parsed.
 * @return Allocated array the caller frees, or NULL.
 */
static SourcetableEntry *bridge_parse_table(const char *raw, int *count)
{
    *count = 0;
    if (!raw) return NULL;

    int n = sourcetable_parse(raw, NULL, 0);      /* count first */
    if (n <= 0) return NULL;
    if (n > BRIDGE_MAX_ENTRIES) n = BRIDGE_MAX_ENTRIES;

    SourcetableEntry *e = (SourcetableEntry *)calloc((size_t)n, sizeof(*e));
    if (!e) return NULL;

    *count = sourcetable_parse(raw, e, n);
    return e;
}

static void bridge_on_event(const NsEvent *ev, void *user);
static void bridge_eph_event(const NsEvent *ev, void *user);

/*
 * Last broadcast station position, per mountpoint, for the life of the
 * process.
 *
 * A satellite cannot be placed in the sky without the station to see it
 * from, so a run has nothing to plot until the first 1005/1006 arrives
 * -- thirty seconds on a station that sends one every thirty seconds.
 * The sky view spent that time showing the handful of satellites the
 * phone's own GNSS could place, then jumped to the full constellation:
 * a flicker that looked like the orbits were missing when only the
 * station's coordinates were.
 *
 * A base does not move between two runs a minute apart, and a network
 * service's virtual position moves by a few kilometres at most, which
 * at 20 000 km is under a hundredth of a degree.  So the previous
 * run's position is a sound thing to draw with until the real one
 * arrives and replaces it.
 *
 * It is deliberately kept out of everything except placement.  The
 * reported ARP and KPI 3 come from the session snapshot and the decoded
 * 1005/1006, never from here: whether a station broadcasts its position
 * is exactly what KPI 3 asks, and a remembered answer would forge it.
 */
#define ARP_MEMO_MAX 8

static struct {
    char   mountpoint[64];
    double x, y, z;
    bool   valid;
} g_arp_memo[ARP_MEMO_MAX];
static int g_arp_memo_next;

static void arp_memo_put(const char *mp, double x, double y, double z)
{
    if (!mp || !*mp) return;
    for (int i = 0; i < ARP_MEMO_MAX; i++) {
        if (g_arp_memo[i].valid && strcmp(g_arp_memo[i].mountpoint, mp) == 0) {
            g_arp_memo[i].x = x; g_arp_memo[i].y = y; g_arp_memo[i].z = z;
            return;
        }
    }
    int i = g_arp_memo_next++ % ARP_MEMO_MAX;
    snprintf(g_arp_memo[i].mountpoint, sizeof(g_arp_memo[i].mountpoint),
             "%s", mp);
    g_arp_memo[i].x = x; g_arp_memo[i].y = y; g_arp_memo[i].z = z;
    g_arp_memo[i].valid = true;
}

static bool arp_memo_get(const char *mp, double *x, double *y, double *z)
{
    if (!mp || !*mp) return false;
    for (int i = 0; i < ARP_MEMO_MAX; i++) {
        if (g_arp_memo[i].valid && strcmp(g_arp_memo[i].mountpoint, mp) == 0) {
            *x = g_arp_memo[i].x; *y = g_arp_memo[i].y; *z = g_arp_memo[i].z;
            return true;
        }
    }
    return false;
}

struct NtripBridge {
    NtripSession *sess;
    KpiRun    run;
    KpiReport rep;
    bool      started;   /**< the KPI clock is running */
    bool      watch;     /**< keep going past the first verdict */
    KpiWatch  w;

    /* Sky coverage.  Orbits come from whichever source has them: a
     * RINEX file the user supplied, the observation stream itself when
     * the station broadcasts ephemerides on it, or the side-stream
     * below when it does not. */
    NtripSession *eph;
    SkyRenderSector sky[SKY_RENDER_N_EL_BANDS][SKY_RENDER_MAX_AZ_BINS];
    RtcmStrBuf sink;           /**< swallows decoder chatter */
    int       eph_frames;      /**< frames seen on the eph stream */
    int       obs_eph;         /**< ephemerides decoded off the obs stream */
    bool      have_ecef;
    RtcmArpInfo arp;           /**< the last 1005/1006, decoded in full */
    bool      have_arp_info;
    double    ex, ey, ez;      /**< station ARP in ECEF metres */
    char      mountpoint[64];

    /* The GGA uplink, driven here rather than by the session's own
     * timer, because the position can move mid-run: the paid edition
     * reports where the phone is now, not where it was at ns_open().
     * ntrip_session.h is explicit that one mechanism or the other
     * drives the uplink, never both. */
    bool      gga_on;
    double    gga_lat, gga_lon;
    double    gga_last_s;      /**< when one was last accepted by the socket */

    /* The network-RTK assertions, judged beside the eight checks. The
     * engine owns the verdicts; this side owns the workflow, and it is
     * the only side that can: A1 and A2 are timed from the moment a GGA
     * was accepted by the socket, which only the sender knows. */
    bool      vrs_mode;
    bool      vrs_gated;       /**< the gate test has begun: GGA stopped */
    VrsRun    vrun;
    VrsReport vrep;

    /* Tier 2: stability over the run, judged by the shared engine.
     * Fed on the *stream's* clock, one sample per second of it -- the
     * CLI's own pacing -- never on wall time, so a replayed capture
     * would report exactly what the live run did. */
    SrState   sr;
    double    sr_fed_at;       /**< stream time of the last sample     */
};

/** @brief GGA cadence, matching what the session's own timer used. */
#define BRIDGE_GGA_INTERVAL_S 10.0

/**
 * @brief Decode a frame into the shared cache if it carries an ephemeris.
 *
 * Takes any frame, so both handlers can hand it everything they see.
 *
 * The decoders print as they decode.  On a phone that output has
 * nowhere useful to go, so it is swallowed -- the same sink the CLI uses
 * in --sky mode.  Cleared each frame so an hours-long run cannot grow it
 * without bound.
 *
 * The sink is thread-local and both sessions are pumped on the one
 * bridge thread, so installing and removing it per frame is what keeps
 * it from leaking into anything else that prints.
 *
 * @return 1 when the frame was an ephemeris, 0 when it was not.
 */
static int bridge_decode_eph(NtripBridge *b, const unsigned char *payload,
                             int payload_len, int msg_type)
{
    rtcm_strbuf_clear(&b->sink);
    rtcm_set_output_buffer(&b->sink);
    int decoded = rtcm_decode_eph(payload, payload_len, msg_type);
    rtcm_set_output_buffer(NULL);
    return decoded;
}

/**
 * @brief Observation-stream events.
 *
 * The snapshot carries everything the KPI screen needs, so this exists
 * for the sky plot: sector accumulation needs the frame itself, which
 * the snapshot does not retain, and so do the orbits.
 *
 * Many stations broadcast ephemerides on the observation stream beside
 * their MSM -- measured on caster.centipede.fr/NEAR, which sent 1020 x16,
 * 1042 x8 and 1046 x23 in fifteen seconds, and advertised by Kadaster's
 * APEL00NLD0 as 1019,1020,1042,1044,1045,1046.  Decoding them here is
 * what lets the free edition draw a sky at all, and saves the paid
 * edition a second connection it does not need.
 */
static void bridge_on_event(const NsEvent *ev, void *user)
{
    NtripBridge *b = (NtripBridge *)user;
    if (!b || !ev || ev->type != NS_EV_FRAME) return;

    const unsigned char *payload = ev->u.frame.data + 3;
    int payload_len = ev->u.frame.len - 6;      /* header and CRC removed */
    int t = ev->u.frame.msg_type;

    if (bridge_decode_eph(b, payload, payload_len, t)) {
        b->obs_eph++;
        return;
    }

    if (t == 1005 || t == 1006) {
        RtcmArpInfo a;
        if (rtcm_extract_arp_info(payload, payload_len, t, &a)) {
            b->ex = a.x; b->ey = a.y; b->ez = a.z;
            b->have_ecef = true;
            b->arp = a;
            b->have_arp_info = true;
            arp_memo_put(b->mountpoint, a.x, a.y, a.z);
        }
        return;
    }

    if (b->have_ecef)
        sky_collect_feed_msm(&b->sky[0][0], payload, payload_len, t,
                             b->ex, b->ey, b->ez);
}

/**
 * @brief Ephemeris-stream events: decode orbits into the shared cache.
 *
 * The cache is a module-level singleton in sv_ephemeris.c, which is why
 * nothing is passed along here -- decoding is the whole effect.
 */
static void bridge_eph_event(const NsEvent *ev, void *user)
{
    NtripBridge *b = (NtripBridge *)user;
    if (!b || !ev || ev->type != NS_EV_FRAME) return;

    b->eph_frames++;

    bridge_decode_eph(b, ev->u.frame.data + 3, ev->u.frame.len - 6,
                      ev->u.frame.msg_type);
}

static NtripBridge *bridge_alloc(void)
{
    NtripBridge *b = (NtripBridge *)calloc(1, sizeof(*b));
    if (!b) return NULL;
    /* The decoder sink needs a real buffer: a zeroed RtcmStrBuf has
     * nowhere to put the text, and the decoders fall back to stdout --
     * which on a phone means megabytes of ephemeris dumps in logcat. */
    rtcm_strbuf_init(&b->sink, 8192);
    return b;
}

NtripBridge *bridge_open(const char *caster, int port, const char *mountpoint,
                         const char *user, const char *password,
                         double lat, double lon, bool send_gga, bool watch,
                         bool vrs, bool tls)
{
    NtripBridge *b = bridge_alloc();
    if (!b) return NULL;
    b->watch    = watch;
    b->vrs_mode = vrs;

    NsOptions opt;
    ns_options_default(&opt);

    snprintf(opt.config.NTRIP_CASTER, sizeof(opt.config.NTRIP_CASTER),
             "%s", caster ? caster : "");
    snprintf(opt.config.MOUNTPOINT, sizeof(opt.config.MOUNTPOINT),
             "%s", mountpoint ? mountpoint : "");
    snprintf(opt.config.USERNAME, sizeof(opt.config.USERNAME),
             "%s", user ? user : "");
    snprintf(opt.config.PASSWORD, sizeof(opt.config.PASSWORD),
             "%s", password ? password : "");
    opt.config.NTRIP_PORT = port;
    opt.config.LATITUDE   = lat;
    opt.config.LONGITUDE  = lon;
    /* The sourcetable fetch below reads the same config, so the caster's
     * flag covers both connections without a second decision. */
    opt.config.TLS        = tls;

    opt.stats_interval_s = 0.0;      /* the app polls; no event needed */
    opt.send_gga         = false;    /* driven from bridge_pump; see below */
    /* A phone loses connectivity constantly -- walking indoors, cell
     * hand-over, screen-off radio policy.  Reconnecting is what a user
     * expects; the KPI sustain clock resets on the gap either way, so a
     * genuinely broken station still cannot pass. */
    opt.auto_reconnect   = true;

    /* A VRS check is a verdict on one connection, as the CLI's is: the
     * assertions time connection edges, and a silent reconnect would
     * hand A5 a stream the gate test is waiting to see drop.  And the
     * check is meaningless without the uplink, so the mode implies it. */
    if (vrs) {
        opt.auto_reconnect = false;
        send_gga = true;
    }

    b->gga_on     = send_gga;
    b->gga_lat    = lat;
    b->gga_lon    = lon;
    /* Far enough back that the first pump after the handshake sends one:
     * a VRS answers nothing until it knows where the rover claims to be,
     * so waiting a full interval would cost the run its first ten
     * seconds of stream. */
    b->gga_last_s = -1e9;

    snprintf(b->mountpoint, sizeof(b->mountpoint), "%s",
             mountpoint ? mountpoint : "");
    sky_collect_reset(&b->sky[0][0]);

    /* Draw from where this mountpoint was last seen to be, until it says
     * so itself.  Placement only -- have_arp_info stays false, so the
     * reported ARP and KPI 3 still wait for a real 1005/1006. */
    if (arp_memo_get(b->mountpoint, &b->ex, &b->ey, &b->ez))
        b->have_ecef = true;

    b->sess = ns_open(&opt, bridge_on_event, b);
    if (!b->sess) { free(b); return NULL; }

    /* What this mountpoint promises, so KPI 8 has something to compare
     * against.  One extra request at open; without it the comparison
     * reports "cannot judge" rather than a false pass. */
    char *table = receive_mount_table(&opt.config,
                                      NTRIP_USER_AGENT(NTRIP_ARTEFACT_LIB));
    if (!table) {
        BLOG("sourcetable fetch failed for %s:%d; KPI 8 cannot judge",
             opt.config.NTRIP_CASTER, opt.config.NTRIP_PORT);
    }
    if (table) {
        int n = 0;
        SourcetableEntry *e = bridge_parse_table(table, &n);
        BLOG("sourcetable: %zu bytes, %d entries parsed", strlen(table), n);
        bool found = false;
        if (e) {
            for (int i = 0; i < n; i++) {
                if (strcmp(e[i].mountpoint, opt.config.MOUNTPOINT) != 0) continue;
                SourcetableType t[NS_MAX_TYPES];
                int nt = sourcetable_parse_types(e[i].format_details,
                                                 t, NS_MAX_TYPES);
                if (nt > 0) ns_set_advertised(b->sess, t, nt);
                ns_set_advertised_gnss(b->sess,
                    sourcetable_navsys_mask(e[i].nav_systems));
                found = true;
                BLOG("%s found in the sourcetable: %d advertised types",
                     opt.config.MOUNTPOINT, nt);
                break;
            }
            free(e);
        }
        if (!found)
            BLOG("%s is not in the %d entries parsed; KPI 8 cannot judge",
                 opt.config.MOUNTPOINT, n);
        free(table);
    }

    return b;
}

NtripBridge *bridge_open_file(const char *path, bool watch)
{
    NtripBridge *b = bridge_alloc();
    if (!b) return NULL;
    b->watch = watch;

    NsOptions opt;
    ns_options_default(&opt);
    opt.stats_interval_s = 0.0;

    b->sess = ns_open_file(path, &opt, bridge_on_event, b);
    if (!b->sess) { free(b); return NULL; }
    return b;
}

int bridge_pump(NtripBridge *b, int timeout_ms, double now_s)
{
    if (!b || !b->sess) return -1;

    if (!b->started) {
        kpi_run_start(&b->run, now_s, NULL);
        kpi_watch_start(&b->w, now_s);
        if (b->vrs_mode) vrs_run_start(&b->vrun, now_s, NULL);
        sr_reset(&b->sr, false, NULL);
        b->sr_fed_at = -1e9;
        b->started = true;
    }

    int r = ns_pump(b->sess, timeout_ms);

    /* The clock only advances on a sentence the socket accepted, so a
     * run that spends its first minute reconnecting still uplinks the
     * moment it is back rather than on the next tick of a free-running
     * timer. */
    if (b->gga_on && !b->vrs_gated &&
        now_s - b->gga_last_s >= BRIDGE_GGA_INTERVAL_S) {
        if (ns_send_gga(b->sess, b->gga_lat, b->gga_lon)) {
            b->gga_last_s = now_s;
            /* Told at the moment of acceptance, which is the fact A1
             * and A2 are timed from. */
            if (b->vrs_mode)
                vrs_note_gga(&b->vrun, ns_stats(b->sess), now_s,
                             b->gga_lat, b->gga_lon);
        }
    }

    /* The ephemeris stream is pumped on the same thread, briefly: it
     * carries a few frames a minute, so it needs no time of its own. */
    if (b->eph) ns_pump(b->eph, 0);

    kpi_update(&b->run, ns_stats(b->sess), now_s, &b->rep);
    kpi_watch_update(&b->w, &b->rep, now_s);

    /* One tier-2 sample per second of stream time, like the CLI. A
     * stream that carries no epochs never advances this clock, and
     * such a run honestly ends INSUFFICIENT. */
    {
        const NsStatsSnapshot *snap = ns_stats(b->sess);
        if (snap && snap->stream_time_s >= 0.0 &&
            snap->stream_time_s - b->sr_fed_at >= 1.0) {
            b->sr_fed_at = snap->stream_time_s;
            sr_feed(&b->sr, snap, snap->stream_time_s);
        }
    }

    if (b->vrs_mode) {
        /* Every pump, even when nothing changed: the engine sees
         * connection edges only through updates (test_vrs.c). */
        vrs_update(&b->vrun, ns_stats(b->sess), now_s, &b->vrep);

        /* The CLI's own gate condition: the KPIs have held their
         * window and the keep-alive assertion has passed, so stopping
         * the GGA now tests the caster rather than the station. */
        if (!b->vrs_gated &&
            b->rep.sustained_s >= KPI_SUSTAIN_S &&
            b->vrep.a[3].verdict == KPI_PASS)
            bridge_vrs_gate(b, now_s);
    }
    return r;
}

void bridge_vrs_gate(NtripBridge *b, double now_s)
{
    if (!b || !b->vrs_mode || b->vrs_gated) return;
    b->vrs_gated = true;
    vrs_begin_gate_test(&b->vrun, now_s);
}

void bridge_set_position(NtripBridge *b, double lat, double lon)
{
    if (!b) return;
    b->gga_lat = lat;
    b->gga_lon = lon;
}

int bridge_overall(const NtripBridge *b)
{
    /* Negative when the verdict has settled, so one call answers both
     * "what is it" and "is it final" without a second crossing. */
    if (!b) return KPI_RUN_RUNNING;
    return b->rep.settled ? -b->rep.overall - 1 : b->rep.overall;
}

/** @brief Append to a bounded buffer, tracking overflow in @p pos. */
static void app(char *out, size_t cap, int *pos, const char *fmt, ...)
{
    if (*pos < 0 || (size_t)*pos >= cap) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out + *pos, cap - (size_t)*pos, fmt, ap);
    va_end(ap);
    if (n < 0) { *pos = -1; return; }
    *pos += n;
}

/** @brief JSON string escaping for the label/detail literals. */
static void app_escaped(char *out, size_t cap, int *pos, const char *s)
{
    if (!s) s = "";
    for (; *s; s++) {
        switch (*s) {
        case '"':  app(out, cap, pos, "\\\""); break;
        case '\\': app(out, cap, pos, "\\\\"); break;
        case '\n': app(out, cap, pos, "\\n");  break;
        default:   app(out, cap, pos, "%c", *s); break;
        }
    }
}

int bridge_snapshot_json(NtripBridge *b, char *out, size_t cap)
{
    if (!b || !b->sess || !out || cap < 64) return -1;

    int pos = 0;
    app(out, cap, &pos, "{\"stats\":");

    /* The snapshot serialiser the daemon already publishes with, so the
     * phone and Munin read identical numbers from identical text. */
    if (pos >= 0 && (size_t)pos < cap) {
        int n = ns_stats_to_json(ns_stats(b->sess), out + pos, cap - (size_t)pos);
        if (n < 0) return -1;
        pos += n;
    }

    app(out, cap, &pos,
        ",\"kpi\":{\"overall\":%d,\"overall_name\":\"%s\","
        "\"elapsed_s\":%.1f,\"sustained_s\":%.1f,\"sustain_target_s\":%.1f,"
        "\"settled\":%s,\"items\":[",
        b->rep.overall, kpi_run_verdict_name(b->rep.overall),
        b->rep.elapsed_s, b->rep.sustained_s, (double)KPI_SUSTAIN_S,
        b->rep.settled ? "true" : "false");

    for (int i = 0; i < KPI_COUNT; i++) {
        const KpiResult *k = &b->rep.kpi[i];
        /* The precision the desktop prints, for the same reason: at
         * three decimals a CRC rate reads 0.004 against a threshold
         * documented as 0.001, and a satellite count reads 40.000. */
        app(out, cap, &pos, "%s{\"verdict\":%d,\"verdict_name\":\"%s\","
                            "\"value\":%.*f,\"label\":\"",
            i ? "," : "", k->verdict, kpi_verdict_name(k->verdict),
            kpi_value_decimals(i), k->value);
        app_escaped(out, cap, &pos, k->label);
        app(out, cap, &pos, "\",\"detail\":\"");
        app_escaped(out, cap, &pos, k->detail);
        app(out, cap, &pos, "\"}");
    }

    app(out, cap, &pos, "]}");

    /* The network-RTK assertions, in the same shape as the eight above
     * them, plus the classification -- which is a result, not a
     * verdict: a fixed base that never drops is NOT_GATED and correct.
     * Absent entirely outside a VRS run, so a normal run's document is
     * byte-identical to before the field existed. */
    if (b->vrs_mode) {
        app(out, cap, &pos,
            ",\"vrs\":{\"gate\":%d,\"gate_name\":\"%s\","
            "\"gate_started\":%s,\"failed\":%s,\"complete\":%s,"
            "\"items\":[",
            b->vrep.gate, vrs_gate_name(b->vrep.gate),
            b->vrs_gated ? "true" : "false",
            b->vrep.failed ? "true" : "false",
            b->vrep.complete ? "true" : "false");
        for (int i = 0; i < VRS_ASSERT_COUNT; i++) {
            const VrsResult *a = &b->vrep.a[i];
            app(out, cap, &pos,
                "%s{\"verdict\":%d,\"verdict_name\":\"%s\","
                "\"value\":%.1f,\"label\":\"",
                i ? "," : "", a->verdict, kpi_verdict_name(a->verdict),
                a->value);
            app_escaped(out, cap, &pos, a->label);
            app(out, cap, &pos, "\",\"detail\":\"");
            app_escaped(out, cap, &pos, a->detail);
            app(out, cap, &pos, "\"}");
        }
        app(out, cap, &pos, "]}");
    }

    /* Tier 2, as the daemon publishes it: sr_to_json verbatim, flat
     * keys frozen for Munin, so the app's export carries the exact
     * dialect a server-side sample would. Labels are deliberately
     * absent from it -- the frozen *keys* cross to Kotlin, which maps
     * them to its own strings, the Failure.kt precedent; details and
     * the headline stay the engine's words. */
    {
        StationReport rep;
        sr_build(&b->sr, &rep);
        SrJsonCtx ctx;
        memset(&ctx, 0, sizeof(ctx));
        ctx.mountpoint = b->mountpoint;
        app(out, cap, &pos, ",\"sr\":");
        if (pos >= 0 && (size_t)pos < cap) {
            int n = sr_to_json(&rep, &ctx, out + pos, cap - (size_t)pos);
            if (n < 0) return -1;
            pos += n;
        }
    }

    /* What the station says about itself.  More than the position: an
     * installer checking a base wants the station ID it will appear
     * under, the realisation its coordinates belong to, and which
     * systems it claims to serve -- which is not always what it
     * streams. */
    if (b->have_arp_info) {
        const RtcmArpInfo *a = &b->arp;
        app(out, cap, &pos,
            ",\"arp\":{\"msg\":%d,\"station_id\":%d,\"itrf_year\":%d,"
            "\"gps\":%s,\"glonass\":%s,\"galileo\":%s,"
            "\"reference\":%s,\"single_osc\":%s,"
            "\"x\":%.4f,\"y\":%.4f,\"z\":%.4f",
            a->msg_type, a->station_id, a->itrf_year,
            a->gps ? "true" : "false",
            a->glonass ? "true" : "false",
            a->galileo ? "true" : "false",
            a->is_reference ? "true" : "false",
            a->single_osc ? "true" : "false",
            a->x, a->y, a->z);
        if (a->has_height)
            app(out, cap, &pos, ",\"antenna_height\":%.4f}", a->antenna_height);
        else
            app(out, cap, &pos, ",\"antenna_height\":null}");
    }

    /* How well the orbit cache can serve the satellites being tracked,
     * and how old it is.  Incompleteness and age are shown in the app
     * rather than left implicit: a sky view drawn from stale or partial
     * orbits looks exactly like one drawn from fresh, complete ones.
     *
     * `from_obs` counts the ephemerides this station broadcast on its own
     * observation stream.  Above zero, the sky view owes nothing to a
     * second connection -- which is the difference between a free edition
     * that can draw a sky and one that cannot. */
    {
        int tracked = 0;
        int placeable = bridge_placeable(b, &tracked);
        double age = bridge_eph_age_s(b);

        app(out, cap, &pos,
            ",\"eph\":{\"tracked\":%d,\"placeable\":%d,\"cached\":%d,"
            "\"from_obs\":%d,",
            tracked, placeable, bridge_eph_count(b), b->obs_eph);
        if (age >= 0.0) app(out, cap, &pos, "\"age_s\":%.0f}", age);
        else            app(out, cap, &pos, "\"age_s\":null}");
    }

    /* The satellites, for the sky view and the C/N0 bars.  Positions are
     * absent by design: the caller joins them from its own source. */
    {
        SvTrackEntry sats[128];
        int n = ns_sat_list(b->sess, sats, 128);
        app(out, cap, &pos, ",\"sats\":[");
        for (int i = 0; i < n; i++) {
            app(out, cap, &pos,
                "%s{\"gnss\":%d,\"prn\":%d,\"cn0\":%.1f,"
                "\"cn0_mean\":%.1f,\"samples\":%u",
                i ? "," : "", sats[i].gnss_id, sats[i].prn,
                sats[i].cnr_dbhz, sats[i].cnr_mean,
                (unsigned)sats[i].samples);

            /* Where the satellite is, from the station's point of view.
             * Absent rather than zero when no orbit is cached: a
             * satellite at 0,0 would be drawn on the horizon due north,
             * which is a lie the plot cannot distinguish from a fact. */
            double az, el;
            if (b->have_ecef &&
                sky_azel_for_sat(sats[i].gnss_id, sats[i].prn,
                                 b->ex, b->ey, b->ez, &az, &el)) {
                app(out, cap, &pos, ",\"az\":%.1f,\"el\":%.1f}", az, el);
            } else {
                app(out, cap, &pos, ",\"az\":null,\"el\":null}");
            }
        }
        app(out, cap, &pos, "]");
    }

    if (b->watch) {
        double avail = kpi_watch_availability(&b->w);
        app(out, cap, &pos,
            ",\"watch\":{\"elapsed_s\":%.1f,\"ok_s\":%.1f,"
            "\"caution_s\":%.1f,\"failed_s\":%.1f,\"streak_s\":%.1f,"
            "\"best_streak_s\":%.1f,\"degradations\":%d,"
            "\"worst\":%d,\"worst_name\":\"%s\",",
            kpi_watch_elapsed(&b->w, b->w.last_t), b->w.ok_s,
            b->w.caution_s, b->w.failed_s, b->w.streak_s,
            b->w.best_streak_s, b->w.degradations,
            b->w.worst, kpi_run_verdict_name(b->w.worst));
        /* Unmeasured figures are null, matching ns_stats_to_json's rule
         * that "not measured" must not read as zero. */
        if (avail >= 0.0) app(out, cap, &pos, "\"availability\":%.4f,", avail);
        else              app(out, cap, &pos, "\"availability\":null,");
        if (b->w.last_degrade_t >= 0.0)
            app(out, cap, &pos, "\"last_degrade_s\":%.1f}", b->w.last_degrade_t);
        else
            app(out, cap, &pos, "\"last_degrade_s\":null}");
    }

    app(out, cap, &pos, "}");

    if (pos < 0 || (size_t)pos >= cap) return -1;   /* truncated */
    return pos;
}

int bridge_stats_csv(NtripBridge *b, char *out, size_t cap)
{
    if (!b || !b->sess || !out || cap < 2) return -1;

    int hdr = ns_stats_csv_header(out, cap);
    if (hdr < 0 || (size_t)hdr >= cap - 1) return -1;
    out[hdr] = '\n';

    int row = ns_stats_to_csv_row(ns_stats(b->sess),
                                  out + hdr + 1, cap - (size_t)hdr - 1);
    if (row < 0 || (size_t)row >= cap - (size_t)hdr - 1) return -1;
    return hdr + 1 + row;
}

void bridge_close(NtripBridge *b)
{
    if (!b) return;
    if (b->eph)  ns_close(b->eph);
    if (b->sess) ns_close(b->sess);
    rtcm_strbuf_free(&b->sink);
    free(b);
}

int bridge_sourcetable_json(const char *caster, int port,
                            const char *user, const char *password,
                            bool tls, char *out, size_t cap)
{
    if (!out || cap < 32) return -1;

    NTRIP_Config cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.NTRIP_CASTER, sizeof(cfg.NTRIP_CASTER), "%s", caster ? caster : "");
    snprintf(cfg.USERNAME,     sizeof(cfg.USERNAME),     "%s", user ? user : "");
    snprintf(cfg.PASSWORD,     sizeof(cfg.PASSWORD),     "%s", password ? password : "");
    cfg.NTRIP_PORT = port;
    cfg.TLS        = tls;

    char *raw = receive_mount_table(&cfg, NTRIP_USER_AGENT(NTRIP_ARTEFACT_LIB));
    if (!raw) return -1;

    int n = 0;
    SourcetableEntry *e = bridge_parse_table(raw, &n);
    free(raw);
    if (!e) return -1;

    int pos = 0;
    app(out, cap, &pos, "{\"entries\":[");
    for (int i = 0; i < n; i++) {
        app(out, cap, &pos, "%s{\"mountpoint\":\"", i ? "," : "");
        app_escaped(out, cap, &pos, e[i].mountpoint);
        app(out, cap, &pos, "\",\"identifier\":\"");
        app_escaped(out, cap, &pos, e[i].identifier);
        app(out, cap, &pos, "\",\"format\":\"");
        app_escaped(out, cap, &pos, e[i].format);
        app(out, cap, &pos, "\",\"nav_systems\":\"");
        app_escaped(out, cap, &pos, e[i].nav_systems);
        app(out, cap, &pos, "\",\"country\":\"");
        app_escaped(out, cap, &pos, e[i].country);
        app(out, cap, &pos,
            "\",\"lat\":%.5f,\"lon\":%.5f,\"carrier\":%d,\"nmea\":%s}",
            e[i].latitude, e[i].longitude, e[i].carrier,
            e[i].nmea ? "true" : "false");
    }
    app(out, cap, &pos, "]}");

    free(e);
    if (pos < 0 || (size_t)pos >= cap) return -1;   /* truncated */
    return pos;
}

/* ── Sky coverage ─────────────────────────────────────────────────────
 * Fed from the observation session's frames via the event callback,
 * because sector accumulation needs the frame itself, not the snapshot.
 */

bool bridge_open_eph(NtripBridge *b, const char *caster, int port,
                     const char *mountpoint,
                     const char *user, const char *password, bool tls)
{
    if (!b || b->eph) return false;

    NsOptions opt;
    ns_options_default(&opt);
    snprintf(opt.config.NTRIP_CASTER, sizeof(opt.config.NTRIP_CASTER),
             "%s", caster ? caster : "");
    snprintf(opt.config.MOUNTPOINT, sizeof(opt.config.MOUNTPOINT),
             "%s", mountpoint ? mountpoint : "");
    snprintf(opt.config.USERNAME, sizeof(opt.config.USERNAME),
             "%s", user ? user : "");
    snprintf(opt.config.PASSWORD, sizeof(opt.config.PASSWORD),
             "%s", password ? password : "");
    opt.config.NTRIP_PORT  = port;
    opt.config.TLS         = tls;   /* its own caster, its own flag */
    opt.stats_interval_s   = 0.0;
    opt.auto_reconnect     = true;

    b->eph = ns_open(&opt, bridge_eph_event, b);
    return b->eph != NULL;
}

int bridge_eph_frames(const NtripBridge *b)
{
    return b ? b->eph_frames : -1;
}

int bridge_eph_count(const NtripBridge *b)
{
    (void)b;                       /* the cache is process-wide */
    /* Not conditional on a side-stream being open.  It used to be, and
     * that answered a different question than the one it was asked:
     * orbits also arrive from a RINEX file, from the observation stream,
     * and from a side-stream already closed because the cache was full.
     * Each of those reported "0 ephemerides" while the sky view was
     * drawing a complete constellation. */
    return sv_eph_count();
}

bool bridge_sky_rgb(NtripBridge *b, unsigned char *rgb,
                    int width, int height)
{
    if (!b || !rgb) return false;
    if (!b->have_ecef) return false;      /* no station: nothing to centre on */

    const NsStatsSnapshot *s = ns_stats(b->sess);
    return sky_render_heatmap_rgb(rgb, width, height, &b->sky[0][0],
                                  s->arp_valid, s->arp_lat, s->arp_lon,
                                  s->arp_alt, b->mountpoint, "");
}

int bridge_load_rinex(NtripBridge *b, const char *path)
{
    (void)b;                       /* the ephemeris cache is process-wide */
    return bridge_check_rinex(path);
}

int bridge_check_rinex(const char *path)
{
    if (!path || !*path) return -1;
    return rinex_nav_load(path, NULL);
}

long long bridge_rinex_newest_utc(void)
{
    return rinex_nav_newest_utc();
}

int bridge_placeable(const NtripBridge *b, int *tracked)
{
    if (tracked) *tracked = 0;
    if (!b || !b->sess) return 0;

    SvTrackEntry sats[128];
    int n = ns_sat_list(b->sess, sats, 128);
    if (tracked) *tracked = n;

    /* Usable, not merely present.  Counting the existence of an
     * ephemeris said "41 of 41 tracked satellites have an orbit" over a
     * sky view placing none of them: every record in the imported file
     * was outside the window sky_azel_for_sat() applies, so the plot
     * fell back to the phone while the card reported full coverage.
     *
     * The same number decides whether pro dials its ephemeris stream and
     * when it hangs up, so a stale file used to make the cache look
     * finished. */
    int gps_week;
    double gps_tow;
    sky_get_gps_time_now(&gps_week, &gps_tow);
    double glo_tod = sky_get_glo_tod_now();

    int have = 0;
    for (int i = 0; i < n; i++) {
        const SvEphemeris *e = sv_eph_get(sats[i].gnss_id, sats[i].prn);
        if (!e) continue;
        double t = (sats[i].gnss_id == 2) ? glo_tod : gps_tow;
        if (sv_eph_is_valid_at(e, gps_week, t)) have++;
    }
    return have;
}

void bridge_close_eph(NtripBridge *b)
{
    if (!b || !b->eph) return;
    ns_close(b->eph);
    b->eph = NULL;              /* the orbits it delivered stay cached */
}

int bridge_obs_eph(const NtripBridge *b)
{
    return b ? b->obs_eph : 0;
}

double bridge_eph_age_s(const NtripBridge *b)
{
    (void)b;

    /* Age is measured from the freshest ephemeris in the cache: that is
     * what says how long ago the app last learned anything about the
     * orbits.  The oldest entry would answer a different question --
     * how stale the worst satellite is -- and would read alarmingly for
     * a cache that is mostly fresh.
     *
     * Freshest is found by comparing ages, not by comparing week and
     * toe.  Those are not on one scale: BeiDou counts weeks from 2006,
     * GLONASS carries no week at all and puts Moscow seconds-of-day in
     * toe.  Sorting on the raw pair picked whichever system numbered
     * highest rather than whichever arrived last.
     *
     * GPS time began 1980-01-06; the 18 s offset from UTC has held
     * since 2017.  Week numbers are ignored on both sides: broadcast
     * ephemerides live hours, far inside the half-week wrap, and GPS's
     * 10-bit week would need un-rolling to be trusted anyway. */
    time_t now = time(NULL);
    double gps_now  = (double)(now - 315964800) + 18.0;
    double tow_now  = fmod(gps_now, 604800.0);
    /* GLONASS reference epochs are Moscow seconds of day: UTC + 3 h. */
    double glo_now  = fmod((double)(now % 86400) + 10800.0, 86400.0);

    int week_now;
    double tow_check;
    sky_get_gps_time_now(&week_now, &tow_check);

    double best = -1.0;
    for (int g = 0; g < SV_EPH_MAX_GNSS; g++) {
        for (int p = 1; p <= SV_EPH_MAX_SATS_PER_GNSS; p++) {
            const SvEphemeris *e = sv_eph_get(g, p);
            if (!e) continue;

            /* Only orbits that could place their satellite.  Age is
             * measured against a wrap -- one week, or one *day* for
             * GLONASS, which carries no week at all -- so a day-old
             * GLONASS record lands a few hours behind now and reported
             * itself fresh.  A cache holding nothing usable then read
             * "newest orbit 0 min old" beside a plot drawn entirely
             * from the phone.  Skipping the unusable makes the wrap
             * unreachable: nothing valid is ever a day old. */
            double t_valid = (e->gnss_id == 2) ? glo_now : tow_check;
            if (!sv_eph_is_valid_at(e, week_now, t_valid)) continue;

            bool glo   = (e->gnss_id == 2);
            double wrap = glo ? 86400.0 : 604800.0;
            double age  = (glo ? glo_now : tow_now) - e->toe;
            if (age < -wrap / 2.0) age += wrap;
            if (age >  wrap / 2.0) age -= wrap;

            /* A reference epoch ahead of now is normal -- NavIC and
             * Galileo routinely broadcast one hours in advance -- and
             * means the data is fresh, not missing.  Reported as a
             * negative age it read as "no orbits at all" and hid a
             * fully populated cache behind "the sky view cannot place
             * anything". */
            if (age < 0.0) age = 0.0;

            if (best < 0.0 || age < best) best = age;
        }
    }
    return best;                 /* -1 only when the cache is empty */
}
