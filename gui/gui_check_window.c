/**
 * @file gui_check_window.c
 * @brief Station Check window: the acceptance test, on screen.
 *
 * See gui_check_window.h for what this is and design/gui-design.md §13
 * for why it is shaped this way.
 *
 * Threading: the run is advanced from the session's statistics event,
 * which arrives on the worker thread, and painted on the UI thread.
 * That is the same arrangement `AppState::lastStats` already lives
 * under -- the worker writes a snapshot, the UI reads it -- and the
 * worst case is a repaint that shows one row from the previous second.
 * The alternative, a lock around a struct written once a second and
 * read on a repaint, would buy nothing a user could observe.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "gui_check_window.h"
#include "resource.h"

#include <stdio.h>
#include <string.h>

/* Height of the painted header above the list. */
#define CHECK_HEADER_H  86
/* Height of the button strip below it. */
#define CHECK_FOOTER_H  40

/* Absolute ceiling on one run, matching the CLI's --check. */
#define CHECK_MAX_S     300.0

double CheckNow(void)
{
    /* Monotonic and unaffected by the wall clock being adjusted mid-run,
     * which a ninety-second test is long enough to see. */
    return (double)GetTickCount64() / 1000.0;
}

/* ── Run control ─────────────────────────────────────────────────────── */

void CheckStart(AppState *state)
{
    if (!state) return;

    double now = CheckNow();
    /* The standard this run is judged by, chosen once and carried for
     * its whole life -- a check cannot change standard halfway. */
    kpi_run_start(&state->checkRun, now, &state->thresholds.kpi);
    memset(&state->checkReport, 0, sizeof(state->checkReport));
    state->checkHaveReport = FALSE;
    state->checkSettled    = FALSE;
    state->checkAbandoned  = FALSE;
    state->checkEndWhy[0]  = '\0';
    state->checkActive     = TRUE;
    state->checkStartedAt  = now;
    state->checkElapsedS   = 0.0;

    /* VRS assertions only where they mean something.  Running them
     * against a fixed base would report a network service failing to
     * behave like one it never claimed to be. */
    state->checkVrsActive   = (state->stationType == STATION_VRS);
    state->checkGateStarted = FALSE;
    memset(&state->checkVrsReport, 0, sizeof(state->checkVrsReport));
    if (state->checkVrsActive)
        vrs_run_start(&state->checkVrs, now);
}

/* End a run that will not produce a verdict, and record which way. */
static void CheckEnd(AppState *state, const char *why)
{
    if (!state || !state->checkActive) return;
    if (!state->checkSettled) {
        state->checkAbandoned = TRUE;
        snprintf(state->checkEndWhy, sizeof(state->checkEndWhy), "%s", why);
    }
    state->checkActive    = FALSE;
    state->checkVrsActive = FALSE;
}

void CheckStop(AppState *state)
{
    CheckEnd(state, "stopped");
}

void CheckNoteGga(AppState *state, double lat, double lon)
{
    if (!state || !state->checkActive || !state->checkVrsActive) return;
    vrs_note_gga(&state->checkVrs, &state->lastStats, CheckNow(), lat, lon);
}

void CheckOnStats(AppState *state, const NsStatsSnapshot *s)
{
    if (!state || !s || !state->checkActive) return;

    double now = CheckNow();
    kpi_update(&state->checkRun, s, now, &state->checkReport);
    state->checkHaveReport = TRUE;
    state->checkElapsedS   = now - state->checkStartedAt;

    if (state->checkVrsActive) {
        vrs_update(&state->checkVrs, s, now, &state->checkVrsReport);

        /* The gate test proves a network service is a network service by
         * stopping the keep-alive GGA and waiting for the caster to drop
         * the stream.  It therefore ends the session it is testing --
         * which is why it is opt-in, and why it starts only once the
         * passive assertions have had their answer. */
        if (state->checkGateWanted && !state->checkGateStarted &&
            state->checkVrsReport.a[3].verdict != KPI_PENDING) {
            state->ggaSendEnabled   = FALSE;
            state->checkGateStarted = TRUE;
            vrs_begin_gate_test(&state->checkVrs, now);
            printf("[INFO] Station check: gate test started -- GGA stopped, "
                   "watching for the caster to drop the stream\n");
            fflush(stdout);
        }
    }

    /* The verdict settling ends the run and freezes what is on screen.
     * A number that keeps moving cannot be quoted in a handover, and
     * that -- not the ninety seconds -- is the point of a bounded run. */
    if (state->checkReport.settled) {
        state->checkSettled = TRUE;
        state->checkActive  = FALSE;
    }

    if (state->hCheckWnd)
        PostMessage(state->hCheckWnd, WM_APP_CHECK_UPDATE, 0, 0);
}

/**
 * @brief A number of seconds, bounded to what a header line can show.
 *
 * `%.0f` will happily write three hundred digits, so a buffer sized for
 * plausible values is a buffer sized wrong. Elapsed and sustained times
 * come from a clock and never approach this ceiling; clamping costs
 * nothing real and makes the width of the formatted line provable.
 */
static double DisplaySeconds(double s)
{
    if (!(s > 0.0))      return 0.0;         /* also catches NaN */
    if (s > 999999.0)    return 999999.0;    /* eleven and a half days */
    return s;
}

/* ── Rows ────────────────────────────────────────────────────────────── */

static int SeverityOf(int verdict)
{
    switch (verdict) {
    case KPI_FAIL: return HEALTH_BAD;
    case KPI_WARN: return HEALTH_WARN;
    case KPI_PASS: return HEALTH_OK;
    default:       return HEALTH_INFO;    /* PENDING: evidence still due */
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
    HWND hLv = GetDlgItem(hwnd, IDC_CHECK_LIST);
    if (!hLv || !state) return;

    if (!state->checkHaveReport) {
        ListView_DeleteAllItems(hLv);
        SetRow(hLv, 0, "Not started", "", "", "",
               state->bWorkerRunning
                   ? "Press Run Check to test the open stream"
                   : "Open a stream first: the check watches what it carries",
               HEALTH_INFO);
        return;
    }

    char num[64], lim[64];
    int row = 0;
    for (int i = 0; i < KPI_COUNT; i++) {
        const KpiResult *k = &state->checkReport.kpi[i];
        char name[96];
        snprintf(name, sizeof(name), "%d. %s", i + 1,
                 k->label ? k->label : "");
        /* The number this check was held to. Blank for the structural
         * ones -- whether RTCM decodes at all is not a comparison. */
        kpi_limit_text(k, i, lim, sizeof(lim));
        /* The unit rides with the number: a Value column of 1562, 46.0
         * and 100.000 is three quantities a reader has to already know
         * the meaning of. */
        const char *unit = kpi_value_unit(i);
        snprintf(num, sizeof(num), "%.*f%s%s", kpi_value_decimals(i),
                 k->value, *unit ? " " : "", unit);
        SetRow(hLv, row++, name, kpi_verdict_name(k->verdict), num, lim,
               k->detail ? k->detail : "", SeverityOf(k->verdict));
    }

    /* VRS assertions continue the same numbering visually but are
     * labelled A1..A5, as the CLI reports them. */
    if (state->checkVrsActive || state->checkVrsReport.a[0].label) {
        for (int i = 0; i < VRS_ASSERT_COUNT; i++) {
            const VrsResult *a = &state->checkVrsReport.a[i];
            char name[96];
            snprintf(name, sizeof(name), "A%d. %s", i + 1,
                     a->label ? a->label : "");
            snprintf(num, sizeof(num), "%.2f", a->value);
            /* The assertions have deadlines, not limits, and each says
             * its own in the detail. */
            SetRow(hLv, row++, name, kpi_verdict_name(a->verdict), num, "",
                   a->detail ? a->detail : "", SeverityOf(a->verdict));
        }
    }

    while (ListView_GetItemCount(hLv) > row)
        ListView_DeleteItem(hLv, row);
}

/* ── Header ──────────────────────────────────────────────────────────── */

static void PaintHeader(HDC hdc, RECT *rc, AppState *state)
{
    COLORREF bg = RGB(245, 245, 245), fg = RGB(20, 20, 20);
    const char *verdict = "Not started";

    if (state->checkHaveReport) {
        switch (state->checkReport.overall) {
        case KPI_RUN_OK:
            verdict = "STATION OK";
            bg = RGB(228, 245, 228); fg = RGB(20, 100, 20); break;
        case KPI_RUN_CAUTION:
            verdict = "CAUTION";
            bg = RGB(255, 246, 225); fg = RGB(130, 80, 0);  break;
        case KPI_RUN_FAILED:
            verdict = "FAILED";
            bg = RGB(255, 235, 235); fg = RGB(150, 20, 20); break;
        default:
            verdict = "RUNNING";
            bg = RGB(234, 242, 255); fg = RGB(20, 60, 150); break;
        }
    }

    char banner[64];
    snprintf(banner, sizeof(banner), "%s%s", verdict,
             state->checkAbandoned ? " (stopped)" : "");
    verdict = banner;

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

    /* The second line is the part an installer reads: how long the
     * verdict has held, and whether it is still moving.
     *
     * Sized from the fields it prints, not from a round number. Caster
     * and mountpoint are 256 bytes each in the configuration, so a fixed
     * 256 here dropped the timing -- the part being read -- as soon as
     * the hostname was long. The seconds are clamped below so their
     * width is bounded too: `%.0f` of a double can run to 300 digits,
     * and a buffer has to be right for what the format can produce, not
     * for what a station plausibly reports. */
    char line[sizeof(state->config.NTRIP_CASTER) +
              sizeof(state->config.MOUNTPOINT) + 96];
    if (!state->checkHaveReport) {
        snprintf(line, sizeof(line),
                 "%s / %s",
                 state->config.NTRIP_CASTER, state->config.MOUNTPOINT);
    } else if (state->checkSettled) {
        snprintf(line, sizeof(line),
                 "%s / %s   settled after %.0f s, held %.0f s",
                 state->config.NTRIP_CASTER, state->config.MOUNTPOINT,
                 DisplaySeconds(state->checkElapsedS),
                 DisplaySeconds(state->checkReport.sustained_s));
    } else if (state->checkAbandoned) {
        /* Whatever is on the rows was true when it ended, and is not a
         * verdict.  Saying so is the difference between an unfinished
         * test and a finished one that happened to read this way. */
        snprintf(line, sizeof(line),
                 "%s / %s   %s after %.0f s -- no verdict",
                 state->config.NTRIP_CASTER, state->config.MOUNTPOINT,
                 state->checkEndWhy[0] ? state->checkEndWhy : "ended",
                 DisplaySeconds(state->checkElapsedS));
    } else {
        snprintf(line, sizeof(line),
                 "%s / %s   %.0f s elapsed, verdict held %.0f of %.0f s",
                 state->config.NTRIP_CASTER, state->config.MOUNTPOINT,
                 DisplaySeconds(state->checkElapsedS),
                 DisplaySeconds(state->checkReport.sustained_s),
                 KPI_SUSTAIN_S);
    }

    HFONT small_f = CreateFont(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    old = (HFONT)SelectObject(hdc, small_f);
    SetTextColor(hdc, RGB(60, 60, 60));
    TextOut(hdc, rc->left + 16, rc->top + 46, line, (int)strlen(line));

    if (state->checkVrsActive || state->checkVrsReport.a[0].label) {
        const char *g = vrs_gate_name(state->checkVrsReport.gate);
        char v[128];
        snprintf(v, sizeof(v), "VRS assertions included -- gate: %s",
                 g ? g : "untested");
        TextOut(hdc, rc->left + 16, rc->top + 64, v, (int)strlen(v));
    }
    SelectObject(hdc, old);
    DeleteObject(small_f);
}

/* ── Window ──────────────────────────────────────────────────────────── */

static void LayoutChildren(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;

    HWND hLv = GetDlgItem(hwnd, IDC_CHECK_LIST);
    if (hLv)
        MoveWindow(hLv, 0, CHECK_HEADER_H, w,
                   h - CHECK_HEADER_H - CHECK_FOOTER_H, TRUE);

    HWND hRun  = GetDlgItem(hwnd, IDC_CHECK_BTN_RUN);
    HWND hGate = GetDlgItem(hwnd, IDC_CHECK_CHK_GATE);
    if (hRun)  MoveWindow(hRun,  8, h - CHECK_FOOTER_H + 6, 110, 26, TRUE);
    if (hGate) MoveWindow(hGate, 130, h - CHECK_FOOTER_H + 9, w - 138, 22, TRUE);
}

static void SyncControls(HWND hwnd, AppState *state)
{
    HWND hRun  = GetDlgItem(hwnd, IDC_CHECK_BTN_RUN);
    HWND hGate = GetDlgItem(hwnd, IDC_CHECK_CHK_GATE);
    if (!hRun || !state) return;

    BOOL streaming = state->bWorkerRunning;
    SetWindowText(hRun, state->checkActive ? "Stop" : "Run Check");
    /* A check with no stream to watch would report nothing but its own
     * absence of evidence. */
    EnableWindow(hRun, streaming || state->checkActive);
    if (hGate) EnableWindow(hGate, !state->checkActive);
}

static LRESULT CALLBACK CheckWndProc(HWND hwnd, UINT msg,
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
            0, 0, 10, 10, hwnd, (HMENU)IDC_CHECK_LIST, hInst, NULL);
        ListView_SetExtendedListViewStyle(hLv,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

        struct { const char *t; int w; } cols[] = {
            { "Check",   230 }, { "Verdict", 70 },
            { "Value",   100 },                       /* "100.000 %"     */
            { "Limit",   110 },                       /* "min 99.900 %"  */
            { "Detail",  420 },
        };
        for (int i = 0; i < 5; i++) {
            LVCOLUMN c;
            ZeroMemory(&c, sizeof(c));
            c.mask     = LVCF_TEXT | LVCF_WIDTH;
            c.pszText  = (char *)cols[i].t;
            c.cx       = cols[i].w;
            ListView_InsertColumn(hLv, i, &c);
        }

        CreateWindowEx(0, "BUTTON", "Run Check",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 10, 10, hwnd, (HMENU)IDC_CHECK_BTN_RUN, hInst, NULL);

        CreateWindowEx(0, "BUTTON",
            "Include the VRS gate test (stops GGA; a real VRS will drop the stream)",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            0, 0, 10, 10, hwnd, (HMENU)IDC_CHECK_CHK_GATE, hInst, NULL);

        /* Inherit the shell font rather than the ancient system one. */
        HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        SendMessage(GetDlgItem(hwnd, IDC_CHECK_BTN_RUN),  WM_SETFONT, (WPARAM)f, TRUE);
        SendMessage(GetDlgItem(hwnd, IDC_CHECK_CHK_GATE), WM_SETFONT, (WPARAM)f, TRUE);
        SendMessage(hLv, WM_SETFONT, (WPARAM)f, TRUE);

        if (state && state->checkGateWanted)
            SendMessage(GetDlgItem(hwnd, IDC_CHECK_CHK_GATE), BM_SETCHECK,
                        BST_CHECKED, 0);

        LayoutChildren(hwnd);
        RefreshRows(hwnd, state);
        SyncControls(hwnd, state);
        SetTimer(hwnd, 1, 1000, NULL);
        return 0;
    }

    case WM_SIZE:
        LayoutChildren(hwnd);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_CHECK_BTN_RUN:
            if (state) {
                if (state->checkActive) CheckStop(state);
                else                    CheckStart(state);
                RefreshRows(hwnd, state);
                SyncControls(hwnd, state);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case IDC_CHECK_CHK_GATE:
            if (state) {
                state->checkGateWanted =
                    (SendMessage(GetDlgItem(hwnd, IDC_CHECK_CHK_GATE),
                                 BM_GETCHECK, 0, 0) == BST_CHECKED);
            }
            return 0;
        }
        break;

    case WM_APP_CHECK_UPDATE:
        RefreshRows(hwnd, state);
        SyncControls(hwnd, state);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_TIMER:
        /* Only the header moves between snapshots: elapsed seconds tick
         * while the rows wait for the next statistics event. */
        if (state && state->checkActive) {
            /* A stream that stops sends no more statistics, so a run
             * left "active" would count seconds up beside rows that had
             * quietly stopped changing.  It is over; say so. */
            if (!state->bWorkerRunning) {
                CheckEnd(state, "the stream closed");
            } else {
                state->checkElapsedS = CheckNow() - state->checkStartedAt;
                /* The same ceiling the CLI applies.  Without it a run
                 * can never end: a mountpoint absent from the caster's
                 * sourcetable leaves KPI 8 pending for ever -- rightly,
                 * since "we could not check" is not a pass -- and a
                 * pending KPI keeps the roll-up at RUNNING. */
                if (state->checkElapsedS > CHECK_MAX_S)
                    CheckEnd(state, "no verdict inside the time limit");
            }
        }
        SyncControls(hwnd, state);
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            rc.bottom = CHECK_HEADER_H;
            InvalidateRect(hwnd, &rc, FALSE);
        }
        return 0;

    case WM_NOTIFY: {
        NMHDR *nmh = (NMHDR *)lParam;
        if (nmh->idFrom == IDC_CHECK_LIST && nmh->code == NM_CUSTOMDRAW) {
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
                if (ListView_GetItem(GetDlgItem(hwnd, IDC_CHECK_LIST), &lvi))
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
        hdr.bottom = CHECK_HEADER_H;
        if (state) PaintHeader(hdc, &hdr, state);

        RECT foot = rc;
        foot.top = rc.bottom - CHECK_FOOTER_H;
        HBRUSH br = GetSysColorBrush(COLOR_BTNFACE);
        FillRect(hdc, &foot, br);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CLOSE:
        if (state) {
            GetWindowRect(hwnd, &state->checkWndRect);
            state->checkWndRectValid = TRUE;
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        /* The run itself keeps going: it belongs to the session, not to
         * this window.  Only the handle is cleared. */
        if (state) state->hCheckWnd = NULL;
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

BOOL RegisterCheckWindowClass(HINSTANCE hInst)
{
    static BOOL registered = FALSE;
    if (registered) return TRUE;

    WNDCLASSEX wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = CheckWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = CHECK_WINDOW_CLASS;
    wc.hIcon         = GuiLoadAppIcon(FALSE);
    wc.hIconSm       = GuiLoadAppIcon(TRUE);

    if (!RegisterClassEx(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return FALSE;
    }
    registered = TRUE;
    return TRUE;
}

HWND CreateCheckWindow(HINSTANCE hInst, HWND hOwner, AppState *state)
{
    if (!RegisterCheckWindowClass(hInst)) return NULL;

    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    int w = CHECK_WIN_DEF_W, h = CHECK_WIN_DEF_H;
    if (state && state->checkWndRectValid) {
        x = state->checkWndRect.left;
        y = state->checkWndRect.top;
        w = state->checkWndRect.right  - state->checkWndRect.left;
        h = state->checkWndRect.bottom - state->checkWndRect.top;
        if (w < 420 || h < 300) { w = CHECK_WIN_DEF_W; h = CHECK_WIN_DEF_H; }
    }

    HWND hwnd = CreateWindowEx(
        WS_EX_TOOLWINDOW,
        CHECK_WINDOW_CLASS, "Station Check",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        x, y, w, h,
        hOwner, NULL, hInst, state);
    if (hwnd) {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
    }
    return hwnd;
}
