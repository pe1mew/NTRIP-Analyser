#!/usr/bin/env python3
"""Build the mountpoint map's built-in basemap from Natural Earth.

The mountpoint map (design/work-items/mountpoint-map.md) is drawn over
outlines the program carries itself, so it needs no tile server, no key
and no network for the map underneath. This turns two Natural Earth
1:50m GeoJSON files into gui/basemap_ne50m.js, which the GUI compiles in
as a resource and writes beside the page, and into the same outlines as
android/app/src/main/assets/basemap_ne50m.bin for the phone's map
(mountpoint-map.md, M6b) -- one run, one rounding, both files.

Natural Earth is public domain: "All versions of Natural Earth raster +
vector map data found on this website are in the public domain", and
"Crediting the authors is unnecessary"
(https://www.naturalearthdata.com/about/terms-of-use/, read 2026-10-12).
It is credited anyway, with the long line those terms suggest -- "Made
with Natural Earth. Free vector and raster map data @
naturalearthdata.com." -- in the output's header, on the map pages, in
THIRD-PARTY-NOTICES.txt and in the readme; docs/licences.md says why.

Input -- release v5.1.2 of https://github.com/nvkelso/natural-earth-vector,
pinned so the output can be rebuilt byte for byte:

    geojson/ne_50m_land.geojson                         1636166 bytes
    geojson/ne_50m_admin_0_boundary_lines_land.geojson   760189 bytes

    curl -fLO https://raw.githubusercontent.com/nvkelso/natural-earth-vector/v5.1.2/geojson/ne_50m_land.geojson
    curl -fLO https://raw.githubusercontent.com/nvkelso/natural-earth-vector/v5.1.2/geojson/ne_50m_admin_0_boundary_lines_land.geojson
    python tools/make_basemap.py <folder with both files>

The downloads stay out of the repository; the output is committed.

Coordinates are rounded to 3 decimals, about 100 m -- finer than 1:50m
data is accurate to -- and points that coincide after rounding are
dropped. Border lines carry a class: 0 an international boundary, 1
anything Natural Earth marks otherwise (disputed, line of control,
indefinite), which the page draws dashed.

Written with newline='' and ASCII only, one feature per line, so the
file is stable under git and diffs show what changed.
"""
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "gui", "basemap_ne50m.js")
OUT_BIN = os.path.join(HERE, "..", "android", "app", "src", "main", "assets",
                       "basemap_ne50m.bin")
VERSION = "5.1.2"


def num(v):
    s = "%.3f" % round(v, 3)
    s = s.rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def flat(coords):
    """[[lon, lat], ...] -> "lon,lat,..." with repeats removed."""
    out, last = [], None
    for lon, lat in ((c[0], c[1]) for c in coords):
        p = (num(lon), num(lat))
        if p != last:
            out.extend(p)
            last = p
    return out


def milli(s):
    """A rounded coordinate as integer thousandths: exact, because `num`
    has already rounded it to three decimals."""
    neg = s.startswith("-")
    whole, _, frac = s.lstrip("-").partition(".")
    v = int(whole) * 1000 + int((frac + "000")[:3])
    return -v if neg else v


def write_bin(polys, blines):
    """The same outlines for the Android app, as an asset it reads
    without a JSON parser: `android/app/src/main/assets/basemap_ne50m.bin`.

    Little-endian throughout:

        "NEBM"  u8 format (1)  u8 n + n ASCII bytes: Natural Earth release
        u32 polygons, each:  u16 rings, each:  u32 points, then per point
                                               i32 lon, i32 lat
        u32 border lines, each:  u8 class  u32 points, then per point
                                           i32 lon, i32 lat

    Coordinates are the JS file's own three-decimal values, as integer
    thousandths of a degree, so the phone and the GUI draw the same
    outlines. Class is as in the JS: 0 international, 1 otherwise.
    """
    out = bytearray(b"NEBM")
    out += struct.pack("<BB", 1, len(VERSION)) + VERSION.encode("ascii")
    out += struct.pack("<I", len(polys))
    for rings in polys:
        out += struct.pack("<H", len(rings))
        for r in rings:
            out += struct.pack("<I", len(r) // 2)
            out += struct.pack("<%di" % len(r), *(milli(v) for v in r))
    out += struct.pack("<I", len(blines))
    for cls, pts in blines:
        out += struct.pack("<BI", cls, len(pts) // 2)
        out += struct.pack("<%di" % len(pts), *(milli(v) for v in pts))
    with open(OUT_BIN, "wb") as f:
        f.write(out)
    print("%s: %d bytes" % (os.path.normpath(OUT_BIN), len(out)))


def main(folder):
    with open(os.path.join(folder, "ne_50m_land.geojson"), encoding="utf-8") as f:
        land = json.load(f)
    with open(os.path.join(folder, "ne_50m_admin_0_boundary_lines_land.geojson"),
              encoding="utf-8") as f:
        borders = json.load(f)

    lines = [
        "/* Natural Earth 1:50m land and country borders, release v%s.\n" % VERSION,
        " * Made with Natural Earth. Free vector and raster map data @ naturalearthdata.com.\n",
        " * Public domain: https://www.naturalearthdata.com/about/terms-of-use/\n",
        " * Generated by tools/make_basemap.py -- do not edit by hand. */\n",
        "var NE_BASEMAP = {\"version\":\"%s\",\"land\":[\n" % VERSION,
    ]

    polys, poly_rings = [], []
    for feat in land["features"]:
        g = feat["geometry"]
        for poly in (g["coordinates"] if g["type"] == "MultiPolygon" else [g["coordinates"]]):
            rings = [flat(r) for r in poly]
            rings = [r for r in rings if len(r) >= 6]     # three points or more
            if rings:
                polys.append("[" + ",".join("[" + ",".join(r) + "]" for r in rings) + "]")
                poly_rings.append(rings)
    lines.append(",\n".join(polys) + "\n],\"borders\":[\n")

    blines, border_pts = [], []
    for feat in borders["features"]:
        cls = 0 if feat["properties"].get("FEATURECLA") == "International boundary (verify)" else 1
        g = feat["geometry"]
        for line in (g["coordinates"] if g["type"] == "MultiLineString" else [g["coordinates"]]):
            pts = flat(line)
            if len(pts) >= 4:
                blines.append("[%d,[%s]]" % (cls, ",".join(pts)))
                border_pts.append((cls, pts))
    lines.append(",\n".join(blines) + "\n]};\n")
    write_bin(poly_rings, border_pts)

    text = "".join(lines)
    text.encode("ascii")    # fails loudly if anything non-ASCII slipped in
    with open(OUT, "w", encoding="ascii", newline="") as f:
        f.write(text)
    print("%s: %d land polygons, %d border lines, %d bytes"
          % (os.path.normpath(OUT), len(polys), len(blines), len(text)))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: make_basemap.py <folder with the two Natural Earth files>")
    main(sys.argv[1])
