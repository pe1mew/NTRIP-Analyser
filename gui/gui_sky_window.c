/**
 * @file gui_sky_window.c
 * @brief Floating polar sky-visibility plot window.
 *
 * Draws a polar (azimuth/elevation) plot of satellites tracked by the
 * connected reference station.  Phase 1 ships only the empty compass rose;
 * subsequent phases plot SV markers fed from cached RTCM 1019/1045/1046
 * ephemerides combined with MSM observations.
 *
 * Threading: lives entirely on the UI thread.  Worker thread will post
 * WM_APP_SKY_UPDATE to the main window, which copies the payload into
 * AppState->skyState and InvalidateRect()s this window.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "resource.h"
#include "gui_state.h"
#include "gui_sky_window.h"
#include "rtcm3x_parser.h"
#include "sv_ephemeris.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Default window size when first opened */
#define SKY_WIN_DEF_W   480
#define SKY_WIN_DEF_H   520

/* SV markers older than this go dim, older than 2x get removed */
#define SKY_STALE_S       5.0
#define SKY_DROP_S       30.0

/* Colours */
#define SKY_BG_COLOR     RGB(255, 255, 255)
#define SKY_RING_COLOR   RGB(180, 180, 180)
#define SKY_AXIS_COLOR   RGB(140, 140, 140)
#define SKY_LABEL_COLOR  RGB(  0,   0,   0)
#define SKY_STATUS_COLOR RGB(120, 120, 120)
#define SKY_GPS_COLOR    RGB( 40, 140,  40)   /* G — green */
#define SKY_GAL_COLOR    RGB( 30,  80, 200)   /* E — blue  */
#define SKY_DIM_COLOR    RGB(180, 180, 180)   /* stale marker */
#define SKY_MARKER_OUTLINE RGB(40, 40, 40)

/**
 * @brief Draw the static compass rose into a memory DC.
 *
 * Maps elevation 0–90° to a radial distance (90° at centre, 0° at outer ring).
 * Azimuth 0° = north, clockwise.  Returns the centre and radius so the
 * marker pass can place SVs without duplicating the geometry.
 */
static void DrawSkyBackground(HDC hdc, int w, int h,
                              int *out_cx, int *out_cy, int *out_radius)
{
    HBRUSH bg = CreateSolidBrush(SKY_BG_COLOR);
    RECT rc = { 0, 0, w, h };
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    int margin = 28;
    int diameter = (w < h ? w : h) - 2 * margin;
    if (diameter < 60) diameter = 60;
    int radius = diameter / 2;
    int cx = w / 2;
    int cy = h / 2;

    SetBkMode(hdc, TRANSPARENT);

    /* Elevation rings (outer 0°, then 15°, 30°, 45°, 60°, 75°) */
    HPEN penRing = CreatePen(PS_SOLID, 1, SKY_RING_COLOR);
    HPEN penOld  = (HPEN)SelectObject(hdc, penRing);
    HBRUSH brOld = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

    const int els[] = { 0, 15, 30, 45, 60, 75 };
    const int nEls = (int)(sizeof(els) / sizeof(els[0]));
    for (int i = 0; i < nEls; i++) {
        int r = (int)((90 - els[i]) / 90.0 * radius + 0.5);
        Ellipse(hdc, cx - r, cy - r, cx + r, cy + r);
    }

    /* N-S and E-W crosshair (dotted) */
    HPEN penAxis = CreatePen(PS_DOT, 1, SKY_AXIS_COLOR);
    SelectObject(hdc, penAxis);
    MoveToEx(hdc, cx - radius, cy, NULL); LineTo(hdc, cx + radius, cy);
    MoveToEx(hdc, cx, cy - radius, NULL); LineTo(hdc, cx, cy + radius);

    /* Compass labels */
    SetTextColor(hdc, SKY_LABEL_COLOR);
    SetTextAlign(hdc, TA_CENTER | TA_TOP);
    TextOut(hdc, cx, cy - radius - 18, "N", 1);
    TextOut(hdc, cx, cy + radius + 2,  "S", 1);
    SetTextAlign(hdc, TA_LEFT | TA_TOP);
    TextOut(hdc, cx + radius + 4, cy - 7, "E", 1);
    SetTextAlign(hdc, TA_RIGHT | TA_TOP);
    TextOut(hdc, cx - radius - 4, cy - 7, "W", 1);

    /* Elevation labels along north side of the N-S axis */
    SetTextAlign(hdc, TA_LEFT | TA_TOP);
    SetTextColor(hdc, SKY_AXIS_COLOR);
    for (int i = 1; i < nEls; i++) {
        int r = (int)((90 - els[i]) / 90.0 * radius + 0.5);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", els[i]);
        TextOut(hdc, cx + 3, cy - r - 2, buf, (int)strlen(buf));
    }

    SelectObject(hdc, penOld);
    SelectObject(hdc, brOld);
    DeleteObject(penRing);
    DeleteObject(penAxis);

    if (out_cx)     *out_cx     = cx;
    if (out_cy)     *out_cy     = cy;
    if (out_radius) *out_radius = radius;
}

/**
 * @brief Draw all tracked satellites onto the polar grid.
 *
 * Reads SkyPlotState from the AppState, projects each (az, el) to pixels
 * using radius = (90 - el)/90 * outer_radius and angle = az clockwise
 * from north, and draws a coloured disc + PRN label per SV.  Stale
 * markers (>SKY_STALE_S since last update) are drawn grey.  Returns the
 * number of markers drawn so the caller can update the status line.
 */
static int DrawSkyMarkers(HDC hdc, const AppState *state,
                          int cx, int cy, int radius)
{
    int drawn = 0;
    double now = gui_get_time_seconds();

    HPEN penOutline = CreatePen(PS_SOLID, 1, SKY_MARKER_OUTLINE);
    HPEN penOld     = (HPEN)SelectObject(hdc, penOutline);
    HBRUSH brOld    = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

    SetBkMode(hdc, TRANSPARENT);
    SetTextAlign(hdc, TA_LEFT | TA_TOP);

    for (int g = 0; g < SV_EPH_MAX_GNSS; g++) {
        char sys;
        COLORREF color_fresh;
        switch (g) {
        case 1: sys = 'G'; color_fresh = SKY_GPS_COLOR; break;
        case 3: sys = 'E'; color_fresh = SKY_GAL_COLOR; break;
        default: continue;
        }

        for (int p = 1; p <= SV_EPH_MAX_SATS_PER_GNSS; p++) {
            const SkySat *s = &state->skyState.sats[g][p - 1];
            if (!s->valid) continue;

            double age = now - s->last_seen_ts;
            if (age > SKY_DROP_S) continue;

            COLORREF c = (age > SKY_STALE_S) ? SKY_DIM_COLOR : color_fresh;

            /* Polar -> Cartesian.  Y axis is flipped for screen coords. */
            double az_rad = s->az_deg * M_PI / 180.0;
            double r_px   = (90.0 - s->el_deg) / 90.0 * (double)radius;
            int x = cx + (int)(r_px * sin(az_rad) + 0.5);
            int y = cy - (int)(r_px * cos(az_rad) + 0.5);

            HBRUSH br = CreateSolidBrush(c);
            HBRUSH oldFill = (HBRUSH)SelectObject(hdc, br);
            Ellipse(hdc, x - 5, y - 5, x + 6, y + 6);
            SelectObject(hdc, oldFill);
            DeleteObject(br);

            /* PRN label */
            char id[8];
            snprintf(id, sizeof(id), "%c%02d", sys, p);
            SetTextColor(hdc, SKY_LABEL_COLOR);
            TextOut(hdc, x + 7, y - 8, id, (int)strlen(id));

            drawn++;
        }
    }

    SelectObject(hdc, penOld);
    SelectObject(hdc, brOld);
    DeleteObject(penOutline);

    return drawn;
}

static LRESULT CALLBACK SkyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)wParam;

    switch (msg) {

    case WM_CREATE: {
        CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return 0;
    }

    case WM_ERASEBKGND:
        /* Skip — WM_PAINT fully covers the client via a double buffer */
        return 1;

    case WM_PAINT: {
        AppState *state = (AppState *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

        PAINTSTRUCT ps;
        HDC hdcScreen = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        HDC     hdcMem  = CreateCompatibleDC(hdcScreen);
        HBITMAP bmpMem  = CreateCompatibleBitmap(hdcScreen, w, h);
        HBITMAP bmpOld  = (HBITMAP)SelectObject(hdcMem, bmpMem);
        HFONT   hFont   = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HFONT   hFontOld = (HFONT)SelectObject(hdcMem, hFont);

        int cx = 0, cy = 0, radius = 0;
        DrawSkyBackground(hdcMem, w, h, &cx, &cy, &radius);

        int drawn = state ? DrawSkyMarkers(hdcMem, state, cx, cy, radius) : 0;

        /* ── Diagnostic status line ────────────────────────────────
         * Distinguishes the two prerequisites so the user knows what
         * to look for in their RTCM stream. */
        bool   have_arp = false;
        double arp_lat = 0, arp_lon = 0;
        rtcm_get_station_arp(&have_arp, NULL, NULL, NULL,
                             &arp_lat, &arp_lon, NULL);

        int eph_gps = 0, eph_gal = 0;
        for (int p = 1; p <= SV_EPH_MAX_SATS_PER_GNSS; p++) {
            if (sv_eph_get(1, p)) eph_gps++;
            if (sv_eph_get(3, p)) eph_gal++;
        }

        SetTextAlign(hdcMem, TA_LEFT | TA_TOP);
        SetTextColor(hdcMem, SKY_STATUS_COLOR);

        char line1[96];
        if (drawn > 0) {
            snprintf(line1, sizeof(line1),
                     "Tracking %d satellite%s  (G:%d  E:%d  eph)",
                     drawn, drawn == 1 ? "" : "s", eph_gps, eph_gal);
        } else if (!have_arp && eph_gps == 0 && eph_gal == 0) {
            snprintf(line1, sizeof(line1),
                     "Waiting: no RTCM 1005/1006 + no 1019/1045 yet");
        } else if (!have_arp) {
            snprintf(line1, sizeof(line1),
                     "Have %d GPS + %d Galileo eph; need RTCM 1005/1006",
                     eph_gps, eph_gal);
        } else if (eph_gps == 0 && eph_gal == 0) {
            snprintf(line1, sizeof(line1),
                     "Have station ARP; need RTCM 1019 (GPS) or 1045/1046 (Galileo)");
        } else {
            snprintf(line1, sizeof(line1),
                     "ARP + %d/%d eph (G/E); awaiting next MSM frame",
                     eph_gps, eph_gal);
        }
        TextOut(hdcMem, 8, 6, line1, (int)strlen(line1));

        BitBlt(hdcScreen, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);

        SelectObject(hdcMem, hFontOld);
        SelectObject(hdcMem, bmpOld);
        DeleteObject(bmpMem);
        DeleteDC(hdcMem);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_DESTROY: {
        AppState *state = (AppState *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (state && state->hSkyWnd == hwnd) {
            /* Capture the un-minimised screen rect so the next open
             * restores the same size and position.  WS_EX_TOOLWINDOW
             * makes rcNormalPosition use screen (not workspace) coords. */
            WINDOWPLACEMENT wp;
            ZeroMemory(&wp, sizeof(wp));
            wp.length = sizeof(wp);
            if (GetWindowPlacement(hwnd, &wp)) {
                state->skyWndRect      = wp.rcNormalPosition;
                state->skyWndRectValid = TRUE;
            }
            state->hSkyWnd = NULL;
        }
        return 0;
    }
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

BOOL RegisterSkyWindowClass(HINSTANCE hInst)
{
    static BOOL registered = FALSE;
    if (registered) return TRUE;

    WNDCLASSEX wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = SkyWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;            /* we paint everything ourselves */
    wc.lpszClassName = SKY_WINDOW_CLASS;
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassEx(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return FALSE;
    }
    registered = TRUE;
    return TRUE;
}

HWND CreateSkyWindow(HINSTANCE hInst, HWND hOwner, AppState *state)
{
    if (!RegisterSkyWindowClass(hInst)) return NULL;

    /* Restore the previous size and position if we have one;
     * otherwise let the OS pick a default location. */
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    int w = SKY_WIN_DEF_W,  h = SKY_WIN_DEF_H;
    if (state && state->skyWndRectValid) {
        x = state->skyWndRect.left;
        y = state->skyWndRect.top;
        w = state->skyWndRect.right  - state->skyWndRect.left;
        h = state->skyWndRect.bottom - state->skyWndRect.top;
        /* Guard against zero/negative sizes from a corrupt rect */
        if (w < 120) w = SKY_WIN_DEF_W;
        if (h < 120) h = SKY_WIN_DEF_H;
    }

    return CreateWindowEx(
        WS_EX_TOOLWINDOW,
        SKY_WINDOW_CLASS,
        "Sky Plot - NTRIP-Analyser",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME |
            WS_MINIMIZEBOX | WS_VISIBLE,
        x, y, w, h,
        hOwner, NULL, hInst, state);
}
