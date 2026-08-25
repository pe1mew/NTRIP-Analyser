/**
 * @file ns_failure.c
 * @brief The failure vocabulary: classification and words.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "core/ns_failure.h"

#include <stdio.h>
#include <string.h>

/* Classifying a caster's *answer* is string work.  Classifying a
 * *socket* error is not -- it needs the platform's own numbers -- so
 * that half lives in src/net/ntrip_handler.c, which already carries the
 * platform headers, and is declared beside these so that a caller sees
 * one vocabulary rather than two.
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
        case NS_FAIL_TLS_HANDSHAKE: return "tls-handshake";
        case NS_FAIL_TLS_CERT:      return "tls-cert";
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
        case NS_FAIL_TLS_HANDSHAKE:
            /* Points at the two fields the user controls: the port and
             * the TLS setting.  The commonest cause by far is a
             * plain-text port with TLS demanded of it. */
            return snprintf(buf, cap,
                "%s:%d did not complete a TLS handshake. The port may "
                "be plain-text -- check the port and the TLS setting.",
                h, port);
        case NS_FAIL_TLS_CERT:
            /* The generic form; the session replaces it with the
             * transport's specific sentence (expired / wrong host /
             * untrusted) when one is available. */
            return snprintf(buf, cap,
                "The caster's certificate was not accepted.");
        case NS_FAIL_NONE:
            break;
    }
    return 0;
}

const char *ns_failure_short(NsFailure f)
{
    /* Each of these fits the sixty characters the GUI's Detail column
     * and the CLI's table allow, with a station name still to come. */
    switch (f) {
        case NS_FAIL_NONE:          return "";
        case NS_FAIL_DNS:           return "The caster address does not resolve";
        case NS_FAIL_REFUSED:       return "Nothing is listening on that port";
        case NS_FAIL_UNREACHABLE:   return "No route to the caster";
        case NS_FAIL_TIMEOUT:       return "The caster did not answer";
        case NS_FAIL_NOT_NTRIP:     return "Answered, but not as an NTRIP caster";
        case NS_FAIL_AUTH:          return "User name or password rejected";
        case NS_FAIL_FORBIDDEN:     return "Not permitted for this mountpoint";
        case NS_FAIL_NO_MOUNTPOINT: return "No such mountpoint on this caster";
        case NS_FAIL_BUSY:          return "The caster is refusing connections";
        case NS_FAIL_REJECTED:      return "The caster refused the request";
        case NS_FAIL_DROPPED:       return "The caster closed the connection";
        case NS_FAIL_STALLED:       return "Connected, but nothing arrived";
        case NS_FAIL_TLS_HANDSHAKE: return "TLS handshake failed";
        case NS_FAIL_TLS_CERT:      return "Certificate not accepted";
    }
    return "";
}
