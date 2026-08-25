#include "net/ntrip_handler.h"
#include "net/ntrip_proto.h"   /* NsFailure, shared with the session */
#include "session/ns_transport.h"  /* the sourcetable fetch rides the
                                    * same transport as the stream */
#include "core/nmea_parser.h"
#include "core/rtcm3x_parser.h"
#include "core/version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
/* Platform headers for ns_failure_from_socket() alone: the WSAE and
 * errno numbers it reconciles.  Every socket this file once opened
 * itself now comes from ns_transport. */
#ifdef _WIN32
    #include <winsock2.h>
#else
    #include <errno.h>
#endif

/*
 * The two get_time_seconds() variants that stood here, and the sketch
 * below them of how the -t option might use one, were never called by
 * anything. The monotonic clock this project actually uses is ns_now()
 * in src/session/ntrip_session.c, which is the same pair of platform
 * branches written once, where the stream loop that needs timing lives.
 */

/* ── Socket errors, reconciled ────────────────────────────────────────
 *
 * The one place POSIX's `errno` and Windows's `WSAE*` numbers are turned
 * into the same answer.  It lives here rather than in ntrip_proto.c
 * because it needs the platform's own headers, and ntrip_proto.c is
 * deliberately free of them -- that is what makes it testable without a
 * network.  It lives in `src/net` rather than in any frontend because a
 * frontend that mapped these itself would be a second opinion about what
 * a connection failure is, and the four of them would drift.
 *
 * Note `WSAECONNREFUSED` and `ECONNREFUSED` are different numbers for
 * the same event; the branches are per-platform, the answers are not.
 */
NsFailure ns_failure_from_socket(int err)
{
#ifdef _WIN32
    switch (err) {
        case WSAECONNREFUSED: return NS_FAIL_REFUSED;
        case WSAETIMEDOUT:    return NS_FAIL_TIMEOUT;
        case WSAEHOSTUNREACH:
        case WSAENETUNREACH:
        case WSAENETDOWN:     return NS_FAIL_UNREACHABLE;
        case WSAHOST_NOT_FOUND:
        case WSANO_DATA:      return NS_FAIL_DNS;
        default: break;
    }
#else
    switch (err) {
        case ECONNREFUSED: return NS_FAIL_REFUSED;
        case ETIMEDOUT:    return NS_FAIL_TIMEOUT;
        case EHOSTUNREACH:
        case ENETUNREACH:
        case ENETDOWN:     return NS_FAIL_UNREACHABLE;
        default: break;
    }
#endif
    /* Anything else reached the network and failed there.  Reported as
     * unreachable rather than invented into a category: it is true, and
     * it points at the network, which is where the fault is. */
    return err ? NS_FAIL_UNREACHABLE : NS_FAIL_NONE;
}

#define BUFFER_SIZE 4096
#define MAX_MSG_TYPES 4096

// Set satellites column width for 30 satellites, 3 chars per satellite (2 digits + space): 30*3 = 90
#define SAT_COL_WIDTH 60

typedef struct {
    int count;
    double min_dt;
    double max_dt;
    double sum_dt;
    double last_time;
    bool seen;
} MsgStats;

bool base64_encode_n(const char *input, char *output, size_t out_cap) {
    const char *base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned char in[3];
    int i, j;
    int input_len = (int)strlen(input);
    int output_index = 0;

    if (!output || out_cap == 0) return false;
    output[0] = '\0';

    /* Base64 expands by 4/3, rounded up to a whole group, plus the
     * terminator.  Refusing here beats truncating: a half-encoded
     * credential authenticates as nothing and looks like a caster
     * problem. */
    size_t need = ((size_t)input_len + 2) / 3 * 4 + 1;
    if (need > out_cap) return false;

    for (i = 0; i < input_len;) {
        int len = 0;
        for (j = 0; j < 3; j++) {
            if (i < input_len) in[j] = input[i++];
            else in[j] = 0, len++;
        }

        output[output_index++] = base64_chars[in[0] >> 2];
        output[output_index++] = base64_chars[((in[0] & 0x03) << 4) | (in[1] >> 4)];
        output[output_index++] = (len >= 2) ? '=' : base64_chars[((in[1] & 0x0f) << 2) | (in[2] >> 6)];
        output[output_index++] = (len >= 1) ? '=' : base64_chars[in[2] & 0x3f];
    }
    output[output_index] = '\0';
    return true;
}

char* receive_mount_table(const NTRIP_Config *config, const char *agent) {
    if (!config) {
        fprintf(stderr, "[ERROR] Config pointer is NULL.\n");
        return NULL; // -1
    }

    char request[1024];
    char buffer[BUFFER_SIZE];
    char *mount_table = NULL;
    size_t mount_table_size = 0;

    /* Same seam, same failure taxonomy as the stream -- and the same
     * TLS flag: every connection to this caster inherits it, so the
     * mountpoint list travels as protected as the stream it offers. */
    NsFailure fail = NS_FAIL_NONE;
    NsTransport *t = config->TLS
        ? ns_transport_connect_tls(config->NTRIP_CASTER,
                                   config->NTRIP_PORT, &fail, NULL, 0)
        : ns_transport_connect(config->NTRIP_CASTER,
                               config->NTRIP_PORT, &fail);
    if (!t) {
        if (fail == NS_FAIL_DNS)
            fprintf(stderr, "DNS lookup failed\n");
        else if (fail == NS_FAIL_TLS_HANDSHAKE || fail == NS_FAIL_TLS_CERT)
            fprintf(stderr, "TLS failed: %s\n", ns_failure_short(fail));
        else
            fprintf(stderr, "Connection failed\n");
        return NULL; // -4
    }

    /* AUTH_BASIC is precomputed by whoever loaded the config file, and
     * a caller that builds NTRIP_Config by hand has no way to know that
     * it must.  Deriving it here when it is empty makes the omission
     * harmless: an open caster tolerates a blank Authorization header,
     * while a caster that requires login answers with an empty
     * sourcetable -- which reads as "this caster has no mountpoints"
     * rather than as "you did not log in". */
    char auth[512];
    if (config->AUTH_BASIC[0]) {
        snprintf(auth, sizeof(auth), "%s", config->AUTH_BASIC);
    } else if (config->USERNAME[0]) {
        char userpass[512];
        snprintf(userpass, sizeof(userpass), "%s:%s",
                 config->USERNAME, config->PASSWORD);
        if (!base64_encode_n(userpass, auth, sizeof(auth))) auth[0] = 0;
    } else {
        auth[0] = '\0';
    }

    /* The caller names itself: this is reachable from both the CLI and
     * the GUI, and a caster operator should see which one asked. */
    snprintf(request, sizeof(request),
             "GET / HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: %s\r\n"
             "Authorization: Basic %s\r\n"
             "\r\n",
             config->NTRIP_CASTER,
             agent ? agent : NTRIP_USER_AGENT(NTRIP_ARTEFACT_LIB),
             auth);

    if (ns_transport_send(t, request, (int)strlen(request)) <= 0) {
        fprintf(stderr, "[ERROR] Failed to send mountpoint list request\n");
        ns_transport_close(t);
        return NULL; // -5
    }

    /* A sourcetable is a few hundred kilobytes at the largest public
     * casters.  The ceiling is not tidiness: this loop grows the buffer
     * by whatever arrives, from a host the user named but nobody
     * vouches for, so without a limit a caster that streams forever
     * exhausts memory -- on a phone, until the app is killed. */
    const size_t MOUNT_TABLE_MAX = 4u * 1024u * 1024u;

    /* Blocking reads (negative timeout): the reply is bounded by
     * ENDSOURCETABLE or by the caster closing, not by a pump loop. */
    int received;
    while ((received = ns_transport_recv(t, (unsigned char *)buffer,
                                         (int)sizeof(buffer) - 1, -1)) > 0) {
        buffer[received] = '\0';
        size_t old_size = mount_table_size;
        if (old_size + (size_t)received > MOUNT_TABLE_MAX) {
            fprintf(stderr,
                    "[WARN] Sourcetable exceeded %zu bytes; truncating. "
                    "A caster sending more than this is not serving a "
                    "mountpoint list.\n", MOUNT_TABLE_MAX);
            break;
        }
        mount_table_size += received;
        char *new_table = realloc(mount_table, mount_table_size + 1);
        if (!new_table) {
            free(mount_table);
            fprintf(stderr, "[ERROR] Memory allocation failed for mount table.\n");
            ns_transport_close(t);
            return NULL; // -6
        }
        mount_table = new_table;
        memcpy(mount_table + old_size, buffer, received + 1);
        if (strstr(buffer, "ENDSOURCETABLE")) {
            break;
        }
    }
    if (mount_table)
        mount_table[mount_table_size] = '\0';

    ns_transport_close(t);
    return mount_table; // 0 (success)
}

void extract_satellites(const unsigned char *data, int len, int msg_type, SatStatsSummary *summary) {
    int prns[MAX_SATS_PER_GNSS];
    int gnss_id = 0;
    int n = msm_extract_prns(data, len, msg_type, prns, MAX_SATS_PER_GNSS, &gnss_id);
    if (n <= 0 || !gnss_id) return;

    int idx = -1;
    for (int i = 0; i < summary->gnss_count; ++i) {
        if (summary->gnss[i].gnss_id == gnss_id) {
            idx = i;
            break;
        }
    }
    if (idx == -1 && summary->gnss_count < MAX_GNSS) {
        idx = summary->gnss_count++;
        summary->gnss[idx].gnss_id = gnss_id;
        memset(summary->gnss[idx].sat_seen, 0, sizeof(summary->gnss[idx].sat_seen));
        summary->gnss[idx].count = 0;
    }
    if (idx == -1) return;

    for (int i = 0; i < n; ++i) {
        int prn = prns[i];
        if (prn < 1 || prn > MAX_SATS_PER_GNSS) continue;
        if (!summary->gnss[idx].sat_seen[prn - 1]) {
            summary->gnss[idx].sat_seen[prn - 1] = 1;
            summary->gnss[idx].count++;
        }
    }
}

const char* rinex_id_from_gnss(int gnss_id, int prn, char *buf, size_t buflen) {
    // RINEX 3: G = GPS, R = GLONASS, E = Galileo, J = QZSS,
    //         C = BeiDou, S = SBAS, I = NavIC / IRNSS
    char sys = '?';
    switch (gnss_id) {
        case 1: sys = 'G'; break; // GPS
        case 2: sys = 'R'; break; // GLONASS
        case 3: sys = 'E'; break; // Galileo
        case 4: sys = 'J'; break; // QZSS
        case 5: sys = 'C'; break; // BeiDou
        case 6: sys = 'S'; break; // SBAS
        case 7: sys = 'I'; break; // NavIC / IRNSS
        default: sys = '?'; break;
    }
    snprintf(buf, buflen, "%c%02d", sys, prn);
    return buf;
}

const char* gnss_name_from_id(int gnss_id) {
    switch (gnss_id) {
        case 1: return "GPS";
        case 2: return "GLONASS";
        case 3: return "Galileo";
        case 4: return "QZSS";
        case 5: return "BeiDou";
        case 6: return "SBAS";
        case 7: return "NavIC";
        default: return "Unknown";
    }
}

/* Note: this mapping must agree with msm_extract_prns() in rtcm3x_parser.c,
 * which is what actually drives the Satellites tab today.  Previously this
 * function had SBAS mis-mapped to the 1130 range and lacked a NavIC entry. */
int get_gnss_id_from_rtcm(int msg_type) {
    if (msg_type >= 1070 && msg_type < 1080) return 1; // GPS
    if (msg_type >= 1080 && msg_type < 1090) return 2; // GLONASS
    if (msg_type >= 1090 && msg_type < 1100) return 3; // Galileo
    if (msg_type >= 1100 && msg_type < 1110) return 6; // SBAS
    if (msg_type >= 1110 && msg_type < 1120) return 4; // QZSS
    if (msg_type >= 1120 && msg_type < 1130) return 5; // BeiDou
    if (msg_type >= 1130 && msg_type < 1140) return 7; // NavIC / IRNSS
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────
 * Ephemeris-only NTRIP worker used by CLI `-s --sky` mode.
 *
 * Mirrors gui_thread.c WorkerOpenEphStream but reduced to the bare CLI
 * essentials: no UI messages, no log redirection -- just connect, parse
 * RTCM frames, dispatch eph-bearing messages (1019/1020/1041/1042/
 * 1044/1045/1046) into the per-SV ephemeris cache via the existing
 * decode_rtcm_* functions.  Polls @p stop_flag every recv() iteration
 * so it can exit promptly on Ctrl-C.
 * ───────────────────────────────────────────────────────────────────── */
