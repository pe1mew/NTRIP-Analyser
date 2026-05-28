/**
 * @file gui_snapshot.h
 * @brief Window-to-PNG snapshot helper for NTRIP-Analyser GUI.
 *
 * Uses the GDI+ flat C API (gdiplus.dll exports) to encode an HBITMAP
 * as PNG without needing C++.  Initialised lazily on first save.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#ifndef GUI_SNAPSHOT_H
#define GUI_SNAPSHOT_H

#define _WIN32_WINNT 0x0601
#include <windows.h>

/**
 * @brief Capture the client area of @p hwnd and save it as a PNG.
 *
 * @param hwnd     The window whose client area should be captured.
 * @param filename Output path (UTF-8 / ANSI; converted to UTF-16 inside).
 * @return TRUE on success, FALSE on any failure.
 */
BOOL save_window_as_png(HWND hwnd, const char *filename);

/** @brief Shutdown GDI+ if it was initialised.  Optional; call on app exit. */
void gui_snapshot_shutdown(void);

#endif /* GUI_SNAPSHOT_H */
