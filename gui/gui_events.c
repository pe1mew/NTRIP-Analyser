/**
 * @file gui_events.c
 * @brief Main window procedure (WndProc) and event dispatch for NTRIP-Analyser GUI.
 *
 * Handles WM_CREATE, WM_SIZE, WM_COMMAND (buttons / menus), WM_NOTIFY (tab switch),
 * WM_GETMINMAXINFO, WM_DESTROY, and custom WM_APP+n messages from worker threads.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "resource.h"
#include "gui_state.h"
#include "rtcm3x_parser.h"
#include "config.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shellapi.h>
#include <commdlg.h>

/* ── Forward declarations for local helpers ───────────────── */
static void OnTabSelChange(AppState *state);
static void OnLoadConfig(HWND hwnd, AppState *state);
static void OnSaveConfig(HWND hwnd, AppState *state);
static void OnGenerateConfig(HWND hwnd, AppState *state);
static void OnGetMountpoints(HWND hwnd, AppState *state);
static void OnOpenStream(HWND hwnd, AppState *state);
static void OnCloseStream(HWND hwnd, AppState *state);
static void OnStreamDone(HWND hwnd, AppState *state);
static void OnStatUpdate(AppState *state, int msg_type, int count);
static void OnSatUpdate(AppState *state);

/* ── Generic ListView sort state ──────────────────────────── */
static int  g_sortColumn    = -1;   /* currently sorted column (-1 = none) */
static BOOL g_sortAscending = TRUE; /* TRUE = A→Z / ascending */
static HWND g_sortListView  = NULL; /* ListView handle for compare callback */
static BOOL g_sortNumeric   = FALSE;/* TRUE = compare as numbers */

/**
 * @brief Comparison callback for ListView_SortItemsEx.
 *
 * Uses g_sortNumeric to decide between numeric (atof) and text (_stricmp)
 * comparison.  Works for any ListView that sets the globals before calling
 * ListView_SortItemsEx.
 */
static int CALLBACK LvCompareFunc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
    (void)lParamSort;

    char buf1[256], buf2[256];
    ListView_GetItemText(g_sortListView, (int)lParam1, g_sortColumn, buf1, sizeof(buf1));
    ListView_GetItemText(g_sortListView, (int)lParam2, g_sortColumn, buf2, sizeof(buf2));

    int result;

    if (g_sortNumeric) {
        double v1 = atof(buf1);
        double v2 = atof(buf2);
        if (v1 < v2)      result = -1;
        else if (v1 > v2) result =  1;
        else               result =  0;
    } else {
        result = _stricmp(buf1, buf2);
    }

    return g_sortAscending ? result : -result;
}

/**
 * @brief Sort a ListView by a column and update the header sort arrows.
 *
 * Tracks per-ListView sort state using two static pairs so that the Msg
 * Stats and Mountpoint ListViews each remember their own column+direction.
 *
 * @param hLv       ListView handle.
 * @param col       Column index that was clicked.
 * @param isNumeric TRUE if the column should be sorted numerically.
 */
static void SortListView(HWND hLv, int col, BOOL isNumeric)
{
    /* ── Per-ListView sort state (two tracked ListViews) ──── */
    static HWND savedHwnd[2]  = { NULL, NULL };
    static int  savedCol[2]   = { -1, -1 };
    static BOOL savedAsc[2]   = { TRUE, TRUE };

    /* Find or assign a slot for this ListView */
    int slot = -1;
    for (int i = 0; i < 2; i++) {
        if (savedHwnd[i] == hLv) { slot = i; break; }
    }
    if (slot < 0) {
        for (int i = 0; i < 2; i++) {
            if (savedHwnd[i] == NULL) { slot = i; savedHwnd[i] = hLv; break; }
        }
    }
    if (slot < 0) slot = 0;  /* fallback: overwrite first slot */

    /* Toggle direction if same column, otherwise ascending */
    if (col == savedCol[slot]) {
        savedAsc[slot] = !savedAsc[slot];
    } else {
        savedCol[slot] = col;
        savedAsc[slot] = TRUE;
    }

    /* Set globals for the compare callback */
    g_sortListView = hLv;
    g_sortColumn   = col;
    g_sortAscending = savedAsc[slot];
    g_sortNumeric   = isNumeric;

    ListView_SortItemsEx(hLv, LvCompareFunc, 0);

    /* Update header sort arrows */
    HWND hHeader = ListView_GetHeader(hLv);
    int nCols = Header_GetItemCount(hHeader);
    for (int c = 0; c < nCols; c++) {
        HDITEM hdi;
        hdi.mask = HDI_FORMAT;
        Header_GetItem(hHeader, c, &hdi);
        hdi.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (c == col) {
            hdi.fmt |= savedAsc[slot] ? HDF_SORTUP : HDF_SORTDOWN;
        }
        Header_SetItem(hHeader, c, &hdi);
    }
}

/* ── ListView clipboard helpers ──────────────────────────── */

/**
 * @brief Select all items in a ListView.
 */
static void LvSelectAll(HWND hLv)
{
    int count = ListView_GetItemCount(hLv);
    for (int i = 0; i < count; i++)
        ListView_SetItemState(hLv, i, LVIS_SELECTED, LVIS_SELECTED);
}

/**
 * @brief Copy selected ListView rows to the clipboard as tab-separated text.
 *
 * Includes a header row from column names, then one line per selected item.
 */
static void LvCopySelection(HWND hLv)
{
    HWND hHeader = ListView_GetHeader(hLv);
    int nCols = Header_GetItemCount(hHeader);
    if (nCols <= 0) return;

    /* Build text in a growable buffer */
    int cap = 4096;
    int len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return;

    #define BUF_APPEND(s, slen) do {                          \
        while (len + (slen) + 1 > cap) {                     \
            cap *= 2;                                         \
            char *tmp = (char *)realloc(buf, cap);            \
            if (!tmp) { free(buf); return; }                  \
            buf = tmp;                                        \
        }                                                     \
        memcpy(buf + len, (s), (slen));                       \
        len += (slen);                                        \
    } while (0)

    /* Header row */
    for (int c = 0; c < nCols; c++) {
        char colName[128] = "";
        HDITEM hdi;
        hdi.mask = HDI_TEXT;
        hdi.pszText = colName;
        hdi.cchTextMax = sizeof(colName);
        Header_GetItem(hHeader, c, &hdi);

        if (c > 0) BUF_APPEND("\t", 1);
        BUF_APPEND(colName, (int)strlen(colName));
    }
    BUF_APPEND("\r\n", 2);

    /* Selected rows */
    int sel = -1;
    while ((sel = ListView_GetNextItem(hLv, sel, LVNI_SELECTED)) >= 0) {
        for (int c = 0; c < nCols; c++) {
            char cell[256] = "";
            ListView_GetItemText(hLv, sel, c, cell, sizeof(cell));
            if (c > 0) BUF_APPEND("\t", 1);
            BUF_APPEND(cell, (int)strlen(cell));
        }
        BUF_APPEND("\r\n", 2);
    }

    #undef BUF_APPEND

    buf[len] = '\0';

    /* Copy to Windows clipboard */
    if (OpenClipboard(hLv)) {
        EmptyClipboard();
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len + 1);
        if (hMem) {
            char *dst = (char *)GlobalLock(hMem);
            memcpy(dst, buf, len + 1);
            GlobalUnlock(hMem);
            SetClipboardData(CF_TEXT, hMem);
        }
        CloseClipboard();
    }
    free(buf);
}

/**
 * @brief Show a context menu with Select All / Copy at the cursor position.
 */
static void LvShowContextMenu(HWND hwnd, HWND hLv)
{
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    int selCount = ListView_GetSelectedCount(hLv);
    int total    = ListView_GetItemCount(hLv);

    AppendMenu(hMenu, MF_STRING, IDM_CTX_SELECT_ALL, "Select &All\tCtrl+A");
    AppendMenu(hMenu, MF_STRING | (selCount > 0 ? 0 : MF_GRAYED),
               IDM_CTX_COPY, "&Copy\tCtrl+C");

    if (total == 0) {
        EnableMenuItem(hMenu, IDM_CTX_SELECT_ALL, MF_GRAYED);
    }

    POINT pt;
    GetCursorPos(&pt);
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                             pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);

    if (cmd == IDM_CTX_SELECT_ALL) {
        LvSelectAll(hLv);
    } else if (cmd == IDM_CTX_COPY) {
        LvCopySelection(hLv);
    }
}

/**
 * @brief Get the Y range (in client coords) of the splitter hit zone.
 *
 * The splitter sits in the 5-pixel gap between the mountpoint ListView
 * bottom edge and the tab control top edge.  We widen the hit zone by
 * 2 px on each side for easier grabbing.
 */
static void GetSplitterRect(AppState *state, int *yTop, int *yBot)
{
    RECT rc;
    GetWindowRect(state->hLvMountpoints, &rc);
    MapWindowPoints(HWND_DESKTOP, state->hMain, (POINT *)&rc, 2);
    int gap = rc.bottom;          /* bottom of mountpoint ListView */
    *yTop = gap - 2;              /* 2 px above the gap */
    *yBot = gap + 5 + 2;         /* 5 px gap + 2 px below */
}

/**
 * @brief Switch visible tab child based on selected tab index.
 */
static void OnTabSelChange(AppState *state)
{
    int sel = TabCtrl_GetCurSel(state->hTabOutput);

    ShowWindow(state->hEditLog,      (sel == 0) ? SW_SHOW : SW_HIDE);
    ShowWindow(state->hLvMsgStats,   (sel == 1) ? SW_SHOW : SW_HIDE);
    ShowWindow(state->hLvSatellites, (sel == 2) ? SW_SHOW : SW_HIDE);
}

/**
 * @brief Append text to the log EDIT control.
 */
static void AppendLog(HWND hLog, const char *text)
{
    int len = GetWindowTextLength(hLog);
    SendMessage(hLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessage(hLog, EM_REPLACESEL, FALSE, (LPARAM)text);
}

/* ── Config ↔ GUI helpers ─────────────────────────────────── */

/**
 * @brief Read all edit controls into the AppState NTRIP_Config.
 */
void GuiToConfig(AppState *state)
{
    char buf[256];

    GetWindowText(state->hEditCaster, state->config.NTRIP_CASTER,
                  sizeof(state->config.NTRIP_CASTER));

    GetWindowText(state->hEditPort, buf, sizeof(buf));
    state->config.NTRIP_PORT = atoi(buf);
    if (state->config.NTRIP_PORT <= 0)
        state->config.NTRIP_PORT = 2101;

    GetWindowText(state->hEditMountpoint, state->config.MOUNTPOINT,
                  sizeof(state->config.MOUNTPOINT));
    GetWindowText(state->hEditUsername, state->config.USERNAME,
                  sizeof(state->config.USERNAME));
    GetWindowText(state->hEditPassword, state->config.PASSWORD,
                  sizeof(state->config.PASSWORD));

    GetWindowText(state->hEditLatitude, buf, sizeof(buf));
    state->config.LATITUDE = atof(buf);

    GetWindowText(state->hEditLongitude, buf, sizeof(buf));
    state->config.LONGITUDE = atof(buf);

    /* Recompute AUTH_BASIC from username:password */
    char auth[512];
    snprintf(auth, sizeof(auth), "%s:%s",
             state->config.USERNAME, state->config.PASSWORD);
    base64_encode(auth, state->config.AUTH_BASIC);
}

/**
 * @brief Populate all edit controls from the AppState NTRIP_Config.
 */
void ConfigToGui(AppState *state)
{
    char buf[64];

    SetWindowText(state->hEditCaster,     state->config.NTRIP_CASTER);

    snprintf(buf, sizeof(buf), "%d", state->config.NTRIP_PORT);
    SetWindowText(state->hEditPort, buf);

    SetWindowText(state->hEditMountpoint, state->config.MOUNTPOINT);
    SetWindowText(state->hEditUsername,   state->config.USERNAME);
    SetWindowText(state->hEditPassword,   state->config.PASSWORD);

    snprintf(buf, sizeof(buf), "%.6f", state->config.LATITUDE);
    SetWindowText(state->hEditLatitude, buf);

    snprintf(buf, sizeof(buf), "%.6f", state->config.LONGITUDE);
    SetWindowText(state->hEditLongitude, buf);
}

/* ── Load Config ──────────────────────────────────────────── */

static void OnLoadConfig(HWND hwnd, AppState *state)
{
    char filename[MAX_PATH] = "";
    OPENFILENAME ofn;

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = hwnd;
    ofn.lpstrFilter  = "JSON Config (*.json)\0*.json\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile    = filename;
    ofn.nMaxFile     = MAX_PATH;
    ofn.lpstrTitle   = "Load NTRIP Configuration";
    ofn.Flags        = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt  = "json";

    if (!GetOpenFileName(&ofn))
        return;  /* user cancelled */

    NTRIP_Config cfg;
    memset(&cfg, 0, sizeof(cfg));

    if (load_config(filename, &cfg) != 0) {
        char errmsg[MAX_PATH + 64];
        snprintf(errmsg, sizeof(errmsg),
                 "Failed to load config file:\n%s", filename);
        MessageBox(hwnd, errmsg, APP_TITLE, MB_ICONERROR | MB_OK);
        return;
    }

    /* Copy loaded config into state and update GUI fields */
    state->config = cfg;
    ConfigToGui(state);

    /* Log success */
    char logmsg[MAX_PATH + 32];
    snprintf(logmsg, sizeof(logmsg), "[INFO] Loaded config: %s\r\n", filename);
    AppendLog(state->hEditLog, logmsg);
}

/* ── Save Config ──────────────────────────────────────────── */

static void OnSaveConfig(HWND hwnd, AppState *state)
{
    /* Read current GUI fields into config */
    GuiToConfig(state);

    char filename[MAX_PATH] = "config.json";
    OPENFILENAME ofn;

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = hwnd;
    ofn.lpstrFilter  = "JSON Config (*.json)\0*.json\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile    = filename;
    ofn.nMaxFile     = MAX_PATH;
    ofn.lpstrTitle   = "Save NTRIP Configuration";
    ofn.Flags        = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt  = "json";

    if (!GetSaveFileName(&ofn))
        return;  /* user cancelled */

    /* Build JSON using cJSON */
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "NTRIP_CASTER", state->config.NTRIP_CASTER);
    cJSON_AddNumberToObject(json, "NTRIP_PORT",   state->config.NTRIP_PORT);
    cJSON_AddStringToObject(json, "MOUNTPOINT",   state->config.MOUNTPOINT);
    cJSON_AddStringToObject(json, "USERNAME",      state->config.USERNAME);
    cJSON_AddStringToObject(json, "PASSWORD",      state->config.PASSWORD);
    cJSON_AddNumberToObject(json, "LATITUDE",      state->config.LATITUDE);
    cJSON_AddNumberToObject(json, "LONGITUDE",     state->config.LONGITUDE);

    char *jsonStr = cJSON_Print(json);
    cJSON_Delete(json);

    if (!jsonStr) {
        MessageBox(hwnd, "Failed to serialize configuration to JSON.",
                   APP_TITLE, MB_ICONERROR | MB_OK);
        return;
    }

    FILE *f = fopen(filename, "w");
    if (!f) {
        char errmsg[MAX_PATH + 64];
        snprintf(errmsg, sizeof(errmsg),
                 "Failed to open file for writing:\n%s", filename);
        MessageBox(hwnd, errmsg, APP_TITLE, MB_ICONERROR | MB_OK);
        free(jsonStr);
        return;
    }

    fputs(jsonStr, f);
    fclose(f);
    free(jsonStr);

    char logmsg[MAX_PATH + 32];
    snprintf(logmsg, sizeof(logmsg), "[INFO] Saved config: %s\r\n", filename);
    AppendLog(state->hEditLog, logmsg);
}

/* ── Generate Template Config ─────────────────────────────── */

static void OnGenerateConfig(HWND hwnd, AppState *state)
{
    char filename[MAX_PATH] = "config.json";
    OPENFILENAME ofn;

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = hwnd;
    ofn.lpstrFilter  = "JSON Config (*.json)\0*.json\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile    = filename;
    ofn.nMaxFile     = MAX_PATH;
    ofn.lpstrTitle   = "Generate Template Configuration";
    ofn.Flags        = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt  = "json";

    if (!GetSaveFileName(&ofn))
        return;  /* user cancelled */

    /* Write template JSON directly (same content as initialize_config) */
    FILE *f = fopen(filename, "w");
    if (!f) {
        char errmsg[MAX_PATH + 64];
        snprintf(errmsg, sizeof(errmsg),
                 "Failed to create file:\n%s", filename);
        MessageBox(hwnd, errmsg, APP_TITLE, MB_ICONERROR | MB_OK);
        return;
    }

    fprintf(f,
        "{\n"
        "    \"NTRIP_CASTER\": \"your.caster.example.com\",\n"
        "    \"NTRIP_PORT\": 2101,\n"
        "    \"MOUNTPOINT\": \"MOUNTPOINT\",\n"
        "    \"USERNAME\": \"your_username\",\n"
        "    \"PASSWORD\": \"your_password\",\n"
        "    \"LATITUDE\": 0.0,\n"
        "    \"LONGITUDE\": 0.0\n"
        "}\n");
    fclose(f);

    char logmsg[MAX_PATH + 48];
    snprintf(logmsg, sizeof(logmsg),
             "[INFO] Template config created: %s\r\n", filename);
    AppendLog(state->hEditLog, logmsg);
}

/* ── Get Mountpoints ──────────────────────────────────────── */

static void OnGetMountpoints(HWND hwnd, AppState *state)
{
    if (state->bWorkerRunning) {
        MessageBox(hwnd, "A background task is already running.\nPlease wait or press Close Stream first.",
                   APP_TITLE, MB_ICONWARNING | MB_OK);
        return;
    }

    /* Validate that a caster is configured */
    if (state->config.NTRIP_CASTER[0] == '\0') {
        GuiToConfig(state);
    }
    if (state->config.NTRIP_CASTER[0] == '\0') {
        MessageBox(hwnd, "Please enter a caster address before requesting mountpoints.",
                   APP_TITLE, MB_ICONWARNING | MB_OK);
        return;
    }

    /* Sync GUI fields into config (ensures latest edits are used) */
    GuiToConfig(state);

    state->bWorkerRunning = TRUE;
    state->bStopRequested = FALSE;
    EnableWindow(state->hBtnCloseStream, TRUE);

    AppendLog(state->hEditLog, "[INFO] Requesting mountpoint list...\r\n");
    SendMessage(state->hStatusBar, SB_SETTEXT, 0, (LPARAM)"Connecting...");

    /* Launch worker thread */
    state->hWorkerThread = CreateThread(NULL, 0, WorkerGetMountpoints, state, 0, NULL);
    if (!state->hWorkerThread) {
        state->bWorkerRunning = FALSE;
        EnableWindow(state->hBtnCloseStream, FALSE);
        AppendLog(state->hEditLog, "[ERROR] Failed to create worker thread.\r\n");
        SendMessage(state->hStatusBar, SB_SETTEXT, 0, (LPARAM)"Error");
    }
}

/* ── Open Stream ──────────────────────────────────────────── */

static void OnOpenStream(HWND hwnd, AppState *state)
{
    if (state->bWorkerRunning) {
        MessageBox(hwnd, "A background task is already running.\nPlease wait or press Close Stream first.",
                   APP_TITLE, MB_ICONWARNING | MB_OK);
        return;
    }

    /* Sync GUI fields into config */
    GuiToConfig(state);

    if (state->config.NTRIP_CASTER[0] == '\0') {
        MessageBox(hwnd, "Please enter a caster address.",
                   APP_TITLE, MB_ICONWARNING | MB_OK);
        return;
    }
    if (state->config.MOUNTPOINT[0] == '\0') {
        MessageBox(hwnd, "Please enter or select a mountpoint.",
                   APP_TITLE, MB_ICONWARNING | MB_OK);
        return;
    }

    state->bWorkerRunning = TRUE;
    state->bStopRequested = FALSE;
    EnableWindow(state->hBtnCloseStream, TRUE);

    /* Clear previous stats and ListViews */
    memset(state->msgStats, 0, sizeof(state->msgStats));
    memset(&state->satStats, 0, sizeof(state->satStats));
    ListView_DeleteAllItems(state->hLvMsgStats);
    ListView_DeleteAllItems(state->hLvSatellites);

    /* Reset stream info */
    InterlockedExchange(&state->streamBytes, 0);
    InterlockedExchange(&state->streamFormat, 0);
    state->streamBytesLast = 0;
    state->streamRateTime  = gui_get_time_seconds();

    /* Look up the Format column from the sourcetable for the selected
     * mountpoint.  This tells us the declared stream format (e.g.
     * "RTCM 3.2", "RT27", "LB2") so the worker can identify RAW
     * streams that are wrapped inside RTCM 3.x framing.
     *
     * Strategy: first check the currently selected/focused row (fast
     * path when user double-clicked a row), then fall back to a name
     * search through all rows.  Skip leading '/' in mountpoint names
     * since some configs store it with or without the slash. */
    state->sourceFormat[0] = '\0';
    state->sourceDetails[0] = '\0';
    {
        const char *mpName = state->config.MOUNTPOINT;
        if (mpName[0] == '/') mpName++;   /* skip leading '/' */

        int found = -1;

        /* Fast path: check focused/selected row first */
        int sel = ListView_GetNextItem(state->hLvMountpoints, -1, LVNI_SELECTED);
        if (sel >= 0) {
            char mp[256] = "";
            ListView_GetItemText(state->hLvMountpoints, sel, 0, mp, sizeof(mp));
            const char *c = (mp[0] == '/') ? mp + 1 : mp;
            if (_stricmp(c, mpName) == 0) found = sel;
        }

        /* Fallback: search all rows */
        if (found < 0) {
            int count = ListView_GetItemCount(state->hLvMountpoints);
            for (int i = 0; i < count; i++) {
                char mp[256] = "";
                ListView_GetItemText(state->hLvMountpoints, i, 0, mp, sizeof(mp));
                const char *c = (mp[0] == '/') ? mp + 1 : mp;
                if (_stricmp(c, mpName) == 0) { found = i; break; }
            }
        }

        if (found >= 0) {
            ListView_GetItemText(state->hLvMountpoints, found, 2,
                                 state->sourceFormat, sizeof(state->sourceFormat));
            ListView_GetItemText(state->hLvMountpoints, found, 3,
                                 state->sourceDetails, sizeof(state->sourceDetails));
        }
    }

    /* Switch to the Msg Stats tab for real-time updates */
    TabCtrl_SetCurSel(state->hTabOutput, 1);
    OnTabSelChange(state);

    AppendLog(state->hEditLog, "[INFO] Opening NTRIP stream...\r\n");
    SendMessage(state->hStatusBar, SB_SETTEXT, 0, (LPARAM)"Connecting...");
    SendMessage(state->hStatusBar, SB_SETTEXT, 1, (LPARAM)"");
    SendMessage(state->hStatusBar, SB_SETTEXT, 2, (LPARAM)"");

    /* Redirect stdout/stderr to pipe so printf output goes to log panel */
    LogRedirectStart(state);

    /* Start a timer to pump pipe data into the log control */
    SetTimer(hwnd, IDT_LOG_PUMP, 100, NULL);  /* 100 ms interval */

    /* Start a timer for status bar updates (data rate, activity) */
    SetTimer(hwnd, IDT_STATUS_UPDATE, 1000, NULL);  /* 1 s interval */

    /* Launch worker thread */
    state->hWorkerThread = CreateThread(NULL, 0, WorkerOpenStream, state, 0, NULL);
    if (!state->hWorkerThread) {
        KillTimer(hwnd, IDT_LOG_PUMP);
        KillTimer(hwnd, IDT_STATUS_UPDATE);
        LogRedirectStop(state);
        state->bWorkerRunning = FALSE;
        EnableWindow(state->hBtnCloseStream, FALSE);
        AppendLog(state->hEditLog, "[ERROR] Failed to create worker thread.\r\n");
        SendMessage(state->hStatusBar, SB_SETTEXT, 0, (LPARAM)"Error");
    }
}

/* ── Close Stream ─────────────────────────────────────────── */

static void OnCloseStream(HWND hwnd, AppState *state)
{
    (void)hwnd;

    if (!state->bWorkerRunning) return;

    /* Close all open detail windows */
    for (int i = 0; i < GUI_MAX_MSG_TYPES; i++) {
        if (state->hDetailWnds[i]) {
            DestroyWindow(state->hDetailWnds[i]);
            state->hDetailWnds[i] = NULL;
        }
    }

    state->bStopRequested = TRUE;
    AppendLog(state->hEditLog, "\r\n[INFO] Closing stream...\r\n");
    SendMessage(state->hStatusBar, SB_SETTEXT, 0, (LPARAM)"Closing...");

    /* Wait for the worker to notice bStopRequested via SO_RCVTIMEO */
    if (state->hWorkerThread) {
        WaitForSingleObject(state->hWorkerThread, 3000);
        CloseHandle(state->hWorkerThread);
        state->hWorkerThread = NULL;
    }

    /* Clean up — stop timers, restore stdout/stderr */
    KillTimer(hwnd, IDT_LOG_PUMP);
    KillTimer(hwnd, IDT_STATUS_UPDATE);
    LogRedirectStop(state);

    state->bWorkerRunning = FALSE;
    EnableWindow(state->hBtnCloseStream, FALSE);

    AppendLog(state->hEditLog, "[INFO] Stream closed.\r\n");
    SendMessage(state->hStatusBar, SB_SETTEXT, 0, (LPARAM)"Disconnected");
    SendMessage(state->hStatusBar, SB_SETTEXT, 1, (LPARAM)"");
    SendMessage(state->hStatusBar, SB_SETTEXT, 2, (LPARAM)"");
}

/* ── Stream Done (worker finished naturally) ──────────────── */

static void OnStreamDone(HWND hwnd, AppState *state)
{
    /* Close all open detail windows */
    for (int i = 0; i < GUI_MAX_MSG_TYPES; i++) {
        if (state->hDetailWnds[i]) {
            DestroyWindow(state->hDetailWnds[i]);
            state->hDetailWnds[i] = NULL;
        }
    }

    /* Stop timers */
    KillTimer(hwnd, IDT_LOG_PUMP);
    KillTimer(hwnd, IDT_STATUS_UPDATE);

    /* Final drain of any remaining pipe data */
    LogPumpTimer(state);

    /* Restore stdout/stderr */
    LogRedirectStop(state);

    state->bWorkerRunning = FALSE;
    EnableWindow(state->hBtnCloseStream, FALSE);

    if (state->hWorkerThread) {
        CloseHandle(state->hWorkerThread);
        state->hWorkerThread = NULL;
    }

    AppendLog(state->hEditLog, "\r\n[INFO] Stream ended.\r\n");
    SendMessage(state->hStatusBar, SB_SETTEXT, 0, (LPARAM)"Disconnected");
    SendMessage(state->hStatusBar, SB_SETTEXT, 1, (LPARAM)"");
    SendMessage(state->hStatusBar, SB_SETTEXT, 2, (LPARAM)"");
}

/* ── Map picker helpers ─────────────────────────────────────── */

/**
 * @brief Open an interactive Leaflet.js map in the default browser.
 *
 * Writes a self-contained HTML file to %TEMP% centered on the current
 * Lat/Lon values.  Clicking the map copies "lat,lon" to the clipboard.
 */
static void OnMapPick(HWND hwnd, AppState *state)
{
    (void)hwnd;

    /* Read current lat/lon from edit controls */
    char latBuf[64], lonBuf[64];
    GetWindowText(state->hEditLatitude,  latBuf, sizeof(latBuf));
    GetWindowText(state->hEditLongitude, lonBuf, sizeof(lonBuf));

    double lat = atof(latBuf);
    double lon = atof(lonBuf);

    /* Default to centre of Europe if coordinates are 0,0 */
    if (lat == 0.0 && lon == 0.0) {
        lat = 51.505;
        lon = -0.09;
    }

    int zoom = 6;

    /* Build HTML content with embedded Leaflet.js */
    char html[8192];
    snprintf(html, sizeof(html),
        "<!DOCTYPE html>\n"
        "<html><head><meta charset=\"utf-8\">\n"
        "<title>Pick Location - NTRIP-Analyser</title>\n"
        "<link rel=\"stylesheet\" href=\"https://unpkg.com/leaflet@1.9.4/dist/leaflet.css\"/>\n"
        "<script src=\"https://unpkg.com/leaflet@1.9.4/dist/leaflet.js\"></script>\n"
        "<style>\n"
        "  body{margin:0;font-family:sans-serif}\n"
        "  #map{height:calc(100vh - 50px)}\n"
        "  #bar{height:50px;display:flex;align-items:center;justify-content:center;"
        "background:#333;color:#fff;font-size:16px;gap:15px}\n"
        "  #coords{font-family:monospace;font-size:18px;color:#0f0}\n"
        "  #status{font-size:13px;color:#aaa;transition:opacity 0.3s}\n"
        "</style>\n"
        "</head><body>\n"
        "<div id=\"bar\">\n"
        "  <span>Click the map to pick a location:</span>\n"
        "  <span id=\"coords\">%.6f, %.6f</span>\n"
        "  <span id=\"status\"></span>\n"
        "</div>\n"
        "<div id=\"map\"></div>\n"
        "<script>\n"
        "var map=L.map('map').setView([%.6f,%.6f],%d);\n"
        "L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png',{\n"
        "  maxZoom:19,attribution:'&copy; OpenStreetMap contributors'}).addTo(map);\n"
        "var marker=L.marker([%.6f,%.6f]).addTo(map);\n"
        "var st=document.getElementById('status');\n"
        "function copyText(t){\n"
        "  if(navigator.clipboard&&window.isSecureContext){\n"
        "    navigator.clipboard.writeText(t);\n"
        "  }else{\n"
        "    var ta=document.createElement('textarea');\n"
        "    ta.value=t;ta.style.position='fixed';ta.style.left='-9999px';\n"
        "    document.body.appendChild(ta);ta.select();\n"
        "    document.execCommand('copy');\n"
        "    document.body.removeChild(ta);\n"
        "  }\n"
        "}\n"
        "map.on('click',function(e){\n"
        "  var la=e.latlng.lat.toFixed(6);\n"
        "  var lo=e.latlng.lng.toFixed(6);\n"
        "  marker.setLatLng(e.latlng);\n"
        "  document.getElementById('coords').textContent=la+', '+lo;\n"
        "  copyText(la+','+lo);\n"
        "  st.textContent='Copied to clipboard!';\n"
        "  st.style.opacity='1';\n"
        "  setTimeout(function(){st.style.opacity='0'},2000);\n"
        "});\n"
        "</script>\n"
        "</body></html>\n",
        lat, lon,       /* initial coords display */
        lat, lon, zoom, /* map centre + zoom */
        lat, lon        /* initial marker */
    );

    /* Write to temp file */
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);

    char filePath[MAX_PATH + 32];
    snprintf(filePath, sizeof(filePath), "%sntrip_map_picker.html", tempPath);

    FILE *f = fopen(filePath, "w");
    if (!f) {
        MessageBox(hwnd, "Failed to create temporary map file.",
                   APP_TITLE, MB_ICONERROR | MB_OK);
        return;
    }
    fputs(html, f);
    fclose(f);

    /* Open in default browser */
    ShellExecuteA(NULL, "open", filePath, NULL, NULL, SW_SHOWNORMAL);

    AppendLog(state->hEditLog,
        "[INFO] Map opened in browser. Click to pick location, "
        "then press \"<<\" to paste coordinates.\r\n");
}

/**
 * @brief Read "lat,lon" from the clipboard and populate the Lat/Lon edit controls.
 */
static void OnMapPaste(HWND hwnd, AppState *state)
{
    if (!OpenClipboard(hwnd)) {
        MessageBox(hwnd, "Cannot open clipboard.",
                   APP_TITLE, MB_ICONWARNING | MB_OK);
        return;
    }

    HANDLE hData = GetClipboardData(CF_TEXT);
    if (!hData) {
        CloseClipboard();
        MessageBox(hwnd,
            "No text data on clipboard.\n\n"
            "Click \"Map\" first, pick a location on the map,\n"
            "then press \"<<\" to paste coordinates.",
            APP_TITLE, MB_ICONINFORMATION | MB_OK);
        return;
    }

    char *clipText = (char *)GlobalLock(hData);
    if (!clipText) {
        CloseClipboard();
        return;
    }

    /* Parse "lat,lon" — allow optional whitespace around comma */
    double lat = 0.0, lon = 0.0;
    int parsed = sscanf(clipText, "%lf , %lf", &lat, &lon);
    if (parsed != 2)
        parsed = sscanf(clipText, "%lf %lf", &lat, &lon);

    GlobalUnlock(hData);
    CloseClipboard();

    if (parsed != 2) {
        MessageBox(hwnd,
            "Clipboard does not contain valid coordinates.\n\n"
            "Expected format: \"lat,lon\" (e.g. \"52.123456,4.567890\")\n\n"
            "Click \"Map\" first, pick a location on the map,\n"
            "then press \"<<\" to paste coordinates.",
            APP_TITLE, MB_ICONWARNING | MB_OK);
        return;
    }

    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) {
        MessageBox(hwnd,
            "Coordinates out of range.\n"
            "Latitude must be -90 to 90, Longitude must be -180 to 180.",
            APP_TITLE, MB_ICONWARNING | MB_OK);
        return;
    }

    /* Populate the edit controls */
    char buf[64];
    snprintf(buf, sizeof(buf), "%.6f", lat);
    SetWindowText(state->hEditLatitude, buf);

    snprintf(buf, sizeof(buf), "%.6f", lon);
    SetWindowText(state->hEditLongitude, buf);

    char logmsg[128];
    snprintf(logmsg, sizeof(logmsg),
             "[INFO] Pasted coordinates: %.6f, %.6f\r\n", lat, lon);
    AppendLog(state->hEditLog, logmsg);
}

/* ── RTCM message type description lookup ─────────────────── */

const char* RtcmMsgDescription(int msg_type)
{
    switch (msg_type) {
    case 1001: return "GPS L1 Observables";
    case 1002: return "GPS L1 Observables (ext)";
    case 1003: return "GPS L1/L2 Observables";
    case 1004: return "GPS L1/L2 Observables (ext)";
    case 1005: return "Station ARP";
    case 1006: return "Station ARP + Height";
    case 1007: return "Antenna Descriptor";
    case 1008: return "Antenna Descriptor + Serial";
    case 1009: return "GLONASS L1 Observables";
    case 1010: return "GLONASS L1 Observables (ext)";
    case 1011: return "GLONASS L1/L2 Observables";
    case 1012: return "GLONASS L1/L2 Observables (ext)";
    case 1013: return "System Parameters";
    case 1019: return "GPS Ephemeris";
    case 1020: return "GLONASS Ephemeris";
    case 1033: return "Receiver + Antenna Descriptor";
    case 1042: return "BeiDou Ephemeris";
    case 1044: return "QZSS Ephemeris";
    case 1045: return "Galileo F/NAV Ephemeris";
    case 1046: return "Galileo I/NAV Ephemeris";
    case 1071: return "MSM1 GPS";
    case 1072: return "MSM2 GPS";
    case 1073: return "MSM3 GPS";
    case 1074: return "MSM4 GPS";
    case 1075: return "MSM5 GPS";
    case 1076: return "MSM6 GPS";
    case 1077: return "MSM7 GPS";
    case 1081: return "MSM1 GLONASS";
    case 1082: return "MSM2 GLONASS";
    case 1083: return "MSM3 GLONASS";
    case 1084: return "MSM4 GLONASS";
    case 1085: return "MSM5 GLONASS";
    case 1086: return "MSM6 GLONASS";
    case 1087: return "MSM7 GLONASS";
    case 1091: return "MSM1 Galileo";
    case 1092: return "MSM2 Galileo";
    case 1093: return "MSM3 Galileo";
    case 1094: return "MSM4 Galileo";
    case 1095: return "MSM5 Galileo";
    case 1096: return "MSM6 Galileo";
    case 1097: return "MSM7 Galileo";
    case 1101: return "MSM1 SBAS";
    case 1102: return "MSM2 SBAS";
    case 1103: return "MSM3 SBAS";
    case 1104: return "MSM4 SBAS";
    case 1105: return "MSM5 SBAS";
    case 1106: return "MSM6 SBAS";
    case 1107: return "MSM7 SBAS";
    case 1111: return "MSM1 QZSS";
    case 1112: return "MSM2 QZSS";
    case 1113: return "MSM3 QZSS";
    case 1114: return "MSM4 QZSS";
    case 1115: return "MSM5 QZSS";
    case 1116: return "MSM6 QZSS";
    case 1117: return "MSM7 QZSS";
    case 1121: return "MSM1 BeiDou";
    case 1122: return "MSM2 BeiDou";
    case 1123: return "MSM3 BeiDou";
    case 1124: return "MSM4 BeiDou";
    case 1125: return "MSM5 BeiDou";
    case 1126: return "MSM6 BeiDou";
    case 1127: return "MSM7 BeiDou";
    case 1131: return "MSM1 NavIC/IRNSS";
    case 1132: return "MSM2 NavIC/IRNSS";
    case 1133: return "MSM3 NavIC/IRNSS";
    case 1134: return "MSM4 NavIC/IRNSS";
    case 1135: return "MSM5 NavIC/IRNSS";
    case 1136: return "MSM6 NavIC/IRNSS";
    case 1137: return "MSM7 NavIC/IRNSS";
    case 1230: return "GLONASS Code-Phase Biases";
    case 4072: return "Reference Station (u-blox)";
    default:   return "";
    }
}

/* ── Stat Update (real-time, per-message) ─────────────────── */

static void OnStatUpdate(AppState *state, int msg_type, int count)
{
    if (msg_type <= 0 || msg_type >= GUI_MAX_MSG_TYPES) return;

    GuiMsgStat *s = &state->msgStats[msg_type];
    char buf[64];

    /* Search for existing row with this message type */
    int nItems = ListView_GetItemCount(state->hLvMsgStats);
    int row = -1;

    for (int i = 0; i < nItems; i++) {
        char existing[32];
        ListView_GetItemText(state->hLvMsgStats, i, 0, existing, sizeof(existing));
        if (atoi(existing) == msg_type) {
            row = i;
            break;
        }
    }

    /* Insert new row if not found */
    if (row < 0) {
        row = nItems;
        snprintf(buf, sizeof(buf), "%d", msg_type);
        LVITEM lvi;
        ZeroMemory(&lvi, sizeof(lvi));
        lvi.mask    = LVIF_TEXT;
        lvi.iItem   = row;
        lvi.pszText = buf;
        ListView_InsertItem(state->hLvMsgStats, &lvi);

        /* Column 5: Description (set once on insert) */
        const char *desc = RtcmMsgDescription(msg_type);
        ListView_SetItemText(state->hLvMsgStats, row, 5, (LPSTR)desc);
    }

    /* Column 1: Count */
    snprintf(buf, sizeof(buf), "%d", count);
    ListView_SetItemText(state->hLvMsgStats, row, 1, buf);

    /* Column 2: Min dt */
    snprintf(buf, sizeof(buf), "%.3f", s->min_dt);
    ListView_SetItemText(state->hLvMsgStats, row, 2, buf);

    /* Column 3: Max dt */
    snprintf(buf, sizeof(buf), "%.3f", s->max_dt);
    ListView_SetItemText(state->hLvMsgStats, row, 3, buf);

    /* Column 4: Avg dt */
    double avg = (s->count > 1) ? s->sum_dt / (s->count - 1) : 0.0;
    snprintf(buf, sizeof(buf), "%.3f", avg);
    ListView_SetItemText(state->hLvMsgStats, row, 4, buf);
}

/* ── Satellite Update (real-time, per-message) ────────────── */

static void OnSatUpdate(AppState *state)
{
    SatStatsSummary *sat = &state->satStats;
    char buf[2048];

    for (int g = 0; g < sat->gnss_count; g++) {
        GnssSatStats *gs = &sat->gnss[g];
        const char *name = gnss_name_from_id(gs->gnss_id);

        /* Search for existing row with this GNSS name */
        int nItems = ListView_GetItemCount(state->hLvSatellites);
        int row = -1;

        for (int i = 0; i < nItems; i++) {
            char existing[32];
            ListView_GetItemText(state->hLvSatellites, i, 0, existing, sizeof(existing));
            if (strcmp(existing, name) == 0) {
                row = i;
                break;
            }
        }

        /* Insert new row if not found */
        if (row < 0) {
            row = nItems;
            LVITEM lvi;
            ZeroMemory(&lvi, sizeof(lvi));
            lvi.mask    = LVIF_TEXT;
            lvi.iItem   = row;
            lvi.pszText = (LPSTR)name;
            ListView_InsertItem(state->hLvSatellites, &lvi);
        }

        /* Column 1: Sats Seen count */
        snprintf(buf, sizeof(buf), "%d", gs->count);
        ListView_SetItemText(state->hLvSatellites, row, 1, buf);

        /* Column 2: RINEX satellite IDs */
        buf[0] = '\0';
        int pos = 0;
        for (int s = 1; s <= MAX_SATS_PER_GNSS; s++) {
            if (gs->sat_seen[s - 1]) {
                char id[8];
                rinex_id_from_gnss(gs->gnss_id, s, id, sizeof(id));
                if (pos > 0 && pos < (int)sizeof(buf) - 6)
                    buf[pos++] = ' ';
                int wrote = snprintf(buf + pos, sizeof(buf) - pos, "%s", id);
                if (wrote > 0) pos += wrote;
            }
        }
        ListView_SetItemText(state->hLvSatellites, row, 2, buf);
    }
}

/**
 * @brief Main window procedure.
 */
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    AppState *state;

    switch (msg) {

    case WM_CREATE: {
        /* Store AppState pointer passed from CreateWindowEx lpParam */
        CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
        state = (AppState *)cs->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)state);

        /* Create all child controls */
        CreateControls(hwnd, state);

        /* Show the log tab by default */
        OnTabSelChange(state);

        /* Initial welcome message */
        AppendLog(state->hEditLog,
            "NTRIP-Analyser GUI started.\r\n"
            "Load a config file or enter connection details, then select an action.\r\n\r\n");

        return 0;
    }

    case WM_SIZE: {
        state = GetAppState(hwnd);
        if (state) {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            ResizeControls(hwnd, state, w, h);
        }
        return 0;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = (MINMAXINFO *)lParam;
        mmi->ptMinTrackSize.x = APP_MIN_WIDTH;
        mmi->ptMinTrackSize.y = APP_MIN_HEIGHT;
        return 0;
    }

    /* ── Splitter: resize mountpoint list by dragging ──────── */

    case WM_SETCURSOR: {
        state = GetAppState(hwnd);
        if (state && (HWND)wParam == hwnd) {
            DWORD pos = GetMessagePos();
            POINT pt = { (short)LOWORD(pos), (short)HIWORD(pos) };
            ScreenToClient(hwnd, &pt);
            int yTop, yBot;
            GetSplitterRect(state, &yTop, &yBot);
            if (pt.y >= yTop && pt.y <= yBot) {
                SetCursor(LoadCursor(NULL, IDC_SIZENS));
                return TRUE;
            }
        }
        break;  /* fall through to DefWindowProc */
    }

    case WM_LBUTTONDOWN: {
        state = GetAppState(hwnd);
        if (!state) break;
        int mouseY = (short)HIWORD(lParam);
        int yTop, yBot;
        GetSplitterRect(state, &yTop, &yBot);
        if (mouseY >= yTop && mouseY <= yBot) {
            state->splitterDragging  = TRUE;
            state->splitterDragStartY = mouseY;
            state->splitterDragStartH = state->splitterLvH;
            SetCapture(hwnd);
            return 0;
        }
        break;
    }

    case WM_MOUSEMOVE: {
        state = GetAppState(hwnd);
        if (state && state->splitterDragging) {
            int mouseY = (short)HIWORD(lParam);
            int delta  = mouseY - state->splitterDragStartY;
            int newH   = state->splitterDragStartH + delta;

            /* Clamp: minimum 60 px, leave at least 80 px for tab area */
            RECT clientRC;
            GetClientRect(hwnd, &clientRC);
            RECT sbRect;
            GetWindowRect(state->hStatusBar, &sbRect);
            int sbH = sbRect.bottom - sbRect.top;

            /* Calculate top of mountpoint list */
            int lvTop = GUI_MARGIN + 110 + 6 + 55 + 6;
            int maxH  = (clientRC.bottom - sbH) - lvTop - 5 - 80;
            if (newH < 60)   newH = 60;
            if (newH > maxH) newH = maxH;

            state->splitterLvH = newH;
            ResizeControls(hwnd, state, clientRC.right, clientRC.bottom);
            return 0;
        }
        break;
    }

    case WM_LBUTTONUP: {
        state = GetAppState(hwnd);
        if (state && state->splitterDragging) {
            state->splitterDragging = FALSE;
            ReleaseCapture();
            return 0;
        }
        break;
    }

    case WM_NOTIFY: {
        NMHDR *nmh = (NMHDR *)lParam;
        state = GetAppState(hwnd);
        if (!state) break;

        if (nmh->idFrom == IDC_TAB_OUTPUT && nmh->code == TCN_SELCHANGE) {
            OnTabSelChange(state);
        }

        /* Double-click on mountpoint ListView → copy mountpoint to config */
        if (nmh->idFrom == IDC_LV_MOUNTPOINTS && nmh->code == NM_DBLCLK) {
            int sel = ListView_GetNextItem(state->hLvMountpoints, -1, LVNI_SELECTED);
            if (sel >= 0) {
                char mount[256] = "";
                ListView_GetItemText(state->hLvMountpoints, sel, 0, mount, sizeof(mount));
                if (mount[0]) {
                    SetWindowText(state->hEditMountpoint, mount);
                    GuiToConfig(state);

                    char logmsg[320];
                    snprintf(logmsg, sizeof(logmsg),
                             "[INFO] Mountpoint set to: %s\r\n", mount);
                    AppendLog(state->hEditLog, logmsg);
                }
            }
        }

        /* Double-click on Msg Stats ListView → open detail window */
        if (nmh->idFrom == IDC_LV_MSG_STATS && nmh->code == NM_DBLCLK) {
            NMITEMACTIVATE *nmia = (NMITEMACTIVATE *)lParam;
            int sel = nmia->iItem;
            if (sel >= 0) {
                char typeBuf[32];
                ListView_GetItemText(state->hLvMsgStats, sel, 0,
                                     typeBuf, sizeof(typeBuf));
                int mt = atoi(typeBuf);
                if (mt > 0 && mt < GUI_MAX_MSG_TYPES) {
                    if (state->hDetailWnds[mt]) {
                        /* Already open — bring to front */
                        SetForegroundWindow(state->hDetailWnds[mt]);
                    } else {
                        /* Create new detail window */
                        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(
                            hwnd, GWLP_HINSTANCE);
                        HWND hDet = CreateDetailWindow(hInst, hwnd, mt);
                        if (hDet)
                            state->hDetailWnds[mt] = hDet;
                    }
                }
            }
        }

        /* Column header click on Msg Stats ListView → sort rows */
        if (nmh->idFrom == IDC_LV_MSG_STATS && nmh->code == LVN_COLUMNCLICK) {
            NMLISTVIEW *nmlv = (NMLISTVIEW *)lParam;
            int col = nmlv->iSubItem;
            /* Columns 0–4 are numeric, column 5 (Description) is text */
            SortListView(state->hLvMsgStats, col, (col <= 4));
        }

        /* Column header click on Mountpoint ListView → sort rows */
        if (nmh->idFrom == IDC_LV_MOUNTPOINTS && nmh->code == LVN_COLUMNCLICK) {
            NMLISTVIEW *nmlv = (NMLISTVIEW *)lParam;
            int col = nmlv->iSubItem;
            /* Columns 8 (Lat), 9 (Lon), 10 (Distance) are numeric; rest is text */
            SortListView(state->hLvMountpoints, col, (col >= 8));
        }

        /* Keyboard shortcuts for mountpoint ListView: Ctrl+A, Ctrl+C */
        if (nmh->idFrom == IDC_LV_MOUNTPOINTS && nmh->code == LVN_KEYDOWN) {
            NMLVKEYDOWN *kd = (NMLVKEYDOWN *)lParam;
            BOOL ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (ctrl && kd->wVKey == 'A') {
                LvSelectAll(state->hLvMountpoints);
            } else if (ctrl && kd->wVKey == 'C') {
                LvCopySelection(state->hLvMountpoints);
            }
        }

        /* Right-click context menu on mountpoint ListView */
        if (nmh->idFrom == IDC_LV_MOUNTPOINTS && nmh->code == NM_RCLICK) {
            LvShowContextMenu(hwnd, state->hLvMountpoints);
        }

        return 0;
    }

    case WM_COMMAND: {
        state = GetAppState(hwnd);
        if (!state) break;

        int id = LOWORD(wParam);

        switch (id) {
        /* ── File menu ──────────────────────────────────────── */
        case IDM_FILE_EXIT:
            PostMessage(hwnd, WM_CLOSE, 0, 0);
            return 0;

        case IDM_FILE_LOAD_CONFIG:
        case IDC_BTN_LOAD_CONFIG:
            OnLoadConfig(hwnd, state);
            return 0;

        case IDM_FILE_SAVE_CONFIG:
        case IDC_BTN_SAVE_CONFIG:
            OnSaveConfig(hwnd, state);
            return 0;

        case IDM_FILE_GENERATE:
        case IDC_BTN_GENERATE:
            OnGenerateConfig(hwnd, state);
            return 0;

        /* ── Connection menu / buttons ──────────────────────── */
        case IDM_CONN_MOUNTPOINTS:
        case IDC_BTN_GET_MOUNTS:
            OnGetMountpoints(hwnd, state);
            return 0;

        case IDM_CONN_OPEN_STREAM:
        case IDC_BTN_OPEN_STREAM:
            OnOpenStream(hwnd, state);
            return 0;

        case IDM_CONN_CLOSE_STREAM:
        case IDC_BTN_CLOSE_STREAM:
            OnCloseStream(hwnd, state);
            return 0;

        /* ── Map picker ─────────────────────────────────────── */
        case IDC_BTN_MAP_PICK:
            OnMapPick(hwnd, state);
            return 0;

        case IDC_BTN_MAP_PASTE:
            OnMapPaste(hwnd, state);
            return 0;

        /* ── Help menu ──────────────────────────────────────── */
        case IDM_HELP_ABOUT:
            MessageBox(hwnd,
                "NTRIP-Analyser v0.1.0\n\n"
                "NTRIP RTCM 3.x Stream Analyser\n"
                "Author: Remko Welling, PE1MEW\n\n"
                "Licensed under Apache License 2.0\nwith Commons Clause.",
                "About NTRIP-Analyser",
                MB_OK | MB_ICONINFORMATION);
            return 0;

        case IDM_HELP_GITHUB:
            ShellExecute(NULL, "open",
                "https://github.com/pe1mew/NTRIP-Analyser",
                NULL, NULL, SW_SHOWNORMAL);
            return 0;

        default:
            break;
        }
        break;
    }

    case WM_TIMER: {
        state = GetAppState(hwnd);
        if (!state) return 0;

        if (wParam == IDT_LOG_PUMP) {
            LogPumpTimer(state);
        }

        if (wParam == IDT_STATUS_UPDATE && state->bWorkerRunning) {
            /* ── Compute data rate and update status bar ──── */
            double now = gui_get_time_seconds();
            double dt  = now - state->streamRateTime;
            LONG totalBytes = InterlockedCompareExchange(&state->streamBytes, 0, 0);

            if (dt > 0.5) {
                LONG delta = totalBytes - state->streamBytesLast;
                double rate = (double)delta / dt;
                state->streamBytesLast = totalBytes;
                state->streamRateTime  = now;

                /* Format: "Streaming ● 1.2 kB/s" or "Streaming ● 0 B/s" */
                char statusBuf[128];
                if (rate >= 1024.0)
                    snprintf(statusBuf, sizeof(statusBuf),
                             "Streaming  %.1f kB/s", rate / 1024.0);
                else
                    snprintf(statusBuf, sizeof(statusBuf),
                             "Streaming  %.0f B/s", rate);
                SendMessage(state->hStatusBar, SB_SETTEXT, 0, (LPARAM)statusBuf);
            }

            /* Total bytes in part 2 */
            char totalBuf[64];
            if (totalBytes >= 1048576)
                snprintf(totalBuf, sizeof(totalBuf), "%.1f MB received",
                         totalBytes / 1048576.0);
            else if (totalBytes >= 1024)
                snprintf(totalBuf, sizeof(totalBuf), "%.1f kB received",
                         totalBytes / 1024.0);
            else
                snprintf(totalBuf, sizeof(totalBuf), "%ld B received",
                         (long)totalBytes);
            SendMessage(state->hStatusBar, SB_SETTEXT, 2, (LPARAM)totalBuf);
        }
        return 0;
    }

    case WM_APP_STREAM_INFO: {
        state = GetAppState(hwnd);
        if (!state) break;

        /* Show detected stream format in status bar part 1 */
        LONG fmt = InterlockedCompareExchange(&state->streamFormat, 0, 0);
        const char *fmtStr;
        switch (fmt) {
        case 1:  fmtStr = "RTCM 3.x";           break;
        case 2:  fmtStr = "UBX";                 break;
        case 3:  fmtStr = "Septentrio SBF";      break;
        case 4:  fmtStr = "RAW Trimble RT27";    break;
        case 5:  fmtStr = "RAW Leica LB2";       break;
        case 6:  fmtStr = "Unknown";              break;
        default: fmtStr = "";                     break;
        }
        SendMessage(state->hStatusBar, SB_SETTEXT, 1, (LPARAM)fmtStr);
        return 0;
    }

    case WM_APP_MSG_RAW: {
        state = GetAppState(hwnd);
        if (!state) break;

        int msg_type = (int)wParam;
        RtcmRawMsg *raw = (RtcmRawMsg *)lParam;

        if (raw && msg_type > 0 && msg_type < GUI_MAX_MSG_TYPES
            && state->hDetailWnds[msg_type]) {
            /* Decode to string buffer on UI thread */
            RtcmStrBuf sb;
            rtcm_strbuf_init(&sb, 4096);
            rtcm_set_output_buffer(&sb);
            analyze_rtcm_message(raw->data, raw->length,
                                 false, &state->config);
            rtcm_set_output_buffer(NULL);

            /* Convert \n → \r\n for Win32 EDIT control, then send */
            if (sb.len > 0) {
                /* Count newlines to size the output buffer */
                int nlCount = 0;
                for (int i = 0; i < sb.len; i++)
                    if (sb.buf[i] == '\n') nlCount++;

                char *text = (char *)HeapAlloc(GetProcessHeap(),
                                               0, sb.len + nlCount + 1);
                if (text) {
                    int j = 0;
                    for (int i = 0; i < sb.len; i++) {
                        if (sb.buf[i] == '\n')
                            text[j++] = '\r';
                        text[j++] = sb.buf[i];
                    }
                    text[j] = '\0';
                    PostMessage(state->hDetailWnds[msg_type],
                                WM_USER + 1, 0, (LPARAM)text);
                }
            }
            rtcm_strbuf_free(&sb);
        }

        /* Always free the raw message */
        if (raw) HeapFree(GetProcessHeap(), 0, raw);
        return 0;
    }

    case WM_APP_DETAIL_CLOSED: {
        state = GetAppState(hwnd);
        if (!state) break;
        int msg_type = (int)wParam;
        if (msg_type > 0 && msg_type < GUI_MAX_MSG_TYPES)
            state->hDetailWnds[msg_type] = NULL;
        return 0;
    }

    case WM_APP_STREAM_DONE: {
        state = GetAppState(hwnd);
        if (state) OnStreamDone(hwnd, state);
        return 0;
    }

    case WM_APP_STAT_UPDATE: {
        state = GetAppState(hwnd);
        if (state) OnStatUpdate(state, (int)wParam, (int)lParam);
        return 0;
    }

    case WM_APP_SAT_UPDATE: {
        state = GetAppState(hwnd);
        if (state) OnSatUpdate(state);
        return 0;
    }

    case WM_APP_MOUNT_RESULT: {
        state = GetAppState(hwnd);
        if (!state) break;

        state->bWorkerRunning = FALSE;
        EnableWindow(state->hBtnCloseStream, FALSE);

        if (state->hWorkerThread) {
            CloseHandle(state->hWorkerThread);
            state->hWorkerThread = NULL;
        }

        char *mount_table = (char *)lParam;

        if (wParam == 0 && mount_table) {
            /* Success — parse and populate the ListView */
            GuiToConfig(state);  /* ensure latest lat/lon from GUI */
            ParseMountTable(mount_table, state->hLvMountpoints,
                            state->config.LATITUDE, state->config.LONGITUDE);

            int count = ListView_GetItemCount(state->hLvMountpoints);
            char logmsg[128];
            snprintf(logmsg, sizeof(logmsg),
                     "[INFO] Received %d mountpoint(s).\r\n", count);
            AppendLog(state->hEditLog, logmsg);
            SendMessage(state->hStatusBar, SB_SETTEXT, 0, (LPARAM)"Disconnected");

            snprintf(logmsg, sizeof(logmsg), "%d mountpoints", count);
            SendMessage(state->hStatusBar, SB_SETTEXT, 1, (LPARAM)logmsg);
        } else {
            /* Error */
            AppendLog(state->hEditLog,
                "[ERROR] Failed to retrieve mountpoint list. "
                "Check caster, port, and credentials.\r\n");
            SendMessage(state->hStatusBar, SB_SETTEXT, 0, (LPARAM)"Error");
        }

        free(mount_table);  /* safe even if NULL */
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}
