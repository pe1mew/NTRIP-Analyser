# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/), and this project adheres to [Semantic Versioning](https://semver.org/). 

## [Unreleased]

### Fixed — a network mountpoint could not be checked at all

Testing the new GGA uplink against a real network service found three
separate faults, each of which turned a working station into a verdict
the tool could not deliver. `caster.centipede.fr` is the caster they
were all found on: open, anonymous, and the only public one we know of
that publishes NMEA mountpoints (`bin/centipede.json`).

**`--check` never sent a GGA.** Only `--check-vrs` did, so a plain
station check on a network mountpoint sat at "connected but no data
arriving" and reported FAILED — a measurement artefact dressed as a
station fault. Whether to uplink is a property of the mountpoint, which
the sourcetable's NMEA flag states and the check already reads for
KPI 8. It now says so as it starts:

    [CHECK] NEAR asks for a GGA uplink; reporting 52.230481, 5.942016

and the station passes: STATION OK, eight of eight, held for 60 s.

**The Android bridge parsed the first 512 sourcetable entries.**
Centipede publishes 1217 and lists `NEAR` at 816, so the entry was never
found, KPI 8 could only answer "no sourcetable entry to compare
against", and — because a run settles only when nothing is pending — the
check ran on for ever with no verdict. The table is now counted and
allocated to fit, as the CLI has always done: 1213 entries parsed, the
mountpoint found, 16 advertised types compared. The mountpoint browser
was truncated at the same 512, so a pro user could not have picked
`NEAR` either.

**A KPI that cannot be judged no longer stalls the run.** After the same
30-second allowance the rest of KPI 8 gets, an unjudgeable comparison
settles as a **caution** rather than staying pending: one of the eight
claims could not be checked, so the station is not certified OK, and the
operator is told which check was missing instead of watching a run that
never ends. Measured on a replayed capture with the mountpoint absent
from the table: CAUTION at 90 s, seven passes and KPI 8 warning, where
the same run previously never finished.

The Android bridge now also reports what its sourcetable fetch did
(`logcat -s ntrip_bridge`) — bytes, entries parsed, and whether the
mountpoint was among them. The C side prints to stderr, which on Android
goes nowhere, and "cannot judge" with no way to ask why is not a
diagnosis.

### Changed — the analysis views swipe, and the elevation plot stops forgetting

**Swipe navigation.** The station screen and the three analysis views
are one sequence: swipe left to go in, right to come back, and left or
right to move between the views. The buttons and the tab row are
unchanged — this is another way in, not a replacement, and nothing is
reachable only by gesture. Swiping does nothing when there is nothing to
analyse yet, which is the condition that already disabled the button.

**The C/N0-versus-elevation plot accumulates into the plot, not into a
list.** It kept every sample and, past 20 000, began discarding the
oldest — about eight minutes at forty satellites a second, so a watch
run of several hours plotted its last eight minutes and dropped the
rest, while shifting a 20 000-element list on every new sample. Samples
are now counted into the cell they land in and forgotten, which is all a
scatter ever needed: an eight-hour run and a two-minute one both keep
everything they measured, in the same 250 kB. Cells are drawn by
density, so a cell hit a thousand times no longer looks like one hit
once.

### Added — the Android app is ready to be released

Everything a Play upload needs, and nothing that changes what the app
measures.

**The version is no longer typed twice.** `app/build.gradle.kts` parses
`src/core/version.h`, the header every other artefact already reads, so
the phone cannot report a version no other program was built from.
`versionCode` follows from it as `MMmmpp` — 3.3.0 becomes 30300.

**Release builds are signed from a keystore this repository never
contains.** `android/keystore.properties` is git-ignored; without it
Gradle falls back to the debug key and says so, which produces something
that runs but that Play will refuse — the right failure, loud and at
submission.

**R8 is on**, with rules for the two things it cannot see: the JNI entry
points, bound by symbol name, and the serializers behind the JSON the C
side writes. Both failures are release-only, so the configuration was
proved rather than assumed: a minified pro build ran against
`ntrip.kadaster.nl` and returned **STATION OK**, held for 60 s, 47 SV,
46 of 46 satellites placeable — with the encrypted profile store
decoding and the sky render intact. APK: 1.75 MB.

**A new icon**, in every form the project ships it — the Windows `.ico`,
the Android launcher bitmaps, the adaptive and themed vectors, and the
512 px store asset — all generated from one geometry by
`tools/make_icons.py`, so they cannot drift apart. The mark is the sky
plot the app draws: the horizon as a ring, north as a tick, satellites
inside it. The signal arcs it replaces said only "something is
streaming", which is true of any network app. Free and pro differ by
accent colour, blue and amber, because on a phone with both installed
the labels truncate to "NTRIP Ana…" and colour is what tells them apart.

**The editions are renamed** to `NTRIP Analyser` and `NTRIP Analyser
Pro`. Play's metadata policy treats *free* in a title as promotional
text, and the word bought nothing: the price is already on the listing.

**The privacy policy is written** (`docs/privacy-policy.md`, published
from `/docs`), and the store listings and data-safety answers are
drafted with the reasoning behind each one
(`docs/work-items/play-listing.md`). One answer is worth stating here:
*is all user data encrypted in transit?* — **no**, because NTRIP sends
the position and the credentials over a plain connection. That is the
protocol rather than a shortcut, the app says so where the password is
typed, and it changes when TLS lands in both editions at once.

### Added — the GGA uplink reports where the rover actually is (Android)

A network-RTK mountpoint serves the position the rover reports, so what
that position is decides what the run is testing. Both editions now send
a GGA when the mountpoint's sourcetable entry asks for one, and the
**paid edition can report the phone's own position** instead of a fixed
one — the difference between *does this station serve the area it
claims* and *am I served properly here*.

Measured against a stub caster with the phone's own bridge code
(`android/app/src/main/cpp/ntrip_bridge.c` compiles on the desktop):
the first sentence arrives at 0.0 s rather than one interval late, then
at 10.0 s and 20.0 s, and a position moved at 12 s is reported at 20 s —

    stub:    0.0 s  <= $GNGGA,110943.00,5200.0000,N,00600.0000,E,1,...
    stub:   10.0 s  <= $GNGGA,110953.00,5200.0000,N,00600.0000,E,1,...
    stub:   20.0 s  <= $GNGGA,111003.00,5130.0000,N,00530.0000,E,1,...

With the mountpoint's `nmea` flag clear, nothing is sent at all. The
uplink moved off the session's built-in timer, which sends from the
position the session was opened with, onto `ns_send_gga()` driven by the
bridge — the mechanism the Windows GUI already uses.

**The phone's position is transmitted only after an explicit consent**,
asked once, naming the caster it goes to, and never in the free edition.
Without a fix — indoors, or with the app off screen, where Android stops
delivering location to a `dataSync` foreground service — the configured
position is sent instead. That fallback is not cosmetic: a GGA of 0,0 is
a valid sentence that puts the rover in the Atlantic, and a VRS will
answer it.

Typing coordinates on a phone is the worst part of the feature, so both
editions gained **From station** (fills the position from the
mountpoint's own sourcetable entry) and **Pick on map**, which opens the
user's map app — or OpenStreetMap in the browser — and reads the
coordinates back from the clipboard. Picking a mountpoint from the
sourcetable in pro now also takes its position and its `nmea` flag.

No map library is embedded in either edition. Osmdroid is Apache 2.0 and
would have been licence-clean; the objection is that it would make a
tool that collects nothing start fetching tiles that say where the user
is looking. `android/design/editions.md` has the whole argument.

### Changed — one configuration format for the whole project

There were two: a single-connection `config.json` for the CLI, the GUI
and the phone, and a `mountpoints[]` list for the monitoring daemon.
Two formats describing the same thing is a tax on everyone who touches
either, so there is now **one** — the list — and every program reads and
writes it.

Which entries a program uses follows from what it does. The analysers
examine one stream at a time, so they take the first and **say so**:

    [CONFIG] fromphone.json lists 2 connections; using the first (HANESE)
             and ignoring the other 1.

A user who exported five connections and sees one is otherwise entitled
to think the file was truncated. The daemon takes them all. The Android
pro edition merges them into its saved connections, updating one it
already has rather than duplicating it — loading a colleague's file
should gain you a connection, not cost you five.

The ephemeris block stays optional, per connection, exactly as it was.

**Files written by earlier releases still work.** The upper-case
single-connection layout is read everywhere it was before; nothing
writes it any more, so saving a configuration migrates it. That
compatibility is deliberate: those files exist on disks, in support
e-mails and in released assets, and they still say exactly what they
meant.

`docs/jsonConfigs.md` documents the format, which program uses how many
entries, and — in as many words — that the passwords in these files are
in the clear, with what follows from that.

Verified end to end: the phone wrote a two-connection file, the CLI read
the first and reported the other, and the phone read the file back
without duplicating either connection.

### Added — the station check in the Windows GUI

`View > Station Check`. The GUI had no acceptance test at all: the CLI
had `--check`, the phone had a station mode, and the GUI never linked
`kpi.c`. It runs the same engine over the stream already open — a
bounded run the user starts, ending in a verdict that stops moving,
because a reading that keeps changing cannot be quoted in a handover.

Wiring it up found a live defect in the GUI rather than in the new
window. KPI 8 reads the mountpoint's promise from the **session**, via
`ns_set_advertised()` and `ns_set_advertised_gnss()` — which the CLI and
the Android bridge both call and the GUI called neither of, computing
its own advertised-versus-observed answer from its Msg Stats ListView
instead. Left alone, the check would have judged against an empty
advertisement and passed every station. The session is now fed from the
sourcetable entry the GUI already fetches on connect, so the two cannot
drift apart. `SourcetableFindMountpoint()` returns the STR nav-system
field for the same reason: the 1005/1006 indicator bits cover GPS,
GLONASS and Galileo only, so a BeiDou-capable base cannot declare it
there and judging by those bits would fault every one.

The run state lives in `AppState`, not the window, so closing the window
does not abandon a ninety-second test, and it advances on the statistics
event rather than a window timer, so the KPI clock steps exactly once
per snapshot.

VRS assertions run when the station is classified as a network service.
The gate test stays opt-in: it stops the keep-alive GGA and waits for the
caster to drop the stream, which ends the session it is testing.

Two ways a run could end without saying so, both found by testing rather
than reasoning. A run stopped before settling left the header reading
"35 s elapsed, verdict held 4 of 60 s" — indistinguishable from one
still counting. And a run had no ceiling: a mountpoint the caster does
not list leaves KPI 8 pending for ever (rightly — "could not check" is
not a pass), a pending KPI holds the roll-up at RUNNING, and nothing
stopped it. The 300-second ceiling from the CLI now applies, and the
header names which of three ways the run ended.

Verified live on RFSEE01: CAUTION, settled after 91 s, held 60 s, KPI 8
warning — the same verdict `--check` reaches on the same station.

### Added — KPI 8, advertised versus actual

The acceptance test grew an eighth KPI, on every frontend: does the
station deliver what its sourcetable entry claims? An advertised message
type that never arrives **fails** — a rover configured from that
sourcetable will not receive what it was told to expect. Sending types or
constellations that were never advertised is an **observation**: a
warning, because the data is right and only the metadata is wrong.

Constellations are judged against the STR nav-system field rather than
the 1005/1006 indicator bits, and only in the one direction. Advertising
a constellation that is not currently streamed is ordinary — QZSS is
advertised across Europe and visible from none of it — and judging that
as a fault failed real stations three separate times before the rule
settled.

`--check` on RFSEE01 now returns CAUTION rather than STATION OK: the
station streams a constellation its sourcetable omits. The stream is
fine; the advertisement is not.

### Added — a regression test for the RINEX navigation loader

`test/test_rinex_nav.c`, run by `cmake --build build --target test_all`.
The project had no test infrastructure; this adds it, linking
`ntrip_core` only so a test run needs no network and no caster.

The fixture is nine real records from BKG's daily broadcast file: one per
constellation, two GLONASS records in the RINEX 3.05 shape back to back
as a real file has them, one shortened to the 3.04 shape, and an SBAS
record that must be skipped without losing phase. The back-to-back pair
is the point — a first fixture with a single GLONASS record passed
against the *broken* parser, because its four-line skip happened to land
exactly on the SBAS record.

### Fixed — GLONASS was missing from every RINEX navigation file

Three defects, and together they cost the free Android edition its only
orbit source.

**The parser lost phase.** RINEX 3.05 gives GLONASS a fourth orbit line
that 3.04 did not. The loader consumed a fixed three, and the leftover
line — taken for an unknown system — triggered a four-line skip that ate
good records. One GLONASS satellite survived out of 279 in a daily file.
Records are now read by structure, not by counting lines per system: an
epoch line names its system in column 1, a continuation line begins with
spaces.

**The validity window then rejected what did parse.** A daily file is
published a couple of hours after its last record, and GLONASS was
allowed two. Measured against the file's own later records, the
state-vector propagator drifts **0.1 km at 2 h and 1.9 km at 6 h** —
0.006° as seen from the ground, against markers about a degree wide. The
window is 4 h, as for GPS and Galileo.

**Ephemeris age was computed across incompatible scales.** It took the
entry with the highest week and toe, over systems whose week numbers
share no origin, and picked a NavIC record with a reference epoch 7.4 h
in the *future* — returning a negative age that the phone rendered as
"no orbits yet, the sky view cannot place anything" over a cache of 139
orbits. Age is now the smallest per-satellite age in each system's own
frame, and an epoch ahead of now counts as fresh.

Verified end to end with BKG's daily file: 113 → **139 satellites
cached, 135 usable**, GLONASS 1 → 27; the free edition places 46 of 46
tracked satellites from the imported file, and the GUI's sky plot draws
27 GLONASS satellites where it drew one.

### Added — the Android navigation-file import says what happened

Importing a file said nothing at all, so a file that copied cleanly but
held no orbits was indistinguishable from one that worked until a run
came up empty an hour later. The file is now read the moment it is
picked — off the main thread, through a session-free entry point, since
the orbit cache belongs to the process rather than to a stream — and the
dialog states the file name and the records accepted, or why none were.

Imports are also staged and promoted only if they carry orbits. Before,
the copy overwrote the live file first, so picking the wrong file from a
crowded Downloads folder destroyed a working set of orbits.

Gzip archives were already unpacked on the way in; the message now says
so, since nothing else did.

### Fixed — the Android sky view and its orbit card

A cluster of things the plot said that were not true. The satellite-orbit
card announced "0 of 41 tracked satellites have an orbit" in red beside a
view that was plainly placing 22 of them from the phone's own GNSS —
having no broadcast orbits is the free edition's ordinary state, not a
fault, so the coverage count now appears only when there are orbits to
count and the card names the source the plot was drawn from. The sky view
credited an "ephemeris stream" for orbits that came from the user's file.
Elevation ring labels disappeared under any satellite sitting on the
ring, and a satellite at zero elevation was drawn half outside the plot.

### Changed — the CLI help and docs said seven KPIs

`--check` reports eight. `docs/cli.md` also carried a sample output that
predated both KPI 8 and the rule that the *verdict*, not each row, is
what must hold for sixty seconds. Replaced with a live run. The GUI's
RINEX load message tallied five constellations and omitted NavIC, so a
file carrying 36 NavIC records reported none.

### Added — the station acceptance test (Android Phase 0)

`src/core/kpi.c`: the KPI verdict engine from the Android design,
in shared core per design-review D1 so the phone, a future CLI `--check`
and anything else judge a station identically.  Each KPI evaluates
PASS/warn/FAIL/pending against the session snapshot; the overall verdict
follows the design's rule — every KPI green for 60 continuous seconds is
STATION OK, a hard-KPI failure (connectivity, format, ARP, multi-GNSS,
CRC) is FAILED, the rest is CAUTION.

Validated live against RFSEE01: STATION OK with all seven PASS after the
full 60 s sustain window.

### Added — the sky-coverage plot on Android

`sky_render` gains an RGB entry point, so a frontend with its own way to
show a bitmap gets the pixels instead of writing a PNG to a temporary
file and reading it back.  The drawing is shared with the PNG path and
verified **pixel-identical over all 490,000 pixels**: the phone and the
desktop cannot disagree about what the sky looked like.

The plot needs orbits, which observations do not carry -- MSM says which
satellites are tracked, never where they are -- so the app opens a second
session against the ephemeris mountpoint, exactly as `--sky` does.
`rtcm_extract_arp_ecef()` returns the station in ECEF as well as
geodetic, since sector accumulation needs the frame's own coordinates
rather than a round trip back through geodetic.

Verified on the device against Kadaster: 66 ephemerides cached from
2003 frames, and the heatmap drawn on screen with the ARP footer.

Three defects surfaced along the way.  The final render sat *after*
`bridge.use { }`, so it measured a closed handle and reported no
ephemerides at all -- which read as a broken ephemeris stream rather
than as a lifetime mistake.  Sky availability was a function the UI
called rather than observed state, so Compose never recomposed when a
finished run produced a plot.  And the plot vanished with the session
that measured it; the coverage is now retained after the run, since a
spot check ends in eighty seconds and taking its sky with it is the one
moment the user wants to look at it.

The decoder chatter is swallowed into the sink the CLI uses: an
uninitialised `RtcmStrBuf` silently falls back to stdout, which on a
phone meant megabytes of ephemeris dumps in logcat.

### Added — the sourcetable browser on Android

Fetched off the main thread, filterable, and in the free edition
readable but inert.

### Changed — the badge no longer claims READY when unconfigured

An app with no mountpoint is not ready for anything; it now says NOT
CONFIGURED, in grey, which points at the settings card instead of
leaving the user to wonder why the run button does nothing.

### Added — the Android app, Normal mode (Phase 1)

`android/` now holds a buildable Android project: one screen, one
verdict, a row per KPI.  It connects, watches for about ninety seconds, and
shows STATION OK, CAUTION or FAILED.

No threshold lives in Kotlin — the verdict comes from `src/core/kpi.c`,
the same engine behind `--check`, so a station cannot pass on the phone
and fail in a script.  The NDK compiles the repository's own core and
session sources rather than a vendored copy; there remains exactly one
RTCM decoder in the project.

The C side is split deliberately.  `ntrip_bridge.c` holds the session
lifecycle, the KPI run and the JSON assembly in plain C99, and
`jni_glue.c` does nothing but marshal parameters.  JNI code cannot be
compiled without an NDK, so anything living there would escape desktop
testing — and this bridge was in fact validated before any Android
toolchain existed: replayed through a capture, then run live against a
caster for 77 polls, reaching STATION OK with a real broadcast ARP and a
document that parses as JSON.

The run itself is a foreground service.  A KPI verdict needs sixty
*continuous* seconds, which is longer than Android reliably lets a
backgrounded activity hold a socket; without the service the system
would pause the pump mid-window and the user would see a measurement
artefact reported as a station fault.

**Verified on hardware**: installed on a Huawei SNE-LX1 (Android 10,
arm64-v8a) and reached STATION OK against a live caster, all seven KPIs
PASS at 1738 B/s and 45 satellites.

Getting there found three defects that compiling could never have
caught.  `@JvmStatic` promotes companion externals to the enclosing
class, so the JNI symbol name needed was `..._NtripBridge_nativeOpen`,
not the `$Companion` form -- and confirming the symbols existed proved
nothing, since they existed under a name nothing looks up.  The Kotlin
model declared the snapshot's doubles non-nullable, but
`ns_stats_to_json()` emits null for anything unmeasured so that "no ARP
yet" is not "a station at 0N 0E"; the first document failed to decode,
and because a decode failure publishes nothing, the screen sat at READY
with no error while the run worked perfectly.  And a stream that never
opened ended the run immediately, dropping the UI back to READY instead
of reporting why -- it now keeps evaluating until the KPI engine returns
FAILED with the reason.

Two more came from watching a real run.  **The verdict never reached the
screen**: the loop published at most once a second and then broke on the
verdict, so a verdict reached mid-second was never published and the
badge read RUNNING 59 of 60 s forever on a station that had passed.  The
final document is now forced out before the loop exits.  And the badge
distinguishes **finished** from **stopped** -- a run that reached its
verdict is finished, and borrowing the abort word for it tells the user
their measurement was cut short when it was not.  A stopped run says so,
with "measurement incomplete".

Two further fixes came from first use: the caster field uses a URI keyboard,
because EMUI inserts a space after every full stop and filtering those
out as the user types fights the IME's composing region and duplicates
characters; and the ongoing notification shows a download icon, since
the app receives corrections and only the optional GGA goes up.

The toolchain is installed (JDK 17, SDK 35, NDK 27, CMake 3.22) and
the project builds: `assembleDebug` produces a 9.4 MB APK carrying
`libntrip_android.so` for arm64-v8a and x86_64, and every JNI symbol
exports with the `$Companion` mangling Kotlin's externals expect.  What
remains untested is runtime behaviour on a device; `android/readme.md`
records that, the toolchain setup for VS Code or Android Studio, the
deployment steps, and the two setup traps that cost time here — escaped
backslashes in `local.properties`, and pinning `ndkVersion`.

### Added — the VRS assertion set, and `--check` / `--check-vrs`

`src/core/vrs_check.c` turns the desktop's manual network-RTK checks into
assertions, in shared core alongside the KPI engine (design-review D2):
the caster accepts the GGA, corrections start within 10 s of it, the
broadcast ARP is near the rover, and the stream holds at the GGA cadence.
A fifth, the **gate test**, stops the uplink and lets the caster's
reaction classify the service — a drop means a live network service,
continued streaming means a fixed base.  That last one is reported as a
classification rather than a failure, because ignoring GGA is correct
behaviour for a physical station.

The CLI surfaces both engines:

    ntrip-analyser --check        # eight KPIs, ~90 s
    ntrip-analyser --check-vrs    # plus the network-RTK assertions

Exit codes make it scriptable for installer sign-off or cron: 0 STATION
OK, 6 caution, 1 failed.

Validated live: `--check` returns STATION OK on RFSEE01 with all seven
PASS, and `--check-vrs` against a Kadaster physical base passes A1–A4,
measures the reference position 2.48 km from the sent GGA, and correctly
classifies the mountpoint as not gated.

This closes the GGA-uplink-compliance and baseline-sanity halves of
backlog item 2.4, which had no assertion layer on any frontend.

### Fixed — the snapshot's ARP block was never populated

`arp_valid`, `arp_lat/lon/alt` existed in the schema from the start, are
serialised into every JSON snapshot, and were filled by nothing: the
daemon has published `arp_valid: false` for every station since
deployment, unnoticed because no Munin graph reads it.  The KPI engine's
first live run failed KPI 3 on a station that demonstrably broadcasts
1005 every 30 s, which is how it surfaced.

The session now extracts the ARP itself via a new quiet parser
accessor, `rtcm_extract_arp()` — unlike `decode_rtcm_1005()` it prints
nothing and touches no globals, so it is safe on the daemon's frame
path.


## [3.3.0] - 2026-08-11

Minor, because it adds capability: a window-layout reset, an
auto-reconnect checkbox in the main window, and a `--reconnect` CLI
flag.  Alongside those, polish of the 3.2.0 ionospheric release after
first live use — the Ionosphere Sky restyled to the coverage heatmap's
idiom, with correct PNG names and remembered placement.

### Fixed — Ionosphere Sky saved PNGs named after the wrong window

Pressing S in the Ionosphere Sky window went through the Sky Plot's save
helper, whose filename suffix comes from the Sky Plot's own mode — so
every capture was named `..._TrackedSats.png` whatever it showed.  The
window now uses the shared saver directly with its own suffix:
`ROTI-Heatmap` or `ROTI-Tracks`, matching which presentation was on
screen.

### Added — auto-reconnect visible in the GUI, and `--reconnect` for the CLI

The GUI setting existed only as a Tools-menu toggle, invisible until the
menu was opened — easy to believe it was never added.  It is now also a
checkbox in the Actions row beside Close Stream; checkbox and menu item
are one setting and stay in sync, and the confirmation dialog is gone
since the checkbox shows the state.

The CLI gains `--reconnect`, applying the session layer's backoff to the
stream modes.  Off by default there too: a finite analysis (`-t 60`)
should fail loudly on a drop rather than quietly extend its measurement
window.

### Added — View > Reset window layout

Forgets every remembered window placement and puts any open floating
window back at its factory size, cascaded from the main window.  The
Sky Plot, Ionosphere Sky and VRS Monitor remember their placements
across reopens; this is the way back when a remembered layout has
outlived its usefulness — a disconnected second monitor, say.  The
Ionosphere Sky also now remembers its own placement, opening at the Sky
Plot's actual size until the user resizes it, after which their own
choice wins.

### Changed — Ionosphere Sky matches the Sky Plot's look

The first cut drew pale floating patches on white.  It now follows the
coverage heatmap's idiom: every sector drawn with a thin grey edge and a
full-strength fill, empty sectors in light grey, so the plot reads as an
instrument.  Elevation numbers on the north axis, a dashed crosshair,
and the Sky Plot's footer (local time, mountpoint).  In the track
presentation, satellite labels -- G05, E11, C27 -- sit beside each live
marker, so a coloured trail traces straight to its row in the Ionosphere
table; markers and trail dots keep their original sizes.  The heatmap
shows no satellites at all: it is a picture of the sky, not of the
constellation.

## [3.2.0] - 2026-08-11

Minor: the ionospheric monitor, end to end.  A ROTI measurement core in
the session layer, validated against live data and the published noise
budget, surfaced in Stream Health, Session History, a per-satellite
table, a dedicated polar sky view with heatmap and 24 h timelapse
presentations, the statistics snapshot, and a Munin graph.

### Added — the ionospheric monitor is wired into every surface

The ROTI core now lives in the session layer, so the daemon, the GUI and
any future frontend report identical numbers.

- **Stream Health** gains an Ionosphere row: verdict, median and worst
  ROTI, dual-frequency satellite count, slips — with the caveat stated in
  the row itself that this is a temporal proxy at the base, not the
  base-rover gradient.
- **Session History** gains a seventh panel, ROTI on the shared time
  axis: a scintillation event reads as ROTI rising while C/N0 falls and
  CRC errors climb.
- **View > Ionosphere Sky**: a separate polar window -- the Sky Plot
  itself is untouched, since its 3-px trail dots are too small to read a
  colour from.  Space toggles two presentations: a **sector heatmap**
  (each sky sector filled with the verdict colour of the most recent
  ROTI measured there, same sector geometry as the coverage heatmap) and
  **24 h tracks** with every dot the ROTI at that moment -- the
  timelapse.  Verdict, mode description and a colour legend are in the
  header, and S saves a PNG.  SkyTrackPoint deliberately widened 16 to
  20 bytes to carry ROTI per trail point (~2.9 MB buffer).
- **View > Ionosphere**: per-satellite table — signal pair, ROTI,
  relative slant TEC, arc length, slips — refreshed each second.
- **Snapshot / daemon**: five iono_* fields in the JSON and CSV
  (additive, schema unchanged), and a new Munin graph `ntrip_iono` with
  warning at 0.5 and critical at 1.0 TECU/min.  Stale snapshots read U,
  as every other graph does.

Verified by replaying the 9-minute capture through the full session
path: identical results to the standalone core (QUIET, median 0.043
TECU/min, 32 satellites), JSON and CSV carrying the new fields with
36 columns matching 36 values.


### Added — ionospheric disturbance monitoring (core module)

`src/core/iono.c` measures how unsettled the ionosphere is above the
base, as **ROTI** — the Rate Of TEC Index, TECU/min — from the
geometry-free combination of dual-frequency MSM7 carrier phases.
Verdicts: QUIET below 0.5, UNSETTLED to 1.0, DISTURBED above.

ROTI rather than TEC, deliberately.  Differencing the geometry-free
combination between epochs cancels the carrier-phase ambiguity exactly,
so the rate needs no calibration; absolute TEC would need code levelling
plus satellite and receiver code biases that an NTRIP stream does not
carry.  Slant TEC is reported only relative to each arc's start, and the
module's header states plainly that a single station observes its own
pierce points, not the base-rover gradient that actually degrades RTK.

Per satellite it maintains a phase arc broken by cycle slips (lock-time
drop), gaps over 30 s, or a signal-pair change; picks the widest usable
frequency separation (L1/L5 over L1/L2 when both are present); and
excludes GLONASS, whose FDMA frequencies depend on a channel number only
the 1020 ephemeris carries.

Two mistakes were caught by validation rather than review, and are why
the constants are what they are:

- **Rates must be timed by the GNSS epoch in the frame**, not arrival
  time.  Six interleaved MSM types space one satellite's frames ~6× its
  true epoch interval apart, scaling every rate down sixfold — and
  network jitter would feed straight into the index.
- **ROT must be sampled at 30 s, not per epoch.**  The geometry-free
  scale is ~7.8 TECU/m, so 2 mm of phase noise differenced over one
  second is 1.3 TECU/min of pure noise — the first run reported a quiet
  ionosphere as DISTURBED, measuring the receiver rather than the sky.
  At the standard 30 s (Pi et al., 1997) the same noise is 0.044.

Validated on a 9-minute live capture from RFSEE01: median ROTI 0.043
TECU/min across 32 dual-frequency satellites — a textbook quiet
mid-latitude value — with 527 s unbroken arcs, and one satellite's
flapping L2P lock correctly breaking its arc 36 times rather than
producing false rate spikes.

Not yet surfaced anywhere: the GUI and daemon wiring (Stream Health row,
Session History chart, sky-plot overlay including a ROTI-painted
timelapse, dedicated window) is the next step of backlog 3.1.

### Added — a Doxyfile, and `cmake --build build --target docs`

Curated rather than dumped: Doxygen defaults everything it is not told,
so only the settings that actually differ are listed.  A full generated
configuration is thousands of lines in which the handful of real
decisions cannot be found, and it goes stale against every release.

The version is **not** written in it.  `PROJECT_NUMBER` reads the
`NTRIP_VERSION` environment variable, which the CMake `docs` target
supplies from `version.h`, so the documentation cannot contradict the
binaries.  A bare `doxygen` run still works and simply omits the version.
The target only exists when Doxygen is installed, so no build fails for
want of a documentation tool.

`lib/` is excluded — documenting vendored cJSON would bury this
project's API under a third-party one several times its size.
`EXTRACT_ALL` is off on purpose: with it on, undocumented entities are
published with empty descriptions and the warnings fall silent, which
hides exactly what the configuration exists to find.

### Fixed — Doxygen documentation duplicated between headers and sources

Ten functions carried a full `/** */` block on both the declaration and
the definition.  Doxygen merges the two, so `ParseMountTable` was
reported as having eight parameters when it takes four.

The contract now lives with the declaration in the header, and the
implementation files keep plain comments for notes that genuinely belong
there — the sourcetable field layout, why a branch exists.  File-local
`static` functions have no header and keep their blocks.

Also fixed: `@license` is not a Doxygen command and was silently dropped
from five file headers (now `@copyright`), four functions were missing
`@param` entries, and one `@ref` pointed at a member that cannot be
resolved that way.

### Removed — dead code in the RTCM parser

`decode_rtcm_1074`, `_1084`, `_1094` and `_1124` were per-type MSM4
decoders superseded by `decode_rtcm_msm4_generic()`, which the dispatcher
has called for some time.  `extract_signed38()` had no caller at all.
365 lines, none of them reachable.

Found by linking with `--gc-sections --print-gc-sections` and taking the
functions discarded by both Linux targets, then checking each candidate
against the whole tree — a function used only by the Windows GUI looks
dead from a Linux link, so the linker's verdict alone would have removed
live code.

Everything else the sweep flagged is deliberate public API with no
current consumer: `ns_run()`, `ns_handshake()`, `ns_uptime()`,
`ns_stats_gnss()`, `sky_collect_reset()` and
`msm_get_multiple_message_bit()`.  `ns_run()` in particular is justified
in `design/architecture.md` §3.2, and `msm_get_multiple_message_bit()` is
recorded in the backlog as deliberately exposed.  Removing those would
reverse recorded decisions, so they stay.

Replaying the reference capture gives byte-identical results either way:
206 frames, 39 satellites, 45.46 dB-Hz.  `-Wall -Wextra` reports nothing
across `core/`, `net/`, `session/` and `cli/`.

### Documented — every header declaration now carries a Doxygen block

The gaps were all in `gui/gui_state.h`, whose cross-module declaration
block used `/* gui_layout.c */` group comments in place of
documentation: fifteen functions, including all four worker-thread entry
points and the whole log-redirection mechanism.

The block now also states the threading contract, which was the part
that could not be inferred from the signatures: everything runs on the
UI thread except the four `Worker*` functions, which communicate only by
`PostMessage`, never touch a control handle, and are never blocked on.

### Fixed — `SHA256SUMS` could not be checked on Linux

CMake's `file(WRITE)` opens in text mode on Windows and turns each LF
into CRLF, so the checksum file the `release` target produced there
carried a trailing CR on every line.  `sha256sum -c` then treated the CR
as part of the filename and reported "No such file or directory" for
assets sitting right beside it.

It is now written with LF endings on every platform, via the only writer
CMake offers with newline control.

The failure appeared only on the platform that did **not** build the
file, which is why it survived local testing: checksums generated on
Windows had only ever been checked on Windows.  It surfaced when the
3.1.0 assets from both platforms were verified side by side.

## [3.1.0] - 2026-08-11

Minor: everything here is additive or a fix.  Two of the fixes matter
more than the new features — a config file missing a single key used to
crash every artefact, and the monitoring service's satellite and C/N0
graphs had read zero since the day it was deployed.

This release also completes the migration begun in 2.0.0: the session
layer is now the only RTCM framer in the tree.

### Changed — CMake now puts executables in `bin/`

The same directory `build-gui.bat` and the VS Code tasks have always
written to, so every build system agrees on one location.

The motive is not tidiness.  The programs read `config.json` from the
working directory, so configs naturally end up beside the executable —
and when that was a CMake build directory, a clean rebuild deleted them.
Caster credentials should not live somewhere `rm -rf` is the normal way
to get a fresh build.  Object files and CMake state stay in the build
directory, which remains disposable.

`.gitignore` now covers `bin/*` with an exception for the `readme.md`
placeholder that makes git create the directory at all — needed on top
of `*.exe`, since the Linux binaries carry no extension.

Verified on both platforms: binaries appear in `bin/` and run, the
`release` target still resolves its target paths and packages correctly,
and `git check-ignore` confirms the binaries, configs and captures are
ignored while `bin/readme.md` stays tracked.

### Added — an application icon

`gui/ntrip-analyser.ico`: a satellite — bus, solar panels, and a
downlink widening toward the ground — generated by
`tools/make-icon.py` rather than drawn by hand, so it stays adjustable.
Six sizes from 16 to 256, each rendered at its own scale rather than
downsampled from one large image.

Sizes below 32 drop the downlink and enlarge the satellite to fill the
tile.  Thin diagonals anti-alias into grey smudges at that size, so
carrying every element down to 16 gives a blob rather than a picture —
and 16 is where a tray icon spends its life.

Every window class and the notification-area icon now use it, through a
`GuiLoadAppIcon()` helper that calls `LoadImage` at the metric size.
`LoadIcon` always returns the system large size and leaves Windows to
squash it, which defeats having per-size renders at all.  The helper
falls back to the system icon if the resource is missing, since a window
with no icon reads as a fault.

The notification area is what motivated it: for a hidden window the icon
is the entire identity, and a generic one there says nothing.

CMake is told explicitly that `gui/resource.rc` depends on the `.ico`
and the manifest.  It scans `.rc` files for `#include`, but `ICON` names
its file by a bare path, which the scanner does not see — so editing the
icon left `windres` believing it was up to date and the executable kept
the previous icon.  A stale build that looks exactly like the change
having silently failed.

### Added — minimise to the notification area (Tools menu)

With the option on, minimising hides the window and leaves an icon in
the notification area.  Double-click restores it; right-click offers
Restore and Exit.  Off by default, so minimising behaves as it always
has unless asked otherwise.

The icon exists **only while the window is hidden**.  Showing it
permanently would put a second, redundant entry beside the taskbar
button; the point of it is to be the only remaining handle on the
program once the window is gone.

The tooltip names the mountpoint in **every** state, including when
idle: running two analysers side by side is normal — comparing a base
against a reference — and two icons both reading "not connected" give no
way to tell which window each belongs to.  It carries the satellite
count and data rate too, refreshed every second, since while the window
is hidden the tooltip is the entire user interface.

Two failure modes are handled deliberately: the icon is removed in
`WM_DESTROY` before anything else, since an icon outliving its window
leaves a ghost that only vanishes when the user hovers over it; and
turning the option off while the window is already hidden restores it
first, rather than stranding a window with neither taskbar button nor
icon.  Exit from the tray menu goes through `WM_CLOSE` so the normal
shutdown path runs — stopping the worker and closing any capture file —
instead of exiting from under it.

### Added — File > Export Statistics

Writes the session's statistics snapshot as JSON or CSV, chosen by the
extension you type rather than only by the filter dropdown.

The proposed name carries a `yyyymmddhhmmss_` prefix, matching the PNG
snapshots — `20260811170056_RFSEE01_stats.json`.  A folder of exports
then sorts by capture time, and repeated exports from one mountpoint no
longer all propose the same name and invite overwriting the previous
one.

It uses the same `ns_stats_to_json()` / `ns_stats_to_csv_row()`
serialisers the monitoring daemon publishes through, so an exported file
and a Munin sample describe a stream identically instead of in two
dialects that drift apart.  The GUI now takes the session snapshot once
a second (`stats_interval_s = 1.0`) to have something to write; it keeps
its own per-message statistics as before.

Truncation is treated as failure rather than written out as though it
were complete: the serialisers are snprintf-style, and a half-written
JSON object is worse than none.

### Added — auto-reconnect in the GUI (Tools > Auto-reconnect on drop)

The session layer has always reconnected with backoff -- the monitoring
service depends on it -- but the GUI opted out at both worker sites with
`auto_reconnect = false`.  It is now a menu toggle.

**Off by default**, deliberately: the GUI has always required a manual
reconnect, and silently re-establishing a stream changes what an
unattended run means.  The setting applies to the next stream opened,
not the running one.

The Stream Health tab gains a **Reconnects** row so a re-established
link is visible rather than showing up only as an unexplained gap in the
history plot.

### Fixed — a config file missing one key crashed the program

`load_config()` read required fields as
`strcpy(dst, cJSON_GetObjectItem(json, key)->valuestring)`, which
dereferences NULL when the key is absent and overruns a fixed buffer
when the value is longer than it.  A hand-edited `config.json` with one
key misspelt was enough to segfault every artefact that loads a config.
Confirmed by running the old code against a config missing
`NTRIP_CASTER`: immediate SIGSEGV.

Missing or wrongly-typed keys now leave the field empty, matching how
the optional `EPH_*` fields in the same function have always behaved.
Empty required fields were already reported downstream by
`--check-config` and by the connection attempt itself.

The same function also ignored `fread`'s return value and terminated the
buffer at the size reported by `ftell`.  Those differ routinely: the file
is opened in text mode, so on Windows every CRLF becomes one LF and
`fread` returns fewer bytes than `ftell` promised, leaving uninitialised
heap between the real end of the data and the terminator — which cJSON
then parsed.  It now terminates at what was actually read, and reports a
read error instead of continuing.

### Changed — the session layer is now the only RTCM framer

`run_sky_obs_stream()` was the last place that framed RTCM by hand.  Its
DNS lookup, socket, GET request, HTTP-header skip, GGA keep-alive and
receive timeout are all `ns_open()` now, and `grep 0xD3 src/cli/main.c`
returns nothing.  That completes the migration begun in
`design/architecture.md` §9: one framer, one stream loop, four frontends.

It carried the header-skip bug the session layer had already fixed:
`buffer[received] = '\0'` where `received` can reach `sizeof(buffer)` —
a one-byte overflow — and `strstr()` across a buffer not guaranteed to
be NUL-terminated.

Two deliberate behaviour changes:

- **"Connected" now means the caster accepted the request.**  It used to
  print the moment `connect()` returned, which is optimistic: a 401 or a
  404 connects perfectly well.  It is now reported once the handshake
  parses, and `--sky` treats "never accepted" as a failure.
- **GGA is sent only when a position is configured.**  The old code sent
  it unconditionally every 5 s, transmitting `0,0` when none was set.

Verified against a binary built from the previous commit: the
deterministic stdin path stays byte-identical (447 frames, 444 MSM,
119 kB, same PNG hash from both binaries run back to back), and live
40-second runs overlap on frames and sector updates.

### Changed — `--sky` reads stdin through the session layer

`run_sky_stdin_stream()` framed RTCM by hand.  It now opens the session
on `stdin` and does its per-frame work in a handler shared with the
observation source, removing one of the last two duplicates of the
session layer's framing and both copies of the per-frame body.

A correctness improvement comes with it: the old loop accepted any frame
with a plausible preamble and length **without checking its CRC**, so a
corrupted frame was decoded as though it were sound.  The session
validates the CRC first.

Verified against a binary built from the previous commit, both fed the
same capture: identical frame, MSM and byte counts (447 / 444 / 119 kB),
byte-identical PNG output when run back to back, and the same 3330
sector updates once the ephemeris cache had filled.

`run_sky_obs_stream()` still frames by hand; its per-frame body is
already the shared handler, so what remains is replacing its socket
setup and GGA push with `ns_open()`.

### Added — `ns_open_stream()`, for sessions fed from an open handle

The general form of `ns_open_file()`, which is now a thin wrapper around
it.  A caller can hand the session a `FILE *` it already owns — `stdin`
above all, which is how a capture is piped in for offline analysis and
which no path-based API can express.  `ns_close()` closes only what the
session opened, so a caller's handle survives.

Verified equivalent to `ns_open_file()` on the same capture (206 frames,
39 satellites, 45.46 dB-Hz through both), and on `stdin`.  Note that a
Windows caller must put the handle in binary mode first: in text mode
the same capture yields **one** frame instead of 206, because CRLF byte
pairs inside RTCM payloads are translated.

### Added — per-constellation C/N0 in the GUI's Satellites tab

The tab now shows **Seen**, **In View**, and C/N0 **Min / Mean / Max**
per constellation, read from the session's tracker rather than
recomputed, so it agrees with the daemon's Munin graphs by construction.

`Seen` and `In View` are deliberately both present: the first counts
every satellite observed since the session opened and never forgets one,
the second is the current five-second window.  "40 seen, 38 in view" is
the useful reading, and either number alone invites the wrong one.

C/N0 shows `-` rather than `0.00` where the stream carries none, since
MSM4/5/6 have no C/N0 field at all and a zero would read as a dead
signal rather than as a stream that never reports one.

### Added — satellite and C/N0 tracking in the session layer

The monitoring daemon's `sats_total` and `cnr_mean` graphs have read
zero since the service was deployed: the numbers existed only in the
GUI, computed as a side effect of drawing the sky plot, where nothing
else could reach them.

A new core module, `src/core/sv_track.c`, keeps a per-(constellation,
PRN) table of what the stream is carrying and at what C/N0.  The session
layer feeds it every MSM frame and summarises it into the snapshot, so
the daemon, the GUI and Android all count the same way.

It deliberately does **not** reuse the GUI's satellite view.  That view
computes azimuth and elevation, which needs a decoded ephemeris per
satellite *and* a station ARP, and goes blank when either is missing.
Counting satellites and averaging C/N0 needs neither — the PRNs come
straight from the MSM satellite mask and the C/N0 from the MSM7 signal
block — so the daemon now reports both from the first frame.

Two properties worth knowing when reading the graphs:

- The count is *currently in view*, not *seen this session*: a setting
  satellite lowers it.  A base losing sky view should show a falling
  line, not a permanently rising one.
- C/N0 requires MSM7.  MSM4/5/6 carry no extended C/N0 field, so those
  streams report satellite counts with `cnr_mean` at zero.

Verified by replaying a 206-frame capture and comparing satellite by
satellite against the pre-existing `extract_satellites()`: the two agree
exactly on all 39 satellites across GPS, GLONASS, Galileo and BeiDou, on
both Windows and Linux.  Mean C/N0 reads 45.5 dB-Hz, consistent with the
corrected MSM7 signal-block layout.

## [3.0.0] - 2026-08-11

Major, because an executable was renamed: scripts calling `ntripanalyse`
or `ntripanalyser` must be updated.  Everything else in this release is
additive or a fix.

### Fixed — the version number did not follow `version.h` either

`v2.0.1` was tagged on a tree whose `version.h` still said `2.0.0`, so
binaries built from that tag report the wrong release and its assets
would have been named `2.0.0`.  Having made every *displayed* version
derive from `version.h`, nothing was bumping `version.h` itself.

The `release` target now refuses to package when the git tag on HEAD
disagrees with `version.h` — packaging is the one moment where both
facts are present at once, so it is the right place to check.  Bump the
version in the commit that earns it, not at release time.

### Changed — the CLI is now `ntrip-analyser`

It previously answered to four names: the artefact constant said
`ntripanalyse`, the built binary was `ntripanalyser`, `--version` printed
a hardcoded `ntrip-analyser`, and the documentation was split between the
first two.  All four are now `ntrip-analyser`, which matches the product,
is consistent with `ntrip-analyser-gui`, and was already what `--version`
printed.

The shell-completion files are *named* for the command, so they were
renamed rather than edited: `share/bash-completion/completions/ntrip-analyser`
and `share/zsh/site-functions/_ntrip-analyser`.  Historical changelog
entries keep the old name — they record what the command was called at
the time.

**If you invoke `ntripanalyse` or `ntripanalyser` from a script, update
it.**  No compatibility alias is installed; with four spellings already
in circulation, adding a fifth path to the same binary seemed the wrong
direction.

One consequence worth naming: the rename made `NTRIP_ARTEFACT_CLI` and
`NTRIP_ARTEFACT_COMMON` the same string, which exposed an ambiguity that
was always there — `receive_mount_table()` is reachable from both the CLI
and the GUI and identified as neither.  It now takes its `User-Agent`
from the caller, so each front end names itself on the sourcetable
request too.

### Added — release packaging

`cmake --build <dir> --target release` builds and packages everything
available on the current platform into `<dir>/dist/`, named
`<artefact>-<version>-<os>-<arch>[.ext]` with the version read from
`src/core/version.h` rather than typed.

Installed binaries deliberately keep their plain, unversioned names — the
systemd unit, the Munin plugin, the documentation and any user script all
refer to them by name, and every one of those breaks if the name changes
each release.  Only the downloadable file is tagged, because it lands in
a Downloads folder stripped of all context.  The reasoning is recorded in
`design/architecture.md` §7.5.

The daemon ships as a tarball holding the binary, the systemd unit, the
sysusers fragment, the Munin plugin, an example config and the manual; a
bare executable would be about a quarter of the product.  Each platform
writes its own `SHA256SUMS-<version>-<os>-<arch>.txt`, since Windows and
Linux assets are built on different machines and a shared file would be
overwritten by whichever upload happened last.

### Fixed — Windows binaries depended on a DLL that is not part of Windows

MinGW links `libwinpthread-1.dll` dynamically by default, so a published
`.exe` would have failed to start with a missing-DLL dialog on any
machine without MinGW installed — which is every machine we ship to.  A
development machine has the DLL and cannot reproduce the failure, so this
was found by reading the import table of a release build rather than by
running it.

Both Windows executables are now linked `-static` and depend only on
system DLLs.

### Fixed — stale version and identity strings

Every version a user or a caster operator can see now derives from
`src/core/version.h`.

- **The GUI's About dialog reported "v0.1.0"** for the whole of the 2.0.0
  release (`gui/gui_events.c`).  It now composes its text from
  `NTRIP_PRODUCT_NAME`, `NTRIP_VERSION_STRING` and `NTRIP_COMPANY_NAME`,
  so it cannot drift from the binary it describes.  The Win32 version
  resource was already correct, which is what made the discrepancy easy
  to miss: the file properties said 2.0.0 while the About box said 0.1.0.
- **Two request paths still sent `User-Agent: NTRIP CClient/1.0`** — the
  sourcetable fetch in `src/net/ntrip_handler.c` and the `--sky`
  observation stream in `src/cli/main.c`.  Both are paths that have not
  yet moved onto the session layer, which had been building a correct
  header all along.  A caster operator reading a connection log was
  therefore told the wrong software under a long-obsolete version.
- **The monitoring daemon did not name itself.**  It never set
  `user_agent`, so it inherited the generic default.  It now sends
  `NTRIP ntrip-monitord/<version>`.  This is the one artefact that holds
  a connection open around the clock, so it is the one an operator most
  needs to recognise.

The `NTRIP <artefact>/<version>` convention had been spelled out by hand
at five call sites.  It is now a single macro, `NTRIP_USER_AGENT()`, in
`version.h` beside the artefact names, so a release bump remains one
edit.  The session layer's fallback now names the project rather than the
CLI: a caller that leaves `user_agent` unset is any front end, and
claiming to be the CLI put the wrong artefact in the caster's log.

### Fixed — the repository could not be built on Linux from a clone

`lib/` was tracked as `lib/cjson` while every build file referenced
`lib/cJSON`.  Windows' case-insensitive filesystem hid this completely;
on Linux a fresh clone failed with "No rule to make target
`../lib/cJSON/cJSON.c`".  It survived undetected because every Linux
build during development was fed by copying files from a Windows
checkout, which carried the capitalised name — never from a clone.

The directory is now tracked as `lib/cJSON`, matching upstream and every
reference.  `.gitmodules` is also removed: it declared `lib/cJSON` as a
submodule that does not exist — cJSON's 161 files are committed directly
— so `git submodule update --init` silently did nothing, which made the
real fault look like a submodule problem.


### Added — Stream health analysis

A new **Stream Health** tab answering "is this mountpoint healthy", not
just "is data arriving". Fourteen rows in connection order, colour-coded
by severity: red for real faults, amber for advisories, blue for
informational.

- **Caster handshake** — NTRIP 1.0 (ICY) versus 2.0 (HTTP), response
  status and caster software, with the full response headers written to
  the log. A caster answering ICY despite an `Ntrip-Version: Ntrip/2.0`
  request is reported as informational, since that is simply an NTRIP 1.0
  caster.
- **Frame integrity** — CRC-24Q error count and rate, malformed frames and
  framing re-syncs. The CRC result was previously computed and discarded.
- **Advertised versus observed message types** — the Msg Stats tab gained
  **Advertised** and **Status** columns comparing what the sourcetable
  promises against what arrives, with verdicts `ok`, `missing`,
  `slow`/`fast Nx` and `extra`. Advertised types are seeded as rows at
  connect, so a type that never arrives is visible rather than absent.
- **Reference-station position** — the sourcetable position cross-checked
  against the broadcast RTCM 1005/1006 ARP, plus detection of a fixed base
  that moves mid-session.
- **Station classification** — VRS / MAC / nearest-base mountpoints are
  identified from sourcetable keywords and from the reference point
  tracking the transmitted GGA. The fixed-base checks are suppressed for
  them, since a moving reference point is correct behaviour there.

Rate comparison is per **epoch**, not per frame: MSM splits one epoch
across several frames when the observations do not fit in one, so frame
counting would report a correctly-behaving base as sending at twice its
advertised rate.

### Added — Signal Quality window

`View → Signal Quality` shows C/N0 bars per satellite for the current
epoch, with a hover tooltip giving satellite, C/N0 and elevation, plus a
C/N0-versus-elevation scatter accumulated over the session with a
per-constellation mean in 5° bins. The scatter is the diagnostic view: a
clean installation rises monotonically from horizon to zenith, while
obstructions and multipath show as a dip at particular elevations.

### Added — Session History window

`View → Session History` plots throughput, message rate, CRC errors,
satellites tracked, mean C/N0 and reference-point drift over time on one
shared axis, sampled once per second for four hours. Each pixel column
shows the peak of the samples it covers rather than their mean, so a
one-second spike stays visible however long the session runs. This
supersedes the long-planned message-rate graph.

### Added — PNG snapshots for the new windows

`Ctrl+S` saves the Signal Quality and Session History windows as PNG. The
sky plot's save routine was factored into `SaveWindowPngWithPrompt()` in
`gui_snapshot.c`, so all three windows share one implementation and the
existing timestamped filename convention.

### Added — Sourcetable fetched on connect

Opening a stream for a hand-typed mountpoint now fetches the sourcetable
on the worker thread, so the advertised-versus-observed comparison works
either way. The fetched table also fills the mountpoint list rather than
being discarded.

### Fixed — MSM7 C/N0 read from the wrong bits

`msm7_extract_cnr()` and `msm7_update_per_band_cnr()` both assumed MSM
signal data is stored as contiguous 80-bit per-cell blocks, and read C/N0
at a fixed offset within each. MSM actually stores each field as its own
array spanning all cells — every fine pseudorange, then every fine phase
range, then every lock time, then every half-cycle flag, then every C/N0 —
as the full MSM7 decoder in the same file already did correctly. Both
extractors were therefore reading pseudorange and phase bits and scaling
them as if they were C/N0.

Measured over a 204-frame MSM7 capture, values spanned 0.75 to 63.94
dB-Hz and peaked in the 60–65 bucket, above the physical range for a
tracked GNSS signal. After the fix they span 35 to 55 and peak at 45–50.

This had corrupted the GUI's sky-plot C/N0 shading and the per-SV detail
window. **CLI output was not affected**: although the CLI links the same
parser, these two extractor helpers are only called from `gui/`, and the
CLI's MSM7 output comes from the full decoder, which reads the layout
correctly.

### Fixed — Rejected connections could be treated as success

The GUI worker tested the caster's response with
`strstr(header, "200") || strstr(header, "ICY")`, a substring search over
the entire header. A `404 Not Found` carrying `Content-Length: 200`, or a
`503` from `Server: caster/2.0.0 build 200`, both satisfied it, after
which the analyser would try to decode an HTML error page as RTCM. The
status line is now parsed properly, and a rejection reports the actual
status alongside the headers.

### Changed — Sky-plot trail samples carry C/N0

`SkyTrackPoint.ts` became a float offset from a session epoch rather than
an absolute double, which freed room for a C/N0 field at no memory cost:
the struct stays 16 bytes and the trail buffer stays 11.3 MB. A
compile-time assertion pins the size, since reintroducing a double would
cost 5.6 MB silently. The header's memory figures were also corrected —
they claimed 24 B and 17.7 MB where the struct was already 16 B and
11.3 MB.

### Fixed — GLONASS sky-plot jumps (two distinct causes)

Two separate bugs hit GLONASS sky-plot trails; both are now fixed.

**1. Lock-free ephemeris cache (eliminated the giant jumps)**

The `sv_ephemeris` cache previously did a non-atomic 268-byte struct
copy on writes from the eph worker thread, with concurrent non-atomic
reads from the obs worker.  Readers could observe a *torn*
`SvEphemeris` -- e.g. position from the new ephemeris glued to
velocity from the old.  Keplerian propagators (GPS / Galileo / QZSS
/ BeiDou / NavIC) shrugged this off because tearing a Kepler element
yields a sub-pixel shift on an 800-px plot, but GLONASS uses a
position+velocity state vector at a reference time and amplified the
inconsistency into 100-500 km errors visible as hard "jumps" between
consecutive 5 s track samples.

Each `(gnss_id, prn)` slot now holds two `SvEphemeris` buffers plus
an atomic `active` index.  Writers fill the inactive buffer in
place, then `__atomic_store_n(..., __ATOMIC_RELEASE)` the new index.
Readers do `__atomic_load_n(..., __ATOMIC_ACQUIRE)` and read the
published buffer.  Lock-free; the GLONASS state vector is never
observed half-written.

**4. Ephemeris-validity grace periods relaxed**

Galileo's grace period was the shortest of any constellation in the
cache (30 min vs 4 h for GPS / QZSS / NavIC, 6 h for BeiDou).  Any
brief gap in the upstream Galileo broadcast caused the eph to flip
invalid, the obs worker to skip that PRN in its sky-update loop,
and the track to fragment when valid eph returned >5 min later
(SKY_TRACK_GAP_BREAK_S).  Other constellations rarely hit their
graces and didn't show the same fragmentation.

Bumped:
  - Galileo:  30 min -> 4 h  (matches GPS now)
  - GLONASS:   1 h   -> 2 h  (for symmetry; broadcasts every 30 min
                              so 2 h gives 4× nominal headroom)

Orbit-propagation error after a few hours past `toe` is still
sub-pixel on an 800-px sky plot (~20 km for Galileo at 4 h = ~1 px
at 23 000 km), so the looser grace doesn't cost visible accuracy.

**3. Track buffer extended to 24 hours**

The per-SV `SkyTrackBuffer` ring buffer was sized for a 1-hour
window at the old 30 s sampling interval (`SKY_TRACK_CAP = 120`).
When the sampling interval was tightened to 5 s during the GLONASS
zig-zag diagnostic, the visible trail collapsed to the most recent
10 minutes -- shorter than typical capture sessions.

Bumped to `SKY_TRACK_CAP = 1440` at `SKY_TRACK_INTERVAL_S = 60 s`:
**24 hours of trail** per SV, ~17 MB total for all 8 GNSS × 64 PRN
slots.  The polyline renderer makes 60-s dots look continuous at
GLONASS orbital speed (~7 px apart on an 800-px plot).  Added
`SKY_TRACK_GAP_BREAK_S = 300 s`: the polyline runner splits the run
when consecutive samples are more than 5 minutes apart, so an SV
that sets and rises hours later draws as two separate arcs rather
than a straight chord across the plot.  Lower `SKY_TRACK_CAP` if
you want a smaller memory footprint at the cost of shorter trails.

**2. PZ-90 -> inertial velocity conversion (eliminated the residual
zig-zag)**

`glonass_to_ecef()` was integrating the orbital ODE in an inertial
frame (gravity + J2 + luni-solar, no Coriolis / centrifugal terms)
but using the broadcast velocity *as if it were inertial*.  Per the
GLONASS ICD § A.3.1, the broadcast `(pos, vel, acc)` triple is in
PZ-90 (Earth-fixed rotating).  Position is identical at the
reference instant `tb` (the two frames are spatially co-aligned
there), but velocity differs by the rotational term:

```
v_inertial = v_pz90 + omega_e x r
  v_inertial_x = v_pz90_x - omega_e * y_pz90
  v_inertial_y = v_pz90_y + omega_e * x_pz90
  v_inertial_z = v_pz90_z
```

Missing this conversion produced an `r(tb)`-dependent position error
on the order of tens of km.  Combined with the now-correctly-
working double-buffer cache alternating between two consecutive
GLONASS rebroadcasts at slightly different `tb`, the error magnitude
oscillated visibly tick-to-tick.  Adding the cross-product
straightens the trails.

### Added — CLI sky-heatmap mode

- **`-S` / `--sky`** — new CLI mode that opens both the observation
  NTRIP stream and (in parallel) an ephemeris NTRIP stream, accumulates
  observed/expected per-sector counts on the same 150-sector grid the
  GUI uses, and on **Ctrl-C** saves a heatmap PNG named
  `YYYYMMDDHHmmss_ARP-EPG.png` — the same convention as the GUI
  snapshot.  Prints "Collecting heatmap data: Ctrl-C to save PNG and
  exit, Ctrl-A to abort without saving" at start and a per-second
  status line (always visible) of the form:
  ```
  [/] frames=21  MSM=21  obs+exp updates=0  rate=  9.6 kbit/s  total=12 KB
  ```
  showing spinner, frame counters, current NTRIP rate (`kB/s`,
  matching the GUI status bar), and total bytes received.  **Ctrl-A** aborts immediately without writing a
  PNG (uses raw-mode `_kbhit`/`_getch` on Windows and termios
  ICANON-off + `select()` on POSIX so the keystroke needs no Enter).
  All RTCM decoder chatter (per-frame ephemeris blocks, station
  1005/1006 ARP info) is suppressed by default to keep the console
  clean; pass `-v` to also dump the full `decode_rtcm_*()` output of
  each frame.  Pre-flight rejects the mode if neither an EPH stream
  is configured nor a RINEX file is provided.
- **`-R` / `--RINEX <file>`** — RINEX 3 NAV file to preload the
  ephemeris cache before the live EPH stream takes over.  Useful for
  offline analysis of a captured stream, or as a fallback when no
  parallel eph NTRIP source is available.
- **New CLI module `src/sky_collect.{c,h}`** — distilled per-MSM
  sector accumulator (mirrors `gui_thread.c` obs-worker logic but
  threadless / GUI-less).
- **New CLI module `src/sky_render.{c,h}`** — portable polar
  heatmap renderer with an embedded PNG encoder (CRC32 + Adler32 +
  zlib stored-deflate).  No GDI+, no libpng, no zlib dependency.
- **New helper `run_eph_stream()`** in `ntrip_handler.{c,h}` — the
  CLI counterpart of the GUI's `WorkerOpenEphStream`.
- The lowercase `-s` / `--sat` short option keeps its original meaning
  (analyze unique satellites for N seconds); the new sky-heatmap mode
  uses capital `-S` / `--sky` to avoid the collision.

### Added — CLI scripting ergonomics

- **`--duration N`** — auto-stop `--sky` collection after N seconds and
  save the PNG normally.  Lets cron jobs / CI runs collect a fixed
  window without a manual Ctrl-C.
- **`-o <path>` / `--output <path>`** — write the `--sky` PNG to a
  caller-specified path instead of the timestamped default name.
  Scripts can pin the output location.
- **`-q` / `--quiet`** — suppress all informational chatter
  (`[OBS]` / `[EPH]` / `[RINEX]` / status line / `[SAVE]` lines).
  Errors still go to stderr; the saved PNG path still prints to
  stdout, so `OUTPUT=$(./ntripanalyse -S --duration 60 -q)` Just Works.
- **`--no-progress`** — keep informational lines but suppress the
  per-second status line specifically (useful for log files).
- **TTY-aware status line** — when stdout is a pipe / file instead of
  a terminal, the status line switches from carriage-return refresh
  to one-line-per-tick so log captures stay readable.
- **Final stdout line is the saved path** — `--sky` always prints the
  PNG filename on its own line as the last stdout output (or only
  stdout output under `-q`), making it trivially captureable.
- **Documented exit codes**:
  | Code | Meaning |
  |---|---|
  | 0 | Success |
  | 1 | Generic / runtime / connect failure |
  | 2 | Bad command-line arguments |
  | 3 | Could not open or parse config file |
  | 4 | `--sky` pre-flight: no ephemeris source configured |
  | 5 | Aborted by user (Ctrl-A) |
- **`--version`** — print `ntrip-analyser X.Y.Z` and exit.
- **Stderr / stdout separation** — all informational chatter (`[OBS]`,
  `[EPH]`, `[RINEX]`, `[SAVE]`, `[DEBUG]`, the status line) now goes
  to **stderr**.  Stdout is reserved for **data**: the sourcetable
  (`-m`), the decoded RTCM dumps (`-d`), stats tables (`-t`/`--sat`),
  the `--check-config` field listing, and the saved `--sky` PNG path.
  Scripts can do `OUTPUT=$(./ntripanalyse -S -q)` without filtering.
- **Per-field config overrides** (precedence: CLI > env > file):
  `--caster`, `--port`, `--mountpoint`, `--user`, `--password`,
  `--eph-caster`, `--eph-port`, `--eph-mountpoint`, `--eph-user`,
  `--eph-password`.  Same names as the `NTRIP_*` config keys, kebab-
  cased.  Lets one config file serve many mountpoints.
- **Env-var fallback** for every override flag above
  (`NTRIP_CASTER`, `NTRIP_PORT`, ..., `NTRIP_EPH_CASTER`,
  `NTRIP_EPH_PORT`, ...) — handy for CI secrets so credentials don't
  live in the JSON file.
- **`--check-config`** — dry-run mode that loads the config, applies
  env-var + CLI overrides, prints every resolved field on stdout
  (passwords masked as `(set)` / `(empty)`), does DNS lookups for
  both casters, and exits 0 on success or 1 if any field is missing
  / DNS fails.  Good for CI fail-fast checks.
- **`-v` verbose config dump now shows every field** — split into an
  `[Obs stream]` block (NTRIP_*, LATITUDE/LONGITUDE) and an
  `[Eph stream]` block (EPH_*); the eph block clearly labels
  itself `configured` vs `(not configured)`.  Passwords are masked as
  `(set)` / `(empty)` matching `--check-config`.

### Added — CLI Tier 3 ergonomics

- **`--json`** — emit one JSON status object per second on stderr in
  `--sky` mode instead of the human-readable line, plus a final
  `{"event":"stop","reason":"sigint|abort|duration|eof|error","saved":...}`
  summary.  Source-specific start event too:
  `{"event":"start","source":"ntrip|stdin",...}`.  Drop-in for `jq`.
- **`--rtcm-stdin`** — read obs RTCM bytes from stdin instead of
  opening the obs NTRIP socket; auto-stops at EOF.  Enables offline
  replay of captured `.rtcm3` files:
  ```
  ntripanalyse --sky --rtcm-stdin -R brdc.rnx -q -o out.png \
      < capture.rtcm3
  ```
  Same parsing pipeline as the live socket; sets stdin to binary mode
  on Windows to avoid CRLF mangling.
- **Action-flag conflict detection** — combinations like `-d -S` or
  `-m --sat` now error out with `[ERROR] Cannot combine action flags:
  ... and ...` (exit 2) instead of silently letting the last verb win.
- **Shell completion** — bash and zsh completion files under
  `share/bash-completion/completions/ntripanalyse` and
  `share/zsh/site-functions/_ntripanalyse`.  Covers all long/short
  options, file-path args (`-c`, `-R`, `-o`), and field overrides.
  See `docs/compile.md` for install paths.

### Changed

- **`src/rinex_nav.c` is no longer GUI-only** — it now compiles into
  the CLI build as well, so the same RINEX 3 loader feeds both
  applications.

### Added — Sky Plot and multi-GNSS

- **Floating Sky Plot window** (View -> Sky Plot...) showing each
  tracked satellite at its azimuth / elevation as seen from the
  reference-station ARP. Compass rose with 0/15/30/45/60/75 deg
  elevation rings, dotted N-S / E-W axes.
- **Two render modes** (M = toggle):
  - **Markers** — coloured per-GNSS dots (G green, R red, E blue,
    J magenta, C orange) shaded by CNR.  120-point ring buffer per
    SV draws a trail of past positions (~1 h of motion).  Left-click
    a marker for a live SV detail popup.
  - **Heatmap** — Onocoy-style observed-vs-expected coverage map.
    150 sectors (9 elevation bands x variable azimuth bins), red ->
    yellow -> green ramp on the ratio, light grey for polar hole.
- **Live ARP + clock footer** so PNG snapshots are self-contained.
- **Per-SV detail window** with PRN, az, el, best CNR, per-band CNR
  table (L1C/L2W/L5Q/E1C/E5Q/R1C/R2P/B1I/B2I/J1C/...), refresh
  timestamp, station context.  Copy button to clipboard.
- **Snapshot to PNG** (S on the sky window, or File -> Save Sky Plot
  as PNG).  Default filename `YYYYMMDDHHmmss_<TrackedSats|ARP-EPG>.png`.

### Added — Multi-GNSS ephemeris support

- **GLONASS** (RTCM 1020) state-vector decoder + RK4 numerical
  propagator with J2 zonal harmonic and luni-solar perturbations.
- **BeiDou** (RTCM 1042 D1 ephemeris) decoder with BDS-specific
  scale factors (2^3 s toc/toe, 2^-6 m harmonics).
- **QZSS** (RTCM 1044) decoder.
- **Galileo I/NAV** (RTCM 1046) decoder.
- Shared `sv_to_ecef` dispatcher routes Keplerian vs numerical
  propagator per GNSS.  GPS / Galileo / QZSS / BeiDou use the
  Keplerian path with per-GNSS gravitational parameters.

### Added — Ephemeris sources

- **Path A: dual-stream NTRIP** (already in prior commit; cleaned
  up here).  Optional second NTRIP connection feeds 1019/1020/1042/
  1044/1045/1046 frames into the eph cache while the primary
  obs stream carries MSM7.  Defaults to BKG products.igs-ip.net /
  BCEP00BKG0; works equally with Kadaster `BCEP00KAD0`.
- **Path B: RINEX 3 nav file loader** (File -> Load Ephemerides...).
  Reads multi-GNSS RINEX 3 nav files (G/E/R/J/C records) and
  populates the same ephemeris cache.  Useful offline.

### Added — RTCM capture and replay

- **Start/Stop RTCM Capture** in the File menu.  Worker writes
  every parsed frame's raw bytes to a `.rtcm3` file with a
  CRITICAL_SECTION-guarded handle.  Default filename
  `YYYYMMDDHHmmss_<mountpoint>.rtcm3`.  Auto-flushes on stream close.
- **Replay RTCM File** reads a captured file and feeds every frame
  back through the same UI-update pipeline (Msg Stats, satellites,
  sky plot, detail).  No pacing -- replay runs as fast as disk + CPU.

### Changed

- **Marker rendering**: brightness scales with the SV's best-signal
  CNR (~20 dB-Hz = dim, ~45 dB-Hz = full saturation).  Track trails
  drawn beneath the live marker in a desaturated GNSS colour.
- **Per-MSM7 frame**: CNR per (sat, signal) cached for the SV detail
  window.  Worker thread also extracts the best-of-signal CNR per
  PRN and forwards it as part of WM_APP_SKY_UPDATE.
- **Ephemeris Stream UI** now matches Connection Settings (same
  field widths, label names, two-row layout).  Fields start empty;
  populated only when a config file is loaded or the user types.

### Fixed

- **MSM7 verbose-decode bug**: `decode_rtcm_msm7_full` started
  reading at bit 0 where the 12-bit message number actually lives,
  shifting every later field by 12 bits and producing nonsense in
  the Msg Stats double-click detail of 1077/1087/1097/1117/1127/1137.
  Now consumes the message-number bits correctly.  Labels SVs with
  the right GNSS letter (E for Galileo, R for GLONASS, J for QZSS,
  C for BeiDou) and proper RTCM signal labels (L1C, L2W, E5Q, B2I,
  ...) instead of "S<NN>" placeholders.
- **`extract_satellites`** in ntrip_handler.c read the MSM sat-mask
  at bit 34 instead of 73 -- now delegates to the corrected
  `msm_extract_prns` helper.  Satellites tab shows the right PRNs.
- **`rtcm_printf`** g_rtcm_strbuf made thread-local so worker-thread
  decoder calls don't race the UI thread's detail-window string
  capture.
- **GPS week rollover** in `sv_eph_is_valid_at` and
  `kepler_to_ecef`: validity / propagation now compare in
  time-of-week only (with half-week wrap) so the 10-bit GPS-week
  field in RTCM 1019 doesn't cause every eph to be rejected.

### Build

- New source files: `src/sv_ephemeris.{c,h}`, `src/sv_orbit.{c,h}`,
  `src/rinex_nav.{c,h}`, `gui/gui_sky_window.{c,h}`,
  `gui/gui_snapshot.{c,h}`, `gui/gui_sv_detail.{c,h}`.
- GUI now links `-lgdiplus` for the PNG snapshot.
- CLI build line updated to include `sv_ephemeris.c` + `sv_orbit.c`
  (CLI doesn't need rinex_nav.c or the GUI-only files).
- Linux build globs `src/*.c` so it picks up the new modules
  automatically.

## [0.2] - 2026 (preceding the above)

### Added
- **Windows GUI Application** - Native Win32 GUI with real-time RTCM stream analysis
  - Interactive connection management and configuration
  - Real-time message monitoring and decoding
  - Live message detail windows with double-click support
  - Message statistics analysis (count, min/avg/max intervals)
  - Satellite counting and constellation breakdown
  - Configuration save/load (JSON format)
  - Clipboard support for copying data
  - Multi-format stream detection
  - Automated build scripts (`build-gui.bat`, `build-gui.ps1`)
- **MSM7 Full Decoder** - Complete rewrite with comprehensive structure
  - Reference Station ID and Epoch Time display
  - Multiple Message Flag, IODS, Clock Steering indicators
  - Divergence-free Smoothing and Smoothing Interval
  - Satellite data (rough range, extended info, phase-range rate)
  - Signal data (fine pseudorange, carrier phase, lock time, CNR, Doppler)
  - Support for GPS, GLONASS, Galileo, QZSS, BeiDou, SBAS (types 1077, 1087, 1097, 1117, 1127, 1137)
- **MSM4 Decoder** - Generic decoder for all GNSS constellations (types 1074, 1084, 1094, 1124)
- **Doxygen Documentation** - Comprehensive API documentation in header files
- **Distance and Heading Calculation** - From rover to base station for RTCM 1005/1006 messages
- **CRC Validation** - RTCM message CRC-24Q verification
- **CMake Support** - CMakeLists.txt for cross-platform builds
- **CI/CD Pipeline** - GitHub Actions workflow for automated builds
- **Debug Mode** - Server response display in ASCII and HEX format with `-v` flag

### Changed
- **RTCM 1013 Decoder** - Fixed System Parameters decoding, corrected field extraction
- **Documentation Structure** - Reorganized with simplified README and detailed GUI guide
- **Build System** - Added VS Code tasks for both CLI and GUI builds
- **Timing Measurement** - Fixed for Linux compilation

### Fixed
- RTCM 1013 message structure and field decoding
- Linux compilation timing measurement issues
- RTCM parser warnings (unused variables now displayed)

### Removed
- Leap seconds field from RTCM 1013 decoder (not part of specification)

## [0.1] - 2025-06-18

### Added
- Initial release with CLI application
- NTRIP client functionality (connect, authenticate, receive streams)
- RTCM 3.x message parsing foundation
- Basic message decoders (1005, 1006, 1007, 1008, 1012, 1013, 1019, 1033, 1045, 1230)
- Message statistics collection (count, intervals)
- Mountpoint list retrieval
- NMEA GGA sentence generation for rover positioning
- JSON configuration support
- Linux and Windows compatibility
- Basic documentation
