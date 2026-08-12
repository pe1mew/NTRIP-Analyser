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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct NtripBridge {
    NtripSession *sess;
    KpiRun    run;
    KpiReport rep;
    bool      started;   /**< the KPI clock is running */
};

/** @brief Events are not surfaced in Phase 1; the snapshot carries all. */
static void bridge_on_event(const NsEvent *ev, void *user)
{
    (void)ev; (void)user;
}

static NtripBridge *bridge_alloc(void)
{
    NtripBridge *b = (NtripBridge *)calloc(1, sizeof(*b));
    return b;
}

NtripBridge *bridge_open(const char *caster, int port, const char *mountpoint,
                         const char *user, const char *password,
                         double lat, double lon, bool send_gga)
{
    NtripBridge *b = bridge_alloc();
    if (!b) return NULL;

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

    b->sess = ns_open(&opt, bridge_on_event, b);
    if (!b->sess) { free(b); return NULL; }
    return b;
}

NtripBridge *bridge_open_file(const char *path)
{
    NtripBridge *b = bridge_alloc();
    if (!b) return NULL;

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
        b->started = true;
    }

    int r = ns_pump(b->sess, timeout_ms);
    kpi_update(&b->run, ns_stats(b->sess), now_s, &b->rep);
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

    app(out, cap, &pos, "]}}");

    if (pos < 0 || (size_t)pos >= cap) return -1;   /* truncated */
    return pos;
}

void bridge_close(NtripBridge *b)
{
    if (!b) return;
    if (b->sess) ns_close(b->sess);
    free(b);
}
