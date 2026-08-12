/**
 * @file sourcetable.c
 * @brief Sourcetable parsing -- implementation.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * @copyright Apache License 2.0 with Commons Clause (see LICENSE for details)
 */

#include "core/sourcetable.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief Copy field @p idx of a semicolon-separated record into @p dst.
 *
 * @return Pointer to the field within @p line, or NULL when the record
 *         ends before that field.
 */
static const char *field(const char *line, const char *end, int idx,
                         char *dst, size_t cap)
{
    const char *p = line;
    for (int i = 0; i < idx; i++) {
        const char *semi = memchr(p, ';', (size_t)(end - p));
        if (!semi) { if (dst && cap) dst[0] = '\0'; return NULL; }
        p = semi + 1;
    }
    const char *semi = memchr(p, ';', (size_t)(end - p));
    const char *stop = semi ? semi : end;

    if (dst && cap) {
        size_t n = (size_t)(stop - p);
        if (n >= cap) n = cap - 1;
        memcpy(dst, p, n);
        dst[n] = '\0';
    }
    return p;
}

/** @brief Field @p idx as a double; 0.0 when absent or unparsable. */
static double field_num(const char *line, const char *end, int idx)
{
    char buf[32];
    if (!field(line, end, idx, buf, sizeof(buf))) return 0.0;
    return atof(buf);
}

int sourcetable_parse(const char *raw, SourcetableEntry *out, int max)
{
    if (!raw) return 0;

    int n = 0;
    const char *p = raw;

    while (*p) {
        const char *nl = strchr(p, '\n');
        const char *end = nl ? nl : p + strlen(p);

        /* Trim a trailing CR: casters send CRLF. */
        const char *line_end = end;
        if (line_end > p && line_end[-1] == '\r') line_end--;

        if ((size_t)(line_end - p) > 4 && strncmp(p, "STR;", 4) == 0) {
            if (!out) {
                n++;
            } else if (n < max) {
                SourcetableEntry *e = &out[n];
                memset(e, 0, sizeof(*e));

                field(p, line_end, 1, e->mountpoint,     sizeof(e->mountpoint));
                field(p, line_end, 2, e->identifier,     sizeof(e->identifier));
                field(p, line_end, 3, e->format,         sizeof(e->format));
                field(p, line_end, 4, e->format_details, sizeof(e->format_details));
                field(p, line_end, 6, e->nav_systems,    sizeof(e->nav_systems));
                field(p, line_end, 7, e->network,        sizeof(e->network));
                field(p, line_end, 8, e->country,        sizeof(e->country));

                e->carrier   = (int)field_num(p, line_end, 5);
                e->latitude  = field_num(p, line_end, 9);
                e->longitude = field_num(p, line_end, 10);
                e->nmea      = field_num(p, line_end, 11) != 0.0;

                n++;
            } else {
                break;      /* caller's array is full */
            }
        }

        if (!nl) break;
        p = nl + 1;
    }
    return n;
}
