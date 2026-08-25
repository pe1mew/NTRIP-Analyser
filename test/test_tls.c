/**
 * @file test_tls.c
 * @brief The valuable TLS tests are the ones where it must fail.
 *
 * A TLS client that connects to a good caster proves almost nothing:
 * every broken client connects happily.  What distinguishes a client
 * that protects its user is refusing -- the expired certificate, the
 * certificate for somebody else's host, the certificate nobody signed,
 * and the plain-text port answering where TLS was demanded.  So the
 * caster here is a real TLS server on the loopback interface, on its
 * own thread like `test_bridge_vrs`'s, presenting the committed
 * fixtures from `test/data/tls/` one bad certificate at a time; the
 * client is the real session, told to use TLS the way every frontend
 * will tell it.
 *
 * Each refusal must land on the right code -- NS_FAIL_TLS_CERT points
 * at the caster, NS_FAIL_TLS_HANDSHAKE at the port -- with the right
 * sentence, because "distrust the caster" and "check the port" are
 * different advice.
 *
 * The client trusts the toy CA through ns_transport_set_ca_override(),
 * the test-only hook; the embedded Mozilla bundle rightly refuses it.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
#include "session/ntrip_session.h"
#include "session/ns_transport.h"
#include "core/config.h"
#include "core/rtcm3x_parser.h"   /* crc24q, for frames worth counting */

#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#  include <pthread.h>
#  include <time.h>
#  include <signal.h>
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

static const char *g_dir;   /* test/data/tls, from argv[1] */

static const char *fixture(const char *name)
{
    static char path[512];
    snprintf(path, sizeof(path), "%s/%s", g_dir, name);
    return path;
}

/* ── A TLS caster on its own thread ──────────────────────────────── */

typedef struct {
    const char  *crt;      /**< certificate file, or NULL for plain TCP */
    const char  *key;
    int          chunked;  /**< speak NTRIP 2: HTTP/1.1 + chunked      */
    int          port;     /**< filled in once listening               */
    volatile int ready;
    volatile int stop;
} Srv;

/** @brief One CRC-valid RTCM frame of the given type; returns its length. */
static int make_frame(unsigned char *out, int type, int paylen)
{
    out[0] = 0xD3;
    out[1] = (unsigned char)((paylen >> 8) & 0x03);
    out[2] = (unsigned char)(paylen & 0xFF);
    memset(out + 3, 0, (size_t)paylen);
    out[3] = (unsigned char)((type >> 4) & 0xFF);
    out[4] = (unsigned char)((type & 0x0F) << 4);
    uint32_t c = crc24q(out, (size_t)(3 + paylen));
    out[3 + paylen]     = (unsigned char)((c >> 16) & 0xFF);
    out[3 + paylen + 1] = (unsigned char)((c >>  8) & 0xFF);
    out[3 + paylen + 2] = (unsigned char)(c & 0xFF);
    return 3 + paylen + 3;
}

static Srv g_srv;

/** @brief Blocking BIO callbacks: the server has a thread to itself. */
static int srv_send(void *ctx, const unsigned char *buf, size_t len)
{
    sock_t *sk = (sock_t *)ctx;
    int n = (int)send(*sk, (const char *)buf, (int)len, 0);
    return n > 0 ? n : -1;
}

static int srv_recv(void *ctx, unsigned char *buf, size_t len)
{
    sock_t *sk = (sock_t *)ctx;
    int n = (int)recv(*sk, (char *)buf, (int)len, 0);
    return n >= 0 ? n : -1;
}

#ifdef _WIN32
static DWORD WINAPI server_thread(LPVOID arg)
#else
static void *server_thread(void *arg)
#endif
{
    Srv *s = (Srv *)arg;

    sock_t lst = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port        = 0;
    bind(lst, (struct sockaddr *)&a, sizeof(a));
    listen(lst, 1);
    socklen_t alen = sizeof(a);
    getsockname(lst, (struct sockaddr *)&a, &alen);
    s->port  = ntohs(a.sin_port);
    s->ready = 1;

    sock_t cl = accept(lst, NULL, NULL);
    if (cl == SOCK_BAD) { close_sock(lst); return 0; }

    static const char icy[] = "ICY 200 OK\r\n\r\n";
    static const char junk[] = "not RTCM, and that is fine: what is "
                               "measured here is bytes through TLS.";

    if (!s->crt) {
        /* The downgrade caster: answers in plain text where the client
         * demanded TLS.  Its bytes are not a TLS record. */
        char scratch[512];
        send(cl, icy, (int)strlen(icy), 0);
        (void)recv(cl, scratch, sizeof(scratch), 0);
        sleep_ms(200);
    } else {
        mbedtls_entropy_context  entropy;
        mbedtls_ctr_drbg_context drbg;
        mbedtls_ssl_config       conf;
        mbedtls_ssl_context      ssl;
        mbedtls_x509_crt         crt;
        mbedtls_pk_context       key;

        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&drbg);
        mbedtls_ssl_config_init(&conf);
        mbedtls_ssl_init(&ssl);
        mbedtls_x509_crt_init(&crt);
        mbedtls_pk_init(&key);

        static const char pers[] = "test-tls-caster";
        int rc = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func,
                                       &entropy,
                                       (const unsigned char *)pers,
                                       sizeof(pers) - 1);
        if (rc == 0) rc = mbedtls_x509_crt_parse_file(&crt, s->crt);
        if (rc == 0) rc = mbedtls_pk_parse_keyfile(&key, s->key, NULL,
                                        mbedtls_ctr_drbg_random, &drbg);
        if (rc == 0) rc = mbedtls_ssl_config_defaults(&conf,
                                        MBEDTLS_SSL_IS_SERVER,
                                        MBEDTLS_SSL_TRANSPORT_STREAM,
                                        MBEDTLS_SSL_PRESET_DEFAULT);
        if (rc == 0) {
            mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
            rc = mbedtls_ssl_conf_own_cert(&conf, &crt, &key);
        }
        if (rc == 0) rc = mbedtls_ssl_setup(&ssl, &conf);
        if (rc == 0) {
            mbedtls_ssl_set_bio(&ssl, &cl, srv_send, srv_recv, NULL);
            do {
                rc = mbedtls_ssl_handshake(&ssl);
            } while (rc == MBEDTLS_ERR_SSL_WANT_READ ||
                     rc == MBEDTLS_ERR_SSL_WANT_WRITE);
        }

        if (rc == 0 && s->chunked) {
            /* The Kadaster shape, found live at L5: NTRIP 2 over TLS,
             * the RTCM wrapped in Transfer-Encoding: chunked -- with
             * the chunk boundaries deliberately landing mid-frame, so
             * only a client that strips the framing sees valid CRCs. */
            static const char http[] =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: gnss/data\r\n"
                "Transfer-Encoding: chunked\r\n"
                "Ntrip-Version: Ntrip/2.0\r\n\r\n";
            mbedtls_ssl_write(&ssl, (const unsigned char *)http,
                              strlen(http));

            unsigned char stream[512];
            int total = 0;
            for (int i = 0; i < 8; i++)
                total += make_frame(stream + total, 1005, 24);

            int off = 0;
            while (off < total && !s->stop) {
                int piece = 23;               /* never a frame boundary */
                if (off + piece > total) piece = total - off;
                char head[16];
                snprintf(head, sizeof(head), "%x\r\n", piece);
                mbedtls_ssl_write(&ssl, (const unsigned char *)head,
                                  strlen(head));
                mbedtls_ssl_write(&ssl, stream + off, (size_t)piece);
                mbedtls_ssl_write(&ssl, (const unsigned char *)"\r\n", 2);
                off += piece;
                sleep_ms(20);
            }
            /* Hold the connection until the client has read it all;
             * the 0-chunk would otherwise race the last frames. */
            while (!s->stop) sleep_ms(20);
            mbedtls_ssl_write(&ssl, (const unsigned char *)"0\r\n\r\n", 5);
            mbedtls_ssl_close_notify(&ssl);
        } else if (rc == 0) {
            /* The good case: a caster, but encrypted.  Serve until the
             * test says stop, so the client can prove data flows. */
            mbedtls_ssl_write(&ssl, (const unsigned char *)icy,
                              strlen(icy));
            while (!s->stop) {
                if (mbedtls_ssl_write(&ssl, (const unsigned char *)junk,
                                      strlen(junk)) < 0) break;
                sleep_ms(50);
            }
            mbedtls_ssl_close_notify(&ssl);
        }
        /* A refused handshake needs nothing more: refusal was the
         * service being provided. */

        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&conf);
        mbedtls_x509_crt_free(&crt);
        mbedtls_pk_free(&key);
        mbedtls_ctr_drbg_free(&drbg);
        mbedtls_entropy_free(&entropy);
    }

    close_sock(cl);
    close_sock(lst);
    return 0;
}

/* ── Running the real session against it ─────────────────────────── */

typedef struct {
    volatile int ended;
} Watch;

static void on_event(const NsEvent *ev, void *user)
{
    Watch *w = (Watch *)user;
    if (ev->type == NS_EV_DISCONNECTED) w->ended = 1;
}

/**
 * @brief One case: serve `crt`/`key` (NULL = plain), connect with TLS,
 *        and return the session for the caller's assertions.
 *
 * The failure code and detail are copied out before close because the
 * snapshot dies with the session.
 */
static void run_case(const char *crt, const char *key, int chunked,
                     int *out_fail, char *out_detail, size_t detail_cap,
                     int *out_got_bytes,
                     uint64_t *out_frames_ok, uint64_t *out_crc_err)
{
    memset(&g_srv, 0, sizeof(g_srv));
    g_srv.crt     = crt;
    g_srv.key     = key;
    g_srv.chunked = chunked;

#ifdef _WIN32
    HANDLE th = CreateThread(NULL, 0, server_thread, &g_srv, 0, NULL);
#else
    pthread_t th;
    pthread_create(&th, NULL, server_thread, &g_srv);
#endif
    while (!g_srv.ready) sleep_ms(10);

    NsOptions opt;
    ns_options_default(&opt);
    snprintf(opt.config.NTRIP_CASTER, sizeof(opt.config.NTRIP_CASTER),
             "localhost");
    opt.config.NTRIP_PORT = g_srv.port;
    snprintf(opt.config.MOUNTPOINT, sizeof(opt.config.MOUNTPOINT),
             "TEST");
    opt.config.TLS     = true;
    opt.auto_reconnect = false;

    Watch w;
    memset(&w, 0, sizeof(w));
    NtripSession *s = ns_open(&opt, on_event, &w);

    /* Pump until the session either fails or demonstrably streams. */
    int deadline_ms = 15000;
    while (!w.ended && deadline_ms > 0) {
        ns_pump(s, 100);
        deadline_ms -= 100;
        const NsStatsSnapshot *st = ns_stats(s);
        if (!st) continue;
        if (chunked) { if (st->frames_ok >= 6) break; }
        else         { if (st->bytes_total > 200) break; }
    }

    const NsStatsSnapshot *st = ns_stats(s);
    *out_fail      = st ? st->failure : -1;
    *out_got_bytes = st && st->bytes_total > 0;
    if (out_frames_ok) *out_frames_ok = st ? st->frames_ok : 0;
    if (out_crc_err)   *out_crc_err   = st ? st->frames_crc_error : 0;
    snprintf(out_detail, detail_cap, "%s", st ? st->failure_detail : "");

    g_srv.stop = 1;
    ns_close(s);
#ifdef _WIN32
    WaitForSingleObject(th, 5000);
    CloseHandle(th);
#else
    pthread_join(th, NULL);
#endif
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: test_tls <test/data/tls>\n");
        return 2;
    }
    g_dir = argv[1];

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#else
    /* The server thread writes to clients that are refusing it -- that
     * is the test working.  The transport sends with MSG_NOSIGNAL; the
     * server side here writes through mbedTLS's own BIO too, so the
     * process-wide default must not be death. */
    signal(SIGPIPE, SIG_IGN);
#endif

    /* Trust the toy CA, exactly as the header warns nobody else to. */
    static unsigned char ca_pem[8192];
    {
        FILE *f = fopen(fixture("ca.crt"), "rb");
        if (!f) { fprintf(stderr, "no fixtures at %s\n", g_dir); return 2; }
        size_t n = fread(ca_pem, 1, sizeof(ca_pem) - 1, f);
        fclose(f);
        ca_pem[n] = 0;
        ns_transport_set_ca_override(ca_pem, n + 1);
    }

    /* The flag rides the shared config, through both readers -- the
     * round-trip every frontend depends on.  The third case seeds the
     * struct with garbage first: absent flags must mean plain text,
     * not whatever the memory held. */
    {
        NTRIP_Config cfg;
        FILE *f;

        memset(&cfg, 0, sizeof(cfg));
        f = fopen("test_tls_cfg.json", "w");
        fputs("{\"mountpoints\":[{\"caster\":\"c\",\"port\":443,"
              "\"mountpoint\":\"M\",\"tls\":true,\"eph_caster\":\"e\","
              "\"eph_port\":443,\"eph_tls\":true}]}", f);
        fclose(f);
        check(load_config("test_tls_cfg.json", &cfg) == 0,
              "the array-format config loads");
        check(cfg.TLS && cfg.EPH_TLS,
              "the array reader carries tls and eph_tls");
        remove("test_tls_cfg.json");

        memset(&cfg, 0, sizeof(cfg));
        f = fopen("test_tls_cfg.json", "w");
        fputs("{\"NTRIP_CASTER\":\"c\",\"NTRIP_PORT\":443,"
              "\"MOUNTPOINT\":\"M\",\"TLS\":true,\"EPH_TLS\":true}", f);
        fclose(f);
        check(load_config("test_tls_cfg.json", &cfg) == 0,
              "the legacy config loads");
        check(cfg.TLS && cfg.EPH_TLS,
              "the legacy reader carries TLS and EPH_TLS");
        remove("test_tls_cfg.json");

        memset(&cfg, 0xff, sizeof(cfg));
        f = fopen("test_tls_cfg.json", "w");
        fputs("{\"mountpoints\":[{\"caster\":\"c\",\"port\":2101,"
              "\"mountpoint\":\"M\"}]}", f);
        fclose(f);
        check(load_config("test_tls_cfg.json", &cfg) == 0,
              "a config from before the flag loads");
        check(!cfg.TLS && !cfg.EPH_TLS,
              "absent flags mean plain text, not leftover memory");
        remove("test_tls_cfg.json");
    }

    int fail, got_bytes;
    char detail[NS_FAILURE_LEN];

    /* The positive case: a good certificate for this host, and the
     * stream flows through the encryption. */
    char crt[512], key[512];
    snprintf(crt, sizeof(crt), "%s", fixture("good.crt"));
    snprintf(key, sizeof(key), "%s", fixture("good.key"));
    run_case(crt, key, 0, &fail, detail, sizeof(detail), &got_bytes,
             NULL, NULL);
    check(fail == NS_FAIL_NONE, "good chain: no failure recorded");
    check(got_bytes,            "good chain: bytes arrive through TLS");

    /* NTRIP 2 over TLS: chunked transfer, boundaries mid-frame.  The
     * shape Kadaster's 443 serves, committed to the harness the day it
     * was found costing a fifth of the frames their CRCs. */
    uint64_t frames_ok = 0, crc_err = 0;
    snprintf(crt, sizeof(crt), "%s", fixture("good.crt"));
    snprintf(key, sizeof(key), "%s", fixture("good.key"));
    run_case(crt, key, 1, &fail, detail, sizeof(detail), &got_bytes,
             &frames_ok, &crc_err);
    check(fail == NS_FAIL_NONE,  "chunked: no failure recorded");
    check(frames_ok >= 6,        "chunked: the frames reassemble across chunk cuts");
    check(crc_err == 0,          "chunked: not one CRC paid for the framing");

    /* Expired. */
    snprintf(crt, sizeof(crt), "%s", fixture("expired.crt"));
    snprintf(key, sizeof(key), "%s", fixture("expired.key"));
    run_case(crt, key, 0, &fail, detail, sizeof(detail), &got_bytes,
             NULL, NULL);
    check(fail == NS_FAIL_TLS_CERT,      "expired: classifies as the certificate");
    check(strstr(detail, "expired") != NULL,
          "expired: the sentence says expired");
    check(!got_bytes,                    "expired: not a byte crossed");

    /* Somebody else's certificate. */
    snprintf(crt, sizeof(crt), "%s", fixture("wronghost.crt"));
    snprintf(key, sizeof(key), "%s", fixture("wronghost.key"));
    run_case(crt, key, 0, &fail, detail, sizeof(detail), &got_bytes,
             NULL, NULL);
    check(fail == NS_FAIL_TLS_CERT,      "wrong host: classifies as the certificate");
    check(strstr(detail, "is not for \"localhost\"") != NULL,
          "wrong host: the sentence names the host asked for");
    check(!got_bytes,                    "wrong host: not a byte crossed");

    /* Nobody signed it. */
    snprintf(crt, sizeof(crt), "%s", fixture("selfsigned.crt"));
    snprintf(key, sizeof(key), "%s", fixture("selfsigned.key"));
    run_case(crt, key, 0, &fail, detail, sizeof(detail), &got_bytes,
             NULL, NULL);
    check(fail == NS_FAIL_TLS_CERT,      "self-signed: classifies as the certificate");
    check(strstr(detail, "trusted authority") != NULL,
          "self-signed: the sentence says untrusted");
    check(!got_bytes,                    "self-signed: not a byte crossed");

    /* The downgrade: plain text answering where TLS was demanded. */
    run_case(NULL, NULL, 0, &fail, detail, sizeof(detail), &got_bytes,
             NULL, NULL);
    check(fail == NS_FAIL_TLS_HANDSHAKE, "plain-text port: classifies as the handshake");
    check(strstr(detail, "TLS handshake") != NULL,
          "plain-text port: the sentence points at the port");
    check(!got_bytes,                    "plain-text port: not a byte crossed");

    ns_transport_set_ca_override(NULL, 0);

    printf(failures ? "%d FAILURE(S)\n" : "all green\n", failures);
    return failures ? 1 : 0;
}
