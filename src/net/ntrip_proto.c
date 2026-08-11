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

    char auth_line[512] = "";
    if (user && *user) {
        char raw[256];
        snprintf(raw, sizeof(raw), "%s:%s", user, pass ? pass : "");
        char enc[400];
        if (b64_encode(raw, enc, sizeof(enc)) > 0)
            snprintf(auth_line, sizeof(auth_line),
                     "Authorization: Basic %s\r\n", enc);
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
