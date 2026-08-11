/**
 * @file ntrip-monitord.c
 * @brief NTRIP monitoring daemon: hold sessions, publish snapshots.
 *
 * The first real consumer of the session layer (design/architecture.md
 * §5, §9 step 3).  It holds one persistent NtripSession per configured
 * mountpoint and, every interval, writes each session's statistics
 * snapshot as single-line JSON to
 *
 *     <output_dir>/<mountpoint>.json
 *
 * atomically: written to a temporary file, then rename()d into place, so
 * a reader never observes a half-written snapshot.  The Munin plugin in
 * service/munin/ reads these files; so can anything else -- the snapshot
 * is the same schema the GUI will export (src/core/ns_stats.h).
 *
 * Why a daemon at all: munin-node invokes plugins every five minutes and
 * expects an answer in seconds.  Rates need a persistent session, and
 * dropouts -- the thing a stream monitor most needs to catch -- are
 * invisible to a probe that connects briefly per poll.
 *
 * Threading: a single thread round-robins ns_pump() over all sessions.
 * Adequate to roughly a dozen mountpoints; revisit per §10 if that grows.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "session/ntrip_session.h"
#include "core/ns_stats.h"
#include "core/version.h"

#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <time.h>

#ifdef _WIN32
  #include <windows.h>
  #include <direct.h>
  #define PATH_SEP "\\"
#else
  #include <unistd.h>
  #include <sys/stat.h>
  #define PATH_SEP "/"
#endif

#define MD_MAX_SESSIONS 16
#define MD_JSON_MAX     16384

/* ── Configuration ───────────────────────────────────────────────── */

typedef struct {
    char   output_dir[512];
    double interval_s;          /* snapshot write interval             */
    int    n;
    NsOptions opt[MD_MAX_SESSIONS];
} MdConfig;

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

/**
 * @brief Load the daemon configuration.
 *
 * Format (JSON):
 * @code
 * {
 *   "output_dir": "/var/lib/ntrip-monitor",
 *   "interval_s": 10,
 *   "mountpoints": [
 *     { "caster": "rfsee.net", "port": 2101, "mountpoint": "RFSEE01",
 *       "username": "u", "password": "p",
 *       "send_gga": false, "latitude": 52.0, "longitude": 6.0 }
 *   ]
 * }
 * @endcode
 *
 * This is deliberately not config.json: that schema describes one
 * connection for an interactive tool, and a monitor needs a list
 * (design/architecture.md §10, question 4 -- resolved this way).
 */
static bool load_md_config(const char *path, MdConfig *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->interval_s = 10.0;
    strncpy(cfg->output_dir, ".", sizeof(cfg->output_dir) - 1);

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "ntrip-monitord: cannot open config %s: %s\n",
                path, strerror(errno));
        return false;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > 1024 * 1024) { fclose(f); return false; }

    char *text = (char *)malloc((size_t)len + 1);
    if (!text) { fclose(f); return false; }
    if (fread(text, 1, (size_t)len, f) != (size_t)len) {
        free(text); fclose(f); return false;
    }
    text[len] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(text);
    free(text);
    if (!root) {
        fprintf(stderr, "ntrip-monitord: %s is not valid JSON\n", path);
        return false;
    }

    const cJSON *v;
    if ((v = cJSON_GetObjectItem(root, "output_dir")) && cJSON_IsString(v))
        strncpy(cfg->output_dir, v->valuestring, sizeof(cfg->output_dir) - 1);
    if ((v = cJSON_GetObjectItem(root, "interval_s")) && cJSON_IsNumber(v) &&
        v->valuedouble >= 1.0)
        cfg->interval_s = v->valuedouble;

    const cJSON *arr = cJSON_GetObjectItem(root, "mountpoints");
    if (!cJSON_IsArray(arr) || cJSON_GetArraySize(arr) == 0) {
        fprintf(stderr, "ntrip-monitord: config has no mountpoints\n");
        cJSON_Delete(root);
        return false;
    }

    const cJSON *mp;
    cJSON_ArrayForEach(mp, arr) {
        if (cfg->n >= MD_MAX_SESSIONS) {
            fprintf(stderr, "ntrip-monitord: more than %d mountpoints; "
                    "the rest are ignored\n", MD_MAX_SESSIONS);
            break;
        }
        NsOptions *o = &cfg->opt[cfg->n];
        ns_options_default(o);
        o->auto_reconnect = true;      /* a monitor must ride out drops */
        o->stats_interval_s = 0.0;     /* the daemon reads stats itself */

        const cJSON *s;
        if ((s = cJSON_GetObjectItem(mp, "caster")) && cJSON_IsString(s))
            strncpy(o->config.NTRIP_CASTER, s->valuestring,
                    sizeof(o->config.NTRIP_CASTER) - 1);
        if ((s = cJSON_GetObjectItem(mp, "port")) && cJSON_IsNumber(s))
            o->config.NTRIP_PORT = s->valueint;
        if ((s = cJSON_GetObjectItem(mp, "mountpoint")) && cJSON_IsString(s))
            strncpy(o->config.MOUNTPOINT, s->valuestring,
                    sizeof(o->config.MOUNTPOINT) - 1);
        if ((s = cJSON_GetObjectItem(mp, "username")) && cJSON_IsString(s))
            strncpy(o->config.USERNAME, s->valuestring,
                    sizeof(o->config.USERNAME) - 1);
        if ((s = cJSON_GetObjectItem(mp, "password")) && cJSON_IsString(s))
            strncpy(o->config.PASSWORD, s->valuestring,
                    sizeof(o->config.PASSWORD) - 1);
        if ((s = cJSON_GetObjectItem(mp, "send_gga")) && cJSON_IsBool(s))
            o->send_gga = cJSON_IsTrue(s);
        if ((s = cJSON_GetObjectItem(mp, "latitude")) && cJSON_IsNumber(s))
            o->config.LATITUDE = s->valuedouble;
        if ((s = cJSON_GetObjectItem(mp, "longitude")) && cJSON_IsNumber(s))
            o->config.LONGITUDE = s->valuedouble;

        if (!o->config.NTRIP_CASTER[0] || !o->config.MOUNTPOINT[0]) {
            fprintf(stderr, "ntrip-monitord: mountpoint entry %d lacks "
                    "caster or mountpoint; skipped\n", cfg->n);
            continue;
        }
        if (o->config.NTRIP_PORT <= 0) o->config.NTRIP_PORT = 2101;
        cfg->n++;
    }

    cJSON_Delete(root);
    return cfg->n > 0;
}

/* ── Snapshot publication ────────────────────────────────────────── */

/**
 * @brief Sanitise a mountpoint into a filename: [A-Za-z0-9._-] only.
 *
 * A mountpoint is caster-supplied text; letting it name a path verbatim
 * would let "../x" escape the output directory.
 */
static void safe_name(const char *mount, char *out, size_t cap)
{
    size_t o = 0;
    for (const char *p = mount; *p && o < cap - 1; p++) {
        char c = *p;
        int ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                 (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        out[o++] = ok ? c : '_';
    }
    if (o == 0) out[o++] = '_';
    out[o] = '\0';
}

/**
 * @brief Write one session's snapshot atomically.
 *
 * POSIX rename() over an existing file is atomic, so a reader sees the
 * old snapshot or the new one, never a torn mix.  Windows rename() fails
 * on an existing target; MoveFileEx with MOVEFILE_REPLACE_EXISTING is
 * the equivalent there (Windows is a development convenience for this
 * daemon, not a deployment target).
 */
static bool publish(const MdConfig *cfg, const NtripSession *sess)
{
    const NsStatsSnapshot *st = ns_stats(sess);
    if (!st) return false;

    static char json[MD_JSON_MAX];
    int n = ns_stats_to_json(st, json, sizeof(json));
    if (n <= 0 || (size_t)n >= sizeof(json)) {
        fprintf(stderr, "ntrip-monitord: snapshot for %s did not fit "
                "(%d bytes)\n", st->mountpoint, n);
        return false;
    }

    char name[128];
    safe_name(st->mountpoint, name, sizeof(name));

    char final_path[768], tmp_path[784];
    snprintf(final_path, sizeof(final_path), "%s" PATH_SEP "%s.json",
             cfg->output_dir, name);
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", final_path);

    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        fprintf(stderr, "ntrip-monitord: cannot write %s: %s\n",
                tmp_path, strerror(errno));
        return false;
    }
    fputs(json, f);
    fputc('\n', f);
    if (fclose(f) != 0) return false;

#ifdef _WIN32
    if (!MoveFileExA(tmp_path, final_path, MOVEFILE_REPLACE_EXISTING)) {
        fprintf(stderr, "ntrip-monitord: replace of %s failed\n", final_path);
        return false;
    }
#else
    if (rename(tmp_path, final_path) != 0) {
        fprintf(stderr, "ntrip-monitord: rename to %s failed: %s\n",
                final_path, strerror(errno));
        return false;
    }
#endif
    return true;
}

/* ── Event sink ──────────────────────────────────────────────────── */

/**
 * @brief Session events go to stderr for journald.
 *
 * Only warnings and errors: at INFO a fleet of streams would write a log
 * line per connection state change per stream, which journald then
 * rate-limits right when something interesting happens.
 */
static void on_event(const NsEvent *ev, void *user)
{
    const char *mount = (const char *)user;
    switch (ev->type) {
    case NS_EV_LOG:
        if (ev->u.log.level != NS_LOG_INFO)
            fprintf(stderr, "[%s] %s\n", mount, ev->u.log.text);
        break;
    case NS_EV_DISCONNECTED:
        fprintf(stderr, "[%s] session ended (reason %d)\n",
                mount, ev->u.end.reason);
        break;
    default:
        break;
    }
}

/* ── Main ────────────────────────────────────────────────────────── */

static double md_now(void)
{
#ifdef _WIN32
    return (double)GetTickCount64() / 1000.0;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

int main(int argc, char **argv)
{
    const char *cfg_path = "monitord.json";
    bool oneshot = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            printf("%s %s\n", NTRIP_ARTEFACT_SERVICE, NTRIP_VERSION_STRING);
            return 0;
        } else if (strcmp(argv[i], "--oneshot") == 0) {
            /* Connect, publish one snapshot per mountpoint, exit.
             * Exists so the pipeline is testable without a service
             * manager: run with --oneshot, then inspect the files. */
            oneshot = true;
        } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            cfg_path = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [--config file.json] [--oneshot] [--version]\n"
                   "NTRIP stream monitor; writes per-mountpoint JSON "
                   "snapshots for Munin.\n", NTRIP_ARTEFACT_SERVICE);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s (try --help)\n", argv[i]);
            return 2;
        }
    }

    MdConfig cfg;
    if (!load_md_config(cfg_path, &cfg)) return 1;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    fprintf(stderr, "%s %s: %d mountpoint(s), interval %.0fs, output %s\n",
            NTRIP_ARTEFACT_SERVICE, NTRIP_VERSION_STRING,
            cfg.n, cfg.interval_s, cfg.output_dir);

    NtripSession *sess[MD_MAX_SESSIONS] = { 0 };
    for (int i = 0; i < cfg.n; i++) {
        sess[i] = ns_open(&cfg.opt[i], on_event,
                          (void *)cfg.opt[i].config.MOUNTPOINT);
        if (!sess[i]) {
            fprintf(stderr, "ntrip-monitord: out of memory\n");
            return 1;
        }
    }

    /* Initialise to now, not zero: the monotonic clock is seconds since
     * boot, so a zero start would make the first publish due immediately,
     * before any data has arrived. */
    double started = md_now();
    double last_publish = started;

    while (!g_stop) {
        /* Round-robin.  The timeout divides across sessions so one idle
         * stream cannot starve the others of pump time. */
        int timeout = cfg.n > 0 ? 200 / cfg.n : 200;
        if (timeout < 10) timeout = 10;

        bool all_ended = true;
        for (int i = 0; i < cfg.n; i++) {
            if (!sess[i]) continue;
            if (ns_pump(sess[i], timeout) >= 0) all_ended = false;
        }

        double now = md_now();
        /* In oneshot mode the periodic schedule is ignored: the single
         * publish happens after a settling window, so the snapshot shows
         * a stream rather than a handshake. */
        bool due = oneshot ? (now - started) >= 5.0
                           : (now - last_publish) >= cfg.interval_s;

        if (due) {
            last_publish = now;
            for (int i = 0; i < cfg.n; i++)
                if (sess[i]) publish(&cfg, sess[i]);
            if (oneshot) break;
        }
        if (all_ended) {
            /* Only reachable when auto_reconnect is off or every session
             * hit a terminal condition; publish the final state and stop
             * rather than spinning. */
            for (int i = 0; i < cfg.n; i++)
                if (sess[i]) publish(&cfg, sess[i]);
            break;
        }
    }

    for (int i = 0; i < cfg.n; i++) ns_close(sess[i]);
    fprintf(stderr, "%s: stopped\n", NTRIP_ARTEFACT_SERVICE);
    return 0;
}
