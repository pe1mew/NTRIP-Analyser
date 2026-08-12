/**
 * @file gui_check_window.h
 * @brief Floating Station Check window: the acceptance test, on screen.
 *
 * The same engine the CLI's `--check` and the Android station mode use
 * (`core/kpi.c`, `core/vrs_check.c`), run over the stream the GUI
 * already has open.  A run is *bounded*: the user starts it, it watches
 * for about ninety seconds, and it ends with a verdict that stops
 * moving.  That last part is what makes the result quotable in a
 * handover -- a reading that keeps changing is not a sign-off.
 *
 * Opened from View -> Station Check.
 *
 * The run state lives in @ref AppState rather than in this window: a
 * ninety-second test must not be abandoned because its window was
 * closed, and the sustain clocks inside @ref KpiRun cannot be rebuilt
 * from a repaint.  See design/gui-design.md §13.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#ifndef GUI_CHECK_WINDOW_H
#define GUI_CHECK_WINDOW_H

/* Default window size; also used by View > Reset window layout. */
#define CHECK_WIN_DEF_W   680
#define CHECK_WIN_DEF_H   520

#include "gui_state.h"

#define CHECK_WINDOW_CLASS "NtripStationCheckClass"

/** Create the floating Station Check window.  Returns the HWND or NULL. */
HWND CreateCheckWindow(HINSTANCE hInst, HWND hOwner, AppState *state);

/** Register the window class (idempotent). */
BOOL RegisterCheckWindowClass(HINSTANCE hInst);

/**
 * @brief The clock the run is timed on: seconds, monotonic.
 *
 * One source for every caller, so the sustain clock cannot be advanced
 * against a different clock than it was started on.
 */
double CheckNow(void);

/**
 * @brief Begin a bounded run against the open stream.
 *
 * Safe to call while one is in progress: it restarts.  VRS assertions
 * begin too when the station has been classified as a VRS.
 */
void CheckStart(AppState *state);

/** @brief Abandon a run in progress, leaving whatever it had shown. */
void CheckStop(AppState *state);

/**
 * @brief Advance the run with a fresh snapshot.
 *
 * Called from the session's statistics event -- the data drives the
 * check, not a window timer, so the KPI clock steps exactly once per
 * snapshot rather than sometimes twice and sometimes not at all.
 *
 * Runs on the worker thread; posts to the UI thread to repaint.
 */
void CheckOnStats(AppState *state, const NsStatsSnapshot *s);

/**
 * @brief Tell a running VRS check that a GGA has just gone out.
 *
 * Assertions A1 and A2 are timed from the first one, and A3 needs the
 * position it carried.
 */
void CheckNoteGga(AppState *state, double lat, double lon);

#endif /* GUI_CHECK_WINDOW_H */
