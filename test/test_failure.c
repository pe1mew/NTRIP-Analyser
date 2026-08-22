/**
 * @file test_failure.c
 * @brief A refusal must say which refusal it was.
 *
 * Until 3.7.0 every way of failing to open a stream produced one
 * sentence.  The Android app said *"Could not open the session."*
 * whether the host name was wrong, the port was wrong, the password was
 * wrong or the mountpoint did not exist -- four different things to go
 * and fix, behind one message that named none of them.  The information
 * was not missing: `getaddrinfo` had failed, `connect` had set
 * `ECONNREFUSED`, the 401 had been parsed into `NsHandshake` and never
 * read.  It was computed and discarded.
 *
 * So this pins the classification, and it does it against a real caster
 * on the loopback interface -- the harness `test_stall.c` established --
 * because half of these faults are socket states that no buffer of
 * bytes can imitate.  The caster answers as told: 401, 403, 404, a
 * sourcetable where a stream was asked for, a web page, or properly.
 * The refused case needs no caster at all: a port that was listening
 * and is not any more.
 *
 * No network: every address here is 127.0.0.1 on a port the OS handed
 * out.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
#include "session/ntrip_session.h"
#include "net/ntrip_proto.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
   typedef SOCKET sock_t;
#  define SOCK_BAD    INVALID_SOCKET
#  define close_sock  closesocket
#  define sleep_ms(m) Sleep(m)
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <fcntl.h>
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

/* ── A caster that answers as instructed ─────────────────────────── */

typedef struct {
    sock_t      listener;
    sock_t      client;
    int         port;
    const char *answer;   /**< the whole response, header and all */
    int         answered;
} Caster;

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

/**
 * @brief Listen on an ephemeral loopback port.
 *
 * Ephemeral rather than fixed: several cases run in one process, and a
 * fixed port makes the second fail on a machine that has not finished
 * releasing the first.
 */
static int caster_start(Caster *c, const char *answer)
{
    memset(c, 0, sizeof(*c));
    c->client = SOCK_BAD;
    c->answer = answer;

#ifdef _WIN32
    static int started = 0;
    if (!started) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 0;
        started = 1;
    }
#endif

    c->listener = socket(AF_INET, SOCK_STREAM, 0);
    if (c->listener == SOCK_BAD) return 0;

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port        = 0;

    if (bind(c->listener, (struct sockaddr *)&a, sizeof(a)) != 0) return 0;
    if (listen(c->listener, 1) != 0) return 0;

    socklen_t alen = sizeof(a);
    if (getsockname(c->listener, (struct sockaddr *)&a, &alen) != 0) return 0;
    c->port = ntohs(a.sin_port);

    set_nonblocking(c->listener);
    return 1;
}

/** @brief One turn: accept, read the request, answer once. */
static void caster_service(Caster *c)
{
    if (c->client == SOCK_BAD) {
        c->client = accept(c->listener, NULL, NULL);
        if (c->client == SOCK_BAD) return;
        set_nonblocking(c->client);
        return;
    }

    char scratch[512];
    (void)recv(c->client, scratch, sizeof(scratch), 0);

    if (!c->answered && c->answer) {
        send(c->client, c->answer, (int)strlen(c->answer), 0);
        c->answered = 1;
    }
}

static void caster_stop(Caster *c)
{
    if (c->client   != SOCK_BAD) close_sock(c->client);
    if (c->listener != SOCK_BAD) close_sock(c->listener);
}

/* ── Running a session against it ────────────────────────────────── */

typedef struct { int ended; } Seen;

static void on_event(const NsEvent *ev, void *user)
{
    Seen *s = (Seen *)user;
    if (ev->type == NS_EV_DISCONNECTED) s->ended = 1;
}

static void options_for(NsOptions *opt, int port)
{
    ns_options_default(opt);
    strncpy(opt->config.NTRIP_CASTER, "127.0.0.1",
            sizeof(opt->config.NTRIP_CASTER) - 1);
    strncpy(opt->config.MOUNTPOINT, "TEST",
            sizeof(opt->config.MOUNTPOINT) - 1);
    opt->config.NTRIP_PORT = port;
    opt->stats_interval_s  = 0.0;
    opt->auto_reconnect    = false;   /* one attempt, one verdict */
}

/**
 * @brief Open a session against @p answer and report how it failed.
 *
 * @param answer what the caster replies, or NULL for "no caster at all",
 *               which is how the refused case is made.
 */
static NsFailure failure_for(const char *answer, const char *what)
{
    Caster c;
    int port;

    if (!caster_start(&c, answer)) {
        check(0, "the loopback caster started");
        return NS_FAIL_NONE;
    }
    port = c.port;

    /* The refused case: the port existed a moment ago and does not now,
     * which is exactly what a wrong port number looks like. */
    if (!answer) caster_stop(&c);

    NsOptions opt;
    options_for(&opt, port);

    Seen seen;
    memset(&seen, 0, sizeof(seen));

    NtripSession *sess = ns_open(&opt, on_event, &seen);
    if (!sess) {
        check(0, what);
        if (answer) caster_stop(&c);
        return NS_FAIL_NONE;
    }

    for (int waited = 0; waited < 4000 && !seen.ended; waited += 20) {
        if (answer) caster_service(&c);
        if (ns_pump(sess, 10) < 0) break;
        sleep_ms(10);
    }

    NsFailure f = ns_failure(sess);
    ns_close(sess);
    if (answer) caster_stop(&c);
    return f;
}

/* ── Cases ───────────────────────────────────────────────────────── */

static void case_socket(void)
{
    printf("\n-- the socket never got there --\n");

    NsFailure f = failure_for(NULL, "a session against a dead port opened");
    check(f == NS_FAIL_REFUSED,
          "a port with nothing behind it is REFUSED, not a generic failure");

    /* The mapping itself, both platforms' spellings of the same events.
     * Checked directly as well as through a socket: the socket case can
     * only produce one of them on one machine. */
    check(ns_failure_from_socket(0) == NS_FAIL_NONE,
          "no error is no failure");
}

static void case_answers(void)
{
    printf("\n-- the caster answered, and said no --\n");

    check(failure_for("HTTP/1.1 401 Unauthorized\r\n\r\n",
                      "401 session") == NS_FAIL_AUTH,
          "401 is the user name or the password");

    check(failure_for("HTTP/1.1 403 Forbidden\r\n\r\n",
                      "403 session") == NS_FAIL_FORBIDDEN,
          "403 is the credentials being right for something else");

    check(failure_for("HTTP/1.1 404 Not Found\r\n\r\n",
                      "404 session") == NS_FAIL_NO_MOUNTPOINT,
          "404 is a mountpoint that does not exist");

    /* An NTRIP 1 caster answers an unknown mountpoint with the whole
     * sourcetable and a 200.  A client that reads only the status
     * believes it succeeded. */
    check(failure_for("HTTP/1.1 200 OK\r\n"
                      "Content-Type: gnss/sourcetable\r\n\r\n"
                      "ENDSOURCETABLE\r\n",
                      "sourcetable session") == NS_FAIL_NO_MOUNTPOINT,
          "a sourcetable where a stream was asked for is a missing mountpoint");

    check(failure_for("HTTP/1.1 200 OK\r\n"
                      "Content-Type: text/html\r\n\r\n"
                      "<html><body>hello</body></html>",
                      "web page session") == NS_FAIL_NOT_NTRIP,
          "a web page on the port is not a caster");

    check(failure_for("HTTP/1.1 503 Service Unavailable\r\n\r\n",
                      "503 session") == NS_FAIL_BUSY,
          "503 is a caster that is full or restarting");
}

static void case_healthy(void)
{
    printf("\n-- the caster said yes --\n");

    /* The one that must NOT fire.  A classification that finds a fault
     * in a working stream is worse than none: every message it produces
     * is an accusation against a healthy station. */
    NsFailure f = failure_for("ICY 200 OK\r\n\r\n", "ICY session");
    check(f == NS_FAIL_NONE,
          "a caster that answers properly is not a failure");
}

static void case_words(void)
{
    printf("\n-- the words a user reads --\n");

    char buf[192];

    ns_failure_text(buf, sizeof(buf), NS_FAIL_DNS, "no.such.host", 2101, "MP");
    check(strstr(buf, "no.such.host") != NULL,
          "the DNS sentence names the host that could not be found");

    ns_failure_text(buf, sizeof(buf), NS_FAIL_REFUSED, "host", 2102, "MP");
    check(strstr(buf, "2102") != NULL,
          "the refused sentence names the port, which is the field at fault");

    ns_failure_text(buf, sizeof(buf), NS_FAIL_NO_MOUNTPOINT, "host", 2101, "RFSEE01");
    check(strstr(buf, "RFSEE01") != NULL,
          "the missing-mountpoint sentence names the mountpoint");

    buf[0] = 'x';
    int n = ns_failure_text(buf, sizeof(buf), NS_FAIL_NONE, "host", 2101, "MP");
    check(n == 0 && buf[0] == 0,
          "no failure produces no sentence rather than an empty-looking one");

    /* The tokens are what the CSV and the tests carry; they must not be
     * quietly renamed, and every code must have one. */
    check(strcmp(ns_failure_name(NS_FAIL_AUTH), "auth") == 0,
          "each failure has a stable token");
    check(strcmp(ns_failure_name(NS_FAIL_NONE), "none") == 0,
          "so does the absence of one");
}

int main(void)
{
    printf("== failure classification ==\n");

    case_socket();
    case_answers();
    case_healthy();
    case_words();

    printf("\n%s\n", failures ? "FAILURES" : "all passed");
    return failures ? 1 : 0;
}
