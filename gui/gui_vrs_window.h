/**
 * @file gui_vrs_window.h
 * @brief Floating VRS / nearby-service monitor window.
 *
 * Visualises the rover GGA position (currently being sent to the
 * caster) versus the broadcast 1005/1006 ARP returned by the network,
 * so the user can:
 *   - see the rover-to-virtual-station distance live (top readout),
 *   - confirm direction-and-distance on a polar plot,
 *   - watch the distance over a rolling 5-minute strip chart,
 *   - see hand-overs / nearest-station swaps as ARP dots accumulate.
 *
 * Opened from View -> VRS Monitor.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#ifndef GUI_VRS_WINDOW_H
#define GUI_VRS_WINDOW_H

#include "gui_state.h"

#define VRS_WINDOW_CLASS "NtripVrsMonitorClass"

/** Create the floating VRS Monitor window.  Returns the HWND or NULL. */
HWND CreateVrsWindow(HINSTANCE hInst, HWND hOwner, AppState *state);

/** Register the window class (idempotent). */
BOOL RegisterVrsWindowClass(HINSTANCE hInst);

#endif /* GUI_VRS_WINDOW_H */
