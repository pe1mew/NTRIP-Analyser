/**
 * @file gui_parsers.c
 * @brief Data parsers for populating GUI list views.
 *
 * Converts raw NTRIP sourcetable strings and analysis result structures
 * into Win32 ListView rows.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "resource.h"
#include "gui_state.h"
#include "core/sourcetable.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * @brief Compute the Haversine distance in kilometres between two WGS-84 points.
 */
static double haversine_km(double lat1, double lon1, double lat2, double lon2)
{
    const double R = 6371.0;  /* Earth mean radius in km */
    double dLat = (lat2 - lat1) * M_PI / 180.0;
    double dLon = (lon2 - lon1) * M_PI / 180.0;
    double rLat1 = lat1 * M_PI / 180.0;
    double rLat2 = lat2 * M_PI / 180.0;

    double a = sin(dLat / 2.0) * sin(dLat / 2.0) +
               cos(rLat1) * cos(rLat2) *
               sin(dLon / 2.0) * sin(dLon / 2.0);
    double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
    return R * c;
}

/* Documented in gui_state.h -- the contract lives with the declaration.
 *
 * Implementation note: the STR records are parsed by
 * `sourcetable_parse()` in the core, the same function the Android app
 * and any future frontend use.  This function is now only presentation:
 * fill the ListView, and compute the distance column the core has no
 * business knowing about.  It previously carried its own tokeniser,
 * which meant two implementations of one format could disagree. */
void ParseMountTable(const char *raw, HWND listview, double userLat, double userLon)
{
    if (!raw || !listview) return;

    ListView_DeleteAllItems(listview);

    int n = sourcetable_parse(raw, NULL, 0);
    if (n <= 0) return;

    SourcetableEntry *e = (SourcetableEntry *)calloc((size_t)n, sizeof(*e));
    if (!e) return;
    n = sourcetable_parse(raw, e, n);

    for (int row = 0; row < n; row++) {
        char buf[32];

        LVITEM lvi;
        ZeroMemory(&lvi, sizeof(lvi));
        lvi.mask    = LVIF_TEXT;
        lvi.iItem   = row;
        lvi.pszText = e[row].mountpoint;
        ListView_InsertItem(listview, &lvi);

        ListView_SetItemText(listview, row, 1, e[row].identifier);
        ListView_SetItemText(listview, row, 2, e[row].format);
        ListView_SetItemText(listview, row, 3, e[row].format_details);

        snprintf(buf, sizeof(buf), "%d", e[row].carrier);
        ListView_SetItemText(listview, row, 4, buf);

        ListView_SetItemText(listview, row, 5, e[row].nav_systems);
        ListView_SetItemText(listview, row, 6, e[row].network);
        ListView_SetItemText(listview, row, 7, e[row].country);

        snprintf(buf, sizeof(buf), "%.2f", e[row].latitude);
        ListView_SetItemText(listview, row, 8, buf);
        snprintf(buf, sizeof(buf), "%.2f", e[row].longitude);
        ListView_SetItemText(listview, row, 9, buf);

        /* Distance from the configured rover position.  A dash where
         * either end of the pair is unknown, rather than a confident
         * distance measured from 0N 0E. */
        if ((userLat == 0.0 && userLon == 0.0) ||
            (e[row].latitude == 0.0 && e[row].longitude == 0.0)) {
            snprintf(buf, sizeof(buf), "-");
        } else {
            snprintf(buf, sizeof(buf), "%.1f",
                     haversine_km(userLat, userLon,
                                  e[row].latitude, e[row].longitude));
        }
        ListView_SetItemText(listview, row, 10, buf);
    }

    free(e);
}

/* ── Advertised message types (sourcetable STR field 4) ──────────────────── */

BOOL SourcetableFindMountpoint(const char *raw, const char *mountpoint,
                               char *fmt_out, size_t fmt_sz,
                               char *det_out, size_t det_sz,
                               char *nav_out, size_t nav_sz)
{
    if (!raw || !mountpoint || !*mountpoint) return FALSE;

    /* Callers pass mountpoints both with and without the leading slash. */
    const char *want = (mountpoint[0] == '/') ? mountpoint + 1 : mountpoint;

    const char *p = raw;
    while (*p) {
        const char *lineEnd = p;
        while (*lineEnd && *lineEnd != '\r' && *lineEnd != '\n') lineEnd++;
        int lineLen = (int)(lineEnd - p);

        if (lineLen > 4 && strncmp(p, "STR;", 4) == 0) {
            char *buf = (char *)malloc(lineLen + 1);
            if (buf) {
                memcpy(buf, p, lineLen);
                buf[lineLen] = '\0';

                /* Split on ';' -- field 1 is the mountpoint, 3 the format,
                 * 4 the format-details list, 6 the nav-system claim. */
                char *fields[20];
                int nFields = 0;
                char *tok = buf;
                while (nFields < 20) {
                    fields[nFields++] = tok;
                    char *semi = strchr(tok, ';');
                    if (!semi) break;
                    *semi = '\0';
                    tok = semi + 1;
                }

                if (nFields > 4) {
                    const char *mp = fields[1];
                    if (mp[0] == '/') mp++;
                    if (_stricmp(mp, want) == 0) {
                        if (fmt_out && fmt_sz) {
                            strncpy(fmt_out, fields[3], fmt_sz - 1);
                            fmt_out[fmt_sz - 1] = '\0';
                        }
                        if (det_out && det_sz) {
                            strncpy(det_out, fields[4], det_sz - 1);
                            det_out[det_sz - 1] = '\0';
                        }
                        if (nav_out && nav_sz) {
                            /* Short STR lines exist in the wild; an
                             * absent nav-system field is empty, not
                             * whatever happened to be in the buffer. */
                            nav_out[0] = '\0';
                            if (nFields > 6) {
                                strncpy(nav_out, fields[6], nav_sz - 1);
                                nav_out[nav_sz - 1] = '\0';
                            }
                        }
                        free(buf);
                        return TRUE;
                    }
                }
                free(buf);
            }
        }

        p = lineEnd;
        while (*p == '\r' || *p == '\n') p++;
    }
    return FALSE;
}

int ParseAdvertisedTypes(const char *details, float *out)
{
    if (!out) return 0;
    for (int i = 0; i < GUI_MAX_MSG_TYPES; i++) out[i] = 0.0f;
    if (!details || !*details) return 0;

    int found = 0;
    const char *p = details;

    while (*p) {
        /* Skip anything that is not the start of a number. */
        while (*p && (*p < '0' || *p > '9')) p++;
        if (!*p) break;

        int type = 0;
        while (*p >= '0' && *p <= '9') {
            type = type * 10 + (*p - '0');
            p++;
        }

        /* Optional "(interval)" immediately after the type.  A bare type
         * with no interval is still advertised -- record -1 so callers can
         * tell "advertised, rate unknown" from "not advertised". */
        float interval = -1.0f;
        const char *q = p;
        while (*q == ' ' || *q == '\t') q++;
        if (*q == '(') {
            q++;
            char numbuf[32];
            int n = 0;
            while (*q && *q != ')' && n < (int)sizeof(numbuf) - 1) numbuf[n++] = *q++;
            numbuf[n] = '\0';
            if (*q == ')') {
                q++;
                double v = atof(numbuf);
                if (v > 0.0) interval = (float)v;
                p = q;
            }
        }

        /* Only accept plausible RTCM 3.x message numbers.  The details
         * field is not reliably a clean message list -- casters put
         * version text such as "RTCM 3.2" in it -- and without this floor
         * the "3" and "2" of that string would be recorded as advertised
         * types 3 and 2, producing phantom "missing" rows. */
        if (type >= 1000 && type < GUI_MAX_MSG_TYPES && out[type] == 0.0f) {
            out[type] = interval;
            found++;
        }
    }
    return found;
}

const char *stristr(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !*needle) return NULL;
    size_t nlen = strlen(needle);
    for (; *haystack; haystack++) {
        if (_strnicmp(haystack, needle, nlen) == 0)
            return haystack;
    }
    return NULL;
}
