#!/usr/bin/env python3
"""Regenerate gui/ntrip-analyser.ico -- a satellite.

Run from the repository root:

    python tools/make-icon.py

Requires Pillow.  Kept in the tree so the icon is reproducible: a binary
.ico with no source is something nobody can adjust later.

Design notes, since they are the part worth keeping:

* Body plus two solar panels, axis-aligned.  A tilted satellite looks
  better at 256 and falls apart at 16, and 16 is where a tray icon
  spends its life, so the small end wins the argument.
* Sizes below 32 drop the downlink beam and enlarge the satellite to
  fill the tile.  Thin diagonals anti-alias into grey smudges at that
  size, so carrying every element down to 16 yields a blob rather than a
  picture; fewer, larger shapes read better than a faithful miniature.
* Each size is rendered at its own scale rather than downsampled from
  one large image, so stroke widths stay proportionate.
* The .ico is written by hand rather than through Pillow's ICO saver,
  which resizes a single base image and would discard the per-size
  renders.  BMP/DIB for sizes up to 64, PNG for 256, per convention.

Project: NTRIP-Analyser
Author: Remko Welling, PE1MEW
License: Apache License 2.0 with Commons Clause
"""

import io
import struct
from PIL import Image, ImageDraw

OUT   = "gui/ntrip-analyser.ico"
SIZES = [16, 24, 32, 48, 64, 256]

BG    = (13, 36, 56, 255)     # deep navy tile
PANEL = (77, 208, 225, 255)   # cyan solar panels
BODY  = (255, 255, 255, 255)  # white bus
SS    = 8                     # supersample factor, for smooth edges


def render(size):
    """Draw the icon at `size` px, rendered at SS x then downsampled."""
    S = size * SS
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, S - 1, S - 1], radius=int(S * 0.22), fill=BG)

    detailed = size >= 32
    if detailed:
        cx, cy = S * 0.5, S * 0.40
        bw, bh = S * 0.16, S * 0.22
        pw, ph = S * 0.20, S * 0.14
        gap = S * 0.05
    else:
        cx, cy = S * 0.5, S * 0.5
        bw, bh = S * 0.22, S * 0.30
        pw, ph = S * 0.24, S * 0.20
        gap = S * 0.055

    for sx in (-1, 1):                      # solar panels
        x0 = cx + sx * (bw / 2 + gap)
        x1 = x0 + sx * pw
        d.rectangle([min(x0, x1), cy - ph / 2, max(x0, x1), cy + ph / 2],
                    fill=PANEL)

    boom = S * (0.020 if detailed else 0.030)
    d.rectangle([cx - (bw / 2 + gap), cy - boom,
                 cx + (bw / 2 + gap), cy + boom], fill=PANEL)

    d.rounded_rectangle([cx - bw / 2, cy - bh / 2, cx + bw / 2, cy + bh / 2],
                        radius=int(S * 0.035), fill=BODY)

    if detailed:
        # Downlink widening toward the ground -- which is what an NTRIP
        # stream is.  Two strokes rather than a filled cone so it stays
        # open instead of blocking up.
        top_y, bot_y, half = cy + bh / 2, S * 0.86, S * 0.21
        w = max(1, int(S * 0.05))
        d.line([cx - S * 0.02, top_y, cx - half, bot_y], fill=PANEL, width=w)
        d.line([cx + S * 0.02, top_y, cx + half, bot_y], fill=PANEL, width=w)

    return im.resize((size, size), Image.LANCZOS)


def encode(size, im):
    """One ICO image: PNG at 256, otherwise a bottom-up BGRA DIB."""
    if size >= 256:
        buf = io.BytesIO()
        im.save(buf, format="PNG")
        return buf.getvalue()

    px = im.load()
    header = struct.pack("<IiiHHIIiiII",
                         40, size, size * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    xor = bytearray()
    for y in range(size - 1, -1, -1):       # DIBs are stored bottom-up
        for x in range(size):
            r, g, b, a = px[x, y]
            xor += bytes((b, g, r, a))
    # 1bpp AND mask, rows padded to 4 bytes.  Left all-zero: the alpha
    # channel carries transparency, and the mask would only fight it.
    row = ((size + 31) // 32) * 4
    return header + bytes(xor) + bytes(bytearray(row * size))


def main():
    blobs = [encode(s, render(s)) for s in SIZES]
    offset = 6 + 16 * len(SIZES)
    out = struct.pack("<HHH", 0, 1, len(SIZES))
    for size, blob in zip(SIZES, blobs):
        dim = 0 if size >= 256 else size    # 0 means 256 in an ICO entry
        out += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32,
                           len(blob), offset)
        offset += len(blob)
    out += b"".join(blobs)

    with open(OUT, "wb") as f:
        f.write(out)
    print("wrote %s: %d bytes, %d images %s"
          % (OUT, len(out), len(SIZES), SIZES))


if __name__ == "__main__":
    main()
