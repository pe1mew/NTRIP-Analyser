/**
 * @file gui_mountmap.h
 * @brief View -> Mountpoint map: every mountpoint of the loaded
 *        sourcetable on a map, in the user's browser.
 *
 * design/work-items/mountpoint-map.md, M3. The map is somebody else's,
 * as the location picker's is (`OnMapPick`): the GUI writes a page to
 * `%TEMP%` and the browser fetches Leaflet and the tiles. Nothing here
 * is a measurement -- the data, each entry's kind and every escape come
 * from the core (`sourcetable_to_geojson`); this file only lays them
 * out.
 *
 * The page writer uses standard C I/O alone, so the very page the GUI
 * opens can be produced and measured outside it. The menu command that
 * calls it is `OnMountMap` in gui_events.c, beside `OnMapPick`.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#ifndef GUI_MOUNTMAP_H
#define GUI_MOUNTMAP_H

#include <stdio.h>

/**
 * @brief Write the mountpoint map page.
 *
 * The GeoJSON is embedded as a script literal. That is safe because the
 * core escapes `<`, `>`, `&` and the JavaScript line terminators in
 * every string; the page in turn never uses `innerHTML` on anything
 * that came from it, since a decoded string is raw text again.
 *
 * Leaflet is loaded from unpkg with Subresource Integrity, so a changed
 * file is refused by the browser rather than run.
 *
 * Under the stations: the user's tile source when @p tiles is given,
 * otherwise the built-in Natural Earth outlines, which the caller writes
 * beside the page as `ntrip_basemap.js`.
 *
 * @param f       Destination, opened for writing.
 * @param geojson Output of `sourcetable_to_geojson()`.
 * @param tiles   The tile source as a JSON object,
 *                `{"url":...,"attribution":...}`, its strings quoted by
 *                `map_json_quote()`; NULL for none.
 * @return 0 on success, -1 on a NULL argument or a write error.
 */
int MountMapWritePage(FILE *f, const char *geojson, const char *tiles);

/**
 * @brief Write the location picker page (the Map button).
 *
 * A click copies `lat,lon` to the clipboard for the GUI's "<<" button.
 * Without a tile source it opens over the outlines and says they cannot
 * show a mast, where it used to open a page of CARTO watermarks.
 *
 * @param f     Destination, opened for writing.
 * @param lat   Starting latitude, degrees; out of range or NaN becomes 0.
 * @param lon   Starting longitude, degrees; likewise.
 * @param tiles As for MountMapWritePage(); NULL for none.
 * @return 0 on success, -1 on a NULL file or a write error.
 */
int MapPickWritePage(FILE *f, double lat, double lon, const char *tiles);

#endif /* GUI_MOUNTMAP_H */
