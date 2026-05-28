/**
 * @file gui_sv_detail.h
 * @brief Live per-satellite detail window opened from the sky plot.
 *
 * Each window is bound to a (gnss_id, prn) pair and refreshes its
 * contents from AppState every second.  Multiple SVs can have detail
 * windows open simultaneously, deduplicated via AppState->hSvDetailWnds.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#ifndef GUI_SV_DETAIL_H
#define GUI_SV_DETAIL_H

#include "gui_state.h"

/**
 * @brief Open (or focus) a detail window for the given satellite.
 *
 * If a window is already open for (g, prn), brings it to the front
 * instead of creating a duplicate.
 *
 * @param hInst   Application instance.
 * @param hOwner  Owner window (typically the sky window).
 * @param state   AppState; the new window reads SkySat state on a timer.
 * @param g       GNSS ID (1=GPS, 2=GLONASS, 3=Galileo, 4=QZSS, 5=BeiDou).
 * @param prn     PRN within that constellation, 1-based.
 * @return Window handle on success, NULL on failure.
 */
HWND ShowSkySvDetailWindow(HINSTANCE hInst, HWND hOwner, AppState *state,
                           int g, int prn);

#endif /* GUI_SV_DETAIL_H */
