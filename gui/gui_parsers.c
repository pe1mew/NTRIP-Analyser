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

/**
 * @brief Parse a raw NTRIP sourcetable and populate a ListView with STR entries.
 *
 * Each STR line has semicolon-separated fields:
 *   STR;Mountpoint;Identifier;Format;Details;Carrier;NavSys;Network;Country;Lat;Lon;...
 *
 * @param raw       Full sourcetable response (may include HTTP headers).
 * @param listview  Handle to the mountpoint ListView control.
 * @param userLat   User latitude from config (for distance calculation).
 * @param userLon   User longitude from config (for distance calculation).
 */
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
