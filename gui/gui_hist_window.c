/**
 * @file gui_hist_window.c
 * @brief Floating session-history window -- implementation.
 *
 * Six stacked panels share one time axis so a fault can be read across
 * metrics at a glance: a reconnect shows as a simultaneous trough in
 * throughput and message rate, while a bad link shows as CRC spikes with
 * throughput unchanged.
 *
 * Threading: UI thread only.  Reads AppState.hist, which the 1 Hz status
 * timer fills on the same thread, so no locking is needed.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "gui_hist_window.h"
#include "gui_snapshot.h"
#include "resource.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define HW_DEF_W         860
#define HW_DEF_H         760

#define HW_BG            RGB(255, 255, 255)
#define HW_HEADER_BG     RGB(245, 245, 245)
#define HW_PANEL_BG      RGB(252, 252, 252)
#define HW_LABEL         RGB(  0,   0,   0)
#define HW_MUTED         RGB(120, 120, 120)
#define HW_GRID          RGB(224, 224, 224)
#define HW_AXIS          RGB(150, 150, 150)

#define HW_THROUGHPUT    RGB( 30,  80, 200)   /* blue   */
#define HW_FRAMES        RGB( 40, 140,  40)   /* green  */
#define HW_CRC           RGB(210,  40,  40)   /* red    */
#define HW_SATS          RGB(180,  40, 160)   /* magenta*/
#define HW_CNR           RGB(220, 130,  20)   /* orange */
#define HW_DRIFT         RGB( 30, 170, 180)   /* teal   */

#define HW_HEADER_H      52
#define HW_FOOTER_H      38    /* tick labels + axis caption */
#define HW_PAD           10
#define HW_AXIS_W        56    /* left gutter for value labels */

/** @brief One vertical time gridline: when, and which sample column it lands on. */
typedef struct {
    double t;    /**< seconds since session start */
    int    idx;  /**< logical index into the ring, 0..count-1 */
} HwTick;

/** @brief Largest number of time gridlines to draw. */
#define HW_MAX_TICKS     12

/**
 * @brief Choose a readable tick interval covering @p span seconds.
 *
 * Steps are the ones people actually read a clock in, rather than
 * arbitrary round numbers: seconds, then quarter/half minutes, then
 * minutes, then quarter/half hours.
 */
static double nice_time_step(double span, int max_ticks)
{
    static const double steps[] = {
        1, 2, 5, 10, 15, 30,             /* seconds */
        60, 120, 300, 600, 900, 1800,    /* minutes */
        3600, 7200, 10800, 21600, 43200  /* hours */
    };
    if (span <= 0.0 || max_ticks <= 0) return 1.0;
    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        if (span / steps[i] <= max_ticks) return steps[i];
    }
    return steps[sizeof(steps) / sizeof(steps[0]) - 1];
}

/**
 * @brief Format a tick label for the axis scale in use.
 *
 * Bare seconds while the session is short, m:ss once it passes a minute,
 * h:mm:ss past an hour -- so the label always reads as elapsed time
 * rather than a raw second count that the user has to divide.
 */
static void format_time_label(double t, double span, char *buf, size_t n)
{
    if (span < 60.0) {
        snprintf(buf, n, "%.0fs", t);
    } else if (span < 3600.0) {
        int m = (int)(t / 60.0);
        int s = (int)(t - m * 60.0 + 0.5);
        if (s == 60) { m++; s = 0; }
        snprintf(buf, n, "%d:%02d", m, s);
    } else {
        int h = (int)(t / 3600.0);
        int m = (int)((t - h * 3600.0) / 60.0);
        int s = (int)(t - h * 3600.0 - m * 60.0 + 0.5);
        if (s == 60) { s = 0; m++; }
        if (m == 60) { m = 0; h++; }
        snprintf(buf, n, "%d:%02d:%02d", h, m, s);
    }
}

/** Which series a panel draws. */
typedef enum {
    HW_SERIES_BYTES = 0,
    HW_SERIES_FRAMES,
    HW_SERIES_CRC,
    HW_SERIES_SATS,
    HW_SERIES_CNR,
    HW_SERIES_DRIFT,
    HW_SERIES_COUNT
} HwSeries;

/* fixed_lo/fixed_hi pin the axis when zero is not a meaningful floor.
 * Throughput, message rate, CRC and drift all read against zero -- a
 * trough to zero is the fault you are looking for.  C/N0 does not: a
 * tracked signal is never 0 dB-Hz, so a zero-based axis squeezes the
 * entire useful range of 35..55 into the top tenth of the panel and
 * hides the variation completely.  The 20..60 band matches the Signal
 * Quality window so the two read consistently. */
static const struct {
    const char *title;
    const char *unit;
    COLORREF    colour;
    double      fixed_lo;
    double      fixed_hi;   /* hi <= lo means auto-range from zero */
} k_panels[HW_SERIES_COUNT] = {
    { "Throughput",          "kB/s",    HW_THROUGHPUT, 0.0,  0.0  },
    { "Message rate",        "frame/s", HW_FRAMES,     0.0,  0.0  },
    { "CRC-24Q errors",      "per s",   HW_CRC,        0.0,  0.0  },
    { "Satellites tracked",  "count",   HW_SATS,       0.0,  0.0  },
    { "Mean C/N0",           "dB-Hz",   HW_CNR,        20.0, 60.0 },
    { "Reference drift",     "m",       HW_DRIFT,      0.0,  0.0  },
};

/** @brief Pull one series' value out of a sample, or NAN if not applicable. */
static double series_value(const HistSample *s, HwSeries which)
{
    switch (which) {
    case HW_SERIES_BYTES:  return s->bytes_per_s / 1024.0;
    case HW_SERIES_FRAMES: return s->frames_per_s;
    case HW_SERIES_CRC:    return (double)s->crc_errors;
    case HW_SERIES_SATS:   return (double)s->sats;
    case HW_SERIES_CNR:    return s->cnr_mean > 0.0f ? s->cnr_mean : NAN;
    case HW_SERIES_DRIFT:  return s->arp_delta_m >= 0.0f ? s->arp_delta_m : NAN;
    default:               return NAN;
    }
}

/**
 * @brief Draw one strip chart.
 *
 * The y range always includes zero and is padded to the next sensible
 * round number, so a flat line at a healthy value does not look like it
 * is pinned to the top of the panel, and a trough to zero is visibly a
 * trough rather than the axis floor.
 */
static void DrawPanel(HDC hdc, const HistState *h, HwSeries which,
                      int x0, int y0, int w, int hgt,
                      const HwTick *ticks, int nticks)
{
    RECT panel = { x0, y0, x0 + w, y0 + hgt };
    HBRUSH bg = CreateSolidBrush(HW_PANEL_BG);
    FillRect(hdc, &panel, bg);
    DeleteObject(bg);

    int px = x0 + HW_AXIS_W;
    int py = y0 + 14;
    int pw = w - HW_AXIS_W - HW_PAD;
    int ph = hgt - 14 - 4;
    if (pw < 40 || ph < 24) return;

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, HW_MUTED);
    char title[96];
    snprintf(title, sizeof(title), "%s (%s)",
             k_panels[which].title, k_panels[which].unit);
    TextOut(hdc, px, y0 + 1, title, (int)strlen(title));

    /* Scan for the maximum in view. */
    double vmax = 0.0;
    bool   any  = false;
    int    start = (h->count < HIST_CAP) ? 0 : h->head;
    for (int i = 0; i < h->count; i++) {
        double v = series_value(&h->pts[(start + i) % HIST_CAP], which);
        if (isnan(v)) continue;
        any = true;
        if (v > vmax) vmax = v;
    }
    if (!any) {
        SetTextColor(hdc, HW_MUTED);
        TextOut(hdc, px + 6, py + ph / 2 - 8, "(no data)", 9);
        return;
    }

    /* Axis range: fixed band where zero is not a meaningful floor,
     * otherwise zero to the max rounded up to a readable step. */
    double bot, top;
    if (k_panels[which].fixed_hi > k_panels[which].fixed_lo) {
        bot = k_panels[which].fixed_lo;
        top = k_panels[which].fixed_hi;
    } else {
        bot = 0.0;
        if (vmax <= 0.0) top = 1.0;
        else {
            double mag  = pow(10.0, floor(log10(vmax)));
            double norm = vmax / mag;
            double step = (norm <= 1.0) ? 1.0 : (norm <= 2.0) ? 2.0 :
                          (norm <= 5.0) ? 5.0 : 10.0;
            top = step * mag;
        }
    }
    double span = (top > bot) ? (top - bot) : 1.0;

    /* Gridlines and value labels at bottom, middle, top. */
    HPEN penGrid = CreatePen(PS_SOLID, 1, HW_GRID);
    HPEN oldPen  = (HPEN)SelectObject(hdc, penGrid);
    for (int k = 0; k <= 2; k++) {
        double v  = bot + span * k / 2.0;
        int    gy = py + ph - (int)(((v - bot) / span) * ph + 0.5);
        MoveToEx(hdc, px, gy, NULL);
        LineTo(hdc, px + pw, gy);

        char lbl[32];
        if (span >= 10.0) snprintf(lbl, sizeof(lbl), "%.0f", v);
        else              snprintf(lbl, sizeof(lbl), "%.1f", v);
        SIZE ts;
        GetTextExtentPoint32(hdc, lbl, (int)strlen(lbl), &ts);
        TextOut(hdc, px - 6 - ts.cx, gy - 8, lbl, (int)strlen(lbl));
    }

    /* Vertical time gridlines, at the same instants in every panel, so a
     * feature can be traced across metrics -- a reconnect should line up
     * across throughput and message rate. */
    for (int t = 0; t < nticks; t++) {
        if (h->count <= 0) break;
        int gx = px + (int)((double)ticks[t].idx / h->count * pw + 0.5);
        if (gx < px || gx > px + pw) continue;
        MoveToEx(hdc, gx, py, NULL);
        LineTo(hdc, gx, py + ph);
    }

    SelectObject(hdc, oldPen);
    DeleteObject(penGrid);

    /* The series itself.  One pixel column per sample when the buffer is
     * wider than the panel, so a long session compresses rather than
     * scrolling out of view. */
    HPEN pen = CreatePen(PS_SOLID, 1, k_panels[which].colour);
    oldPen = (HPEN)SelectObject(hdc, pen);

    bool pen_down = false;
    for (int col = 0; col < pw; col++) {
        /* Map this pixel column back to a sample range and take the
         * maximum, so a one-second CRC spike survives compression rather
         * than being averaged into invisibility. */
        int i0 = (int)((double)col       / pw * h->count);
        int i1 = (int)((double)(col + 1) / pw * h->count);
        if (i1 <= i0) i1 = i0 + 1;
        if (i0 >= h->count) break;

        double peak = NAN;
        for (int i = i0; i < i1 && i < h->count; i++) {
            double v = series_value(&h->pts[(start + i) % HIST_CAP], which);
            if (isnan(v)) continue;
            if (isnan(peak) || v > peak) peak = v;
        }
        if (isnan(peak)) { pen_down = false; continue; }

        int gx = px + col;
        int gy = py + ph - (int)(((peak - bot) / span) * ph + 0.5);
        if (gy < py)          gy = py;
        if (gy > py + ph)     gy = py + ph;

        if (!pen_down) { MoveToEx(hdc, gx, gy, NULL); pen_down = true; }
        else           { LineTo(hdc, gx, gy); }
    }
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    /* Baseline. */
    HPEN penAxis = CreatePen(PS_SOLID, 1, HW_AXIS);
    oldPen = (HPEN)SelectObject(hdc, penAxis);
    MoveToEx(hdc, px, py + ph, NULL);
    LineTo(hdc, px + pw, py + ph);
    SelectObject(hdc, oldPen);
    DeleteObject(penAxis);
}

/** @brief Header: session length and what the window is showing. */
static void DrawHeader(HDC hdc, const HistState *h, int w)
{
    RECT hdr = { 0, 0, w, HW_HEADER_H };
    HBRUSH bg = CreateSolidBrush(HW_HEADER_BG);
    FillRect(hdc, &hdr, bg);
    DeleteObject(bg);

    SetBkMode(hdc, TRANSPARENT);
    char buf[192];

    SetTextColor(hdc, HW_LABEL);
    if (h->count > 0) {
        int    last = (h->head - 1 + HIST_CAP) % HIST_CAP;
        double span = h->pts[last].ts_rel;
        if (span >= 3600.0)
            snprintf(buf, sizeof(buf), "Session %.1f hours   %d samples",
                     span / 3600.0, h->count);
        else if (span >= 60.0)
            snprintf(buf, sizeof(buf), "Session %.0f min %02.0f s   %d samples",
                     floor(span / 60.0), fmod(span, 60.0), h->count);
        else
            snprintf(buf, sizeof(buf), "Session %.0f s   %d samples",
                     span, h->count);
    } else {
        snprintf(buf, sizeof(buf), "No samples yet");
    }
    TextOut(hdc, HW_PAD, 8, buf, (int)strlen(buf));

    SetTextColor(hdc, HW_MUTED);
    if (h->count >= HIST_CAP)
        snprintf(buf, sizeof(buf),
                 "Buffer full -- the oldest samples are being overwritten. "
                 "Panels share one time axis; older is left.");
    else
        snprintf(buf, sizeof(buf),
                 "Panels share one time axis; older is left. A dropout shows "
                 "as a simultaneous trough in throughput and message rate.");
    TextOut(hdc, HW_PAD, 30, buf, (int)strlen(buf));

    /* Keyboard hint, right-aligned: a shortcut nobody can see is a
     * shortcut nobody uses. */
    const char *hint = "Ctrl+S: save as PNG";
    SIZE hs;
    GetTextExtentPoint32(hdc, hint, (int)strlen(hint), &hs);
    TextOut(hdc, w - HW_PAD - hs.cx, 8, hint, (int)strlen(hint));
}

static LRESULT CALLBACK HistWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return 0;
    }

    case WM_KEYDOWN: {
        AppState *state = (AppState *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (!state) break;
        /* Ctrl+S or plain S saves the window as a PNG -- the sky plot
         * uses plain S, so both are accepted here for consistency. */
        if (wParam == 'S') {
            SaveWindowPngWithPrompt(hwnd, state->hEditLog,
                                    "Save Session History as PNG",
                                    "SessionHistory", "Session history");
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

        HBRUSH bg = CreateSolidBrush(HW_BG);
        RECT all = { 0, 0, w, h };
        FillRect(hdcMem, &all, bg);
        DeleteObject(bg);

        const HistState *hist = &state->hist;
        DrawHeader(hdcMem, hist, w);

        /* ── Time axis ─────────────────────────────────────────
         * Ticks are placed by locating the sample nearest each round
         * time, then using that sample's position in the ring.  Going
         * via the sample index rather than interpolating time directly
         * keeps the gridlines aligned with the traces even if sampling
         * stutters, since the traces are drawn by index too. */
        HwTick ticks[HW_MAX_TICKS];
        int    nticks  = 0;
        double t_first = 0.0, t_last = 0.0, span = 0.0;

        if (hist->count > 0) {
            int start   = (hist->count < HIST_CAP) ? 0 : hist->head;
            int last_i  = (hist->head - 1 + HIST_CAP) % HIST_CAP;
            t_first = hist->pts[start].ts_rel;
            t_last  = hist->pts[last_i].ts_rel;
            span    = t_last - t_first;

            if (span > 0.0) {
                double step = nice_time_step(span, HW_MAX_TICKS);
                double t0   = ceil(t_first / step) * step;
                for (double t = t0; t <= t_last + 1e-6 && nticks < HW_MAX_TICKS;
                     t += step) {
                    int best = 0;
                    double bestd = 1e18;
                    for (int i = 0; i < hist->count; i++) {
                        double d = fabs(hist->pts[(start + i) % HIST_CAP].ts_rel - t);
                        if (d < bestd) { bestd = d; best = i; }
                        if (hist->pts[(start + i) % HIST_CAP].ts_rel > t) break;
                    }
                    ticks[nticks].t   = t;
                    ticks[nticks].idx = best;
                    nticks++;
                }
            }
        }

        int body_y = HW_HEADER_H + 2;
        int body_h = h - body_y - HW_FOOTER_H;
        int panel_h = body_h / HW_SERIES_COUNT;
        if (panel_h > 20) {
            for (int i = 0; i < HW_SERIES_COUNT; i++) {
                DrawPanel(hdcMem, hist, (HwSeries)i,
                          0, body_y + i * panel_h, w, panel_h - 2,
                          ticks, nticks);
            }
        }

        /* ── Footer: the shared time axis, labelled ────────────── */
        RECT f = { 0, h - HW_FOOTER_H, w, h };
        HBRUSH fb = CreateSolidBrush(HW_HEADER_BG);
        FillRect(hdcMem, &f, fb);
        DeleteObject(fb);
        SetBkMode(hdcMem, TRANSPARENT);

        int ax_x = HW_AXIS_W;
        int ax_w = w - HW_AXIS_W - HW_PAD;
        int ax_y = h - HW_FOOTER_H;

        if (hist->count > 0 && ax_w > 40) {
            /* Axis line with a tick mark under each gridline. */
            HPEN penAxis = CreatePen(PS_SOLID, 1, HW_AXIS);
            HPEN oldP    = (HPEN)SelectObject(hdcMem, penAxis);
            MoveToEx(hdcMem, ax_x, ax_y, NULL);
            LineTo(hdcMem, ax_x + ax_w, ax_y);

            SetTextColor(hdcMem, HW_MUTED);
            int last_label_end = -1000;
            for (int t = 0; t < nticks; t++) {
                int gx = ax_x + (int)((double)ticks[t].idx / hist->count * ax_w + 0.5);
                MoveToEx(hdcMem, gx, ax_y, NULL);
                LineTo(hdcMem, gx, ax_y + 4);

                char lbl[32];
                format_time_label(ticks[t].t, span, lbl, sizeof(lbl));
                SIZE ts;
                GetTextExtentPoint32(hdcMem, lbl, (int)strlen(lbl), &ts);
                int lx = gx - ts.cx / 2;
                /* Skip a label rather than overprint its neighbour. */
                if (lx > last_label_end + 6) {
                    TextOut(hdcMem, lx, ax_y + 6, lbl, (int)strlen(lbl));
                    last_label_end = lx + ts.cx;
                }
            }
            SelectObject(hdcMem, oldP);
            DeleteObject(penAxis);

            /* Session start on the left, "now" on the right, so the
             * direction of time is never ambiguous. */
            char lbl[48];
            format_time_label(t_first, span, lbl, sizeof(lbl));
            SIZE ts;
            GetTextExtentPoint32(hdcMem, lbl, (int)strlen(lbl), &ts);
            TextOut(hdcMem, HW_PAD, ax_y + 6, "start", 5);

            const char *nowlbl = "now";
            GetTextExtentPoint32(hdcMem, nowlbl, 3, &ts);
            TextOut(hdcMem, w - HW_PAD - ts.cx, ax_y + 20, nowlbl, 3);

            SetTextColor(hdcMem, HW_MUTED);
            const char *cap = "elapsed time";
            GetTextExtentPoint32(hdcMem, cap, (int)strlen(cap), &ts);
            TextOut(hdcMem, ax_x + (ax_w - ts.cx) / 2, ax_y + 20,
                    cap, (int)strlen(cap));
        }

        BitBlt(hdcScreen, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);

        SelectObject(hdcMem, hOldFont);
        SelectObject(hdcMem, bmpOld);
        DeleteObject(bmp);
        DeleteDC(hdcMem);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY: {
        AppState *state = (AppState *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (state && state->hHistWnd == hwnd) {
            WINDOWPLACEMENT wp;
            ZeroMemory(&wp, sizeof(wp));
            wp.length = sizeof(wp);
            if (GetWindowPlacement(hwnd, &wp)) {
                state->histWndRect      = wp.rcNormalPosition;
                state->histWndRectValid = TRUE;
            }
            state->hHistWnd = NULL;
        }
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

BOOL RegisterHistWindowClass(HINSTANCE hInst)
{
    static BOOL registered = FALSE;
    if (registered) return TRUE;

    WNDCLASSEX wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = HistWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = HIST_WINDOW_CLASS;
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassEx(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return FALSE;
    }
    registered = TRUE;
    return TRUE;
}

HWND CreateHistWindow(HINSTANCE hInst, HWND hOwner, AppState *state)
{
    if (!RegisterHistWindowClass(hInst)) return NULL;

    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    int w = HW_DEF_W,      h = HW_DEF_H;
    if (state && state->histWndRectValid) {
        x = state->histWndRect.left;
        y = state->histWndRect.top;
        w = state->histWndRect.right  - state->histWndRect.left;
        h = state->histWndRect.bottom - state->histWndRect.top;
        if (w < 400 || h < 400) { w = HW_DEF_W; h = HW_DEF_H; }
    }

    HWND hwnd = CreateWindowEx(
        WS_EX_TOOLWINDOW,
        HIST_WINDOW_CLASS, "Session History",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        x, y, w, h,
        hOwner, NULL, hInst, state);
    if (hwnd) {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
    }
    return hwnd;
}
