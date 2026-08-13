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

Needed: phone screenshots (at least two, 16:9 or 9:16). Candidates are
the ones the repository already has under `docs/images/`.

**Open question 6 stands**: the existing screenshots name real
third-party mountpoints. Reshooting them against a mountpoint the author
operates, or blurring the name, avoids implying an endorsement by — or
of — someone else's infrastructure.

Store icon: `docs/images/icon-free-512.png` and `icon-pro-512.png`,
generated by `tools/make_icons.py`.

## Data safety declaration

**The answers live in `docs/work-items/play-data-safety.md`**, question
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

- [ ] Enable GitHub Pages (branch `main`, folder `/docs`) and check the
      policy URL resolves.
- [ ] Fill the contact address on both listings; the policy points at it.
- [ ] Confirm both editions install side by side and are distinguishable
      on the home screen.
- [ ] Upload from a release build signed with the release keystore, not
      the debug fallback (`android/keystore.properties.example`).
