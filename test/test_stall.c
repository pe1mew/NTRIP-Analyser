/**
 * @file test_stall.c
 * @brief A caster that stops sending must be noticed, not believed.
 *
 * The fault this covers has no symptom of its own.  The caster stops
 * sending and closes nothing: the socket stays established, `recv`
 * keeps saying "nothing yet", and a session with no clock of its own
 * reports itself connected for as long as the process lives.  One
 * monitored stream delivered nothing for fourteen hours that way while
 * every status it published said it was fine.
 *
 * So the caster here is a real one, on the loopback interface, and it
 * misbehaves on purpose: it accepts, answers ICY 200 OK, and then --
 * depending on the case -- keeps sending, goes quiet, or goes quiet
 * having never sent a byte.  A test that fed the session a buffer could
 * not tell those apart, because what distinguishes them is a socket
 * that is open and idle, which only a socket can be.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
#include "session/ntrip_session.h"

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

/* ── A caster that misbehaves on purpose ─────────────────────────── */

/** How the fake caster behaves once it has accepted a client. */
typedef enum {
    FEED_ALWAYS,   /**< keep sending, so the timeout must not fire */
    FEED_THEN_QUIET,
    QUIET_ALWAYS   /**< accept, answer, and never send a byte      */
} CasterMode;

typedef struct {
    sock_t     listener;
    sock_t     client;
    int        port;
    CasterMode mode;
    int        fed;       /**< payload writes performed so far     */
    int        answered;  /**< the ICY line has gone out           */
} Caster;

/** @brief Put a socket in non-blocking mode. */
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
 * The port is whatever the OS hands out rather than a fixed number: two
 * cases run in one process here, and a fixed port makes the second fail
 * on a machine that has not finished releasing the first.
 *
 * @return true on success, with @p c->port filled in.
 */
static bool caster_start(Caster *c, CasterMode mode)
{
    memset(c, 0, sizeof(*c));
    c->client = SOCK_BAD;
    c->mode   = mode;

    c->listener = socket(AF_INET, SOCK_STREAM, 0);
    if (c->listener == SOCK_BAD) return false;

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port        = 0;                    /* let the OS choose */

    if (bind(c->listener, (struct sockaddr *)&a, sizeof(a)) != 0) return false;
    if (listen(c->listener, 1) != 0) return false;

    socklen_t alen = sizeof(a);
    if (getsockname(c->listener, (struct sockaddr *)&a, &alen) != 0) return false;
    c->port = ntohs(a.sin_port);

    set_nonblocking(c->listener);
    return true;
}

/**
 * @brief One turn of the caster: accept, answer, and feed or not.
 *
 * Called from the same loop that pumps the session, so no thread is
 * involved and nothing has to be synchronised.
 */
static void caster_service(Caster *c)
{
    if (c->client == SOCK_BAD) {
        c->client = accept(c->listener, NULL, NULL);
        if (c->client == SOCK_BAD) return;
        set_nonblocking(c->client);
        return;
    }

    /* Drain whatever the client sent; the request is of no interest
     * here, but leaving it unread would eventually stall the client. */
    char scratch[512];
    (void)recv(c->client, scratch, sizeof(scratch), 0);

    if (!c->answered) {
        const char *ok = "ICY 200 OK\r\n\r\n";
        send(c->client, ok, (int)strlen(ok), 0);
        c->answered = 1;
        return;
    }

    if (c->mode == QUIET_ALWAYS) return;
    if (c->mode == FEED_THEN_QUIET && c->fed >= 3) return;

    /* Not RTCM, deliberately: what is being measured is bytes off the
     * socket, and the framer's opinion of them must not enter into it. */
    const char *filler = "....";
    send(c->client, filler, (int)strlen(filler), 0);
    c->fed++;
}

static void caster_stop(Caster *c)
{
    if (c->client   != SOCK_BAD) close_sock(c->client);
    if (c->listener != SOCK_BAD) close_sock(c->listener);
}

/* ── Running a session against it ────────────────────────────────── */

typedef struct {
    int  end_reason;      /**< NsEndReason, or -1 while running   */
    int  ended;
    int  warns;           /**< NS_LOG_WARN events seen            */
    char last_warn[160];
} Seen;

static void on_event(const NsEvent *ev, void *user)
{
    Seen *s = (Seen *)user;
    if (ev->type == NS_EV_DISCONNECTED) {
        s->end_reason = ev->u.end.reason;
        s->ended      = 1;
    } else if (ev->type == NS_EV_LOG && ev->u.log.level == NS_LOG_WARN) {
        s->warns++;
        strncpy(s->last_warn, ev->u.log.text, sizeof(s->last_warn) - 1);
        s->last_warn[sizeof(s->last_warn) - 1] = '\0';
    }
}

/**
 * @brief Pump a session against @p c for at most @p budget_ms.
 *
 * Stops early when the session ends.  The budget is what keeps a broken
 * timeout from hanging the suite instead of failing it.
 */
static void run_against(Caster *c, NtripSession *sess, Seen *seen,
                        int budget_ms)
{
    for (int waited = 0; waited < budget_ms && !seen->ended; waited += 20) {
        caster_service(c);
        if (ns_pump(sess, 10) < 0) break;
        sleep_ms(10);
    }
}

/** @brief Options aimed at the loopback caster, with a one-second leash. */
static void options_for(NsOptions *opt, int port, double stall_s)
{
    ns_options_default(opt);
    strncpy(opt->config.NTRIP_CASTER, "127.0.0.1",
            sizeof(opt->config.NTRIP_CASTER) - 1);
    strncpy(opt->config.MOUNTPOINT, "TEST",
            sizeof(opt->config.MOUNTPOINT) - 1);
    opt->config.NTRIP_PORT  = port;
    opt->stats_interval_s   = 0.0;
    opt->auto_reconnect     = false;   /* so the end reason is visible */
    opt->stall_timeout_s    = stall_s;
}

/* ── Cases ───────────────────────────────────────────────────────── */

/** A stream that goes quiet with the socket open must end NS_END_STALLED. */
static void case_goes_quiet(void)
{
    printf("\n-- a caster that stops sending --\n");

    Caster c;
    if (!caster_start(&c, FEED_THEN_QUIET)) {
        check(0, "the loopback caster started");
        return;
    }

    NsOptions opt;
    options_for(&opt, c.port, 1.0);

    Seen seen;
    memset(&seen, 0, sizeof(seen));
    seen.end_reason = -1;

    NtripSession *sess = ns_open(&opt, on_event, &seen);
    check(sess != NULL, "the session opened");
    if (!sess) { caster_stop(&c); return; }

    run_against(&c, sess, &seen, 6000);

    check(c.fed > 0, "the caster sent something before going quiet");
    check(seen.ended, "the session ended rather than waiting forever");
    check(seen.end_reason == NS_END_STALLED,
          "it ended NS_END_STALLED, not EOF: nothing was closed");
    check(seen.warns > 0, "the silence was logged as a warning");
    if (seen.warns)
        printf("      warning: \"%s\"\n", seen.last_warn);

    ns_close(sess);
    caster_stop(&c);
}

/**
 * A caster that accepts and never sends anything is the same fault:
 * the timer runs from the moment the socket is connected, so a stream
 * which never starts is caught by the rule that catches one that stops.
 */
static void case_never_sends(void)
{
    printf("\n-- a caster that never sends at all --\n");

    Caster c;
    if (!caster_start(&c, QUIET_ALWAYS)) {
        check(0, "the loopback caster started");
        return;
    }

    NsOptions opt;
    options_for(&opt, c.port, 1.0);

    Seen seen;
    memset(&seen, 0, sizeof(seen));
    seen.end_reason = -1;

    NtripSession *sess = ns_open(&opt, on_event, &seen);
    if (!sess) { check(0, "the session opened"); caster_stop(&c); return; }

    run_against(&c, sess, &seen, 6000);

    check(seen.ended && seen.end_reason == NS_END_STALLED,
          "a stream that never starts stalls too");

    ns_close(sess);
    caster_stop(&c);
}

/**
 * The half that stops this being a timer that always fires.  A caster
 * that keeps sending must survive well past its own timeout, or the
 * dead-man's switch would kill healthy streams -- a worse fault than
 * the one it exists to catch.
 */
static void case_keeps_sending(void)
{
    printf("\n-- a caster that keeps sending --\n");

    Caster c;
    if (!caster_start(&c, FEED_ALWAYS)) {
        check(0, "the loopback caster started");
        return;
    }

    NsOptions opt;
    options_for(&opt, c.port, 1.0);

    Seen seen;
    memset(&seen, 0, sizeof(seen));
    seen.end_reason = -1;

    NtripSession *sess = ns_open(&opt, on_event, &seen);
    if (!sess) { check(0, "the session opened"); caster_stop(&c); return; }

    run_against(&c, sess, &seen, 3000);   /* three times the timeout */

    check(!seen.ended, "it was still running after three times the timeout");
    check(c.fed > 10, "and the caster had kept feeding it");

    ns_close(sess);
    caster_stop(&c);
}

/** Zero means wait forever, which is what a caller asks for by asking. */
static void case_disabled(void)
{
    printf("\n-- the timeout turned off --\n");

    Caster c;
    if (!caster_start(&c, QUIET_ALWAYS)) {
        check(0, "the loopback caster started");
        return;
    }

    NsOptions opt;
    options_for(&opt, c.port, 0.0);      /* 0 = never give up */

    Seen seen;
    memset(&seen, 0, sizeof(seen));
    seen.end_reason = -1;

    NtripSession *sess = ns_open(&opt, on_event, &seen);
    if (!sess) { check(0, "the session opened"); caster_stop(&c); return; }

    run_against(&c, sess, &seen, 2000);

    check(!seen.ended, "a silent caster is tolerated when the leash is off");

    ns_close(sess);
    caster_stop(&c);
}

/** The default has to be a real number, or every frontend inherits zero. */
static void case_default(void)
{
    printf("\n-- the default --\n");

    NsOptions opt;
    ns_options_default(&opt);
    check(opt.stall_timeout_s > 0.0,
          "a session that was never configured still has a dead-man's switch");
    printf("      default: %.0f s\n", opt.stall_timeout_s);
}

int main(void)
{
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("FAIL: Winsock did not start\n");
        return 1;
    }
#endif

    printf("== stall detection ==\n");

    case_default();
    case_goes_quiet();
    case_never_sends();
    case_keeps_sending();
    case_disabled();

    printf("\n%s\n", failures ? "FAILURES" : "all checks passed");

#ifdef _WIN32
    WSACleanup();
#endif
    return failures ? 1 : 0;
}
