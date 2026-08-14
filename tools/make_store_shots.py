"""Frame device captures as Play store screenshots.

Play wants a 16:9 or 9:16 aspect ratio, and a modern handset is neither
-- this device is 1080x2340, which is 9:19.5. Rather than crop the app
to fit, each capture is placed on a 1080x1920 canvas with a caption: the
ratio is then exactly 9:16, the status and navigation bars are gone, and
the listing says what the reader is looking at instead of leaving them
to guess.

    python tools/make_store_shots.py <captures-dir> free|pro

Captures are ordinary `adb exec-out screencap -p` PNGs named after the
screen: main, sky, signal, elevation, sourcetable. Only the ones present
are framed, in the order below, because Play shows them in upload order
and the first is the one most people ever see.

The caption is the promise; the screenshot is the evidence. Keep them
honest -- every claim here is one the app makes on screen.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

# The app's own palette, so the frames belong to the icon.
FIELD = (18, 58, 99)
TEXT = (240, 246, 251)
SUBTLE = (150, 180, 215)

CANVAS = (1080, 1920)
STATUS_BAR = 90        # cropped off the top of a capture
NAV_BAR = 110          # and off the bottom

CAPTIONS = {
    'main': ("Is this station fit for RTK?",
             "Eight checks, one verdict, and the number behind each"),
    'elevation': ("What the antenna is really doing",
                  "C/N0 against elevation, over a whole session"),
    # Not "the station's own orbits": that is one of three sources, and
    # neither edition's captures used it -- pro's came from an ephemeris
    # stream, free's from an imported navigation file. What is true of
    # every one of them, and is the distinction that matters, is that the
    # positions are broadcast orbits rather than this handset's guess.
    'sky': ("Every satellite the station carries",
            "Placed from broadcast orbits, not from this phone"),
    'signal': ("Signal strength, satellite by satellite",
               "Averaged in power, as it should be"),
    'sourcetable': ("The caster's own sourcetable",
                    "Every mountpoint, its format and its constellations"),
}

ORDER = ['main', 'elevation', 'sky', 'signal', 'sourcetable']

TILE_BG = (230, 224, 233)      # the connection tile's surface colour
TILE_INK = (29, 27, 32)        # and its text
PAGE_BG = (254, 247, 255)      # the page behind it, under the sky plot
PAGE_INK = (73, 69, 79)        # and the muted footer text on it

# All four measured from the captures rather than taken from the theme:
# a fill that is nearly right is more obvious than one that is wrong.

# ── Redaction ────────────────────────────────────────────────────────
# A store listing is marketing, and the caster in these captures is a
# real host that belongs to someone. The mountpoint and the measurements
# stay -- they are what the screenshot is *for* -- but the address is
# replaced with a documentation domain (RFC 2606 reserves example.com
# precisely so that nobody has to own it).
#
# Done here rather than by hand so it survives a re-capture: the boxes
# are where the text sits on a 1080-wide capture, measured from the
# pixels rather than guessed. Re-measure if the layout changes; a box
# that has drifted paints over the wrong line, which is obvious the
# moment you look at the result.
# Keyed by edition, because the box is a position and the two editions
# do not put the tile in the same place: free's verdict banner carries
# two extra lines once a check has finished ("Held for 60 s", "Finished
# after 90 s") and pushes the connection tile 66 px down. One shared box
# painted over free's mountpoint instead of its caster -- the drift this
# file warns about, met on the first re-use.
#
# Two things are hidden. The **caster address**, because a listing seen
# by thousands should not advertise a host that belongs to a person; and
# the **ARP coordinates** in the sky view's footer, which are the
# station's own position to six decimals -- about a tenth of a metre,
# and this station stands where its owner lives. The mountpoint and the
# measurements stay: they are what the screenshot is for.
#
# The replacement keeps the shape of what it hides -- degrees, comma
# decimal separator, six places -- so the reader still sees what the app
# reports, without the value.
ARP_HIDDEN = '52,xxxxxx, 5,xxxxxx'

REDACTIONS = {
    'pro': {
        'main': [
            # (box, replacement, monospace, size, fill, ink) -- the
            # connection tile's caster line, monospace in the app.
            ((84, 752, 470, 796), 'ntrip.example.com:2101', True, 44,
             TILE_BG, TILE_INK),
        ],
        'sky': [
            ((368, 2152, 812, 2202), ARP_HIDDEN, True, 40,
             PAGE_BG, PAGE_INK),
        ],
    },
    'free': {
        'main': [
            ((79, 806, 466, 864), 'ntrip.example.com:2101', True, 44,
             TILE_BG, TILE_INK),
        ],
        # Same geometry in both editions: the footer is drawn by the same
        # composable, below a plot of fixed height.
        'sky': [
            ((368, 2152, 812, 2202), ARP_HIDDEN, True, 40,
             PAGE_BG, PAGE_INK),
        ],
    },
}


def mono(size):
    for name in ('consola.ttf', 'cour.ttf'):
        path = os.path.join(os.environ.get('WINDIR', 'C:/Windows'),
                            'Fonts', name)
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return font(size)


def redact(shot, name, edition):
    """Paint out anything in the capture that should not be published."""
    for box, text, monospace, size, bg, ink in             REDACTIONS.get(edition, {}).get(name, []):
        draw = ImageDraw.Draw(shot)
        draw.rectangle(box, fill=bg)
        f = mono(size) if monospace else font(size)
        # Sit the replacement on the same baseline as what it replaces.
        top = box[1] + (box[3] - box[1] - size) // 2
        draw.text((box[0] + 4, top), text, font=f, fill=ink)
    return shot


def font(size, bold=False):
    """A real font if this machine has one; PIL's bitmap face otherwise."""
    for name in (('segoeuib.ttf', 'arialbd.ttf') if bold
                 else ('segoeui.ttf', 'arial.ttf')):
        path = os.path.join(os.environ.get('WINDIR', 'C:/Windows'),
                            'Fonts', name)
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def centred(draw, y, text, f, fill):
    w = draw.textbbox((0, 0), text, font=f)[2]
    draw.text(((CANVAS[0] - w) / 2, y), text, font=f, fill=fill)


def frame(capture_path, title, subtitle, edition):
    """One store screenshot: redacted, cropped, captioned."""
    shot = Image.open(capture_path).convert('RGB')
    shot = redact(shot, os.path.splitext(os.path.basename(capture_path))[0],
                  edition)
    shot = shot.crop((0, STATUS_BAR, shot.width, shot.height - NAV_BAR))

    canvas = Image.new('RGB', CANVAS, FIELD)
    draw = ImageDraw.Draw(canvas)

    centred(draw, 78, title, font(54, bold=True), TEXT)
    centred(draw, 152, subtitle, font(34), SUBTLE)

    # The capture fills what is left, whole: never cropped to fit, so a
    # reader sees the screen as it is.
    top = 240
    avail_h = CANVAS[1] - top - 60
    avail_w = CANVAS[0] - 120
    scale = min(avail_w / shot.width, avail_h / shot.height)
    shot = shot.resize((int(shot.width * scale), int(shot.height * scale)),
                       Image.LANCZOS)

    x = (CANVAS[0] - shot.width) // 2
    # A hairline so the app's pale background does not bleed into the navy.
    draw.rectangle([x - 2, top - 2, x + shot.width + 1, top + shot.height + 1],
                   outline=SUBTLE)
    canvas.paste(shot, (x, top))
    return canvas


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    src, edition = sys.argv[1], sys.argv[2]
    if edition not in REDACTIONS:
        print('unknown edition %r: no redaction boxes are measured '
              'for it, and framing without them would publish the '
              'real host' % edition)
        return 1
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_dir = os.path.join(root, 'docs', 'images', 'store', edition)
    os.makedirs(out_dir, exist_ok=True)

    n = 0
    for name in ORDER:
        path = os.path.join(src, name + '.png')
        if not os.path.exists(path):
            continue
        n += 1
        title, subtitle = CAPTIONS[name]
        out = os.path.join(out_dir, '%d-%s.png' % (n, name))
        frame(path, title, subtitle, edition).save(out)
        print('wrote', os.path.relpath(out, root))

    if n < 2:
        print('Play wants at least two screenshots; found %d' % n)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
