/**
 * @file ntrip_proto.c
 * @brief NTRIP protocol text -- implementation.
 *
 * No sockets and no platform headers: this is string handling, which is
 * why it can be unit-tested without a caster.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "net/ntrip_proto.h"

#include <stdio.h>
#include <stdlib.h>     /* atoi */
#include <string.h>
#include <ctype.h>
#ifndef _WIN32
#include <strings.h>    /* strncasecmp lives here under POSIX */
#endif

/* ── What went wrong, and what to say about it ────────────────────────
 *
 * The classification of a caster's *answer* is string work like the rest
 * of this file.  Classifying a *socket* error is not -- it needs the
 * platform's own numbers -- so that half lives in ntrip_handler.c, which
 * already carries the platform headers.
 */

NsFailure ns_failure_from_response(bool valid, int status,
                                   const char *content_type)
{
    /* Not a status line at all: a web server, a proxy's error page, or
     * a service that is not NTRIP.  The commonest cause is a port that
     * belongs to something else. */
    if (!valid) return NS_FAIL_NOT_NTRIP;

    switch (status) {
        case 401: return NS_FAIL_AUTH;
        case 403: return NS_FAIL_FORBIDDEN;
        case 404: return NS_FAIL_NO_MOUNTPOINT;
        case 409:
        case 503: return NS_FAIL_BUSY;
        default: break;
    }

    /* A sourcetable where a stream was asked for.  NTRIP 1 casters
     * answer an unknown mountpoint this way -- with a 200 -- so a
     * client that only reads the status believes it succeeded and then
     * spends the run wondering why the "RTCM" will not decode. */
    if (status == 200 && content_type && *content_type &&
        strstr(content_type, "sourcetable"))
        return NS_FAIL_NO_MOUNTPOINT;

    /* A 200 with a web page behind it: the port belongs to a server
     * that answers everything politely.  Believing the status here is
     * how a run spends its length wondering why the "RTCM" will not
     * decode.  An NTRIP caster sends gnss/data, or -- speaking ICY --
     * nothing at all, so only an explicit page is rejected. */
    if (status == 200 && content_type && strstr(content_type, "text/html"))
        return NS_FAIL_NOT_NTRIP;

    if (status == 200) return NS_FAIL_NONE;

    /* Refused, in words this version has no sentence for.  Named rather
     * than guessed at: the status line goes in the detail, and the user
     * sees what the caster actually said. */
    return NS_FAIL_REJECTED;
}

const char *ns_failure_name(NsFailure f)
{
    switch (f) {
        case NS_FAIL_NONE:          return "none";
        case NS_FAIL_DNS:           return "dns";
        case NS_FAIL_REFUSED:       return "refused";
        case NS_FAIL_UNREACHABLE:   return "unreachable";
        case NS_FAIL_TIMEOUT:       return "timeout";
        case NS_FAIL_NOT_NTRIP:     return "not-ntrip";
        case NS_FAIL_AUTH:          return "auth";
        case NS_FAIL_FORBIDDEN:     return "forbidden";
        case NS_FAIL_NO_MOUNTPOINT: return "no-mountpoint";
        case NS_FAIL_BUSY:          return "busy";
        case NS_FAIL_REJECTED:      return "rejected";
        case NS_FAIL_DROPPED:       return "dropped";
        case NS_FAIL_STALLED:       return "stalled";
    }
    return "none";
}

int ns_failure_text(char *buf, size_t cap, NsFailure f,
                    const char *host, int port, const char *mountpoint)
{
    if (!buf || cap == 0) return 0;
    buf[0] = 0;
    if (f == NS_FAIL_NONE) return 0;

    const char *h  = host       ? host       : "";
    const char *mp = mountpoint ? mountpoint : "";

    /* Each sentence names the thing to look at, because a message that
     * says only what failed leaves the reader to guess which of the four
     * fields they typed is the wrong one. */
    switch (f) {
        case NS_FAIL_DNS:
            return snprintf(buf, cap, "Cannot find \"%s\". Check the address.", h);
        case NS_FAIL_REFUSED:
            return snprintf(buf, cap,
                "Nothing is listening on %s:%d. Check the port.", h, port);
        case NS_FAIL_UNREACHABLE:
            return snprintf(buf, cap,
                "No route to %s. Check the network.", h);
        case NS_FAIL_TIMEOUT:
            return snprintf(buf, cap,
                "%s:%d did not answer. The service may be down, or a "
                "firewall may be dropping it.", h, port);
        case NS_FAIL_NOT_NTRIP:
            return snprintf(buf, cap,
                "%s:%d answered, but not as an NTRIP caster.", h, port);
        case NS_FAIL_AUTH:
            return snprintf(buf, cap,
                "The caster rejected the user name or password.");
        case NS_FAIL_FORBIDDEN:
            return snprintf(buf, cap,
                "The credentials are accepted, but not for mountpoint "
                "\"%s\".", mp);
        case NS_FAIL_NO_MOUNTPOINT:
            return snprintf(buf, cap,
                "This caster has no mountpoint \"%s\".", mp);
        case NS_FAIL_BUSY:
            return snprintf(buf, cap,
                "The caster is refusing new connections just now.");
        case NS_FAIL_REJECTED:
            return snprintf(buf, cap, "The caster refused the request.");
        case NS_FAIL_DROPPED:
            return snprintf(buf, cap,
                "The caster closed the connection.");
        case NS_FAIL_STALLED:
            return snprintf(buf, cap,
                "Connected, but nothing is arriving.");
        case NS_FAIL_NONE:
            break;
    }
    return 0;
}

/* ── Base64, for HTTP Basic authentication ────────────────────────────
 * Implemented here rather than reusing base64_encode() from
 * src/ntrip_handler.h, which would make this module depend on the
 * procedure-shaped API it is meant to replace.  That header's copy is
 * removed when the CLI moves onto the session layer
 * (design/architecture.md §9, step 5). */
static const char k_b64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * @brief Base64-encode @p in into @p out.
 * @return Bytes written excluding the NUL, or -1 if @p cap is too small.
 */
static int b64_encode(const char *in, char *out, size_t cap)
{
    size_t n = strlen(in);
    size_t need = ((n + 2) / 3) * 4;
    if (cap < need + 1) return -1;

    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = (unsigned char)in[i] << 16;
        if (i + 1 < n) v |= (unsigned char)in[i + 1] << 8;
        if (i + 2 < n) v |= (unsigned char)in[i + 2];

        out[o++] = k_b64[(v >> 18) & 0x3F];
        out[o++] = k_b64[(v >> 12) & 0x3F];
        out[o++] = (i + 1 < n) ? k_b64[(v >> 6) & 0x3F] : '=';
        out[o++] = (i + 2 < n) ? k_b64[v & 0x3F]        : '=';
    }
    out[o] = '\0';
    return (int)o;
}

/** @brief Case-insensitive substring search. */
static const char *stristr_local(const char *hay, const char *needle)
{
    if (!hay || !needle || !*needle) return NULL;
    size_t nlen = strlen(needle);
    for (; *hay; hay++) {
#ifdef _WIN32
        if (_strnicmp(hay, needle, nlen) == 0) return hay;
#else
        if (strncasecmp(hay, needle, nlen) == 0) return hay;
#endif
    }
    return NULL;
}

static int strncasecmp_local(const char *a, const char *b, size_t n)
{
#ifdef _WIN32
    return _strnicmp(a, b, n);
#else
    return strncasecmp(a, b, n);
#endif
}

int ns_proto_build_request(char *out, size_t cap,
                           const char *host, const char *mountpoint,
                           const char *user, const char *pass,
                           const char *agent)
{
    if (!out || cap == 0) return -1;

    const char *mp = mountpoint ? mountpoint : "";
    if (mp[0] == '/') mp++;          /* callers pass it both ways */

    /* HTTP Basic is what NTRIP specifies, and base64 is an encoding, not
     * encryption: on a plain TCP connection these credentials are
     * readable by anything on the path.  This client speaks no TLS, so
     * the honest thing is to say so once per session rather than let a
     * user assume otherwise -- see design/security-review.md F3.
     *
     * Only when credentials are actually sent: an anonymous stream has
     * nothing to expose, and warning about it would train the user to
     * ignore the warning that matters. */
    char auth_line[512] = "";
    if (user && *user) {
        char raw[256];
        snprintf(raw, sizeof(raw), "%s:%s", user, pass ? pass : "");
        char enc[400];
        if (b64_encode(raw, enc, sizeof(enc)) > 0)
            snprintf(auth_line, sizeof(auth_line),
                     "Authorization: Basic %s\r\n", enc);

        static bool warned = false;
        if (!warned) {
            warned = true;
            fprintf(stderr,
                    "[SECURITY] Credentials for %s are sent as HTTP Basic "
                    "over a plain TCP connection: base64 is an encoding, "
                    "not encryption, and anything on the network path can "
                    "read them. This client does not support TLS.\n",
                    host ? host : "the caster");
        }
    }

    int n = snprintf(out, cap,
                     "GET /%s HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "Ntrip-Version: Ntrip/2.0\r\n"
                     "User-Agent: %s\r\n"
                     "%s"
                     "Connection: close\r\n"
                     "\r\n",
                     mp,
                     host ? host : "",
                     agent ? agent : "NTRIP ntrip-analyser",
                     auth_line);
    return n;
}

int ns_proto_header_end(const unsigned char *data, int len)
{
    if (!data) return -1;
    for (int i = 0; i + 1 < len; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n') {
            if (i + 3 < len && data[i + 2] == '\r' && data[i + 3] == '\n')
                return i + 4;
        }
        /* Some casters emit bare LF; accept LF LF as a terminator too. */
        if (data[i] == '\n' && data[i + 1] == '\n')
            return i + 2;
    }
    return -1;
}

bool ns_proto_header_value(const char *header, const char *name,
                           char *out, size_t cap)
{
    if (!header || !name || !out || cap == 0) return false;
    out[0] = '\0';

    size_t nlen = strlen(name);
    const char *p = header;

    while (p && *p) {
        /* Only match at the start of a line, so a value containing the
         * header's name cannot be mistaken for the header itself. */
        bool at_line_start = (p == header) || (p[-1] == '\n');
        if (at_line_start && strncasecmp_local(p, name, nlen) == 0) {
            const char *v = p + nlen;
            while (*v == ' ' || *v == '\t') v++;
            size_t o = 0;
            while (*v && *v != '\r' && *v != '\n' && o < cap - 1)
                out[o++] = *v++;
            while (o > 0 && (out[o - 1] == ' ' || out[o - 1] == '\t')) o--;
            out[o] = '\0';
            return true;
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
    return false;
}

bool ns_proto_parse_response(const char *header, NsHandshake *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!header || !*header) return false;

    strncpy(out->raw, header, sizeof(out->raw) - 1);

    /* Status line = up to the first CR or LF. */
    size_t n = 0;
    while (header[n] && header[n] != '\r' && header[n] != '\n' &&
           n < sizeof(out->status_line) - 1) {
        out->status_line[n] = header[n];
        n++;
    }
    out->status_line[n] = '\0';
    if (n == 0) return false;

    const char *sl = out->status_line;

    if (strncasecmp_local(sl, "ICY", 3) == 0) {
        out->version = NS_PROTO_V1;
        const char *p = sl + 3;
        while (*p == ' ') p++;
        out->status = atoi(p);
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        strncpy(out->reason, p, sizeof(out->reason) - 1);
    } else if (strncasecmp_local(sl, "HTTP/", 5) == 0) {
        out->version = NS_PROTO_V2;
        const char *p = sl + 5;
        while (*p && *p != ' ') p++;      /* skip "1.1" */
        while (*p == ' ') p++;
        out->status = atoi(p);
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        strncpy(out->reason, p, sizeof(out->reason) - 1);
    } else {
        /* Neither -- e.g. a SOURCETABLE reply for an unknown mountpoint.
         * Reporting this as unparsed is what stops a client treating an
         * error document as a stream. */
        out->version = NS_PROTO_UNKNOWN;
        out->status  = 0;
        return false;
    }

    ns_proto_header_value(header, "Server:", out->server, sizeof(out->server));
    ns_proto_header_value(header, "Content-Type:", out->content_type,
                          sizeof(out->content_type));
    ns_proto_header_value(header, "Ntrip-Version:", out->ntrip_version_hdr,
                          sizeof(out->ntrip_version_hdr));

    char te[64];
    if (ns_proto_header_value(header, "Transfer-Encoding:", te, sizeof(te)))
        out->chunked = (stristr_local(te, "chunked") != NULL);

    out->valid = true;
    return true;
}
