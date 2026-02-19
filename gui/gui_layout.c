/**
 * @file gui_layout.c
 * @brief Control creation and layout management for NTRIP-Analyser GUI.
 *
 * Creates all child controls (edits, buttons, list views, tab, status bar)
 * and handles resizing via WM_SIZE.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "resource.h"
#include "gui_state.h"

/**
 * @brief Measure the pixel width of a string using the given font.
 * Returns the width in pixels, plus a small padding.
 */
static int MeasureTextWidth(HWND hwnd, HFONT hFont, const char *text, int padding)
{
    HDC hdc = GetDC(hwnd);
    HFONT hOld = (HFONT)SelectObject(hdc, hFont);
    SIZE sz;
    GetTextExtentPoint32(hdc, text, (int)strlen(text), &sz);
    SelectObject(hdc, hOld);
    ReleaseDC(hwnd, hdc);
    return sz.cx + padding;
}

/**
 * @brief EnumChildWindows callback: apply DEFAULT_GUI_FONT to each child.
 */
static BOOL CALLBACK SetChildFont(HWND child, LPARAM lParam)
{
    SendMessage(child, WM_SETFONT, (WPARAM)lParam, TRUE);
    return TRUE;
}

/* Helper: create a STATIC label */
static HWND CreateLabel(HWND parent, int id, const char *text,
                        int x, int y, int w, int h)
{
    return CreateWindowEx(0, "STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, h, parent, (HMENU)(intptr_t)id,
        (HINSTANCE)GetWindowLongPtr(parent, GWLP_HINSTANCE), NULL);
}

/* Helper: create an EDIT control */
static HWND CreateEdit(HWND parent, int id, const char *text,
                       int x, int y, int w, int h, DWORD extraStyle)
{
    return CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | extraStyle,
        x, y, w, h, parent, (HMENU)(intptr_t)id,
        (HINSTANCE)GetWindowLongPtr(parent, GWLP_HINSTANCE), NULL);
}

/* Helper: create a BUTTON */
static HWND CreateBtn(HWND parent, int id, const char *text,
                      int x, int y, int w, int h)
{
    return CreateWindowEx(0, "BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x, y, w, h, parent, (HMENU)(intptr_t)id,
        (HINSTANCE)GetWindowLongPtr(parent, GWLP_HINSTANCE), NULL);
}

/* Helper: add a column to a ListView */
static void LvAddColumn(HWND hLv, int index, const char *text, int width)
{
    LVCOLUMN col;
    ZeroMemory(&col, sizeof(col));
    col.mask    = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    col.cx      = width;
    col.pszText = (LPSTR)text;
    col.iSubItem = index;
    ListView_InsertColumn(hLv, index, &col);
}

/**
 * @brief Create all child controls and store handles in AppState.
 * Called once from WM_CREATE.
 */
void CreateControls(HWND hwnd, AppState *state)
{
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
    HFONT hGuiFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    int m  = GUI_MARGIN;
    int eh = GUI_EDIT_H;
    int bh = GUI_BTN_H;
    int bw = GUI_BTN_W;

    /* Default mountpoint ListView height (user can drag splitter to change) */
    state->splitterLvH = 140;

    /* ── Row 1: Connection settings ─────────────────────────── */
    int y = m;
    int x = m;

    /* Spacing constants for the connection settings grid */
    int sp   = 8;   /* gap between a label-edit pair and the next pair */
    int lg   = 4;   /* gap between label and its edit control */
    int inX  = m + 12;  /* left indent inside group box */

    /* Group box (purely decorative at this point; resized later) */
    state->hGroupConnection = CreateWindowEx(0, "BUTTON", "Connection Settings",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        m, y, 700, 110, hwnd, (HMENU)(intptr_t)IDC_GROUP_CONNECTION, hInst, NULL);

    y += 20;  /* inside group box */
    x = inX;

    /* Measure label widths at runtime so they adapt to DPI / font size.
     * Padding of 4 px ensures the text never clips against the edit. */
    int wCaster     = MeasureTextWidth(hwnd, hGuiFont, "Caster:",     4);
    int wPort       = MeasureTextWidth(hwnd, hGuiFont, "Port:",       4);
    int wMountpoint = MeasureTextWidth(hwnd, hGuiFont, "Mountpoint:", 4);
    int wUser       = MeasureTextWidth(hwnd, hGuiFont, "User:",       4);
    int wPassword   = MeasureTextWidth(hwnd, hGuiFont, "Password:",   4);
    int wLat        = MeasureTextWidth(hwnd, hGuiFont, "Lat:",        4);
    int wLon        = MeasureTextWidth(hwnd, hGuiFont, "Lon:",        4);

    /* Row A: Caster / Port / Mountpoint */
    state->hLblCaster = CreateLabel(hwnd, IDC_LBL_CASTER, "Caster:", x, y + 2, wCaster, eh);
    x += wCaster + lg;
    state->hEditCaster = CreateEdit(hwnd, IDC_EDIT_CASTER, "", x, y, 180, eh, 0);
    x += 180 + sp;

    state->hLblPort = CreateLabel(hwnd, IDC_LBL_PORT, "Port:", x, y + 2, wPort, eh);
    x += wPort + lg;
    state->hEditPort = CreateEdit(hwnd, IDC_EDIT_PORT, "2101", x, y, 55, eh, ES_NUMBER);
    x += 55 + sp;

    state->hLblMountpoint = CreateLabel(hwnd, IDC_LBL_MOUNTPOINT, "Mountpoint:", x, y + 2, wMountpoint, eh);
    x += wMountpoint + lg;
    state->hEditMountpoint = CreateEdit(hwnd, IDC_EDIT_MOUNTPOINT, "", x, y, 160, eh, 0);

    /* Row B: User / Password / Lat / Lon */
    y += eh + 6;
    x = inX;

    state->hLblUsername = CreateLabel(hwnd, IDC_LBL_USERNAME, "User:", x, y + 2, wUser, eh);
    x += wUser + lg;
    state->hEditUsername = CreateEdit(hwnd, IDC_EDIT_USERNAME, "", x, y, 140, eh, 0);
    x += 140 + sp;

    state->hLblPassword = CreateLabel(hwnd, IDC_LBL_PASSWORD, "Password:", x, y + 2, wPassword, eh);
    x += wPassword + lg;
    state->hEditPassword = CreateEdit(hwnd, IDC_EDIT_PASSWORD, "", x, y, 120, eh, ES_PASSWORD);
    x += 120 + sp;

    state->hLblLatitude = CreateLabel(hwnd, IDC_LBL_LATITUDE, "Lat:", x, y + 2, wLat, eh);
    x += wLat + lg;
    state->hEditLatitude = CreateEdit(hwnd, IDC_EDIT_LATITUDE, "0.0", x, y, 75, eh, 0);
    x += 75 + sp;

    state->hLblLongitude = CreateLabel(hwnd, IDC_LBL_LONGITUDE, "Lon:", x, y + 2, wLon, eh);
    x += wLon + lg;
    state->hEditLongitude = CreateEdit(hwnd, IDC_EDIT_LONGITUDE, "0.0", x, y, 75, eh, 0);
    x += 75 + sp;

    /* Map picker: "Map" opens browser-based map, "<<" pastes coords from clipboard */
    state->hBtnMapPick  = CreateBtn(hwnd, IDC_BTN_MAP_PICK,  "Map", x, y, 36, eh);
    x += 36 + 2;
    state->hBtnMapPaste = CreateBtn(hwnd, IDC_BTN_MAP_PASTE, "<<",  x, y, 26, eh);

    /* Row C: Config buttons */
    y += eh + 8;
    x = inX;

    state->hBtnLoadConfig = CreateBtn(hwnd, IDC_BTN_LOAD_CONFIG, "Load Config", x, y, 110, bh);
    x += 110 + sp;
    state->hBtnSaveConfig = CreateBtn(hwnd, IDC_BTN_SAVE_CONFIG, "Save Config", x, y, 110, bh);
    x += 110 + sp;
    state->hBtnGenerate   = CreateBtn(hwnd, IDC_BTN_GENERATE,    "Generate Template", x, y, 160, bh);

    /* ── Row 2: Action buttons ──────────────────────────────── */
    y += bh + 14;
    x = m;

    state->hGroupActions = CreateWindowEx(0, "BUTTON", "Actions",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        m, y, 700, 55, hwnd, (HMENU)(intptr_t)IDC_GROUP_ACTIONS, hInst, NULL);

    y += 18;
    x = m + 10;

    state->hBtnGetMounts    = CreateBtn(hwnd, IDC_BTN_GET_MOUNTS,     "Get Mountpoints", x, y, bw, bh); x += bw + 5;
    state->hBtnOpenStream   = CreateBtn(hwnd, IDC_BTN_OPEN_STREAM,   "Open Stream",     x, y, bw, bh); x += bw + 5;
    state->hBtnCloseStream  = CreateBtn(hwnd, IDC_BTN_CLOSE_STREAM,  "Close Stream",    x, y, bw, bh);
    EnableWindow(state->hBtnCloseStream, FALSE);  /* disabled until a worker starts */

    /* ── Mountpoint ListView ────────────────────────────────── */
    y += bh + 14;
    state->hLvMountpoints = CreateWindowEx(WS_EX_CLIENTEDGE,
        WC_LISTVIEW, "",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
        m, y, 700, 140, hwnd, (HMENU)(intptr_t)IDC_LV_MOUNTPOINTS, hInst, NULL);
    ListView_SetExtendedListViewStyle(state->hLvMountpoints,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LvAddColumn(state->hLvMountpoints, 0,  "Mountpoint",    100);
    LvAddColumn(state->hLvMountpoints, 1,  "Identifier",    100);
    LvAddColumn(state->hLvMountpoints, 2,  "Format",         70);
    LvAddColumn(state->hLvMountpoints, 3,  "Details",        80);
    LvAddColumn(state->hLvMountpoints, 4,  "Carrier",        55);
    LvAddColumn(state->hLvMountpoints, 5,  "Nav Sys",        65);
    LvAddColumn(state->hLvMountpoints, 6,  "Network",        70);
    LvAddColumn(state->hLvMountpoints, 7,  "Country",        55);
    LvAddColumn(state->hLvMountpoints, 8,  "Lat",            60);
    LvAddColumn(state->hLvMountpoints, 9,  "Lon",            60);
    LvAddColumn(state->hLvMountpoints, 10, "Distance (km)",  90);

    /* ── Tab control (Log / Message Stats / Satellites) ─────── */
    y += 145;
    state->hTabOutput = CreateWindowEx(0,
        WC_TABCONTROL, "",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        m, y, 700, 200, hwnd, (HMENU)(intptr_t)IDC_TAB_OUTPUT, hInst, NULL);

    /* Insert tabs */
    TCITEM tci;
    ZeroMemory(&tci, sizeof(tci));
    tci.mask = TCIF_TEXT;
    tci.pszText = "Log";       TabCtrl_InsertItem(state->hTabOutput, 0, &tci);
    tci.pszText = "Msg Stats"; TabCtrl_InsertItem(state->hTabOutput, 1, &tci);
    tci.pszText = "Satellites"; TabCtrl_InsertItem(state->hTabOutput, 2, &tci);

    /* Child controls inside the tab area */
    RECT tabRC;
    GetClientRect(state->hTabOutput, &tabRC);
    TabCtrl_AdjustRect(state->hTabOutput, FALSE, &tabRC);

    int tx = tabRC.left + 2;
    int ty = y + tabRC.top + 2;
    int tw = tabRC.right - tabRC.left - 4;
    int th = tabRC.bottom - tabRC.top - 4;

    /* Log: multiline read-only EDIT */
    state->hEditLog = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY,
        tx, ty, tw, th, hwnd, (HMENU)(intptr_t)IDC_EDIT_LOG, hInst, NULL);
    /* Increase text limit to 1 MB */
    SendMessage(state->hEditLog, EM_SETLIMITTEXT, 0x100000, 0);
    /* Create a monospace font for the log (applied after EnumChildWindows below) */
    HFONT hMono = CreateFont(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");

    /* Message Stats ListView (hidden by default — shown on tab switch) */
    state->hLvMsgStats = CreateWindowEx(WS_EX_CLIENTEDGE,
        WC_LISTVIEW, "",
        WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        tx, ty, tw, th, hwnd, (HMENU)(intptr_t)IDC_LV_MSG_STATS, hInst, NULL);
    ListView_SetExtendedListViewStyle(state->hLvMsgStats,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LvAddColumn(state->hLvMsgStats, 0, "Message Type", 100);
    LvAddColumn(state->hLvMsgStats, 1, "Count",         70);
    LvAddColumn(state->hLvMsgStats, 2, "Min dt (s)",   100);
    LvAddColumn(state->hLvMsgStats, 3, "Max dt (s)",   100);
    LvAddColumn(state->hLvMsgStats, 4, "Avg dt (s)",   100);
    LvAddColumn(state->hLvMsgStats, 5, "Description",  220);

    /* Satellites ListView (hidden by default) */
    state->hLvSatellites = CreateWindowEx(WS_EX_CLIENTEDGE,
        WC_LISTVIEW, "",
        WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        tx, ty, tw, th, hwnd, (HMENU)(intptr_t)IDC_LV_SATELLITES, hInst, NULL);
    ListView_SetExtendedListViewStyle(state->hLvSatellites,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LvAddColumn(state->hLvSatellites, 0, "GNSS",        90);
    LvAddColumn(state->hLvSatellites, 1, "Sats Seen",   80);
    LvAddColumn(state->hLvSatellites, 2, "Satellites",  400);

    /* ── Status bar ─────────────────────────────────────────── */
    state->hStatusBar = CreateWindowEx(0,
        STATUSCLASSNAME, NULL,
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, hwnd, (HMENU)(intptr_t)IDC_STATUSBAR, hInst, NULL);

    /* Three-part status bar */
    int parts[3] = { 350, 500, -1 };
    SendMessage(state->hStatusBar, SB_SETPARTS, 3, (LPARAM)parts);
    SendMessage(state->hStatusBar, SB_SETTEXT, 0, (LPARAM)"Disconnected");
    SendMessage(state->hStatusBar, SB_SETTEXT, 1, (LPARAM)"");
    SendMessage(state->hStatusBar, SB_SETTEXT, 2, (LPARAM)"");

    /* ── Apply DEFAULT_GUI_FONT to ALL child controls ────────── */
    EnumChildWindows(hwnd, SetChildFont, (LPARAM)hGuiFont);

    /* Re-apply Consolas monospace font to the log edit control */
    if (hMono) SendMessage(state->hEditLog, WM_SETFONT, (WPARAM)hMono, TRUE);
}

/**
 * @brief Reposition / resize controls when the window size changes.
 * Called from WM_SIZE.
 */
void ResizeControls(HWND hwnd, AppState *state, int width, int height)
{
    int m = GUI_MARGIN;
    (void)hwnd;

    /* Status bar auto-sizes itself */
    SendMessage(state->hStatusBar, WM_SIZE, 0, 0);

    /* Get status bar height */
    RECT sbRect;
    GetWindowRect(state->hStatusBar, &sbRect);
    int sbHeight = sbRect.bottom - sbRect.top;

    int usableW = width - 2 * m;
    int usableH = height - sbHeight;

    /* Resize group boxes to fill width */
    /* Connection group: starts at y=m, height fixed 110 */
    MoveWindow(state->hGroupConnection, m, m, usableW, 110, TRUE);

    /* Actions group: starts below connection group */
    int actY = m + 110 + 6;
    MoveWindow(state->hGroupActions, m, actY, usableW, 55, TRUE);

    /* Mountpoint ListView: below actions group (height set by splitter) */
    int lvY = actY + 55 + 6;
    int lvH = state->splitterLvH;
    if (lvH < 60) lvH = 60;   /* minimum height */
    MoveWindow(state->hLvMountpoints, m, lvY, usableW, lvH, TRUE);

    /* Tab control: takes remaining space (5 px gap = splitter hit zone) */
    int tabY = lvY + lvH + 5;
    int tabH = usableH - tabY - 5;
    if (tabH < 80) tabH = 80;
    MoveWindow(state->hTabOutput, m, tabY, usableW, tabH, TRUE);

    /* Resize tab children to fit inside the tab area */
    RECT tabRC;
    GetClientRect(state->hTabOutput, &tabRC);
    TabCtrl_AdjustRect(state->hTabOutput, FALSE, &tabRC);

    /* Convert tab-relative coords to parent-relative */
    int tx = m + tabRC.left + 2;
    int ty = tabY + tabRC.top + 2;
    int tw = tabRC.right - tabRC.left - 4;
    int th = tabRC.bottom - tabRC.top - 4;

    MoveWindow(state->hEditLog,      tx, ty, tw, th, TRUE);
    MoveWindow(state->hLvMsgStats,   tx, ty, tw, th, TRUE);
    MoveWindow(state->hLvSatellites, tx, ty, tw, th, TRUE);

    /* Update status bar parts proportionally */
    int parts[3] = { width / 3, width * 2 / 3, -1 };
    SendMessage(state->hStatusBar, SB_SETPARTS, 3, (LPARAM)parts);
}
