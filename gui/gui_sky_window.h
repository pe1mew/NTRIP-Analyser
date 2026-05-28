/**
 * @file gui_sky_window.h
 * @brief Floating polar sky-visibility plot window for NTRIP-Analyser GUI.
 *
 * Toggled by the "View → Sky Plot..." menu item.  Phase 1 draws an empty
 * compass rose and elevation rings; later phases populate it with satellite
 * markers from cached RTCM 1019/1045/1046 ephemerides.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#ifndef GUI_SKY_WINDOW_H
#define GUI_SKY_WINDOW_H

#include "gui_state.h"

#define SKY_WINDOW_CLASS "NtripSkyWindowClass"

/**
 * @brief Register the sky-window class with the OS.  Idempotent.
 *
 * @param hInst Application instance handle.
 * @return TRUE on success.
 */
BOOL RegisterSkyWindowClass(HINSTANCE hInst);

/**
 * @brief Create a floating sky-plot window owned by the main window.
 *
 * Window is created visible.  AppState pointer is stored on the window so
 * the sky window's WM_DESTROY can null out state->hSkyWnd.
 *
 * @param hInst  Application instance handle.
 * @param hOwner Owner window (typically the main window).
 * @param state  Pointer to AppState.
 * @return Window handle on success, NULL on failure.
 */
HWND CreateSkyWindow(HINSTANCE hInst, HWND hOwner, AppState *state);

/**
 * @brief Prompt for a PNG filename and save the sky window's snapshot.
 *
 * Used by both the sky window's 'S' key and the main window's
 * "File -> Save Sky Plot..." menu item.  Returns TRUE on success.
 *
 * @param hSky  The sky window to capture.
 * @param state AppState for log routing (may be NULL to skip logging).
 */
BOOL SkySavePngWithPrompt(HWND hSky, AppState *state);

#endif /* GUI_SKY_WINDOW_H */
