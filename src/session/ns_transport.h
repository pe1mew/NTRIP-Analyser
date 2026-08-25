/**
 * @file ns_transport.h
 * @brief The byte transport beneath the NTRIP client -- the seam.
 *
 * Connect, send, receive, close, as an indirection: the session's
 * stream loop and the sourcetable fetch speak this interface and never
 * a socket, which is what lets an encrypted transport be a second
 * implementation rather than a second code path.  Plain TCP is the
 * first implementation (`ns_transport.c`); TLS arrives behind the same
 * four calls (design/work-items/tls-rollout.md, L3).
 *
 * This is a *client* seam.  The test harnesses' loopback casters are
 * servers and keep their raw sockets: they play the network, they do
 * not use it.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#ifndef NS_TRANSPORT_H
#define NS_TRANSPORT_H

#include "core/ns_failure.h"

#ifdef __cplusplus
extern "C" {
#endif

/** An open connection.  Opaque: what it holds is the implementation's
 *  business, and today that is one socket. */
typedef struct NsTransport NsTransport;

/**
 * @brief Connect to host:port over plain TCP.  NULL on failure.
 *
 * @param fail Set to why it failed, which is the whole point: the
 *             difference between a name that does not resolve and a
 *             port with nothing behind it is the difference between two
 *             fields the user typed, and this is where it is knowable.
 */
NsTransport *ns_transport_connect(const char *host, int port,
                                  NsFailure *fail);

/**
 * @brief Connect to host:port and complete a TLS handshake, or fail.
 *
 * Verification is not optional: the chain is checked against the
 * embedded Mozilla bundle (`ns_ca_bundle.c`) and the certificate must
 * name @p host -- `mbedtls_ssl_set_hostname`, the call the design note
 * makes non-negotiable.  There is no connect-anyway mode, because a
 * measurement tool that shrugs at a wrong certificate is measuring the
 * wrong station for an attacker's convenience.
 *
 * @param fail NS_FAIL_TLS_CERT when the certificate was the problem,
 *             NS_FAIL_TLS_HANDSHAKE when the negotiation itself failed
 *             (commonest cause: a plain-text port), or the plain
 *             connect failures when TCP never got that far.
 * @param why  Optional (may be NULL): receives the specific sentence
 *             for a certificate failure -- expired, wrong host, or
 *             untrusted -- which the session shows in place of the
 *             generic one.  Empty when the generic sentence is right.
 */
NsTransport *ns_transport_connect_tls(const char *host, int port,
                                      NsFailure *fail,
                                      char *why, size_t why_cap);

/**
 * @brief Trust this PEM instead of the embedded bundle.  TESTS ONLY.
 *
 * The loopback TLS caster presents certificates from a toy CA that the
 * real bundle rightly refuses; this is how the test suite points the
 * client at that CA.  NULL restores the embedded bundle.  Process-wide
 * and not thread-safe by design -- a production caller has no business
 * here, which is why it is not in any config.
 */
void ns_transport_set_ca_override(const unsigned char *pem, size_t len);

/**
 * @brief Send the whole buffer.
 * @return >0 on success, <=0 when the connection failed under it.
 */
int ns_transport_send(NsTransport *t, const void *buf, int len);

/**
 * @brief Receive with a timeout.
 *
 * @param timeout_ms  Milliseconds to wait; negative blocks until data
 *                    or close, for a caller that reads a bounded reply
 *                    (the sourcetable fetch) rather than pumping.
 * @return >0 bytes, 0 on timeout (never in blocking mode), <0 on close
 *         or error.
 */
int ns_transport_recv(NsTransport *t, unsigned char *buf, int cap,
                      int timeout_ms);

/** @brief Close and free.  NULL is tolerated. */
void ns_transport_close(NsTransport *t);

#ifdef __cplusplus
}
#endif

#endif /* NS_TRANSPORT_H */
