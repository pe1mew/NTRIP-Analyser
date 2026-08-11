/**
 * @file gui_iono_sky_window.c
 * @brief Polar ROTI sky view -- implementation.
 *
 * Reads `AppState.skyState` only; every satellite position and trail
 * point already carries the ROTI measured at that moment, so this window
 * adds presentation, not measurement.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * @copyright Apache License 2.0 with Commons Clause (see LICENSE for details)
 */

#include "gui_iono_sky_window.h"
#include "gui_snapshot.h"     /* SaveWindowPngWithPrompt */
#include "gui_sky_window.h"    /* SKY_WIN_DEF_W/H, shared default size */
#include <math.h>
#include <stdio.h>
#include <time.h>

#define IONOSKY_CLASS   "NtripIonoSkyWindow"
#define IDT_IONOSKY     1
#define IONOSKY_TOP     58      /* header rows above the plot */

/** @brief Verdict colour: grey unknown, green quiet, amber, red. */
static COLORREF isky_colour(float roti)
{
    if (roti < 0.0f)                 return RGB(240, 240, 240);
    if (roti >= IONO_ROTI_DISTURBED) return RGB(215,  40,  40);
    if (roti >= IONO_ROTI_UNSETTLED) return RGB(230, 160,  20);
    return RGB( 70, 175,  90);
}

/** @brief One sector's most recent ROTI sample. */
typedef struct { float roti; float ts; BOOL has; } IskySector;

/**
 * @brief Fill the sector grid from every SV's trail and live position,
 *        most recent sample winning per sector.
 *
 * Uses the same elevation-band / azimuth-bin geometry as the Sky Plot's
 * coverage heatmap, so the two windows slice the sky identically.
 */
static void isky_collect(const AppState *state,
                         IskySector grid[SKY_N_EL_BANDS][SKY_MAX_AZ_BINS])
{
    memset(grid, 0, sizeof(IskySector) * SKY_N_EL_BANDS * SKY_MAX_AZ_BINS);

    for (int g = 0; g < SV_EPH_MAX_GNSS; g++) {
        for (int p = 0; p < SV_EPH_MAX_SATS_PER_GNSS; p++) {
            const SkySat *s = &state->skyState.sats[g][p];
            if (!s->valid) continue;
            /* Trail points, then the live position as the newest sample. */
            for (int i = -1; i < s->track.count; i++) {
                float az, el, rv, ts;
                if (i < 0) {
                    az = s->az_deg; el = s->el_deg; rv = s->roti;
                    ts = 3.0e9f;                     /* live beats any trail */
                } else {
                    int idx = (s->track.head - s->track.count + i +
                               2 * SKY_TRACK_CAP) % SKY_TRACK_CAP;
                    const SkyTrackPoint *tp = &s->track.pts[idx];
                    az = tp->az_deg; el = tp->el_deg;
                    rv = tp->roti;   ts = tp->ts_rel;
                }
                if (el < 0.0f || el > 90.0f) continue;
                if (rv < 0.0f) continue;             /* unmeasured: leave sector */
                int band = (int)(el / 10.0f);
                if (band >= SKY_N_EL_BANDS) band = SKY_N_EL_BANDS - 1;
                int n_az = sky_az_bins_per_band[band];
                float a = az;
                while (a < 0.0f) a += 360.0f;
                while (a >= 360.0f) a -= 360.0f;
                int bin = (n_az <= 1) ? 0 : (int)(a / (360.0f / n_az));
                if (bin >= n_az) bin = n_az - 1;
                IskySector *sec = &grid[band][bin];
                if (!sec->has || ts >= sec->ts) {
                    sec->roti = rv; sec->ts = ts; sec->has = TRUE;
                }
            }
        }
    }
}

/** @brief Draw one annular wedge, arcs approximated at ~3 deg. */
static void isky_wedge(HDC hdc, int cx, int cy,
                       double r_in, double r_out,
                       double az_lo, double az_hi)
{
    if (az_hi - az_lo >= 359.9) {          /* zenith cap: a disc */
        Ellipse(hdc, cx - (int)r_out, cy - (int)r_out,
                     cx + (int)r_out, cy + (int)r_out);
        return;
    }
    int n_seg = (int)((az_hi - az_lo) / 3.0);
    if (n_seg < 2) n_seg = 2;
    POINT pts[2 * 124 + 2];
    if (n_seg > 120) n_seg = 120;
    int n = 0;
    for (int s = 0; s <= n_seg; s++) {
        double az = (az_lo + (az_hi - az_lo) * s / n_seg) * M_PI / 180.0;
        pts[n].x = cx + (int)(r_in * sin(az) + 0.5);
        pts[n].y = cy - (int)(r_in * cos(az) + 0.5);
        n++;
    }
    for (int s = n_seg; s >= 0; s--) {
        double az = (az_lo + (az_hi - az_lo) * s / n_seg) * M_PI / 180.0;
        pts[n].x = cx + (int)(r_out * sin(az) + 0.5);
        pts[n].y = cy - (int)(r_out * cos(az) + 0.5);
        n++;
    }
    Polygon(hdc, pts, n);
}

static void isky_paint(HWND hwnd, AppState *state)
{
    PAINTSTRUCT ps;
    HDC hdcWin = BeginPaint(hwnd, &ps);
    RECT rc; GetClientRect(hwnd, &rc);
    int w = rc.right, h = rc.bottom;

    HDC hdc = CreateCompatibleDC(hdcWin);
    HBITMAP bmp = CreateCompatibleBitmap(hdcWin, w, h);
    HBITMAP oldBmp = (HBITMAP)SelectObject(hdc, bmp);

    FillRect(hdc, &rc, (HBRUSH)(COLOR_WINDOW + 1));
    SetBkMode(hdc, TRANSPARENT);
    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT oldF = (HFONT)SelectObject(hdc, font);

    int cx = w / 2;
    int cy = IONOSKY_TOP + (h - IONOSKY_TOP) / 2;
    int radius = (w < (h - IONOSKY_TOP) ? w : (h - IONOSKY_TOP)) / 2 - 24;
    if (radius < 40) radius = 40;

    /* ── Content beneath the grid ─────────────────────────────────── */
    if (state->ionoSkyHeatmap) {
        static IskySector grid[SKY_N_EL_BANDS][SKY_MAX_AZ_BINS];
        isky_collect(state, grid);
        /* Same idiom as the coverage heatmap: every sector drawn with a
         * thin grey edge and a full-strength fill.  The complete lattice
         * of edges is what makes the plot read as an instrument rather
         * than as blobs floating on white. */
        HPEN penEdge = CreatePen(PS_SOLID, 1, RGB(140, 140, 140));
        HPEN oldP = (HPEN)SelectObject(hdc, penEdge);
        for (int band = SKY_N_EL_BANDS - 1; band >= 0; band--) {
            int n_az = sky_az_bins_per_band[band];
            double el_lo = band * 10.0, el_hi = el_lo + 10.0;
            double r_out = (90.0 - el_lo) / 90.0 * radius;
            double r_in  = (90.0 - el_hi) / 90.0 * radius;
            for (int b = 0; b < n_az; b++) {
                HBRUSH br = CreateSolidBrush(grid[band][b].has
                                ? isky_colour(grid[band][b].roti)
                                : RGB(240, 240, 240));
                HBRUSH oldB = (HBRUSH)SelectObject(hdc, br);
                double aw = 360.0 / n_az;
                isky_wedge(hdc, cx, cy, r_in, r_out, b * aw, (b + 1) * aw);
                SelectObject(hdc, oldB);
                DeleteObject(br);
            }
        }
        SelectObject(hdc, oldP);
        DeleteObject(penEdge);
    }

    /* ── Grid: crosshair, elevation numbers, cardinal labels ──────── */
    HBRUSH nullBr = (HBRUSH)GetStockObject(NULL_BRUSH);
    HBRUSH oldBg = (HBRUSH)SelectObject(hdc, nullBr);
    if (!state->ionoSkyHeatmap) {
        /* Rings only in track mode -- in heatmap mode the sector edges
         * already form them. */
        HPEN penRing = CreatePen(PS_SOLID, 1, RGB(180, 180, 180));
        HPEN oldPr = (HPEN)SelectObject(hdc, penRing);
        for (int el = 0; el < 90; el += 15) {
            int r = (int)((90.0 - el) / 90.0 * radius);
            Ellipse(hdc, cx - r, cy - r, cx + r, cy + r);
        }
        SelectObject(hdc, oldPr);
        DeleteObject(penRing);
    }
    HPEN penAxis = CreatePen(PS_DOT, 1, RGB(120, 120, 120));
    HPEN oldPg = (HPEN)SelectObject(hdc, penAxis);
    MoveToEx(hdc, cx - radius, cy, NULL); LineTo(hdc, cx + radius, cy);
    MoveToEx(hdc, cx, cy - radius, NULL); LineTo(hdc, cx, cy + radius);
    SelectObject(hdc, oldPg);
    DeleteObject(penAxis);

    SetTextColor(hdc, RGB(120, 120, 120));
    for (int el = 15; el <= 75; el += 15) {
        char t[8];
        snprintf(t, sizeof(t), "%d", el);
        int r = (int)((90.0 - el) / 90.0 * radius);
        TextOut(hdc, cx + 3, cy - r - 14, t, (int)strlen(t));
    }
    SetTextColor(hdc, RGB(30, 30, 30));
    TextOut(hdc, cx - 4, cy - radius - 16, "N", 1);
    TextOut(hdc, cx + radius + 4, cy - 7, "E", 1);
    TextOut(hdc, cx - 4, cy + radius + 3, "S", 1);
    TextOut(hdc, cx - radius - 14, cy - 7, "W", 1);
    SelectObject(hdc, oldBg);

    /* ── Track mode: per-dot ROTI colours, 5x5 so they read ───────── */
    if (!state->ionoSkyHeatmap) {
        HPEN penNull = (HPEN)GetStockObject(NULL_PEN);
        HPEN oldP = (HPEN)SelectObject(hdc, penNull);
        HBRUSH br[4] = {
            CreateSolidBrush(isky_colour(-1.0f)),
            CreateSolidBrush(isky_colour(0.0f)),
            CreateSolidBrush(isky_colour((float)IONO_ROTI_UNSETTLED)),
            CreateSolidBrush(isky_colour((float)IONO_ROTI_DISTURBED)),
        };
        HBRUSH oldB = (HBRUSH)SelectObject(hdc, br[0]);
        for (int g = 0; g < SV_EPH_MAX_GNSS; g++) {
            for (int p = 0; p < SV_EPH_MAX_SATS_PER_GNSS; p++) {
                const SkySat *s = &state->skyState.sats[g][p];
                if (!s->valid) continue;
                for (int i = -1; i < s->track.count; i++) {
                    float az, el, rv; int sz;
                    if (i < 0) { az = s->az_deg; el = s->el_deg;
                                 rv = s->roti;  sz = 4; }
                    else {
                        int idx = (s->track.head - s->track.count + i +
                                   2 * SKY_TRACK_CAP) % SKY_TRACK_CAP;
                        az = s->track.pts[idx].az_deg;
                        el = s->track.pts[idx].el_deg;
                        rv = s->track.pts[idx].roti;
                        sz = 2;
                    }
                    if (el < 0.0f || el > 90.0f) continue;
                    double azr = az * M_PI / 180.0;
                    double r = (90.0 - el) / 90.0 * radius;
                    int x = cx + (int)(r * sin(azr) + 0.5);
                    int y = cy - (int)(r * cos(azr) + 0.5);
                    int bucket = (rv < 0.0f)                        ? 0
                               : (rv >= (float)IONO_ROTI_DISTURBED)  ? 3
                               : (rv >= (float)IONO_ROTI_UNSETTLED)  ? 2
                                                                     : 1;
                    SelectObject(hdc, br[bucket]);
                    Ellipse(hdc, x - sz, y - sz, x + sz + 1, y + sz + 1);
                }
            }
        }
        SelectObject(hdc, oldB);
        for (int b = 0; b < 4; b++) DeleteObject(br[b]);
        SelectObject(hdc, oldP);
    }

    /* ── Header: verdict, mode, colour legend ─────────────────────── */
    char line[200];
    SetTextColor(hdc, RGB(0, 0, 0));
    if (state->haveStats && state->lastStats.iono_roti_median >= 0.0f)
        snprintf(line, sizeof(line),
                 "Ionosphere: %s  --  median ROTI %.2f TECU/min over %d "
                 "dual-frequency SVs",
                 iono_verdict_name(state->lastStats.iono_verdict),
                 state->lastStats.iono_roti_median,
                 state->lastStats.iono_sats_dualfreq);
    else
        snprintf(line, sizeof(line),
                 "Ionosphere: waiting for dual-frequency MSM7 arcs "
                 "(~1 minute of stream)");
    TextOut(hdc, 8, 6, line, (int)strlen(line));

    snprintf(line, sizeof(line),
             state->ionoSkyHeatmap
               ? "Mode: Sector heatmap, most recent ROTI per sky sector  "
                 "[Space=tracks  S=save]"
               : "Mode: 24 h satellite tracks, each dot the ROTI at that "
                 "moment  [Space=heatmap  S=save]");
    TextOut(hdc, 8, 22, line, (int)strlen(line));

    /* Legend chips */
    static const struct { float v; const char *t; } leg[] = {
        { 0.0f,                        "quiet < 0.5"     },
        { (float)IONO_ROTI_UNSETTLED,  "unsettled"       },
        { (float)IONO_ROTI_DISTURBED,  "disturbed > 1.0" },
        { -1.0f,                       "no data"         },
    };
    int lx = 8;
    for (int i = 0; i < 4; i++) {
        HBRUSH b = CreateSolidBrush(isky_colour(leg[i].v));
        RECT chip = { lx, 40, lx + 12, 52 };
        FillRect(hdc, &chip, b);
        DeleteObject(b);
        TextOut(hdc, lx + 16, 38, leg[i].t, (int)strlen(leg[i].t));
        lx += 16 + 8 * (int)strlen(leg[i].t) + 18;
    }
    TextOut(hdc, lx + 6, 38, "(TECU/min)", 10);

    /* ── Satellite labels, track mode only ────────────────────────────
     * The letter+PRN traces a coloured trail back to its row in the
     * Ionosphere table.  Text only: the track loop above already drew
     * the live marker at its usual size, and an extra anchor dot on top
     * of it read as a smaller satellite.  The heatmap deliberately shows
     * no satellites at all -- it is a picture of the sky, not of the
     * constellation. */
    if (!state->ionoSkyHeatmap) {
        static const char gletter[8] =
            { '?', 'G', 'R', 'E', 'J', 'C', 'S', 'I' };
        SetTextColor(hdc, RGB(30, 30, 30));
        for (int g = 1; g < SV_EPH_MAX_GNSS; g++) {
            for (int p = 0; p < SV_EPH_MAX_SATS_PER_GNSS; p++) {
                const SkySat *sv = &state->skyState.sats[g][p];
                if (!sv->valid) continue;
                if (sv->el_deg < 0.0f || sv->el_deg > 90.0f) continue;
                double azr = sv->az_deg * M_PI / 180.0;
                double rr = (90.0 - sv->el_deg) / 90.0 * radius;
                int x = cx + (int)(rr * sin(azr) + 0.5);
                int y = cy - (int)(rr * cos(azr) + 0.5);
                char lbl[8];
                snprintf(lbl, sizeof(lbl), "%c%02d", gletter[g], p + 1);
                TextOut(hdc, x + 6, y - 7, lbl, (int)strlen(lbl));
            }
        }
    }

    /* ── Footer: wall clock left, mountpoint right, as the Sky Plot ── */
    {
        char foot[96];
        time_t now_t = time(NULL);
        struct tm *lt = localtime(&now_t);
        if (lt) strftime(foot, sizeof(foot), "%Y-%m-%d %H:%M:%S local", lt);
        else    snprintf(foot, sizeof(foot), "-");
        SetTextColor(hdc, RGB(120, 120, 120));
        TextOut(hdc, 8, h - 18, foot, (int)strlen(foot));
        if (state->config.MOUNTPOINT[0]) {
            SIZE sz;
            GetTextExtentPoint32(hdc, state->config.MOUNTPOINT,
                                 (int)strlen(state->config.MOUNTPOINT), &sz);
            TextOut(hdc, w - sz.cx - 8, h - 18, state->config.MOUNTPOINT,
                    (int)strlen(state->config.MOUNTPOINT));
        }
    }

    BitBlt(hdcWin, 0, 0, w, h, hdc, 0, 0, SRCCOPY);
    SelectObject(hdc, oldF);
    SelectObject(hdc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(hdc);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK IonoSkyProc(HWND hwnd, UINT msg,
                                    WPARAM wParam, LPARAM lParam)
{
    AppState *state = (AppState *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE:
        SetWindowLongPtr(hwnd, GWLP_USERDATA,
                         (LONG_PTR)((CREATESTRUCT *)lParam)->lpCreateParams);
        SetTimer(hwnd, IDT_IONOSKY, 1000, NULL);
        return 0;
    case WM_TIMER:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_PAINT:
        if (state) { isky_paint(hwnd, state); return 0; }
        break;
    case WM_ERASEBKGND:
        return 1;                       /* double-buffered in WM_PAINT */
    case WM_KEYDOWN:
        if (!state) break;
        if (wParam == VK_SPACE || wParam == 'M') {
            state->ionoSkyHeatmap = !state->ionoSkyHeatmap;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (wParam == 'S') {
            /* Shared save flow, but this window's own suffix: the Sky
             * Plot's saver names files by the Sky Plot's mode, which is
             * a different window's state entirely. */
            SaveWindowPngWithPrompt(hwnd, state->hEditLog,
                                    "Save Ionosphere Sky as PNG",
                                    state->ionoSkyHeatmap
                                        ? "ROTI-Heatmap" : "ROTI-Tracks",
                                    "Ionosphere sky");
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, IDT_IONOSKY);
        if (state) {
            /* Remember the un-minimised placement, exactly as the Sky
             * Plot does, so the next open restores the user's size. */
            WINDOWPLACEMENT wp;
            ZeroMemory(&wp, sizeof(wp));
            wp.length = sizeof(wp);
            if (GetWindowPlacement(hwnd, &wp)) {
                state->ionoSkyWndRect      = wp.rcNormalPosition;
                state->ionoSkyWndRectValid = TRUE;
            }
            if (state->hIonoSkyWnd == hwnd) state->hIonoSkyWnd = NULL;
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

HWND IonoSkyWindowOpen(HINSTANCE hInst, HWND parent, AppState *state)
{
    if (state->hIonoSkyWnd) {
        SetForegroundWindow(state->hIonoSkyWnd);
        return state->hIonoSkyWnd;
    }
    static BOOL registered = FALSE;
    if (!registered) {
        WNDCLASSEX wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = IonoSkyProc;
        wc.hInstance     = hInst;
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = IONOSKY_CLASS;
        wc.hIcon         = GuiLoadAppIcon(FALSE);
        wc.hIconSm       = GuiLoadAppIcon(TRUE);
        if (!RegisterClassEx(&wc)) return NULL;
        registered = TRUE;
    }
    state->ionoSkyHeatmap = TRUE;       /* heatmap is the readable default */
    /* Open at the Sky Plot's size, so the two polar views sit side by
     * side at the same scale.  "Its size" means the size it actually is:
     * the live window if open, else the placement it saved on closing,
     * and only then the shared factory default -- the Sky Plot remembers
     * its size across opens, so matching the define alone would only
     * match a Sky Plot nobody ever resized. */
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    int w = SKY_WIN_DEF_W,  h = SKY_WIN_DEF_H;
    RECT r;
    if (state->ionoSkyWndRectValid) {
        /* The user's own last size for this window wins over anything
         * derived from the Sky Plot. */
        x = state->ionoSkyWndRect.left;
        y = state->ionoSkyWndRect.top;
        w = state->ionoSkyWndRect.right  - state->ionoSkyWndRect.left;
        h = state->ionoSkyWndRect.bottom - state->ionoSkyWndRect.top;
    } else if (state->hSkyWnd && GetWindowRect(state->hSkyWnd, &r)) {
        w = r.right - r.left;
        h = r.bottom - r.top;
        x = r.left + 32;          /* offset so it does not stack exactly */
        y = r.top  + 32;
    } else if (state->skyWndRectValid) {
        w = state->skyWndRect.right  - state->skyWndRect.left;
        h = state->skyWndRect.bottom - state->skyWndRect.top;
    }
    if (w < 240) w = SKY_WIN_DEF_W;
    if (h < 240) h = SKY_WIN_DEF_H;

    state->hIonoSkyWnd = CreateWindowEx(WS_EX_TOOLWINDOW, IONOSKY_CLASS,
        "Ionosphere Sky - NTRIP-Analyser",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        x, y, w, h,
        parent, NULL, hInst, state);
    return state->hIonoSkyWnd;
}
