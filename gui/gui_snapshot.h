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

/**
 * @brief Prompt for a filename and save @p hwnd's client area as a PNG.
 *
 * Shared by every floating chart window so they all offer the same
 * behaviour: a timestamped default name, an overwrite prompt, and a line
 * in the log recording where the image went.
 *
 * @param hwnd        Window to capture.
 * @param hLog        Log EDIT control to report into; may be NULL.
 * @param dialogTitle Title for the Save dialog, e.g. "Save Sky Plot as PNG".
 * @param suffix      Filename suffix after the timestamp, e.g. "SignalQuality";
 *                    the default name is "YYYYMMDDHHmmss_<suffix>.png".
 * @param logLabel    What to call the image in the log, e.g. "Signal Quality".
 * @return TRUE if an image was written; FALSE on cancel or failure.
 */
BOOL SaveWindowPngWithPrompt(HWND hwnd, HWND hLog,
                             const char *dialogTitle,
                             const char *suffix,
                             const char *logLabel);

/**
 * @brief The same flow, for a window that needs more than its pixels.
 *
 * @param with_children Render the window with PrintWindow so its child
 *        controls are in the image -- a list of figures, say -- and a
 *        part covered by another window is drawn rather than missing.
 *        @ref SaveWindowPngWithPrompt is this with FALSE, which suits
 *        the painted chart windows that have no children.
 * @param path_out      Receives the path the user chose, so the caller
 *        can write a companion file beside the image; empty on cancel
 *        or failure. May be NULL.
 * @param path_cap      Size of @p path_out.
 * @return TRUE if an image was written.
 */
BOOL SaveWindowPngWithPromptEx(HWND hwnd, HWND hLog,
                               const char *dialogTitle,
                               const char *suffix,
                               const char *logLabel,
                               BOOL with_children,
                               char *path_out, size_t path_cap);

/** @brief Shutdown GDI+ if it was initialised.  Optional; call on app exit. */
void gui_snapshot_shutdown(void);

#endif /* GUI_SNAPSHOT_H */
