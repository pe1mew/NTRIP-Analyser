/**
 * @file gui_signal_window.c
 * @brief Floating signal-quality (C/N0) window -- implementation.
 *
 * Layout (top to bottom):
 *   - Header strip: tracked-SV count, mean/min/max C/N0 this epoch.
 *   - Signal bars:  one bar per tracked SV, current-epoch C/N0.
 *   - Scatter:      C/N0 vs elevation over the whole session, with a
 *                   per-constellation binned mean polyline.
 *   - Legend/footer: constellation swatches and sample count.
 *
 * Threading: lives entirely on the UI thread.  Reads AppState.skyState,
 * which is written by the WM_APP_SKY_UPDATE handler on the same thread,
 * so no locking is needed.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "gui_signal_window.h"
#include "gui_snapshot.h"
#include "resource.h"
#include "net/ntrip_handler.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <windowsx.h>   /* GET_X_LPARAM / GET_Y_LPARAM */

#define SIG_WIN_DEF_W    760
#define SIG_WIN_DEF_H    720

#define SIG_BG_COLOR     RGB(255, 255, 255)
#define SIG_HEADER_BG    RGB(245, 245, 245)
#define SIG_PANEL_BG     RGB(252, 252, 252)
#define SIG_LABEL_COLOR  RGB(  0,   0,   0)
#define SIG_MUTED_COLOR  RGB(120, 120, 120)
#define SIG_GRID_COLOR   RGB(215, 215, 215)
#define SIG_AXIS_COLOR   RGB(140, 140, 140)

/* Constellation colours -- deliberately identical to gui_sky_window.c so
 * a satellite reads as the same colour in both windows. */
#define SIG_GPS_COLOR    RGB( 40, 140,  40)   /* G -- green   */
#define SIG_GLO_COLOR    RGB(210,  40,  40)   /* R -- red     */
#define SIG_GAL_COLOR    RGB( 30,  80, 200)   /* E -- blue    */
#define SIG_QZS_COLOR    RGB(180,  40, 160)   /* J -- magenta */
#define SIG_BDS_COLOR    RGB(220, 130,  20)   /* C -- orange  */
#define SIG_NAVIC_COLOR  RGB( 30, 170, 180)   /* I -- teal    */
#define SIG_OTHER_COLOR  RGB(120, 120, 120)

#define SIG_HEADER_H      64
#define SIG_FOOTER_H      26
#define SIG_PAD           12

/** SVs whose last update is older than this are not drawn in the bar view. */
#define SIG_STALE_S       5.0

/** Width reserved for the C/N0 axis labels, left of every plot area.
 * Panel titles start at this offset so they cannot collide with the
 * topmost gridline label. */
#define SIG_AXIS_W        38

/**
 * @struct SigBarHit
 * @brief One bar's screen rectangle plus what it represents.
 *
 * Rebuilt on every paint of the bar panel and used by WM_MOUSEMOVE to
 * hit-test the cursor, so the hover tooltip never disagrees with what is
 * actually on screen.
 */
typedef struct {
    RECT  r;
    int   gnss_id;
    int   prn;
    float cn0;
    float el_deg;
} SigBarHit;

static SigBarHit g_bars[SV_EPH_MAX_GNSS * SV_EPH_MAX_SATS_PER_GNSS];
static int       g_bar_count = 0;
static POINT     g_mouse     = { -1, -1 };
static BOOL      g_hovering  = FALSE;

/** C/N0 axis range.  Real GNSS C/N0 sits roughly 25..55 dB-Hz; the extra
 * headroom to 60 keeps strong signals off the ceiling rather than pinning
 * a row of bars to the top of the plot. */
#define SIG_CN0_MIN      20.0
#define SIG_CN0_MAX      60.0

/* SIG_EL_BIN_DEG / SIG_EL_BINS / SIG_SCATTER_CAP live in gui_state.h,
 * alongside the SigCnrState they describe. */

/** @brief Marker colour for a GNSS id (G=1, R=2, E=3, J=4, C=5, S=6, I=7). */
static COLORREF gnss_color(int gnss_id)
{
    switch (gnss_id) {
        case 1:  return SIG_GPS_COLOR;
        case 2:  return SIG_GLO_COLOR;
        case 3:  return SIG_GAL_COLOR;
        case 4:  return SIG_QZS_COLOR;
        case 5:  return SIG_BDS_COLOR;
        case 7:  return SIG_NAVIC_COLOR;
        default: return SIG_OTHER_COLOR;
    }
}

/** @brief Single-letter RINEX constellation prefix for a GNSS id. */
static char gnss_letter(int gnss_id)
{
    switch (gnss_id) {
        case 1:  return 'G';
        case 2:  return 'R';
        case 3:  return 'E';
        case 4:  return 'J';
        case 5:  return 'C';
        case 6:  return 'S';
        case 7:  return 'I';
        default: return '?';
    }
}

/** @brief Map a C/N0 value to a y pixel inside [y0, y0+h]. */
static int cn0_to_y(double cn0, int y0, int h)
{
    if (cn0 < SIG_CN0_MIN) cn0 = SIG_CN0_MIN;
    if (cn0 > SIG_CN0_MAX) cn0 = SIG_CN0_MAX;
    double frac = (cn0 - SIG_CN0_MIN) / (SIG_CN0_MAX - SIG_CN0_MIN);
    return y0 + h - (int)(frac * h + 0.5);
}

/** @brief Draw a filled legend swatch with a thin outline. */
static void draw_swatch(HDC hdc, int x, int y, int w, int h, COLORREF c)
{
    RECT r = { x, y, x + w, y + h };
    HBRUSH b = CreateSolidBrush(c);
    FillRect(hdc, &r, b);
    DeleteObject(b);
    FrameRect(hdc, &r, (HBRUSH)GetStockObject(GRAY_BRUSH));
}

/**
 * @brief Header strip: tracked-SV count and current-epoch C/N0 summary.
 */
static void DrawHeader(HDC hdc, AppState *state, int w)
{
    RECT hdr = { 0, 0, w, SIG_HEADER_H };
    HBRUSH bg = CreateSolidBrush(SIG_HEADER_BG);
    FillRect(hdc, &hdr, bg);
    DeleteObject(bg);

    double now = gui_get_time_seconds();
    int    n = 0;
    double sum = 0.0, lo = 1e9, hi = -1e9;

    for (int g = 0; g < SV_EPH_MAX_GNSS; g++) {
        for (int p = 0; p < SV_EPH_MAX_SATS_PER_GNSS; p++) {
            const SkySat *s = &state->skyState.sats[g][p];
            if (!s->valid || s->cnr_dbhz <= 0.0f)      continue;
            if ((now - s->last_seen_ts) > SIG_STALE_S) continue;
            n++;
            sum += s->cnr_dbhz;
            if (s->cnr_dbhz < lo) lo = s->cnr_dbhz;
            if (s->cnr_dbhz > hi) hi = s->cnr_dbhz;
        }
    }

    SetBkMode(hdc, TRANSPARENT);
    char buf[160];

    SetTextColor(hdc, SIG_LABEL_COLOR);
    if (n > 0) {
        snprintf(buf, sizeof(buf), "%d satellites   mean %.1f dB-Hz", n, sum / n);
    } else {
        snprintf(buf, sizeof(buf), "No satellites with C/N0 yet");
    }
    TextOut(hdc, SIG_PAD, 10, buf, (int)strlen(buf));

    SetTextColor(hdc, SIG_MUTED_COLOR);
    if (n > 0) {
        snprintf(buf, sizeof(buf),
                 "range %.1f to %.1f dB-Hz   (C/N0 below ~35 dB-Hz at high "
                 "elevation suggests an antenna or siting problem)", lo, hi);
    } else {
        snprintf(buf, sizeof(buf),
                 "Open an RTCM 3.x stream carrying MSM observations to populate "
                 "this view.");
    }
    TextOut(hdc, SIG_PAD, 34, buf, (int)strlen(buf));

    /* Keyboard hint, right-aligned: a shortcut nobody can see is a
     * shortcut nobody uses. */
    const char *hint = "Ctrl+S: save as PNG";
    SIZE hs;
    GetTextExtentPoint32(hdc, hint, (int)strlen(hint), &hs);
    TextOut(hdc, w - SIG_PAD - hs.cx, 10, hint, (int)strlen(hint));
}

/**
 * @brief Signal-bar view: one bar per tracked SV, current-epoch C/N0.
 *
 * Bars are ordered by constellation then PRN, so the grouping is stable
 * frame to frame and a satellite does not jump around as values change.
 */
static void DrawBars(HDC hdc, AppState *state, int x0, int y0, int w, int h)
{
    RECT panel = { x0, y0, x0 + w, y0 + h };
    HBRUSH bg = CreateSolidBrush(SIG_PANEL_BG);
    FillRect(hdc, &panel, bg);
    DeleteObject(bg);

    const int bm = 20;                 /* bottom margin for PRN labels  */
    int plot_x = x0 + SIG_AXIS_W;
    int plot_y = y0 + 20;
    int plot_w = w - SIG_AXIS_W - SIG_PAD;
    int plot_h = h - 20 - bm;
    if (plot_w < 40 || plot_h < 40) { g_bar_count = 0; return; }

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, SIG_MUTED_COLOR);
    /* Title starts at plot_x, clear of the axis-label column. */
    const char *title = "C/N0 by satellite (this epoch)";
    TextOut(hdc, plot_x, y0 + 3, title, (int)strlen(title));

    /* Horizontal gridlines + axis labels every 10 dB-Hz. */
    HPEN penGrid = CreatePen(PS_SOLID, 1, SIG_GRID_COLOR);
    HPEN oldPen  = (HPEN)SelectObject(hdc, penGrid);
    for (double v = SIG_CN0_MIN; v <= SIG_CN0_MAX + 0.1; v += 10.0) {
        int gy = cn0_to_y(v, plot_y, plot_h);
        MoveToEx(hdc, plot_x, gy, NULL);
        LineTo(hdc, plot_x + plot_w, gy);
        char lbl[16];
        snprintf(lbl, sizeof(lbl), "%.0f", v);
        /* Right-align against the plot edge so the column reads cleanly. */
        SIZE ts;
        GetTextExtentPoint32(hdc, lbl, (int)strlen(lbl), &ts);
        TextOut(hdc, plot_x - 6 - ts.cx, gy - 8, lbl, (int)strlen(lbl));
    }
    SelectObject(hdc, oldPen);
    DeleteObject(penGrid);

    /* Collect the SVs to draw, in (gnss, prn) order. */
    struct { int g, prn; float cn0, el; } list[SV_EPH_MAX_GNSS * SV_EPH_MAX_SATS_PER_GNSS];
    int n = 0;
    double now = gui_get_time_seconds();
    for (int g = 0; g < SV_EPH_MAX_GNSS; g++) {
        for (int p = 0; p < SV_EPH_MAX_SATS_PER_GNSS; p++) {
            const SkySat *s = &state->skyState.sats[g][p];
            if (!s->valid || s->cnr_dbhz <= 0.0f)      continue;
            if ((now - s->last_seen_ts) > SIG_STALE_S) continue;
            list[n].g   = g;
            list[n].prn = p + 1;
            list[n].cn0 = s->cnr_dbhz;
            list[n].el  = (float)s->el_deg;
            n++;
        }
    }

    if (n == 0) {
        g_bar_count = 0;
        SetTextColor(hdc, SIG_MUTED_COLOR);
        const char *m = "(waiting for MSM7 observations)";
        TextOut(hdc, plot_x + 8, plot_y + plot_h / 2 - 8, m, (int)strlen(m));
        return;
    }

    /* Bar geometry: cap thickness so a handful of SVs do not produce
     * absurdly wide bars, and allow them to thin down when many are up. */
    int slot = plot_w / n;
    if (slot < 3) slot = 3;
    int bw = slot - 2;
    if (bw > 24) bw = 24;
    if (bw < 2)  bw = 2;

    HPEN penAxis = CreatePen(PS_SOLID, 1, SIG_AXIS_COLOR);
    oldPen = (HPEN)SelectObject(hdc, penAxis);
    MoveToEx(hdc, plot_x, plot_y + plot_h, NULL);
    LineTo(hdc, plot_x + plot_w, plot_y + plot_h);
    SelectObject(hdc, oldPen);
    DeleteObject(penAxis);

    g_bar_count = 0;
    for (int i = 0; i < n; i++) {
        int bx = plot_x + i * slot + (slot - bw) / 2;
        int by = cn0_to_y(list[i].cn0, plot_y, plot_h);
        RECT r = { bx, by, bx + bw, plot_y + plot_h };
        HBRUSH b = CreateSolidBrush(gnss_color(list[i].g));
        FillRect(hdc, &r, b);
        DeleteObject(b);

        /* Cache for hover hit-testing.  Widen the hit zone to the full
         * slot so there are no dead gaps between bars, and extend it to
         * the top of the plot so a short bar is still easy to hit. */
        if (g_bar_count < (int)(sizeof(g_bars) / sizeof(g_bars[0]))) {
            SigBarHit *hb = &g_bars[g_bar_count++];
            hb->r.left    = plot_x + i * slot;
            hb->r.right   = plot_x + (i + 1) * slot;
            hb->r.top     = plot_y;
            hb->r.bottom  = plot_y + plot_h;
            hb->gnss_id   = list[i].g;
            hb->prn       = list[i].prn;
            hb->cn0       = list[i].cn0;
            hb->el_deg    = list[i].el;
        }

        /* PRN label, only when there is room for it to be legible. */
        if (slot >= 22) {
            char lbl[12];
            snprintf(lbl, sizeof(lbl), "%c%02d",
                     gnss_letter(list[i].g), list[i].prn);
            SetTextColor(hdc, SIG_MUTED_COLOR);
            TextOut(hdc, bx - 3, plot_y + plot_h + 3, lbl, (int)strlen(lbl));
        }
    }
}

/**
 * @brief Draw the hover tooltip for whichever bar is under the cursor.
 *
 * Called after the bar panel so the box is never painted over.  The box
 * flips to the left of / below the cursor when it would otherwise run off
 * the window edge.
 */
static void DrawBarTooltip(HDC hdc, int w, int h)
{
    if (!g_hovering) return;

    const SigBarHit *hit = NULL;
    for (int i = 0; i < g_bar_count; i++) {
        if (PtInRect(&g_bars[i].r, g_mouse)) { hit = &g_bars[i]; break; }
    }
    if (!hit) return;

    char line[96];
    snprintf(line, sizeof(line), "%c%02d   %.1f dB-Hz   el %.0f\xB0",
             gnss_letter(hit->gnss_id), hit->prn, hit->cn0, hit->el_deg);

    SetBkMode(hdc, TRANSPARENT);
    SIZE ts;
    GetTextExtentPoint32(hdc, line, (int)strlen(line), &ts);

    int pad = 6;
    int bw  = ts.cx + pad * 2 + 12;   /* +12 for the constellation swatch */
    int bh  = ts.cy + pad;
    int bx  = g_mouse.x + 14;
    int by  = g_mouse.y - bh - 6;
    if (bx + bw > w) bx = g_mouse.x - bw - 14;
    if (bx < 0)      bx = 0;
    if (by < 0)      by = g_mouse.y + 18;
    if (by + bh > h) by = h - bh;

    RECT box = { bx, by, bx + bw, by + bh };
    HBRUSH bg = CreateSolidBrush(RGB(255, 255, 225));   /* info-tip yellow */
    FillRect(hdc, &box, bg);
    DeleteObject(bg);
    FrameRect(hdc, &box, (HBRUSH)GetStockObject(GRAY_BRUSH));

    /* Constellation swatch, then the text. */
    draw_swatch(hdc, bx + pad, by + bh / 2 - 4, 8, 8, gnss_color(hit->gnss_id));
    SetTextColor(hdc, SIG_LABEL_COLOR);
    TextOut(hdc, bx + pad + 12, by + pad / 2, line, (int)strlen(line));
}

/**
 * @brief C/N0 vs elevation scatter over the whole session.
 *
 * Every sample in every SV's track buffer is plotted, so the picture
 * fills in as the session runs.  A per-constellation mean is overlaid in
 * SIG_EL_BIN_DEG bins -- that line is what makes an obstruction visible,
 * since individual samples are noisy enough to hide a several-dB dip.
 */
static void DrawScatter(HDC hdc, AppState *state, int x0, int y0, int w, int h,
                        long *out_samples)
{
    RECT panel = { x0, y0, x0 + w, y0 + h };
    HBRUSH bg = CreateSolidBrush(SIG_PANEL_BG);
    FillRect(hdc, &panel, bg);
    DeleteObject(bg);

    /* Bottom margin holds two stacked rows: the tick labels, then the
     * axis title beneath them. */
    const int bm = 38;
    int plot_x = x0 + SIG_AXIS_W;
    int plot_y = y0 + 20;
    int plot_w = w - SIG_AXIS_W - SIG_PAD;
    int plot_h = h - 20 - bm;
    if (plot_w < 60 || plot_h < 60) { *out_samples = 0; return; }

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, SIG_MUTED_COLOR);
    /* Title starts at plot_x, clear of the axis-label column. */
    const char *title = "C/N0 vs elevation (whole session)";
    TextOut(hdc, plot_x, y0 + 3, title, (int)strlen(title));

    /* Grid: C/N0 every 10 dB-Hz, elevation every 15 degrees. */
    HPEN penGrid = CreatePen(PS_SOLID, 1, SIG_GRID_COLOR);
    HPEN oldPen  = (HPEN)SelectObject(hdc, penGrid);
    for (double v = SIG_CN0_MIN; v <= SIG_CN0_MAX + 0.1; v += 10.0) {
        int gy = cn0_to_y(v, plot_y, plot_h);
        MoveToEx(hdc, plot_x, gy, NULL);
        LineTo(hdc, plot_x + plot_w, gy);
        char lbl[16];
        snprintf(lbl, sizeof(lbl), "%.0f", v);
        SIZE ts;
        GetTextExtentPoint32(hdc, lbl, (int)strlen(lbl), &ts);
        TextOut(hdc, plot_x - 6 - ts.cx, gy - 8, lbl, (int)strlen(lbl));
    }
    for (int e = 0; e <= 90; e += 15) {
        int gx = plot_x + (int)((e / 90.0) * plot_w + 0.5);
        MoveToEx(hdc, gx, plot_y, NULL);
        LineTo(hdc, gx, plot_y + plot_h);
        char lbl[16];
        snprintf(lbl, sizeof(lbl), "%d", e);
        SIZE ts;
        GetTextExtentPoint32(hdc, lbl, (int)strlen(lbl), &ts);
        TextOut(hdc, gx - ts.cx / 2, plot_y + plot_h + 4, lbl, (int)strlen(lbl));
    }
    SelectObject(hdc, oldPen);
    DeleteObject(penGrid);

    /* Axis title on its own row below the tick labels, centred by measured
     * width rather than a guessed offset. */
    SetTextColor(hdc, SIG_MUTED_COLOR);
    const char *xlab = "elevation (deg)";
    SIZE xs;
    GetTextExtentPoint32(hdc, xlab, (int)strlen(xlab), &xs);
    TextOut(hdc, plot_x + (plot_w - xs.cx) / 2, plot_y + plot_h + 20,
            xlab, (int)strlen(xlab));

    /* Point cloud from the recent-sample ring. */
    const SigCnrState *sc = &state->sigCnr;
    int start = (sc->count < SIG_SCATTER_CAP) ? 0 : sc->head;
    for (int i = 0; i < sc->count; i++) {
        int idx = (start + i) % SIG_SCATTER_CAP;
        const SigSample *sp = &sc->pts[idx];
        double el = sp->el_deg;
        if (el < 0.0)  continue;
        if (el > 90.0) el = 90.0;

        COLORREF c = gnss_color(sp->gnss_id);
        int px = plot_x + (int)((el / 90.0) * plot_w + 0.5);
        int py = cn0_to_y(sp->cnr_dbhz, plot_y, plot_h);
        SetPixel(hdc, px,     py,     c);
        SetPixel(hdc, px + 1, py,     c);
        SetPixel(hdc, px,     py + 1, c);
        SetPixel(hdc, px + 1, py + 1, c);
    }

    /* Mean overlay: 2-px polyline per constellation across populated bins.
     * Driven by the unbounded bin sums rather than the ring, so the mean
     * still reflects the whole session once the ring has wrapped.  Bins
     * with very few samples are skipped -- a mean over a handful of points
     * is noise, and drawing it would imply more confidence than exists. */
    for (int g = 0; g < SV_EPH_MAX_GNSS; g++) {
        POINT line[SIG_EL_BINS];
        int   nl = 0;
        for (int b = 0; b < SIG_EL_BINS; b++) {
            if (sc->binCnt[g][b] < 5) continue;
            double el   = (b + 0.5) * SIG_EL_BIN_DEG;
            double mean = sc->binSum[g][b] / sc->binCnt[g][b];
            line[nl].x = plot_x + (int)((el / 90.0) * plot_w + 0.5);
            line[nl].y = cn0_to_y(mean, plot_y, plot_h);
            nl++;
        }
        if (nl >= 2) {
            HPEN pen = CreatePen(PS_SOLID, 2, gnss_color(g));
            HPEN op  = (HPEN)SelectObject(hdc, pen);
            Polyline(hdc, line, nl);
            SelectObject(hdc, op);
            DeleteObject(pen);
        }
    }

    if (sc->count == 0) {
        SetTextColor(hdc, SIG_MUTED_COLOR);
        TextOut(hdc, plot_x + 8, plot_y + plot_h / 2 - 8,
                "(waiting for MSM7 observations)", 31);
    }

    *out_samples = sc->total;
}

/** @brief Footer: constellation legend and accumulated sample count. */
static void DrawFooter(HDC hdc, AppState *state, int w, int h, long samples)
{
    (void)state;
    int y = h - SIG_FOOTER_H;
    RECT f = { 0, y, w, h };
    HBRUSH bg = CreateSolidBrush(SIG_HEADER_BG);
    FillRect(hdc, &f, bg);
    DeleteObject(bg);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, SIG_MUTED_COLOR);

    const struct { int id; const char *lbl; } items[] = {
        { 1, "GPS" }, { 2, "GLONASS" }, { 3, "Galileo" },
        { 4, "QZSS" }, { 5, "BeiDou" }, { 7, "NavIC" },
    };
    /* Reserve the right-hand end for the sample count, then lay the
     * legend out left to right using measured text widths -- a fixed
     * per-character estimate runs entries together in a proportional
     * font. */
    char buf[64];
    snprintf(buf, sizeof(buf), "%ld samples", samples);
    SIZE cs;
    GetTextExtentPoint32(hdc, buf, (int)strlen(buf), &cs);
    int count_x = w - SIG_PAD - cs.cx;
    TextOut(hdc, count_x, y + 5, buf, (int)strlen(buf));

    int x = SIG_PAD;
    for (int i = 0; i < 6; i++) {
        int len = (int)strlen(items[i].lbl);
        SIZE ts;
        GetTextExtentPoint32(hdc, items[i].lbl, len, &ts);
        int entry_w = 10 + 4 + ts.cx;
        if (x + entry_w > count_x - SIG_PAD) break;   /* no room; drop the rest */

        draw_swatch(hdc, x, y + 8, 10, 10, gnss_color(items[i].id));
        TextOut(hdc, x + 14, y + 5, items[i].lbl, len);
        x += entry_w + 14;
    }
}

static LRESULT CALLBACK SignalWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        /* 1 Hz repaint -- matches the status-bar cadence.  The scatter only
         * gains a point per SV per SKY_TRACK_INTERVAL_S, so anything faster
         * would just burn CPU. */
        SetTimer(hwnd, 1, 1000, NULL);
        return 0;
    }

    case WM_TIMER:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_MOUSEMOVE: {
        POINT p = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        /* Only repaint when the cursor actually moved to a different bar,
         * otherwise every mouse twitch would redraw the whole window. */
        int was = -1, now_i = -1;
        for (int i = 0; i < g_bar_count; i++) {
            if (PtInRect(&g_bars[i].r, g_mouse)) { was   = i; break; }
        }
        for (int i = 0; i < g_bar_count; i++) {
            if (PtInRect(&g_bars[i].r, p))       { now_i = i; break; }
        }
        g_mouse = p;
        if (!g_hovering) {
            g_hovering = TRUE;
            TRACKMOUSEEVENT tme;
            ZeroMemory(&tme, sizeof(tme));
            tme.cbSize    = sizeof(tme);
            tme.dwFlags   = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            InvalidateRect(hwnd, NULL, FALSE);
        } else if (was != now_i) {
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        g_hovering = FALSE;
        g_mouse.x  = -1;
        g_mouse.y  = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_KEYDOWN: {
        AppState *state = (AppState *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (!state) break;
        /* Ctrl+S or plain S saves the window as a PNG -- the sky plot
         * uses plain S, so both are accepted here for consistency. */
        if (wParam == 'S') {
            SaveWindowPngWithPrompt(hwnd, state->hEditLog,
                                    "Save Signal Quality as PNG",
                                    "SignalQuality", "Signal Quality plot");
            return 0;
        }
        break;
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

        HDC     hdcMem = CreateCompatibleDC(hdcScreen);
        HBITMAP bmp    = CreateCompatibleBitmap(hdcScreen, w, h);
        HBITMAP bmpOld = (HBITMAP)SelectObject(hdcMem, bmp);

        HFONT hGui     = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HFONT hOldFont = (HFONT)SelectObject(hdcMem, hGui);

        HBRUSH bg = CreateSolidBrush(SIG_BG_COLOR);
        RECT all = { 0, 0, w, h };
        FillRect(hdcMem, &all, bg);
        DeleteObject(bg);

        DrawHeader(hdcMem, state, w);

        int body_y = SIG_HEADER_H + 4;
        int body_h = h - body_y - SIG_FOOTER_H - 4;
        if (body_h < 120) body_h = 120;
        int bars_h = body_h * 2 / 5;
        if (bars_h < 90) bars_h = 90;

        long samples = 0;
        DrawBars(hdcMem, state, 0, body_y, w, bars_h);
        DrawScatter(hdcMem, state, 0, body_y + bars_h + 6,
                    w, body_h - bars_h - 6, &samples);
        DrawFooter(hdcMem, state, w, h, samples);
        /* Last, so the tip is never overpainted by a panel. */
        DrawBarTooltip(hdcMem, w, h);

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
        if (state && state->hSignalWnd == hwnd) {
            WINDOWPLACEMENT wp;
            ZeroMemory(&wp, sizeof(wp));
            wp.length = sizeof(wp);
            if (GetWindowPlacement(hwnd, &wp)) {
                state->signalWndRect      = wp.rcNormalPosition;
                state->signalWndRectValid = TRUE;
            }
            state->hSignalWnd = NULL;
        }
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

BOOL RegisterSignalWindowClass(HINSTANCE hInst)
{
    static BOOL registered = FALSE;
    if (registered) return TRUE;

    WNDCLASSEX wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = SignalWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = SIGNAL_WINDOW_CLASS;
    wc.hIcon         = GuiLoadAppIcon(FALSE);
    wc.hIconSm       = GuiLoadAppIcon(TRUE);

    if (!RegisterClassEx(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return FALSE;
    }
    registered = TRUE;
    return TRUE;
}

HWND CreateSignalWindow(HINSTANCE hInst, HWND hOwner, AppState *state)
{
    if (!RegisterSignalWindowClass(hInst)) return NULL;

    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    int w = SIG_WIN_DEF_W,  h = SIG_WIN_DEF_H;
    if (state && state->signalWndRectValid) {
        x = state->signalWndRect.left;
        y = state->signalWndRect.top;
        w = state->signalWndRect.right  - state->signalWndRect.left;
        h = state->signalWndRect.bottom - state->signalWndRect.top;
        if (w < 360 || h < 360) { w = SIG_WIN_DEF_W; h = SIG_WIN_DEF_H; }
    }

    HWND hwnd = CreateWindowEx(
        WS_EX_TOOLWINDOW,
        SIGNAL_WINDOW_CLASS, "Signal Quality",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        x, y, w, h,
        hOwner, NULL, hInst, state);
    if (hwnd) {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
    }
    return hwnd;
}
