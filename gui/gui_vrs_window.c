/**
 * @file gui_vrs_window.c
 * @brief Floating VRS / nearby-service monitor window.
 *
 * Layout (top to bottom):
 *   - Header strip:  big distance readout, GGA send-count + last send,
 *                    auto-send / override status hints.
 *   - Polar plot:    rover at centre, virtual ARP at true az + scaled
 *                    radius, with ARP history dots accumulating.
 *   - Strip chart:   rolling 5-minute distance graph at the bottom.
 *   - Footer:        configured rover lat/lon vs current GGA lat/lon,
 *                    so the user can tell at a glance which test
 *                    (shift / pause) is active.
 *
 * Threading: lives entirely on the UI thread.  Reads ggaCurrent*,
 * ggaOverride*, ggaSendEnabled, vrsDistance*, vrsArp* and
 * vrsDistHist* from AppState (all updated by the status-bar timer
 * in gui_events.c on the same thread, so no locking).
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "gui_vrs_window.h"
#include "resource.h"
#include "rtcm3x_parser.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define VRS_WIN_DEF_W   620
#define VRS_WIN_DEF_H   700

#define VRS_BG_COLOR        RGB(255, 255, 255)
#define VRS_HEADER_BG       RGB(245, 245, 245)
#define VRS_FOOTER_COLOR    RGB(120, 120, 120)
#define VRS_LABEL_COLOR     RGB(  0,   0,   0)
#define VRS_RING_COLOR      RGB(180, 180, 180)
#define VRS_AXIS_COLOR      RGB(140, 140, 140)
#define VRS_ROVER_COLOR     RGB( 30,  80, 200)   /* blue   */
#define VRS_ARP_COLOR       RGB(210,  40,  40)   /* red    */
#define VRS_ARP_HIST_COLOR  RGB(240, 170, 170)
#define VRS_GOOD_COLOR      RGB( 40, 140,  40)
#define VRS_WARN_COLOR      RGB(220, 150,  20)
#define VRS_BAD_COLOR       RGB(210,  40,  40)
#define VRS_CHART_LINE      RGB( 40, 140,  40)
#define VRS_CHART_GAP       RGB(200, 200, 200)
#define VRS_CHART_BG        RGB(252, 252, 252)

#define VRS_HEADER_H        110
#define VRS_FOOTER_H         28
#define VRS_CHART_H         110

/* Pick a readout colour for the distance number based on usability. */
static COLORREF vrs_dist_color(double km)
{
    if (km <  5.0) return VRS_GOOD_COLOR;
    if (km < 50.0) return VRS_WARN_COLOR;
    return VRS_BAD_COLOR;
}

/* Haversine distance + initial bearing from (lat1, lon1) to (lat2, lon2).
 * Returns km and bearing in degrees clockwise from north.  Used to plot
 * the ARP on the polar chart in true direction-and-distance. */
static void vrs_az_dist(double lat1, double lon1,
                        double lat2, double lon2,
                        double *out_dist_km, double *out_az_deg)
{
    const double R = 6371.0;
    double phi1 = lat1 * M_PI / 180.0;
    double phi2 = lat2 * M_PI / 180.0;
    double dphi = (lat2 - lat1) * M_PI / 180.0;
    double dlam = (lon2 - lon1) * M_PI / 180.0;
    double a = sin(dphi / 2.0) * sin(dphi / 2.0)
             + cos(phi1) * cos(phi2)
               * sin(dlam / 2.0) * sin(dlam / 2.0);
    double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
    if (out_dist_km) *out_dist_km = R * c;
    /* Initial bearing -- clockwise from north. */
    double y = sin(dlam) * cos(phi2);
    double x = cos(phi1) * sin(phi2)
             - sin(phi1) * cos(phi2) * cos(dlam);
    double az = atan2(y, x) * 180.0 / M_PI;
    if (az < 0.0) az += 360.0;
    if (out_az_deg) *out_az_deg = az;
}

/* ── Header: big distance readout + status hints ──────────────────── */
static void DrawHeader(HDC hdc, const AppState *state, int w)
{
    /* Background panel */
    HBRUSH bg = CreateSolidBrush(VRS_HEADER_BG);
    RECT rc = { 0, 0, w, VRS_HEADER_H };
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    SetBkMode(hdc, TRANSPARENT);

    /* Big distance number, centred horizontally. */
    char dist_buf[64];
    if (state->vrsDistanceValid) {
        if (state->vrsDistanceKm < 1.0)
            snprintf(dist_buf, sizeof(dist_buf), "%.0f m",
                     state->vrsDistanceKm * 1000.0);
        else if (state->vrsDistanceKm < 100.0)
            snprintf(dist_buf, sizeof(dist_buf), "%.2f km",
                     state->vrsDistanceKm);
        else
            snprintf(dist_buf, sizeof(dist_buf), "%.0f km",
                     state->vrsDistanceKm);
    } else {
        snprintf(dist_buf, sizeof(dist_buf), "-- ---");
    }

    HFONT hBig = CreateFont(48, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                            ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                            VARIABLE_PITCH | FF_SWISS, "Segoe UI");
    HFONT old = (HFONT)SelectObject(hdc, hBig);
    SetTextColor(hdc,
        state->vrsDistanceValid ? vrs_dist_color(state->vrsDistanceKm)
                                 : VRS_FOOTER_COLOR);
    SetTextAlign(hdc, TA_CENTER | TA_TOP);
    TextOut(hdc, w / 2, 14, dist_buf, (int)strlen(dist_buf));
    SelectObject(hdc, old);
    DeleteObject(hBig);

    /* "Rover <-> Virtual reference station" caption above the number. */
    SetTextAlign(hdc, TA_CENTER | TA_TOP);
    SetTextColor(hdc, VRS_LABEL_COLOR);
    const char *cap = "Rover  <->  Virtual reference station";
    TextOut(hdc, w / 2, 4, cap, (int)strlen(cap));

    /* Status hints (auto-send GGA / override active). */
    char hint[256];
    LONG ggaSent = InterlockedCompareExchange(
                       (volatile LONG *)&state->ggaSendCount, 0, 0);
    LONG ggaLast = InterlockedCompareExchange(
                       (volatile LONG *)&state->ggaLastSendUnix, 0, 0);
    char lastBuf[32] = "never";
    if (ggaLast > 0) {
        time_t now = time(NULL);
        long age = (long)(now - (time_t)ggaLast);
        if (age < 60)       snprintf(lastBuf, sizeof(lastBuf), "%lds ago", age);
        else if (age < 3600) snprintf(lastBuf, sizeof(lastBuf), "%ldm ago", age / 60);
        else                 snprintf(lastBuf, sizeof(lastBuf), "%ldh ago", age / 3600);
    }
    snprintf(hint, sizeof(hint),
             "Auto-send GGA: %s   |   Sent: %ld   |   Last: %s%s",
             state->ggaSendEnabled ? "ON" : "OFF (paused)",
             (long)ggaSent,
             lastBuf,
             state->ggaOverrideValid ? "   |   GGA OVERRIDE ACTIVE" : "");
    SetTextAlign(hdc, TA_CENTER | TA_TOP);
    SetTextColor(hdc, state->ggaOverrideValid ? VRS_BAD_COLOR
                                              : VRS_FOOTER_COLOR);
    TextOut(hdc, w / 2, 80, hint, (int)strlen(hint));
}

/* ── Polar plot: rover at centre, ARP at true az + distance ──────── */
static void DrawPolar(HDC hdc, const AppState *state,
                      int x0, int y0, int w, int h)
{
    int cx = x0 + w / 2;
    int cy = y0 + h / 2;
    int radius = ((w < h) ? w : h) / 2 - 18;
    if (radius < 30) return;

    /* Determine the dynamic max-distance for the radial scale.  Use
     * the larger of current distance and the largest ARP-history
     * distance, with a sensible floor.  Snap to 1/5/20/50/100/500 km
     * so the rings read as round numbers. */
    double max_km = 1.0;
    if (state->vrsDistanceValid && state->vrsDistanceKm > max_km)
        max_km = state->vrsDistanceKm;
    for (int i = 0; i < state->vrsArpHistCount; i++) {
        double dk, az;
        vrs_az_dist(state->ggaCurrentLat, state->ggaCurrentLon,
                    state->vrsArpHistLat[i],
                    state->vrsArpHistLon[i], &dk, &az);
        if (dk > max_km) max_km = dk;
    }
    double snaps[] = { 1.0, 5.0, 20.0, 50.0, 100.0, 500.0, 2000.0 };
    double R_km = 2000.0;
    for (int i = 0; i < (int)(sizeof(snaps) / sizeof(snaps[0])); i++) {
        if (snaps[i] >= max_km * 1.1) { R_km = snaps[i]; break; }
    }

    SetBkMode(hdc, TRANSPARENT);

    /* Concentric rings at 25/50/75/100 % of the scale + radial axes. */
    HPEN penRing = CreatePen(PS_SOLID, 1, VRS_RING_COLOR);
    HPEN oldP    = (HPEN)SelectObject(hdc, penRing);
    HBRUSH oldB  = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    for (int i = 1; i <= 4; i++) {
        int r = (radius * i) / 4;
        Ellipse(hdc, cx - r, cy - r, cx + r, cy + r);
    }
    /* Dotted N-S / E-W axes */
    for (int t = -radius; t <= radius; t += 3) {
        SetPixel(hdc, cx + t, cy, VRS_AXIS_COLOR);
        SetPixel(hdc, cx, cy + t, VRS_AXIS_COLOR);
    }
    SelectObject(hdc, oldP);
    DeleteObject(penRing);

    /* Ring labels (km at each ring) */
    SetTextColor(hdc, VRS_AXIS_COLOR);
    SetTextAlign(hdc, TA_LEFT | TA_TOP);
    for (int i = 1; i <= 4; i++) {
        int r = (radius * i) / 4;
        char buf[24];
        double km = R_km * i / 4.0;
        if (km < 1.0)
            snprintf(buf, sizeof(buf), "%.0f m", km * 1000.0);
        else
            snprintf(buf, sizeof(buf), "%.0f km", km);
        TextOut(hdc, cx + 3, cy - r - 9, buf, (int)strlen(buf));
    }

    /* Compass labels */
    SetTextColor(hdc, VRS_LABEL_COLOR);
    SetTextAlign(hdc, TA_CENTER | TA_TOP);
    TextOut(hdc, cx, cy - radius - 14, "N", 1);
    TextOut(hdc, cx, cy + radius + 2,  "S", 1);
    SetTextAlign(hdc, TA_LEFT | TA_TOP);
    TextOut(hdc, cx + radius + 3, cy - 6, "E", 1);
    SetTextAlign(hdc, TA_RIGHT | TA_TOP);
    TextOut(hdc, cx - radius - 3, cy - 6, "W", 1);
    SetTextAlign(hdc, TA_LEFT | TA_TOP);

    /* ARP history dots (faded red) */
    HBRUSH brHist = CreateSolidBrush(VRS_ARP_HIST_COLOR);
    HBRUSH oldHB  = (HBRUSH)SelectObject(hdc, brHist);
    HPEN   penNull = (HPEN)GetStockObject(NULL_PEN);
    HPEN   oldHP   = (HPEN)SelectObject(hdc, penNull);
    for (int i = 0; i < state->vrsArpHistCount; i++) {
        double dk, az;
        vrs_az_dist(state->ggaCurrentLat, state->ggaCurrentLon,
                    state->vrsArpHistLat[i],
                    state->vrsArpHistLon[i], &dk, &az);
        double r = (R_km > 0.0) ? (dk / R_km) * (double)radius : 0.0;
        double az_r = az * M_PI / 180.0;
        int hx = cx + (int)(r * sin(az_r) + 0.5);
        int hy = cy - (int)(r * cos(az_r) + 0.5);
        Ellipse(hdc, hx - 2, hy - 2, hx + 3, hy + 3);
    }
    SelectObject(hdc, oldHP);
    SelectObject(hdc, oldHB);
    DeleteObject(brHist);

    /* Rover marker (blue dot at centre) */
    HBRUSH brRover = CreateSolidBrush(VRS_ROVER_COLOR);
    SelectObject(hdc, brRover);
    HPEN penEdge = CreatePen(PS_SOLID, 1, RGB(40, 40, 40));
    SelectObject(hdc, penEdge);
    Ellipse(hdc, cx - 5, cy - 5, cx + 6, cy + 6);
    SetTextColor(hdc, VRS_ROVER_COLOR);
    TextOut(hdc, cx + 8, cy - 6, "Rover", 5);
    DeleteObject(penEdge);
    SelectObject(hdc, oldB);
    DeleteObject(brRover);

    /* Current ARP marker (red dot + line from rover) */
    if (state->vrsDistanceValid && state->vrsArpHistCount > 0) {
        int last = state->vrsArpHistCount - 1;
        double dk, az;
        vrs_az_dist(state->ggaCurrentLat, state->ggaCurrentLon,
                    state->vrsArpHistLat[last],
                    state->vrsArpHistLon[last], &dk, &az);
        double r = (R_km > 0.0) ? (dk / R_km) * (double)radius : 0.0;
        double az_r = az * M_PI / 180.0;
        int ax = cx + (int)(r * sin(az_r) + 0.5);
        int ay = cy - (int)(r * cos(az_r) + 0.5);

        HPEN penLine = CreatePen(PS_SOLID, 1, VRS_ARP_COLOR);
        HPEN oldLine = (HPEN)SelectObject(hdc, penLine);
        MoveToEx(hdc, cx, cy, NULL);
        LineTo(hdc, ax, ay);
        SelectObject(hdc, oldLine);
        DeleteObject(penLine);

        HBRUSH brArp = CreateSolidBrush(VRS_ARP_COLOR);
        HBRUSH oldArp = (HBRUSH)SelectObject(hdc, brArp);
        Ellipse(hdc, ax - 5, ay - 5, ax + 6, ay + 6);
        SetTextColor(hdc, VRS_ARP_COLOR);
        TextOut(hdc, ax + 7, ay - 6, "ARP", 3);
        SelectObject(hdc, oldArp);
        DeleteObject(brArp);
    }
    SelectObject(hdc, oldP);
}

/* ── Strip chart: distance over the last 5 minutes ───────────────── */
static void DrawChart(HDC hdc, const AppState *state,
                      int x0, int y0, int w, int h)
{
    HBRUSH bg = CreateSolidBrush(VRS_CHART_BG);
    RECT rc = { x0, y0, x0 + w, y0 + h };
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    /* Axes + label */
    HPEN penAxis = CreatePen(PS_SOLID, 1, VRS_AXIS_COLOR);
    HPEN oldP    = (HPEN)SelectObject(hdc, penAxis);
    MoveToEx(hdc, x0 + 30, y0 + h - 16, NULL);
    LineTo(hdc, x0 + w - 4, y0 + h - 16);
    MoveToEx(hdc, x0 + 30, y0 + 4, NULL);
    LineTo(hdc, x0 + 30, y0 + h - 16);
    SelectObject(hdc, oldP);
    DeleteObject(penAxis);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, VRS_FOOTER_COLOR);
    SetTextAlign(hdc, TA_LEFT | TA_TOP);
    TextOut(hdc, x0 + 4, y0 + 4,
            "Distance (5 min rolling)",
            (int)strlen("Distance (5 min rolling)"));

    if (state->vrsDistHistCount == 0) return;

    /* Compute max in window for scaling */
    int start = (state->vrsDistHistCount < VRS_DIST_BUFFER_N)
                ? 0 : state->vrsDistHistHead;
    double maxv = 1.0;
    for (int i = 0; i < state->vrsDistHistCount; i++) {
        int idx = (start + i) % VRS_DIST_BUFFER_N;
        float v = state->vrsDistHistKm[idx];
        if (v > 0.0f && v > maxv) maxv = v;
    }
    /* Snap max for readability */
    double snaps[] = { 1, 5, 10, 50, 100, 500, 1000, 5000 };
    double scale = 5000;
    for (int i = 0; i < (int)(sizeof(snaps) / sizeof(snaps[0])); i++) {
        if (snaps[i] >= maxv) { scale = snaps[i]; break; }
    }
    char ylab[24];
    if (scale < 1.0) snprintf(ylab, sizeof(ylab), "%.1f km", scale);
    else             snprintf(ylab, sizeof(ylab), "%.0f km", scale);
    TextOut(hdc, x0 + 2, y0 + 18, ylab, (int)strlen(ylab));
    TextOut(hdc, x0 + 2, y0 + h - 30, "0", 1);

    int chart_l = x0 + 32;
    int chart_r = x0 + w - 6;
    int chart_t = y0 + 18;
    int chart_b = y0 + h - 18;
    int chart_w = chart_r - chart_l;
    int chart_h = chart_b - chart_t;

    HPEN penLine = CreatePen(PS_SOLID, 2, VRS_CHART_LINE);
    HPEN penGap  = CreatePen(PS_DOT,   1, VRS_CHART_GAP);
    HPEN oldL    = (HPEN)SelectObject(hdc, penLine);

    int prev_x = -1, prev_y = -1;
    bool prev_valid = false;
    for (int i = 0; i < state->vrsDistHistCount; i++) {
        int idx = (start + i) % VRS_DIST_BUFFER_N;
        float v = state->vrsDistHistKm[idx];
        int x = chart_l + (chart_w * i) / VRS_DIST_BUFFER_N;
        if (v >= 0.0f) {
            int y = chart_b - (int)((double)v / scale * chart_h);
            if (y < chart_t) y = chart_t;
            if (prev_valid) {
                MoveToEx(hdc, prev_x, prev_y, NULL);
                LineTo(hdc, x, y);
            }
            prev_x = x; prev_y = y; prev_valid = true;
        } else {
            /* Gap sample -- draw a short dotted vertical to mark it */
            if (prev_valid) {
                SelectObject(hdc, penGap);
                MoveToEx(hdc, x, chart_t, NULL);
                LineTo(hdc, x, chart_b);
                SelectObject(hdc, penLine);
            }
            prev_valid = false;
        }
    }
    SelectObject(hdc, oldL);
    DeleteObject(penLine);
    DeleteObject(penGap);
}

/* ── Footer: current GGA + configured rover position ─────────────── */
static void DrawFooter(HDC hdc, const AppState *state, int w, int h)
{
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, VRS_FOOTER_COLOR);
    SetTextAlign(hdc, TA_LEFT | TA_TOP);

    char buf[256];
    snprintf(buf, sizeof(buf),
             "Rover (config): %.6f, %.6f   |   GGA sending: %.6f, %.6f%s",
             state->config.LATITUDE, state->config.LONGITUDE,
             state->ggaCurrentLat,    state->ggaCurrentLon,
             state->ggaOverrideValid ? "  [TEST]" : "");
    TextOut(hdc, 8, h - VRS_FOOTER_H + 6, buf, (int)strlen(buf));
}

/* ── Window procedure ──────────────────────────────────────────── */
static LRESULT CALLBACK VrsWndProc(HWND hwnd, UINT msg,
                                   WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
        HFONT hGui = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        /* Direction shift buttons (28x22) and reset button (60x22),
         * positioned in the polar plot's outer margin in WM_SIZE. */
        struct { int id; const char *txt; int w; } btns[] = {
            { IDC_VRS_BTN_RESET, "Reset",  60 },
            { IDC_VRS_BTN_N,     "N",      28 },
            { IDC_VRS_BTN_E,     "E",      28 },
            { IDC_VRS_BTN_S,     "S",      28 },
            { IDC_VRS_BTN_W,     "W",      28 },
        };
        for (int i = 0; i < (int)(sizeof(btns) / sizeof(btns[0])); i++) {
            HWND b = CreateWindowEx(0, "BUTTON", btns[i].txt,
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                0, 0, btns[i].w, 22,
                hwnd, (HMENU)(intptr_t)btns[i].id, hInst, NULL);
            if (b) SendMessage(b, WM_SETFONT, (WPARAM)hGui, TRUE);
        }
        /* 1 Hz repaint so the live readouts stay current. */
        SetTimer(hwnd, 1, 1000, NULL);
        return 0;
    }

    case WM_SIZE: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        int polar_y0 = VRS_HEADER_H + 4;
        int chart_y0 = h - VRS_FOOTER_H - VRS_CHART_H - 4;
        int polar_h  = chart_y0 - polar_y0;
        if (polar_h < 100) polar_h = 100;
        int cx = w / 2;
        int cy = polar_y0 + polar_h / 2;

        /* N top-centre, S bottom-centre, E right-centre, W left-centre */
        MoveWindow(GetDlgItem(hwnd, IDC_VRS_BTN_N),
                   cx - 14, polar_y0 + 4, 28, 22, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_VRS_BTN_S),
                   cx - 14, chart_y0 - 26, 28, 22, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_VRS_BTN_E),
                   w - 34, cy - 11, 28, 22, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_VRS_BTN_W),
                   4, cy - 11, 28, 22, TRUE);
        /* Reset top-left of polar area */
        MoveWindow(GetDlgItem(hwnd, IDC_VRS_BTN_RESET),
                   4, polar_y0 + 4, 60, 22, TRUE);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_COMMAND: {
        AppState *state = (AppState *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (!state) break;
        int id = LOWORD(wParam);

        if (id == IDC_VRS_BTN_N || id == IDC_VRS_BTN_E ||
            id == IDC_VRS_BTN_S || id == IDC_VRS_BTN_W) {
            /* Throttle: refuse if a prior shift hasn't been transmitted
             * yet (worker hasn't bumped ggaSendCount past the stamp). */
            LONG cnt = InterlockedCompareExchange(
                           (volatile LONG *)&state->ggaSendCount, 0, 0);
            LONG req = InterlockedCompareExchange(
                           (volatile LONG *)&state->ggaShiftRequestedAtCount,
                           0, 0);
            if (req >= 0 && cnt <= req) {
                MessageBeep(MB_ICONWARNING);
                return 0;
            }

            /* Compose shift onto whichever lat/lon is currently being
             * sent.  Step size is small (5 km) so the test stays
             * within a typical nearest-station / nearby-service
             * coverage area and surfaces the gradual station
             * hand-overs rather than just kicking the rover outside
             * the network's footprint.  Successive presses compose
             * -- six presses N walks 30 km north total.  Longitude
             * scales by 1/cos(lat) to keep the metric offset honest. */
            double base_lat = state->ggaOverrideValid
                              ? state->ggaOverrideLat
                              : state->config.LATITUDE;
            double base_lon = state->ggaOverrideValid
                              ? state->ggaOverrideLon
                              : state->config.LONGITUDE;
            const double dkm = 5.0;
            const double dlat_per_km = 1.0 / 111.0;
            double dlon_per_km =
                1.0 / (111.0 * cos(base_lat * M_PI / 180.0));
            double new_lat = base_lat, new_lon = base_lon;
            switch (id) {
            case IDC_VRS_BTN_N: new_lat += dkm * dlat_per_km; break;
            case IDC_VRS_BTN_S: new_lat -= dkm * dlat_per_km; break;
            case IDC_VRS_BTN_E: new_lon += dkm * dlon_per_km; break;
            case IDC_VRS_BTN_W: new_lon -= dkm * dlon_per_km; break;
            }
            state->ggaOverrideLat   = new_lat;
            state->ggaOverrideLon   = new_lon;
            state->ggaOverrideValid = TRUE;
            InterlockedExchange(
                (volatile LONG *)&state->ggaShiftRequestedAtCount, cnt);
            InterlockedExchange(
                (volatile LONG *)&state->ggaLastShiftUnix,
                (LONG)time(NULL));
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        if (id == IDC_VRS_BTN_RESET) {
            /* Snap back to the config lat/lon and clear any pending
             * shift throttle in one go. */
            state->ggaOverrideValid = FALSE;
            InterlockedExchange(
                (volatile LONG *)&state->ggaShiftRequestedAtCount, -1);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        break;
    }

    case WM_TIMER: {
        /* Update the throttle's button-enabled state every second.
         * EnableWindow is a no-op if the state already matches, so
         * calling it unconditionally each tick is cheap. */
        AppState *state = (AppState *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (state) {
            LONG cnt = InterlockedCompareExchange(
                           (volatile LONG *)&state->ggaSendCount, 0, 0);
            LONG req = InterlockedCompareExchange(
                           (volatile LONG *)&state->ggaShiftRequestedAtCount,
                           0, 0);
            BOOL free_to_shift = (req < 0) || (cnt > req);
            EnableWindow(GetDlgItem(hwnd, IDC_VRS_BTN_N), free_to_shift);
            EnableWindow(GetDlgItem(hwnd, IDC_VRS_BTN_E), free_to_shift);
            EnableWindow(GetDlgItem(hwnd, IDC_VRS_BTN_S), free_to_shift);
            EnableWindow(GetDlgItem(hwnd, IDC_VRS_BTN_W), free_to_shift);
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        AppState *state = (AppState *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (!state) return DefWindowProc(hwnd, msg, wParam, lParam);

        PAINTSTRUCT ps;
        HDC hdcScreen = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        /* Off-screen buffer for flicker-free repaint */
        HDC hdcMem = CreateCompatibleDC(hdcScreen);
        HBITMAP bmp = CreateCompatibleBitmap(hdcScreen, w, h);
        HBITMAP bmpOld = (HBITMAP)SelectObject(hdcMem, bmp);

        HFONT hGui = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HFONT hOldFont = (HFONT)SelectObject(hdcMem, hGui);

        /* Background */
        HBRUSH bg = CreateSolidBrush(VRS_BG_COLOR);
        RECT all = { 0, 0, w, h };
        FillRect(hdcMem, &all, bg);
        DeleteObject(bg);

        /* Sections */
        DrawHeader(hdcMem, state, w);

        int polar_y0 = VRS_HEADER_H + 4;
        int chart_y0 = h - VRS_FOOTER_H - VRS_CHART_H - 4;
        int polar_h  = chart_y0 - polar_y0;
        if (polar_h < 100) polar_h = 100;

        DrawPolar(hdcMem, state, 0, polar_y0, w, polar_h);
        DrawChart(hdcMem, state, 0, chart_y0, w, VRS_CHART_H);
        DrawFooter(hdcMem, state, w, h);

        BitBlt(hdcScreen, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);

        SelectObject(hdcMem, hOldFont);
        SelectObject(hdcMem, bmpOld);
        DeleteObject(bmp);
        DeleteDC(hdcMem);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY: {
        KillTimer(hwnd, 1);
        AppState *state = (AppState *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (state && state->hVrsWnd == hwnd) {
            WINDOWPLACEMENT wp;
            ZeroMemory(&wp, sizeof(wp));
            wp.length = sizeof(wp);
            if (GetWindowPlacement(hwnd, &wp)) {
                state->vrsWndRect      = wp.rcNormalPosition;
                state->vrsWndRectValid = TRUE;
            }
            state->hVrsWnd = NULL;
        }
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

BOOL RegisterVrsWindowClass(HINSTANCE hInst)
{
    static BOOL registered = FALSE;
    if (registered) return TRUE;

    WNDCLASSEX wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = VrsWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = VRS_WINDOW_CLASS;
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassEx(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return FALSE;
    }
    registered = TRUE;
    return TRUE;
}

HWND CreateVrsWindow(HINSTANCE hInst, HWND hOwner, AppState *state)
{
    if (!RegisterVrsWindowClass(hInst)) return NULL;

    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    int w = VRS_WIN_DEF_W,  h = VRS_WIN_DEF_H;
    if (state && state->vrsWndRectValid) {
        x = state->vrsWndRect.left;
        y = state->vrsWndRect.top;
        w = state->vrsWndRect.right  - state->vrsWndRect.left;
        h = state->vrsWndRect.bottom - state->vrsWndRect.top;
        if (w < 320 || h < 320) { w = VRS_WIN_DEF_W; h = VRS_WIN_DEF_H; }
    }

    HWND hwnd = CreateWindowEx(
        WS_EX_TOOLWINDOW,
        VRS_WINDOW_CLASS, "VRS Monitor",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        x, y, w, h,
        hOwner, NULL, hInst, state);
    if (hwnd) {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
    }
    return hwnd;
}
