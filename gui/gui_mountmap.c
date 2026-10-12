/**
 * @file gui_mountmap.c
 * @brief View -> Mountpoint map -- implementation.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "gui_mountmap.h"

#include <string.h>

/*
 * The page, around the data. Plain ASCII throughout: the only non-ASCII
 * text on the page is what the caster sent, and that arrives as JSON.
 *
 * Leaflet 1.9.4 with Subresource Integrity. The hashes were computed
 * from the files unpkg served on 2026-10-12 (openssl dgst -sha256 of
 * dist/leaflet.css and dist/leaflet.js); the location picker's page
 * (OnMapPick) carries the same pair.
 *
 * No tiles. The map underneath is Natural Earth 1:50m, public domain,
 * built into the program (tools/make_basemap.py, gui/basemap_ne50m.js)
 * and written beside this page as ntrip_basemap.js -- found 2026-10-12
 * that CARTO's tiles now need a key from everyone, and the OSM
 * Foundation's refuse a page opened from a file (no Referer). The only
 * thing fetched is Leaflet itself.
 */
static const char k_page_head[] =
    "<!DOCTYPE html>\n"
    "<html><head><meta charset=\"utf-8\">\n"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
    "<title>Mountpoint map - NTRIP-Analyser</title>\n"
    "<link rel=\"stylesheet\" href=\"https://unpkg.com/leaflet@1.9.4/dist/leaflet.css\"\n"
    "  integrity=\"sha256-p4NxAoJBhIIN+hmNHrzRCf9tD/miZyoHS5obTRR9BMY=\" crossorigin=\"\"/>\n"
    "<script src=\"https://unpkg.com/leaflet@1.9.4/dist/leaflet.js\"\n"
    "  integrity=\"sha256-20nQCchB9co0qIjJZRGuk2/Z9VM+kNiyxNV1lvTlZBo=\" crossorigin=\"\"></script>\n"
    "<style>\n"
    /* Flex, so the map has its size before Leaflet measures it: a map
     * placed below the bar by script was 0 px high when fitBounds ran,
     * and opened at maximum zoom on empty sea. */
    "  html,body{margin:0;height:100%;font-family:Segoe UI,sans-serif}\n"
    "  body{display:flex;flex-direction:column}\n"
    "  #bar{background:#333;color:#fff;padding:6px 12px;font-size:14px;"
    "display:flex;flex-wrap:wrap;gap:6px 18px;align-items:center}\n"
    "  #title{font-weight:600}\n"
    "  #note{color:#bbb;font-size:12px}\n"
    "  .key{display:inline-flex;align-items:center;gap:5px;margin-right:12px;font-size:13px}\n"
    "  .sw{display:inline-block;border-radius:50%;box-sizing:border-box}\n"
    "  #map{flex:1 1 auto;min-height:0;background:#cfe3ef}\n"
    "  .pop .mp{font-weight:600;font-size:15px}\n"
    "  .pop .id{color:#555;margin-bottom:4px}\n"
    "  .pop th{text-align:left;font-weight:normal;color:#666;padding-right:8px;vertical-align:top}\n"
    "  .pop td{max-width:260px;word-break:break-word}\n"
    "</style>\n"
    "</head><body>\n"
    "<div id=\"bar\"><span id=\"title\"></span><span id=\"counts\"></span>"
    "<span id=\"legend\"></span><span id=\"note\"></span></div>\n"
    "<div id=\"map\"></div>\n"
    "<script src=\"ntrip_basemap.js\"></script>\n"
    "<script>\n"
    "var data = ";

static const char k_page_tail[] =
    ";\n"
    "(function () {\n"
    "  var t0 = performance.now();\n"
    "  /* Colour and shape, so the kinds stay apart without colour. */\n"
    "  var KIND = {\n"
    "    fixed:   {label: 'Fixed base',       color: '#1d4ed8', fill: '#3b82f6', r: 6, fo: 0.9, w: 1},\n"
    "    network: {label: 'Network service',  color: '#c2410c', fill: '#c2410c', r: 7, fo: 0,   w: 3},\n"
    "    product: {label: 'Product, no station', color: '#6d28d9', fill: '#a78bfa', r: 4, fo: 0.9, w: 1},\n"
    "    unknown: {label: 'Unknown',          color: '#4b5563', fill: '#9ca3af', r: 6, fo: 0.5, w: 2, dash: '2,3'}\n"
    "  };\n"
    "  function kindOf(p) { return KIND.hasOwnProperty(p.kind) ? p.kind : 'unknown'; }\n"
    "  /* Every caster-supplied string goes through textContent. The JSON\n"
    "   * escaping protects the page source; once decoded, a name is raw\n"
    "   * text again and must never reach innerHTML. */\n"
    "  function el(tag, text, cls) {\n"
    "    var e = document.createElement(tag);\n"
    "    if (text !== null && text !== undefined) e.textContent = text;\n"
    "    if (cls) e.className = cls;\n"
    "    return e;\n"
    "  }\n"
    "  function fmtDist(m) {\n"
    "    if (m < 1000) return Math.round(m) + ' m';\n"
    "    return (m < 100000 ? (m / 1000).toFixed(2) : Math.round(m / 1000)) + ' km';\n"
    "  }\n"
    "  function popup(p, c) {\n"
    "    var d = el('div', null, 'pop');\n"
    "    d.appendChild(el('div', p.mountpoint, 'mp'));\n"
    "    if (p.identifier) d.appendChild(el('div', p.identifier, 'id'));\n"
    "    var tb = el('table');\n"
    "    function row(a, b) {\n"
    "      if (b === null || b === undefined || b === '') return;\n"
    "      var tr = el('tr'); tr.appendChild(el('th', a)); tr.appendChild(el('td', String(b)));\n"
    "      tb.appendChild(tr);\n"
    "    }\n"
    "    row('Kind', KIND[kindOf(p)].label + ' (' + p.kind_why + ')');\n"
    "    row('Network', p.network);\n"
    "    row('Format', p.format);\n"
    "    row('Systems', p.nav_systems);\n"
    "    row('Country', p.country);\n"
    "    row('GGA expected', p.nmea ? 'yes' : 'no');\n"
    "    row('Registered at', c[1].toFixed(6) + ', ' + c[0].toFixed(6));\n"
    "    if (p.distance_m !== null)\n"
    "      row('From your position', fmtDist(p.distance_m) + ' at ' +\n"
    "          (Math.round(p.bearing_deg) % 360) + '\\u00b0');\n"
    "    d.appendChild(tb);\n"
    "    return d;\n"
    "  }\n"
    "\n"
    "  /* The bar first: its height decides the map's, and Leaflet\n"
    "   * measures the map once, when it is made. */\n"
    "  var basemap = (typeof NE_BASEMAP !== 'undefined');\n"
    "  var counts = {fixed: 0, network: 0, product: 0, unknown: 0};\n"
    "  data.features.forEach(function (f) { counts[kindOf(f.properties)]++; });\n"
    "  var shown = data.features.length;\n"
    "  document.getElementById('title').textContent = data.caster + ':' + data.port;\n"
    "  document.title = data.caster + ' - Mountpoint map - NTRIP-Analyser';\n"
    "  document.getElementById('counts').textContent = shown + ' of ' + data.total +\n"
    "    ' mountpoints drawn' + (data.omitted ? '; ' + data.omitted +\n"
    "    ' have no usable position and are not drawn' : '');\n"
    "  var lg = document.getElementById('legend');\n"
    "  Object.keys(KIND).forEach(function (key) {\n"
    "    var k = KIND[key], s = el('span', null, 'key'), sw = el('span', null, 'sw');\n"
    "    var dia = 2 * k.r + 2;\n"
    "    sw.style.width = dia + 'px'; sw.style.height = dia + 'px';\n"
    "    sw.style.border = k.w + 'px ' + (k.dash ? 'dotted ' : 'solid ') + k.color;\n"
    "    sw.style.background = k.fo > 0 ? k.fill : 'transparent';\n"
    "    s.appendChild(sw); s.appendChild(el('span', k.label + ' ' + counts[key]));\n"
    "    lg.appendChild(s);\n"
    "  });\n"
    "  document.getElementById('note').textContent =\n"
    "    (basemap ? '' : 'The built-in map could not be loaded (ntrip_basemap.js). ') +\n"
    "    'Kinds and positions are what the sourcetable states, not measurements. ' +\n"
    "    'The outlines are 1:50m: good for a country, coarse for a town.';\n"
    "\n"
    "  var map = L.map('map', {preferCanvas: true, maxZoom: 12});\n"
    "  if (basemap) {\n"
    "    /* Flat [lon,lat,...] as tools/make_basemap.py writes it. */\n"
    "    var ll = function (a) {\n"
    "      var r = [];\n"
    "      for (var i = 0; i + 1 < a.length; i += 2) r.push([a[i + 1], a[i]]);\n"
    "      return r;\n"
    "    };\n"
    "    NE_BASEMAP.land.forEach(function (p) {\n"
    "      L.polygon(p.map(ll), {interactive: false, color: '#b9b09f', weight: 0.6,\n"
    "        fillColor: '#f4f1ea', fillOpacity: 1}).addTo(map);\n"
    "    });\n"
    "    NE_BASEMAP.borders.forEach(function (b) {\n"
    "      L.polyline(ll(b[1]), {interactive: false, color: '#8c7f6c', weight: 1,\n"
    "        dashArray: b[0] ? '4,4' : null}).addTo(map);\n"
    "    });\n"
    /* Natural Earth's own suggested credit. Public domain needs none;
     * the project gives it anyway (THIRD-PARTY-NOTICES.txt too). The
     * version comes from the project's own generated file. */
    "    map.attributionControl.addAttribution('<a href=\"https://www.naturalearthdata.com\" ' +\n"
    "      'target=\"_blank\" rel=\"noopener\">Made with Natural Earth</a> ' +\n"
    "      NE_BASEMAP.version + ', 1:50m, public domain');\n"
    "  }\n"
    "\n"
    "  var pts = [];\n"
    "  data.features.forEach(function (f) {\n"
    "    var p = f.properties, c = f.geometry.coordinates, k = KIND[kindOf(p)];\n"
    "    var m = L.circleMarker([c[1], c[0]], {radius: k.r, color: k.color, fillColor: k.fill,\n"
    "      fillOpacity: k.fo, weight: k.w, dashArray: k.dash || null});\n"
    "    m.bindPopup(function () { return popup(p, c); }, {maxWidth: 340});\n"
    /* An element, never a string: Leaflet sets a string tooltip as
     * innerHTML. Passing p.mountpoint itself ran a hostile name's
     * onerror on hover in the first draft's test. */
    "    m.bindTooltip(el('span', p.mountpoint));\n"
    "    m.addTo(map);\n"
    "    pts.push([c[1], c[0]]);\n"
    "  });\n"
    "\n"
    "  if (data.origin) {\n"
    "    var o = [data.origin[1], data.origin[0]];\n"
    "    L.circleMarker(o, {radius: 9, color: '#111', weight: 2, fillOpacity: 0}).addTo(map)\n"
    "      .bindTooltip('Your position (Lat/Lon fields)');\n"
    "    L.circleMarker(o, {radius: 2, color: '#111', fillColor: '#111', fillOpacity: 1}).addTo(map);\n"
    "    pts.push(o);\n"
    "  }\n"
    "  /* Open no closer than zoom 8: at 1:50m a closer view of two\n"
    "   * stations 25 km apart (RFSEE01, HANESE) was plain land with no\n"
    "   * coast or border in sight. Zooming further stays possible. */\n"
    "  if (pts.length > 1) map.fitBounds(pts, {padding: [30, 30], maxZoom: 8});\n"
    "  else if (pts.length === 1) map.setView(pts[0], 8);\n"
    "  else map.setView([30, 0], 2);\n"
    "  L.control.scale({imperial: false}).addTo(map);\n"
    "\n"
    "  window.mountmapStats = {drawn: shown, zoom: map.getZoom(),\n"
    "    ms: Math.round(performance.now() - t0)};\n"
    "})();\n"
    "</script>\n"
    "</body></html>\n";

int MountMapWritePage(FILE *f, const char *geojson)
{
    if (!f || !geojson) return -1;
    if (fputs(k_page_head, f) == EOF) return -1;
    if (fputs(geojson, f)     == EOF) return -1;
    if (fputs(k_page_tail, f) == EOF) return -1;
    return ferror(f) ? -1 : 0;
}

