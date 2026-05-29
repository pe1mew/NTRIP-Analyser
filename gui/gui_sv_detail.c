/**
 * @file gui_sv_detail.c
 * @brief Live per-satellite detail window opened from the sky plot.
 *
 * Mirrors the gui_detail.c pattern (read-only multiline EDIT, Consolas
 * monospace font, refresh via posted messages) but driven by a 1-Hz
 * timer that re-reads AppState.skyState.sats[g][prn-1].
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "gui_sv_detail.h"
#include "resource.h"
#include "gui_state.h"
#include "rtcm3x_parser.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* Encode (g, prn) into one 32-bit value for SetWindowLongPtr. */
#define SV_PACK(g, p)   (((int)(g) << 16) | ((int)(p) & 0xFFFF))
#define SV_GNSS(v)      ((int)(((v) >> 16) & 0xFFFF))
#define SV_PRN(v)       ((int)((v) & 0xFFFF))

#define IDC_SV_DETAIL_EDIT  4101
#define IDC_SV_DETAIL_COPY  4102
#define IDT_SV_DETAIL_TICK  4001

/* Button strip dimensions reserved at the top of the client area.
 * Strip is taller than the button so it can sit centred vertically. */
#define SV_BTN_H     26
#define SV_BTN_W     90
#define SV_BTN_PAD    6
#define SV_STRIP_H   (SV_BTN_H + 2 * SV_BTN_PAD)
#define SV_BTN_Y     SV_BTN_PAD

static BOOL      g_svDetailClassRegistered = FALSE;
/* The app has a single AppState; cache it here so the WndProc can find
 * it without a Set/GetProp dance that races with WM_CREATE. */
static AppState *g_appState = NULL;

/* ── GNSS short / long name lookup ────────────────────────── */
static char gnss_letter(int g)
{
    switch (g) {
    case 1: return 'G'; case 2: return 'R'; case 3: return 'E';
    case 4: return 'J'; case 5: return 'C';
    case 6: return 'S'; case 7: return 'I';
    default: return '?';
    }
}
static const char *gnss_name(int g)
{
    switch (g) {
    case 1: return "GPS";     case 2: return "GLONASS"; case 3: return "Galileo";
    case 4: return "QZSS";    case 5: return "BeiDou";
    case 6: return "SBAS";    case 7: return "NavIC";
    default: return "Unknown";
    }
}

/* ── Format current SV state into the EDIT control ────────── */
static void sv_detail_refresh(HWND hwnd)
{
    AppState *state = g_appState;
    if (!state) return;

    int packed = (int)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    int g = SV_GNSS(packed);
    int p = SV_PRN(packed);
    if (g < 0 || g >= SV_EPH_MAX_GNSS) return;
    if (p < 1 || p > SV_EPH_MAX_SATS_PER_GNSS) return;

    const SkySat *s = &state->skyState.sats[g][p - 1];

    double now = gui_get_time_seconds();
    double age = (s->valid) ? (now - s->last_seen_ts) : -1.0;

    char cnr_str[32];
    if (s->cnr_dbhz > 0.0f)
        snprintf(cnr_str, sizeof(cnr_str), "%.2f dB-Hz", s->cnr_dbhz);
    else
        snprintf(cnr_str, sizeof(cnr_str), "(unknown)");

    char age_str[32];
    if (!s->valid)              snprintf(age_str, sizeof(age_str), "(never tracked)");
    else if (age >= 1000.0)     snprintf(age_str, sizeof(age_str), "%.0f s ago", age);
    else                        snprintf(age_str, sizeof(age_str), "%.1f s ago", age);

    /* ARP context (so the user knows where the az/el is measured from) */
    bool   have_arp = false;
    double lat = 0, lon = 0, alt = 0;
    rtcm_get_station_arp(&have_arp, NULL, NULL, NULL, &lat, &lon, &alt);

    /* Format wall clock so a snapshot of the window is self-explanatory */
    time_t   now_t = time(NULL);
    struct tm *lt  = localtime(&now_t);
    char wall[40] = "";
    if (lt) strftime(wall, sizeof(wall), "%Y-%m-%d %H:%M:%S local", lt);

    char body[2048];
    int  n = 0;
    n += snprintf(body + n, sizeof(body) - n,
                  "Satellite %c%02d   (%s)\r\n"
                  "============================================\r\n\r\n",
                  gnss_letter(g), p, gnss_name(g));

    n += snprintf(body + n, sizeof(body) - n,
                  "Live position\r\n"
                  "  Azimuth      : %8.3f deg   (0 = N, clockwise)\r\n"
                  "  Elevation    : %8.3f deg   (90 = zenith)\r\n"
                  "  CNR (best)   : %s\r\n"
                  "  Last seen    : %s\r\n\r\n",
                  s->az_deg, s->el_deg, cnr_str, age_str);

    /* Per-band CNR section (only populated for MSM7 streams) */
    float per_band[32];
    get_sv_per_band_cnr(g, p, per_band);
    int any_band = 0;
    for (int i = 0; i < 32; i++) if (per_band[i] > 0.0f) { any_band = 1; break; }
    if (any_band) {
        n += snprintf(body + n, sizeof(body) - n,
                      "Signals tracked (per-band CNR)\r\n");
        for (int i = 0; i < 32; i++) {
            if (per_band[i] <= 0.0f) continue;
            const char *lbl = msm_signal_label(g, i);
            n += snprintf(body + n, sizeof(body) - n,
                          "  %-5s        : %6.2f dB-Hz\r\n",
                          lbl, per_band[i]);
        }
        n += snprintf(body + n, sizeof(body) - n, "\r\n");
    }

    n += snprintf(body + n, sizeof(body) - n,
                  "Station ARP\r\n"
                  "  Mountpoint   : %s\r\n",
                  state->config.MOUNTPOINT[0] ? state->config.MOUNTPOINT
                                              : "(none)");
    if (have_arp) {
        n += snprintf(body + n, sizeof(body) - n,
                  "  Lat / Lon    : %.6f, %.6f\r\n"
                  "  Altitude     : %.2f m\r\n",
                  lat, lon, alt);
    } else {
        n += snprintf(body + n, sizeof(body) - n,
                  "  Position     : (waiting for RTCM 1005/1006)\r\n");
    }

    snprintf(body + n, sizeof(body) - n,
             "\r\nRefreshed    : %s\r\n", wall);

    HWND hEdit = GetDlgItem(hwnd, IDC_SV_DETAIL_EDIT);
    if (hEdit) SetWindowText(hEdit, body);
}

/* ── Window procedure ──────────────────────────────────────── */

static LRESULT CALLBACK SvDetailWndProc(HWND hwnd, UINT msg,
                                         WPARAM wParam, LPARAM lParam)
{
    switch (msg) {

    case WM_CREATE: {
        CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
        /* Pack (g, prn) into USERDATA; AppState* arrives via the file
         * static g_appState set by ShowSkySvDetailWindow before this fires. */
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)(intptr_t)cs->lpCreateParams);

        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
        HFONT hGui = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        HWND hCopy = CreateWindowEx(
            0, "BUTTON", "Copy",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            8, SV_BTN_Y, SV_BTN_W, SV_BTN_H, hwnd,
            (HMENU)(intptr_t)IDC_SV_DETAIL_COPY, hInst, NULL);
        if (hCopy) SendMessage(hCopy, WM_SETFONT, (WPARAM)hGui, TRUE);

        HWND hEdit = CreateWindowEx(
            WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY,
            0, SV_STRIP_H, 100, 100, hwnd,
            (HMENU)(intptr_t)IDC_SV_DETAIL_EDIT, hInst, NULL);

        if (hEdit) {
            SendMessage(hEdit, EM_SETLIMITTEXT, 0x10000, 0);
            HFONT hMono = CreateFont(
                14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
            if (hMono) SendMessage(hEdit, WM_SETFONT, (WPARAM)hMono, TRUE);
        }

        SetTimer(hwnd, IDT_SV_DETAIL_TICK, 1000, NULL);
        sv_detail_refresh(hwnd);
        return 0;
    }

    case WM_SIZE: {
        int w = LOWORD(lParam), h = HIWORD(lParam);
        HWND hEdit = GetDlgItem(hwnd, IDC_SV_DETAIL_EDIT);
        if (hEdit)
            MoveWindow(hEdit, 0, SV_STRIP_H, w, h - SV_STRIP_H, TRUE);
        return 0;
    }

    case WM_COMMAND: {
        if (LOWORD(wParam) == IDC_SV_DETAIL_COPY) {
            HWND hEdit = GetDlgItem(hwnd, IDC_SV_DETAIL_EDIT);
            if (hEdit) {
                int len = GetWindowTextLength(hEdit);
                SendMessage(hEdit, EM_SETSEL, 0, len);
                SendMessage(hEdit, WM_COPY, 0, 0);
                /* Leave the selection -- visual feedback that copy ran. */
            }
            return 0;
        }
        break;
    }

    case WM_TIMER:
        if (wParam == IDT_SV_DETAIL_TICK)
            sv_detail_refresh(hwnd);
        return 0;

    case WM_CLOSE: {
        AppState *state = g_appState;
        int packed = (int)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        int g = SV_GNSS(packed);
        int p = SV_PRN(packed);
        if (state && g >= 0 && g < SV_EPH_MAX_GNSS &&
            p >= 1 && p <= SV_EPH_MAX_SATS_PER_GNSS) {
            if (state->hSvDetailWnds[g][p - 1] == hwnd)
                state->hSvDetailWnds[g][p - 1] = NULL;
        }
        DestroyWindow(hwnd);
        return 0;
    }

    case WM_DESTROY: {
        KillTimer(hwnd, IDT_SV_DETAIL_TICK);
        HWND hEdit = GetDlgItem(hwnd, IDC_SV_DETAIL_EDIT);
        if (hEdit) {
            HFONT hFont = (HFONT)SendMessage(hEdit, WM_GETFONT, 0, 0);
            if (hFont) DeleteObject(hFont);
        }
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* ── Register the class once ──────────────────────────────── */

static void ensure_sv_detail_class(HINSTANCE hInst)
{
    if (g_svDetailClassRegistered) return;

    WNDCLASSEX wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = SvDetailWndProc;
    wc.hInstance     = hInst;
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = SV_DETAIL_CLASS_NAME;
    wc.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);

    if (RegisterClassEx(&wc) ||
        GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
        g_svDetailClassRegistered = TRUE;
    }
}

HWND ShowSkySvDetailWindow(HINSTANCE hInst, HWND hOwner, AppState *state,
                           int g, int prn)
{
    if (!state) return NULL;
    if (g < 0 || g >= SV_EPH_MAX_GNSS)             return NULL;
    if (prn < 1 || prn > SV_EPH_MAX_SATS_PER_GNSS) return NULL;

    /* Cache the singleton AppState before the window's WM_CREATE fires,
     * so the first sv_detail_refresh inside WM_CREATE already has data. */
    g_appState = state;

    /* Dedup: if already open, focus and return. */
    HWND existing = state->hSvDetailWnds[g][prn - 1];
    if (existing) {
        if (IsIconic(existing)) ShowWindow(existing, SW_RESTORE);
        SetForegroundWindow(existing);
        return existing;
    }

    ensure_sv_detail_class(hInst);

    char title[64];
    snprintf(title, sizeof(title), "%c%02d - %s   (Sky Plot)",
             gnss_letter(g), prn, gnss_name(g));

    HWND hwnd = CreateWindowEx(
        0, SV_DETAIL_CLASS_NAME, title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        420, 360,
        hOwner, NULL, hInst,
        (LPVOID)(intptr_t)SV_PACK(g, prn));

    if (!hwnd) return NULL;

    state->hSvDetailWnds[g][prn - 1] = hwnd;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return hwnd;
}
