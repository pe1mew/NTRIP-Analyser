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
    opt.auto_reconnect   = false;
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
    opt.auto_reconnect   = false;
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
