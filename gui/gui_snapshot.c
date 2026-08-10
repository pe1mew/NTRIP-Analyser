/**
 * @file gui_snapshot.c
 * @brief Window-to-PNG capture via GDI+ flat C API.
 *
 * MinGW's <gdiplus.h> requires C++; we sidestep that by forward-declaring
 * just the half-dozen GDI+ flat functions we need from gdiplus.dll.  Link
 * line gets `-lgdiplus`.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "gui_snapshot.h"

#include <objbase.h>    /* GUID / CLSID */
#include <commdlg.h>    /* GetSaveFileNameA */
#include <stdio.h>
#include <time.h>

/* ── GDI+ flat C API (gdiplus.dll exports) ───────────────────────────── */

typedef int          GpStatus;
#define GDIP_OK      0

typedef struct {
    UINT32   GdiplusVersion;
    void    *DebugEventCallback;
    BOOL     SuppressBackgroundThread;
    BOOL     SuppressExternalCodecs;
} GdiplusStartupInput;

typedef void GpImage;
typedef void GpBitmap;

WINAPI GpStatus GdiplusStartup(ULONG_PTR *token,
                               const GdiplusStartupInput *input,
                               void *output);
WINAPI void     GdiplusShutdown(ULONG_PTR token);

WINAPI GpStatus GdipCreateBitmapFromHBITMAP(HBITMAP hbm, HPALETTE hpal,
                                            GpBitmap **bitmap);
WINAPI GpStatus GdipSaveImageToFile(GpImage *image, const WCHAR *filename,
                                    const CLSID *clsidEncoder,
                                    const void *encoderParams);
WINAPI GpStatus GdipDisposeImage(GpImage *image);

/* PNG encoder CLSID, per Microsoft Image Format CLSIDs documentation:
 * {557CF406-1A04-11D3-9A73-0000F81EF32E} */
static const CLSID PNG_ENCODER_CLSID = {
    0x557CF406, 0x1A04, 0x11D3,
    { 0x9A, 0x73, 0x00, 0x00, 0xF8, 0x1E, 0xF3, 0x2E }
};

static ULONG_PTR g_gdiplus_token = 0;
static BOOL      g_gdiplus_initialised = FALSE;

static BOOL ensure_gdiplus(void)
{
    if (g_gdiplus_initialised) return TRUE;
    GdiplusStartupInput in = { 1, NULL, FALSE, FALSE };
    if (GdiplusStartup(&g_gdiplus_token, &in, NULL) != GDIP_OK)
        return FALSE;
    g_gdiplus_initialised = TRUE;
    return TRUE;
}

void gui_snapshot_shutdown(void)
{
    if (g_gdiplus_initialised) {
        GdiplusShutdown(g_gdiplus_token);
        g_gdiplus_initialised = FALSE;
        g_gdiplus_token = 0;
    }
}

BOOL save_window_as_png(HWND hwnd, const char *filename)
{
    if (!hwnd || !filename) return FALSE;
    if (!ensure_gdiplus()) return FALSE;

    /* ── Capture the window's client area via BitBlt ────────────── */
    RECT rc;
    if (!GetClientRect(hwnd, &rc)) return FALSE;
    int w = rc.right  - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return FALSE;

    HDC hdcSrc = GetDC(hwnd);
    if (!hdcSrc) return FALSE;

    HDC     hdcMem = CreateCompatibleDC(hdcSrc);
    HBITMAP hbm    = CreateCompatibleBitmap(hdcSrc, w, h);
    if (!hdcMem || !hbm) {
        if (hbm)    DeleteObject(hbm);
        if (hdcMem) DeleteDC(hdcMem);
        ReleaseDC(hwnd, hdcSrc);
        return FALSE;
    }

    HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbm);
    BOOL    blt_ok = BitBlt(hdcMem, 0, 0, w, h, hdcSrc, 0, 0, SRCCOPY);
    SelectObject(hdcMem, hbmOld);
    DeleteDC(hdcMem);
    ReleaseDC(hwnd, hdcSrc);

    if (!blt_ok) {
        DeleteObject(hbm);
        return FALSE;
    }

    /* ── Convert filename to UTF-16 for GDI+ ─────────────────────── */
    WCHAR wfilename[1024];
    int   wlen = MultiByteToWideChar(CP_ACP, 0, filename, -1,
                                     wfilename,
                                     (int)(sizeof(wfilename) / sizeof(WCHAR)));
    if (wlen <= 0) {
        DeleteObject(hbm);
        return FALSE;
    }

    /* ── Wrap HBITMAP, save as PNG ──────────────────────────────── */
    GpBitmap *bmp = NULL;
    GpStatus  status = GdipCreateBitmapFromHBITMAP(hbm, NULL, &bmp);
    if (status != GDIP_OK || !bmp) {
        DeleteObject(hbm);
        return FALSE;
    }

    status = GdipSaveImageToFile((GpImage *)bmp, wfilename,
                                 &PNG_ENCODER_CLSID, NULL);

    GdipDisposeImage((GpImage *)bmp);
    DeleteObject(hbm);

    return (status == GDIP_OK);
}

/* ── Shared "save this window as PNG" flow ───────────────────────────────
 * Every floating chart window offers the same thing, so the dialog, the
 * timestamped default name and the log line live here rather than being
 * re-implemented per window. */

BOOL SaveWindowPngWithPrompt(HWND hwnd, HWND hLog,
                             const char *dialogTitle,
                             const char *suffix,
                             const char *logLabel)
{
    if (!hwnd) return FALSE;

    /* Default name "YYYYMMDDHHmmss_<suffix>.png" -- sorting by name then
     * sorts by capture time, which is what you want in a folder full of
     * these. */
    char filename[MAX_PATH];
    {
        time_t now_t = time(NULL);
        struct tm *lt = localtime(&now_t);
        char ts[16] = "00000000000000";
        if (lt) strftime(ts, sizeof(ts), "%Y%m%d%H%M%S", lt);
        snprintf(filename, sizeof(filename), "%s_%s.png",
                 ts, suffix ? suffix : "snapshot");
    }

    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter = "PNG Image (*.png)\0*.png\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile   = filename;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = dialogTitle;
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = "png";
    if (!GetSaveFileNameA(&ofn))
        return FALSE;   /* user cancelled */

    BOOL ok = save_window_as_png(hwnd, filename);

    if (hLog) {
        char msg[MAX_PATH + 96];
        if (ok) snprintf(msg, sizeof(msg), "[INFO] %s saved to %s\r\n",
                         logLabel ? logLabel : "Image", filename);
        else    snprintf(msg, sizeof(msg), "[ERROR] Failed to save %s to %s\r\n",
                         logLabel ? logLabel : "image", filename);
        int len = GetWindowTextLength(hLog);
        SendMessage(hLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
        SendMessage(hLog, EM_REPLACESEL, FALSE, (LPARAM)msg);
    }
    return ok;
}
