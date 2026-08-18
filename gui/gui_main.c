/**
 * @file gui_main.c
 * @brief WinMain entry point, window class registration, and message loop
 *        for the NTRIP-Analyser Windows GUI.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "resource.h"
#include "gui_state.h"

#include <stdlib.h>

/**
 * @brief Application entry point (GUI).
 */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;

    /* ── Initialize Common Controls (ListView, Tab, StatusBar) ── */
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(icex);
    icex.dwICC  = ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icex);

    /* ── Initialize Winsock ───────────────────────────────────── */
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        MessageBox(NULL, "Winsock initialization failed.", APP_TITLE,
                   MB_ICONERROR | MB_OK);
        return 1;
    }

    /* ── Allocate application state ───────────────────────────── */
    AppState *state = (AppState*)calloc(1, sizeof(AppState));
    if (!state) {
        MessageBox(NULL, "Out of memory.", APP_TITLE, MB_ICONERROR | MB_OK);
        WSACleanup();
        return 1;
    }

    /* Critical section guarding the RTCM-dump FILE* between the UI thread
     * (open/close) and the worker thread (per-frame fwrite). */
    InitializeCriticalSection(&state->csRtcmDump);
    state->csRtcmDumpInit = TRUE;

    /* ── Register window class ────────────────────────────────── */
    WNDCLASSEX wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = MainWndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = hInstance;
    wc.hIcon         = GuiLoadAppIcon(FALSE);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszMenuName  = MAKEINTRESOURCE(IDR_MAIN_MENU);
    wc.lpszClassName = APP_CLASS_NAME;
    wc.hIconSm      = GuiLoadAppIcon(TRUE);

    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, "Window class registration failed.", APP_TITLE,
                   MB_ICONERROR | MB_OK);
        free(state);
        WSACleanup();
        return 1;
    }

    /* ── Create main window ───────────────────────────────────── */
    HWND hwnd = CreateWindowEx(
        0,
        APP_CLASS_NAME,
        APP_TITLE,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        APP_INIT_WIDTH, APP_INIT_HEIGHT,
        NULL,
        NULL,
        hInstance,
        state   /* passed to WM_CREATE via CREATESTRUCT.lpCreateParams */
    );

    if (!hwnd) {
        MessageBox(NULL, "Window creation failed.", APP_TITLE,
                   MB_ICONERROR | MB_OK);
        free(state);
        WSACleanup();
        return 1;
    }

    state->hMain = hwnd;
    /* Before any window can judge anything: built-in values, overlaid by
     * whichever policy was loaded last time. */
    GuiThresholdsInit(state);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    /* ── Message loop ─────────────────────────────────────────── */
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        /* Tab between the fields, arrow keys within a group, Enter and
         * Escape as commands: all of it is the dialog manager's work,
         * and none of it happens in a plain window unless the loop
         * offers it the message first.  The controls have carried
         * WS_TABSTOP since they were written; only this was missing.
         *
         * Messages belonging to the detail, sky and report windows pass
         * through untouched -- they are top-level windows of their own,
         * not children of this one, so IsDialogMessage declines them. */
        if (IsDialogMessage(hwnd, &msg)) continue;

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    /* ── Cleanup ──────────────────────────────────────────────────────
     * No capture file to close here any more: the session owns it and
     * the worker closes it as it leaves, which has already happened by
     * the time the message loop ends. */
    if (state->csRtcmDumpInit)
        DeleteCriticalSection(&state->csRtcmDump);
    free(state);
    WSACleanup();

    return (int)msg.wParam;
}
