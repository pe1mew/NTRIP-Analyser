/**
 * @file sky_render.h
 * @brief Portable polar sky-heatmap renderer and PNG writer.
 *
 * Generates the same Onocoy-style observed/expected sector heatmap as the
 * GUI's gui_sky_window.c (heatmap mode), but rendered into an in-memory
 * RGB buffer and serialised as a PNG file.  No GDI+ / no libpng / no zlib
 * dependency: the file embeds a minimal PNG encoder that uses stored
 * (uncompressed) deflate blocks, which is sufficient for the modest size
 * (~ a few MB at 800x800) of a sky-plot snapshot.
 *
 * Used by the CLI `-s` / `--sky` mode.  Geometry constants match
 * gui_state.h (SKY_N_EL_BANDS, sky_az_bins_per_band[]) so the GUI and
 * CLI produce visually-identical heatmaps.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#ifndef SKY_RENDER_H
#define SKY_RENDER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Elevation-band count: 9 bands of 10 deg each (0..10, 10..20, ..., 80..90). */
#define SKY_RENDER_N_EL_BANDS  9

/** Widest azimuth-bin count (horizon band). */
#define SKY_RENDER_MAX_AZ_BINS 33

/**
 * @brief Per-sector observation accumulator (compatible with GUI's SkySector).
 *
 * `observed` counts (epoch, SV) pairs where the SV was actually tracked
 * by the receiver in this sector; `expected` counts pairs where any
 * cached ephemeris placed an SV in this sector regardless of tracking.
 */
typedef struct {
    int observed;
    int expected;
} SkyRenderSector;

/** Azimuth-bin count per elevation band (must mirror gui_state.h). */
extern const int sky_render_az_bins_per_band[SKY_RENDER_N_EL_BANDS];

/**
 * @brief Save a sector-heatmap PNG to disk.
 *
 * @param filename     Path of the PNG file to create (overwrites if present).
 * @param sectors      Sector grid; indexed [band][bin], bin < sky_render_az_bins_per_band[band].
 * @param width        Output image width in pixels  (recommended 800-1024).
 * @param height       Output image height in pixels (recommended 800-1024).
 * @param have_arp     If true, footer prints ARP lat/lon/alt.
 * @param arp_lat_deg  ARP latitude (degrees), only used when have_arp is true.
 * @param arp_lon_deg  ARP longitude (degrees).
 * @param arp_alt_m    ARP altitude (metres ellipsoidal).
 * @param mountpoint   NTRIP mountpoint name for the footer (NULL or "" hides it).
 * @param utc_label    UTC timestamp string for the footer (e.g. "2026-05-29 18:42:01 UTC").
 * @return true on success, false on I/O or allocation failure.
 */
bool sky_render_heatmap_png(const char *filename,
                            const SkyRenderSector *sectors,
                            int  width, int height,
                            bool have_arp,
                            double arp_lat_deg,
                            double arp_lon_deg,
                            double arp_alt_m,
                            const char *mountpoint,
                            const char *utc_label);

#ifdef __cplusplus
}
#endif

#endif /* SKY_RENDER_H */
