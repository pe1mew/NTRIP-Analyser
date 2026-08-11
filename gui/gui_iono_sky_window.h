/**
 * @file gui_iono_sky_window.h
 * @brief Polar sky view of ionospheric disturbance (ROTI).
 *
 * A separate window from the Sky Plot, deliberately: the Sky Plot's job
 * is satellites and signal strength, and its 3-px trail dots are too
 * small to read a colour from.  This view paints whole sky sectors, so
 * a disturbed patch of sky is legible at a glance.
 *
 * Two presentations, toggled with the space bar:
 *  - **Heatmap** (default): each az/el sector filled with the verdict
 *    colour of the most recent ROTI measured there.
 *  - **Tracks**: each satellite's 24 h trail drawn with every dot in the
 *    colour of the ROTI at that moment -- a timelapse of ionospheric
 *    structure in one image.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * @copyright Apache License 2.0 with Commons Clause (see LICENSE for details)
 */

#ifndef GUI_IONO_SKY_WINDOW_H
#define GUI_IONO_SKY_WINDOW_H

#include "gui_state.h"

/**
 * @brief Open the ROTI sky window, or bring it to the front if open.
 *
 * @param hInst  Module instance.
 * @param parent Owner window.
 * @param state  Application state; the window reads `skyState` (which
 *               carries ROTI per satellite and per trail point) on a 1 s
 *               timer.
 * @return The window handle, or NULL on failure.
 */
HWND IonoSkyWindowOpen(HINSTANCE hInst, HWND parent, AppState *state);

#endif /* GUI_IONO_SKY_WINDOW_H */
