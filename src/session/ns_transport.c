/**
 * @file ns_transport.c
 * @brief Both implementations of the transport seam: plain TCP and TLS.
 *
 * The only file in the client path that touches a socket API, and now
 * the only one that touches mbedTLS.  The platform branches that used
 * to sit in `ntrip_session.c` moved here whole -- resolve-and-try-each-
 * address connect, select-based receive, the Windows error read before
 * close -- and TLS arrived behind the same four calls, which is the
 * whole point of the seam (design/work-items/tls-rollout.md, L3).
 *
 * Verification is mandatory.  The chain is checked against the embedded
 * Mozilla bundle, the hostname against the certificate; there is no
 * connect-anyway mode.  A failed connection tells the caller *which* of
 * the two TLS faults it was, because "check the port" and "distrust the
 * caster" are different advice.
 *
 * Winsock startup happens here, once, and is never cleaned up: the OS
 * reclaims it at exit, and a WSACleanup from one caller would pull the
 * stack out from under every other connection in the process.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "session/ns_transport.h"
#include "session/ns_ca_bundle.h"


#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mbedtls/ssl.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"  /* the BIO error codes alone */

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <bcrypt.h>
  typedef SOCKET ns_sock_t;
  #define NS_INVALID_SOCK INVALID_SOCKET
#else
  #include <sys/socket.h>
  #include <sys/select.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  #include <errno.h>
  typedef int ns_sock_t;
  #define NS_INVALID_SOCK (-1)
  #define closesocket(s) close(s)
#endif

/** How long a silent peer may sit inside the TLS handshake before the
 *  attempt is called failed.  Generous: a healthy handshake is
 *  round-trips, not seconds.  A plain-text port usually fails far
 *  faster -- its bytes are not a TLS record. */
#define NS_TLS_HANDSHAKE_TIMEOUT_MS 10000

struct NsTransport {
    ns_sock_t sock;
    bool      tls;

    /* How long the BIO recv callback waits inside mbedTLS: a deadline
     * during the handshake, none (0) afterwards, because the session's
     * own select in ns_transport_recv is the pacing and a blocking read
     * inside a half-received record would defeat the stall detector
     * above it. */
    int       bio_timeout_ms;

    mbedtls_ctr_drbg_context drbg;
    mbedtls_ssl_config       conf;
    mbedtls_ssl_context      ssl;
    mbedtls_x509_crt         ca;
};

/* Tests only -- see the header.  Points the client at the toy CA the
 * loopback caster's certificates chain to. */
static const unsigned char *g_ca_override     = NULL;
static size_t               g_ca_override_len = 0;

void ns_transport_set_ca_override(const unsigned char *pem, size_t len)
{
    g_ca_override     = pem;
    g_ca_override_len = len;
}

static bool sock_startup(void)
{
#ifdef _WIN32
    static bool done = false;
    if (done) return true;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
    done = true;
#endif
    return true;
}

/**
 * @brief Resolve and connect, trying each address.  The raw half of
 *        both connect entry points.
 *
 * @param fail Set to why it failed, which is the whole point: the
 *             difference between a name that does not resolve and a
 *             port with nothing behind it is the difference between two
 *             fields the user typed, and this is where it is knowable.
 */
static ns_sock_t raw_connect(const char *host, int port, NsFailure *fail)
{
    if (!sock_startup()) {
        if (fail) *fail = NS_FAIL_UNREACHABLE;
        return NS_INVALID_SOCK;
    }

    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;      /* IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) {
        if (fail) *fail = NS_FAIL_DNS;
        return NS_INVALID_SOCK;
    }

    ns_sock_t sk = NS_INVALID_SOCK;
    int last_err = 0;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        sk = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sk == NS_INVALID_SOCK) continue;
        if (connect(sk, ai->ai_addr, (int)ai->ai_addrlen) == 0) break;
        /* Read before closing: closesocket() clobbers the error on
         * Windows, which is how the reason for a refusal disappears. */
#ifdef _WIN32
        last_err = WSAGetLastError();
#else
        last_err = errno;
#endif
        closesocket(sk);
        sk = NS_INVALID_SOCK;
    }
    freeaddrinfo(res);
    if (sk == NS_INVALID_SOCK && fail)
        *fail = ns_failure_from_socket(last_err);
    return sk;
}

/** @brief Wait until the socket is readable.  1 ready, 0 timeout, -1 error. */
static int wait_readable(ns_sock_t sk, int timeout_ms)
{
    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(sk, &rd);

    struct timeval tv;
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    return select((int)sk + 1, &rd, NULL, NULL, &tv);
}

NsTransport *ns_transport_connect(const char *host, int port,
                                  NsFailure *fail)
{
    if (fail) *fail = NS_FAIL_NONE;

    ns_sock_t sk = raw_connect(host, port, fail);
    if (sk == NS_INVALID_SOCK) return NULL;

    NsTransport *t = (NsTransport *)calloc(1, sizeof(NsTransport));
    if (!t) {
        closesocket(sk);
        if (fail) *fail = NS_FAIL_UNREACHABLE;
        return NULL;
    }
    t->sock = sk;
    return t;
}

/* ── TLS ─────────────────────────────────────────────────────────── */

/**
 * @brief mbedTLS BIO callbacks over our socket.
 *
 * The recv side honours @ref NsTransport::bio_timeout_ms: returning
 * WANT_READ instead of blocking is what keeps the session's pump (and
 * its stall detector) in charge of time.
 */
static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    NsTransport *t = (NsTransport *)ctx;
    int n = (int)send(t->sock, (const char *)buf, (int)len, 0);
    if (n <= 0) return MBEDTLS_ERR_NET_SEND_FAILED;
    return n;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    NsTransport *t = (NsTransport *)ctx;

    if (t->bio_timeout_ms >= 0) {
        int r = wait_readable(t->sock, t->bio_timeout_ms);
        if (r == 0)  {                       return MBEDTLS_ERR_SSL_WANT_READ; }
        if (r < 0)   return MBEDTLS_ERR_NET_RECV_FAILED;
    }

    int n = (int)recv(t->sock, (char *)buf, (int)len, 0);
    if (n == 0)  return 0;                        /* peer closed */
    if (n < 0)   return MBEDTLS_ERR_NET_RECV_FAILED;
    return n;
}

/** @brief The specific certificate sentence, from the verify flags. */
static void cert_why(uint32_t flags, const char *host,
                     char *why, size_t why_cap)
{
    if (!why || why_cap == 0) return;
    if (flags & MBEDTLS_X509_BADCERT_EXPIRED)
        snprintf(why, why_cap, "The caster's certificate has expired.");
    else if (flags & MBEDTLS_X509_BADCERT_FUTURE)
        snprintf(why, why_cap,
                 "The caster's certificate is not valid yet. Check the "
                 "device clock.");
    else if (flags & MBEDTLS_X509_BADCERT_CN_MISMATCH)
        snprintf(why, why_cap,
                 "The caster's certificate is not for \"%s\".", host);
    else if (flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED)
        snprintf(why, why_cap,
                 "The caster's certificate is not signed by a trusted "
                 "authority.");
    /* Anything else keeps the generic sentence. */
}

/**
 * @brief Entropy for the DRBG, read from the operating system directly.
 *
 * Not mbedtls_entropy_func, deliberately: its accumulator blocked
 * without returning on an EMUI 10 handset (L5, found live -- the seed
 * call simply never came back), and everything it adds on top of the
 * OS RNG is machinery this client does not need.  The OS pool is the
 * root of trust either way.
 */
static int os_entropy(void *data, unsigned char *out, size_t len)
{
    (void)data;
#ifdef _WIN32
    return BCryptGenRandom(NULL, out, (ULONG)len,
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0
           ? 0 : MBEDTLS_ERR_CTR_DRBG_ENTROPY_SOURCE_FAILED;
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return MBEDTLS_ERR_CTR_DRBG_ENTROPY_SOURCE_FAILED;
    size_t got = fread(out, 1, len, f);
    fclose(f);
    return got == len ? 0 : MBEDTLS_ERR_CTR_DRBG_ENTROPY_SOURCE_FAILED;
#endif
}

static void tls_free(NsTransport *t)
{
    mbedtls_ssl_free(&t->ssl);
    mbedtls_ssl_config_free(&t->conf);
    mbedtls_x509_crt_free(&t->ca);
    mbedtls_ctr_drbg_free(&t->drbg);
}

NsTransport *ns_transport_connect_tls(const char *host, int port,
                                      NsFailure *fail,
                                      char *why, size_t why_cap)
{
    if (why && why_cap) why[0] = 0;
    if (fail) *fail = NS_FAIL_NONE;

    ns_sock_t sk = raw_connect(host, port, fail);
    if (sk == NS_INVALID_SOCK) return NULL;

    NsTransport *t = (NsTransport *)calloc(1, sizeof(NsTransport));
    if (!t) {
        closesocket(sk);
        if (fail) *fail = NS_FAIL_UNREACHABLE;
        return NULL;
    }
    t->sock = sk;
    t->tls  = true;
    t->bio_timeout_ms = NS_TLS_HANDSHAKE_TIMEOUT_MS;

    mbedtls_ctr_drbg_init(&t->drbg);
    mbedtls_ssl_config_init(&t->conf);
    mbedtls_ssl_init(&t->ssl);
    mbedtls_x509_crt_init(&t->ca);

    const unsigned char *pem = g_ca_override ? g_ca_override
                                             : ns_ca_bundle_pem;
    size_t pem_len = g_ca_override ? g_ca_override_len
                                   : (size_t)ns_ca_bundle_pem_len;

    static const char pers[] = "ntrip-analyser";
    int rc = mbedtls_ctr_drbg_seed(&t->drbg, os_entropy, NULL,
                                   (const unsigned char *)pers,
                                   sizeof(pers) - 1);
    if (rc == 0)
        rc = mbedtls_ssl_config_defaults(&t->conf, MBEDTLS_SSL_IS_CLIENT,
                                         MBEDTLS_SSL_TRANSPORT_STREAM,
                                         MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc == 0) {
        /* A bundle parse may skip roots with algorithms this build does
         * not carry; that is a positive return and harmless.  Only a
         * negative -- nothing usable -- is fatal. */
        rc = mbedtls_x509_crt_parse(&t->ca, pem, pem_len);
        if (rc > 0) rc = 0;
    }
    if (rc == 0) {
        mbedtls_ssl_conf_authmode(&t->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&t->conf, &t->ca, NULL);
        mbedtls_ssl_conf_rng(&t->conf, mbedtls_ctr_drbg_random, &t->drbg);
        rc = mbedtls_ssl_setup(&t->ssl, &t->conf);
    }
    if (rc == 0)
        rc = mbedtls_ssl_set_hostname(&t->ssl, host);
    if (rc != 0) {
        /* Setup failures are ours, not the caster's; handshake is the
         * nearest honest word the vocabulary has. */
        if (fail) *fail = NS_FAIL_TLS_HANDSHAKE;
        tls_free(t);
        closesocket(sk);
        free(t);
        return NULL;
    }

    mbedtls_ssl_set_bio(&t->ssl, t, bio_send, bio_recv, NULL);

    while ((rc = mbedtls_ssl_handshake(&t->ssl)) != 0) {
        /* WANT_READ here means the BIO's handshake deadline passed with
         * the peer silent -- a hang, reported as a failed handshake
         * rather than lived with. */
        if (rc == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        break;
    }

    if (rc != 0) {
        if (rc == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
            if (fail) *fail = NS_FAIL_TLS_CERT;
            cert_why(mbedtls_ssl_get_verify_result(&t->ssl), host,
                     why, why_cap);
        } else {
            if (fail) *fail = NS_FAIL_TLS_HANDSHAKE;
        }
        tls_free(t);
        closesocket(sk);
        free(t);
        return NULL;
    }

    /* Data phase: reads inside mbedTLS must not block.  The session's
     * own select paces the stream, and the stall detector above it must
     * keep seeing time pass. */
    t->bio_timeout_ms = 0;
    return t;
}

/* ── The four calls ──────────────────────────────────────────────── */

int ns_transport_send(NsTransport *t, const void *buf, int len)
{
    if (!t) return -1;

    if (!t->tls)
        return (int)send(t->sock, (const char *)buf, len, 0);

    int done = 0;
    while (done < len) {
        int n = mbedtls_ssl_write(&t->ssl,
                                  (const unsigned char *)buf + done,
                                  (size_t)(len - done));
        if (n == MBEDTLS_ERR_SSL_WANT_READ ||
            n == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (n <= 0) return -1;
        done += n;
    }
    return done;
}

int ns_transport_recv(NsTransport *t, unsigned char *buf, int cap,
                      int timeout_ms)
{
    if (!t) return -1;

    if (!t->tls) {
        if (timeout_ms >= 0) {
            int r = wait_readable(t->sock, timeout_ms);
            if (r == 0)  return 0;
            if (r < 0)   return -1;
        }
        int n = (int)recv(t->sock, (char *)buf, cap, 0);
        if (n == 0) return -1;      /* peer closed */
        if (n < 0)  return -1;
        return n;
    }

    /* TLS.  Decrypted bytes may already be buffered from an earlier
     * record; only when there are none does the socket get a say. */
    if (mbedtls_ssl_get_bytes_avail(&t->ssl) == 0) {
        if (timeout_ms >= 0) {
            int r = wait_readable(t->sock, timeout_ms);
            if (r == 0)  return 0;
            if (r < 0)   return -1;
            t->bio_timeout_ms = 0;
        } else {
            /* Blocking caller (the sourcetable fetch): let the BIO
             * block too. */
            t->bio_timeout_ms = -1;
        }
    }

    int n = mbedtls_ssl_read(&t->ssl, buf, (size_t)cap);
    t->bio_timeout_ms = 0;
    if (n == MBEDTLS_ERR_SSL_WANT_READ ||
        n == MBEDTLS_ERR_SSL_WANT_WRITE)
        return 0;                   /* a record still in flight */
    if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return -1;
    if (n <= 0) return -1;
    return n;
}

void ns_transport_close(NsTransport *t)
{
    if (!t) return;
    if (t->tls) {
        /* Best effort: the notify is politeness, not a requirement, and
         * a dead peer must not stop the close. */
        mbedtls_ssl_close_notify(&t->ssl);
        tls_free(t);
    }
    closesocket(t->sock);
    free(t);
}
