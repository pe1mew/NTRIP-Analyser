# Play listings and the data-safety declaration

What goes into the Play Console for both editions. Written here rather
than typed straight into the console so it can be reviewed, corrected and
reused — a listing is a public statement about what the software does,
and it should be held to the same standard as the software.

Sources: `design/telemetry.md` (what is collected: nothing),
`android/design/editions.md` (what pro transmits, and when),
`docs/privacy-policy.md` (the published text), `design/tls.md` (why
nothing is encrypted in transit yet).

## Names

| | Play title | On-device label |
|---|---|---|
| Free | `NTRIP Analyser` | NTRIP Analyser |
| Pro | `NTRIP Analyser Pro` | NTRIP Analyser Pro |

**Not "NTRIP Analyser - free".** Play's metadata policy treats
promotional words in a title — *free*, *sale*, *#1* — as grounds for
rejection, and the word buys nothing: the price is already on the
listing. "Pro" is the ordinary way to name a paid edition and is not
promotional in that sense.

The two launcher icons differ in accent colour (blue and amber) for the
same reason: on a phone with both installed, the labels truncate to
"NTRIP Ana…" and the colour is what tells them apart.

## Short description (80 characters)

**Free**

    Is this GNSS base station fit for RTK? Measure it, and see why.

**Pro**

    Field-grade NTRIP checks: watch mode, live position, saved casters.

## Full description

Shared opening, then an edition-specific section. Play descriptions take
no formatting beyond line breaks.

    NTRIP Analyser connects to an NTRIP caster and answers one question
    about a GNSS base station: is it fit to serve RTK, and if not, why?

    It does not steer a rover and it does not compute a position. It
    measures what the station actually delivers -- message types and
    their rates, satellites and their signal strength, the reference
    position it broadcasts, and how steadily it holds all of that -- and
    states a verdict you can act on or hand to whoever runs the station.

    WHAT IT CHECKS

    Eight measurements, each with its own verdict and the number behind
    it:

    - the connection authenticates and data flows
    - the stream really is RTCM 3.x, and its frames pass CRC
    - the station broadcasts its reference position (1005/1006)
    - observations arrive at the rate the mountpoint advertises
    - enough satellites, across the constellations claimed
    - signal strength is what a working antenna produces
    - the stream holds its verdict for a full minute
    - what the sourcetable advertises is what the stream delivers

    A run ends with STATION OK, CAUTION or FAILED -- and with the
    evidence, so the verdict can be argued with.

    WHAT ELSE IT SHOWS

    - Sky view: satellites by constellation, from broadcast orbits or
      from a RINEX navigation file you supply
    - Signal quality per satellite and per constellation
    - Message statistics: types, epochs, and the interval between them
    - The sourcetable, as the caster publishes it

    PRIVACY

    No account, no advertising, no analytics library, and no server
    belonging to the developer. What the app sends, it sends to the
    caster you configure. Credentials are stored encrypted on the
    device; NTRIP itself sends them over a plain connection, and the app
    says so where you type them.

    The app is a client: it connects where you tell it to, with your own
    credentials. Having permission to use a caster, and observing its
    terms, is yours -- the data belongs to whoever operates the station.

**Free — append**

    THIS EDITION

    The full eight-KPI check, the analysis views, and the sourcetable
    browser, for one caster at a time. Positions for network mountpoints
    are taken from the station's own sourcetable entry or picked on a
    map; this edition never sends the phone's position anywhere.

**Pro — append**

    THIS EDITION ADDS

    - Watch mode: keep measuring for hours, with availability, streak
      and degradation counts
    - Live position: report where this phone actually is to a network
      mountpoint, so the service answers for where you are standing
      (asked once, explicitly, and revocable)
    - Saved connections: several casters, switched from the main screen
    - Import and export of the shared configuration file
    - Pick mountpoints straight from the sourcetable
    - Ephemeris side-stream for a complete sky view

    Bought once. No subscription, and nothing is measured differently
    from the free edition -- the thresholds and the verdicts are the
    same engine.

## Category and tags

Tools. Not "Maps & Navigation": the app navigates nothing, and being
listed beside consumer GPS apps would attract the wrong installs and the
wrong reviews.

Content rating questionnaire: no user-generated content, no
communication features, no purchases inside the app (pro is paid up
front).

## Screenshots

`docs/images/store/<edition>/`, built by `tools/make_store_shots.py`
from ordinary `adb exec-out screencap -p` captures.

A modern handset is 9:19.5 and Play wants 16:9 or 9:16, so each capture
is placed **whole** on a 1080x1920 canvas in the app's own navy, with a
caption: the ratio is exact, the status and navigation bars are gone,
and the reader is told what they are looking at. Nothing is cropped to
fit — the screen is shown as it is.

    python tools/make_store_shots.py <captures-dir> pro

**Pro — four, shot on 2026-08-13:**

| | Screen | Caption |
|---|---|---|
| 1 | Verdict, watch running | Is this station fit for RTK? |
| 2 | C/N0 versus elevation | What the antenna is really doing |
| 3 | Sky view | Every satellite the station carries |
| 4 | Signal quality | Signal strength, satellite by satellite |

Number 2 is the one to lead with in any marketing beyond the store: a
24-minute watch, **50 325 samples**, the antenna curve climbing from the
horizon to zenith with a visible dent in GLONASS around 30-45 degrees —
the view earning its place in a single image.

**Open question 6 is resolved for pro**: the shots are of the author's
own caster and station, so no third party's infrastructure is named in
marketing material — and **the caster's address is replaced** with
`ntrip.example.com:2101` by `REDACTIONS` in the framing tool, since a
store listing is marketing and a real host does not belong in it. The
mountpoint stays: it is the subject of the measurement, and a screenshot
of a station with no name is a screenshot of nothing.

The redaction is in the tool rather than done by hand, so it survives a
re-capture. Its box was measured from the pixels; if the tile's layout
changes, re-measure, because a box that has drifted paints over the
wrong line.

**Free — four, shot on 2026-08-14**, on a release build, same station
and the same redaction:

| | Screen | What it shows |
|---|---|---|
| 1 | STATION OK, held 60 s, finished after 90 s | the verdict and the first three checks |
| 2 | C/N0 versus elevation | 2984 samples from one 90 s check |
| 3 | Sky view | 38 of 41 satellites, from an imported navigation file |
| 4 | Signal quality | per-satellite C/N0 |

**The redaction box is per edition now.** Free's verdict banner carries
two extra lines once a check has finished, which pushes the connection
tile 66 px down; the single shared box would have painted over free's
*mountpoint* instead of its caster — the drift the tool's own comment
warns about, met the first time it was re-used. `REDACTIONS` is keyed by
edition, and an unknown edition is refused rather than framed
unredacted.

**The fifth shot was dropped, for both editions.** The sourcetable
dialog is titled with the caster's host, and this caster lists two
mountpoints — thin evidence for a caption promising "every mountpoint,
its format and its constellations".

**The settings dialog used to show the password in clear text**, which
is how a capture of it was taken during this session — deleted before
framing. The field is masked by default now, with a Show toggle. Even
so, nothing on that screen belongs in a listing.

**The ARP coordinates are redacted from both sky shots.** The footer
reports the station's own position to six decimals, and this station
stands where its owner lives; it is replaced with `52,xxxxxx, 5,xxxxxx`,
which keeps the shape of what the app reports without the value.

⚠ **Still visible, and your call**: free's `1-main.png` carries *"GGA
uplink from 52,20000, 5,97000"* — the configured uplink position, not
the station's. It is rounded to about a kilometre, so it names a
district rather than a house. Say the word and it gets a box too.

**The sky caption was corrected.** It read "placed from the station's own
orbits, not guessed", which neither edition's capture actually shows:
pro's came from the ephemeris stream and free's from a navigation file,
because `RFSEE01` broadcasts no orbits. It now reads "placed from
broadcast orbits, not from this phone" — true of all three sources, and
the distinction that matters. Pro's sky shot was re-taken on 2026-08-14
to carry it (39 of 39 from the ephemeris stream); pro's other three are
unchanged, since its elevation shot is a 24-minute watch that a 90 s
re-take would only make worse.

Store icon: `docs/images/icon-free-512.png` and `icon-pro-512.png`,
generated by `tools/make_icons.py`.

## Data safety declaration

**The answers live in `design/work-items/play-data-safety.md`**, question
by question and per edition, with the reasoning behind each. In short:
free declares nothing, pro declares precise location as *shared,
optional, app functionality*, and both answer **No** to encryption in
transit until TLS lands.

Two statements of one fact must never disagree, so this file does not
repeat them.

Privacy policy URL: the published copy of `docs/privacy-policy.md` --
GitHub Pages from `/docs`, the same URL for both listings, because one
document covers both editions and marks every difference between them.

## Before submitting

- [ ] Run `python tools/check_release.py` and see it agree. It settles
      the mechanical half of this list — version, the addresses the app
      can open, the check count, Play's length and title-policy limits,
      and whether the generated notices match the dependencies they
      name. Everything below it needs a person.
- [ ] Enable GitHub Pages (branch `main`, folder `/docs`) and check the
      policy URL resolves. **The app now links it too** (About → Privacy
      policy), so until Pages is on, that button opens a 404.
- [ ] Publish the wiki: `bash tools/publish_wiki.sh --push`. The app
      links into it from About → Documentation and from the orbit badge,
      so unpublished pages leave those buttons pointing at the
      repository front page.
- [ ] Fill the contact address on both listings; the policy points at it.
- [ ] Confirm both editions install side by side and are distinguishable
      on the home screen.
- [ ] Upload from a release build signed with the release keystore, not
      the debug fallback (`android/keystore.properties.example`).
