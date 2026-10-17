# Play listings and the data-safety declaration

What goes into the Play Console for both editions. Written here rather
than typed straight into the console so it can be reviewed, corrected and
reused — a listing is a public statement about what the software does,
and it should be held to the same standard as the software.

Sources: `design/telemetry.md` (what is collected: nothing),
`android/design/editions.md` (what pro transmits, and when),
`docs/privacy-policy.md` (the published text), `design/tls.md` and
`design/work-items/archive/tls-rollout.md` (TLS, shipped 3.8.0 in both
editions -- per-connection, verified, never inferred from the port).

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

    Watch stations for hours: stability verdicts, network-RTK checks, exports.

## Pending change to the full description

Kept outside the description's own section on purpose:
`tools/check_release.py` measures everything in that section against
Play's 4000-character limit, and a note there was counted as listing
text (4181 characters, CI, 2026-10-12).

**Decided 2026-10-12, reversed 2026-10-15 (author):** the sentence
*"It does not steer a rover and it does not compute a position."*
stays, on both listings. The app computes nothing a user would survey
with; everything it shows is provided by others, the self-position
figures included -- they are the station's own observations against
the station's own broadcast position. The 2026-10-12 reasoning is in
`design/work-items/station-self-position.md`, "The gate".

**Live 2026-10-17, read from the store pages:** pro gained the
Self-position bullet and the watch's drop record, both listings name
the mountpoint map and the hand-over from free, and free lost the
closed-test text it had shown since going public ("First closed test of
NTRIP Analyser.", until 2026-10-16). Both listings gained a Dutch
translation of their own, replacing Google's machine translation
(*Dutch listing*, below). The text in the next section is what is
live; change it there when the console changes.

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
    - The sourcetable, as the caster publishes it: every mountpoint with
      its distance and bearing, in a list or on a map

    PRIVACY

    No account, no advertising, no analytics library, and no server
    belonging to the developer. What the app sends, it sends to the
    caster you configure. Credentials are stored encrypted on the
    device. If the caster offers TLS, tick "Use TLS" and the whole
    connection is encrypted and verified against trusted roots; over a
    plain connection the app says plainly, where you type the password,
    that anything on the path can read what NTRIP sends.

    The app is a client: it connects where you tell it to, with your own
    credentials. Having permission to use a caster, and observing its
    terms, is yours -- the data belongs to whoever operates the station.

**Free — append**

    THIS EDITION

    The full eight-check verdict, the analysis views, and the
    sourcetable browser, for one caster at a time. Positions for network
    mountpoints are taken from the station's own sourcetable entry or
    picked on a map; this edition never sends the phone's position
    anywhere. Moving to the paid edition later? It can take over the
    connection saved here, once you agree.

**Pro — append**

    THIS EDITION ADDS

    - Watch mode: keep measuring for hours, through the screen going
      dark, with availability, streaks and reconnect counts, and every
      drop out of STATION OK: when, how long, which checks
    - Stability report: six metrics over the whole run -- availability,
      frame integrity, signal, satellites held, ionosphere, delivery --
      each with its own verdict: STABLE, DEGRADED or UNSTABLE
    - Network-RTK (VRS) check: does the network accept your position
      and deliver corrections fit for RTK where you stand
    - Reference-position watch: know the moment a network hands you to
      another station, and how far it moved
    - Self-position: where the station's own GPS observations put it,
      against the position it broadcasts -- figures and a plot, no verdict
    - Live position: report where this phone actually is to a network
      mountpoint (asked once, explicitly, and revocable)
    - Satellite tracks on the sky view, up to a day per satellite
    - Statistics export: the full snapshot as JSON, or CSV in the
      monitoring daemon's own dialect
    - Saved connections, starting with the free edition's if you agree
      to share it; mountpoints picked by tapping them in the list or on
      the map; an ephemeris side-stream for a complete sky view; and
      import/export of the shared configuration file

    Bought once. No subscription, and nothing is measured differently
    from the free edition -- the eight checks and their verdicts are
    the same engine.

## Category and tags

Tools. Not "Maps & Navigation": the app navigates nothing, and being
listed beside consumer GPS apps would attract the wrong installs and the
wrong reviews. Free sat in *Maps & Navigation* from its first release
until 2026-10-16, when the store page was read whole for the first
time; both are Tools since.

Content rating questionnaire: no user-generated content, no
communication features, no purchases inside the app (pro is paid up
front).

## Dutch listing (nl-NL)

Entered in the console as a translation of its own (2026-10-16),
replacing Google's machine translation, which rendered *mountpoint* as
*montagepunt*, *caster* as *zender* and *watch mode* as *Kijkmodus*.
Screen names stay in English (*Watch mode*, *Self-position*, *Use
TLS*, the verdicts) because the app shows them in English. Titles are
the English ones. Free's text below is what is live, including the
author's own wording of its first two checks.

### Short description

**Free**

    Is dit GNSS-basisstation geschikt voor RTK? Meet het, en zie waarom.

**Pro**

    Bewaak stations urenlang: stabiliteitsoordelen, netwerk-RTK, exports.

### Full description, free

    NTRIP Analyser maakt verbinding met een NTRIP-caster en beantwoordt
    een vraag over een GNSS-basisstation: is het geschikt voor RTK, en zo
    niet, waarom niet?

    Het stuurt geen rover aan en berekent geen positie. Het meet wat het
    station werkelijk levert -- berichttypen en hun frequentie, satellieten
    en hun signaalsterkte, de referentiepositie die het uitzendt, en hoe
    stabiel het dat allemaal volhoudt -- en geeft een oordeel waar je mee
    verder kunt, of dat je doorgeeft aan wie het station beheert.

    WAT HET CONTROLEERT

    Acht metingen, elk met een eigen oordeel en het getal erachter:

    - de verbinding authenticeren en komen er gegevens binnen
    - de stream is echt RTCM 3.x, en de ontvangen frames zijn correct (CRC)
    - het station zendt zijn referentiepositie uit (1005/1006)
    - waarnemingen komen binnen in het tempo dat het mountpoint opgeeft
    - genoeg satellieten, over de opgegeven constellaties
    - de signaalsterkte past bij een werkende antenne
    - de stream houdt zijn oordeel een volle minuut vast
    - wat de sourcetable aankondigt, levert de stream ook

    Een meting eindigt met STATION OK, CAUTION of FAILED -- met het
    bewijs erbij, zodat over het oordeel te praten valt.

    WAT HET VERDER LAAT ZIEN

    - Sky view: satellieten per constellatie, uit uitgezonden banen of
      uit een RINEX-navigatiebestand dat je zelf aanlevert
    - Signaalkwaliteit per satelliet en per constellatie
    - Berichtstatistiek: typen, epochs en de tijd daartussen
    - De sourcetable zoals de caster die publiceert: elk mountpoint met
      afstand en richting, in een lijst of op een kaart

    PRIVACY

    Geen account, geen advertenties, geen analysebibliotheek en geen
    server van de ontwikkelaar. Wat de app verstuurt, gaat naar de caster
    die je instelt. Inloggegevens worden versleuteld op het toestel
    bewaard. Biedt de caster TLS, vink dan "Use TLS" aan: de hele
    verbinding is dan versleuteld en gecontroleerd tegen vertrouwde
    rootcertificaten. Over een onversleutelde verbinding zegt de app
    duidelijk, op de plek waar je het wachtwoord intypt, dat alles
    onderweg kan lezen wat NTRIP verstuurt.

    De app is een client: hij verbindt waar jij hem heen stuurt, met je
    eigen inloggegevens. Toestemming om een caster te gebruiken, en je
    aan de voorwaarden houden, is jouw zaak -- de gegevens zijn van wie
    het station beheert.

    DEZE EDITIE

    De volledige controle met acht metingen, de analyseweergaven en de
    sourcetable-browser, voor een caster tegelijk. Posities voor
    netwerk-mountpoints komen uit de eigen sourcetable-regel van het
    station of kies je op een kaart; deze editie stuurt de positie van de
    telefoon nergens heen. Later over naar de betaalde editie? Die kan de
    hier opgeslagen verbinding overnemen, zodra je daarmee instemt.

### Full description, pro

    NTRIP Analyser maakt verbinding met een NTRIP-caster en beantwoordt
    een vraag over een GNSS-basisstation: is het geschikt voor RTK, en zo
    niet, waarom niet?

    Het stuurt geen rover aan en berekent geen positie. Het meet wat het
    station werkelijk levert -- berichttypen en hun frequentie, satellieten
    en hun signaalsterkte, de referentiepositie die het uitzendt, en hoe
    stabiel het dat allemaal volhoudt -- en geeft een oordeel waar je mee
    verder kunt, of dat je doorgeeft aan wie het station beheert.

    WAT HET CONTROLEERT

    Acht metingen, elk met een eigen oordeel en het getal erachter:

    - de verbinding authenticeert en er komen gegevens binnen
    - de stream is echt RTCM 3.x, en de frames doorstaan de CRC
    - het station zendt zijn referentiepositie uit (1005/1006)
    - waarnemingen komen binnen in het tempo dat het mountpoint opgeeft
    - genoeg satellieten, over de opgegeven constellaties
    - de signaalsterkte past bij een werkende antenne
    - de stream houdt zijn oordeel een volle minuut vast
    - wat de sourcetable aankondigt, levert de stream ook

    Een meting eindigt met STATION OK, CAUTION of FAILED -- met het
    bewijs erbij, zodat over het oordeel te praten valt.

    WAT HET VERDER LAAT ZIEN

    - Sky view: satellieten per constellatie, uit uitgezonden banen of
      uit een RINEX-navigatiebestand dat je zelf aanlevert
    - Signaalkwaliteit per satelliet en per constellatie
    - Berichtstatistiek: typen, epochs en de tijd daartussen
    - De sourcetable zoals de caster die publiceert: elk mountpoint met
      afstand en richting, in een lijst of op een kaart

    PRIVACY

    Geen account, geen advertenties, geen analysebibliotheek en geen
    server van de ontwikkelaar. Wat de app verstuurt, gaat naar de caster
    die je instelt. Inloggegevens worden versleuteld op het toestel
    bewaard. Biedt de caster TLS, vink dan "Use TLS" aan: de hele
    verbinding is dan versleuteld en gecontroleerd tegen vertrouwde
    rootcertificaten. Over een onversleutelde verbinding zegt de app
    duidelijk, op de plek waar je het wachtwoord intypt, dat alles
    onderweg kan lezen wat NTRIP verstuurt.

    De app is een client: hij verbindt waar jij hem heen stuurt, met je
    eigen inloggegevens. Toestemming om een caster te gebruiken, en je
    aan de voorwaarden houden, is jouw zaak -- de gegevens zijn van wie
    het station beheert.

    DEZE EDITIE VOEGT TOE

    - Watch mode: urenlang doormeten, ook met het scherm uit, met
      beschikbaarheid, reeksen en aantallen herverbindingen, en elke
      terugval uit STATION OK: wanneer, hoe lang, welke controles
    - Stabiliteitsrapport: zes metingen over de hele run --
      beschikbaarheid, frame-integriteit, signaal, vastgehouden
      satellieten, ionosfeer, levering -- elk met een eigen oordeel:
      STABLE, DEGRADED of UNSTABLE
    - Netwerk-RTK-controle (VRS): accepteert het netwerk je positie, en
      levert het correcties die geschikt zijn voor RTK waar je staat
    - Bewaking van de referentiepositie: zie op het moment zelf dat een
      netwerk je naar een ander station overzet, en hoe ver dat ligt
    - Self-position: waar de eigen GPS-waarnemingen van het station het
      plaatsen, ten opzichte van de positie die het uitzendt -- getallen
      en een plot, geen oordeel
    - Live position: meld waar deze telefoon werkelijk is aan een
      netwerk-mountpoint (een keer gevraagd, expliciet, en herroepbaar)
    - Satellietsporen in de sky view, tot een dag per satelliet
    - Export van statistiek: de volledige momentopname als JSON, of als
      CSV in het dialect van de monitoring-daemon
    - Opgeslagen verbindingen, te beginnen met die van de gratis editie
      als je die wilt delen; mountpoints kiezen door ze aan te tikken in
      de lijst of op de kaart; een ephemeris-stream ernaast voor een
      complete sky view; en import en export van het gedeelde
      configuratiebestand

    Eenmalig gekocht. Geen abonnement, en er wordt niets anders gemeten
    dan in de gratis editie -- de acht controles en hun oordelen komen
    uit dezelfde engine.

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

**The GGA position in free's `1-main.png` stays**, decided 2026-08-14.
*"GGA uplink from 52,20000, 5,97000"* is the configured uplink position,
not the station's: the author reduced its resolution deliberately and
picked a random point in their own town. It is already anonymous, and
redacting it would hide a feature the screenshot is there to show.

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

**Feature graphic** (1024x500, required by Play, shown above the
screenshots): `docs/images/store/<edition>/feature-1024x500.png`, from
`tools/make_feature_graphic.py`. It imports the mark, palette and
proportions from `make_icons.py` rather than restating them, so the
listing and the launcher icon cannot drift apart. Play crops it on some
surfaces, so the text sits well inside a safe area and the mark is far
enough from the left edge to survive a centre crop.

Free reads *"Is this base station fit for RTK?"*; pro, *"Watch a station
for hours, from where you stand"* — the paid edition's difference, not a
louder version of the same claim.

**Contact details on the listing** (decided 2026-08-14): an alias
address rather than a personal one, since Play publishes it; no phone
number, which would be public and contradicts the support posture in
`design/telemetry.md`; website
`https://pe1mew.github.io/NTRIP-Analyser/`, which links the policy, the
manuals and the repository.

**Foreground-service declaration**: the `dataSync` justification plus a
demonstration video, recorded 2026-08-14 on the S23 against the public
anonymous `caster.centipede.fr/NEAR` — no credentials on screen, status
bar in demo mode so no carrier or battery level appears, and the app in
front throughout so no home screen or wallpaper is shown.
`https://www.youtube.com/watch?v=Lrmiezg9Gng`

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
- [x] Enable GitHub Pages (branch `main`, folder `/docs`) — **done
      2026-08-14**, `https://pe1mew.github.io/NTRIP-Analyser/privacy-policy`
      resolves and the app links it.
- [x] Publish the wiki — **done 2026-08-14**, twelve pages;
      `bash tools/publish_wiki.sh --push` republishes after any
      `docs/wiki/` change.
- [ ] Fill the contact address on both listings; the policy points at it.
- [ ] Confirm both editions install side by side and are distinguishable
      on the home screen.
- [ ] Upload the **app bundle**, not an APK: `gradlew bundleFreeRelease`
      → `app/build/outputs/bundle/freeRelease/*.aab`. Play refuses an APK
      for a new app; the APK remains the artefact for Samsung and for a
      self-hosted F-Droid repository.
- [ ] Upload from a release build signed with the release keystore, not
      the debug fallback (`android/keystore.properties.example`).
- [ ] Note in the listing that the app is **64-bit only**
      (`abiFilters`): a 32-bit-only ARM phone will find it listed as
      incompatible rather than failing to run.
- [ ] Once the closed track exists, confirm the tester opt-in link in the
      console and correct it in `readme.md` if it differs from
      `https://play.google.com/apps/testing/<package>`. The readme is the
      only place testers are recruited, and a dead link there costs
      fourteen days rather than a click.

**This list is Play's.** Samsung and F-Droid get their own, written from
the rules studies in `design/work-items/release-to-play.md` phase 10 —
not by assuming their requirements resemble these.
