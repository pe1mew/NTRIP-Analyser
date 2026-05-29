#define _WIN32_WINNT 0x0601 // Define Windows version (Windows 7 or higher)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <getopt.h>
#include <signal.h>
#include <time.h>
#include <stdbool.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h> // Required for getaddrinfo
#include <process.h>  // _beginthreadex
#include <conio.h>    // _kbhit / _getch for Ctrl-A polling
#include <io.h>       // _isatty / _fileno / _setmode for TTY + binary stdin
#include <fcntl.h>    // _O_BINARY for _setmode
#else
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <termios.h>
#include <sys/select.h>
#endif
#include "cJSON.h" // Include cJSON library
#include "rtcm3x_parser.h" // Include RTCM parser header
#include "ntrip_handler.h"
#include "config.h"
#include "cli_help.h"
#include "sky_collect.h"
#include "sky_render.h"
#include "rinex_nav.h"
#include "nmea_parser.h"

#define BUFFER_SIZE 4096
#define MAX_MSG_TYPES 4096

// Define column widths for verbose printing
#define CONF_KEY_WIDTH 14
#define CONF_VAL_WIDTH 26

typedef struct {
    int count;
    double min_dt;
    double max_dt;
    double sum_dt;
    double last_time;
    bool seen;
} MsgStats;

bool verbose = false;
bool quiet   = false;        /* -q / --quiet: suppress info chatter */
bool no_progress = false;    /* --no-progress: never emit the per-second status line */
bool json_output = false;    /* --json: machine-readable status lines */
bool rtcm_stdin  = false;    /* --rtcm-stdin: read obs RTCM from stdin */
int filter_list[MAX_MSG_TYPES] = {0};
int filter_count = 0;

/* ── Exit codes (documented in --help and docs/compile.md) ─────────── */
#define EXIT_OK              0
#define EXIT_GENERIC         1   /* connect/runtime failure */
#define EXIT_BAD_ARGS        2   /* getopt unknown / malformed flag */
#define EXIT_CONFIG_ERROR    3   /* load_config() failed */
#define EXIT_NO_EPH          4   /* --sky pre-flight: no eph source */
#define EXIT_ABORTED         5   /* user pressed Ctrl-A */

/* INFO / ERR macros.  Both write to stderr so that stdout stays reserved
 * for "data" output (mountpoint sourcetable, decoded RTCM dumps, the
 * --sky PNG path, etc.).  Scripts piping stdout get just the data. */
#define INFO(...)  do { if (!quiet) fprintf(stderr, __VA_ARGS__); } while (0)
#define ERR(...)   do { fprintf(stderr, __VA_ARGS__); } while (0)

/* ── Action-flag conflict detection ─────────────────────────────────────
 * Reject combinations like  -d -S  or  -m --sat  rather than silently
 * letting the last verb win.  claim_action() is called for every action
 * flag; if two different ops were seen, main() rejects with
 * EXIT_BAD_ARGS after parsing finishes. */
static const char *action_seen_first  = NULL;   /* e.g. "-d / --decode" */
static const char *action_seen_second = NULL;
static void claim_action(Operation *op, Operation candidate, const char *flag_name)
{
    if (*op != OP_NONE && *op != candidate && !action_seen_second) {
        action_seen_second = flag_name;
    } else if (*op == OP_NONE) {
        action_seen_first = flag_name;
    }
    *op = candidate;
}

/* ── Config-field overrides (CLI > env > config file) ──────────────────
 * Each pointer is NULL when not set.  Filled by getopt parsing, then by
 * getenv() for any still-NULL slots, then applied to NTRIP_Config.
 */
typedef struct {
    const char *caster;
    const char *port;
    const char *mountpoint;
    const char *user;
    const char *password;
    const char *eph_caster;
    const char *eph_port;
    const char *eph_mountpoint;
    const char *eph_user;
    const char *eph_password;
} ConfigOverrides;

static void overrides_apply_env(ConfigOverrides *o)
{
    if (!o->caster)         o->caster         = getenv("NTRIP_CASTER");
    if (!o->port)           o->port           = getenv("NTRIP_PORT");
    if (!o->mountpoint)     o->mountpoint     = getenv("NTRIP_MOUNTPOINT");
    if (!o->user)           o->user           = getenv("NTRIP_USERNAME");
    if (!o->password)       o->password       = getenv("NTRIP_PASSWORD");
    if (!o->eph_caster)     o->eph_caster     = getenv("NTRIP_EPH_CASTER");
    if (!o->eph_port)       o->eph_port       = getenv("NTRIP_EPH_PORT");
    if (!o->eph_mountpoint) o->eph_mountpoint = getenv("NTRIP_EPH_MOUNTPOINT");
    if (!o->eph_user)       o->eph_user       = getenv("NTRIP_EPH_USERNAME");
    if (!o->eph_password)   o->eph_password   = getenv("NTRIP_EPH_PASSWORD");
}

#define ASSIGN_STR(dst, src) do {                              \
    strncpy((dst), (src), sizeof(dst) - 1);                    \
    (dst)[sizeof(dst) - 1] = '\0';                             \
} while (0)

static void overrides_apply_to_config(NTRIP_Config *cfg, const ConfigOverrides *o)
{
    if (o->caster         && o->caster[0])         ASSIGN_STR(cfg->NTRIP_CASTER,    o->caster);
    if (o->port           && o->port[0])           cfg->NTRIP_PORT = atoi(o->port);
    if (o->mountpoint     && o->mountpoint[0])     ASSIGN_STR(cfg->MOUNTPOINT,      o->mountpoint);
    if (o->user           && o->user[0])           ASSIGN_STR(cfg->USERNAME,        o->user);
    if (o->password       && o->password[0])       ASSIGN_STR(cfg->PASSWORD,        o->password);
    if (o->eph_caster     && o->eph_caster[0])     ASSIGN_STR(cfg->EPH_CASTER,      o->eph_caster);
    if (o->eph_port       && o->eph_port[0])       cfg->EPH_PORT = atoi(o->eph_port);
    if (o->eph_mountpoint && o->eph_mountpoint[0]) ASSIGN_STR(cfg->EPH_MOUNTPOINT,  o->eph_mountpoint);
    if (o->eph_user       && o->eph_user[0])       ASSIGN_STR(cfg->EPH_USERNAME,    o->eph_user);
    if (o->eph_password   && o->eph_password[0])   ASSIGN_STR(cfg->EPH_PASSWORD,    o->eph_password);
}

/* ── --check-config dry-run ──────────────────────────────────────────
 * Validates the resolved config (after CLI overrides + env vars have been
 * applied), reports each field on stdout (so a script can grep/parse),
 * and DNS-resolves the casters.  Returns EXIT_OK if everything looks
 * good; EXIT_GENERIC if a DNS lookup or required-field check fails. */
static int run_check_config(const NTRIP_Config *cfg)
{
    int problems = 0;

    printf("NTRIP_CASTER         = %s\n", cfg->NTRIP_CASTER);
    printf("NTRIP_PORT           = %d\n", cfg->NTRIP_PORT);
    printf("MOUNTPOINT           = %s\n", cfg->MOUNTPOINT);
    printf("USERNAME             = %s\n", cfg->USERNAME);
    printf("PASSWORD             = %s\n", cfg->PASSWORD[0] ? "(set)" : "(empty)");
    printf("LATITUDE             = %.6f\n", cfg->LATITUDE);
    printf("LONGITUDE            = %.6f\n", cfg->LONGITUDE);

    bool have_eph = cfg->EPH_CASTER[0] && cfg->EPH_PORT > 0 && cfg->EPH_MOUNTPOINT[0];
    printf("EPH_CASTER           = %s\n", cfg->EPH_CASTER[0] ? cfg->EPH_CASTER : "(none)");
    printf("EPH_PORT             = %d\n", cfg->EPH_PORT);
    printf("EPH_MOUNTPOINT       = %s\n", cfg->EPH_MOUNTPOINT[0] ? cfg->EPH_MOUNTPOINT : "(none)");
    printf("EPH_USERNAME         = %s\n", cfg->EPH_USERNAME);
    printf("EPH_PASSWORD         = %s\n", cfg->EPH_PASSWORD[0] ? "(set)" : "(empty)");
    printf("EPH_STREAM           = %s\n", have_eph ? "configured" : "(not configured)");

    /* Required-field checks */
    if (!cfg->NTRIP_CASTER[0]) {
        ERR("[CHECK] NTRIP_CASTER is empty\n");
        problems++;
    }
    if (cfg->NTRIP_PORT <= 0) {
        ERR("[CHECK] NTRIP_PORT is %d (expected positive)\n", cfg->NTRIP_PORT);
        problems++;
    }
    if (!cfg->MOUNTPOINT[0]) {
        ERR("[CHECK] MOUNTPOINT is empty\n");
        problems++;
    }

    /* DNS lookup of casters */
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (cfg->NTRIP_CASTER[0]) {
        int rc = getaddrinfo(cfg->NTRIP_CASTER, NULL, &hints, &res);
        if (rc == 0) {
            char ip[INET_ADDRSTRLEN] = "";
            struct sockaddr_in *sa = (struct sockaddr_in *)res->ai_addr;
            inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip));
            printf("DNS_OBS              = %s (%s)\n", cfg->NTRIP_CASTER, ip);
            freeaddrinfo(res);
        } else {
            printf("DNS_OBS              = FAILED for %s\n", cfg->NTRIP_CASTER);
            problems++;
        }
    }
    if (have_eph) {
        int rc = getaddrinfo(cfg->EPH_CASTER, NULL, &hints, &res);
        if (rc == 0) {
            char ip[INET_ADDRSTRLEN] = "";
            struct sockaddr_in *sa = (struct sockaddr_in *)res->ai_addr;
            inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip));
            printf("DNS_EPH              = %s (%s)\n", cfg->EPH_CASTER, ip);
            freeaddrinfo(res);
        } else {
            printf("DNS_EPH              = FAILED for %s\n", cfg->EPH_CASTER);
            problems++;
        }
    }

    printf("STATUS               = %s\n",
           problems == 0 ? "ok" : "issues detected");
    return problems == 0 ? EXIT_OK : EXIT_GENERIC;
}

/* ── Stop / abort flags ────────────────────────────────────────────── */
/* g_stop_requested  -- normal end (Ctrl-C); PNG is written before exit. */
/* g_abort_requested -- abort (Ctrl-A);     PNG is NOT written.           */
static volatile int g_stop_requested  = 0;
static volatile int g_abort_requested = 0;
static void on_sigint(int sig) { (void)sig; g_stop_requested = 1; }

/* Why did the --sky collection loop end?  Reported in the JSON summary. */
typedef enum {
    STOP_REASON_NONE,
    STOP_REASON_SIGINT,    /* Ctrl-C */
    STOP_REASON_ABORT,     /* Ctrl-A */
    STOP_REASON_DURATION,  /* --duration timer expired */
    STOP_REASON_EOF,       /* stdin EOF on --rtcm-stdin */
    STOP_REASON_ERROR      /* socket / read error */
} StopReason;
static const char *stop_reason_name(StopReason r) {
    switch (r) {
    case STOP_REASON_SIGINT:   return "sigint";
    case STOP_REASON_ABORT:    return "abort";
    case STOP_REASON_DURATION: return "duration";
    case STOP_REASON_EOF:      return "eof";
    case STOP_REASON_ERROR:    return "error";
    default:                   return "none";
    }
}

/* ── Raw-keyboard polling for Ctrl-A ───────────────────────────────────
 *
 * Cross-platform: on Windows we can poll _kbhit/_getch directly without
 * touching the console mode; on POSIX we need to put the terminal into
 * non-canonical, no-echo mode and select() with a zero timeout. */
#ifdef _WIN32
static void terminal_setup(void)   { /* no-op */ }
static void terminal_restore(void) { /* no-op */ }
static int  poll_for_ctrl_a(void)
{
    while (_kbhit()) {
        int c = _getch();
        if (c == 1) return 1;   /* Ctrl-A in cooked ASCII = SOH (0x01) */
        /* swallow any other keypress so the console doesn't queue them */
    }
    return 0;
}
#else
static struct termios g_old_tio;
static int            g_tio_saved = 0;
static void terminal_setup(void)
{
    if (!isatty(STDIN_FILENO)) return;
    if (tcgetattr(STDIN_FILENO, &g_old_tio) == 0) {
        struct termios newt = g_old_tio;
        newt.c_lflag &= ~(ICANON | ECHO);
        newt.c_cc[VMIN]  = 0;
        newt.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &newt) == 0)
            g_tio_saved = 1;
    }
}
static void terminal_restore(void)
{
    if (g_tio_saved) tcsetattr(STDIN_FILENO, TCSANOW, &g_old_tio);
    g_tio_saved = 0;
}
static int poll_for_ctrl_a(void)
{
    fd_set rs;
    FD_ZERO(&rs);
    FD_SET(STDIN_FILENO, &rs);
    struct timeval tv = { 0, 0 };
    if (select(STDIN_FILENO + 1, &rs, NULL, NULL, &tv) > 0) {
        char c;
        if (read(STDIN_FILENO, &c, 1) == 1 && c == 1)
            return 1;
    }
    return 0;
}
#endif

/* ── Thread wrapper for the eph NTRIP worker ───────────────────────── */
typedef struct {
    const NTRIP_Config *config;
    bool verbose;
} EphThreadArgs;

#ifdef _WIN32
static unsigned __stdcall eph_thread_entry(void *p)
{
    EphThreadArgs *a = (EphThreadArgs *)p;
    run_eph_stream(a->config, &g_stop_requested, a->verbose);
    return 0;
}
#else
static void *eph_thread_entry(void *p)
{
    EphThreadArgs *a = (EphThreadArgs *)p;
    run_eph_stream(a->config, &g_stop_requested, a->verbose);
    return NULL;
}
#endif

/* ── Sky-mode: read obs RTCM from stdin (--rtcm-stdin) ───────────────
 * Mirrors run_sky_obs_stream() but reads from stdin instead of a
 * socket.  Auto-stops at EOF (reason=eof); Ctrl-C and Ctrl-A behave
 * the same.  Useful for offline replay of a captured .rtcm3 file:
 *     ./ntripanalyse --sky --rtcm-stdin -R brdc.rnx < capture.rtcm3
 */
static int run_sky_stdin_stream(const NTRIP_Config *config,
                                SkyRenderSector *sectors,
                                int duration_s,
                                bool verbose,
                                StopReason *reason)
{
    (void)verbose;

#ifdef _WIN32
    /* Avoid CRLF mangling on Windows. */
    _setmode(_fileno(stdin), _O_BINARY);
    bool stderr_is_tty = _isatty(_fileno(stderr)) != 0;
#else
    bool stderr_is_tty = isatty(fileno(stderr)) != 0;
#endif
    bool show_progress = !quiet && !no_progress;

    time_t t_start = time(NULL);

    INFO("[OBS] Reading RTCM from stdin\n");
    if (duration_s > 0)
        INFO("Collecting heatmap data for %d s (Ctrl-C to save early, Ctrl-A to abort)\n",
             duration_s);
    else
        INFO("Collecting heatmap data: Ctrl-C to save PNG and exit, Ctrl-A to abort without saving\n");
    if (json_output)
        fprintf(stderr,
                "{\"event\":\"start\",\"source\":\"stdin\",\"t\":%ld,\"duration_s\":%d}\n",
                (long)t_start, duration_s);

    terminal_setup();

    /* Same mute as run_sky_obs_stream: silence per-frame 1005/1006 chatter
     * (rtcm_printf writes) unless the user passed -v. */
    RtcmStrBuf sink;
    int        sink_used = 0;
    if (!verbose) {
        rtcm_strbuf_init(&sink, 8192);
        rtcm_set_output_buffer(&sink);
        sink_used = 1;
    }

    unsigned char msg_buffer[BUFFER_SIZE];
    int  msg_buffer_len = 0;
    char buffer[BUFFER_SIZE];

    long  msm_total     = 0;
    long  obs_total     = 0;
    long  frame_total   = 0;
    long  bytes_total   = 0;
    long  bytes_at_tick = 0;
    time_t last_tick    = t_start;
    const char spin[] = "|/-\\";
    int spin_i = 0;

    while (!g_stop_requested && !g_abort_requested) {
        size_t got = fread(buffer, 1, sizeof(buffer), stdin);
        if (got == 0) {
            if (feof(stdin)) {
                *reason = STOP_REASON_EOF;
                INFO("\n[OBS] stdin EOF\n");
                break;
            }
            if (ferror(stdin)) {
                *reason = STOP_REASON_ERROR;
                break;
            }
            continue;
        }
        int received = (int)got;

        if (poll_for_ctrl_a()) {
            g_abort_requested = 1;
            *reason = STOP_REASON_ABORT;
            break;
        }

        time_t now = time(NULL);
        if (duration_s > 0 && difftime(now, t_start) >= (double)duration_s) {
            INFO("\n[OBS] Reached --duration %d s\n", duration_s);
            *reason = STOP_REASON_DURATION;
            break;
        }

        bytes_total += received;

        if (show_progress && now != last_tick) {
            long bytes_in_window = bytes_total - bytes_at_tick;
            bytes_at_tick = bytes_total;
            double kbps = (double)bytes_in_window * 8.0 / 1000.0;
            if (json_output) {
                fprintf(stderr,
                    "{\"event\":\"tick\",\"t\":%ld,\"frames\":%ld,\"msm\":%ld,"
                    "\"upd\":%ld,\"kbps\":%.2f,\"total_kb\":%ld,\"source\":\"stdin\"}\n",
                    (long)now, frame_total, msm_total, obs_total,
                    kbps, bytes_total / 1024);
            } else if (stderr_is_tty) {
                fprintf(stderr,
                    "\r [%c] stdin frames=%ld  MSM=%ld  upd=%ld  rate=%6.1f kbit/s  total=%ld KB    ",
                    spin[spin_i & 3], frame_total, msm_total, obs_total,
                    kbps, bytes_total / 1024);
            } else {
                fprintf(stderr,
                    "[%c] stdin frames=%ld  MSM=%ld  upd=%ld  rate=%6.1f kbit/s  total=%ld KB\n",
                    spin[spin_i & 3], frame_total, msm_total, obs_total,
                    kbps, bytes_total / 1024);
            }
            fflush(stderr);
            spin_i++;
            last_tick = now;
        }

        /* Same frame-parsing pipeline as run_sky_obs_stream. */
        int buf_pos = 0;
        while (buf_pos < received) {
            if (msg_buffer_len == 0) {
                while (buf_pos < received &&
                       (unsigned char)buffer[buf_pos] != 0xD3) buf_pos++;
                if (buf_pos >= received) break;
            }
            int to_copy = received - buf_pos;
            if (msg_buffer_len + to_copy > BUFFER_SIZE)
                to_copy = BUFFER_SIZE - msg_buffer_len;
            memcpy(msg_buffer + msg_buffer_len, buffer + buf_pos, to_copy);
            msg_buffer_len += to_copy;
            buf_pos += to_copy;

            while (msg_buffer_len >= 3) {
                if (msg_buffer[0] != 0xD3) {
                    memmove(msg_buffer, msg_buffer + 1, --msg_buffer_len);
                    continue;
                }
                int msg_length = ((msg_buffer[1] & 0x03) << 8) | msg_buffer[2];
                int frame_len  = 3 + msg_length + 3;
                if (msg_buffer_len < frame_len) break;
                if (msg_length < 2) {
                    memmove(msg_buffer, msg_buffer + 1, --msg_buffer_len);
                    continue;
                }
                int mt = ((int)msg_buffer[3] << 4) | ((int)msg_buffer[4] >> 4);
                frame_total++;

                if (mt == 1005) {
                    decode_rtcm_1005(&msg_buffer[3], msg_length, config);
                } else if (mt == 1006) {
                    decode_rtcm_1006(&msg_buffer[3], msg_length, config);
                }

                int subtype = mt % 10;
                if (mt >= 1070 && mt <= 1139 && subtype >= 4 && subtype <= 7) {
                    msm_total++;
                    bool   arp_valid = false;
                    double sx = 0, sy = 0, sz = 0;
                    rtcm_get_station_arp(&arp_valid, &sx, &sy, &sz, NULL, NULL, NULL);
                    if (!arp_valid &&
                        (config->LATITUDE != 0.0 || config->LONGITUDE != 0.0)) {
                        geodetic_to_ecef(config->LATITUDE, config->LONGITUDE, 0.0,
                                         &sx, &sy, &sz);
                        arp_valid = true;
                    }
                    if (arp_valid) {
                        int contrib = sky_collect_feed_msm(
                            sectors, &msg_buffer[3], msg_length, mt,
                            sx, sy, sz);
                        obs_total += contrib;
                    }
                }

                memmove(msg_buffer, msg_buffer + frame_len,
                        msg_buffer_len - frame_len);
                msg_buffer_len -= frame_len;
            }
        }
    }

    if (*reason == STOP_REASON_NONE) {
        if (g_abort_requested)     *reason = STOP_REASON_ABORT;
        else if (g_stop_requested) *reason = STOP_REASON_SIGINT;
    }

    if (show_progress && stderr_is_tty && !json_output) INFO("\n");
    INFO("[OBS] stdin closed (frames=%ld  MSM=%ld  sector updates=%ld  total=%ld KB)\n",
         frame_total, msm_total, obs_total, bytes_total / 1024);
    terminal_restore();
    if (sink_used) {
        rtcm_set_output_buffer(NULL);
        rtcm_strbuf_free(&sink);
    }
    return 0;
}

/* ── Sky-mode: obs stream with on-the-fly sector accumulation ──────── */
/* Returns 0 on clean stop, non-zero on connection failure.  Sets *reason
 * to indicate why the loop exited (used by JSON summary). */
static int run_sky_obs_stream(const NTRIP_Config *config,
                              SkyRenderSector *sectors,
                              int duration_s,
                              bool verbose,
                              StopReason *reason)
{
#ifdef _WIN32
    typedef SOCKET sock_t;
#define SK_CLOSE closesocket
#define SK_INVALID INVALID_SOCKET
#else
    typedef int sock_t;
#define SK_CLOSE close
#define SK_INVALID -1
#endif

    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(config->NTRIP_CASTER, NULL, &hints, &result) != 0) {
        fprintf(stderr, "[OBS] DNS lookup failed for %s\n", config->NTRIP_CASTER);
        return 1;
    }

    sock_t sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == SK_INVALID) {
        fprintf(stderr, "[OBS] Socket creation failed\n");
        freeaddrinfo(result);
        return 1;
    }

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port   = htons(config->NTRIP_PORT);
    server.sin_addr   = ((struct sockaddr_in *)result->ai_addr)->sin_addr;
    memset(&(server.sin_zero), 0, 8);
    freeaddrinfo(result);

    if (connect(sock, (struct sockaddr *)&server, sizeof(struct sockaddr)) < 0) {
        fprintf(stderr, "[OBS] Connect failed to %s:%d\n",
                config->NTRIP_CASTER, config->NTRIP_PORT);
        SK_CLOSE(sock);
        return 1;
    }

    char request[1024];
    snprintf(request, sizeof(request),
             "GET /%s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Ntrip-Version: Ntrip/2.0\r\n"
             "User-Agent: NTRIP CClient/1.0\r\n"
             "Authorization: Basic %s\r\n"
             "\r\n",
             config->MOUNTPOINT, config->NTRIP_CASTER, config->AUTH_BASIC);
    send(sock, request, strlen(request), 0);

    /* Optional GGA push (some casters require it before sending data). */
    char gga[100];
    create_gngga_sentence(config->LATITUDE, config->LONGITUDE, gga);
    char gga_with_crlf[104];
    snprintf(gga_with_crlf, sizeof(gga_with_crlf), "%s\r\n", gga);
    time_t last_gga_time = time(NULL);

    /* Short recv timeout so the SIGINT flag is polled. */
#ifdef _WIN32
    DWORD tv_ms = 500;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv_ms, sizeof(tv_ms));
#else
    struct timeval tv = { 0, 500000 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
#endif

    INFO("[OBS] Connected to %s:%d /%s\n",
         config->NTRIP_CASTER, config->NTRIP_PORT, config->MOUNTPOINT);
    if (duration_s > 0)
        INFO("Collecting heatmap data for %d s (Ctrl-C to save early, Ctrl-A to abort without saving)\n",
             duration_s);
    else
        INFO("Collecting heatmap data: Ctrl-C to save PNG and exit, Ctrl-A to abort without saving\n");
    if (json_output)
        fprintf(stderr,
                "{\"event\":\"start\",\"source\":\"ntrip\",\"caster\":\"%s\",\"port\":%d,"
                "\"mountpoint\":\"%s\",\"t\":%ld,\"duration_s\":%d}\n",
                config->NTRIP_CASTER, config->NTRIP_PORT, config->MOUNTPOINT,
                (long)time(NULL), duration_s);
    fflush(stdout);

    /* TTY detection on stderr (status line writes there now so stdout
     * stays reserved for data).  When stderr is a pipe / file, the
     * carriage-return spinner becomes garbage -- switch to one-line-
     * per-tick.  --quiet and --no-progress suppress the line entirely. */
#ifdef _WIN32
    bool stderr_is_tty = _isatty(_fileno(stderr)) != 0;
#else
    bool stderr_is_tty = isatty(fileno(stderr)) != 0;
#endif
    bool show_progress = !quiet && !no_progress;

    /* Mute the per-frame decode chatter (RTCM 1005/1006 reference-station
     * blocks) unless -v is on.  The user's own [OBS] / spinner output
     * uses printf directly, so it stays visible either way. */
    RtcmStrBuf sink;
    int        sink_used = 0;
    if (!verbose) {
        rtcm_strbuf_init(&sink, 8192);
        rtcm_set_output_buffer(&sink);
        sink_used = 1;
    }

    int header_skipped = 0;
    unsigned char msg_buffer[BUFFER_SIZE];
    int msg_buffer_len = 0;
    char buffer[BUFFER_SIZE];

    long  msm_total      = 0;
    long  obs_total      = 0;
    long  frame_total    = 0;
    long  bytes_total    = 0;       /* lifetime payload bytes (after HTTP header) */
    long  bytes_at_tick  = 0;       /* snapshot at the start of the current second */
    time_t t_start       = time(NULL);
    time_t last_tick     = t_start;
    const char spin[] = "|/-\\";
    int spin_i = 0;

    /* Switch the controlling terminal into a non-canonical mode so we can
     * poll for Ctrl-A without waiting for the user to press Enter.  Always
     * call terminal_restore() before returning so the user's shell isn't
     * left with echo off. */
    terminal_setup();

    while (!g_stop_requested && !g_abort_requested) {
        int received = recv(sock, buffer, sizeof(buffer), 0);
        if (received == 0) {
            fprintf(stderr, "\n[OBS] Caster closed the connection\n");
            *reason = STOP_REASON_EOF;
            break;
        }
        if (received < 0) {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT) { /* fall through to housekeeping */ }
            else { *reason = STOP_REASON_ERROR; break; }
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                /* fall through */
            } else { *reason = STOP_REASON_ERROR; break; }
#endif
            received = 0;
        }

        /* Poll for Ctrl-A on every iteration -- raw mode means the
         * keystroke is available immediately, with no Enter required. */
        if (poll_for_ctrl_a()) {
            g_abort_requested = 1;
            *reason = STOP_REASON_ABORT;
            break;
        }

        time_t now = time(NULL);

        /* --duration: auto-stop after N seconds and save normally. */
        if (duration_s > 0 && difftime(now, t_start) >= (double)duration_s) {
            INFO("\n[OBS] Reached --duration %d s\n", duration_s);
            *reason = STOP_REASON_DURATION;
            break;
        }

        /* Keep-alive GGA every 5 s. */
        if (now - last_gga_time >= 5) {
            send(sock, gga_with_crlf, strlen(gga_with_crlf), 0);
            last_gga_time = now;
        }

        if (received > 0) bytes_total += received;

        /* Heartbeat: spinner + counters + bps.  When stdout is a TTY use
         * a carriage-return so the line refreshes in place; otherwise
         * print a fresh line per tick so a pipe / log file stays clean.
         * --quiet and --no-progress suppress it entirely. */
        if (show_progress && now != last_tick) {
            long bytes_in_window = bytes_total - bytes_at_tick;
            bytes_at_tick = bytes_total;

            double kbps = (double)bytes_in_window * 8.0 / 1000.0;
            /* Status goes to stderr so stdout stays clean for data
             * (the saved PNG filename).  --json switches to one JSON
             * object per tick (newline-delimited, parseable). */
            if (json_output) {
                fprintf(stderr,
                    "{\"event\":\"tick\",\"t\":%ld,\"frames\":%ld,\"msm\":%ld,"
                    "\"upd\":%ld,\"kbps\":%.2f,\"total_kb\":%ld}\n",
                    (long)now, frame_total, msm_total, obs_total,
                    kbps, bytes_total / 1024);
            } else if (stderr_is_tty) {
                fprintf(stderr,
                    "\r [%c] frames=%ld  MSM=%ld  obs+exp updates=%ld  rate=%6.1f kbit/s  total=%ld KB    ",
                    spin[spin_i & 3], frame_total, msm_total, obs_total,
                    kbps, bytes_total / 1024);
            } else {
                fprintf(stderr,
                    "[%c] frames=%ld  MSM=%ld  obs+exp updates=%ld  rate=%6.1f kbit/s  total=%ld KB\n",
                    spin[spin_i & 3], frame_total, msm_total, obs_total,
                    kbps, bytes_total / 1024);
            }
            fflush(stderr);
            spin_i++;
            last_tick = now;
        }

        if (received == 0) continue;

        if (!header_skipped) {
            buffer[received] = '\0';
            char *ptr = strstr(buffer, "\r\n\r\n");
            if (ptr) {
                int offset = (ptr - buffer) + 4;
                memmove(buffer, buffer + offset, received - offset);
                received -= offset;
                header_skipped = 1;
            } else {
                continue;
            }
        }

        int buf_pos = 0;
        while (buf_pos < received) {
            if (msg_buffer_len == 0) {
                while (buf_pos < received &&
                       (unsigned char)buffer[buf_pos] != 0xD3) buf_pos++;
                if (buf_pos >= received) break;
            }
            int to_copy = received - buf_pos;
            if (msg_buffer_len + to_copy > BUFFER_SIZE)
                to_copy = BUFFER_SIZE - msg_buffer_len;
            memcpy(msg_buffer + msg_buffer_len, buffer + buf_pos, to_copy);
            msg_buffer_len += to_copy;
            buf_pos += to_copy;

            while (msg_buffer_len >= 3) {
                if (msg_buffer[0] != 0xD3) {
                    memmove(msg_buffer, msg_buffer + 1, --msg_buffer_len);
                    continue;
                }
                int msg_length = ((msg_buffer[1] & 0x03) << 8) | msg_buffer[2];
                int frame_len  = 3 + msg_length + 3;
                if (msg_buffer_len < frame_len) break;
                if (msg_length < 2) {
                    memmove(msg_buffer, msg_buffer + 1, --msg_buffer_len);
                    continue;
                }
                int mt = ((int)msg_buffer[3] << 4) | ((int)msg_buffer[4] >> 4);

                frame_total++;

                /* Reset the discard sink between frames so it can't grow
                 * unbounded if we're running for hours. */
                if (sink_used) rtcm_strbuf_clear(&sink);

                /* Decode 1005/1006 so the station ARP gets cached for
                 * azel_from_ecef.  Don't decode MSM frames -- we only need
                 * the sat-mask + sectorisation, which sky_collect handles. */
                if (mt == 1005) {
                    decode_rtcm_1005(&msg_buffer[3], msg_length, config);
                } else if (mt == 1006) {
                    decode_rtcm_1006(&msg_buffer[3], msg_length, config);
                }

                int subtype = mt % 10;
                if (mt >= 1070 && mt <= 1139 && subtype >= 4 && subtype <= 7) {
                    msm_total++;

                    /* Get current ARP -- prefer cached 1005/1006, fall back
                     * to the configured rover lat/lon at altitude 0. */
                    bool   arp_valid = false;
                    double sx = 0, sy = 0, sz = 0;
                    rtcm_get_station_arp(&arp_valid, &sx, &sy, &sz,
                                         NULL, NULL, NULL);
                    if (!arp_valid &&
                        (config->LATITUDE != 0.0 || config->LONGITUDE != 0.0)) {
                        geodetic_to_ecef(config->LATITUDE, config->LONGITUDE,
                                         0.0, &sx, &sy, &sz);
                        arp_valid = true;
                    }

                    if (arp_valid) {
                        int contrib = sky_collect_feed_msm(
                            sectors, &msg_buffer[3], msg_length, mt,
                            sx, sy, sz);
                        obs_total += contrib;
                    }
                }

                memmove(msg_buffer, msg_buffer + frame_len,
                        msg_buffer_len - frame_len);
                msg_buffer_len -= frame_len;
            }
        }
    }

    /* If the loop ended via the while-condition (not an explicit break)
     * the reason is either SIGINT or the abort flag flipped from outside;
     * fall back to those if no break-path stamped a reason. */
    if (*reason == STOP_REASON_NONE) {
        if (g_abort_requested)       *reason = STOP_REASON_ABORT;
        else if (g_stop_requested)   *reason = STOP_REASON_SIGINT;
    }

    /* Final newline so subsequent output starts on a clean row.
     * Only needed when the TTY spinner left the cursor mid-line. */
    if (show_progress && stderr_is_tty && !json_output) INFO("\n");
    INFO("[OBS] Stream stopped (frames=%ld  MSM=%ld  sector updates=%ld  total=%ld KB)\n",
         frame_total, msm_total, obs_total, bytes_total / 1024);
    if (g_abort_requested)
        INFO("[OBS] Aborted by Ctrl-A; PNG will NOT be written.\n");
    fflush(stdout);

    terminal_restore();

    if (sink_used) {
        rtcm_set_output_buffer(NULL);
        rtcm_strbuf_free(&sink);
    }
    SK_CLOSE(sock);
    return 0;

#undef SK_CLOSE
#undef SK_INVALID
}

/* ── Sky-mode entry point ──────────────────────────────────────────── */
static int run_sky_mode(NTRIP_Config *config,
                        const char *rinex_path,
                        const char *output_path,
                        int duration_s,
                        bool verbose)
{
    /* Pre-flight: need an ephemeris source -- RINEX file OR EPH stream. */
    bool have_eph_stream =
        config->EPH_CASTER[0] && config->EPH_PORT > 0 && config->EPH_MOUNTPOINT[0];
    bool have_rinex = (rinex_path && rinex_path[0]);

    if (!have_eph_stream && !have_rinex) {
        ERR("[ERROR] --sky requires an ephemeris source.\n"
            "        Either configure an EPH_CASTER / EPH_PORT / EPH_MOUNTPOINT\n"
            "        in the config file, or pass a RINEX 3 NAV file with -R/--RINEX.\n");
        return EXIT_NO_EPH;
    }

    /* Prepare EPH HTTP Basic auth if we have an eph stream. */
    if (have_eph_stream) {
        char auth[512];
        snprintf(auth, sizeof(auth), "%s:%s",
                 config->EPH_USERNAME, config->EPH_PASSWORD);
        base64_encode(auth, config->EPH_AUTH_BASIC);
    }

    /* SIGINT handler. */
    signal(SIGINT, on_sigint);
#ifdef SIGTERM
    signal(SIGTERM, on_sigint);
#endif

    /* Allocate the sector grid (flat array, indexed band*MAX_AZ_BINS+bin). */
    SkyRenderSector *sectors = (SkyRenderSector *)calloc(
        (size_t)SKY_RENDER_N_EL_BANDS * SKY_RENDER_MAX_AZ_BINS,
        sizeof(SkyRenderSector));
    if (!sectors) {
        ERR("[ERROR] Out of memory allocating sector grid\n");
        return EXIT_GENERIC;
    }

    /* Stage 1: load RINEX (if any).  Always runs first so the eph cache
     * has something useful before the first obs frame arrives.  The eph
     * stream will then add to / refresh the cache as broadcast updates
     * come in. */
    if (have_rinex) {
        int counts[RINEX_NAV_MAX_GNSS] = { 0 };
        INFO("[RINEX] Loading %s ...\n", rinex_path);
        int total = rinex_nav_load(rinex_path, counts);
        if (total < 0) {
            ERR("[ERROR] Could not read RINEX file: %s\n", rinex_path);
            free(sectors);
            return EXIT_GENERIC;
        }
        INFO("[RINEX] Loaded %d ephemerides "
             "(G:%d R:%d E:%d J:%d C:%d I:%d)\n",
             total, counts[1], counts[2], counts[3], counts[4],
             counts[5], counts[7]);
    }

    /* Stage 2: spawn the EPH worker thread (if configured). */
    EphThreadArgs eph_args;
    eph_args.config  = config;
    eph_args.verbose = verbose;
#ifdef _WIN32
    HANDLE hEphThread = NULL;
#else
    pthread_t eph_thread;
    int eph_thread_started = 0;
#endif
    if (have_eph_stream) {
#ifdef _WIN32
        hEphThread = (HANDLE)_beginthreadex(NULL, 0, eph_thread_entry,
                                            &eph_args, 0, NULL);
        if (!hEphThread) {
            ERR("[ERROR] Could not start EPH worker thread\n");
        }
#else
        if (pthread_create(&eph_thread, NULL, eph_thread_entry, &eph_args) == 0)
            eph_thread_started = 1;
        else
            ERR("[ERROR] pthread_create for EPH worker failed\n");
#endif
    }

    /* Stage 3: drive the obs source until SIGINT / Ctrl-A / timeout / EOF.
     * Source is either the obs NTRIP stream (default) or stdin if the
     * user passed --rtcm-stdin (offline replay of a captured .rtcm3). */
    StopReason stop_reason = STOP_REASON_NONE;
    if (rtcm_stdin)
        run_sky_stdin_stream(config, sectors, duration_s, verbose, &stop_reason);
    else
        run_sky_obs_stream(config, sectors, duration_s, verbose, &stop_reason);

    /* Stage 4: wait for the eph thread to finish (it polls the same flag).
     * Set BOTH flags so an abort also stops the eph stream cleanly -- the
     * eph worker only checks g_stop_requested.  Ctrl-A pressed in the obs
     * loop must propagate here too. */
    g_stop_requested = 1;
#ifdef _WIN32
    if (hEphThread) {
        WaitForSingleObject(hEphThread, 3000);
        CloseHandle(hEphThread);
    }
#else
    if (eph_thread_started) {
        pthread_join(eph_thread, NULL);
    }
#endif

    /* Stage 5: render the snapshot PNG -- unless the user aborted. */
    if (g_abort_requested) {
        INFO("[SAVE] Skipped (aborted)\n");
        if (json_output) {
            fprintf(stderr,
                "{\"event\":\"stop\",\"reason\":\"%s\",\"saved\":null}\n",
                stop_reason_name(stop_reason));
        }
        free(sectors);
        return EXIT_ABORTED;
    }
    /* Pick the output path: --output wins; otherwise default to the
     * timestamped name (same convention as the GUI snapshot). */
    char filename_buf[260];
    const char *filename;
    if (output_path && output_path[0]) {
        filename = output_path;
    } else {
        time_t now_t = time(NULL);
        struct tm *lt = localtime(&now_t);
        char ts[16] = "00000000000000";
        if (lt) strftime(ts, sizeof(ts), "%Y%m%d%H%M%S", lt);
        snprintf(filename_buf, sizeof(filename_buf), "%s_ARP-EPG.png", ts);
        filename = filename_buf;
    }

    /* Pull final ARP for the footer. */
    bool   arp_valid = false;
    double lat = 0, lon = 0, alt = 0;
    rtcm_get_station_arp(&arp_valid, NULL, NULL, NULL, &lat, &lon, &alt);
    if (!arp_valid) {
        lat = config->LATITUDE;
        lon = config->LONGITUDE;
        alt = 0.0;
        if (lat != 0.0 || lon != 0.0) arp_valid = true;
    }

    /* UTC label */
    char utc_label[40] = "";
    {
        time_t now_t = time(NULL);
        struct tm *gt = gmtime(&now_t);
        if (gt) strftime(utc_label, sizeof(utc_label),
                         "%Y-%m-%d %H:%M:%S UTC", gt);
    }

    INFO("[SAVE] Writing %s ...\n", filename);
    bool ok = sky_render_heatmap_png(filename, sectors,
                                     800, 800,
                                     arp_valid, lat, lon, alt,
                                     config->MOUNTPOINT, utc_label);
    if (!ok) {
        ERR("[ERROR] Failed to write %s\n", filename);
        free(sectors);
        return EXIT_GENERIC;
    }
    INFO("[SAVE] OK\n");

    /* Machine-readable summary line for --json consumers. */
    if (json_output) {
        fprintf(stderr,
            "{\"event\":\"stop\",\"reason\":\"%s\",\"saved\":\"%s\"}\n",
            stop_reason_name(stop_reason), filename);
    }

    /* Script-friendly: print the saved path on its own line to stdout.
     * Scripts can capture it with $(./ntripanalyse --sky --duration 60 -q). */
    printf("%s\n", filename);
    fflush(stdout);

    free(sectors);
    return EXIT_OK;
}

int main(int argc, char *argv[]) {
    NTRIP_Config config;
    const char *config_filename = "config.json";
    const char *rinex_path = NULL;
    const char *output_path = NULL;     /* -o / --output for --sky */
    int duration_s = 0;                 /* --duration: auto-stop sky mode */
    bool check_config_only = false;     /* --check-config: dry-run validation */
    ConfigOverrides ov = { 0 };         /* per-field CLI overrides */
    int opt;
    int analysis_time = 60; // default to 60 seconds
    Operation operation = OP_NONE;

    static struct option long_options[] = {
        {"config",      optional_argument, 0, 'c'},
        {"types",       optional_argument, 0, 't'},
        {"mounts",      no_argument,       0, 'm'},
        {"decode",      optional_argument, 0, 'd'},
        {"sat",         optional_argument, 0, 's'},
        {"sky",         no_argument,       0, 'S'},
        {"RINEX",       required_argument, 0, 'R'},
        {"rinex",       required_argument, 0, 'R'},
        {"raw",         no_argument,       0, 'r'},
        {"latitude",    required_argument, 0,  1 },
        {"lat",         required_argument, 0,  2 },
        {"longitude",   required_argument, 0,  3 },
        {"lon",         required_argument, 0,  4 },
        {"verbose",     no_argument,       0, 'v'},
        {"quiet",       no_argument,       0, 'q'},
        {"output",      required_argument, 0, 'o'},
        {"duration",    required_argument, 0,  5 },
        {"no-progress", no_argument,       0,  6 },
        {"version",     no_argument,       0,  7 },
        /* Per-field config overrides (CLI > env > file) */
        {"caster",         required_argument, 0,  8 },
        {"port",           required_argument, 0,  9 },
        {"mountpoint",     required_argument, 0, 10 },
        {"user",           required_argument, 0, 11 },
        {"password",       required_argument, 0, 12 },
        {"eph-caster",     required_argument, 0, 13 },
        {"eph-port",       required_argument, 0, 14 },
        {"eph-mountpoint", required_argument, 0, 15 },
        {"eph-user",       required_argument, 0, 16 },
        {"eph-password",   required_argument, 0, 17 },
        {"check-config",   no_argument,       0, 18 },
        {"json",           no_argument,       0, 19 },
        {"rtcm-stdin",     no_argument,       0, 20 },
        {"generate",    no_argument,       0, 'g'},
        {"info",        no_argument,       0, 'i'},
        {"help",        no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "c::t::md::vqs::SgirR:o:h",
                              long_options, &option_index)) != -1) {
        switch (opt) {
            case 'c':
                if (optarg) {
                    config_filename = optarg;
                } else if (optind < argc && argv[optind] && argv[optind][0] != '-') {
                    config_filename = argv[optind];
                    optind++;
                } else {
                    config_filename = "config.json";
                }
                break;
            case 't':
                claim_action(&operation, OP_ANALYZE_TYPES, "-t / --time");
                if (optarg) {
                    analysis_time = atoi(optarg);
                    if (analysis_time <= 0) analysis_time = 60;
                } else if (optind < argc && argv[optind] && argv[optind][0] != '-') {
                    analysis_time = atoi(argv[optind]);
                    if (analysis_time <= 0) analysis_time = 60;
                    optind++;
                } else {
                    analysis_time = 60;
                }
                break;
            case 'm':
                claim_action(&operation, OP_SHOW_MOUNT_FORMATTED, "-m / --mounts");
                break;
            case 'r':
                claim_action(&operation, OP_SHOW_MOUNT_RAW, "-r / --raw");
                break;
            case 'd':
                claim_action(&operation, OP_DECODE_STREAM, "-d / --decode");
                if (optarg) {
                    char *token = strtok(optarg, ", ");
                    while (token && filter_count < MAX_MSG_TYPES) {
                        filter_list[filter_count++] = atoi(token);
                        token = strtok(NULL, ", ");
                    }
                } else if (optind < argc && argv[optind] && argv[optind][0] != '-') {
                    char *token = strtok(argv[optind], ", ");
                    while (token && filter_count < MAX_MSG_TYPES) {
                        filter_list[filter_count++] = atoi(token);
                        token = strtok(NULL, ", ");
                    }
                    optind++;
                }
                break;
            case 's':   /* -s / --sat (original satellite-analysis mode) */
                claim_action(&operation, OP_ANALYZE_SATS, "-s / --sat");
                if (optarg) {
                    analysis_time = atoi(optarg);
                    if (analysis_time <= 0) analysis_time = 60;
                } else if (optind < argc && argv[optind] && argv[optind][0] != '-') {
                    analysis_time = atoi(argv[optind]);
                    if (analysis_time <= 0) analysis_time = 60;
                    optind++;
                } else {
                    analysis_time = 60;
                }
                break;
            case 'S':   /* -S / --sky */
                claim_action(&operation, OP_SKY_HEATMAP, "-S / --sky");
                break;
            case 'R':   /* -R / --RINEX path */
                rinex_path = optarg;
                break;
            case 1: // --latitude
            case 2: // --lat
                if (optarg) config.LATITUDE = atof(optarg);
                break;
            case 3: // --longitude
            case 4: // --lon
                if (optarg) config.LONGITUDE = atof(optarg);
                break;
            case 'v':
                verbose = true;
                break;
            case 'q':       /* -q / --quiet */
                quiet = true;
                break;
            case 'o':       /* -o / --output PATH */
                output_path = optarg;
                break;
            case 5:         /* --duration SECONDS */
                if (optarg) duration_s = atoi(optarg);
                if (duration_s < 0) duration_s = 0;
                break;
            case 6:         /* --no-progress */
                no_progress = true;
                break;
            case 7:         /* --version */
                printf("ntrip-analyser %s\n", NTRIP_ANALYSER_VERSION);
                return EXIT_OK;
            case 8:  ov.caster         = optarg; break;   /* --caster */
            case 9:  ov.port           = optarg; break;   /* --port */
            case 10: ov.mountpoint     = optarg; break;   /* --mountpoint */
            case 11: ov.user           = optarg; break;   /* --user */
            case 12: ov.password       = optarg; break;   /* --password */
            case 13: ov.eph_caster     = optarg; break;   /* --eph-caster */
            case 14: ov.eph_port       = optarg; break;   /* --eph-port */
            case 15: ov.eph_mountpoint = optarg; break;   /* --eph-mountpoint */
            case 16: ov.eph_user       = optarg; break;   /* --eph-user */
            case 17: ov.eph_password   = optarg; break;   /* --eph-password */
            case 18: check_config_only = true;   break;   /* --check-config */
            case 19: json_output       = true;   break;   /* --json */
            case 20: rtcm_stdin        = true;   break;   /* --rtcm-stdin */
            case 'g':
                initialize_config("config.json");
                return EXIT_OK;
            case 'i':
                print_program_info();
                return EXIT_OK;
            case 'h':
                print_help(argv[0]);
                return EXIT_OK;
            default:
                print_help(argv[0]);
                return EXIT_BAD_ARGS;
        }
    }

    // If no arguments provided, force verbose to show "No action specified"
    if (argc == 1) {
        verbose = true;
    }

    /* Reject combinations of action flags (e.g. `-d -S`).  --raw is allowed
     * to combine with -m (it modifies the mount-list format), and the
     * -d/--decode action also tolerates -m setting the verb first since
     * the historical CLI lets `-m -d` chain the two.  Everything else
     * is rejected so the user knows their request was ambiguous. */
    if (action_seen_second) {
        ERR("[ERROR] Cannot combine action flags: %s and %s\n"
            "        Pick one action per invocation.\n",
            action_seen_first, action_seen_second);
        return EXIT_BAD_ARGS;
    }

    if (load_config(config_filename, &config) != 0) {
        ERR("[ERROR] Could not open or parse config file: %s\n", config_filename);
        ERR("Aborting.\n");
        return EXIT_CONFIG_ERROR;
    }

    /* Precedence: CLI overrides > env vars > config file.
     * Fill env-var slots first, then write any non-NULL override into the
     * loaded config so the rest of main() / runtime sees the final values. */
    overrides_apply_env(&ov);
    overrides_apply_to_config(&config, &ov);

#ifdef _WIN32   // === Windows-specific: Initialize Winsock ===
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        ERR("[ERROR] WSAStartup failed.\n");
        return EXIT_GENERIC;
    }
#endif

    char auth[512];
    snprintf(auth, sizeof(auth), "%s:%s", config.USERNAME, config.PASSWORD);
    base64_encode(auth, config.AUTH_BASIC);

    // === --check-config: dry-run validation, then exit ===
    if (check_config_only) {
        int rc = run_check_config(&config);
#ifdef _WIN32
        WSACleanup();
#endif
        return rc;
    }

    // === Verbose reporting ===
    if (verbose) {
        print_verbose_config(
            &config,
            config_filename,
            operation
        );
    }

    // === 0. Analyze message types if requested ===
    if (operation == OP_ANALYZE_TYPES) {
        analyze_message_types(&config, analysis_time);
#ifdef _WIN32
        WSACleanup();
#endif
        return 0;
    }

    // === 1. Request and display mountpoint list ===
    if (operation == OP_SHOW_MOUNT_FORMATTED || operation == OP_SHOW_MOUNT_RAW) {
        INFO("[DEBUG] Requesting mountpoint list (sourcetable)...\n");
        char *mount_table = receive_mount_table(&config);
        if (mount_table) {
            /* Sourcetable IS the data -- keep it on stdout. */
            if (operation == OP_SHOW_MOUNT_RAW) {
                printf("%s", mount_table);
            } else {
                printf("%s\n", mount_table);
            }
            free(mount_table);
        } else {
            ERR("[ERROR] Failed to retrieve mountpoint list.\n");
#ifdef _WIN32
            WSACleanup();
#endif
            return EXIT_GENERIC;
        }
        // If only -m or -r is set, exit after showing the table
        if (operation != OP_DECODE_STREAM) {
#ifdef _WIN32
            WSACleanup();
#endif
            return EXIT_OK;
        }
    }

    // === 2. Start NTRIP stream from configured mountpoint ===
    if (operation == OP_DECODE_STREAM) {
        INFO("[DEBUG] Starting NTRIP stream from mountpoint '%s'...\n", config.MOUNTPOINT);
        if (filter_count > 0) {
            INFO("[DEBUG] Filter list: ");
            for (int i = 0; i < filter_count; ++i) {
                if (!quiet) fprintf(stderr, "%d ", filter_list[i]);
            }
            INFO("\n");
        } else {
            INFO("[DEBUG] No filter: all message types will be shown.\n");
        }
        start_ntrip_stream_with_filter(&config, filter_list, filter_count, verbose);
#ifdef _WIN32
        WSACleanup();
#endif
        return 0;
    }

    if (operation == OP_ANALYZE_SATS) {
        analyze_satellites_stream(&config, analysis_time);
#ifdef _WIN32
        WSACleanup();
#endif
        return 0;
    }

    if (operation == OP_SKY_HEATMAP) {
        int rc = run_sky_mode(&config, rinex_path,
                              output_path, duration_s, verbose);
#ifdef _WIN32
        WSACleanup();
#endif
        return rc;
    }

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
