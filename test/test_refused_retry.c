/**
 * @file test_refused_retry.c
 * @brief A refusal that passes by itself must not end a session for good.
 *
 * A session used to treat every refusal as final, even with
 * `auto_reconnect` on and even after days of streaming. shuttle2's daemon
 * lost four stations in 41 hours that way (2026-10-29, -30), each to one
 * 503 or one missing mountpoint, and then exited with status 0. A base
 * going offline drops out of the sourcetable and comes back; that is
 * the event a monitor exists to watch.
 *
 * The other half matters as much: a wrong password still ends at once,
 * and so does a mountpoint mistyped on a first connect, or a frontend
 * would retry a typing error for ever and say nothing.
 *
 * The caster is a real one on the loopback, answering each connection
 * from a script, because what is being pinned is what happens across
 * connections.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
#include "session/ntrip_session.h"
#include "core/ns_failure.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

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

/* ── A caster that answers from a script ─────────────────────────── */

/** One connection's worth of behaviour. */
typedef enum {
    ANS_STREAM,        /**< accept, send some bytes, then close        */
    ANS_BUSY,          /**< 503                                        */
    ANS_NO_MOUNT_404,  /**< 404                                        */
    ANS_SOURCETABLE,   /**< 200 with a sourcetable: NTRIP 1's "no"     */
    ANS_AUTH,          /**< 401                                        */
    ANS_FORBIDDEN      /**< 403                                        */
} Answer;

#define MAX_SCRIPT 8

typedef struct {
    sock_t listener;
    int    port;
    Answer script[MAX_SCRIPT];
    int    n_script;
    int    next;               /**< connections accepted so far        */
    double at[MAX_SCRIPT];     /**< when each one arrived, seconds     */
} Caster;

/** @brief Seconds on a clock that runs while this process sleeps. */
static double wall_s(void)
{
#ifdef _WIN32
    return (double)GetTickCount() / 1000.0;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
}

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

static bool caster_start(Caster *c, const Answer *script, int n)
{
    memset(c, 0, sizeof(*c));
    memcpy(c->script, script, (size_t)n * sizeof(Answer));
    c->n_script = n;

    c->listener = socket(AF_INET, SOCK_STREAM, 0);
    if (c->listener == SOCK_BAD) return false;

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port        = 0;

    if (bind(c->listener, (struct sockaddr *)&a, sizeof(a)) != 0) return false;
    if (listen(c->listener, 4) != 0) return false;

    socklen_t alen = sizeof(a);
    if (getsockname(c->listener, (struct sockaddr *)&a, &alen) != 0) return false;
    c->port = ntohs(a.sin_port);

    set_nonblocking(c->listener);
    return true;
}

/** @brief Accept one waiting connection, answer it, and close it. */
static void caster_service(Caster *c)
{
    sock_t k = accept(c->listener, NULL, NULL);
    if (k == SOCK_BAD) return;

    /* Read the request first: closing with it unread sends a reset,
     * which would discard the answer on its way to the client. */
    char scratch[1024];
    sleep_ms(20);
    (void)recv(k, scratch, sizeof(scratch), 0);

    Answer a = c->next < c->n_script ? c->script[c->next] : ANS_BUSY;
    if (c->next < MAX_SCRIPT) c->at[c->next] = wall_s();
    c->next++;

    const char *r = "";
    switch (a) {
        case ANS_STREAM:
            r = "ICY 200 OK\r\n\r\n....";
            break;
        case ANS_BUSY:
            r = "HTTP/1.1 503 Service Unavailable\r\n\r\n";
            break;
        case ANS_NO_MOUNT_404:
            r = "HTTP/1.1 404 Not Found\r\n\r\n";
            break;
        case ANS_SOURCETABLE:
            r = "HTTP/1.1 200 OK\r\n"
                "Content-Type: gnss/sourcetable\r\n\r\nENDSOURCETABLE\r\n";
            break;
        case ANS_AUTH:
            r = "HTTP/1.1 401 Unauthorized\r\n\r\n";
            break;
        case ANS_FORBIDDEN:
            r = "HTTP/1.1 403 Forbidden\r\n\r\n";
            break;
    }
    send(k, r, (int)strlen(r), 0);
    sleep_ms(20);
    close_sock(k);
}

static void caster_stop(Caster *c)
{
    if (c->listener != SOCK_BAD) close_sock(c->listener);
}

/* ── Running a session against it ────────────────────────────────── */

typedef struct {
    int ended;
    int end_reason;
} Seen;

static void on_event(const NsEvent *ev, void *user)
{
    Seen *s = (Seen *)user;
    if (ev->type == NS_EV_DISCONNECTED) {
        s->ended      = 1;
        s->end_reason = ev->u.end.reason;
    }
}

/** @brief Pump until the session ends, @p until conns arrive, or time's up. */
static void run_until(Caster *c, NtripSession *sess, Seen *seen,
                      int until, int budget_ms)
{
    double end = wall_s() + budget_ms / 1000.0;
    while (wall_s() < end && !seen->ended && c->next < until) {
        caster_service(c);
        if (ns_pump(sess, 10) < 0) break;
        sleep_ms(5);
    }
    /* Let the last answer arrive and be read. */
    for (int i = 0; i < 20 && !seen->ended; i++) {
        if (ns_pump(sess, 10) < 0) break;
        sleep_ms(5);
    }
}

static void options_for(NsOptions *opt, int port)
{
    ns_options_default(opt);
    strncpy(opt->config.NTRIP_CASTER, "127.0.0.1",
            sizeof(opt->config.NTRIP_CASTER) - 1);
    strncpy(opt->config.MOUNTPOINT, "TEST",
            sizeof(opt->config.MOUNTPOINT) - 1);
    opt->config.NTRIP_PORT      = port;
    opt->stats_interval_s       = 0.0;
    opt->auto_reconnect         = true;
    opt->reconnect_backoff_max_s = 1;   /* keeps the suite quick */
}

/* ── Cases ───────────────────────────────────────────────────────── */

/** The classification itself: which refusals pass, which need a person. */
static void case_classification(void)
{
    printf("\n-- which refusals are worth retrying --\n");
    check(ns_failure_is_transient(NS_FAIL_BUSY), "busy passes by itself");
    check(ns_failure_is_transient(NS_FAIL_NO_MOUNTPOINT),
          "a missing mountpoint passes by itself: its base comes back");
    check(!ns_failure_is_transient(NS_FAIL_AUTH), "a wrong password does not");
    check(!ns_failure_is_transient(NS_FAIL_FORBIDDEN),
          "a mountpoint the account may not use does not");
    check(!ns_failure_is_transient(NS_FAIL_NOT_NTRIP),
          "a port that is not a caster does not");
    check(!ns_failure_is_transient(NS_FAIL_NONE), "nothing wrong is not");
}

/**
 * shuttle2's night: a stream that worked, then a caster restarting. Each
 * refusal must be survived, and the session must stream again after.
 */
static void case_rides_out_a_restart(void)
{
    printf("\n-- a caster restarting under a working stream --\n");

    static const Answer script[] = {
        ANS_STREAM, ANS_BUSY, ANS_SOURCETABLE, ANS_NO_MOUNT_404, ANS_STREAM
    };
    Caster c;
    if (!caster_start(&c, script, 5)) {
        check(0, "the loopback caster started");
        return;
    }
    NsOptions opt;
    options_for(&opt, c.port);

    Seen seen;
    memset(&seen, 0, sizeof(seen));
    NtripSession *sess = ns_open(&opt, on_event, &seen);
    if (!sess) { check(0, "the session opened"); caster_stop(&c); return; }

    run_until(&c, sess, &seen, 3, 8000);
    check(!seen.ended, "the session survived a 503 and a sourcetable");
    const NsStatsSnapshot *mid = ns_stats(sess);
    check(!mid->connected, "and did not call itself connected meanwhile");
    check(ns_failure(sess) == NS_FAIL_NO_MOUNTPOINT ||
          ns_failure(sess) == NS_FAIL_BUSY,
          "and kept saying why, for the snapshot to carry");

    run_until(&c, sess, &seen, 5, 8000);
    check(!seen.ended, "and survived a 404 as well");
    check(c.next >= 5, "it reached the connection that streams again");
    check(ns_failure(sess) == NS_FAIL_NONE ||
          ns_failure(sess) == NS_FAIL_DROPPED,
          "and the refusal no longer stands once accepted");
    check(ns_stats(sess)->reconnects >= 4, "every attempt was counted");

    ns_close(sess);
    caster_stop(&c);
}

/** Needing a person is final, even after days of streaming. */
static void case_needs_a_person(Answer refusal, NsFailure expected,
                                const char *name)
{
    printf("\n-- %s after a working stream --\n", name);

    Answer script[2];
    script[0] = ANS_STREAM;
    script[1] = refusal;
    Caster c;
    if (!caster_start(&c, script, 2)) {
        check(0, "the loopback caster started");
        return;
    }
    NsOptions opt;
    options_for(&opt, c.port);

    Seen seen;
    memset(&seen, 0, sizeof(seen));
    NtripSession *sess = ns_open(&opt, on_event, &seen);
    if (!sess) { check(0, "the session opened"); caster_stop(&c); return; }

    run_until(&c, sess, &seen, 3, 6000);
    check(seen.ended, "the session ended");
    check(seen.end_reason == NS_END_REJECTED, "as rejected");
    check(ns_failure(sess) == expected, "and says why");
    check(c.next == 2, "without knocking again");

    ns_close(sess);
    caster_stop(&c);
}

/** A first connect refused: a typing error must say so now. */
static void case_first_connect(void)
{
    printf("\n-- refused on the first connect, as a frontend opens it --\n");

    static const Answer script[] = { ANS_SOURCETABLE };
    Caster c;
    if (!caster_start(&c, script, 1)) {
        check(0, "the loopback caster started");
        return;
    }
    NsOptions opt;
    options_for(&opt, c.port);

    Seen seen;
    memset(&seen, 0, sizeof(seen));
    NtripSession *sess = ns_open(&opt, on_event, &seen);
    if (!sess) { check(0, "the session opened"); caster_stop(&c); return; }

    run_until(&c, sess, &seen, 2, 4000);
    check(seen.ended && seen.end_reason == NS_END_REJECTED,
          "a mistyped mountpoint ends at once");
    check(c.next == 1, "without retrying");

    ns_close(sess);
    caster_stop(&c);
}

/** The daemon's option: start through an outage. */
static void case_retry_before_accept(void)
{
    printf("\n-- refused on the first connect, as the daemon opens it --\n");

    static const Answer script[] = { ANS_BUSY, ANS_SOURCETABLE, ANS_STREAM };
    Caster c;
    if (!caster_start(&c, script, 3)) {
        check(0, "the loopback caster started");
        return;
    }
    NsOptions opt;
    options_for(&opt, c.port);
    opt.retry_before_accept = true;

    Seen seen;
    memset(&seen, 0, sizeof(seen));
    NtripSession *sess = ns_open(&opt, on_event, &seen);
    if (!sess) { check(0, "the session opened"); caster_stop(&c); return; }

    run_until(&c, sess, &seen, 3, 8000);
    check(!seen.ended, "a boot during an outage loses no station");
    check(c.next >= 3, "and it reached the stream");

    ns_close(sess);
    caster_stop(&c);
}

/** With reconnecting off, nothing changes: a refusal ends the session. */
static void case_no_auto_reconnect(void)
{
    printf("\n-- reconnecting off, as a one-shot check runs --\n");

    static const Answer script[] = { ANS_BUSY };
    Caster c;
    if (!caster_start(&c, script, 1)) {
        check(0, "the loopback caster started");
        return;
    }
    NsOptions opt;
    options_for(&opt, c.port);
    opt.auto_reconnect      = false;
    opt.retry_before_accept = true;

    Seen seen;
    memset(&seen, 0, sizeof(seen));
    NtripSession *sess = ns_open(&opt, on_event, &seen);
    if (!sess) { check(0, "the session opened"); caster_stop(&c); return; }

    run_until(&c, sess, &seen, 2, 4000);
    check(seen.ended && seen.end_reason == NS_END_REJECTED,
          "the option needs auto_reconnect: alone it retries nothing");

    ns_close(sess);
    caster_stop(&c);
}

/**
 * The backoff must grow across refusals. It used to restart at each
 * connect, which on a refused stream is every second for as long as the
 * caster stays down -- the worst moment to knock.
 */
static void case_backoff_grows(void)
{
    printf("\n-- refusals back off --\n");

    static const Answer script[] = {
        ANS_BUSY, ANS_BUSY, ANS_BUSY, ANS_BUSY
    };
    Caster c;
    if (!caster_start(&c, script, 4)) {
        check(0, "the loopback caster started");
        return;
    }
    NsOptions opt;
    options_for(&opt, c.port);
    opt.retry_before_accept     = true;
    opt.reconnect_backoff_max_s = 60;

    Seen seen;
    memset(&seen, 0, sizeof(seen));
    NtripSession *sess = ns_open(&opt, on_event, &seen);
    if (!sess) { check(0, "the session opened"); caster_stop(&c); return; }

    run_until(&c, sess, &seen, 4, 12000);
    check(c.next >= 4, "four attempts arrived");

    double gap1 = c.at[2] - c.at[1];   /* expected 2 s */
    double gap2 = c.at[3] - c.at[2];   /* expected 4 s */
    printf("      gaps %.2f s then %.2f s\n", gap1, gap2);
    check(gap1 > 1.5, "the second wait is about two seconds, not one");
    check(gap2 > 3.5, "and the third about four");

    ns_close(sess);
    caster_stop(&c);
}

int main(void)
{
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    case_classification();
    case_rides_out_a_restart();
    case_needs_a_person(ANS_AUTH, NS_FAIL_AUTH, "a 401");
    case_needs_a_person(ANS_FORBIDDEN, NS_FAIL_FORBIDDEN, "a 403");
    case_first_connect();
    case_retry_before_accept();
    case_no_auto_reconnect();
    case_backoff_grows();

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "all passed",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
