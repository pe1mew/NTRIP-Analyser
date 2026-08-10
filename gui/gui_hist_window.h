/**
 * @file gui_hist_window.h
 * @brief Floating session-history window: metrics over time.
 *
 * Six stacked strip charts sharing one time axis -- throughput, message
 * rate, CRC errors, satellites tracked, mean C/N0 and reference-point
 * drift.  The Msg Stats min/max/avg columns hide the faults that matter,
 * because a 45-second dropout and a steady stream can average alike; the
 * same numbers plotted over time make gaps, bursts and reconnects
 * self-evident.
 *
 * Opened from View -> Session History.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#ifndef GUI_HIST_WINDOW_H
#define GUI_HIST_WINDOW_H

#include "gui_state.h"

#define HIST_WINDOW_CLASS "NtripHistWindowClass"

/** @brief Register the history-window class with the OS.  Idempotent. */
BOOL RegisterHistWindowClass(HINSTANCE hInst);

/**
 * @brief Create a floating session-history window owned by the main window.
 *
 * @param hInst  Application instance handle.
 * @param hOwner Owner window (typically the main window).
 * @param state  Pointer to AppState.
 * @return Window handle on success, NULL on failure.
 */
HWND CreateHistWindow(HINSTANCE hInst, HWND hOwner, AppState *state);

#endif /* GUI_HIST_WINDOW_H */
