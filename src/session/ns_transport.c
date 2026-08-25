/**
 * @file ns_transport.c
 * @brief Plain TCP, the first implementation of the transport seam.
 *
 * The only file in the client path that touches a socket API.  The
 * platform branches that used to sit in `ntrip_session.c` moved here
 * whole -- resolve-and-try-each-address connect, select-based receive,
 * the Windows error read before close -- so the session above became
 * transport-agnostic without any of them changing.
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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
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

struct NsTransport {
    ns_sock_t sock;
};

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

NsTransport *ns_transport_connect(const char *host, int port,
                                  NsFailure *fail)
{
    if (fail) *fail = NS_FAIL_NONE;
    if (!sock_startup()) {
        if (fail) *fail = NS_FAIL_UNREACHABLE;
        return NULL;
    }

    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;      /* IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) {
        if (fail) *fail = NS_FAIL_DNS;
        return NULL;
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
    if (sk == NS_INVALID_SOCK) {
        if (fail) *fail = ns_failure_from_socket(last_err);
        return NULL;
    }

    NsTransport *t = (NsTransport *)calloc(1, sizeof(NsTransport));
    if (!t) {
        closesocket(sk);
        if (fail) *fail = NS_FAIL_UNREACHABLE;
        return NULL;
    }
    t->sock = sk;
    return t;
}

int ns_transport_send(NsTransport *t, const void *buf, int len)
{
    if (!t) return -1;
    return (int)send(t->sock, (const char *)buf, len, 0);
}

int ns_transport_recv(NsTransport *t, unsigned char *buf, int cap,
                      int timeout_ms)
{
    if (!t) return -1;

    if (timeout_ms >= 0) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(t->sock, &rd);

        struct timeval tv;
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int r = select((int)t->sock + 1, &rd, NULL, NULL, &tv);
        if (r == 0)  return 0;
        if (r < 0)   return -1;
    }

    int n = (int)recv(t->sock, (char *)buf, cap, 0);
    if (n == 0) return -1;      /* peer closed */
    if (n < 0)  return -1;
    return n;
}

void ns_transport_close(NsTransport *t)
{
    if (!t) return;
    closesocket(t->sock);
    free(t);
}
