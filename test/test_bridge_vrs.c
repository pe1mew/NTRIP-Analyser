/**
 * @file test_bridge_vrs.c
 * @brief The bridge's VRS workflow, over a real socket.
 *
 * V1 (`test_vrs.c`) proved the engine against synthetic snapshots; this
 * proves the *plumbing* the phone will use: `bridge_open(vrs=true)`
 * noting each accepted GGA, updating per pump, and publishing the
 * report in the document -- against a loopback caster, through
 * `bridge_snapshot_json`, which is the exact text the app decodes.
 * The bridge compiles unmodified on the desktop; its Android logging is
 * `__ANDROID__`-guarded, and `bridge_pump` takes the clock from the
 * caller, so the 60 s hold window costs nothing here either.
 *
 * What this deliberately does not cover: the *automatic* gate entry,
 * which fires on the CLI's condition -- eight KPIs sustained -- and a
 * loopback serving junk frames cannot sustain eight KPIs.  The gate is
 * entered by `bridge_vrs_gate()`, the explicit way in that
 * `vrs_check.h`'s contract anticipates for a frontend with a button.
 * The automatic condition is four lines lifted from `cli_stream.c` and
 * is verified on a live caster in V3.
 *
 * The caster needs a thread: `bridge_open` *blocks* fetching the
 * sourcetable while the observation connection already sits in the
 * backlog, so a single-threaded harness would deadlock against it.
 * The thread serves both, telling them apart by the request line.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
#include "ntrip_bridge.h"
#include "core/rtcm3x_parser.h"   /* crc24q, for valid junk frames */
#include "core/ns_stats.h"        /* the CSV dialect the export must match */

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
   typedef SOCKET sock_t;
#  define SOCK_BAD    INVALID_SOCKET
#  define close_sock  closesocket
#  define sleep_ms(m) Sleep(m)
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <pthread.h>
#  include <time.h>
   typedef int sock_t;
#  define SOCK_BAD    (-1)
#  define close_sock  close
   static void sleep_ms(int ms)
   {
       struct timespec ts;
       ts.tv_sec  = ms / 1000;
       ts.tv_nsec = (long)(ms % 1000) * 1000000L;
       nanosleep(&ts, NULL);
   }
#endif

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

/* ── A caster on its own thread ──────────────────────────────────── */

#define TABLE_ANSWER                                                    \
    "SOURCETABLE 200 OK\r\n"                                            \
    "Content-Type: text/plain\r\n"                                      \
    "\r\n"                                                              \
    "STR;TEST;TEST;RTCM 3.2;1074(1);2;GPS;NET;NLD;52.00;5.00;1;0;"      \
    "none;none;B;N;0;\r\n"                                              \
    "ENDSOURCETABLE\r\n"

static volatile int g_drop;   /**< close the stream: the gate's answer */
static volatile int g_quit;

static sock_t g_listener;

static void set_nonblocking(sock_t sk)
{
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket(sk, FIONBIO, &on);
#else
    int fl = fcntl(sk, F_GETFL, 0);
    fcntl(sk, F_SETFL, fl | O_NONBLOCK);
#endif
}

/** One CRC-valid frame of junk: enough for frames_ok, nothing more. */
static int junk_frame(unsigned char *out)
{
    const int payload_len = 20;
    out[0] = 0xD3;
    out[1] = 0;
    out[2] = (unsigned char)payload_len;
    out[3] = (unsigned char)(1074 >> 4);            /* message type */
    out[4] = (unsigned char)((1074 & 0x0F) << 4);
    for (int i = 5; i < 3 + payload_len; i++)
        out[i] = (unsigned char)(i * 7);
    uint32_t crc = crc24q(out, (size_t)(3 + payload_len));
    out[3 + payload_len + 0] = (unsigned char)((crc >> 16) & 0xFF);
    out[3 + payload_len + 1] = (unsigned char)((crc >> 8)  & 0xFF);
    out[3 + payload_len + 2] = (unsigned char)( crc        & 0xFF);
    return payload_len + 6;
}

/**
 * @brief Serve the two connections `bridge_open` makes.
 *
 * A slot per client, none blocking: the sourcetable fetch and the
 * observation stream connect near-simultaneously and the kernel hands
 * them over in whichever order it likes, so each is identified by what
 * it asks for, not by when it arrived.
 */
#ifdef _WIN32
static DWORD WINAPI caster_thread(LPVOID arg)
#else
static void *caster_thread(void *arg)
#endif
{
    (void)arg;
    sock_t client[2] = { SOCK_BAD, SOCK_BAD };
    int    role[2]   = { 0, 0 };          /* 0 unknown, 1 stream */
    unsigned char frame[64];
    int flen = junk_frame(frame);

    while (!g_quit) {
        for (int i = 0; i < 2; i++) {
            if (client[i] != SOCK_BAD) continue;
            sock_t c = accept(g_listener, NULL, NULL);
            if (c == SOCK_BAD) break;
            set_nonblocking(c);
            client[i] = c;
            role[i]   = 0;
        }

        for (int i = 0; i < 2; i++) {
            if (client[i] == SOCK_BAD || role[i]) continue;
            char req[512];
            int n = recv(client[i], req, sizeof(req) - 1, 0);
            if (n <= 0) continue;
            req[n] = 0;
            if (strstr(req, "GET / ")) {          /* the sourcetable */
                send(client[i], TABLE_ANSWER, (int)strlen(TABLE_ANSWER), 0);
                close_sock(client[i]);
                client[i] = SOCK_BAD;
            } else {                              /* the stream */
                const char *icy = "ICY 200 OK\r\n\r\n";
                send(client[i], icy, (int)strlen(icy), 0);
                role[i] = 1;
            }
        }

        for (int i = 0; i < 2; i++) {
            if (client[i] == SOCK_BAD || role[i] != 1) continue;
            if (g_drop) {
                close_sock(client[i]);
                client[i] = SOCK_BAD;
                role[i]   = 0;
            } else {
                send(client[i], (const char *)frame, flen, 0);
                char scratch[256];               /* drain the GGA uplink */
                (void)recv(client[i], scratch, sizeof(scratch), 0);
            }
        }
        sleep_ms(20);
    }

    for (int i = 0; i < 2; i++)
        if (client[i] != SOCK_BAD) close_sock(client[i]);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/* ── The test ────────────────────────────────────────────────────── */

static char doc[32768];

static int pump_json(NtripBridge *b, double now)
{
    bridge_pump(b, 20, now);
    return bridge_snapshot_json(b, doc, sizeof(doc));
}

/** Pump at a held clock until @p needle appears, or ~3 s real pass. */
static int pump_until(NtripBridge *b, double now, const char *needle)
{
    for (int i = 0; i < 150; i++) {
        if (pump_json(b, now) > 0 && strstr(doc, needle)) return 1;
        sleep_ms(20);
    }
    return 0;
}

int main(void)
{
    /* The listener, before the thread that serves it. */
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    g_listener = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(g_listener, (struct sockaddr *)&a, sizeof(a)) != 0 ||
        listen(g_listener, 4) != 0) {
        check(0, "the loopback caster listens");
        return 1;
    }
    socklen_t alen = sizeof(a);
    getsockname(g_listener, (struct sockaddr *)&a, &alen);
    int port = ntohs(a.sin_port);
    set_nonblocking(g_listener);

#ifdef _WIN32
    HANDLE th = CreateThread(NULL, 0, caster_thread, NULL, 0, NULL);
#else
    pthread_t th;
    pthread_create(&th, NULL, caster_thread, NULL);
#endif

    /* ── A VRS-mode run, end to end ─────────────────────────────── */
    NtripBridge *b = bridge_open("127.0.0.1", port, "TEST", "", "",
                                 52.0, 5.0, false, false, true);
    check(b != NULL, "the bridge opens in VRS mode");
    if (!b) { g_quit = 1; return 1; }

    check(pump_until(b, 1.0, "\"vrs\":{"),
          "the document carries the vrs object");
    check(pump_until(b, 1.0, "\"label\":\"GGA accepted by caster\""),
          "the assertions travel with their engine labels");
    check(pump_until(b, 2.0, "Corrections flowing inside the deadline"),
          "A2 sees the frames, timed from the accepted GGA");

    /* Past the acceptance window, then past the hold window: the same
     * clock jumps the engine test makes, on a live socket. */
    check(pump_until(b, 6.0, "No disconnect in the window"),
          "A1 passes once the acceptance window closes clean");
    check(pump_until(b, 61.0, "Continuous through the window"),
          "A4 passes after the hold window");
    check(strstr(doc, "\"gate_started\":false") != NULL,
          "the gate has not started by itself on a junk-frame loopback");

    /* The gate: entered explicitly -- the tapped-button way in -- and
     * answered by the caster closing the stream. */
    bridge_vrs_gate(b, 61.0);
    pump_json(b, 61.5);
    check(strstr(doc, "\"gate_started\":true") != NULL,
          "the explicit entry is reported");
    check(strstr(doc, "GGA stopped; watching for the drop") != NULL,
          "A5 is watching once the gate is entered");
    g_drop = 1;
    check(pump_until(b, 70.0, "GGA-gated (network service)"),
          "the drop after GGA stopped classifies the service as gated");
    check(strstr(doc, "\"failed\":false") != NULL,
          "nothing failed on the way");
    bridge_close(b);

    /* ── The same caster, a normal run: no vrs object at all ─────── */
    g_drop = 0;
    b = bridge_open("127.0.0.1", port, "TEST", "", "",
                    52.0, 5.0, false, false, false);
    check(b != NULL, "the bridge opens in normal mode");
    if (b) {
        int n = pump_json(b, 1.0);
        check(n > 0 && strstr(doc, "\"vrs\"") == NULL,
              "a normal run's document carries no vrs object");

        /* The statistics export speaks the daemon's dialect or it does
         * not ship: the first line must be the core's own header, byte
         * for byte, and the row must fill every column it names. */
        char csv[8192];
        int c = bridge_stats_csv(b, csv, sizeof(csv));
        check(c > 0, "the bridge writes the CSV export");
        if (c > 0) {
            char hdr[4096];
            int h = ns_stats_csv_header(hdr, sizeof(hdr));
            check(h > 0 && strncmp(csv, hdr, (size_t)h) == 0 &&
                  csv[h] == '\n',
                  "its first line is the core's header, byte for byte");

            int cols_hdr = 1, cols_row = 1;
            for (int i = 0; i < h; i++)
                if (hdr[i] == ',') cols_hdr++;
            for (int i = h + 1; i < c; i++)
                if (csv[i] == ',') cols_row++;
            check(cols_hdr == cols_row,
                  "the row fills every column the header names");
        }

        check(bridge_stats_csv(b, csv, 64) < 0,
              "a buffer too small is refused, never half-written");
        bridge_close(b);
    }

    g_quit = 1;
#ifdef _WIN32
    WaitForSingleObject(th, 2000);
#else
    pthread_join(th, NULL);
#endif
    close_sock(g_listener);

    printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
