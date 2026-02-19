/**
 * @file gui_detail.c
 * @brief RTCM message detail window — shows live decoded message content.
 *
 * Each detail window displays the fully decoded output of one RTCM
 * message type. Multiple windows can be open simultaneously.
 * The main window forwards decoded text via WM_USER+1 whenever a
 * new raw frame arrives for that message type.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "resource.h"
#include "gui_state.h"

#include <stdio.h>

/* ── Private message for the detail window ─────────────────── */
#define WM_DETAIL_UPDATE  (WM_USER + 1)   /* lParam = heap-allocated char* */

static BOOL g_detailClassRegistered = FALSE;

/* ── Detail window procedure ──────────────────────────────── */

static LRESULT CALLBACK DetailWndProc(HWND hwnd, UINT msg,
                                       WPARAM wParam, LPARAM lParam)
{
    switch (msg) {

    case WM_CREATE: {
        CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
        int msg_type = (int)(intptr_t)cs->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)msg_type);

        /* Create a read-only multiline EDIT control (fills the window) */
        HWND hEdit = CreateWindowEx(
            WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY,
            0, 0, 100, 100, hwnd,
            (HMENU)(intptr_t)IDC_DETAIL_EDIT,
            (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);

        /* Allow large text (256 KB) */
        SendMessage(hEdit, EM_SETLIMITTEXT, 0x40000, 0);

        /* Apply Consolas monospace font */
        HFONT hMono = CreateFont(
            14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
        if (hMono)
            SendMessage(hEdit, WM_SETFONT, (WPARAM)hMono, TRUE);

        return 0;
    }

    case WM_SIZE: {
        HWND hEdit = GetDlgItem(hwnd, IDC_DETAIL_EDIT);
        if (hEdit)
            MoveWindow(hEdit, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
        return 0;
    }

    case WM_DETAIL_UPDATE: {
        /* lParam = heap-allocated char* with decoded text (we free it) */
        char *text = (char *)lParam;
        if (text) {
            HWND hEdit = GetDlgItem(hwnd, IDC_DETAIL_EDIT);
            if (hEdit)
                SetWindowText(hEdit, text);
            HeapFree(GetProcessHeap(), 0, text);
        }
        return 0;
    }

    case WM_CLOSE: {
        /* Notify parent to clear the slot in hDetailWnds[] */
        int msg_type = (int)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        HWND hParent = GetParent(hwnd);
        if (hParent)
            PostMessage(hParent, WM_APP_DETAIL_CLOSED,
                        (WPARAM)msg_type, 0);
        DestroyWindow(hwnd);
        return 0;
    }

    case WM_DESTROY: {
        /* Clean up the Consolas font */
        HWND hEdit = GetDlgItem(hwnd, IDC_DETAIL_EDIT);
        if (hEdit) {
            HFONT hFont = (HFONT)SendMessage(hEdit, WM_GETFONT, 0, 0);
            if (hFont) DeleteObject(hFont);
        }
        return 0;
    }
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* ── Register the detail window class (once) ─────────────── */

static void EnsureDetailClassRegistered(HINSTANCE hInst)
{
    if (g_detailClassRegistered) return;

    WNDCLASSEX wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = DetailWndProc;
    wc.hInstance      = hInst;
    wc.hIcon          = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground  = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName  = DETAIL_CLASS_NAME;
    wc.hIconSm        = LoadIcon(NULL, IDI_APPLICATION);

    RegisterClassEx(&wc);
    g_detailClassRegistered = TRUE;
}

/* ── Create a detail window for the given message type ────── */

HWND CreateDetailWindow(HINSTANCE hInst, HWND hParent, int msg_type)
{
    EnsureDetailClassRegistered(hInst);

    /* Build window title: "RTCM 1077 - MSM7 GPS" */
    const char *desc = RtcmMsgDescription(msg_type);
    char title[128];
    if (desc && desc[0])
        snprintf(title, sizeof(title), "RTCM %d - %s", msg_type, desc);
    else
        snprintf(title, sizeof(title), "RTCM %d", msg_type);

    HWND hwnd = CreateWindowEx(
        0, DETAIL_CLASS_NAME, title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        600, 500,
        hParent, NULL, hInst,
        (LPVOID)(intptr_t)msg_type);

    if (hwnd) {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
    }
    return hwnd;
}
