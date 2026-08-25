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
