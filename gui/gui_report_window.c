/**
 * @file gui_report_window.c
 * @brief Stability window: tier 2, on screen.
 *
 * See gui_report_window.h for what this is and why it is shaped this
 * way, and design/work-items/measurement-tiers.md for the tier itself.
 *
 * Threading: the accumulator advances from the session's statistics
 * event, which arrives on the worker thread, and is painted on the UI
 * thread -- the same arrangement `AppState::lastStats` and the station
 * check already live under. The worst case is a repaint showing one row
 * from the previous sample.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "gui_report_window.h"
#include "resource.h"

#include <stdio.h>
#include <string.h>

/* Height of the painted header above the list. */
#define REPORT_HEADER_H  86
/* Height of the button strip below it. */
#define REPORT_FOOTER_H  40

/* Seconds of *stream* between samples -- the CLI's cadence and the
 * daemon's, so the same session produces the same report whichever
 * program is watching it. */
#define REPORT_SAMPLE_S  1.0

/* ── Accumulation ────────────────────────────────────────────────────── */

void ReportReset(AppState *state, BOOL from_capture)
{
    if (!state) return;
    sr_reset(&state->reportRun, from_capture ? true : false);
    memset(&state->reportOut, 0, sizeof(state->reportOut));
    state->reportHave       = FALSE;
    state->reportFromCapture = from_capture;
    state->reportLastSample = -1e9;

    if (state->hReportWnd)
        PostMessage(state->hReportWnd, WM_APP_REPORT_UPDATE, 0, 0);
}

void ReportOnStats(AppState *state, const NsStatsSnapshot *s)
{
    if (!state || !s) return;

    /* No epochs, no clock, no window.  A stream of station and antenna
     * messages carries nothing to measure a window with, and guessing
     * with the host's clock is what makes a replay disagree with the
     * live run that recorded it. */
    double t = s->stream_time_s;
    if (t < 0.0) return;
    if (t - state->reportLastSample < REPORT_SAMPLE_S) return;
    state->reportLastSample = t;

    sr_feed(&state->reportRun, s, t);
    sr_build(&state->reportRun, &state->reportOut);
    state->reportHave = TRUE;

    if (state->hReportWnd)
        PostMessage(state->hReportWnd, WM_APP_REPORT_UPDATE, 0, 0);
}

/* ── Rows ────────────────────────────────────────────────────────────── */

static int SeverityOf(int verdict)
{
    switch (verdict) {
    case SR_UNSTABLE: return HEALTH_BAD;
    case SR_DEGRADED: return HEALTH_WARN;
    case SR_STABLE:   return HEALTH_OK;
    default:          return HEALTH_INFO;   /* evidence still due */
    }
}

static void SetRow(HWND hLv, int row, const char *name, const char *verdict,
                   const char *value, const char *limit, const char *detail,
                   int severity)
{
    if (ListView_GetItemCount(hLv) <= row) {
        LVITEM lvi;
        ZeroMemory(&lvi, sizeof(lvi));
        lvi.mask    = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem   = row;
        lvi.pszText = (char *)name;
        lvi.lParam  = severity;
        ListView_InsertItem(hLv, &lvi);
    } else {
        ListView_SetItemText(hLv, row, 0, (char *)name);
        LVITEM lvi;
        ZeroMemory(&lvi, sizeof(lvi));
        lvi.mask   = LVIF_PARAM;
        lvi.iItem  = row;
        lvi.lParam = severity;
        ListView_SetItem(hLv, &lvi);
    }
    ListView_SetItemText(hLv, row, 1, (char *)verdict);
    ListView_SetItemText(hLv, row, 2, (char *)value);
    ListView_SetItemText(hLv, row, 3, (char *)limit);
    ListView_SetItemText(hLv, row, 4, (char *)detail);
}

static void RefreshRows(HWND hwnd, AppState *state)
{
    HWND hLv = GetDlgItem(hwnd, IDC_REPORT_LIST);
    if (!hLv || !state) return;

    if (!state->reportHave) {
        ListView_DeleteAllItems(hLv);
        SetRow(hLv, 0, "Nothing measured yet", "", "", "",
               state->bWorkerRunning
                   ? "Gathering: the first verdict needs ten minutes of stream"
                   : "Open a stream or replay a capture; this watches what it carries",
               HEALTH_INFO);
        return;
    }

    int row = 0;
    for (int i = 0; i < SR_METRIC_COUNT; i++) {
        const SrMetric *m = &state->reportOut.metric[i];
        char name[96], num[64], lim[64];
        snprintf(name, sizeof(name), "%d. %s", i + 1,
                 m->label ? m->label : "");

        /* The limit shows on every row, including one that cannot be
         * measured here: what a capture *would* have been held to is
         * still worth a reader knowing. */
        sr_metric_limit_text(m, i, lim, sizeof(lim));

        /* An unavailable metric shows "n/a" and no number.  A live-only
         * figure absent from a replay is not a figure measured as zero,
         * and a clean 0.000 in that column would be an invention. */
        /* detail is an array, not a pointer: always valid, never NULL. */
        if (!m->available) {
            SetRow(hLv, row++, name, "n/a", "--", lim, m->detail,
                   HEALTH_INFO);
            continue;
        }
        const char *unit = sr_metric_unit(i);
        snprintf(num, sizeof(num), "%.*f%s%s", sr_metric_decimals(i),
                 m->value, *unit ? " " : "", unit);
        SetRow(hLv, row++, name, sr_verdict_name(m->verdict), num, lim,
               m->detail, SeverityOf(m->verdict));
    }

    while (ListView_GetItemCount(hLv) > row)
        ListView_DeleteItem(hLv, row);
}

/* ── Header ──────────────────────────────────────────────────────────── */

static void PaintHeader(HDC hdc, RECT *rc, AppState *state)
{
    COLORREF bg = RGB(245, 245, 245), fg = RGB(20, 20, 20);
    const char *verdict = "No evidence yet";

    if (state->reportHave) {
        switch (state->reportOut.overall) {
        case SR_STABLE:
            verdict = "STABLE";
            bg = RGB(228, 245, 228); fg = RGB(20, 100, 20); break;
        case SR_DEGRADED:
            verdict = "DEGRADED";
            bg = RGB(255, 246, 225); fg = RGB(130, 80, 0);  break;
        case SR_UNSTABLE:
            verdict = "UNSTABLE";
            bg = RGB(255, 235, 235); fg = RGB(150, 20, 20); break;
        default:
            /* Not a failure: the window is too short to judge, which is
             * true of the first ten minutes of every session. */
            verdict = "INSUFFICIENT EVIDENCE";
            bg = RGB(234, 242, 255); fg = RGB(20, 60, 150); break;
        }
    }

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
    TextOut(hdc, rc->left + 14, rc->top + 8, verdict, (int)strlen(verdict));
    SelectObject(hdc, old);
    DeleteObject(big);

    /* The second line carries the evidence: how much stream this verdict
     * rests on. A verdict without its window is a rumour, which is the
     * rule the whole tier is built on.
     *
     * What it must *not* do is repeat the banner. The first version
     * printed the verdict here and again below it, so a window three
     * lines tall said "INSUFFICIENT EVIDENCE" twice and the sample count
     * twice, and the one genuinely new number -- how much more stream it
     * wants -- was buried among the repetitions. */
    char line[sizeof(state->config.NTRIP_CASTER) +
              sizeof(state->config.MOUNTPOINT) + 160];
    if (!state->reportHave) {
        snprintf(line, sizeof(line), "%s / %s",
                 state->config.NTRIP_CASTER, state->config.MOUNTPOINT);
    } else {
        char need[48] = "";
        if (state->reportOut.overall == SR_INSUFFICIENT)
            snprintf(need, sizeof(need), "   (%.0f s needed to judge)",
                     SR_MIN_WINDOW_S);
        snprintf(line, sizeof(line),
                 "%s / %s   %.0f s of stream, %d samples%s%s",
                 state->config.NTRIP_CASTER, state->config.MOUNTPOINT,
                 state->reportOut.window_s, state->reportOut.samples, need,
                 state->reportFromCapture ? "   (from a capture)" : "");
    }

    HFONT small_f = CreateFont(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    old = (HFONT)SelectObject(hdc, small_f);
    SetTextColor(hdc, RGB(60, 60, 60));
    TextOut(hdc, rc->left + 16, rc->top + 46, line, (int)strlen(line));

    /* Third line: the culprit, and only the culprit.  The headline is
     * written for a terminal, where it stands alone and has to carry the
     * verdict with it; here the banner above has already said that, so
     * only the clause after the dash -- "Frame integrity: worst CRC
     * error rate 0.140 %" -- is worth the room.  A clean STABLE names no
     * culprit and gets no third line, which is the right amount to say
     * about a station with nothing wrong with it. */
    if (state->reportHave && state->reportOut.overall != SR_INSUFFICIENT) {
        const char *culprit = strstr(state->reportOut.headline, " -- ");
        if (culprit) {
            culprit += 4;
            TextOut(hdc, rc->left + 16, rc->top + 64,
                    culprit, (int)strlen(culprit));
        }
    }
    SelectObject(hdc, old);
    DeleteObject(small_f);
}

/* ── Window ──────────────────────────────────────────────────────────── */

/**
 * @brief Width the verdict column needs, in this font, at this DPI.
 *
 * Measured, not guessed.  Two guesses had already clipped the longest
 * value -- and "INSUFFICIENT EVIDENCE" is not a rare one: it is what
 * every session shows for its first ten minutes, so it is the worst of
 * the four to truncate.  A number that fits the developer's font at the
 * developer's scaling is not a number that fits.
 */
static int VerdictColumnWidth(HWND hLv)
{
    int widest = 0;
    HDC hdc = GetDC(hLv);
    if (!hdc) return 190;

    HFONT f = (HFONT)SendMessage(hLv, WM_GETFONT, 0, 0);
    HFONT old = f ? (HFONT)SelectObject(hdc, f) : NULL;

    for (int v = SR_INSUFFICIENT; v <= SR_UNSTABLE; v++) {
        const char *s = sr_verdict_name(v);
        SIZE sz;
        if (GetTextExtentPoint32(hdc, s, (int)strlen(s), &sz) &&
            sz.cx > widest)
            widest = sz.cx;
    }
    if (old) SelectObject(hdc, old);
    ReleaseDC(hLv, hdc);

    /* The list draws a margin either side of the text, and a column
     * exactly as wide as its content still shows an ellipsis. */
    return widest + 24;
}

static void LayoutChildren(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;

    HWND hLv = GetDlgItem(hwnd, IDC_REPORT_LIST);
    if (hLv) {
        MoveWindow(hLv, 0, REPORT_HEADER_H, w,
                   h - REPORT_HEADER_H - REPORT_FOOTER_H, TRUE);

        /* Detail takes whatever is left, so widening the window widens
         * the column that actually varies rather than leaving a strip of
         * grey beside four fixed ones. */
        int fixed = ListView_GetColumnWidth(hLv, 0)
                  + ListView_GetColumnWidth(hLv, 1)
                  + ListView_GetColumnWidth(hLv, 2)
                  + ListView_GetColumnWidth(hLv, 3);
        int detail = w - fixed - GetSystemMetrics(SM_CXVSCROLL) - 4;
        if (detail < 160) detail = 160;
        ListView_SetColumnWidth(hLv, 4, detail);
    }

    HWND hRst = GetDlgItem(hwnd, IDC_REPORT_BTN_RESET);
    if (hRst) MoveWindow(hRst, 8, h - REPORT_FOOTER_H + 6, 140, 26, TRUE);
}

static LRESULT CALLBACK ReportWndProc(HWND hwnd, UINT msg,
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
            0, 0, 10, 10, hwnd, (HMENU)IDC_REPORT_LIST, hInst, NULL);
        ListView_SetExtendedListViewStyle(hLv,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

        /* The font first, and then the columns: the verdict's width is
         * measured in whatever font the list will actually draw with,
         * and measuring before this would measure the stock system font
         * the control is born with. */
        HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        SendMessage(hLv, WM_SETFONT, (WPARAM)f, TRUE);

        /* The verdict's width is measured from the strings it will hold;
         * Detail is resized to fill in LayoutChildren(). */
        struct { const char *t; int w; } cols[] = {
            { "Stability", 150 }, { "Verdict", VerdictColumnWidth(hLv) },
            { "Value",     110 },  /* 0.06 TECU/min */
            { "Limit",     120 },  /* max 0.50 TECU/min */
            { "Detail",    290 },
        };
        for (int i = 0; i < 5; i++) {
            LVCOLUMN c;
            ZeroMemory(&c, sizeof(c));
            c.mask    = LVCF_TEXT | LVCF_WIDTH;
            c.pszText = (char *)cols[i].t;
            c.cx      = cols[i].w;
            ListView_InsertColumn(hLv, i, &c);
        }

        CreateWindowEx(0, "BUTTON", "Restart window",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 10, 10, hwnd, (HMENU)IDC_REPORT_BTN_RESET, hInst, NULL);

        SendMessage(GetDlgItem(hwnd, IDC_REPORT_BTN_RESET), WM_SETFONT,
                    (WPARAM)f, TRUE);

        LayoutChildren(hwnd);
        RefreshRows(hwnd, state);
        return 0;
    }

    case WM_SIZE:
        LayoutChildren(hwnd);
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_REPORT_BTN_RESET) {
            if (state) {
                /* Keep whichever source the session has: restarting the
                 * window must not turn a replay into a live run and
                 * start inventing availability figures for it. */
                ReportReset(state, state->reportFromCapture);
                RefreshRows(hwnd, state);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        break;

    case WM_APP_REPORT_UPDATE:
        RefreshRows(hwnd, state);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_NOTIFY: {
        NMHDR *nmh = (NMHDR *)lParam;
        if (nmh->idFrom == IDC_REPORT_LIST && nmh->code == NM_CUSTOMDRAW) {
            NMLVCUSTOMDRAW *cd = (NMLVCUSTOMDRAW *)lParam;
            switch (cd->nmcd.dwDrawStage) {
            case CDDS_PREPAINT:
                return CDRF_NOTIFYITEMDRAW;
            case CDDS_ITEMPREPAINT: {
                LVITEM lvi;
                ZeroMemory(&lvi, sizeof(lvi));
                lvi.mask  = LVIF_PARAM;
                lvi.iItem = (int)cd->nmcd.dwItemSpec;
                LPARAM sev = HEALTH_OK;
                if (ListView_GetItem(GetDlgItem(hwnd, IDC_REPORT_LIST), &lvi))
                    sev = lvi.lParam;
                switch (sev) {
                case HEALTH_BAD:
                    cd->clrText   = RGB(150,  20,  20);
                    cd->clrTextBk = RGB(255, 235, 235);
                    break;
                case HEALTH_WARN:
                    cd->clrText   = RGB(130,  80,   0);
                    cd->clrTextBk = RGB(255, 246, 225);
                    break;
                case HEALTH_INFO:
                    cd->clrText   = RGB( 20,  60, 150);
                    cd->clrTextBk = RGB(234, 242, 255);
                    break;
                default:
                    break;
                }
                return CDRF_NEWFONT;
            }
            default:
                return CDRF_DODEFAULT;
            }
        }
        break;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        RECT hdr = rc;
        hdr.bottom = REPORT_HEADER_H;
        if (state) PaintHeader(hdc, &hdr, state);

        RECT foot = rc;
        foot.top = rc.bottom - REPORT_FOOTER_H;
        FillRect(hdc, &foot, GetSysColorBrush(COLOR_BTNFACE));

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CLOSE:
        if (state) {
            GetWindowRect(hwnd, &state->reportWndRect);
            state->reportWndRectValid = TRUE;
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        /* The accumulator keeps going: it belongs to the session, not to
         * this window.  Closing a window must not throw away an hour of
         * evidence. */
        if (state) state->hReportWnd = NULL;
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

BOOL RegisterReportWindowClass(HINSTANCE hInst)
{
    static BOOL registered = FALSE;
    if (registered) return TRUE;

    WNDCLASSEX wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = ReportWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = REPORT_WINDOW_CLASS;
    wc.hIcon         = GuiLoadAppIcon(FALSE);
    wc.hIconSm       = GuiLoadAppIcon(TRUE);

    if (!RegisterClassEx(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return FALSE;
    }
    registered = TRUE;
    return TRUE;
}

HWND CreateReportWindow(HINSTANCE hInst, HWND hOwner, AppState *state)
{
    if (!RegisterReportWindowClass(hInst)) return NULL;

    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    int w = REPORT_WIN_DEF_W, h = REPORT_WIN_DEF_H;
    if (state && state->reportWndRectValid) {
        x = state->reportWndRect.left;
        y = state->reportWndRect.top;
        w = state->reportWndRect.right  - state->reportWndRect.left;
        h = state->reportWndRect.bottom - state->reportWndRect.top;
        if (w < 420 || h < 260) { w = REPORT_WIN_DEF_W; h = REPORT_WIN_DEF_H; }
    }

    HWND hwnd = CreateWindowEx(
        WS_EX_TOOLWINDOW,
        REPORT_WINDOW_CLASS, "Stability",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        x, y, w, h,
        hOwner, NULL, hInst, state);
    if (hwnd) {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
    }
    return hwnd;
}
