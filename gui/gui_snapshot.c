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
#include <stdio.h>

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
