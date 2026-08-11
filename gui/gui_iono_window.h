/**
 * @file gui_iono_window.h
 * @brief Floating Ionosphere window: per-satellite ROTI table.
 *
 * The instrument view of the ionospheric monitor (core/iono.h): one row
 * per dual-frequency satellite with its signal pair, ROTI, relative
 * slant TEC, arc length and slip count, refreshed once a second from
 * `AppState.ionoView`.  The verdict and thresholds are in the header
 * line, so a screenshot of this window is self-describing.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * @copyright Apache License 2.0 with Commons Clause (see LICENSE for details)
 */

#ifndef GUI_IONO_WINDOW_H
#define GUI_IONO_WINDOW_H

#include "gui_state.h"

/**
 * @brief Open the Ionosphere window, or bring it to the front if open.
 *
 * @param hInst  Module instance.
 * @param parent Owner window (the main window).
 * @param state  Application state; the window reads `ionoView` and
 *               `lastStats` on a 1 s timer.
 * @return The window handle, or NULL on failure.
 */
HWND IonoWindowOpen(HINSTANCE hInst, HWND parent, AppState *state);

#endif /* GUI_IONO_WINDOW_H */
