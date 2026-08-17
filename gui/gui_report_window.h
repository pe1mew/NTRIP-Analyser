/**
 * @file gui_report_window.h
 * @brief Floating Stability window: tier 2, on screen.
 *
 * The Station Check (gui_check_window.h) asks whether this station is
 * fit **now** and answers in ninety seconds.  This asks the other
 * question — has it *been* fit, and is it staying that way — which no
 * ninety-second window can reach at any price.  Same engine as the
 * CLI's `--report` and the daemon's `<mountpoint>.report.json`
 * (`core/station_report.c`), over the stream the GUI already has open.
 *
 * Three differences from the check follow from the question, and each
 * is deliberate:
 *
 * - **It is not a run.** There is nothing to start: it accumulates for
 *   as long as the stream is open, and says `INSUFFICIENT EVIDENCE`
 *   until it has ten minutes to judge on. During commissioning that is
 *   the useful shape — you connect, you work, and the verdict is there
 *   when you next look at it.
 * - **It never borrows the check's words.** `STABLE` / `DEGRADED` /
 *   `UNSTABLE`, never `STATION OK`. A station can be fit this second
 *   and have been unstable all week; both are true, and a user seeing
 *   one vocabulary twice would conclude one of them is broken.
 * - **The window is stream time**, so replaying a capture reports the
 *   window the capture holds rather than the seconds the disk took.
 *   Over a replay availability reads `n/a`, because a file holds no
 *   arrival times and never drops.
 *
 * Opened from View -> Stability.
 *
 * The accumulator lives in @ref AppState rather than in this window: an
 * hour of evidence must not be thrown away because a window was closed,
 * and @ref SrState cannot be rebuilt from a repaint. Same rule as the
 * check, for the same reason.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#ifndef GUI_REPORT_WINDOW_H
#define GUI_REPORT_WINDOW_H

/* Default window size; also used by View > Reset window layout. */
#define REPORT_WIN_DEF_W   700
#define REPORT_WIN_DEF_H   400

#include "gui_state.h"

#define REPORT_WINDOW_CLASS "NtripStabilityClass"

/** Create the floating Stability window.  Returns the HWND or NULL. */
HWND CreateReportWindow(HINSTANCE hInst, HWND hOwner, AppState *state);

/** Register the window class (idempotent). */
BOOL RegisterReportWindowClass(HINSTANCE hInst);

/**
 * @brief Begin a fresh window of evidence.
 *
 * Called when a stream opens, and by the window's Restart button —
 * which exists for commissioning: having changed the antenna, an
 * installer wants the next hour judged on its own, not averaged with
 * the hour that prompted the change.
 *
 * @param from_capture true when the source is a replayed `.rtcm3`,
 *        which makes the live-only metrics unavailable rather than zero.
 */
void ReportReset(AppState *state, BOOL from_capture);

/**
 * @brief Add a snapshot to the window.
 *
 * Called from the session's statistics event, as the check is, so the
 * data drives the report rather than a window timer.  Samples are
 * stamped and paced by @ref NsStatsSnapshot::stream_time_s: a stream
 * that stops sending stops advancing its own window instead of banking
 * the silence as evidence.
 *
 * Runs on the worker thread; posts to the UI thread to repaint.
 */
void ReportOnStats(AppState *state, const NsStatsSnapshot *s);

#endif /* GUI_REPORT_WINDOW_H */
