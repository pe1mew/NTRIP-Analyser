/**
 * @file gui_sky_window.c
 * @brief Floating polar sky-visibility plot window.
 *
 * Draws a polar (azimuth/elevation) plot of satellites tracked by the
 * connected reference station.  Phase 1 ships only the empty compass rose;
 * subsequent phases plot SV markers fed from cached RTCM 1019/1045/1046
 * ephemerides combined with MSM observations.
 *
 * Threading: lives entirely on the UI thread.  Worker thread will post
 * WM_APP_SKY_UPDATE to the main window, which copies the payload into
 * AppState->skyState and InvalidateRect()s this window.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "resource.h"
#include "gui_state.h"
#include "gui_sky_window.h"
#include "gui_sv_detail.h"
#include "gui_snapshot.h"
#include "rtcm3x_parser.h"
#include "sv_ephemeris.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <commdlg.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Default window size when first opened — chosen large enough that
 * the rose has room to breathe and the legend fits comfortably. */
#define SKY_WIN_DEF_W   860
#define SKY_WIN_DEF_H   880

/* Sector grid: band 0 = 0..10 deg elevation (horizon), band 8 = 80..90 deg
 * (zenith).  Widest bands at low elevation get the most azimuth slices so
 * each sector covers a comparable solid angle. */
const int sky_az_bins_per_band[SKY_N_EL_BANDS] = {
    33,  /* 0..10  deg */
    30,  /* 10..20 deg */
    25,  /* 20..30 deg */
    21,  /* 30..40 deg */
    16,  /* 40..50 deg */
    11,  /* 50..60 deg */
     8,  /* 60..70 deg */
     5,  /* 70..80 deg */
     1,  /* 80..90 deg (zenith cap) */
};

/* SV markers older than this go dim, older than 2x get removed */
#define SKY_STALE_S       5.0
#define SKY_DROP_S       30.0

/* Colours */
#define SKY_BG_COLOR     RGB(255, 255, 255)
#define SKY_RING_COLOR   RGB(180, 180, 180)
#define SKY_AXIS_COLOR   RGB(140, 140, 140)
#define SKY_LABEL_COLOR  RGB(  0,   0,   0)
#define SKY_STATUS_COLOR RGB(120, 120, 120)
#define SKY_GPS_COLOR    RGB( 40, 140,  40)   /* G -- green   */
#define SKY_GLO_COLOR    RGB(210,  40,  40)   /* R -- red     */
#define SKY_GAL_COLOR    RGB( 30,  80, 200)   /* E -- blue    */
#define SKY_QZS_COLOR    RGB(180,  40, 160)   /* J -- magenta */
#define SKY_BDS_COLOR    RGB(220, 130,  20)   /* C -- orange  */
#define SKY_DIM_COLOR    RGB(180, 180, 180)   /* stale marker */
#define SKY_MARKER_OUTLINE RGB(40, 40, 40)

/**
 * @brief Compute the polar-rose centre + outer radius for a given window size.
 *
 * Shared by the paint code (so the rose lands in the same place) and the
 * right-click hit-test (so click positions match the rendered markers).
 * Top margin is larger to leave room for the 3-row header (status / mode
 * / legend).
 */
static void sky_compute_geometry(int w, int h,
                                 int *out_cx, int *out_cy, int *out_radius)
{
    const int margin_top   = 80;
    const int margin_other = 28;

    int avail_w = w - 2 * margin_other;
    int avail_h = h - margin_top - margin_other;
    int diameter = (avail_w < avail_h) ? avail_w : avail_h;
    if (diameter < 60) diameter = 60;

    if (out_cx)     *out_cx     = w / 2;
    if (out_cy)     *out_cy     = margin_top + diameter / 2;
    if (out_radius) *out_radius = diameter / 2;
}

/**
 * @brief Fill the window background and return the rose geometry.
 *
 * Splits out from the old DrawSkyBackground so heatmap mode can paint
 * sectors between the fill and the grid foreground.
 */
static void DrawSkyFill(HDC hdc, int w, int h,
                       int *out_cx, int *out_cy, int *out_radius)
{
    HBRUSH bg = CreateSolidBrush(SKY_BG_COLOR);
    RECT rc = { 0, 0, w, h };
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    sky_compute_geometry(w, h, out_cx, out_cy, out_radius);
}

/**
 * @brief Draw the compass-rose foreground (rings, crosshair, N/E/S/W,
 *        elevation labels) on top of whatever's already in the buffer.
 *
 * Maps elevation 0..90 to a radial distance (90 at centre, 0 at outer ring).
 * Azimuth 0 = north, clockwise.
 */
static void DrawSkyGrid(HDC hdc, int cx, int cy, int radius)
{
    SetBkMode(hdc, TRANSPARENT);

    /* Elevation rings (outer 0 deg, then 15, 30, 45, 60, 75) */
    HPEN penRing = CreatePen(PS_SOLID, 1, SKY_RING_COLOR);
    HPEN penOld  = (HPEN)SelectObject(hdc, penRing);
    HBRUSH brOld = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

    const int els[] = { 0, 15, 30, 45, 60, 75 };
    const int nEls = (int)(sizeof(els) / sizeof(els[0]));
    for (int i = 0; i < nEls; i++) {
        int r = (int)((90 - els[i]) / 90.0 * radius + 0.5);
        Ellipse(hdc, cx - r, cy - r, cx + r, cy + r);
    }

    /* N-S and E-W crosshair (dotted) */
    HPEN penAxis = CreatePen(PS_DOT, 1, SKY_AXIS_COLOR);
    SelectObject(hdc, penAxis);
    MoveToEx(hdc, cx - radius, cy, NULL); LineTo(hdc, cx + radius, cy);
    MoveToEx(hdc, cx, cy - radius, NULL); LineTo(hdc, cx, cy + radius);

    /* Compass labels */
    SetTextColor(hdc, SKY_LABEL_COLOR);
    SetTextAlign(hdc, TA_CENTER | TA_TOP);
    TextOut(hdc, cx, cy - radius - 18, "N", 1);
    TextOut(hdc, cx, cy + radius + 2,  "S", 1);
    SetTextAlign(hdc, TA_LEFT | TA_TOP);
    TextOut(hdc, cx + radius + 4, cy - 7, "E", 1);
    SetTextAlign(hdc, TA_RIGHT | TA_TOP);
    TextOut(hdc, cx - radius - 4, cy - 7, "W", 1);

    /* Elevation labels along north side of the N-S axis */
    SetTextAlign(hdc, TA_LEFT | TA_TOP);
    SetTextColor(hdc, SKY_AXIS_COLOR);
    for (int i = 1; i < nEls; i++) {
        int r = (int)((90 - els[i]) / 90.0 * radius + 0.5);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", els[i]);
        TextOut(hdc, cx + 3, cy - r - 2, buf, (int)strlen(buf));
    }

    SelectObject(hdc, penOld);
    SelectObject(hdc, brOld);
    DeleteObject(penRing);
    DeleteObject(penAxis);
}

/**
 * @brief Map an observed/expected ratio to a colour on the red->green ramp.
 *
 * Expected == 0 sectors return a pale grey ("nothing was supposed to be
 * here -- polar hole or below horizon").  Otherwise the ratio drives a
 * smooth red->yellow->green gradient.  Values >1 (more observed than
 * expected -- shouldn't happen with the current accumulator) are clamped.
 */
static COLORREF heatmap_color(int observed, int expected)
{
    if (expected == 0) return RGB(240, 240, 240);   /* polar hole */

    double ratio = (double)observed / (double)expected;
    if (ratio > 1.0) ratio = 1.0;
    if (ratio < 0.0) ratio = 0.0;

    int r, g, b;
    if (ratio < 0.5) {
        /* Red (220,60,60) -> Yellow (230,220,70) */
        double t = ratio * 2.0;
        r = (int)(220 + t * (230 - 220));
        g = (int)( 60 + t * (220 -  60));
        b = (int)( 60 + t * ( 70 -  60));
    } else {
        /* Yellow (230,220,70) -> Green (70,170,70) */
        double t = (ratio - 0.5) * 2.0;
        r = (int)(230 + t * ( 70 - 230));
        g = (int)(220 + t * (170 - 220));
        b = (int)( 70 + t * ( 70 -  70));
    }
    return RGB(r, g, b);
}

/**
 * @brief Draw a small colour swatch with a black outline at @p x, @p y.
 */
static void draw_swatch(HDC hdc, int x, int y, int w, int h, COLORREF color)
{
    HBRUSH br = CreateSolidBrush(color);
    RECT r = { x, y, x + w, y + h };
    FillRect(hdc, &r, br);
    DeleteObject(br);

    HBRUSH ob = CreateSolidBrush(RGB(60, 60, 60));
    FrameRect(hdc, &r, ob);
    DeleteObject(ob);
}

/**
 * @brief Render the mode-appropriate legend as a horizontal strip.
 *
 * Marker mode: colour swatches paired with their GNSS abbreviations.
 * Heatmap mode: a red->yellow->green gradient bar with percentage scale
 * and a swatch for "no expected" (polar hole / below horizon).
 */
static void DrawSkyLegend(HDC hdc, int x, int y, SkyPlotMode mode)
{
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, SKY_LABEL_COLOR);
    SetTextAlign(hdc, TA_LEFT | TA_TOP);

    const int sw = 12, sh = 10;
    const int gap_after_label = 44;     /* spacing between GNSS entries */

    if (mode == SKY_MODE_MARKERS) {
        const struct { COLORREF c; const char *lbl; } items[] = {
            { SKY_GPS_COLOR, "GPS"  },
            { SKY_GLO_COLOR, "GLO"  },
            { SKY_GAL_COLOR, "GAL"  },
            { SKY_QZS_COLOR, "QZS"  },
            { SKY_BDS_COLOR, "BDS"  },
        };
        int xx = x;
        for (int i = 0; i < 5; i++) {
            draw_swatch(hdc, xx, y + 2, sw, sh, items[i].c);
            xx += sw + 4;
            TextOut(hdc, xx, y, items[i].lbl, (int)strlen(items[i].lbl));
            xx += gap_after_label;
        }
    } else {
        /* "Coverage:" + gradient bar + "0% / 50% / 100%" + polar-hole swatch */
        const char *hdr = "Coverage:";
        TextOut(hdc, x, y, hdr, (int)strlen(hdr));
        int xx = x + 64;

        /* Gradient bar: 16 segments wide */
        const int bar_w = 120;
        const int bar_h = 10;
        const int n_seg = 16;
        for (int i = 0; i < n_seg; i++) {
            int observed = i;
            int expected = n_seg - 1;
            HBRUSH br = CreateSolidBrush(heatmap_color(observed, expected));
            RECT r = {
                xx + (i * bar_w) / n_seg,       y + 2,
                xx + ((i + 1) * bar_w) / n_seg, y + 2 + bar_h
            };
            FillRect(hdc, &r, br);
            DeleteObject(br);
        }
        /* Outline the gradient bar */
        HBRUSH outline = CreateSolidBrush(RGB(60, 60, 60));
        RECT bar = { xx, y + 2, xx + bar_w, y + 2 + bar_h };
        FrameRect(hdc, &bar, outline);
        DeleteObject(outline);

        /* % labels just below the bar */
        SetTextColor(hdc, SKY_AXIS_COLOR);
        TextOut(hdc, xx,             y + 2 + bar_h + 1, "0%",  2);
        SetTextAlign(hdc, TA_CENTER | TA_TOP);
        TextOut(hdc, xx + bar_w / 2, y + 2 + bar_h + 1, "50%", 3);
        SetTextAlign(hdc, TA_RIGHT  | TA_TOP);
        TextOut(hdc, xx + bar_w,     y + 2 + bar_h + 1, "100%", 4);
        SetTextAlign(hdc, TA_LEFT   | TA_TOP);

        /* Polar-hole swatch + label after the bar */
        xx += bar_w + 14;
        draw_swatch(hdc, xx, y + 2, sw, sh, RGB(240, 240, 240));
        xx += sw + 4;
        SetTextColor(hdc, SKY_LABEL_COLOR);
        TextOut(hdc, xx, y, "no eph", 6);
    }
}

/**
 * @brief Draw the sector heatmap (observed/expected per sector).
 *
 * For each sector with a non-zero counter, draws an annular wedge in the
 * colour from heatmap_color().  The wedge is approximated by a polygon
 * with arc-segment vertices.  The zenith cap (band 8) is drawn as a
 * solid disc since it has only one azimuth slice.
 */
static void DrawSkyHeatmap(HDC hdc, const AppState *state,
                           int cx, int cy, int radius)
{
    HPEN penEdge = CreatePen(PS_SOLID, 1, RGB(140, 140, 140));
    HPEN penOld  = (HPEN)SelectObject(hdc, penEdge);

    /* Arc-segment granularity: ~3 deg of azimuth per segment. */
    const double seg_step_deg = 3.0;

    for (int band = 0; band < SKY_N_EL_BANDS; band++) {
        int n_az = sky_az_bins_per_band[band];
        if (n_az < 1) continue;

        double el_lo = (double)band * 10.0;
        double el_hi = el_lo + 10.0;

        /* radius increases with decreasing elevation, so r_outer is at el_lo */
        double r_outer = (90.0 - el_lo) / 90.0 * (double)radius;
        double r_inner = (90.0 - el_hi) / 90.0 * (double)radius;

        double az_width = 360.0 / (double)n_az;

        for (int az_bin = 0; az_bin < n_az; az_bin++) {
            const SkySector *sec = &state->skyState.sectors[band][az_bin];

            HBRUSH br = CreateSolidBrush(
                heatmap_color(sec->observed, sec->expected));
            HBRUSH brOld = (HBRUSH)SelectObject(hdc, br);

            double az_lo = (double)az_bin * az_width;
            double az_hi = az_lo + az_width;

            if (n_az == 1) {
                /* Zenith cap: just a disc of radius r_outer */
                Ellipse(hdc,
                        cx - (int)r_outer, cy - (int)r_outer,
                        cx + (int)r_outer, cy + (int)r_outer);
            } else {
                /* Build the polygon: inner arc az_lo->az_hi, then outer arc
                 * az_hi->az_lo (reversed) to close the wedge. */
                int   n_seg = (int)((az_hi - az_lo) / seg_step_deg);
                if (n_seg < 2) n_seg = 2;
                int   pts_cap = (n_seg + 1) * 2 + 2;
                POINT *pts = (POINT *)HeapAlloc(GetProcessHeap(), 0,
                                                 sizeof(POINT) * (size_t)pts_cap);
                int   n_pts = 0;
                if (pts) {
                    /* Inner arc, az_lo -> az_hi (sweep clockwise) */
                    for (int s = 0; s <= n_seg; s++) {
                        double t = (double)s / (double)n_seg;
                        double az = az_lo + t * (az_hi - az_lo);
                        double az_rad = az * M_PI / 180.0;
                        pts[n_pts].x = cx + (int)(r_inner * sin(az_rad) + 0.5);
                        pts[n_pts].y = cy - (int)(r_inner * cos(az_rad) + 0.5);
                        n_pts++;
                    }
                    /* Outer arc, az_hi -> az_lo */
                    for (int s = n_seg; s >= 0; s--) {
                        double t = (double)s / (double)n_seg;
                        double az = az_lo + t * (az_hi - az_lo);
                        double az_rad = az * M_PI / 180.0;
                        pts[n_pts].x = cx + (int)(r_outer * sin(az_rad) + 0.5);
                        pts[n_pts].y = cy - (int)(r_outer * cos(az_rad) + 0.5);
                        n_pts++;
                    }
                    Polygon(hdc, pts, n_pts);
                    HeapFree(GetProcessHeap(), 0, pts);
                }
            }

            SelectObject(hdc, brOld);
            DeleteObject(br);
        }
    }

    SelectObject(hdc, penOld);
    DeleteObject(penEdge);
}

/**
 * @brief Blend a colour toward a dim grey based on CNR strength.
 *
 * CNR 45+ dB-Hz returns the fresh colour unchanged.  CNR 20 dB-Hz blends
 * roughly 80% toward grey (very dim).  CNR <= 0 means "unknown" -- return
 * fresh, so SVs without CNR data don't appear artificially dim.
 */
static COLORREF cnr_shade(COLORREF fresh, float cnr_dbhz)
{
    if (cnr_dbhz <= 0.0f) return fresh;

    /* Map CNR [20..45] dB-Hz to brightness [0.2 .. 1.0]. */
    float t = (cnr_dbhz - 20.0f) / 25.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float brightness = 0.2f + 0.8f * t;

    int r = GetRValue(fresh);
    int g = GetGValue(fresh);
    int b = GetBValue(fresh);
    const int grey = 200;
    int blended_r = (int)((1.0f - brightness) * grey + brightness * r);
    int blended_g = (int)((1.0f - brightness) * grey + brightness * g);
    int blended_b = (int)((1.0f - brightness) * grey + brightness * b);
    return RGB(blended_r, blended_g, blended_b);
}

/**
 * @brief Draw all tracked satellites onto the polar grid.
 *
 * Reads SkyPlotState from the AppState, projects each (az, el) to pixels
 * using radius = (90 - el)/90 * outer_radius and angle = az clockwise
 * from north, and draws a coloured disc + PRN label per SV.  Stale
 * markers (>SKY_STALE_S since last update) are drawn grey; otherwise
 * the fresh GNSS colour is shaded toward grey by the SV's CNR.
 */
static int DrawSkyMarkers(HDC hdc, const AppState *state,
                          int cx, int cy, int radius)
{
    int drawn = 0;
    double now = gui_get_time_seconds();

    HPEN penOutline = CreatePen(PS_SOLID, 1, SKY_MARKER_OUTLINE);
    HPEN penOld     = (HPEN)SelectObject(hdc, penOutline);
    HBRUSH brOld    = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

    SetBkMode(hdc, TRANSPARENT);
    SetTextAlign(hdc, TA_LEFT | TA_TOP);

    for (int g = 0; g < SV_EPH_MAX_GNSS; g++) {
        char sys;
        COLORREF color_fresh;
        switch (g) {
        case 1: sys = 'G'; color_fresh = SKY_GPS_COLOR; break;
        case 2: sys = 'R'; color_fresh = SKY_GLO_COLOR; break;
        case 3: sys = 'E'; color_fresh = SKY_GAL_COLOR; break;
        case 4: sys = 'J'; color_fresh = SKY_QZS_COLOR; break;
        case 5: sys = 'C'; color_fresh = SKY_BDS_COLOR; break;
        default: continue;
        }

        /* Trail dots: keep the GNSS hue but lighten ~30% toward white so
         * they read as "history" without competing visually with the live
         * marker. */
        int tr_r = (GetRValue(color_fresh) * 7 + 255 * 3) / 10;
        int tr_g = (GetGValue(color_fresh) * 7 + 255 * 3) / 10;
        int tr_b = (GetBValue(color_fresh) * 7 + 255 * 3) / 10;
        COLORREF trail_color = RGB(tr_r, tr_g, tr_b);

        for (int p = 1; p <= SV_EPH_MAX_SATS_PER_GNSS; p++) {
            const SkySat *s = &state->skyState.sats[g][p - 1];
            if (!s->valid) continue;

            double age = now - s->last_seen_ts;
            if (age > SKY_DROP_S) continue;

            /* ── Track trail (since stream open / last reset) ──
             * Walk oldest->newest, draw a 3x3 dot at each historical (az,el)
             * in a desaturated GNSS colour so it doesn't compete with the
             * live marker. */
            if (s->track.count > 0) {
                HBRUSH brTrail = CreateSolidBrush(trail_color);
                HBRUSH oldB = (HBRUSH)SelectObject(hdc, brTrail);
                HPEN   penNull = (HPEN)GetStockObject(NULL_PEN);
                HPEN   oldP = (HPEN)SelectObject(hdc, penNull);

                int start_idx = (s->track.count < SKY_TRACK_CAP)
                                ? 0
                                : s->track.head;
                for (int i = 0; i < s->track.count; i++) {
                    int idx = (start_idx + i) % SKY_TRACK_CAP;
                    const SkyTrackPoint *tp = &s->track.pts[idx];
                    double az_r = tp->az_deg * M_PI / 180.0;
                    double r_t  = (90.0 - tp->el_deg) / 90.0 * (double)radius;
                    int tx = cx + (int)(r_t * sin(az_r) + 0.5);
                    int ty = cy - (int)(r_t * cos(az_r) + 0.5);
                    /* 5x5 px dot for clear visibility */
                    Ellipse(hdc, tx - 2, ty - 2, tx + 3, ty + 3);
                }
                SelectObject(hdc, oldP);
                SelectObject(hdc, oldB);
                DeleteObject(brTrail);
            }

            COLORREF c = (age > SKY_STALE_S)
                         ? SKY_DIM_COLOR
                         : cnr_shade(color_fresh, s->cnr_dbhz);

            /* Polar -> Cartesian.  Y axis is flipped for screen coords. */
            double az_rad = s->az_deg * M_PI / 180.0;
            double r_px   = (90.0 - s->el_deg) / 90.0 * (double)radius;
            int x = cx + (int)(r_px * sin(az_rad) + 0.5);
            int y = cy - (int)(r_px * cos(az_rad) + 0.5);

            HBRUSH br = CreateSolidBrush(c);
            HBRUSH oldFill = (HBRUSH)SelectObject(hdc, br);
            Ellipse(hdc, x - 5, y - 5, x + 6, y + 6);
            SelectObject(hdc, oldFill);
            DeleteObject(br);

            /* PRN label */
            char id[8];
            snprintf(id, sizeof(id), "%c%02d", sys, p);
            SetTextColor(hdc, SKY_LABEL_COLOR);
            TextOut(hdc, x + 7, y - 8, id, (int)strlen(id));

            drawn++;
        }
    }

    SelectObject(hdc, penOld);
    SelectObject(hdc, brOld);
    DeleteObject(penOutline);

    return drawn;
}

/**
 * @brief Hit-test the click point against currently drawn SV markers.
 *
 * Walks skyState.sats[][] applying the same age filter the paint code
 * uses and finds the marker closest to (@p mx, @p my) within @p tol_px
 * pixels.  Returns the (gnss, prn) pair via out params; (0, 0) if no hit.
 *
 * Only meaningful in marker mode -- heatmap mode has no per-SV markers
 * to click.
 */
static BOOL sky_hit_test(const AppState *state, int w, int h,
                         int mx, int my, double tol_px,
                         int *out_g, int *out_p)
{
    if (!state) return FALSE;
    if (state->skyState.mode != SKY_MODE_MARKERS) return FALSE;

    int cx, cy, radius;
    sky_compute_geometry(w, h, &cx, &cy, &radius);

    double now = gui_get_time_seconds();
    double best_dist2 = tol_px * tol_px;
    int    best_g = 0, best_p = 0;

    for (int g = 0; g < SV_EPH_MAX_GNSS; g++) {
        if (g != 1 && g != 2 && g != 3 && g != 4 && g != 5) continue;
        for (int p = 1; p <= SV_EPH_MAX_SATS_PER_GNSS; p++) {
            const SkySat *s = &state->skyState.sats[g][p - 1];
            if (!s->valid) continue;
            if ((now - s->last_seen_ts) > SKY_DROP_S) continue;

            double az_rad = s->az_deg * M_PI / 180.0;
            double r_px   = (90.0 - s->el_deg) / 90.0 * (double)radius;
            double sx = (double)cx + r_px * sin(az_rad);
            double sy = (double)cy - r_px * cos(az_rad);
            double dx = (double)mx - sx;
            double dy = (double)my - sy;
            double d2 = dx*dx + dy*dy;
            if (d2 <= best_dist2) {
                best_dist2 = d2;
                best_g = g;
                best_p = p;
            }
        }
    }

    if (best_g == 0) return FALSE;
    if (out_g) *out_g = best_g;
    if (out_p) *out_p = best_p;
    return TRUE;
}

/**
 * @brief Prompt the user for a PNG path and save the sky-window snapshot.
 *
 * Logs success / failure to the main window's log via state->hEditLog.
 * Returns TRUE on success.  Public surface declared in gui_sky_window.h.
 */
BOOL SkySavePngWithPrompt(HWND hSky, AppState *state)
{
    /* Build a default filename of the form
     *   YYYYMMDDHHmmss_<mode>.png
     * where <mode> is "TrackedSats" for the marker view or "ARP-EPG" for
     * the observed/expected heatmap view. */
    char filename[MAX_PATH];
    {
        time_t now_t = time(NULL);
        struct tm *lt = localtime(&now_t);
        char ts[16] = "00000000000000";
        if (lt) strftime(ts, sizeof(ts), "%Y%m%d%H%M%S", lt);

        const char *mode_suffix = "TrackedSats";
        if (state && state->skyState.mode == SKY_MODE_HEATMAP)
            mode_suffix = "ARP-EPG";

        snprintf(filename, sizeof(filename), "%s_%s.png", ts, mode_suffix);
    }

    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = hSky;
    ofn.lpstrFilter  = "PNG Image (*.png)\0*.png\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile    = filename;
    ofn.nMaxFile     = MAX_PATH;
    ofn.lpstrTitle   = "Save Sky Plot as PNG";
    ofn.Flags        = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt  = "png";
    if (!GetSaveFileNameA(&ofn))
        return FALSE;   /* user cancelled */

    BOOL ok = save_window_as_png(hSky, filename);

    if (state && state->hEditLog) {
        char msg[MAX_PATH + 64];
        if (ok) snprintf(msg, sizeof(msg),
                         "[INFO] Sky plot saved to %s\r\n", filename);
        else    snprintf(msg, sizeof(msg),
                         "[ERROR] Failed to save sky plot to %s\r\n", filename);
        int len = GetWindowTextLength(state->hEditLog);
        SendMessage(state->hEditLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
        SendMessage(state->hEditLog, EM_REPLACESEL, FALSE, (LPARAM)msg);
    }
    return ok;
}

static LRESULT CALLBACK SkyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)wParam;

    switch (msg) {

    case WM_CREATE: {
        CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        /* 1-Hz heartbeat for the footer clock.  Repaints the whole window
         * — double-buffered draw keeps it cheap. */
        SetTimer(hwnd, IDT_SKY_CLOCK, 1000, NULL);
        return 0;
    }

    case WM_TIMER:
        if (wParam == IDT_SKY_CLOCK)
            InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_ERASEBKGND:
        /* Skip — WM_PAINT fully covers the client via a double buffer */
        return 1;

    case WM_PAINT: {
        AppState *state = (AppState *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

        PAINTSTRUCT ps;
        HDC hdcScreen = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        HDC     hdcMem  = CreateCompatibleDC(hdcScreen);
        HBITMAP bmpMem  = CreateCompatibleBitmap(hdcScreen, w, h);
        HBITMAP bmpOld  = (HBITMAP)SelectObject(hdcMem, bmpMem);
        HFONT   hFont   = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HFONT   hFontOld = (HFONT)SelectObject(hdcMem, hFont);

        int cx = 0, cy = 0, radius = 0;
        DrawSkyFill(hdcMem, w, h, &cx, &cy, &radius);

        /* Heatmap goes between background fill and foreground grid so the
         * compass rose, rings and labels stay legible. */
        if (state && state->skyState.mode == SKY_MODE_HEATMAP)
            DrawSkyHeatmap(hdcMem, state, cx, cy, radius);

        DrawSkyGrid(hdcMem, cx, cy, radius);

        int drawn = (state && state->skyState.mode == SKY_MODE_MARKERS)
                    ? DrawSkyMarkers(hdcMem, state, cx, cy, radius)
                    : 0;

        /* ── Diagnostic status line ────────────────────────────────
         * Distinguishes the two prerequisites so the user knows what
         * to look for in their RTCM stream. */
        bool   have_arp = false;
        double arp_lat = 0, arp_lon = 0;
        rtcm_get_station_arp(&have_arp, NULL, NULL, NULL,
                             &arp_lat, &arp_lon, NULL);

        int eph_gps = 0, eph_glo = 0, eph_gal = 0, eph_qzs = 0, eph_bds = 0;
        for (int p = 1; p <= SV_EPH_MAX_SATS_PER_GNSS; p++) {
            if (sv_eph_get(1, p)) eph_gps++;
            if (sv_eph_get(2, p)) eph_glo++;
            if (sv_eph_get(3, p)) eph_gal++;
            if (sv_eph_get(4, p)) eph_qzs++;
            if (sv_eph_get(5, p)) eph_bds++;
        }
        int eph_total = eph_gps + eph_glo + eph_gal + eph_qzs + eph_bds;

        SetTextAlign(hdcMem, TA_LEFT | TA_TOP);
        SetTextColor(hdcMem, SKY_STATUS_COLOR);

        char line1[160];
        if (drawn > 0) {
            snprintf(line1, sizeof(line1),
                     "Tracking %d sat%s  (eph G:%d R:%d E:%d J:%d C:%d)",
                     drawn, drawn == 1 ? "" : "s",
                     eph_gps, eph_glo, eph_gal, eph_qzs, eph_bds);
        } else if (!have_arp && eph_total == 0) {
            snprintf(line1, sizeof(line1),
                     "Waiting: no RTCM 1005/1006 + no 1019/1020/1042/1044/1045 yet");
        } else if (!have_arp) {
            snprintf(line1, sizeof(line1),
                     "Have %d eph (G:%d R:%d E:%d J:%d C:%d); need RTCM 1005/1006",
                     eph_total, eph_gps, eph_glo, eph_gal, eph_qzs, eph_bds);
        } else if (eph_total == 0) {
            snprintf(line1, sizeof(line1),
                     "Have station ARP; need RTCM 1019/1020/1042/1044/1045/1046");
        } else {
            snprintf(line1, sizeof(line1),
                     "ARP + eph G:%d R:%d E:%d J:%d C:%d; awaiting next MSM frame",
                     eph_gps, eph_glo, eph_gal, eph_qzs, eph_bds);
        }
        TextOut(hdcMem, 8, 6, line1, (int)strlen(line1));

        /* Second line: mode + hint + keyboard / mouse shortcuts */
        const char *mode_label =
            (state && state->skyState.mode == SKY_MODE_HEATMAP)
                ? "Mode: Heatmap (observed/expected per sector)  [M=toggle  S=save]"
                : "Mode: Live SVs (brightness ~ CNR; click SV for detail)  [M=toggle  S=save]";
        TextOut(hdcMem, 8, 22, mode_label, (int)strlen(mode_label));

        /* Third line: mode-specific colour legend */
        SkyPlotMode legend_mode =
            (state) ? state->skyState.mode : SKY_MODE_MARKERS;
        DrawSkyLegend(hdcMem, 8, 38, legend_mode);

        /* Footer:  live local time on the left, station identity on the right.
         * The right side shows the mountpoint + ARP coords so PNG snapshots
         * make it obvious which station the plot is centred on. */
        {
            time_t now_t = time(NULL);
            struct tm *lt = localtime(&now_t);
            char tbuf[40] = "";
            if (lt)
                strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S local", lt);
            SetTextAlign(hdcMem, TA_LEFT | TA_BOTTOM);
            SetTextColor(hdcMem, SKY_AXIS_COLOR);
            TextOut(hdcMem, 8, h - 6, tbuf, (int)strlen(tbuf));

            /* Right side: "MountPoint | lat lon alt"  */
            bool   have_arp = false;
            double lat = 0, lon = 0, alt = 0;
            rtcm_get_station_arp(&have_arp, NULL, NULL, NULL, &lat, &lon, &alt);

            const char *mp = (state) ? state->config.MOUNTPOINT : "";
            if (!mp) mp = "";

            char rbuf[512];   /* MOUNTPOINT is up to 256 bytes */
            if (have_arp) {
                snprintf(rbuf, sizeof(rbuf),
                         "%s   ARP: %.6f, %.6f, %.1f m",
                         mp[0] ? mp : "(none)", lat, lon, alt);
            } else if (mp[0]) {
                snprintf(rbuf, sizeof(rbuf),
                         "%s   ARP: (waiting for RTCM 1005/1006)", mp);
            } else {
                rbuf[0] = '\0';
            }

            if (rbuf[0]) {
                SetTextAlign(hdcMem, TA_RIGHT | TA_BOTTOM);
                TextOut(hdcMem, w - 8, h - 6, rbuf, (int)strlen(rbuf));
            }

            /* restore default alignment so subsequent draws are predictable */
            SetTextAlign(hdcMem, TA_LEFT | TA_TOP);
        }

        BitBlt(hdcScreen, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);

        SelectObject(hdcMem, hFontOld);
        SelectObject(hdcMem, bmpOld);
        DeleteObject(bmpMem);
        DeleteDC(hdcMem);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONDOWN: {
        AppState *state = (AppState *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (!state) break;

        RECT rc;
        GetClientRect(hwnd, &rc);

        int mx = (short)LOWORD(lParam);
        int my = (short)HIWORD(lParam);
        int g = 0, p = 0;
        /* 14 px tolerance so clicks on the PRN label also count
         * (label sits ~7-12 px from the marker centre). */
        if (sky_hit_test(state, rc.right - rc.left, rc.bottom - rc.top,
                         mx, my, 14.0, &g, &p)) {
            HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd,
                                                          GWLP_HINSTANCE);
            ShowSkySvDetailWindow(hInst, hwnd, state, g, p);
        }
        return 0;
    }

    case WM_KEYDOWN: {
        AppState *state = (AppState *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (!state) break;
        /* M (or Space) toggles SV-markers <-> heatmap. */
        if (wParam == 'M' || wParam == VK_SPACE) {
            state->skyState.mode = (state->skyState.mode == SKY_MODE_MARKERS)
                                   ? SKY_MODE_HEATMAP : SKY_MODE_MARKERS;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        /* S = save snapshot as PNG */
        if (wParam == 'S') {
            SkySavePngWithPrompt(hwnd, state);
            return 0;
        }
        break;
    }

    case WM_DESTROY: {
        KillTimer(hwnd, IDT_SKY_CLOCK);
        AppState *state = (AppState *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (state && state->hSkyWnd == hwnd) {
            /* Capture the un-minimised screen rect so the next open
             * restores the same size and position.  WS_EX_TOOLWINDOW
             * makes rcNormalPosition use screen (not workspace) coords. */
            WINDOWPLACEMENT wp;
            ZeroMemory(&wp, sizeof(wp));
            wp.length = sizeof(wp);
            if (GetWindowPlacement(hwnd, &wp)) {
                state->skyWndRect      = wp.rcNormalPosition;
                state->skyWndRectValid = TRUE;
            }
            state->hSkyWnd = NULL;
        }
        return 0;
    }
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

BOOL RegisterSkyWindowClass(HINSTANCE hInst)
{
    static BOOL registered = FALSE;
    if (registered) return TRUE;

    WNDCLASSEX wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = SkyWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;            /* we paint everything ourselves */
    wc.lpszClassName = SKY_WINDOW_CLASS;
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassEx(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return FALSE;
    }
    registered = TRUE;
    return TRUE;
}

HWND CreateSkyWindow(HINSTANCE hInst, HWND hOwner, AppState *state)
{
    if (!RegisterSkyWindowClass(hInst)) return NULL;

    /* Restore the previous size and position if we have one; otherwise
     * use the SKY_WIN_DEF_* default that gives the rose enough breathing
     * room for the rings, legend and SV labels. */
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    int w = SKY_WIN_DEF_W,  h = SKY_WIN_DEF_H;
    if (state && state->skyWndRectValid) {
        x = state->skyWndRect.left;
        y = state->skyWndRect.top;
        w = state->skyWndRect.right  - state->skyWndRect.left;
        h = state->skyWndRect.bottom - state->skyWndRect.top;
        /* Guard against zero/negative sizes from a corrupt rect */
        if (w < 120) w = SKY_WIN_DEF_W;
        if (h < 120) h = SKY_WIN_DEF_H;
    }

    return CreateWindowEx(
        WS_EX_TOOLWINDOW,
        SKY_WINDOW_CLASS,
        "Sky Plot - NTRIP-Analyser",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME |
            WS_MINIMIZEBOX | WS_VISIBLE,
        x, y, w, h,
        hOwner, NULL, hInst, state);
}
