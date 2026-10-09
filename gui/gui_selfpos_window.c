/**
 * @file gui_selfpos_window.c
 * @brief Self-position window: where the station's own data puts it.
 *
 * See gui_selfpos_window.h for what this is and why it carries no
 * verdict, and design/work-items/station-self-position.md for the
 * measurement itself.
 *
 * Threading: the ring fills from the session's statistics event, which
 * arrives on the worker thread, and is drawn on the UI thread -- the
 * arrangement `AppState::lastStats`, the station check and the stability
 * report already live under. A point is written whole before `count` is
 * raised, so the painter never reads a slot that holds nothing; the
 * worst case is a repaint one epoch behind, or, at the moment the ring
 * wraps, one dot drawn from a point being overwritten. Neither survives
 * the next repaint, and no figure is derived from the ring.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "gui_selfpos_window.h"
#include "core/spp.h"       /* SppStatus, and its sentence for a failure */
#include "resource.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Height of the painted header above the plot and the figures. */
#define SP_HEADER_H   86
/* Height of the painted caption strip under the plot. */
#define SP_CAPTION_H  22
#define SP_PAD        10

#define SP_BG         RGB(255, 255, 255)
#define SP_PLOT_BG    RGB(252, 252, 252)
#define SP_GRID       RGB(224, 224, 224)
#define SP_AXIS       RGB(150, 150, 150)
#define SP_LABEL      RGB( 90,  90,  90)
#define SP_DOT        RGB( 70, 110, 200)   /* the run                   */
#define SP_DOT_LAST   RGB(220, 130,  20)   /* the epoch just solved     */
#define SP_MEAN       RGB(200,  40,  40)   /* where the run centres     */

/* ── Accumulation ────────────────────────────────────────────────────── */

void SelfPosReset(AppState *state)
{
    if (!state) return;

    /* The points only.  Everything else this window shows belongs to the
     * report's accumulator, which has its own reset. */
    state->selfpos.head        = 0;
    state->selfpos.count       = 0;
    state->selfpos.t0          = 0.0;
    state->selfpos.lastStreamT = -1e9;
    state->selfpos.haveT0      = FALSE;

    if (state->hSelfPosWnd)
        PostMessage(state->hSelfPosWnd, WM_APP_SELFPOS_UPDATE, 0, 0);
}

void SelfPosOnStats(AppState *state, const NsStatsSnapshot *s)
{
    if (!state || !s) return;

    SelfPosRing *r = &state->selfpos;

    /* An epoch that did not solve contributes no point.  A point at the
     * origin is what a station sitting exactly on its declared
     * coordinates looks like, and that is the one thing a station which
     * cannot be solved at all must not appear to be doing. */
    const bool solved = (s->selfpos_status == 0 && s->selfpos_sats > 0 &&
                         s->selfpos_e != NS_UNSET);

    /* Stamped and paced by the stream's own clock, so a replay plots the
     * span the capture holds; a snapshot republished with the same
     * stream time is the same epoch and is not plotted twice. */
    const double t = s->stream_time_s;
    if (solved && t >= 0.0 && t > r->lastStreamT) {
        r->lastStreamT = t;
        if (!r->haveT0) { r->t0 = t; r->haveT0 = TRUE; }

        SelfPosPoint p;
        p.ts_rel = (float)(t - r->t0);
        p.e      = (float)s->selfpos_e;
        p.n      = (float)s->selfpos_n;
        p.u      = (float)s->selfpos_u;

        /* Written whole, then published: a painter that reads `count`
         * first can only see slots that are complete. */
        r->pts[r->head] = p;
        r->head = (r->head + 1) % SELFPOS_CAP;
        if (r->count < SELFPOS_CAP) r->count++;
    }

    /* Repaint even when nothing solved: the figures and the reason in
     * the header move whether or not a point was added. */
    if (state->hSelfPosWnd)
        PostMessage(state->hSelfPosWnd, WM_APP_SELFPOS_UPDATE, 0, 0);
}

/* ── Figures ─────────────────────────────────────────────────────────── */

static void SetRow(HWND hLv, int row, const char *name, const char *value,
                   const char *note)
{
    if (ListView_GetItemCount(hLv) <= row) {
        LVITEM lvi;
        ZeroMemory(&lvi, sizeof(lvi));
        lvi.mask    = LVIF_TEXT;
        lvi.iItem   = row;
        lvi.pszText = (char *)name;
        ListView_InsertItem(hLv, &lvi);
    } else {
        ListView_SetItemText(hLv, row, 0, (char *)name);
    }
    ListView_SetItemText(hLv, row, 1, (char *)value);
    ListView_SetItemText(hLv, row, 2, (char *)note);
}

/**
 * @brief Fill the figures list from the report and the latest epoch.
 *
 * Window figures come from @ref StationReport -- core's accumulation
 * over the same window of stream time the Stability window shows, so
 * the two windows cannot state different numbers for one stream. The
 * last four rows are the latest epoch, from the snapshot, and are
 * labelled as such: one epoch is a diagnostic, never the measurement.
 */
static void RefreshRows(HWND hwnd, AppState *state)
{
    HWND hLv = GetDlgItem(hwnd, IDC_SELFPOS_LIST);
    if (!hLv || !state) return;

    const StationReport *r  = &state->reportOut;
    const NsStatsSnapshot *s = state->haveStats ? &state->lastStats : NULL;
    const BOOL solved_window = state->reportHave && r->sp_samples > 0;

    int row = 0;
    char v[64], note[160];

    snprintf(v, sizeof(v), "%d", solved_window ? r->sp_samples : 0);
    snprintf(note, sizeof(note), "of %d snapshot(s) in the window",
             state->reportHave ? r->samples : 0);
    SetRow(hLv, row++, "Epochs solved", v, note);

    if (solved_window) {
        snprintf(v, sizeof(v), "%+.3f m", r->sp_mean_enu[0]);
        SetRow(hLv, row++, "Mean offset east", v,
               "against the position the station broadcasts");
        snprintf(v, sizeof(v), "%+.3f m", r->sp_mean_enu[1]);
        SetRow(hLv, row++, "Mean offset north", v, "");
        snprintf(v, sizeof(v), "%+.3f m", r->sp_mean_enu[2]);
        SetRow(hLv, row++, "Mean offset up", v,
               "mostly this solution's own bias, not a moved antenna");

        snprintf(v, sizeof(v), "%.3f m", r->sp_scatter_m);
        SetRow(hLv, row++, "Scatter about the mean", v,
               "3-D RMS -- the half of it that measures something");

        snprintf(v, sizeof(v), "%.3f m", r->sp_rms_worst);
        SetRow(hLv, row++, "Worst code residual", v,
               "RMS, over the whole window");

        if (r->sp_speed_max_mms != NS_UNSET) {
            snprintf(v, sizeof(v), "%.2f mm/s", r->sp_speed_max_mms);
            SetRow(hLv, row++, "Fastest apparent motion", v,
                   "a base that is standing still reads zero");
        } else {
            SetRow(hLv, row++, "Fastest apparent motion", "--",
                   "no phase-range rates in this stream (MSM4, MSM6, legacy)");
        }
    } else {
        /* Nothing solved.  The reason, in SppStatus's own words, rather
         * than an empty window that leaves a reader guessing whether
         * the station is silent or the program is broken. */
        SetRow(hLv, row++, "Position", "not computable",
               state->reportHave
                   ? spp_status_text((SppStatus)r->sp_last_status)
                   : (state->bWorkerRunning
                          ? "waiting for the first observation epoch"
                          : "open a stream or replay a capture"));
    }

    /* ── The latest epoch ──────────────────────────────────────────── */
    if (s && s->selfpos_status == 0 && s->selfpos_sats > 0) {
        snprintf(v, sizeof(v), "%d", s->selfpos_sats);
        snprintf(note, sizeof(note), "PDOP %.1f", s->selfpos_pdop);
        SetRow(hLv, row++, "Latest epoch: satellites", v, note);

        snprintf(v, sizeof(v), "%.3f m", s->selfpos_code_rms_m);
        SetRow(hLv, row++, "Latest epoch: code residual", v, "RMS");

        if (s->selfpos_has_vel) {
            snprintf(v, sizeof(v), "%.4f m/s", s->selfpos_rate_rms_ms);
            SetRow(hLv, row++, "Latest epoch: rate residual", v,
                   "RMS -- what the velocity was measured to");
            snprintf(v, sizeof(v), "%+.4f m/s", s->selfpos_drift_ms);
            SetRow(hLv, row++, "Latest epoch: clock drift", v,
                   "the receiver's own clock, solved beside the velocity");
        }
    }

    while (ListView_GetItemCount(hLv) > row)
        ListView_DeleteItem(hLv, row);
}

/* ── Header ──────────────────────────────────────────────────────────── */

static void PaintHeader(HDC hdc, RECT *rc, AppState *state)
{
    const StationReport *r = &state->reportOut;
    const BOOL solved = state->reportHave && r->sp_samples > 0;

    /* No verdict colours here, and that is the point: this window has no
     * verdict to colour.  One neutral band for "solved", one for "not",
     * neither borrowing tier 2's green or red. */
    COLORREF bg = solved ? RGB(240, 244, 250) : RGB(245, 245, 245);
    COLORREF fg = solved ? RGB( 30,  60, 120) : RGB( 70,  70,  70);
    const char *title = solved ? "SELF-POSITION" : "NOT COMPUTABLE";

    HBRUSH br = CreateSolidBrush(bg);
    FillRect(hdc, rc, br);
    DeleteObject(br);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, fg);

    HFONT big = CreateFont(-26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HFONT old = (HFONT)SelectObject(hdc, big);
    TextOut(hdc, rc->left + 14, rc->top + 8, title, (int)strlen(title));
    SelectObject(hdc, old);
    DeleteObject(big);

    HFONT small_f = CreateFont(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    old = (HFONT)SelectObject(hdc, small_f);
    SetTextColor(hdc, RGB(60, 60, 60));

    char line[sizeof(state->config.NTRIP_CASTER) +
              sizeof(state->config.MOUNTPOINT) + 160];
    if (state->reportHave) {
        snprintf(line, sizeof(line),
                 "%s / %s   %.0f s of stream, %d epoch(s) solved%s",
                 state->config.NTRIP_CASTER, state->config.MOUNTPOINT,
                 r->window_s, r->sp_samples,
                 state->reportFromCapture ? "   (from a capture)" : "");
    } else {
        snprintf(line, sizeof(line), "%s / %s",
                 state->config.NTRIP_CASTER, state->config.MOUNTPOINT);
    }
    TextOut(hdc, rc->left + 16, rc->top + 46, line, (int)strlen(line));

    /* The third line says what is missing rather than leaving a gap:
     * either why nothing solved, or -- when it did -- that nothing here
     * is graded, which a reader who has just come from the Stability
     * window with its four verdicts will otherwise wonder about. */
    const char *why =
        solved ? "No verdict: no threshold for a station's own scatter has "
                 "been established from evidence"
               : (state->reportHave
                      ? spp_status_text((SppStatus)r->sp_last_status)
                      : "nothing measured yet");
    SetTextColor(hdc, RGB(110, 110, 110));
    TextOut(hdc, rc->left + 16, rc->top + 64, why, (int)strlen(why));

    SelectObject(hdc, old);
    DeleteObject(small_f);
}

/* ── Plot ────────────────────────────────────────────────────────────── */

/**
 * @brief Half-width of the plot, in metres: a round number that fits.
 *
 * Chosen from the points themselves rather than fixed, because the range
 * this spans is not knowable in advance -- a good dual-frequency station
 * scatters under a metre, and a station with half its constellation
 * missing can scatter tens.  Round steps only, so the rings a reader
 * measures against are numbers rather than artefacts of the data.
 */
static double PlotRadius(double widest)
{
    static const double steps[] = {
        0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0, 500.0
    };
    const double want = widest * 1.15;          /* room outside the dots */
    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++)
        if (want <= steps[i]) return steps[i];
    return steps[sizeof(steps) / sizeof(steps[0]) - 1];
}

static void PaintPlot(HDC hdc, const RECT *area, AppState *state)
{
    const SelfPosRing *r = &state->selfpos;

    HBRUSH bg = CreateSolidBrush(SP_PLOT_BG);
    FillRect(hdc, (RECT *)area, bg);
    DeleteObject(bg);

    HPEN frame = CreatePen(PS_SOLID, 1, SP_AXIS);
    HPEN oldPen = (HPEN)SelectObject(hdc, frame);
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, area->left, area->top, area->right, area->bottom);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(frame);

    const int cx = (area->left + area->right) / 2;
    const int cy = (area->top + area->bottom) / 2;
    const int half = ((area->right - area->left) < (area->bottom - area->top)
                      ? (area->right - area->left)
                      : (area->bottom - area->top)) / 2 - 18;
    if (half < 20) return;

    /* The widest point decides the scale.  The mean is included so a run
     * centred two metres away is not drawn against its own middle --
     * self-centring would hide the offset, which is one of the two
     * things this window exists to show. */
    double widest = 0.0;
    const int start = (r->count < SELFPOS_CAP) ? 0 : r->head;
    const int n = r->count;
    for (int i = 0; i < n; i++) {
        const SelfPosPoint *p = &r->pts[(start + i) % SELFPOS_CAP];
        double d = sqrt((double)p->e * p->e + (double)p->n * p->n);
        if (d > widest) widest = d;
    }
    const double radius = PlotRadius(widest);
    const double scale  = (double)half / radius;

    SetBkMode(hdc, TRANSPARENT);
    HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT oldFont = (HFONT)SelectObject(hdc, f);

    /* Rings at the full radius and half of it, and the cross through the
     * reference the station broadcasts -- which is the origin here, and
     * stays the origin. */
    HPEN grid = CreatePen(PS_SOLID, 1, SP_GRID);
    oldPen = (HPEN)SelectObject(hdc, grid);
    oldBr  = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    for (int k = 1; k <= 2; k++) {
        int rr = half * k / 2;
        Ellipse(hdc, cx - rr, cy - rr, cx + rr, cy + rr);
    }
    MoveToEx(hdc, cx - half, cy, NULL); LineTo(hdc, cx + half, cy);
    MoveToEx(hdc, cx, cy - half, NULL); LineTo(hdc, cx, cy + half);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(grid);

    SetTextColor(hdc, SP_LABEL);
    char lbl[48];
    snprintf(lbl, sizeof(lbl), "%.4g m", radius);
    TextOut(hdc, cx + half - 44, cy + 3, lbl, (int)strlen(lbl));
    snprintf(lbl, sizeof(lbl), "%.4g m", radius / 2.0);
    TextOut(hdc, cx + half / 2 - 34, cy + 3, lbl, (int)strlen(lbl));
    TextOut(hdc, cx + 4, area->top + 4, "N", 1);
    TextOut(hdc, area->right - 16, cy - 16, "E", 1);

    if (n == 0) {
        const char *none = "no epoch has solved yet";
        SetTextColor(hdc, RGB(140, 140, 140));
        TextOut(hdc, cx - 70, cy - 30, none, (int)strlen(none));
        SelectObject(hdc, oldFont);
        return;
    }

    /* The points.  Two pixels each: at one epoch a second an hour is
     * three and a half thousand of them, and a dot large enough to see
     * individually would be a solid disc long before that. */
    HBRUSH dot = CreateSolidBrush(SP_DOT);
    double sum_e = 0.0, sum_n = 0.0;
    for (int i = 0; i < n; i++) {
        const SelfPosPoint *p = &r->pts[(start + i) % SELFPOS_CAP];
        sum_e += p->e;
        sum_n += p->n;
        int x = cx + (int)lround(p->e * scale);
        int y = cy - (int)lround(p->n * scale);     /* north is up */
        if (x < area->left + 2 || x > area->right - 2 ||
            y < area->top + 2  || y > area->bottom - 2) continue;
        RECT d = { x - 1, y - 1, x + 2, y + 2 };
        FillRect(hdc, &d, dot);
    }
    DeleteObject(dot);

    /* Where the run centres, from the points drawn -- the plot's own
     * crosshair, not a published figure.  The figure beside the plot is
     * core's, over core's window; this is only here so a reader can see
     * which part of the cloud the mean is talking about. */
    {
        int x = cx + (int)lround(sum_e / n * scale);
        int y = cy - (int)lround(sum_n / n * scale);
        HPEN mean = CreatePen(PS_SOLID, 2, SP_MEAN);
        oldPen = (HPEN)SelectObject(hdc, mean);
        MoveToEx(hdc, x - 7, y, NULL); LineTo(hdc, x + 8, y);
        MoveToEx(hdc, x, y - 7, NULL); LineTo(hdc, x, y + 8);
        SelectObject(hdc, oldPen);
        DeleteObject(mean);
    }

    /* The epoch that just solved, large enough to follow while watching:
     * the newest point is the one a reader is looking for. */
    {
        const SelfPosPoint *p = &r->pts[(r->head + SELFPOS_CAP - 1)
                                        % SELFPOS_CAP];
        int x = cx + (int)lround(p->e * scale);
        int y = cy - (int)lround(p->n * scale);
        HBRUSH last = CreateSolidBrush(SP_DOT_LAST);
        RECT d = { x - 3, y - 3, x + 4, y + 4 };
        FillRect(hdc, &d, last);
        DeleteObject(last);
    }

    SelectObject(hdc, oldFont);
}

static void PaintCaption(HDC hdc, const RECT *rc, AppState *state)
{
    const SelfPosRing *r = &state->selfpos;
    FillRect(hdc, (RECT *)rc, GetSysColorBrush(COLOR_BTNFACE));

    char line[200];
    double span = 0.0;
    if (r->count > 0) {
        const int start = (r->count < SELFPOS_CAP) ? 0 : r->head;
        span = (double)r->pts[(r->head + SELFPOS_CAP - 1) % SELFPOS_CAP].ts_rel
             - (double)r->pts[start].ts_rel;
    }
    snprintf(line, sizeof(line),
             "east/north about the broadcast reference -- %d point(s) over "
             "%.0f s of stream; up is in the figures. Blue: the run.  "
             "Orange: the latest epoch.  Red cross: where it centres.",
             r->count, span);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(80, 80, 80));
    HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT old = (HFONT)SelectObject(hdc, f);
    TextOut(hdc, rc->left + 6, rc->top + 3, line, (int)strlen(line));
    SelectObject(hdc, old);
}

/* ── Window ──────────────────────────────────────────────────────────── */

/**
 * @brief Where the plot goes, and where the figures go.
 *
 * The plot takes a square of the left-hand side, because a scatter drawn
 * on unequal axes is a scatter nobody can read: a metre east has to be a
 * metre on the screen too, or the cloud's shape is the window's shape.
 */
static void PlotRect(HWND hwnd, RECT *out, int *list_x)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;

    int avail_h = h - SP_HEADER_H - SP_CAPTION_H - 2 * SP_PAD;
    int side = avail_h;
    if (side > w / 2) side = w / 2;
    if (side < 120) side = 120;

    out->left   = SP_PAD;
    out->top    = SP_HEADER_H + SP_PAD;
    out->right  = SP_PAD + side;
    out->bottom = out->top + side;
    *list_x     = out->right + SP_PAD;
}

static void LayoutChildren(HWND hwnd)
{
    RECT rc, plot;
    int list_x = 0;
    GetClientRect(hwnd, &rc);
    PlotRect(hwnd, &plot, &list_x);

    HWND hLv = GetDlgItem(hwnd, IDC_SELFPOS_LIST);
    if (!hLv) return;

    int w = rc.right - list_x - SP_PAD;
    if (w < 180) w = 180;
    MoveWindow(hLv, list_x, SP_HEADER_H + SP_PAD, w,
               rc.bottom - SP_HEADER_H - 2 * SP_PAD, TRUE);

    /* The note column takes what is left, so widening the window widens
     * the column that varies rather than leaving grey beside it. */
    int fixed = ListView_GetColumnWidth(hLv, 0)
              + ListView_GetColumnWidth(hLv, 1);
    int note = w - fixed - GetSystemMetrics(SM_CXVSCROLL) - 4;
    if (note < 120) note = 120;
    ListView_SetColumnWidth(hLv, 2, note);
}

static LRESULT CALLBACK SelfPosWndProc(HWND hwnd, UINT msg,
                                       WPARAM wParam, LPARAM lParam)
{
    AppState *state = (AppState *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
        state = (AppState *)cs->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)state);

        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);

        HWND hLv = CreateWindowEx(0, WC_LISTVIEW, "",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
            0, 0, 10, 10, hwnd, (HMENU)IDC_SELFPOS_LIST, hInst, NULL);
        ListView_SetExtendedListViewStyle(hLv,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

        HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        SendMessage(hLv, WM_SETFONT, (WPARAM)f, TRUE);

        /* Three columns, and deliberately no fourth: a Verdict column
         * is what this window does not have. */
        struct { const char *t; int w; } cols[] = {
            { "Figure", 190 }, { "Value", 90 }, { "What it means", 210 },
        };
        for (int i = 0; i < 3; i++) {
            LVCOLUMN c;
            ZeroMemory(&c, sizeof(c));
            c.mask    = LVCF_TEXT | LVCF_WIDTH;
            c.pszText = (char *)cols[i].t;
            c.cx      = cols[i].w;
            ListView_InsertColumn(hLv, i, &c);
        }

        LayoutChildren(hwnd);
        RefreshRows(hwnd, state);
        return 0;
    }

    case WM_SIZE:
        LayoutChildren(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_APP_SELFPOS_UPDATE:
        RefreshRows(hwnd, state);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        if (!state) break;

        PAINTSTRUCT ps;
        HDC hdcScreen = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;

        /* Drawn into a bitmap and blitted once: the plot repaints on
         * every epoch, and a cloud of several thousand dots drawn
         * straight to the screen flickers while it does. */
        HDC     hdcMem = CreateCompatibleDC(hdcScreen);
        HBITMAP bmp    = CreateCompatibleBitmap(hdcScreen, w, h);
        HBITMAP bmpOld = (HBITMAP)SelectObject(hdcMem, bmp);

        HBRUSH bg = CreateSolidBrush(SP_BG);
        RECT all = { 0, 0, w, h };
        FillRect(hdcMem, &all, bg);
        DeleteObject(bg);

        RECT hdr = all;
        hdr.bottom = SP_HEADER_H;
        PaintHeader(hdcMem, &hdr, state);

        RECT plot;
        int list_x = 0;
        PlotRect(hwnd, &plot, &list_x);
        PaintPlot(hdcMem, &plot, state);

        RECT cap = { plot.left, plot.bottom + 2, plot.right,
                     plot.bottom + 2 + SP_CAPTION_H };
        PaintCaption(hdcMem, &cap, state);

        BitBlt(hdcScreen, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);

        SelectObject(hdcMem, bmpOld);
        DeleteObject(bmp);
        DeleteDC(hdcMem);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CLOSE:
        if (state) {
            GetWindowRect(hwnd, &state->selfposWndRect);
            state->selfposWndRectValid = TRUE;
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        /* The ring keeps filling: it belongs to the session, not to this
         * window.  Closing a window must not throw away an hour of
         * points, exactly as closing the Stability window does not throw
         * away its window of evidence. */
        if (state) state->hSelfPosWnd = NULL;
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

BOOL RegisterSelfPosWindowClass(HINSTANCE hInst)
{
    static BOOL registered = FALSE;
    if (registered) return TRUE;

    WNDCLASSEX wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = SelfPosWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = SELFPOS_WINDOW_CLASS;
    wc.hIcon         = GuiLoadAppIcon(FALSE);
    wc.hIconSm       = GuiLoadAppIcon(TRUE);

    if (!RegisterClassEx(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return FALSE;
    }
    registered = TRUE;
    return TRUE;
}

HWND CreateSelfPosWindow(HINSTANCE hInst, HWND hOwner, AppState *state)
{
    if (!RegisterSelfPosWindowClass(hInst)) return NULL;

    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    int w = SELFPOS_WIN_DEF_W, h = SELFPOS_WIN_DEF_H;
    if (state && state->selfposWndRectValid) {
        x = state->selfposWndRect.left;
        y = state->selfposWndRect.top;
        w = state->selfposWndRect.right  - state->selfposWndRect.left;
        h = state->selfposWndRect.bottom - state->selfposWndRect.top;
        if (w < 480 || h < 320) { w = SELFPOS_WIN_DEF_W;
                                  h = SELFPOS_WIN_DEF_H; }
    }

    HWND hwnd = CreateWindowEx(
        WS_EX_TOOLWINDOW,
        SELFPOS_WINDOW_CLASS, "Self-position",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        x, y, w, h,
        hOwner, NULL, hInst, state);
    if (hwnd) {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
    }
    return hwnd;
}
