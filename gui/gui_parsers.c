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
 * Implementation note: each STR line is semicolon-separated,
 *   STR;Mountpoint;Identifier;Format;Details;Carrier;NavSys;Network;Country;Lat;Lon;...
 * and the response may still carry HTTP headers, which are skipped. */
void ParseMountTable(const char *raw, HWND listview, double userLat, double userLon)
{
    if (!raw || !listview) return;

    ListView_DeleteAllItems(listview);

    const char *p = raw;
    int row = 0;

    while (*p) {
        /* Find end of current line */
        const char *lineEnd = p;
        while (*lineEnd && *lineEnd != '\r' && *lineEnd != '\n')
            lineEnd++;

        int lineLen = (int)(lineEnd - p);

        /* Only process STR lines */
        if (lineLen > 4 && strncmp(p, "STR;", 4) == 0) {
            /* Copy line into a temporary buffer for tokenizing */
            char *buf = (char *)malloc(lineLen + 1);
            if (buf) {
                memcpy(buf, p, lineLen);
                buf[lineLen] = '\0';

                /* Split by ';' into fields */
                char *fields[20];
                int nFields = 0;
                char *tok = buf;

                while (nFields < 20) {
                    fields[nFields++] = tok;
                    char *sep = strchr(tok, ';');
                    if (!sep) break;
                    *sep = '\0';
                    tok = sep + 1;
                }

                /* Need at least 11 fields: STR + 10 data columns */
                if (nFields >= 11) {
                    LVITEM lvi;
                    ZeroMemory(&lvi, sizeof(lvi));
                    lvi.mask    = LVIF_TEXT;
                    lvi.iItem   = row;
                    lvi.pszText = fields[1];  /* Mountpoint */
                    ListView_InsertItem(listview, &lvi);

                    ListView_SetItemText(listview, row, 1, fields[2]);   /* Identifier */
                    ListView_SetItemText(listview, row, 2, fields[3]);   /* Format */
                    ListView_SetItemText(listview, row, 3, fields[4]);   /* Details */
                    ListView_SetItemText(listview, row, 4, fields[5]);   /* Carrier */
                    ListView_SetItemText(listview, row, 5, fields[6]);   /* Nav Sys */
                    ListView_SetItemText(listview, row, 6, fields[7]);   /* Network */
                    ListView_SetItemText(listview, row, 7, fields[8]);   /* Country */
                    ListView_SetItemText(listview, row, 8, fields[9]);   /* Lat */
                    ListView_SetItemText(listview, row, 9, fields[10]);  /* Lon */

                    /* Column 10: Distance (km) from user position */
                    double mpLat = atof(fields[9]);
                    double mpLon = atof(fields[10]);
                    char distBuf[32];

                    if (userLat == 0.0 && userLon == 0.0) {
                        /* No user position configured */
                        snprintf(distBuf, sizeof(distBuf), "-");
                    } else if (mpLat == 0.0 && mpLon == 0.0) {
                        /* Mountpoint has no coordinates */
                        snprintf(distBuf, sizeof(distBuf), "-");
                    } else {
                        double dist = haversine_km(userLat, userLon, mpLat, mpLon);
                        snprintf(distBuf, sizeof(distBuf), "%.1f", dist);
                    }
                    ListView_SetItemText(listview, row, 10, distBuf);

                    row++;
                }

                free(buf);
            }
        }

        /* Advance past end-of-line characters */
        p = lineEnd;
        while (*p == '\r' || *p == '\n') p++;
    }
}

/* ── Advertised message types (sourcetable STR field 4) ──────────────────── */

BOOL SourcetableFindMountpoint(const char *raw, const char *mountpoint,
                               char *fmt_out, size_t fmt_sz,
                               char *det_out, size_t det_sz)
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
                 * 4 the format-details list we are after. */
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
