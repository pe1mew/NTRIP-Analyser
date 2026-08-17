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

const char *cli_capture_path      = NULL;  /* set by --capture     */
uint64_t    cli_capture_max_bytes = 0;     /* set by --capture-max */
bool        cli_report            = false; /* set by --report      */
bool        cli_replay_stdin      = false; /* set by --rtcm-stdin  */
#include "session/ntrip_session.h"
#include "core/version.h"
#include "core/rtcm3x_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>   /* Ctrl-C must close a capture, not kill it */

/* ── The thresholds in force ────────────────────────────────────────
 *
 * Built-in until --thresholds loads a file over them. Global for the
 * same reason cli_report is: a run has one standard, and threading it
 * through four mode functions would only create the opportunity for two
 * of them to disagree. */

Thresholds  cli_thresholds;
static bool cli_thresholds_ready = false;

static const Thresholds *cli_th(void)
{
    /* Filled on first use, so a mode that never asks still judges by
     * something rather than by a struct of zeros -- which would pass
     * every station ever measured. */
    if (!cli_thresholds_ready) {
        thresholds_defaults(&cli_thresholds);
        cli_thresholds_ready = true;
    }
    return &cli_thresholds;
}

bool cli_thresholds_load(const char *path)
{
    if (!path) return false;
    (void)cli_th();                    /* defaults first: this overlays */

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[ERROR] Cannot open thresholds file: %s\n", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > 1024 * 1024) {
        fprintf(stderr, "[ERROR] %s is empty or implausibly large\n", path);
        fclose(f);
        return false;
    }
    char *text = (char *)malloc((size_t)len + 1);
    if (!text) { fclose(f); return false; }
    if (fread(text, 1, (size_t)len, f) != (size_t)len) {
        fprintf(stderr, "[ERROR] Could not read %s\n", path);
        free(text);
        fclose(f);
        return false;
    }
    text[len] = '\0';
    fclose(f);

    char err[256] = "";
    bool ok = thresholds_parse(&cli_thresholds, text, err, sizeof(err));
    free(text);

    if (!ok) {
        /* Named, and refused. A clamped value would produce a verdict
         * the user did not ask for and could not reproduce. */
        fprintf(stderr, "[ERROR] %s: %s\n", path, err);
        fprintf(stderr, "        Thresholds unchanged; nothing was "
                        "half-applied.\n");
        return false;
    }
    /* A policy with no name of its own is named for its file, so the
     * banner and the report can still say which standard was used. */
    if (!cli_thresholds.name[0])
        snprintf(cli_thresholds.name, sizeof(cli_thresholds.name), "%s", path);
    return true;
}

const char *cli_thresholds_banner(void)
{
    /* Sized from what it prints, not from a round number: the fixed text
     * plus a full-length policy name plus the fingerprint. */
    static char line[TH_NAME_LEN + 160];
    const Thresholds *t = cli_th();
    if (!t->loaded) return "";

    char fp[16];
    thresholds_fingerprint(t, fp, sizeof(fp));
    snprintf(line, sizeof(line),
             "[POLICY] Judged by \"%s\" (fingerprint %s), not the built-in "
             "thresholds\n", t->name, fp);
    return line;
}

void cli_thresholds_print(void)
{
    const Thresholds *t = cli_th();
    char fp[16];
    thresholds_fingerprint(t, fp, sizeof(fp));

    printf("Thresholds in force: %s (fingerprint %s)\n",
           t->loaded ? t->name : "built-in defaults", fp);
    printf("\n%-24s %14s  %-9s %s\n", "field", "value", "from", "meaning");

    int last_tier = 0;
    for (int i = 0; i < thresholds_field_count(); i++) {
        const ThField *f = thresholds_field(i);
        if ((int)f->tier != last_tier) {
            printf("\n-- %s\n",
                   f->tier == TH_TIER1 ? "tier 1 (the acceptance check)"
                 : f->tier == TH_TIER2 ? "tier 2 (the stability report)"
                                       : "vrs (the network-RTK assertions)");
            last_tier = (int)f->tier;
        }
        char val[40];
        snprintf(val, sizeof(val), "%.*f%s%s", f->decimals,
                 thresholds_value(t, i), *f->unit ? " " : "", f->unit);
        printf("%-24s %14s  %-9s %s\n", f->key, val,
               thresholds_is_set(t, i) ? "file" : "built-in", f->what);
    }

    printf("\n-- tier 1 satellites expected, per constellation\n");
    static const char *names[8] = { NULL, "gps", "glonass", "galileo",
                                    "qzss", "beidou", "sbas", "navic" };
    for (int g = 1; g <= 7; g++)
        printf("%-24s %14d\n", names[g], t->kpi.expect_sats[g]);

    printf("\nA station is held to the sum over the constellations it "
           "streams, never to a flat total.\n"
           "Every one of these is a judgement rather than a fact: "
           "docs/thresholds.md says how well founded each is.\n");
}

/* The old implementations printed "GGA " into the type stream on every
 * uplink, once per second.  Kept: it is a useful heartbeat and scripts
 * may key on it. */
#define CLI_GGA_INTERVAL_S 1

/* Seconds of *stream* between tier-2 samples.  One a second matches what
 * a 1 Hz station sends, and makes the sample count a reader can compare
 * against the window an honest number in both directions. */
#define CLI_REPORT_SAMPLE_S 1.0

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

    /* tier 2, accumulated once a second while --report is set */
    SrState    sr;
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

/* ── The tier-2 report ──────────────────────────────────────────────── */

void cli_report_print(const SrState *sr)
{
    if (!cli_report || !sr) return;

    StationReport r;
    sr_build(sr, &r);

    fputs(cli_thresholds_banner(), stdout);

    /* 21 characters wide because "INSUFFICIENT EVIDENCE" is that long,
     * and it is the value most runs will show. */
    printf("\n%-3s %-18s %-21s %13s %-18s %s\n",
           "", "Stability", "verdict", "value", "limit", "detail");
    for (int i = 0; i < SR_METRIC_COUNT; i++) {
        const SrMetric *m = &r.metric[i];
        char lim[48];
        sr_metric_limit_text(m, i, lim, sizeof(lim));
        if (!m->available) {
            /* Stated, not omitted: a metric missing without explanation
             * reads as a metric that passed. */
            printf("%-3s %-18s %-21s %13s %-18s %s\n", "-", m->label,
                   "n/a", "--", lim, m->detail);
            continue;
        }
        char num[48];
        const char *unit = sr_metric_unit(i);
        snprintf(num, sizeof(num), "%.*f%s%s", sr_metric_decimals(i),
                 m->value, *unit ? " " : "", unit);
        /* 13 wide: "0.00 TECU/min" is the longest a metric produces. */
        printf("%-3d %-18s %-21s %13s %-18s %s\n", i + 1, m->label,
               sr_verdict_name(m->verdict), num, lim, m->detail);
    }

    printf("\n== %s ==  window %.0f s, %d samples\n",
           r.headline, r.window_s, r.samples);

    /* Nothing was measured at all, which has two causes and no way here
     * to tell them apart -- so say both rather than let a blank report
     * read as a stream that behaved. */
    if (r.samples == 0)
        printf("(no samples: the run was shorter than the %.0f s warm-up, "
               "or the stream carries no observation epochs and so has no "
               "clock to measure a window with)\n", SR_WARMUP_S);

    /* Deliberately no exit code: see cli_report in the header. */
}

/* ── Capture, shared by every mode that opens a stream ──────────────── */

/* Ctrl-C is handled in --sky and nowhere else, so in the stream modes it
 * kills the process outright: no ns_close(), no fclose().  With a
 * capture running that is the wrong ending -- the file is the artefact
 * the run existed for, and an operator stopping a day-long -t early
 * should get a complete file, and its census with it.
 *
 * Installed only while capturing.  Without it the modes keep the
 * behaviour they have always had, which is not this change's to alter;
 * whether they should all stop gracefully is a question on the track. */
static volatile sig_atomic_t cli_stop_requested = 0;

static void cli_on_sigint(int sig) { (void)sig; cli_stop_requested = 1; }

static void cli_capture_catch_sigint(void)
{
    if (!cli_capture_path) return;
    cli_stop_requested = 0;
    signal(SIGINT, cli_on_sigint);
#ifdef SIGTERM
    signal(SIGTERM, cli_on_sigint);
#endif
}

void cli_capture_apply(NsOptions *opt)
{
    if (!opt) return;
    opt->capture_path      = cli_capture_path;
    opt->capture_max_bytes = cli_capture_max_bytes;
    cli_capture_catch_sigint();
}

bool cli_capture_finish(const NtripSession *s)
{
    if (!s || !cli_capture_path) return false;

    uint64_t bytes = 0, frames = 0;
    const char *path = ns_capture_status(s, &bytes, &frames);

    if (ns_capture_failed(s)) {
        /* The session already said why on its log event.  This line is
         * the summary a script's operator reads in the morning. */
        fprintf(stderr, "[ERROR] Capture failed after %llu bytes\n",
                (unsigned long long)bytes);
        return true;
    }
    if (!path) return false;

    /* Reconnects are gaps in the capture, and RTCM carries no wall
     * clock -- so the count is the only way to know how many to expect
     * before a converter is pointed at the file. */
    const NsStatsSnapshot *st = ns_stats(s);
    fprintf(stderr, "[INFO] Capture: %llu frames, %llu bytes -> %s",
            (unsigned long long)frames, (unsigned long long)bytes, path);
    if (st && st->reconnects > 0)
        fprintf(stderr, " (%d reconnect%s, so expect that many gaps)",
                st->reconnects, st->reconnects == 1 ? "" : "s");
    fprintf(stderr, "\n");
    return false;
}

/**
 * @brief Open a session for a CLI mode and pump it.
 *
 * Live or replayed, on the same code path: under `--rtcm-stdin` the
 * bytes come from stdin instead of a caster, and everything downstream
 * -- framing, CRC, statistics, the report -- is identical, which is the
 * only way offline analysis cannot drift from live behaviour.
 *
 * @param seconds 0 = run until the stream ends or the process is
 *                interrupted; otherwise stop after this many seconds of
 *                **stream**.  Live that is wall time too; over a replay
 *                it is the amount of the capture analysed, so `-t 600`
 *                means the same thing to both.
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
    cli_capture_apply(&opt);

    /* stdin is not a path, so it is the one source ns_open_file() cannot
     * express; ns_close() leaves the handle open, since we do not own
     * it.  Capturing a replay is not a contradiction -- it rewrites the
     * input keeping only frames with a valid CRC. */
    NtripSession *sess = cli_replay_stdin
        ? ns_open_stream(stdin, false, &opt, cli_on_event, c)
        : ns_open(&opt, cli_on_event, c);
    if (!sess) {
        fprintf(stderr, "[ERROR] Out of memory opening the stream session\n");
        return NULL;
    }

    time_t t_start   = time(NULL);
    time_t last_gga  = 0;   /* forces an immediate first GGA */

    /* A replay holds no arrival times and never drops, so availability
     * is reported as unavailable rather than as a clean zero it did not
     * earn.  test_station_report.c pins that distinction. */
    sr_reset(&c->sr, cli_replay_stdin, &cli_th()->sr);

    /* Stream time of the last tier-2 sample.  The cadence is the
     * stream's, not this host's: a six-hour capture read from disk in
     * two seconds must yield six hours of samples, not two.  Live the
     * two cadences are the same one. */
    double last_srs = -1e9;

    for (;;) {
        if (cli_stop_requested) break;   /* only set while capturing */

        const NsStatsSnapshot *snap = ns_stats(sess);
        double t_stream = snap ? snap->stream_time_s : NS_UNSET;

        if (cli_report && t_stream >= 0.0
            && t_stream - last_srs >= CLI_REPORT_SAMPLE_S) {
            last_srs = t_stream;
            sr_feed(&c->sr, snap, t_stream);
        }

        if (cli_replay_stdin) {
            /* The duration bounds the stream analysed, not the seconds
             * spent analysing it.  A capture shorter than `seconds` ends
             * at EOF, which ns_pump() reports below. */
            if (seconds > 0 && t_stream >= (double)seconds) break;
        } else {
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
        }

        /* No wait on a replay: there is nothing to wait for, and a
         * 200 ms poll would turn six hours of capture into a crawl. */
        if (ns_pump(sess, cli_replay_stdin ? 0 : 200) < 0) break;
    }
    return sess;
}

/* ── Modes ──────────────────────────────────────────────────────────── */

int cli_stream_decode(const NTRIP_Config *config,
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
    cli_report_print(&c.sr);
    int rc = cli_capture_finish(sess) ? EXIT_CAPTURE_FAILED : 0;
    ns_close(sess);
    return rc;
}

int cli_analyze_types(const NTRIP_Config *config, int seconds)
{
    if (cli_replay_stdin)
        printf("[INFO] Replaying RTCM from stdin, analyzing message types "
               "over %d s of stream...\n", seconds);
    else
        printf("[INFO] Analyzing message types for %d seconds...\n", seconds);

    CliCtx c;
    memset(&c, 0, sizeof(c));
    c.config = config;
    c.mode   = CLI_MODE_TYPES;

    NtripSession *sess = cli_run(&c, seconds);
    if (!sess) return 0;

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

    cli_report_print(&c.sr);
    int rc = cli_capture_finish(sess) ? EXIT_CAPTURE_FAILED : 0;
    ns_close(sess);
    return rc;
}

int cli_analyze_sats(const NTRIP_Config *config, int seconds)
{
    if (cli_replay_stdin)
        printf("Replaying RTCM from stdin and analyzing satellites over "
               "%d s of stream...\n", seconds);
    else
        printf("Opening NTRIP stream and analyzing satellites for "
               "%d seconds...\n", seconds);

    CliCtx c;
    memset(&c, 0, sizeof(c));
    c.config = config;
    c.mode   = CLI_MODE_SATS;

    NtripSession *sess = cli_run(&c, seconds);
    if (!sess) return 0;
    int rc = cli_capture_finish(sess) ? EXIT_CAPTURE_FAILED : 0;
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
    cli_report_print(&c.sr);
    return rc;
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

        /* 1005/1006 are among the types this deliberately ignores: an
         * eph caster's own station position must not touch the obs ARP. */
        c->eph_count += rtcm_decode_eph(frame + 3, payload_len, mt);

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
    /* "%.*s" rather than strncpy for the same reason as in the session
     * layer: the truncation is intended, and stating the bound in the
     * call is the spelling gcc does not warn about. */
    snprintf(opt.config.NTRIP_CASTER, sizeof(opt.config.NTRIP_CASTER), "%.*s",
             (int)sizeof(opt.config.NTRIP_CASTER) - 1, config->EPH_CASTER);
    opt.config.NTRIP_PORT = config->EPH_PORT;
    snprintf(opt.config.MOUNTPOINT, sizeof(opt.config.MOUNTPOINT), "%.*s",
             (int)sizeof(opt.config.MOUNTPOINT) - 1, config->EPH_MOUNTPOINT);
    snprintf(opt.config.USERNAME, sizeof(opt.config.USERNAME), "%.*s",
             (int)sizeof(opt.config.USERNAME) - 1, config->EPH_USERNAME);
    snprintf(opt.config.PASSWORD, sizeof(opt.config.PASSWORD), "%.*s",
             (int)sizeof(opt.config.PASSWORD) - 1, config->EPH_PASSWORD);
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
    /* The standard, above the verdict it produced: a run under a policy
     * that is not the built-in one must not be quotable without it. */
    fputs(cli_thresholds_banner(), stdout);
    printf("\n%-3s %-26s %-5s %13s %-18s %s\n",
           "#", "KPI", "verd", "value", "limit", "detail");
    for (int i = 0; i < KPI_COUNT; i++)
    {
        char num[48], lim[48];
        const char *unit = kpi_value_unit(i);
        snprintf(num, sizeof(num), "%.*f%s%s", kpi_value_decimals(i),
                 kr->kpi[i].value, *unit ? " " : "", unit);
        kpi_limit_text(&kr->kpi[i], i, lim, sizeof(lim));
        printf("%-3d %-26s %-5s %13s %-18s %s\n", i + 1,
               kr->kpi[i].label, kpi_verdict_name(kr->kpi[i].verdict),
               num, lim, kr->kpi[i].detail);
    }
    if (vr) {
        for (int i = 0; i < VRS_ASSERT_COUNT; i++)
            /* The assertions carry no limit of their own: each is a
             * yes-or-no about caster behaviour, and its deadline is in
             * the detail. The column is left blank rather than filled
             * with a number that would not mean the same thing. */
            printf("V%-2d %-26s %-5s %13.2f %-18s %s\n", i + 1,
                   vr->a[i].label, kpi_verdict_name(vr->a[i].verdict),
                   vr->a[i].value, "", vr->a[i].detail);
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
    cli_capture_apply(&opt);

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
    kpi_run_start(&krun, 0.0, &cli_th()->kpi);
    vrs_run_start(&vrun, 0.0, &cli_th()->vrs);

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

    SrState sr;
    sr_reset(&sr, false, &cli_th()->sr);
    double last_srs = -1e9;   /* stream time of the last tier-2 sample */

    for (;;) {
        if (cli_stop_requested) break;   /* only set while capturing */
        bool alive = ns_pump(sess, 200) >= 0;
        double el = (double)(time(NULL) - t0);
        const NsStatsSnapshot *snap = ns_stats(sess);

        /* Paced and stamped by the stream's clock rather than el, which
         * is the same rule cli_run() follows: one rule, so a reader does
         * not have to work out which clock a given report used. */
        if (cli_report && snap && snap->stream_time_s >= 0.0
            && snap->stream_time_s - last_srs >= CLI_REPORT_SAMPLE_S) {
            last_srs = snap->stream_time_s;
            sr_feed(&sr, snap, snap->stream_time_s);
        }

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

    /* A failed capture outranks the verdict, including the caution
     * code: if the file this run existed to produce is not there, the
     * station's grade is not the news the operator needs first. */
    if (cli_capture_finish(sess)) rc = EXIT_CAPTURE_FAILED;

    printf("\n== %s ==", kpi_run_verdict_name(kr.overall));
    if (vrs_mode) printf("  [service: %s]", vrs_gate_name(vr.gate));
    printf("  exit=%d\n", rc);

    /* Tier 2 beneath tier 1, and plainly a different question: a ~90 s
     * check will almost always report INSUFFICIENT EVIDENCE here, which
     * is the honest answer and worth seeing. */
    cli_report_print(&sr);

    ns_close(sess);
    return rc;
}
