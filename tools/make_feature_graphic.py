"""Build the listing artwork, from the icon's own geometry.

    python tools/make_feature_graphic.py

Two kinds of output, for two audiences:

  * `docs/images/store/<edition>/feature-1024x500.png` — Play, per
    edition, shown above the screenshots.
  * `docs/images/social-preview-1280x640.png` — the repository's social
    preview, which is what a link to it looks like in a GitHub card, in
    Slack or on Mastodon.

**The social preview must be uploaded by hand**, once: GitHub exposes it
only in Settings → General → Social preview, with no API behind it. The
file is generated all the same, so the picture cannot drift from the
mark, and re-uploading after a change is a drag-and-drop.

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
SOCIAL_SIZE = (1280, 640)      # GitHub's social preview, and 2:1 for cards
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


def build_social():
    """The repository's social preview: what a link to it looks like.

    A different audience from the store graphic. Whoever sees this is
    looking at a *repository* — in a GitHub link card, in Slack, on
    Mastodon — so it names the project rather than an edition, wears the
    project's own blue rather than a paid accent, and says what the whole
    suite is instead of what one app does.

    1280x640 is GitHub's recommendation and a clean 2:1, which is what
    most link cards crop to. Everything sits well inside the edges,
    because a card may trim a little on either side and the first
    casualty is always text near a border.
    """
    w, h = SOCIAL_SIZE[0] * SCALE, SOCIAL_SIZE[1] * SCALE
    img = Image.new("RGB", (w, h), icons.FIELD)
    d = ImageDraw.Draw(img)

    r = h * 0.26
    icons.draw_mark(d, w * 0.20, h * 0.50, r, icons.ACCENT_FREE)

    x = w * 0.38
    t = font(int(h * 0.125), bold=True)
    s = font(int(h * 0.062))
    u = font(int(h * 0.044))

    title = "NTRIP-Analyser"
    promise = "Is this base station fit for RTK?"
    suite = "One core  ·  CLI  ·  Windows GUI  ·  Linux daemon  ·  Android"

    tb = d.textbbox((0, 0), title, font=t)
    sb = d.textbbox((0, 0), promise, font=s)
    ub = d.textbbox((0, 0), suite, font=u)

    gap1, gap2 = int(h * 0.055), int(h * 0.045)
    block = (tb[3] - tb[1]) + gap1 + (sb[3] - sb[1]) + gap2 + (ub[3] - ub[1])
    y = (h - block) / 2 - tb[1]

    d.text((x, y), title, font=t, fill=TEXT)
    y += (tb[3] - tb[1]) + gap1
    d.text((x, y), promise, font=s, fill=SUBTLE)
    y += (sb[3] - sb[1]) + gap2
    d.text((x, y), suite, font=u, fill=SUBTLE)

    return img.resize(SOCIAL_SIZE, Image.LANCZOS)


def main():
    out = os.path.join(ROOT, "docs", "images", "social-preview-1280x640.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    build_social().save(out)
    print("wrote", os.path.relpath(out, ROOT))

    for edition in EDITIONS:
        out = os.path.join(ROOT, "docs", "images", "store", edition,
                           "feature-1024x500.png")
        os.makedirs(os.path.dirname(out), exist_ok=True)
        build(edition).save(out)
        print("wrote", os.path.relpath(out, ROOT))


if __name__ == "__main__":
    main()
