/**
 * @file sky_render.c
 * @brief Portable polar sky-heatmap renderer + minimal PNG encoder.
 *
 * Produces a PNG of the same sector heatmap the GUI draws (gui_sky_window.c
 * SKY_MODE_HEATMAP).  No external dependencies: the file embeds a small
 * PNG writer (PNG chunks + CRC32 + Adler32 + zlib stored-deflate blocks),
 * which is enough for the modest size of a sky-plot snapshot.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "sky_render.h"

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ── Geometry: must mirror gui_state.h sky_az_bins_per_band[] ───────── */
const int sky_render_az_bins_per_band[SKY_RENDER_N_EL_BANDS] = {
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

/* ── RGB pixel helper ───────────────────────────────────────────────── */
typedef struct {
    uint8_t *pixels;   /* row-major: pixels[y*stride + x*3] = R,G,B */
    int      w, h;
    int      stride;   /* bytes per row = w*3 */
} RGB;

static void put_px(RGB *img, int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    if (x < 0 || x >= img->w || y < 0 || y >= img->h) return;
    uint8_t *p = img->pixels + y * img->stride + x * 3;
    p[0] = r; p[1] = g; p[2] = b;
}

static void fill_rect(RGB *img, int x0, int y0, int x1, int y1,
                      uint8_t r, uint8_t g, uint8_t b)
{
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= img->w) x1 = img->w - 1;
    if (y1 >= img->h) y1 = img->h - 1;
    for (int y = y0; y <= y1; y++) {
        uint8_t *row = img->pixels + y * img->stride + x0 * 3;
        for (int x = x0; x <= x1; x++) {
            row[0] = r; row[1] = g; row[2] = b;
            row += 3;
        }
    }
}

static void fill_all(RGB *img, uint8_t r, uint8_t g, uint8_t b)
{
    fill_rect(img, 0, 0, img->w - 1, img->h - 1, r, g, b);
}

/* Bresenham line, 1-px wide. */
static void draw_line(RGB *img, int x0, int y0, int x1, int y1,
                      uint8_t r, uint8_t g, uint8_t b)
{
    int dx =  abs(x1 - x0), sx = (x0 < x1) ? 1 : -1;
    int dy = -abs(y1 - y0), sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        put_px(img, x0, y0, r, g, b);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* Bresenham midpoint circle, 1-px wide. */
static void draw_circle(RGB *img, int cx, int cy, int radius,
                        uint8_t r, uint8_t g, uint8_t b)
{
    int x = radius, y = 0;
    int err = 1 - radius;
    while (x >= y) {
        put_px(img, cx + x, cy + y, r, g, b);
        put_px(img, cx + y, cy + x, r, g, b);
        put_px(img, cx - y, cy + x, r, g, b);
        put_px(img, cx - x, cy + y, r, g, b);
        put_px(img, cx - x, cy - y, r, g, b);
        put_px(img, cx - y, cy - x, r, g, b);
        put_px(img, cx + y, cy - x, r, g, b);
        put_px(img, cx + x, cy - y, r, g, b);
        y++;
        if (err < 0) err += 2 * y + 1;
        else        { x--; err += 2 * (y - x) + 1; }
    }
}

/* ── 5x7 bitmap font (subset of printable ASCII) ────────────────────────
 * Each glyph: 7 bytes, one per row.  Bits 4..0 hold the 5 columns left-
 * to-right.  Missing chars render as a blank space. */
typedef struct { char c; uint8_t rows[7]; } Glyph;

static const Glyph g_font[] = {
    {' ', {0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
    {'!', {0x04,0x04,0x04,0x04,0x00,0x00,0x04}},
    {'(', {0x02,0x04,0x08,0x08,0x08,0x04,0x02}},
    {')', {0x08,0x04,0x02,0x02,0x02,0x04,0x08}},
    {'+', {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}},
    {',', {0x00,0x00,0x00,0x00,0x00,0x04,0x08}},
    {'-', {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}},
    {'.', {0x00,0x00,0x00,0x00,0x00,0x00,0x04}},
    {'/', {0x01,0x02,0x02,0x04,0x08,0x08,0x10}},
    {'0', {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}},
    {'1', {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
    {'2', {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}},
    {'3', {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E}},
    {'4', {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}},
    {'5', {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}},
    {'6', {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}},
    {'7', {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
    {'8', {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}},
    {'9', {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}},
    {':', {0x00,0x04,0x00,0x00,0x00,0x04,0x00}},
    {'A', {0x0E,0x11,0x11,0x11,0x1F,0x11,0x11}},
    {'B', {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
    {'C', {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}},
    {'D', {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}},
    {'E', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}},
    {'F', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
    {'G', {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}},
    {'H', {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'I', {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}},
    {'J', {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}},
    {'K', {0x11,0x12,0x14,0x18,0x14,0x12,0x11}},
    {'L', {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
    {'M', {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}},
    {'N', {0x11,0x11,0x19,0x15,0x13,0x11,0x11}},
    {'O', {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}},
    {'P', {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
    {'Q', {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}},
    {'R', {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
    {'S', {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}},
    {'T', {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
    {'U', {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}},
    {'V', {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
    {'W', {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}},
    {'X', {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
    {'Y', {0x11,0x11,0x11,0x0A,0x04,0x04,0x04}},
    {'Z', {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
};

static const Glyph *find_glyph(char c)
{
    /* Treat lowercase as uppercase for lookup. */
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    const int n = (int)(sizeof(g_font) / sizeof(g_font[0]));
    for (int i = 0; i < n; i++)
        if (g_font[i].c == c) return &g_font[i];
    return NULL;
}

/* Draw a single 5x7 glyph at (x, y) (top-left). */
static void draw_glyph(RGB *img, int x, int y, char c,
                       uint8_t r, uint8_t g, uint8_t b)
{
    const Glyph *gl = find_glyph(c);
    if (!gl) return;
    for (int row = 0; row < 7; row++) {
        uint8_t bits = gl->rows[row];
        for (int col = 0; col < 5; col++) {
            if (bits & (1 << (4 - col)))
                put_px(img, x + col, y + row, r, g, b);
        }
    }
}

/* Draw a string left-aligned at (x, y).  Returns the x past the last glyph. */
static int draw_text(RGB *img, int x, int y, const char *s,
                     uint8_t r, uint8_t g, uint8_t b)
{
    if (!s) return x;
    for (; *s; s++) {
        draw_glyph(img, x, y, *s, r, g, b);
        x += 6;     /* 5 px glyph + 1 px tracking */
    }
    return x;
}

/* Compute the rendered width of a string in pixels (used for right-align). */
static int text_width(const char *s)
{
    if (!s) return 0;
    return 6 * (int)strlen(s);
}

static int draw_text_right(RGB *img, int xr, int y, const char *s,
                           uint8_t r, uint8_t g, uint8_t b)
{
    int w = text_width(s);
    int x = xr - w;
    draw_text(img, x, y, s, r, g, b);
    return x;
}

/* ── Sector colour ramp (matches gui_sky_window.c heatmap_color) ─────── */
static void heatmap_color(int observed, int expected,
                          uint8_t *out_r, uint8_t *out_g, uint8_t *out_b)
{
    if (expected <= 0) {
        /* "no eph" / polar hole — light grey */
        *out_r = 240; *out_g = 240; *out_b = 240;
        return;
    }
    double t = (double)observed / (double)expected;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    int r, g, b;
    if (t < 0.5) {
        /* red -> yellow */
        double s = t / 0.5;
        r = (int)(220 + s * (220 - 220));
        g = (int)( 70 + s * (220 -  70));
        b = (int)( 70 + s * ( 70 -  70));
    } else {
        /* yellow -> green */
        double s = (t - 0.5) / 0.5;
        r = (int)(220 + s * ( 70 - 220));
        g = (int)(220 + s * (170 - 220));
        b = (int)( 70 + s * ( 70 -  70));
    }
    *out_r = (uint8_t)r;
    *out_g = (uint8_t)g;
    *out_b = (uint8_t)b;
}

/* ── Heatmap rasterizer ──────────────────────────────────────────────── */
static void render_heatmap_disc(RGB *img, int cx, int cy, int radius,
                                const SkyRenderSector *sectors)
{
    /* Walk the bounding square; for each pixel inside the disc compute
     * polar (r_norm, az_deg) and look up the sector. */
    int x0 = cx - radius, x1 = cx + radius;
    int y0 = cy - radius, y1 = cy + radius;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= img->w) x1 = img->w - 1;
    if (y1 >= img->h) y1 = img->h - 1;

    double R = (double)radius;
    for (int y = y0; y <= y1; y++) {
        double dy = (double)y - (double)cy;
        for (int x = x0; x <= x1; x++) {
            double dx = (double)x - (double)cx;
            double r_pix = sqrt(dx * dx + dy * dy);
            if (r_pix > R) continue;       /* outside disc */
            /* r_norm = 0 at zenith, 1 at horizon — same convention as the
             * GUI: pixel-radius = (90 - el) / 90 * R. */
            double el_deg = 90.0 - (r_pix / R) * 90.0;
            if (el_deg < 0.0) el_deg = 0.0;
            if (el_deg > 90.0) el_deg = 90.0;
            int band = (int)(el_deg / 10.0);
            if (band >= SKY_RENDER_N_EL_BANDS) band = SKY_RENDER_N_EL_BANDS - 1;

            int n_bins = sky_render_az_bins_per_band[band];
            int bin = 0;
            if (n_bins > 1) {
                /* az = atan2(dx, -dy) so 0=N, +90=E, ±180=S, -90=W. */
                double az_rad = atan2(dx, -dy);
                double az_deg = az_rad * 180.0 / M_PI;
                if (az_deg < 0.0) az_deg += 360.0;
                bin = (int)floor(az_deg * (double)n_bins / 360.0);
                if (bin < 0) bin = 0;
                if (bin >= n_bins) bin = n_bins - 1;
            }
            const SkyRenderSector *s = &sectors[band * SKY_RENDER_MAX_AZ_BINS + bin];
            uint8_t r, g, b;
            heatmap_color(s->observed, s->expected, &r, &g, &b);
            put_px(img, x, y, r, g, b);
        }
    }
}

/* Draw rings (0,15,30,45,60,75 deg elevation), the N-S / E-W axes, and
 * the per-ring elevation-in-degrees labels alongside the N axis. */
static void draw_compass_rose(RGB *img, int cx, int cy, int radius)
{
    const uint8_t rg = 140, gg = 140, bg = 140;   /* ring grey */

    int el_lines[] = {0, 15, 30, 45, 60, 75};
    for (int i = 0; i < 6; i++) {
        int r_pix = (int)(((90.0 - (double)el_lines[i]) / 90.0) * (double)radius + 0.5);
        if (r_pix < 1) continue;
        draw_circle(img, cx, cy, r_pix, rg, gg, bg);
    }

    /* Dotted axes — sample every 3rd pixel. */
    for (int t = -radius; t <= radius; t += 3) {
        put_px(img, cx + t, cy, 100, 100, 100);
        put_px(img, cx, cy + t, 100, 100, 100);
    }

    /* Elevation labels in degrees, placed just right of the N axis where
     * each ring crosses it.  Skip the 0 deg ring -- it sits at the very
     * edge and the "N" compass label already lives there.  Drawn in axis
     * grey so they read as scale rather than as data. */
    for (int i = 1; i < 6; i++) {
        int r_pix = (int)(((90.0 - (double)el_lines[i]) / 90.0) * (double)radius + 0.5);
        if (r_pix < 4) continue;
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", el_lines[i]);
        /* Glyph is 7 px tall; place top edge ~2 px above the ring so the
         * text sits cleanly outside the line. */
        draw_text(img, cx + 3, cy - r_pix - 9, buf, 100, 100, 100);
    }
}

static void draw_axis_labels(RGB *img, int cx, int cy, int radius)
{
    const uint8_t r = 0, g = 0, b = 0;
    /* N: above ring */
    draw_text(img, cx - 2, cy - radius - 10, "N", r, g, b);
    /* S: below ring */
    draw_text(img, cx - 2, cy + radius + 3,  "S", r, g, b);
    /* E: right of ring */
    draw_text(img, cx + radius + 4, cy - 3,  "E", r, g, b);
    /* W: left of ring */
    draw_text(img, cx - radius - 10, cy - 3, "W", r, g, b);
}

static void draw_legend_and_footer(RGB *img,
                                   bool have_arp,
                                   double lat, double lon, double alt,
                                   const char *mountpoint,
                                   const char *utc_label)
{
    /* Header: title at top-left */
    draw_text(img, 8, 8,
              "NTRIP-ANALYSER SKY HEATMAP (OBSERVED / EXPECTED)",
              0, 0, 0);

    /* Mini gradient bar in the top-right (0% .. 100%). */
    int bar_x = img->w - 230;
    int bar_y = 10;
    int bar_w = 160, bar_h = 8;
    for (int i = 0; i < bar_w; i++) {
        double t = (double)i / (double)(bar_w - 1);
        int observed = (int)(t * 1000);
        int expected = 1000;
        uint8_t cr, cg, cb;
        heatmap_color(observed, expected, &cr, &cg, &cb);
        for (int j = 0; j < bar_h; j++)
            put_px(img, bar_x + i, bar_y + j, cr, cg, cb);
    }
    /* Outline */
    draw_line(img, bar_x,           bar_y,         bar_x + bar_w, bar_y,         60, 60, 60);
    draw_line(img, bar_x,           bar_y + bar_h, bar_x + bar_w, bar_y + bar_h, 60, 60, 60);
    draw_line(img, bar_x,           bar_y,         bar_x,         bar_y + bar_h, 60, 60, 60);
    draw_line(img, bar_x + bar_w,   bar_y,         bar_x + bar_w, bar_y + bar_h, 60, 60, 60);
    draw_text(img, bar_x - 30,           bar_y, "0%",   0, 0, 0);
    draw_text(img, bar_x + bar_w + 6,    bar_y, "100%", 0, 0, 0);

    /* Footer (bottom 12 px reserved). */
    int foot_y = img->h - 12;

    if (utc_label && utc_label[0]) {
        draw_text(img, 8, foot_y, utc_label, 80, 80, 80);
    }

    /* Right-aligned: mountpoint + ARP block. */
    char rbuf[512];
    rbuf[0] = '\0';
    if (have_arp) {
        snprintf(rbuf, sizeof(rbuf),
                 "%s   ARP: %.6f, %.6f, %.1f M",
                 (mountpoint && mountpoint[0]) ? mountpoint : "(NONE)",
                 lat, lon, alt);
    } else if (mountpoint && mountpoint[0]) {
        snprintf(rbuf, sizeof(rbuf),
                 "%s   ARP: (WAITING FOR RTCM 1005/1006)",
                 mountpoint);
    }
    if (rbuf[0])
        draw_text_right(img, img->w - 8, foot_y, rbuf, 80, 80, 80);
}

/* ─────────────────────────────────────────────────────────────────────
 * Minimal PNG writer — no external dependencies.
 *
 * Uses zlib stored (uncompressed) deflate blocks, which always work and
 * are sufficient for an 800x800 heatmap snapshot (~1.9 MB on disk).
 * ───────────────────────────────────────────────────────────────────── */

static uint32_t g_crc_table[256];
static int      g_crc_init = 0;

static void crc_init(void)
{
    if (g_crc_init) return;
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
        g_crc_table[n] = c;
    }
    g_crc_init = 1;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *buf, size_t len)
{
    crc_init();
    crc ^= 0xFFFFFFFFU;
    for (size_t i = 0; i < len; i++)
        crc = g_crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFU;
}

static uint32_t adler32_buf(const uint8_t *buf, size_t len)
{
    uint32_t a = 1, b = 0;
    const uint32_t MOD = 65521;
    for (size_t i = 0; i < len; i++) {
        a = (a + buf[i]) % MOD;
        b = (b + a) % MOD;
    }
    return (b << 16) | a;
}

static void write_be32(FILE *f, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16),
                     (uint8_t)(v >> 8),  (uint8_t)(v) };
    fwrite(b, 1, 4, f);
}

static void write_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
    write_be32(f, len);
    fwrite(type, 1, 4, f);
    if (len) fwrite(data, 1, len, f);

    /* CRC over type + data. */
    uint32_t c = crc32_update(0, (const uint8_t *)type, 4);
    if (len) c = crc32_update(c, data, len);
    /* crc32_update finalises by XOR with 0xFFFFFFFF; we need a fresh start
     * because we passed crc=0 as the seed.  Re-do correctly: */
    {
        uint32_t crc = 0xFFFFFFFFU;
        crc_init();
        for (int i = 0; i < 4; i++)
            crc = g_crc_table[(crc ^ (uint8_t)type[i]) & 0xFF] ^ (crc >> 8);
        for (uint32_t i = 0; i < len; i++)
            crc = g_crc_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
        write_be32(f, crc ^ 0xFFFFFFFFU);
    }
    (void)c;
}

/* Build the IDAT payload: zlib stream of (filter=0 + scanline) per row,
 * encoded as one or more stored-deflate blocks. */
static uint8_t *build_idat(const RGB *img, size_t *out_len)
{
    /* Raw uncompressed image data: each row prefixed by filter byte 0. */
    size_t row_bytes = (size_t)img->w * 3;
    size_t raw_len   = (size_t)img->h * (1 + row_bytes);
    uint8_t *raw = (uint8_t *)malloc(raw_len);
    if (!raw) return NULL;

    {
        uint8_t *p = raw;
        for (int y = 0; y < img->h; y++) {
            *p++ = 0;   /* filter "None" */
            memcpy(p, img->pixels + (size_t)y * img->stride, row_bytes);
            p += row_bytes;
        }
    }

    /* zlib wrapper: 2-byte header + deflate stream + 4-byte Adler32 (BE).
     * Stored deflate overhead per block: 1 byte header + 4 bytes len/nlen
     * = 5 bytes.  At most 65535 raw bytes per block. */
    size_t n_blocks = (raw_len + 65534) / 65535;
    if (n_blocks == 0) n_blocks = 1;
    size_t zlib_len = 2 + n_blocks * 5 + raw_len + 4;
    uint8_t *zlib = (uint8_t *)malloc(zlib_len);
    if (!zlib) { free(raw); return NULL; }

    uint8_t *p = zlib;
    /* CMF + FLG -- CMF=0x78 (deflate, 32K window), FLG chosen so that
     * (CMF<<8 | FLG) % 31 == 0.  0x78 0x01 satisfies that. */
    *p++ = 0x78;
    *p++ = 0x01;

    size_t remain = raw_len;
    const uint8_t *src = raw;
    while (remain > 0) {
        size_t chunk = (remain > 65535) ? 65535 : remain;
        int is_last = (chunk == remain);
        *p++ = (uint8_t)(is_last ? 0x01 : 0x00);  /* BFINAL | BTYPE=00 */
        *p++ = (uint8_t)(chunk & 0xFF);
        *p++ = (uint8_t)((chunk >> 8) & 0xFF);
        uint16_t nlen = (uint16_t)~chunk;
        *p++ = (uint8_t)(nlen & 0xFF);
        *p++ = (uint8_t)((nlen >> 8) & 0xFF);
        memcpy(p, src, chunk);
        p += chunk;
        src += chunk;
        remain -= chunk;
    }

    /* Adler32 of the raw uncompressed data, big-endian. */
    uint32_t adl = adler32_buf(raw, raw_len);
    *p++ = (uint8_t)((adl >> 24) & 0xFF);
    *p++ = (uint8_t)((adl >> 16) & 0xFF);
    *p++ = (uint8_t)((adl >>  8) & 0xFF);
    *p++ = (uint8_t)( adl        & 0xFF);

    free(raw);

    *out_len = (size_t)(p - zlib);
    return zlib;
}

static bool write_png(const char *filename, const RGB *img)
{
    FILE *f = fopen(filename, "wb");
    if (!f) return false;

    static const uint8_t SIG[8] = { 0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A };
    fwrite(SIG, 1, 8, f);

    /* IHDR: width(4 BE) height(4 BE) depth=8 colorType=2 (RGB) comp=0 filt=0 interlace=0 */
    uint8_t ihdr[13];
    ihdr[0] = (uint8_t)((img->w >> 24) & 0xFF);
    ihdr[1] = (uint8_t)((img->w >> 16) & 0xFF);
    ihdr[2] = (uint8_t)((img->w >>  8) & 0xFF);
    ihdr[3] = (uint8_t)( img->w        & 0xFF);
    ihdr[4] = (uint8_t)((img->h >> 24) & 0xFF);
    ihdr[5] = (uint8_t)((img->h >> 16) & 0xFF);
    ihdr[6] = (uint8_t)((img->h >>  8) & 0xFF);
    ihdr[7] = (uint8_t)( img->h        & 0xFF);
    ihdr[8]  = 8;     /* bit depth */
    ihdr[9]  = 2;     /* color type: RGB */
    ihdr[10] = 0;     /* compression */
    ihdr[11] = 0;     /* filter */
    ihdr[12] = 0;     /* interlace */
    write_chunk(f, "IHDR", ihdr, sizeof(ihdr));

    size_t idat_len = 0;
    uint8_t *idat = build_idat(img, &idat_len);
    if (!idat) { fclose(f); return false; }
    write_chunk(f, "IDAT", idat, (uint32_t)idat_len);
    free(idat);

    write_chunk(f, "IEND", NULL, 0);

    fclose(f);
    return true;
}

/* ── Public entry point ─────────────────────────────────────────────── */
bool sky_render_heatmap_png(const char *filename,
                            const SkyRenderSector *sectors,
                            int  width, int height,
                            bool have_arp,
                            double arp_lat_deg,
                            double arp_lon_deg,
                            double arp_alt_m,
                            const char *mountpoint,
                            const char *utc_label)
{
    if (!filename || !sectors || width < 100 || height < 100) return false;

    RGB img;
    img.w = width;
    img.h = height;
    img.stride = width * 3;
    img.pixels = (uint8_t *)malloc((size_t)img.h * (size_t)img.stride);
    if (!img.pixels) return false;

    fill_all(&img, 255, 255, 255);

    /* Geometry: leave 80 px top margin (title + legend), 32 px elsewhere. */
    int margin_top   = 60;
    int margin_other = 32;
    int avail_w = width  - 2 * margin_other;
    int avail_h = height - margin_top - 60;  /* 60 px bottom (axis label + footer) */
    int diameter = (avail_w < avail_h) ? avail_w : avail_h;
    if (diameter < 100) diameter = 100;
    int cx = width  / 2;
    int cy = margin_top + diameter / 2;
    int radius = diameter / 2;

    render_heatmap_disc(&img, cx, cy, radius, sectors);
    draw_compass_rose(&img, cx, cy, radius);
    draw_axis_labels(&img, cx, cy, radius);
    draw_legend_and_footer(&img, have_arp, arp_lat_deg, arp_lon_deg,
                           arp_alt_m, mountpoint, utc_label);

    bool ok = write_png(filename, &img);
    free(img.pixels);
    return ok;
}
