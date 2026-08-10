/**
 * @file gui_signal_window.h
 * @brief Floating signal-quality (C/N0) window.
 *
 * Two stacked views over the same per-SV data held in AppState.skyState:
 *   - Signal bars:  one bar per currently-tracked satellite, height =
 *                   C/N0 this epoch, coloured by constellation.  Answers
 *                   "what is the base receiver seeing right now".
 *   - C/N0 vs elevation:  every accumulated track sample plotted as
 *                   elevation against C/N0, with a per-constellation
 *                   binned mean overlaid.  Answers "is this antenna and
 *                   site any good" -- a clean install rises monotonically
 *                   from horizon to zenith, while obstructions and
 *                   multipath show as a dip at particular elevations.
 *
 * Opened from View -> Signal Quality.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#ifndef GUI_SIGNAL_WINDOW_H
#define GUI_SIGNAL_WINDOW_H

#include "gui_state.h"

#define SIGNAL_WINDOW_CLASS "NtripSignalWindowClass"

/** @brief Register the signal-window class with the OS.  Idempotent. */
BOOL RegisterSignalWindowClass(HINSTANCE hInst);

/**
 * @brief Create a floating signal-quality window owned by the main window.
 *
 * Created visible.  The AppState pointer is stored on the window so
 * WM_DESTROY can null out state->hSignalWnd and remember the placement.
 *
 * @param hInst  Application instance handle.
 * @param hOwner Owner window (typically the main window).
 * @param state  Pointer to AppState.
 * @return Window handle on success, NULL on failure.
 */
HWND CreateSignalWindow(HINSTANCE hInst, HWND hOwner, AppState *state);

#endif /* GUI_SIGNAL_WINDOW_H */
