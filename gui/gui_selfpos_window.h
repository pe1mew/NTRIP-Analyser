/**
 * @file gui_selfpos_window.h
 * @brief Floating Self-position window: where the station puts itself.
 *
 * The station broadcasts a reference position in 1005/1006. This solves
 * the position from the station's **own observations** and shows the
 * difference — the same engine as the CLI's `--report` block and the
 * daemon's `selfpos_*` keys (`src/core/spp.c`, `src/core/station_report.c`),
 * over the stream the GUI already has open.
 *
 * Three things about it are deliberate, and each is a decision recorded
 * in `design/work-items/station-self-position.md`:
 *
 * - **There is no verdict.** Not `STABLE`, not `STATION OK`, not a
 *   coloured row: figures only. A verdict needs a threshold, and what
 *   counts as abnormal scatter for a station's own position is not yet
 *   known from any measurement of a real station. Inventing a number to
 *   fill the column would be worse than leaving it empty, so the column
 *   does not exist.
 * - **One epoch means nothing here.** The offset is a
 *   broadcast-ephemeris solution's own error — metre-level by
 *   construction, and it says nothing about the antenna. The scatter
 *   over a run is the half that measures something, which is why the
 *   window is mostly a plot of the run rather than a reading.
 * - **A single-frequency station says *not computable*, with the
 *   reason.** An empty window with no explanation is the failure this
 *   project keeps writing down; the header carries `SppStatus`'s own
 *   sentence instead.
 *
 * Opened from View -> Self-position.
 *
 * Where the state lives: the figures come from @ref AppState::reportOut,
 * which `core/station_report.c` accumulates over the same window of
 * stream time the Stability window shows — so the two cannot disagree,
 * and restarting that window restarts this one. The only thing this
 * window owns is @ref AppState::selfpos, a ring of points kept for
 * **drawing**: a plot cannot be rebuilt from a repaint, and nothing
 * derived from it is published as a measurement.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#ifndef GUI_SELFPOS_WINDOW_H
#define GUI_SELFPOS_WINDOW_H

/* Default window size; also used by View > Reset window layout.
 *
 * Wide enough that the plot and three measured columns both fit without
 * a horizontal scrollbar: the first default was 860 and the figures
 * opened under one, with the last column's heading cut to
 * "What it mea...". */
#define SELFPOS_WIN_DEF_W  1000
#define SELFPOS_WIN_DEF_H  620

#include "gui_state.h"

#define SELFPOS_WINDOW_CLASS "NtripSelfPosClass"

/** Create the floating Self-position window.  Returns the HWND or NULL. */
HWND CreateSelfPosWindow(HINSTANCE hInst, HWND hOwner, AppState *state);

/** Register the window class (idempotent). */
BOOL RegisterSelfPosWindowClass(HINSTANCE hInst);

/**
 * @brief Empty the plot.
 *
 * Called wherever the tier-2 window of evidence restarts — a stream
 * opening, a replay starting, the Stability window's Restart button —
 * so the plot and the figures beside it always describe the same window.
 * Two spans in one window would be a plot of one hour beside a mean of
 * another, which is the kind of disagreement nobody can read.
 */
void SelfPosReset(AppState *state);

/**
 * @brief Add this epoch's solution to the plot.
 *
 * Called from the session's statistics event, beside `ReportOnStats()`,
 * for the same reason: the data drives the plot rather than a timer that
 * knows nothing about epochs. Stamped by the snapshot's own
 * @ref NsStatsSnapshot::stream_time_s, so replaying a capture plots the
 * span the capture holds rather than the seconds the disk took, and a
 * repeated snapshot is not plotted twice.
 *
 * An epoch that did not solve adds no point. It is not a point at the
 * origin, which is what a station sitting exactly on its declared
 * coordinates would look like.
 *
 * Runs on the worker thread; posts to the UI thread to repaint.
 */
void SelfPosOnStats(AppState *state, const NsStatsSnapshot *s);

#endif /* GUI_SELFPOS_WINDOW_H */
