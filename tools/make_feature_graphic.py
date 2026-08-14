"""Build the Play listing's feature graphic, from the icon's own geometry.

    python tools/make_feature_graphic.py

Play requires a 1024x500 graphic per listing and shows it above the
screenshots. It is the one asset with no functional purpose at all --
which is exactly why it should not be invented separately: it is drawn
with `make_icons.py`'s mark, palette and proportions, so a reader who
has seen the launcher icon recognises the listing as the same thing.

**Play crops it.** On some surfaces the graphic is shown at other aspect
ratios, and text near an edge is the first casualty. Everything here
sits inside a wide safe area, and the mark is far enough from the left
edge to survive a centre crop.

Outputs `docs/images/store/<edition>/feature-1024x500.png`, beside the
screenshots the same listing uses.
"""
import importlib.util
import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The icon script owns the geometry and the palette; import rather than
# restate, or the two drift and the listing stops looking like the app.
_spec = importlib.util.spec_from_file_location(
    "make_icons", os.path.join(ROOT, "tools", "make_icons.py"))
icons = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(icons)

SIZE = (1024, 500)
SCALE = 3                      # supersample, for the ring and the text

TEXT = (240, 246, 251)
SUBTLE = (150, 180, 215)

EDITIONS = {
    "free": (icons.ACCENT_FREE, "NTRIP Analyser",
             "Is this base station fit for RTK?"),
    "pro": (icons.ACCENT_PRO, "NTRIP Analyser Pro",
            "Watch a station for hours, from where you stand"),
}


def font(px, bold=False):
    for name in (("segoeuib.ttf", "arialbd.ttf") if bold
                 else ("segoeui.ttf", "arial.ttf")):
        path = os.path.join(os.environ.get("WINDIR", "C:/Windows"),
                            "Fonts", name)
        if os.path.exists(path):
            return ImageFont.truetype(path, px)
    return ImageFont.load_default()


def build(edition):
    accent, title, promise = EDITIONS[edition]
    w, h = SIZE[0] * SCALE, SIZE[1] * SCALE
    img = Image.new("RGB", (w, h), icons.FIELD)
    d = ImageDraw.Draw(img)

    # The mark, left of centre, at the height of the title block.
    r = h * 0.30
    cx, cy = w * 0.20, h * 0.50
    icons.draw_mark(d, cx, cy, r, accent)

    x = w * 0.38
    t = font(int(h * 0.155), bold=True)
    s = font(int(h * 0.077))
    tw = d.textbbox((0, 0), title, font=t)
    sw = d.textbbox((0, 0), promise, font=s)
    block = (tw[3] - tw[1]) + int(h * 0.06) + (sw[3] - sw[1])
    y = (h - block) / 2 - tw[1]

    d.text((x, y), title, font=t, fill=TEXT)
    d.text((x, y + (tw[3] - tw[1]) + int(h * 0.06)), promise,
           font=s, fill=SUBTLE)

    return img.resize(SIZE, Image.LANCZOS)


def main():
    for edition in EDITIONS:
        out = os.path.join(ROOT, "docs", "images", "store", edition,
                           "feature-1024x500.png")
        os.makedirs(os.path.dirname(out), exist_ok=True)
        build(edition).save(out)
        print("wrote", os.path.relpath(out, ROOT))


if __name__ == "__main__":
    main()
