/**
 * @file ns_failure.h
 * @brief Why a stream could not be opened, and what to say about it.
 *
 * In `core` rather than in `net`, and it took P4.3 to show why: KPI 1
 * has to name the failure, `kpi.c` is core, and core may not reach up
 * into net. The vocabulary belongs where the snapshot that carries it
 * lives.
 *
 * One function stays in `net` -- @ref ns_failure_from_socket, which
 * needs the platform's own error numbers. Everything here is plain C
 * with no sockets in it and no headers to reconcile.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
#ifndef NS_FAILURE_H
#define NS_FAILURE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Why a stream could not be opened, or did not last.
 *
 * GUI v3, P4.1 (`design/guiV3spec.md` §5). Every frontend used to say
 * one sentence for every cause -- the Android app's was *"Could not open
 * the session."* -- while the information needed to say which was
 * computed here and thrown away: `getaddrinfo` failing, `connect`
 * setting `ECONNREFUSED`, a 401 parsed into @ref NsHandshake and never
 * read. This is that information, kept.
 *
 * Deliberately separate from @ref NsEndReason. *How* a session ended and
 * *why it could not run* are different questions: the daemon has been
 * reading end reasons since 3.5.0 and they must not shift underneath it.
 */
typedef enum {
    NS_FAIL_NONE = 0,       /**< nothing has gone wrong                 */
    NS_FAIL_DNS,            /**< the host name does not resolve         */
    NS_FAIL_REFUSED,        /**< TCP reset: nothing is listening there  */
    NS_FAIL_UNREACHABLE,    /**< no route, or the network is down       */
    NS_FAIL_TIMEOUT,        /**< the SYN went out; nothing came back    */
    NS_FAIL_NOT_NTRIP,      /**< it answered, but not as a caster       */
    NS_FAIL_AUTH,           /**< 401 -- user name or password           */
    NS_FAIL_FORBIDDEN,      /**< 403 -- known user, not this mountpoint */
    NS_FAIL_NO_MOUNTPOINT,  /**< 404, or a sourcetable came back        */
    NS_FAIL_BUSY,           /**< 409 / 503 -- full, or restarting       */
    NS_FAIL_REJECTED,       /**< refused in a way with no sentence yet  */
    NS_FAIL_DROPPED,        /**< it worked, then the peer closed        */
    NS_FAIL_STALLED,        /**< open and silent -- 3.5.0's stall check */
    NS_FAIL_TLS_HANDSHAKE,  /**< no TLS there, or negotiation failed    */
    NS_FAIL_TLS_CERT,       /**< expired, wrong host, or untrusted      */
} NsFailure;

/**
 * @brief Classify a socket error.
 *
 * The one place the platforms are reconciled: POSIX `errno` and the
 * `WSAE*` numbers mean the same things by different names, and a
 * frontend that mapped them itself would be a second opinion about what
 * a connection failure is.
 *
 * @param err `errno` on POSIX, `WSAGetLastError()` on Windows.
 */
NsFailure ns_failure_from_socket(int err);

/**
 * @brief Classify a caster's answer.
 *
 * @param valid  Whether a status line was recognised at all.
 * @param status The HTTP status, or 0.
 * @param content_type The `Content-Type` header, or "". A sourcetable
 *        where a stream was asked for is how NTRIP 1 casters say the
 *        mountpoint does not exist, and it arrives as a **200**.
 */
NsFailure ns_failure_from_response(bool valid, int status,
                                   const char *content_type);

/**
 * @brief A stable token for a failure -- for tests, logs and the CSV.
 *
 * Never shown to a user; @ref ns_failure_text is what a user reads.
 */
const char *ns_failure_name(NsFailure f);

/**
 * @brief The sentence a user reads, in English, with the detail filled in.
 *
 * The core's wording, so the CLI, the GUI and the daemon all say the
 * same thing about the same fault. The Android app maps the code to its
 * own `strings.xml` instead, which is what leaves room for translation.
 *
 * @return the number of characters written, or 0 for @ref NS_FAIL_NONE.
 */
int ns_failure_text(char *buf, size_t cap, NsFailure f,
                    const char *host, int port, const char *mountpoint);

/**
 * @brief The same failure in a clause, for a table cell.
 *
 * KPI 1's detail is read in the Win32 GUI's Detail column, about sixty
 * characters wide, and in the CLI's table. @ref ns_failure_text says
 * what to check as well, which is right for a message with a screen to
 * itself and too long for a row in a list. Both come from one switch
 * each, in one file, so they cannot describe different faults.
 */
const char *ns_failure_short(NsFailure f);

#ifdef __cplusplus
}
#endif

#endif /* NS_FAILURE_H */
