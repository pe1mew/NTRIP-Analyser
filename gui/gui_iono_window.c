/**
 * @file gui_iono_window.c
 * @brief Floating Ionosphere window -- implementation.
 *
 * A header line with the roll-up verdict, then a ListView with one row
 * per dual-frequency satellite.  All data arrives through
 * `AppState.ionoView` / `AppState.lastStats`, which the worker refreshes;
 * this window only ever reads, on its own 1 s timer, so it needs no
 * messages from anyone.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * @copyright Apache License 2.0 with Commons Clause (see LICENSE for details)
 */

#include "gui_iono_window.h"
#include <commctrl.h>
#include <stdio.h>

#define IONO_WND_CLASS  "NtripIonoWindow"
#define IDT_IONO_TICK   1
#define IDC_IONO_LIST   100

static const char *gnss_short(int id)
{
    switch (id) {
    case 1: return "GPS";  case 2: return "GLO";  case 3: return "GAL";
    case 4: return "QZS";  case 5: return "BDS";  case 6: return "SBA";
    case 7: return "IRN";  default: return "?";
    }
}

/** @brief Rebuild the ListView and the title's verdict line. */
static void iono_refresh(HWND hwnd, AppState *state)
{
    HWND hLv = GetDlgItem(hwnd, IDC_IONO_LIST);
    if (!hLv) return;

    char title[192];
    if (state->haveStats && state->lastStats.iono_roti_median >= 0.0f) {
        snprintf(title, sizeof(title),
                 "Ionosphere - %s  (median ROTI %.2f TECU/min, quiet < %.1f, "
                 "disturbed > %.1f)",
                 iono_verdict_name(state->lastStats.iono_verdict),
                 state->lastStats.iono_roti_median,
                 IONO_ROTI_UNSETTLED, IONO_ROTI_DISTURBED);
    } else {
        snprintf(title, sizeof(title),
                 "Ionosphere - waiting for dual-frequency MSM7 arcs");
    }
    SetWindowText(hwnd, title);

    /* Rewrite in place.  Rows are already in constellation/PRN order
     * from iono_sat_view(), so the table does not jump about. */
    int have = ListView_GetItemCount(hLv);
    int want = state->nIonoView;
    char buf[64];

    for (int i = 0; i < want; i++) {
        const IonoSatView *v = &state->ionoView[i];
        snprintf(buf, sizeof(buf), "%s %d", gnss_short(v->gnss_id), v->prn);
        if (i >= have) {
            LVITEM lvi;
            ZeroMemory(&lvi, sizeof(lvi));
            lvi.mask = LVIF_TEXT; lvi.iItem = i; lvi.pszText = buf;
            ListView_InsertItem(hLv, &lvi);
        } else {
            ListView_SetItemText(hLv, i, 0, buf);
        }
        snprintf(buf, sizeof(buf), "%s / %s", v->sig_a, v->sig_b);
        ListView_SetItemText(hLv, i, 1, buf);
        if (v->roti >= 0.0f) snprintf(buf, sizeof(buf), "%.3f", v->roti);
        else                 snprintf(buf, sizeof(buf), "-");
        ListView_SetItemText(hLv, i, 2, buf);
        snprintf(buf, sizeof(buf), "%+.2f", v->stec_rel);
        ListView_SetItemText(hLv, i, 3, buf);
        snprintf(buf, sizeof(buf), "%.0f", v->arc_len_s);
        ListView_SetItemText(hLv, i, 4, buf);
        snprintf(buf, sizeof(buf), "%d", v->slips);
        ListView_SetItemText(hLv, i, 5, buf);
    }
    while (have > want)
        ListView_DeleteItem(hLv, --have);
}

static LRESULT CALLBACK IonoWndProc(HWND hwnd, UINT msg,
                                    WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);

        HWND hLv = CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTVIEW, "",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
            0, 0, 100, 100, hwnd, (HMENU)(intptr_t)IDC_IONO_LIST,
            ((CREATESTRUCT *)lParam)->hInstance, NULL);
        ListView_SetExtendedListViewStyle(hLv,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        SendMessage(hLv, WM_SETFONT,
                    (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

        struct { const char *name; int w; } cols[] = {
            { "SV",           70 },
            { "Signal pair", 100 },
            { "ROTI",         70 },   /* TECU/min */
            { "STEC rel",     80 },   /* TECU since arc start */
            { "Arc (s)",      60 },
            { "Slips",        50 },
        };
        for (int i = 0; i < 6; i++) {
            LVCOLUMN col;
            ZeroMemory(&col, sizeof(col));
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            col.pszText  = (LPSTR)cols[i].name;
            col.cx       = cols[i].w;
            col.iSubItem = i;
            ListView_InsertColumn(hLv, i, &col);
        }

        SetTimer(hwnd, IDT_IONO_TICK, 1000, NULL);
        return 0;
    }

    case WM_TIMER: {
        AppState *state = (AppState *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (state) iono_refresh(hwnd, state);
        return 0;
    }

    case WM_SIZE: {
        HWND hLv = GetDlgItem(hwnd, IDC_IONO_LIST);
        if (hLv)
            MoveWindow(hLv, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY: {
        KillTimer(hwnd, IDT_IONO_TICK);
        AppState *state = (AppState *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (state && state->hIonoWnd == hwnd) state->hIonoWnd = NULL;
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

HWND IonoWindowOpen(HINSTANCE hInst, HWND parent, AppState *state)
{
    if (state->hIonoWnd) {
        SetForegroundWindow(state->hIonoWnd);
        return state->hIonoWnd;
    }

    static BOOL registered = FALSE;
    if (!registered) {
        WNDCLASSEX wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = IonoWndProc;
        wc.hInstance     = hInst;
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = IONO_WND_CLASS;
        wc.hIcon         = GuiLoadAppIcon(FALSE);
        wc.hIconSm       = GuiLoadAppIcon(TRUE);
        if (!RegisterClassEx(&wc)) return NULL;
        registered = TRUE;
    }

    state->hIonoWnd = CreateWindowEx(WS_EX_TOOLWINDOW, IONO_WND_CLASS,
        "Ionosphere", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 470, 420,
        parent, NULL, hInst, state);
    return state->hIonoWnd;
}
