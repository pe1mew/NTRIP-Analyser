/**
 * @file cli_stream.c
 * @brief CLI stream modes on the session layer -- implementation.
 *
 * Each mode is a small event handler over an NtripSession; the connect,
 * framing and CRC work all four used to duplicate now happens once, in
 * src/session/.  Output formats follow the legacy implementations so
 * scripts keep working, with two deliberate corrections noted in the
 * header.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "cli/cli_stream.h"
#include "core/kpi.h"
#include "core/sourcetable.h"
#include "core/vrs_check.h"
#include <time.h>

bool cli_auto_reconnect = false;   /* set by --reconnect in main.c */
#include "session/ntrip_session.h"
#include "core/version.h"
#include "core/rtcm3x_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The old implementations printed "GGA " into the type stream on every
 * uplink, once per second.  Kept: it is a useful heartbeat and scripts
 * may key on it. */
#define CLI_GGA_INTERVAL_S 1

/* ── Shared context ─────────────────────────────────────────────────── */

typedef enum {
    CLI_MODE_DECODE = 0,
    CLI_MODE_TYPES,
    CLI_MODE_SATS,
} CliMode;

typedef struct {
    const NTRIP_Config *config;
    CliMode    mode;
    bool       debug;

    /* decode mode */
    const int *filter;
    int        nfilter;

    /* sats mode */
    SatStatsSummary summary;
} CliCtx;

/** @brief Common event handling; per-mode work happens on NS_EV_FRAME. */
static void cli_on_event(const NsEvent *ev, void *user)
{
    CliCtx *c = (CliCtx *)user;

    switch (ev->type) {
    case NS_EV_HANDSHAKE:
        if (c->debug && ev->u.handshake->raw[0]) {
            printf("[NTRIP] Server response after login:\n%s\n",
                   ev->u.handshake->raw);
            fflush(stdout);
        }
        break;

    case NS_EV_LOG:
        if (ev->u.log.level != NS_LOG_INFO) {
            fprintf(stderr, "[%s] %s\n",
                    ev->u.log.level == NS_LOG_ERROR ? "ERROR" : "WARN",
                    ev->u.log.text);
        }
        break;

    case NS_EV_FRAME: {
        const unsigned char *frame = ev->u.frame.data;
        int frame_len   = ev->u.frame.len;
        int payload_len = frame_len - 6;
        int msg_type    = ev->u.frame.msg_type;

        switch (c->mode) {
        case CLI_MODE_DECODE:
            if (c->nfilter == 0) {
                analyze_rtcm_message(frame, frame_len, false, c->config);
            } else {
                int in_filter = 0;
                for (int i = 0; i < c->nfilter; i++) {
                    if (msg_type == c->filter[i]) {
                        analyze_rtcm_message(frame, frame_len, false,
                                             c->config);
                        in_filter = 1;
                        break;
                    }
                }
                if (!in_filter) {
                    printf("%d ", msg_type);
                    fflush(stdout);
                }
            }
            break;

        case CLI_MODE_TYPES:
            /* Statistics come from the session snapshot at the end;
             * live, just echo the type stream as before. */
            printf("%d ", msg_type);
            fflush(stdout);
            break;

        case CLI_MODE_SATS: {
            extract_satellites(frame + 3, payload_len, msg_type,
                               &c->summary);
            int total_unique = 0;
            for (int i = 0; i < c->summary.gnss_count; i++)
                total_unique += c->summary.gnss[i].count;
            printf("%d ", total_unique);
            fflush(stdout);
            break;
        }
        }
        break;
    }

    default:
        break;
    }
}

/**
 * @brief Open a session for a CLI mode and pump it.
 *
 * @param seconds 0 = run until the stream ends or the process is
 *                interrupted; otherwise stop after this many seconds.
 * @return The session, still open, so the caller can read its statistics;
 *         the caller must ns_close() it.  NULL when it could not be
 *         created.
 */
static NtripSession *cli_run(CliCtx *c, int seconds)
{
    NsOptions opt;
    ns_options_default(&opt);
    opt.config           = *c->config;
    opt.stats_interval_s = 0.0;
    opt.send_gga         = false;   /* driven below, with the "GGA " echo */
    opt.auto_reconnect   = cli_auto_reconnect;
    opt.user_agent       = NTRIP_USER_AGENT(NTRIP_ARTEFACT_CLI);

    NtripSession *sess = ns_open(&opt, cli_on_event, c);
    if (!sess) {
        fprintf(stderr, "[ERROR] Out of memory opening the stream session\n");
        return NULL;
    }

    time_t t_start   = time(NULL);
    time_t last_gga  = 0;   /* forces an immediate first GGA */

    for (;;) {
        time_t now = time(NULL);
        if (seconds > 0 && (now - t_start) >= seconds) break;

        if ((now - last_gga) >= CLI_GGA_INTERVAL_S) {
            if (ns_send_gga(sess, c->config->LATITUDE,
                            c->config->LONGITUDE)) {
                printf("GGA ");
                fflush(stdout);
            }
            last_gga = now;
        }

        if (ns_pump(sess, 200) < 0) break;
    }
    return sess;
}

/* ── Modes ──────────────────────────────────────────────────────────── */

void cli_stream_decode(const NTRIP_Config *config,
                       const int *filter_list, int filter_count,
                       bool debug)
{
    CliCtx c;
    memset(&c, 0, sizeof(c));
    c.config  = config;
    c.mode    = CLI_MODE_DECODE;
    c.filter  = filter_list;
    c.nfilter = filter_count;
    c.debug   = debug;

    NtripSession *sess = cli_run(&c, 0);
    ns_close(sess);
}

void cli_analyze_types(const NTRIP_Config *config, int seconds)
{
    printf("[INFO] Analyzing message types for %d seconds...\n", seconds);

    CliCtx c;
    memset(&c, 0, sizeof(c));
    c.config = config;
    c.mode   = CLI_MODE_TYPES;

    NtripSession *sess = cli_run(&c, seconds);
    if (!sess) return;

    /* The legacy table, fed from the session's statistics.  Two changes
     * of substance, both corrections: intervals are per epoch, so an MSM
     * type split across frames is not reported at a multiple of its true
     * rate; and the average is a real mean of the measured intervals --
     * the old computation could print an average below its own minimum. */
    const NsStatsSnapshot *st = ns_stats(sess);

    /* Print ascending by message type, as the old table did. */
    int order[NS_MAX_TYPES];
    for (int i = 0; i < st->n_types; i++) order[i] = i;
    for (int i = 0; i < st->n_types; i++)
        for (int j = i + 1; j < st->n_types; j++)
            if (st->types[order[j]].msg_type < st->types[order[i]].msg_type) {
                int t = order[i]; order[i] = order[j]; order[j] = t;
            }

    printf("\n[INFO] Message type analysis complete. Statistics:\n");
    printf("+-------------+-------+---------------+---------------+---------------+\n");
    printf("| MessageType | Count |  Min-DT (S)   |  Max-DT (S)   |  Avg-DT (S)   |\n");
    printf("+-------------+-------+---------------+---------------+---------------+\n");
    for (int i = 0; i < st->n_types; i++) {
        const NsTypeStats *t = &st->types[order[i]];
        if (t->frames == 0) continue;
        printf("| %-11d | %5llu | %13.3f | %13.3f | %13.3f |\n",
               t->msg_type, (unsigned long long)t->frames,
               t->min_dt, t->max_dt, t->avg_dt);
    }
    printf("+-------------+-------+---------------+---------------+---------------+\n");
    if (st->types_truncated)
        printf("(more than %d distinct types; the rest were not tracked)\n",
               NS_MAX_TYPES);

    ns_close(sess);
}

void cli_analyze_sats(const NTRIP_Config *config, int seconds)
{
    printf("Opening NTRIP stream and analyzing satellites for %d seconds...\n",
           seconds);

    CliCtx c;
    memset(&c, 0, sizeof(c));
    c.config = config;
    c.mode   = CLI_MODE_SATS;

    NtripSession *sess = cli_run(&c, seconds);
    if (!sess) return;
    ns_close(sess);

    /* The legacy table, verbatim. */
    SatStatsSummary *summary = &c.summary;
    int total_unique = 0;
    for (int i = 0; i < summary->gnss_count; i++)
        total_unique += summary->gnss[i].count;

    #define SAT_COL_WIDTH 60
    char border[256];
    int pos = 0;
    border[pos++] = '+';
    for (int i = 0; i < 11; ++i) border[pos++] = '-';
    border[pos++] = '+';
    for (int i = 0; i < 12; ++i) border[pos++] = '-';
    border[pos++] = '+';
    for (int i = 0; i < SAT_COL_WIDTH + 1; ++i) border[pos++] = '-';
    border[pos++] = '+';
    border[pos]   = '\0';

    printf("\nGNSS systems and satellites seen:\n");
    printf("%s\n", border);
    printf("|   GNSS    | #Sats Seen | Satellites%*s|\n", SAT_COL_WIDTH - 10, "");
    printf("%s\n", border);
    for (int i = 0; i < summary->gnss_count; ++i) {
        char sat_list[MAX_SATS_PER_GNSS * 4] = "";
        int lpos = 0;
        int first = 1;
        char idbuf[8];
        for (int s = 0; s < MAX_SATS_PER_GNSS; ++s) {
            if (summary->gnss[i].sat_seen[s]) {
                const char *rinex = rinex_id_from_gnss(
                    summary->gnss[i].gnss_id, s + 1, idbuf, sizeof(idbuf));
                lpos += snprintf(sat_list + lpos, sizeof(sat_list) - lpos,
                                 "%s%s", first ? "" : " ", rinex);
                first = 0;
            }
        }
        if (lpos == 0) snprintf(sat_list, sizeof(sat_list), "None");

        int len = (int)strlen(sat_list);
        int offset = 0;
        int first_line = 1;
        while (offset < len) {
            char line_buf[SAT_COL_WIDTH + 1];
            int copy_len = (len - offset > SAT_COL_WIDTH)
                           ? SAT_COL_WIDTH : (len - offset);
            strncpy(line_buf, sat_list + offset, copy_len);
            line_buf[copy_len] = '\0';

            if (first_line) {
                printf("| %-9s | %10d | %-*s|\n",
                       gnss_name_from_id(summary->gnss[i].gnss_id),
                       summary->gnss[i].count,
                       SAT_COL_WIDTH, line_buf);
                first_line = 0;
            } else {
                printf("| %-9s | %10s | %-*s|\n", "", "",
                       SAT_COL_WIDTH, line_buf);
            }
            offset += copy_len;
            while (sat_list[offset] == ' ') offset++;
        }
    }
    printf("%s\n", border);
    printf("| Total     | %10d | %-*s|\n", total_unique, SAT_COL_WIDTH, "");
    printf("%s\n", border);
    #undef SAT_COL_WIDTH
}

/* ── Ephemeris side-stream (used by --sky) ──────────────────────────── */

typedef struct {
    const volatile int *stop_flag;
    bool       verbose;
    int        eph_count;
    RtcmStrBuf sink;        /* swallows decoder text unless verbose */
    int        sink_used;
} EphCliCtx;

static void eph_cli_on_event(const NsEvent *ev, void *user)
{
    EphCliCtx *c = (EphCliCtx *)user;

    switch (ev->type) {
    case NS_EV_LOG:
        if (ev->u.log.level != NS_LOG_INFO)
            fprintf(stderr, "[EPH] %s\n", ev->u.log.text);
        break;

    case NS_EV_FRAME: {
        const unsigned char *frame = ev->u.frame.data;
        int payload_len = ev->u.frame.len - 6;
        int mt = ev->u.frame.msg_type;

        /* Reset the discard sink between frames so it cannot grow
         * unbounded over an hours-long run. */
        if (c->sink_used) rtcm_strbuf_clear(&c->sink);

        switch (mt) {
        case 1019: decode_rtcm_1019(frame + 3, payload_len); c->eph_count++; break;
        case 1020: decode_rtcm_1020(frame + 3, payload_len); c->eph_count++; break;
        case 1041: decode_rtcm_1041(frame + 3, payload_len); c->eph_count++; break;
        case 1042: decode_rtcm_1042(frame + 3, payload_len); c->eph_count++; break;
        case 1044: decode_rtcm_1044(frame + 3, payload_len); c->eph_count++; break;
        case 1045: decode_rtcm_1045(frame + 3, payload_len); c->eph_count++; break;
        case 1046: decode_rtcm_1046(frame + 3, payload_len); c->eph_count++; break;
        default: break;   /* incl. 1005/1006: must not touch the obs ARP */
        }

        if (c->verbose && mt >= 1019 && mt <= 1046) {
            fprintf(stderr, "[EPH] type=%d  (total cached: %d)\n",
                    mt, c->eph_count);
            fflush(stderr);
        }
        break;
    }

    default:
        break;
    }
}

int run_eph_stream(const NTRIP_Config *config,
                   const volatile int *stop_flag, bool verbose)
{
    if (!config || !config->EPH_CASTER[0] || !config->EPH_MOUNTPOINT[0])
        return 1;

    EphCliCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.stop_flag = stop_flag;
    ctx.verbose   = verbose;

    if (!verbose) {
        rtcm_strbuf_init(&ctx.sink, 8192);
        rtcm_set_output_buffer(&ctx.sink);
        ctx.sink_used = 1;
    }

    NsOptions opt;
    ns_options_default(&opt);
    opt.config = *config;
    strncpy(opt.config.NTRIP_CASTER, config->EPH_CASTER,
            sizeof(opt.config.NTRIP_CASTER) - 1);
    opt.config.NTRIP_PORT = config->EPH_PORT;
    strncpy(opt.config.MOUNTPOINT, config->EPH_MOUNTPOINT,
            sizeof(opt.config.MOUNTPOINT) - 1);
    strncpy(opt.config.USERNAME, config->EPH_USERNAME,
            sizeof(opt.config.USERNAME) - 1);
    strncpy(opt.config.PASSWORD, config->EPH_PASSWORD,
            sizeof(opt.config.PASSWORD) - 1);
    opt.stats_interval_s = 0.0;
    opt.send_gga         = false;
    opt.auto_reconnect   = cli_auto_reconnect;
    opt.user_agent       = NTRIP_USER_AGENT(NTRIP_ARTEFACT_CLI);

    NtripSession *sess = ns_open(&opt, eph_cli_on_event, &ctx);
    int rc = 0;
    if (!sess) {
        rc = 1;
    } else {
        fprintf(stderr, "[EPH] Connected to %s:%d /%s\n",
                config->EPH_CASTER, config->EPH_PORT,
                config->EPH_MOUNTPOINT);
        while (!stop_flag || !*stop_flag) {
            if (ns_pump(sess, 200) < 0) break;
        }
        ns_close(sess);
    }

    if (ctx.sink_used) {
        rtcm_set_output_buffer(NULL);
        rtcm_strbuf_free(&ctx.sink);
    }
    fprintf(stderr, "[EPH] Stream closed (decoded %d ephemerides)\n",
            ctx.eph_count);
    fflush(stderr);
    return rc;
}

/* ── The acceptance test ──────────────────────────────────────────────
 * One shared engine (core/kpi.c, core/vrs_check.c) so this run, the
 * Android app and the GUI can never disagree about a station.
 */

static void check_on_event(const NsEvent *ev, void *user)
{
    (void)ev; (void)user;
}

/** @brief Print the KPI table, and the VRS assertions when present. */
static void check_print(const KpiReport *kr, const VrsReport *vr)
{
    printf("\n%-3s %-26s %-5s %12s  %s\n",
           "#", "KPI", "verd", "value", "detail");
    for (int i = 0; i < KPI_COUNT; i++)
        printf("%-3d %-26s %-5s %12.2f  %s\n", i + 1,
               kr->kpi[i].label, kpi_verdict_name(kr->kpi[i].verdict),
               kr->kpi[i].value, kr->kpi[i].detail);
    if (vr) {
        for (int i = 0; i < VRS_ASSERT_COUNT; i++)
            printf("V%-2d %-26s %-5s %12.2f  %s\n", i + 1,
                   vr->a[i].label, kpi_verdict_name(vr->a[i].verdict),
                   vr->a[i].value, vr->a[i].detail);
    }
}

int cli_check(const NTRIP_Config *config, bool vrs_mode)
{
    NsOptions opt;
    ns_options_default(&opt);
    opt.config           = *config;
    opt.stats_interval_s = 0.0;
    /* GGA is driven below rather than by the session timer, so the
     * assertion engine knows exactly when each one went out. */
    opt.send_gga         = false;
    /* Set from the mountpoint's own sourcetable entry, below. */
    bool wants_gga       = false;
    /* A drop is a finding here, not a nuisance to paper over. */
    opt.auto_reconnect   = false;

    NtripSession *sess = ns_open(&opt, check_on_event, NULL);
    if (!sess) {
        fprintf(stderr, "[CHECK] Could not open the session\n");
        return 1;
    }

    /* What this mountpoint promises, so the advertised-versus-actual
     * KPI has something to compare against.  Without it that KPI can
     * only report "cannot judge", and the run never reaches a verdict. */
    {
        char *table = receive_mount_table(config,
                                          NTRIP_USER_AGENT(NTRIP_ARTEFACT_CLI));
        if (table) {
            int n = sourcetable_parse(table, NULL, 0);
            SourcetableEntry *e = (n > 0)
                ? (SourcetableEntry *)calloc((size_t)n, sizeof(*e)) : NULL;
            if (e) {
                n = sourcetable_parse(table, e, n);
                for (int i = 0; i < n; i++) {
                    if (strcmp(e[i].mountpoint, config->MOUNTPOINT) != 0) continue;
                    SourcetableType t[NS_MAX_TYPES];
                    int nt = sourcetable_parse_types(e[i].format_details,
                                                     t, NS_MAX_TYPES);
                    if (nt > 0) {
                        ns_set_advertised(sess, t, nt);
                        ns_set_advertised_gnss(sess,
                            sourcetable_navsys_mask(e[i].nav_systems));
                        fprintf(stderr, "[CHECK] %s advertises %d message types\n",
                                config->MOUNTPOINT, nt);
                    }
                    /* Whether to uplink a GGA is a property of the
                     * mountpoint, not of the run: the STR record's NMEA
                     * flag says the caster expects one, and a network
                     * service that receives none sends nothing back.
                     * Without this, --check on a VRS mountpoint reported
                     * a healthy service as FAILED -- "connected but no
                     * data arriving" -- which is a measurement artefact
                     * dressed as a station fault.  Verified against
                     * caster.centipede.fr/NEAR, which streams within a
                     * second of the first sentence and not at all
                     * before it. */
                    wants_gga = e[i].nmea;
                    break;
                }
                free(e);
            }
            free(table);
        } else {
            fprintf(stderr, "[CHECK] No sourcetable; KPI 8 cannot be judged\n");
        }
    }

    KpiRun krun;  KpiReport kr;
    VrsRun vrun;  VrsReport vr;
    memset(&kr, 0, sizeof(kr));
    memset(&vr, 0, sizeof(vr));
    kpi_run_start(&krun, 0.0);
    vrs_run_start(&vrun, 0.0);

    time_t t0 = time(NULL);
    double last_gga = -1e9;
    bool   gate_started = false;
    int    last_tick = -1;

    fprintf(stderr, "[CHECK] %s acceptance test on %s:%d/%s\n",
            vrs_mode ? "Network-RTK" : "Station",
            config->NTRIP_CASTER, config->NTRIP_PORT, config->MOUNTPOINT);

    if (wants_gga && !vrs_mode)
        fprintf(stderr, "[CHECK] %s asks for a GGA uplink; reporting "
                        "%.6f, %.6f\n",
                config->MOUNTPOINT, config->LATITUDE, config->LONGITUDE);

    for (;;) {
        bool alive = ns_pump(sess, 200) >= 0;
        double el = (double)(time(NULL) - t0);
        const NsStatsSnapshot *snap = ns_stats(sess);

        if (vrs_mode && !gate_started && el - last_gga >= 10.0) {
            if (ns_send_gga(sess, config->LATITUDE, config->LONGITUDE))
                vrs_note_gga(&vrun, snap, el,
                             config->LATITUDE, config->LONGITUDE);
            last_gga = el;
        } else if (!vrs_mode && wants_gga && el - last_gga >= 10.0) {
            /* No bookkeeping here: the network-RTK assertions above
             * need to know when each sentence went out, a plain station
             * check only needs the stream to flow. */
            ns_send_gga(sess, config->LATITUDE, config->LONGITUDE);
            last_gga = el;
        }

        kpi_update(&krun, snap, el, &kr);
        if (vrs_mode) vrs_update(&vrun, snap, el, &vr);

        if ((int)el / 15 != last_tick) {
            last_tick = (int)el / 15;
            fprintf(stderr, "[CHECK] t=%3.0fs  %-10s  sustained=%2.0fs%s\n",
                    el, kpi_run_verdict_name(kr.overall), kr.sustained_s,
                    gate_started ? "  (gate test)" : "");
        }

        /* The gate test comes last: stop sending GGA and let the
         * caster's reaction classify the service. */
        if (vrs_mode && !gate_started &&
            kr.sustained_s >= KPI_SUSTAIN_S &&
            vr.a[3].verdict == KPI_PASS) {
            fprintf(stderr, "[CHECK] KPIs met; stopping GGA for the gate test\n");
            vrs_begin_gate_test(&vrun, el);
            gate_started = true;
        }

        bool done = false;
        if (!vrs_mode) {
            done = kr.settled;
        } else if (gate_started) {
            done = (vr.gate == VRS_GATE_GATED || vr.gate == VRS_GATE_NOT_GATED);
        } else {
            done = vr.failed || kr.overall == KPI_RUN_FAILED;
        }
        if (done) break;
        if (!alive && !(vrs_mode && gate_started)) break;
        if (el > 300.0) break;              /* absolute ceiling */
    }

    check_print(&kr, vrs_mode ? &vr : NULL);

    int rc;
    if (kr.overall == KPI_RUN_FAILED || (vrs_mode && vr.failed)) rc = 1;
    else if (kr.overall == KPI_RUN_OK)                           rc = 0;
    else                                                         rc = 6;

    printf("\n== %s ==", kpi_run_verdict_name(kr.overall));
    if (vrs_mode) printf("  [service: %s]", vrs_gate_name(vr.gate));
    printf("  exit=%d\n", rc);

    ns_close(sess);
    return rc;
}
