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
#include "core/sourcetable.h"
#include "core/rtcm3x_parser.h"  /* eph decoders, ARP, output sink */
#include "core/sky_collect.h"
#include "core/sky_render.h"
#include "core/sv_ephemeris.h"
#include "net/ntrip_handler.h"   /* receive_mount_table */
#include "core/version.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void bridge_on_event(const NsEvent *ev, void *user);
static void bridge_eph_event(const NsEvent *ev, void *user);

struct NtripBridge {
    NtripSession *sess;
    KpiRun    run;
    KpiReport rep;
    bool      started;   /**< the KPI clock is running */
    bool      watch;     /**< keep going past the first verdict */
    KpiWatch  w;

    /* Sky coverage.  The observation stream cannot place a satellite in
     * the sky by itself, so this stays empty until an ephemeris
     * side-stream is attached and has delivered orbits. */
    NtripSession *eph;
    SkyRenderSector sky[SKY_RENDER_N_EL_BANDS][SKY_RENDER_MAX_AZ_BINS];
    RtcmStrBuf sink;           /**< swallows decoder chatter */
    int       eph_frames;      /**< frames seen on the eph stream */
    bool      have_ecef;
    double    ex, ey, ez;      /**< station ARP in ECEF metres */
    char      mountpoint[64];
};

/**
 * @brief Observation-stream events.
 *
 * The snapshot carries everything the KPI screen needs, so this exists
 * only for the sky plot: sector accumulation needs the frame itself,
 * which the snapshot does not retain.
 */
static void bridge_on_event(const NsEvent *ev, void *user)
{
    NtripBridge *b = (NtripBridge *)user;
    if (!b || !ev || ev->type != NS_EV_FRAME) return;

    const unsigned char *payload = ev->u.frame.data + 3;
    int payload_len = ev->u.frame.len - 6;      /* header and CRC removed */
    int t = ev->u.frame.msg_type;

    if (t == 1005 || t == 1006) {
        double la, lo, al, x, y, z;
        if (rtcm_extract_arp_ecef(payload, payload_len, t,
                                  &la, &lo, &al, &x, &y, &z)) {
            b->ex = x; b->ey = y; b->ez = z;
            b->have_ecef = true;
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

    const unsigned char *p = ev->u.frame.data + 3;
    int len = ev->u.frame.len - 6;

    /* The decoders print as they decode.  On a phone that output has
     * nowhere useful to go, so it is swallowed -- the same sink the CLI
     * uses in --sky mode.  Cleared each frame so an hours-long run
     * cannot grow it without bound. */
    rtcm_strbuf_clear(&b->sink);
    rtcm_set_output_buffer(&b->sink);

    switch (ev->u.frame.msg_type) {
    case 1019: decode_rtcm_1019(p, len); break;
    case 1020: decode_rtcm_1020(p, len); break;
    case 1041: decode_rtcm_1041(p, len); break;
    case 1042: decode_rtcm_1042(p, len); break;
    case 1044: decode_rtcm_1044(p, len); break;
    case 1045: decode_rtcm_1045(p, len); break;
    case 1046: decode_rtcm_1046(p, len); break;
    default: break;
    }

    rtcm_set_output_buffer(NULL);
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
                         double lat, double lon, bool send_gga, bool watch)
{
    NtripBridge *b = bridge_alloc();
    if (!b) return NULL;
    b->watch = watch;

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

    opt.stats_interval_s = 0.0;      /* the app polls; no event needed */
    opt.send_gga         = send_gga;
    opt.gga_interval_s   = 10.0;
    /* A phone loses connectivity constantly -- walking indoors, cell
     * hand-over, screen-off radio policy.  Reconnecting is what a user
     * expects; the KPI sustain clock resets on the gap either way, so a
     * genuinely broken station still cannot pass. */
    opt.auto_reconnect   = true;

    snprintf(b->mountpoint, sizeof(b->mountpoint), "%s",
             mountpoint ? mountpoint : "");
    sky_collect_reset(&b->sky[0][0]);

    b->sess = ns_open(&opt, bridge_on_event, b);
    if (!b->sess) { free(b); return NULL; }
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
        kpi_run_start(&b->run, now_s);
        kpi_watch_start(&b->w, now_s);
        b->started = true;
    }

    int r = ns_pump(b->sess, timeout_ms);

    /* The ephemeris stream is pumped on the same thread, briefly: it
     * carries a few frames a minute, so it needs no time of its own. */
    if (b->eph) ns_pump(b->eph, 0);

    kpi_update(&b->run, ns_stats(b->sess), now_s, &b->rep);
    kpi_watch_update(&b->w, &b->rep, now_s);
    return r;
}

int bridge_overall(const NtripBridge *b)
{
    return b ? b->rep.overall : KPI_RUN_RUNNING;
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
        "\"items\":[",
        b->rep.overall, kpi_run_verdict_name(b->rep.overall),
        b->rep.elapsed_s, b->rep.sustained_s, (double)KPI_SUSTAIN_S);

    for (int i = 0; i < KPI_COUNT; i++) {
        const KpiResult *k = &b->rep.kpi[i];
        app(out, cap, &pos, "%s{\"verdict\":%d,\"verdict_name\":\"%s\","
                            "\"value\":%.3f,\"label\":\"",
            i ? "," : "", k->verdict, kpi_verdict_name(k->verdict), k->value);
        app_escaped(out, cap, &pos, k->label);
        app(out, cap, &pos, "\",\"detail\":\"");
        app_escaped(out, cap, &pos, k->detail);
        app(out, cap, &pos, "\"}");
    }

    app(out, cap, &pos, "]}");

    /* The satellites, for the sky view and the C/N0 bars.  Positions are
     * absent by design: the caller joins them from its own source. */
    {
        SvTrackEntry sats[128];
        int n = ns_sat_list(b->sess, sats, 128);
        app(out, cap, &pos, ",\"sats\":[");
        for (int i = 0; i < n; i++) {
            app(out, cap, &pos,
                "%s{\"gnss\":%d,\"prn\":%d,\"cn0\":%.1f,"
                "\"cn0_mean\":%.1f,\"samples\":%u}",
                i ? "," : "", sats[i].gnss_id, sats[i].prn,
                sats[i].cnr_dbhz, sats[i].cnr_mean,
                (unsigned)sats[i].samples);
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

void bridge_close(NtripBridge *b)
{
    if (!b) return;
    if (b->eph)  ns_close(b->eph);
    if (b->sess) ns_close(b->sess);
    rtcm_strbuf_free(&b->sink);
    free(b);
}

/** @brief How many sourcetable records to carry across the bridge. */
#define BRIDGE_MAX_ENTRIES 512

int bridge_sourcetable_json(const char *caster, int port,
                            const char *user, const char *password,
                            char *out, size_t cap)
{
    if (!out || cap < 32) return -1;

    NTRIP_Config cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.NTRIP_CASTER, sizeof(cfg.NTRIP_CASTER), "%s", caster ? caster : "");
    snprintf(cfg.USERNAME,     sizeof(cfg.USERNAME),     "%s", user ? user : "");
    snprintf(cfg.PASSWORD,     sizeof(cfg.PASSWORD),     "%s", password ? password : "");
    cfg.NTRIP_PORT = port;

    char *raw = receive_mount_table(&cfg, NTRIP_USER_AGENT(NTRIP_ARTEFACT_LIB));
    if (!raw) return -1;

    SourcetableEntry *e = (SourcetableEntry *)
        calloc(BRIDGE_MAX_ENTRIES, sizeof(SourcetableEntry));
    if (!e) { free(raw); return -1; }

    int n = sourcetable_parse(raw, e, BRIDGE_MAX_ENTRIES);
    free(raw);

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
                     const char *user, const char *password)
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
    if (!b || !b->eph) return 0;
    int n = 0;
    for (int g = 0; g < SV_EPH_MAX_GNSS; g++)
        for (int p = 0; p < SV_EPH_MAX_SATS_PER_GNSS; p++)
            if (sv_eph_get(g, p)) n++;
    return n;
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
