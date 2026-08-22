/**
 * @file ntrip_proto.h
 * @brief NTRIP protocol text: request construction and response parsing.
 *
 * Pure text handling with no sockets, so it can be tested without a
 * network and reused by every consumer of a stream.  Socket I/O lives in
 * the session layer that calls this.
 *
 * Both NTRIP versions are handled.  NTRIP 1.0 answers a request with
 * `ICY 200 OK`, which is not HTTP at all; NTRIP 2.0 answers with an
 * ordinary HTTP status line.  A caster that receives an
 * `Ntrip-Version: Ntrip/2.0` request and replies ICY is simply an
 * NTRIP 1.0 caster, which is worth reporting but is not an error.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#ifndef NTRIP_PROTO_H
#define NTRIP_PROTO_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief NTRIP protocol version a caster answered with. */
typedef enum {
    NS_PROTO_UNKNOWN = 0,
    NS_PROTO_V1      = 1,   /**< "ICY 200 OK" -- not HTTP            */
    NS_PROTO_V2      = 2,   /**< "HTTP/1.x 200 OK"                   */
} NsProtoVersion;

/**
 * @struct NsHandshake
 * @brief What the caster said when the stream was opened.
 *
 * Kept as a caster-compliance record: version and header differences
 * explain the class of report that says a mountpoint works with one
 * client but not another.
 *
 * @note The GUI carries an equivalent `NtripHandshake` in
 *       `gui/gui_state.h`.  That duplicate is deliberate and temporary:
 *       the GUI switches to this one when it moves onto the session
 *       layer (design/architecture.md §9, step 4), and the old struct is
 *       deleted then.
 */
typedef struct {
    bool valid;                /**< a status line was recognised        */
    int  version;              /**< @ref NsProtoVersion                 */
    int  status;               /**< 200, 401, 404, ...; 0 if unparsed   */
    char reason[64];           /**< status reason phrase                */
    char status_line[128];     /**< the raw first line                  */
    char server[96];           /**< Server: header -- the caster software */
    char content_type[64];     /**< Content-Type: header                */
    char ntrip_version_hdr[32];/**< Ntrip-Version: header, if echoed    */
    bool chunked;              /**< Transfer-Encoding: chunked          */
    char raw[2048];            /**< full header text, for diagnostics   */
} NsHandshake;

/**
 * @brief Build an NTRIP GET request.
 *
 * Emits an NTRIP 2.0 request. A 1.0 caster ignores the extra headers and
 * answers ICY, which @ref ns_proto_parse_response reports.
 *
 * @param out        Destination buffer.
 * @param cap        Capacity of @p out.
 * @param host       Caster hostname, for the Host: header.
 * @param mountpoint Mountpoint, with or without a leading '/'.
 * @param user       Username, or NULL/"" for no authentication.
 * @param pass       Password; ignored when @p user is empty.
 * @param agent      User-Agent product token, e.g. "NTRIP ntrip-analyse/2.0.0".
 * @return Bytes needed excluding the NUL, snprintf-style: a value >= @p cap
 *         means the request was truncated and must not be sent.
 */
int ns_proto_build_request(char *out, size_t cap,
                           const char *host, const char *mountpoint,
                           const char *user, const char *pass,
                           const char *agent);

/**
 * @brief Parse a caster's response header.
 *
 * Reads the status code from the **status line only**.  Searching the
 * whole header for "200" accepts a 404 whose `Content-Length` happens to
 * be 200, after which a client decodes an HTML error page as RTCM.
 *
 * @param header NUL-terminated response header text.
 * @param out    [out] Parsed result; zeroed by this call.
 * @return true if a status line was recognised.
 */
bool ns_proto_parse_response(const char *header, NsHandshake *out);

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
 * @brief Find the end of a response header in a byte stream.
 *
 * Looks for the CRLF CRLF terminator, tolerating bare LF LF, which some
 * casters emit.
 *
 * @param data Received bytes so far.
 * @param len  Length of @p data.
 * @return Offset of the first payload byte, or -1 if the header is
 *         incomplete.
 */
int ns_proto_header_end(const unsigned char *data, int len);

/**
 * @brief Case-insensitive search for @p needle as a whole line-leading
 *        header name, returning its trimmed value.
 *
 * @return true if the header was present.
 */
bool ns_proto_header_value(const char *header, const char *name,
                           char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* NTRIP_PROTO_H */
