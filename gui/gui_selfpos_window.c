/**
 * @file gui_selfpos_window.c
 * @brief Self-position window: where the station's own data puts it.
 *
 * See gui_selfpos_window.h for what this is and why it carries no
 * verdict, and design/work-items/station-self-position.md for the
 * measurement itself.
 *
 * Threading: the ring fills from the session's statistics event, which
 * arrives on the worker thread, and is drawn on the UI thread -- the
 * arrangement `AppState::lastStats`, the station check and the stability
 * report already live under. A point is written whole before `count` is
 * raised, so the painter never reads a slot that holds nothing; the
 * worst case is a repaint one epoch behind, or, at the moment the ring
 * wraps, one dot drawn from a point being overwritten. Neither survives
 * the next repaint, and no figure is derived from the ring.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "gui_selfpos_window.h"
#include "gui_snapshot.h"   /* the shared Save-as-PNG flow */
#include "core/spp.h"       /* SppStatus, and its sentence for a failure */
#include "resource.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Height of the painted header above the plot and the figures. */
#define SP_HEADER_H   86
/* Height of the painted caption strip under the plot. */
/* Two lines of caption: one of prose, one of legend. The legend gets a
 * row of its own rather than being run on to the end of the first and
 * cut off the bottom of the strip, which is where it spent its first
 * two days. */
#define SP_CAPTION_LINE 17
#define SP_CAPTION_H    (2 * SP_CAPTION_LINE + 4)
#define SP_PAD        10

#define SP_BG         RGB(255, 255, 255)
#define SP_PLOT_BG    RGB(252, 252, 252)
#define SP_GRID       RGB(224, 224, 224)
#define SP_AXIS       RGB(150, 150, 150)
#define SP_LABEL      RGB( 90,  90,  90)
#define SP_DOT        RGB( 70, 110, 200)   /* the run                   */
#define SP_DOT_LAST   RGB(220, 130,  20)   /* the epoch just solved     */
#define SP_MEAN       RGB(200,  40,  40)   /* where the run centres     */
#define SP_ARP        RGB( 90,  90, 110)   /* the broadcast reference   */

/* ── Accumulation ────────────────────────────────────────────────────── */

void SelfPosReset(AppState *state)
{
    if (!state) return;

    /* The points only.  Everything else this window shows belongs to the
     * report's accumulator, which has its own reset. */
    state->selfpos.head        = 0;
    state->selfpos.count       = 0;
    state->selfpos.t0          = 0.0;
    state->selfpos.lastStreamT = -1e9;
    state->selfpos.haveT0      = FALSE;

    if (state->hSelfPosWnd)
        PostMessage(state->hSelfPosWnd, WM_APP_SELFPOS_UPDATE, 0, 0);
}

void SelfPosOnStats(AppState *state, const NsStatsSnapshot *s)
{
    if (!state || !s) return;

    SelfPosRing *r = &state->selfpos;

    /* An epoch that did not solve contributes no point.  A point at the
     * origin is what a station sitting exactly on its declared
     * coordinates looks like, and that is the one thing a station which
     * cannot be solved at all must not appear to be doing. */
    const bool solved = (s->selfpos_status == 0 && s->selfpos_sats > 0 &&
                         s->selfpos_e != NS_UNSET);

    /* Stamped and paced by the stream's own clock, so a replay plots the
     * span the capture holds; a snapshot republished with the same
     * stream time is the same epoch and is not plotted twice. */
    const double t = s->stream_time_s;
    if (solved && t >= 0.0 && t > r->lastStreamT) {
        r->lastStreamT = t;
        if (!r->haveT0) { r->t0 = t; r->haveT0 = TRUE; }

        SelfPosPoint p;
        p.ts_rel = (float)(t - r->t0);
        p.e      = (float)s->selfpos_e;
        p.n      = (float)s->selfpos_n;
        p.u      = (float)s->selfpos_u;

        /* Written whole, then published: a painter that reads `count`
         * first can only see slots that are complete. */
        r->pts[r->head] = p;
        r->head = (r->head + 1) % SELFPOS_CAP;
        if (r->count < SELFPOS_CAP) r->count++;
    }

    /* Repaint even when nothing solved: the figures and the reason in
     * the header move whether or not a point was added. */
    if (state->hSelfPosWnd)
        PostMessage(state->hSelfPosWnd, WM_APP_SELFPOS_UPDATE, 0, 0);
}

/* The longest string each column can hold.  Named here so the widths
 * measured when the list is built and the width reserved for it when
 * the window is laid out cannot drift apart. */
#define SP_WIDEST_FIGURE "Latest epoch: rate residual"
#define SP_WIDEST_VALUE  "not computable"
/* Forty characters, and both of these are exactly that long: the
 * longest note a solved window writes, and the longest reason a stream
 * that cannot be solved gives. Anything longer belongs in the header,
 * which has the width for it. */
#define SP_WIDEST_NOTE   "single-frequency station: not computable"

/**
 * @brief Width of @p s in the font @p hwnd actually draws with.
 *
 * Measured rather than guessed, which is the only way a column fits on
 * a machine that is not the developer's.
 */
static int TextWidth(HWND hwnd, const char *s)
{
    SIZE sz = { 0, 0 };
    HDC hdc = GetDC(hwnd);
    if (!hdc) return 8 * (int)strlen(s);     /* a poor guess, but bounded */

    HFONT f = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
    HFONT old = f ? (HFONT)SelectObject(hdc, f) : NULL;
    GetTextExtentPoint32(hdc, s, (int)strlen(s), &sz);
    if (old) SelectObject(hdc, old);
    ReleaseDC(hwnd, hdc);
    return sz.cx;
}

/** @brief A column wide enough for its heading and its longest value. */
static int SpColWidth(HWND hLv, const char *header, const char *widest)
{
    const int a = TextWidth(hLv, header);
    const int b = TextWidth(hLv, widest);
    /* The list draws a margin either side, so a column exactly as wide
     * as its content still shows an ellipsis. */
    return (a > b ? a : b) + 24;
}

/* ── What this window is entitled to say ─────────────────────────────── */

/**
 * @brief Solved, still gathering, never going to, or tried and failed.
 *
 * The distinction is the whole difference between a fact about the
 * station and a fact about the clock. Thirty seconds into a stream
 * nothing has solved yet, and the first version of this window said
 * **NOT COMPUTABLE** at it -- a verdict on the station, from a window
 * too short to carry one. The Signal level and Ionosphere rows of the
 * Stability window had each made exactly that mistake before, which is
 * why they say "gathering" until there is evidence to judge on.
 *
 * `SPP_SINGLE_FREQ` is the one status that is a property of the stream
 * rather than of how long we have watched it: a station sending one
 * frequency will not start sending two. Everything else -- no epoch
 * yet, no orbits yet, too few satellites, a solve that did not settle
 * -- is ordinary at the start of a run and only becomes a finding once
 * the window is long enough that it should have resolved.
 */
typedef enum {
    SP_SOLVED,          /**< at least one epoch in this window solved   */
    SP_GATHERING,       /**< too early to say anything                  */
    SP_NOT_COMPUTABLE,  /**< single-frequency: it never will            */
    SP_NOT_SOLVED       /**< long enough to have solved, and did not    */
} SpState;

static SpState SelfPosState(const AppState *state)
{
    const StationReport *r = &state->reportOut;

    if (state->reportHave && r->sp_samples > 0) return SP_SOLVED;

    /* A single-frequency stream is told apart at once: waiting longer
     * cannot change it, and saying "gathering" would be a promise. */
    if (state->reportHave && r->sp_last_status == SPP_SINGLE_FREQ)
        return SP_NOT_COMPUTABLE;

    if (state->reportHave && r->window_s >= SR_MIN_WINDOW_S)
        return SP_NOT_SOLVED;

    return SP_GATHERING;
}

/**
 * @brief The same thing in a column's worth of words.
 *
 * The header has a window's width to explain itself in; the note column
 * has about forty characters, measured from @ref SP_WIDEST_NOTE. Given
 * the header's sentence it ellipsised it, which left the shortened
 * half of an explanation sitting beside the whole one.
 */
static const char *SelfPosWhyShort(const AppState *state, SpState st)
{
    const StationReport *r = &state->reportOut;

    if (st == SP_NOT_COMPUTABLE || st == SP_NOT_SOLVED)
        return spp_status_text((SppStatus)r->sp_last_status);
    if (!state->bWorkerRunning)
        return "open a stream or replay a capture";
    if (state->reportHave && r->sp_last_status == SPP_NO_EPHEMERIS)
        return "waiting for orbits (ephemerides)";
    if (state->reportHave && r->sp_last_status == SPP_TOO_FEW_SATS)
        return "waiting for four usable satellites";
    return "waiting for the first epoch to solve";
}

/** @brief The sentence under the banner: why there is nothing yet. */
static const char *SelfPosWhy(const AppState *state, SpState st)
{
    const StationReport *r = &state->reportOut;

    switch (st) {
    case SP_SOLVED:
        return "No verdict: no threshold for a station's own scatter has "
               "been established from evidence";
    case SP_NOT_COMPUTABLE:
    case SP_NOT_SOLVED:
        return spp_status_text((SppStatus)r->sp_last_status);
    default:
        break;
    }

    /* Gathering.  Say what is being waited for, and how long it takes,
     * rather than naming a status a reader would read as a fault. */
    if (!state->bWorkerRunning)
        return "open a stream or replay a capture; this watches what it "
               "carries";
    if (state->reportHave && r->sp_last_status == SPP_NO_EPHEMERIS)
        return "waiting for orbits: a station's own position needs the "
               "ephemerides its stream or a RINEX file supplies";
    if (state->reportHave && r->sp_last_status == SPP_TOO_FEW_SATS)
        return "waiting for four usable satellites on two frequencies";
    return "waiting for the first observation epoch to solve";
}

/* ── Figures ─────────────────────────────────────────────────────────── */

static void SetRow(HWND hLv, int row, const char *name, const char *value,
                   const char *note)
{
    if (ListView_GetItemCount(hLv) <= row) {
        LVITEM lvi;
        ZeroMemory(&lvi, sizeof(lvi));
        lvi.mask    = LVIF_TEXT;
        lvi.iItem   = row;
        lvi.pszText = (char *)name;
        ListView_InsertItem(hLv, &lvi);
    } else {
        ListView_SetItemText(hLv, row, 0, (char *)name);
    }
    ListView_SetItemText(hLv, row, 1, (char *)value);
    ListView_SetItemText(hLv, row, 2, (char *)note);
}

/**
 * @brief Fill the figures list from the report and the latest epoch.
 *
 * Window figures come from @ref StationReport -- core's accumulation
 * over the same window of stream time the Stability window shows, so
 * the two windows cannot state different numbers for one stream. The
 * last four rows are the latest epoch, from the snapshot, and are
 * labelled as such: one epoch is a diagnostic, never the measurement.
 */
static void RefreshRows(HWND hwnd, AppState *state)
{
    HWND hLv = GetDlgItem(hwnd, IDC_SELFPOS_LIST);
    if (!hLv || !state) return;

    const StationReport *r  = &state->reportOut;
    const NsStatsSnapshot *s = state->haveStats ? &state->lastStats : NULL;
    const SpState st = SelfPosState(state);
    const BOOL solved_window = (st == SP_SOLVED);

    int row = 0;
    char v[64], note[160];

    snprintf(v, sizeof(v), "%d", solved_window ? r->sp_samples : 0);
    snprintf(note, sizeof(note), "of %d snapshot(s) in the window",
             state->reportHave ? r->samples : 0);
    SetRow(hLv, row++, "Epochs solved", v, note);

    if (solved_window) {
        snprintf(v, sizeof(v), "%+.3f m", r->sp_mean_enu[0]);
        SetRow(hLv, row++, "Mean offset east", v,
               "against the position the station broadcasts");
        snprintf(v, sizeof(v), "%+.3f m", r->sp_mean_enu[1]);
        SetRow(hLv, row++, "Mean offset north", v, "");
        snprintf(v, sizeof(v), "%+.3f m", r->sp_mean_enu[2]);
        SetRow(hLv, row++, "Mean offset up", v,
               "mostly this solution's own bias, not a moved antenna");

        snprintf(v, sizeof(v), "%.3f m", r->sp_scatter_m);
        SetRow(hLv, row++, "Scatter about the mean", v,
               "3-D RMS -- the half of it that measures something");

        snprintf(v, sizeof(v), "%.3f m", r->sp_rms_worst);
        SetRow(hLv, row++, "Worst code residual", v,
               "RMS, over the whole window");

        if (r->sp_speed_median_mms != NS_UNSET) {
            snprintf(v, sizeof(v), "%.1f mm/s", r->sp_speed_median_mms);
            SetRow(hLv, row++, "Median apparent motion", v,
                   "a still base reads near zero");
        } else {
            SetRow(hLv, row++, "Median apparent motion", "--",
                   "no phase-range rates (MSM4, MSM6, legacy)");
        }
    } else {
        /* Nothing solved.  Which of the three that is, and why, rather
         * than an empty window that leaves a reader guessing whether
         * the station is silent or the program is broken. */
        SetRow(hLv, row++, "Position",
               (st == SP_NOT_COMPUTABLE) ? "not computable"
                                         : "gathering",
               SelfPosWhyShort(state, st));
    }

    /* ── The latest epoch ──────────────────────────────────────────── */
    if (s && s->selfpos_status == 0 && s->selfpos_sats > 0) {
        snprintf(v, sizeof(v), "%d", s->selfpos_sats);
        snprintf(note, sizeof(note), "PDOP %.1f", s->selfpos_pdop);
        SetRow(hLv, row++, "Latest epoch: satellites", v, note);

        snprintf(v, sizeof(v), "%.3f m", s->selfpos_code_rms_m);
        SetRow(hLv, row++, "Latest epoch: code residual", v, "RMS");

        if (s->selfpos_has_vel) {
            snprintf(v, sizeof(v), "%.4f m/s", s->selfpos_rate_rms_ms);
            SetRow(hLv, row++, "Latest epoch: rate residual", v,
                   "RMS -- what the velocity was measured to");
            snprintf(v, sizeof(v), "%+.4f m/s", s->selfpos_drift_ms);
            SetRow(hLv, row++, "Latest epoch: clock drift", v,
                   "the receiver's own clock, solved beside the velocity");

            /* Shown only when it is news. A station encoding its rates
             * the way RTCM defines them is the expected case and earns
             * no row; one encoding them reversed is a fault in the
             * station's receiver that any rover trusting those rates
             * inherits, and it is corrected here only for this
             * window's own velocity. */
            if (s->selfpos_rate_sign < 0)
                /* Within SP_WIDEST_NOTE, which the column is measured
                 * from -- a longer note would be cut to an ellipsis. */
                SetRow(hLv, row++, "Phase-range rate sign", "REVERSED",
                       "its receiver encodes it backwards");
        }
    }

    while (ListView_GetItemCount(hLv) > row)
        ListView_DeleteItem(hLv, row);
}

/* ── Header ──────────────────────────────────────────────────────────── */

static void PaintHeader(HDC hdc, RECT *rc, AppState *state)
{
    const StationReport *r = &state->reportOut;
    const SpState st = SelfPosState(state);
    const BOOL solved = (st == SP_SOLVED);

    /* No verdict colours here, and that is the point: this window has no
     * verdict to colour.  One neutral band for "solved", one for "not",
     * neither borrowing tier 2's green or red. */
    COLORREF bg = solved ? RGB(240, 244, 250) : RGB(245, 245, 245);
    COLORREF fg = solved ? RGB( 30,  60, 120) : RGB( 70,  70,  70);
    const char *title;
    switch (st) {
    case SP_SOLVED:         title = "SELF-POSITION";  break;
    case SP_NOT_COMPUTABLE: title = "NOT COMPUTABLE"; break;
    case SP_NOT_SOLVED:     title = "NOT SOLVED";     break;
    default:                title = "GATHERING";      break;
    }

    HBRUSH br = CreateSolidBrush(bg);
    FillRect(hdc, rc, br);
    DeleteObject(br);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, fg);

    HFONT big = CreateFont(-26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HFONT old = (HFONT)SelectObject(hdc, big);
    TextOut(hdc, rc->left + 14, rc->top + 8, title, (int)strlen(title));
    SelectObject(hdc, old);
    DeleteObject(big);

    HFONT small_f = CreateFont(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    old = (HFONT)SelectObject(hdc, small_f);
    SetTextColor(hdc, RGB(60, 60, 60));

    /* "caster / mountpoint" -- unless there is no stream, when that
     * formatting leaves a bare "/" on the line and says nothing. */
    char who[sizeof(state->config.NTRIP_CASTER) +
             sizeof(state->config.MOUNTPOINT) + 8];
    if (state->config.NTRIP_CASTER[0] || state->config.MOUNTPOINT[0])
        snprintf(who, sizeof(who), "%s / %s",
                 state->config.NTRIP_CASTER, state->config.MOUNTPOINT);
    else
        snprintf(who, sizeof(who), "no stream open");

    char line[sizeof(who) + 160];
    if (state->reportHave)
        snprintf(line, sizeof(line),
                 "%s   %.0f s of stream, %d epoch(s) solved%s", who,
                 r->window_s, r->sp_samples,
                 state->reportFromCapture ? "   (from a capture)" : "");
    else
        snprintf(line, sizeof(line), "%s", who);
    TextOut(hdc, rc->left + 16, rc->top + 46, line, (int)strlen(line));

    /* The third line says what is missing rather than leaving a gap:
     * either why nothing has solved, or -- when something has -- that
     * nothing here is graded, which a reader who has just come from the
     * Stability window and its four verdicts will otherwise wonder
     * about. */
    const char *why = SelfPosWhy(state, st);
    SetTextColor(hdc, RGB(110, 110, 110));
    TextOut(hdc, rc->left + 16, rc->top + 64, why, (int)strlen(why));

    SelectObject(hdc, old);
    DeleteObject(small_f);
}

/* ── Plot ────────────────────────────────────────────────────────────── */

/**
 * @brief Half-width of the plot, in metres: a round number that fits.
 *
 * Chosen from the points themselves rather than fixed, because the range
 * this spans is not knowable in advance -- a good dual-frequency station
 * scatters under a metre, and a station with half its constellation
 * missing can scatter tens.  Round steps only, so the rings a reader
 * measures against are numbers rather than artefacts of the data.
 */
/**
 * @brief A ring label a reader can take in: "250 m", "2.5 km".
 *
 * `%.4g m` printed a 10 km ring as "1e+04 m", which is a number nobody
 * reads off a plot.
 */
static void FormatDistance(char *out, size_t cap, double metres)
{
    if (metres >= 1000.0)
        snprintf(out, cap, "%.3g km", metres / 1000.0);
    else
        snprintf(out, cap, "%.3g m", metres);
}

static double PlotRadius(double widest)
{
    const double want = widest * 1.15;          /* room outside the dots */
    if (!(want > 0.0)) return 0.25;

    /* 1, 2 or 5 times a power of ten, computed rather than tabulated.
     *
     * A table has a largest entry, and a plot whose scale cannot exceed
     * it does not scale at all: the first one stopped at 500 m and drew
     * a station 7.8 km out as an empty frame with rings marked 250 m
     * and 500 m -- every dot clipped away, the plot contradicting the
     * figures beside it. A solve that has gone wrong is exactly when
     * the scatter is kilometres, which is exactly when a reader needs
     * to see it. */
    const double decade = pow(10.0, floor(log10(want)));
    const double norm   = want / decade;
    const double mant   = (norm <= 1.0) ? 1.0 : (norm <= 2.0) ? 2.0
                        : (norm <= 5.0) ? 5.0 : 10.0;
    const double r = mant * decade;
    return (r < 0.25) ? 0.25 : r;      /* no finer than a quarter metre */
}

static void PaintPlot(HDC hdc, const RECT *area, AppState *state)
{
    const SelfPosRing *r = &state->selfpos;

    HBRUSH bg = CreateSolidBrush(SP_PLOT_BG);
    FillRect(hdc, (RECT *)area, bg);
    DeleteObject(bg);

    HPEN frame = CreatePen(PS_SOLID, 1, SP_AXIS);
    HPEN oldPen = (HPEN)SelectObject(hdc, frame);
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, area->left, area->top, area->right, area->bottom);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(frame);

    const int cx = (area->left + area->right) / 2;
    const int cy = (area->top + area->bottom) / 2;
    const int half = ((area->right - area->left) < (area->bottom - area->top)
                      ? (area->right - area->left)
                      : (area->bottom - area->top)) / 2 - 18;
    if (half < 20) return;

    /* The widest point decides the scale.  The mean is included so a run
     * centred two metres away is not drawn against its own middle --
     * self-centring would hide the offset, which is one of the two
     * things this window exists to show. */
    double widest = 0.0;
    const int start = (r->count < SELFPOS_CAP) ? 0 : r->head;
    const int n = r->count;
    for (int i = 0; i < n; i++) {
        const SelfPosPoint *p = &r->pts[(start + i) % SELFPOS_CAP];
        double d = sqrt((double)p->e * p->e + (double)p->n * p->n);
        if (d > widest) widest = d;
    }
    const double radius = PlotRadius(widest);
    const double scale  = (double)half / radius;

    SetBkMode(hdc, TRANSPARENT);
    HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT oldFont = (HFONT)SelectObject(hdc, f);

    /* Rings at the full radius and half of it, and the cross through the
     * reference the station broadcasts -- which is the origin here, and
     * stays the origin. */
    HPEN grid = CreatePen(PS_SOLID, 1, SP_GRID);
    oldPen = (HPEN)SelectObject(hdc, grid);
    oldBr  = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    for (int k = 1; k <= 2; k++) {
        int rr = half * k / 2;
        Ellipse(hdc, cx - rr, cy - rr, cx + rr, cy + rr);
    }
    MoveToEx(hdc, cx - half, cy, NULL); LineTo(hdc, cx + half, cy);
    MoveToEx(hdc, cx, cy - half, NULL); LineTo(hdc, cx, cy + half);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(grid);

    SetTextColor(hdc, SP_LABEL);
    /* Inside the frame, not on it: against the border the E was half a
     * letter into the line. */
    TextOut(hdc, cx + 4, area->top + 4, "N", 1);
    TextOut(hdc, area->right - 20, cy - 18, "E", 1);

    /* The origin, named.  It is the position the station broadcasts in
     * 1005/1006, every offset here is measured from it, and a reader
     * should not have to infer that from a caption: an unlabelled
     * centre reads as "wherever the cloud happens to sit". */
    {
        HPEN arp = CreatePen(PS_SOLID, 1, SP_ARP);
        oldPen = (HPEN)SelectObject(hdc, arp);
        MoveToEx(hdc, cx - 5, cy, NULL); LineTo(hdc, cx + 6, cy);
        MoveToEx(hdc, cx, cy - 5, NULL); LineTo(hdc, cx, cy + 6);
        SelectObject(hdc, oldPen);
        DeleteObject(arp);
        SetTextColor(hdc, SP_ARP);
        TextOut(hdc, cx + 7, cy + 3, "ARP", 3);
        SetTextColor(hdc, SP_LABEL);
    }

    /* The rings are labelled only when points set the scale.  An empty
     * plot was labelling its rings "0.125 m" and "0.25 m" -- the
     * smallest step the chooser can return, which measured nothing and
     * read as a station holding to a tenth of a metre. */
    if (n == 0) {
        const char *none = "no epoch has solved yet";
        SIZE sz = { 0, 0 };
        GetTextExtentPoint32(hdc, none, (int)strlen(none), &sz);
        SetTextColor(hdc, RGB(140, 140, 140));
        TextOut(hdc, cx - sz.cx / 2, cy - half / 2 - sz.cy,
                none, (int)strlen(none));
        SelectObject(hdc, oldFont);
        return;
    }

    {
        char lbl[48];
        SIZE sz;
        FormatDistance(lbl, sizeof(lbl), radius);
        GetTextExtentPoint32(hdc, lbl, (int)strlen(lbl), &sz);
        TextOut(hdc, cx + half - sz.cx - 4, cy + 3, lbl, (int)strlen(lbl));
        FormatDistance(lbl, sizeof(lbl), radius / 2.0);
        GetTextExtentPoint32(hdc, lbl, (int)strlen(lbl), &sz);
        TextOut(hdc, cx + half / 2 - sz.cx - 4, cy + 3, lbl,
                (int)strlen(lbl));
    }

    /* The points.  Two pixels each: at one epoch a second an hour is
     * three and a half thousand of them, and a dot large enough to see
     * individually would be a solid disc long before that. */
    HBRUSH dot = CreateSolidBrush(SP_DOT);
    double sum_e = 0.0, sum_n = 0.0;
    for (int i = 0; i < n; i++) {
        const SelfPosPoint *p = &r->pts[(start + i) % SELFPOS_CAP];
        sum_e += p->e;
        sum_n += p->n;
        int x = cx + (int)lround(p->e * scale);
        int y = cy - (int)lround(p->n * scale);     /* north is up */
        if (x < area->left + 2 || x > area->right - 2 ||
            y < area->top + 2  || y > area->bottom - 2) continue;
        RECT d = { x - 1, y - 1, x + 2, y + 2 };
        FillRect(hdc, &d, dot);
    }
    DeleteObject(dot);

    /* Where the run centres, from the points drawn -- the plot's own
     * crosshair, not a published figure.  The figure beside the plot is
     * core's, over core's window; this is only here so a reader can see
     * which part of the cloud the mean is talking about. */
    {
        int x = cx + (int)lround(sum_e / n * scale);
        int y = cy - (int)lround(sum_n / n * scale);
        HPEN mean = CreatePen(PS_SOLID, 2, SP_MEAN);
        oldPen = (HPEN)SelectObject(hdc, mean);
        MoveToEx(hdc, x - 7, y, NULL); LineTo(hdc, x + 8, y);
        MoveToEx(hdc, x, y - 7, NULL); LineTo(hdc, x, y + 8);
        SelectObject(hdc, oldPen);
        DeleteObject(mean);
    }

    /* The epoch that just solved, large enough to follow while watching:
     * the newest point is the one a reader is looking for. */
    {
        const SelfPosPoint *p = &r->pts[(r->head + SELFPOS_CAP - 1)
                                        % SELFPOS_CAP];
        int x = cx + (int)lround(p->e * scale);
        int y = cy - (int)lround(p->n * scale);
        HBRUSH last = CreateSolidBrush(SP_DOT_LAST);
        RECT d = { x - 3, y - 3, x + 4, y + 4 };
        FillRect(hdc, &d, last);
        DeleteObject(last);
    }

    SelectObject(hdc, oldFont);
}

/**
 * @brief One legend label, and how far it moved the cursor.
 *
 * @return Width used, including the gap before the next entry.
 */
static int LegendLabel(HDC hdc, int x, int y, const char *text)
{
    SIZE sz = { 0, 0 };
    const int len = (int)strlen(text);
    GetTextExtentPoint32(hdc, text, len, &sz);
    TextOut(hdc, x, y, text, len);
    return sz.cx + 16;
}

static void PaintCaption(HDC hdc, const RECT *rc, AppState *state)
{
    const SelfPosRing *r = &state->selfpos;

    /* The page's own background, not a button face: a grey band under
     * the plot reads as a toolbar nobody put anything in. */
    HBRUSH bg = CreateSolidBrush(SP_BG);
    FillRect(hdc, (RECT *)rc, bg);
    DeleteObject(bg);

    char line[256];
    double span = 0.0;
    if (r->count > 0) {
        const int start = (r->count < SELFPOS_CAP) ? 0 : r->head;
        span = (double)r->pts[(r->head + SELFPOS_CAP - 1) % SELFPOS_CAP].ts_rel
             - (double)r->pts[start].ts_rel;
    }

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(80, 80, 80));
    HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT old = (HFONT)SelectObject(hdc, f);

    /* The caption is as wide as the plot and no wider, so the sentence
     * is chosen to fit rather than written and then cut: the first
     * version ended "47 point(s) o..." under a plot with plenty of
     * room above it. Longest first, and the shortest always fits
     * because it is four words.
     *
     * The centre needs less explaining than it did -- the cross on the
     * plot is labelled ARP -- so the long form is the one that adds
     * the window's span, not the one that repeats the label. */
    const int avail = (rc->right - rc->left) - 6;
    const char *chosen = NULL;
    if (r->count > 0) {
        const char *forms[3];
        char a[200], b[160], c[96];
        snprintf(a, sizeof(a),
                 "east/north from the ARP the station broadcasts, "
                 "%d point(s) over %.0f s; up is in the figures",
                 r->count, span);
        snprintf(b, sizeof(b),
                 "east/north from the broadcast ARP, %d point(s) over "
                 "%.0f s", r->count, span);
        snprintf(c, sizeof(c), "%d point(s) over %.0f s", r->count, span);
        forms[0] = a; forms[1] = b; forms[2] = c;
        for (int i = 0; i < 3 && !chosen; i++) {
            SIZE sz;
            if (GetTextExtentPoint32(hdc, forms[i], (int)strlen(forms[i]),
                                     &sz) && sz.cx <= avail)
                chosen = forms[i];
        }
        snprintf(line, sizeof(line), "%s", chosen ? chosen : c);
    } else {
        const char *full = "east/north from the ARP the station broadcasts";
        SIZE sz;
        const bool fits = GetTextExtentPoint32(hdc, full, (int)strlen(full),
                                               &sz) && sz.cx <= avail;
        snprintf(line, sizeof(line), "%s",
                 fits ? full : "east/north from the broadcast ARP");
    }

    /* One line, never wrapped: the row below it is the legend and must
     * keep its place. Written as a wrapping paragraph, the legend was
     * pushed off the bottom of the strip -- in the code, invisible on
     * screen. */
    RECT t = *rc;
    t.left += 2;
    t.bottom = t.top + SP_CAPTION_LINE;
    DrawText(hdc, line, -1, &t,
             DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS |
             DT_NOPREFIX);

    /* The legend, drawn with the marks it describes rather than named
     * in words: a reader matching "orange" to a dot has to be told
     * which orange, and the marks differ in shape as well as colour. */
    if (r->count > 0) {
        int x = rc->left + 2;
        const int y = rc->top + SP_CAPTION_LINE;
        const int my = y + 8;                       /* mark centre line */

        /* the run: a dot the size the plot draws */
        HBRUSH b = CreateSolidBrush(SP_DOT);
        RECT d = { x, my - 2, x + 4, my + 2 };
        FillRect(hdc, &d, b);
        DeleteObject(b);
        x += 9;
        x += LegendLabel(hdc, x, y, "the run");

        /* the latest epoch: the larger square */
        b = CreateSolidBrush(SP_DOT_LAST);
        RECT d2 = { x, my - 3, x + 6, my + 3 };
        FillRect(hdc, &d2, b);
        DeleteObject(b);
        x += 11;
        x += LegendLabel(hdc, x, y, "latest epoch");

        /* where the run centres: the cross */
        HPEN p = CreatePen(PS_SOLID, 2, SP_MEAN);
        HPEN oldp = (HPEN)SelectObject(hdc, p);
        MoveToEx(hdc, x, my, NULL);      LineTo(hdc, x + 9, my);
        MoveToEx(hdc, x + 4, my - 4, NULL); LineTo(hdc, x + 4, my + 5);
        SelectObject(hdc, oldp);
        DeleteObject(p);
        x += 13;
        (void)LegendLabel(hdc, x, y, "where the run centres");
    }

    SelectObject(hdc, old);
}

/* ── Export ──────────────────────────────────────────────────────────── */

/** @brief Write @p s as a JSON string literal, escaped. */
static void json_str(FILE *f, const char *s)
{
    fputc('"', f);
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { fputc('\\', f); fputc(c, f); }
        else if (c < 0x20)         fprintf(f, "\\u%04x", c);
        else                       fputc(c, f);
    }
    fputc('"', f);
}

/** @brief One of core's serialisers, sized first so nothing is cut. */
static void json_embed(FILE *f, int need, char *buf)
{
    if (need > 0 && buf) fwrite(buf, 1, (size_t)need, f);
    else                 fputs("null", f);
}

/**
 * @brief Write the companion to an exported image: the numbers behind it.
 *
 * The picture is for a person; this is for whoever has to check it. It
 * holds three things:
 *
 * - the **report** -- core's own serialiser, the same document the
 *   daemon publishes as `<mountpoint>.report.json`, so the window's
 *   figures and their evidence are carried in the project's one format
 *   rather than in a second dialect invented for an export;
 * - the **snapshot** -- the latest epoch, the same record File > Export
 *   Statistics writes;
 * - the **points** behind the plot, in time order, as `[t, e, n, u]`,
 *   so the cloud can be re-plotted or analysed without the image.
 *
 * @return true when the whole file was written.
 */
static bool WriteSelfPosCompanion(AppState *state, const char *png,
                                  const char *path)
{
    /* The image's own name, without its folder: the two files travel
     * together and the reference must survive being moved. */
    const char *base = png;
    for (const char *p = png; *p; p++)
        if (*p == '\\' || *p == '/') base = p + 1;

    /* Sized, then written: a truncated document must never be saved as
     * though it were complete. */
    char *rep = NULL, *snap = NULL;
    int rep_n = -1, snap_n = -1;
    if (state->reportHave) {
        SrJsonCtx ctx;
        ctx.mountpoint  = state->config.MOUNTPOINT;
        ctx.policy      = state->thresholdsPath;
        ctx.fingerprint = state->thresholdsFp;
        rep_n = sr_to_json(&state->reportOut, &ctx, NULL, 0);
        if (rep_n > 0 && (rep = (char *)malloc((size_t)rep_n + 1)) != NULL)
            rep_n = sr_to_json(&state->reportOut, &ctx, rep, (size_t)rep_n + 1);
    }
    if (state->haveStats) {
        snap_n = ns_stats_to_json(&state->lastStats, NULL, 0);
        if (snap_n > 0 && (snap = (char *)malloc((size_t)snap_n + 1)) != NULL)
            snap_n = ns_stats_to_json(&state->lastStats, snap,
                                      (size_t)snap_n + 1);
    }

    FILE *f = fopen(path, "wb");
    if (!f) { free(rep); free(snap); return false; }

    char when[32] = "";
    {
        time_t now_t = time(NULL);
        struct tm *lt = localtime(&now_t);
        if (lt) strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", lt);
    }

    fputs("{\"selfpos_export_version\":1", f);
    fputs(",\"image\":", f);      json_str(f, base);
    fputs(",\"caster\":", f);     json_str(f, state->config.NTRIP_CASTER);
    fputs(",\"mountpoint\":", f); json_str(f, state->config.MOUNTPOINT);
    fputs(",\"exported_local\":", f); json_str(f, when);
    fputs(",\"points_frame\":", f);
    json_str(f, "metres east, north and up from the ARP the station "
                "broadcasts; t is stream seconds since the plot began");
    fputs(",\"report\":", f);   json_embed(f, rep  ? rep_n  : -1, rep);
    fputs(",\"snapshot\":", f); json_embed(f, snap ? snap_n : -1, snap);

    /* The points, oldest first.  Read once into locals: the ring is
     * filled on the worker thread, and an export taken while it wraps
     * should describe one consistent span. */
    const SelfPosRing *r = &state->selfpos;
    const int count = r->count, head = r->head;
    const int start = (count < SELFPOS_CAP) ? 0 : head;
    fputs(",\"points\":[", f);
    for (int i = 0; i < count; i++) {
        const SelfPosPoint *p = &r->pts[(start + i) % SELFPOS_CAP];
        fprintf(f, "%s[%.1f,%.3f,%.3f,%.3f]", i ? "," : "",
                (double)p->ts_rel, (double)p->e, (double)p->n, (double)p->u);
    }
    fputs("]}\n", f);

    free(rep);
    free(snap);
    const bool ok = !ferror(f);
    return (fclose(f) == 0) && ok;
}

/**
 * @brief S: the window as a PNG, and its numbers beside it.
 *
 * The same key and the same prompt as every other chart window -- the
 * timestamped default name, the overwrite question, the line in the
 * log -- with one difference forced by this window's shape: its figures
 * are a list control, a child window, and an ordinary copy of the
 * window's pixels does not reliably contain children. So it is rendered
 * instead, which also draws any part another window happens to cover.
 */
static void ExportSelfPos(HWND hwnd, AppState *state)
{
    /* A row left highlighted -- by a click, or by the list's own
     * type-ahead, which an S keystroke reaching it selects "Scatter
     * about the mean" with -- would be in the picture. */
    HWND hLv = GetDlgItem(hwnd, IDC_SELFPOS_LIST);
    if (hLv) ListView_SetItemState(hLv, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);

    char png[MAX_PATH];
    if (!SaveWindowPngWithPromptEx(hwnd, state->hEditLog,
                                   "Save Self-position as PNG",
                                   "SelfPosition", "Self-position",
                                   TRUE, png, sizeof(png)))
        return;

    /* Same name, .json: the pair sort together and are obviously a pair. */
    char json[MAX_PATH];
    snprintf(json, sizeof(json), "%s", png);
    char *dot = strrchr(json, '.');
    char *sep = strrchr(json, '\\');
    if (dot && (!sep || dot > sep)) *dot = '\0';
    if (strlen(json) + 5 < sizeof(json)) strcat(json, ".json");

    const bool ok = WriteSelfPosCompanion(state, png, json);
    char msg[MAX_PATH + 96];
    snprintf(msg, sizeof(msg), ok ? "[INFO] Self-position data saved to %s\r\n"
                                  : "[ERROR] Failed to write %s\r\n", json);
    if (state->hEditLog) {
        int len = GetWindowTextLength(state->hEditLog);
        SendMessage(state->hEditLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
        SendMessage(state->hEditLog, EM_REPLACESEL, FALSE, (LPARAM)msg);
    }
}

/* ── Window ──────────────────────────────────────────────────────────── */

/**
 * @brief Where the plot goes, and where the figures go.
 *
 * The plot takes a square of the left-hand side, because a scatter drawn
 * on unequal axes is a scatter nobody can read: a metre east has to be a
 * metre on the screen too, or the cloud's shape is the window's shape.
 */
static void PlotRect(HWND hwnd, RECT *out, int *list_x)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;

    int avail_h = h - SP_HEADER_H - SP_CAPTION_H - 2 * SP_PAD;
    int side = avail_h;
    if (side > w / 2) side = w / 2;

    /* And no wider than leaves the figures room to be read.  Half the
     * window sounds fair and is not: with three measured columns beside
     * it the plot pushed the notes under a horizontal scrollbar, which
     * is how the first build shipped "What it mea..." as a heading. The
     * figures are the half that carries the numbers, so they get their
     * width first and the plot takes what is left.
     *
     * The note column is measured here rather than read back from the
     * control: LayoutChildren stretches it to fill, so reading it would
     * feed the previous layout's answer into this one. */
    HWND hLv = GetDlgItem(hwnd, IDC_SELFPOS_LIST);
    if (hLv) {
        int need = ListView_GetColumnWidth(hLv, 0)
                 + ListView_GetColumnWidth(hLv, 1)
                 + SpColWidth(hLv, "What it means", SP_WIDEST_NOTE)
                 + GetSystemMetrics(SM_CXVSCROLL) + 8;
        int left = w - need - 3 * SP_PAD;
        if (side > left) side = left;
    }
    if (side < 120) side = 120;

    out->left   = SP_PAD;
    out->top    = SP_HEADER_H + SP_PAD;
    out->right  = SP_PAD + side;
    out->bottom = out->top + side;
    *list_x     = out->right + SP_PAD;
}

static void LayoutChildren(HWND hwnd)
{
    RECT rc, plot;
    int list_x = 0;
    GetClientRect(hwnd, &rc);
    PlotRect(hwnd, &plot, &list_x);

    HWND hLv = GetDlgItem(hwnd, IDC_SELFPOS_LIST);
    if (!hLv) return;

    int w = rc.right - list_x - SP_PAD;
    if (w < 180) w = 180;
    MoveWindow(hLv, list_x, SP_HEADER_H + SP_PAD, w,
               rc.bottom - SP_HEADER_H - 2 * SP_PAD, TRUE);

    /* The note column takes what is left, so widening the window widens
     * the column that varies rather than leaving grey beside it. */
    int fixed = ListView_GetColumnWidth(hLv, 0)
              + ListView_GetColumnWidth(hLv, 1);
    int note = w - fixed - GetSystemMetrics(SM_CXVSCROLL) - 4;
    if (note < 120) note = 120;
    ListView_SetColumnWidth(hLv, 2, note);
}

static LRESULT CALLBACK SelfPosWndProc(HWND hwnd, UINT msg,
                                       WPARAM wParam, LPARAM lParam)
{
    AppState *state = (AppState *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
        state = (AppState *)cs->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)state);

        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);

        HWND hLv = CreateWindowEx(0, WC_LISTVIEW, "",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
            0, 0, 10, 10, hwnd, (HMENU)IDC_SELFPOS_LIST, hInst, NULL);
        ListView_SetExtendedListViewStyle(hLv,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

        /* The font first, then the columns: a width measured before
         * this would be measured in the stock font the control is born
         * with rather than the one it draws in.  Same order, and the
         * same reason, as the Stability window's verdict column. */
        HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        SendMessage(hLv, WM_SETFONT, (WPARAM)f, TRUE);

        /* Three columns, and deliberately no fourth: a Verdict column
         * is what this window does not have.
         *
         * Widths are measured from the longest string each column can
         * hold, never guessed -- the first guess clipped "What it
         * means" in its own header and truncated every note beside it.
         * §14.6 of design/gui-design.md records the same lesson from
         * the Stability window; it did not transfer by being written
         * down. */
        struct { const char *t; const char *widest; } cols[] = {
            { "Figure",        SP_WIDEST_FIGURE },
            { "Value",         SP_WIDEST_VALUE  },
            { "What it means", SP_WIDEST_NOTE   },
        };
        for (int i = 0; i < 3; i++) {
            LVCOLUMN c;
            ZeroMemory(&c, sizeof(c));
            c.mask    = LVCF_TEXT | LVCF_WIDTH;
            c.pszText = (char *)cols[i].t;
            c.cx      = SpColWidth(hLv, cols[i].t, cols[i].widest);
            ListView_InsertColumn(hLv, i, &c);
        }

        LayoutChildren(hwnd);
        RefreshRows(hwnd, state);
        return 0;
    }

    case WM_SIZE:
        LayoutChildren(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_APP_SELFPOS_UPDATE:
        RefreshRows(hwnd, state);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    /* S exports, as it does in every other chart window -- but those
     * have no child that takes the keyboard, and this one does: once
     * the figures list is clicked, keystrokes go to it and never reach
     * this procedure. So the list's own key notification is honoured
     * too, or S would stop working the moment someone touched a row.
     *
     * Posted rather than run on the spot, so the keystroke has finished
     * -- the list's type-ahead included -- before a dialog opens. */
    case WM_KEYDOWN:
        if (wParam == 'S') {
            PostMessage(hwnd, WM_APP_SELFPOS_EXPORT, 0, 0);
            return 0;
        }
        break;

    case WM_NOTIFY: {
        const NMHDR *nh = (const NMHDR *)lParam;
        if (nh && nh->idFrom == IDC_SELFPOS_LIST && nh->code == LVN_KEYDOWN &&
            ((const NMLVKEYDOWN *)lParam)->wVKey == 'S') {
            PostMessage(hwnd, WM_APP_SELFPOS_EXPORT, 0, 0);
            return 0;
        }
        break;
    }

    case WM_APP_SELFPOS_EXPORT:
        if (state) ExportSelfPos(hwnd, state);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        if (!state) break;

        PAINTSTRUCT ps;
        HDC hdcScreen = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;

        /* Drawn into a bitmap and blitted once: the plot repaints on
         * every epoch, and a cloud of several thousand dots drawn
         * straight to the screen flickers while it does. */
        HDC     hdcMem = CreateCompatibleDC(hdcScreen);
        HBITMAP bmp    = CreateCompatibleBitmap(hdcScreen, w, h);
        HBITMAP bmpOld = (HBITMAP)SelectObject(hdcMem, bmp);

        HBRUSH bg = CreateSolidBrush(SP_BG);
        RECT all = { 0, 0, w, h };
        FillRect(hdcMem, &all, bg);
        DeleteObject(bg);

        RECT hdr = all;
        hdr.bottom = SP_HEADER_H;
        PaintHeader(hdcMem, &hdr, state);

        RECT plot;
        int list_x = 0;
        PlotRect(hwnd, &plot, &list_x);
        PaintPlot(hdcMem, &plot, state);

        RECT cap = { plot.left, plot.bottom + 2, plot.right,
                     plot.bottom + 2 + SP_CAPTION_H };
        PaintCaption(hdcMem, &cap, state);

        BitBlt(hdcScreen, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);

        SelectObject(hdcMem, bmpOld);
        DeleteObject(bmp);
        DeleteDC(hdcMem);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CLOSE:
        if (state) {
            GetWindowRect(hwnd, &state->selfposWndRect);
            state->selfposWndRectValid = TRUE;
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        /* The ring keeps filling: it belongs to the session, not to this
         * window.  Closing a window must not throw away an hour of
         * points, exactly as closing the Stability window does not throw
         * away its window of evidence. */
        if (state) state->hSelfPosWnd = NULL;
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

BOOL RegisterSelfPosWindowClass(HINSTANCE hInst)
{
    static BOOL registered = FALSE;
    if (registered) return TRUE;

    WNDCLASSEX wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = SelfPosWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = SELFPOS_WINDOW_CLASS;
    wc.hIcon         = GuiLoadAppIcon(FALSE);
    wc.hIconSm       = GuiLoadAppIcon(TRUE);

    if (!RegisterClassEx(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return FALSE;
    }
    registered = TRUE;
    return TRUE;
}

HWND CreateSelfPosWindow(HINSTANCE hInst, HWND hOwner, AppState *state)
{
    if (!RegisterSelfPosWindowClass(hInst)) return NULL;

    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    int w = SELFPOS_WIN_DEF_W, h = SELFPOS_WIN_DEF_H;
    if (state && state->selfposWndRectValid) {
        x = state->selfposWndRect.left;
        y = state->selfposWndRect.top;
        w = state->selfposWndRect.right  - state->selfposWndRect.left;
        h = state->selfposWndRect.bottom - state->selfposWndRect.top;
        if (w < 480 || h < 320) { w = SELFPOS_WIN_DEF_W;
                                  h = SELFPOS_WIN_DEF_H; }
    }

    HWND hwnd = CreateWindowEx(
        WS_EX_TOOLWINDOW,
        SELFPOS_WINDOW_CLASS, "Self-position",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        x, y, w, h,
        hOwner, NULL, hInst, state);
    if (hwnd) {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
    }
    return hwnd;
}
