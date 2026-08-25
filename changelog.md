# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/), and this project adheres to [Semantic Versioning](https://semver.org/).  

## [3.7.1] - 2026-08-23

### Fixed — the legend, under the navigation buttons

On a phone drawn edge to edge with three-button navigation, the sky
view's coordinate line and constellation legend were drawn *behind* the
system's own buttons. Reported from the S23 against free 3.7.0 as it
came from Play.

**The source was never wrong.** The fix went in before 3.7.0 was tagged
and is inside the tag; what reached Play was an older binary. The
Android release is built by two commands, `assembleFreeRelease` for the
APK and `bundleFreeRelease` for the bundle Play takes, and only the
first was re-run after the fix. The GitHub release therefore carried a
corrected APK while Play carried a bundle built four and a half hours
earlier -- the same version number on two different programs.

So this release also removes the way that happened:

* `tools/check_release.py` now refuses to call a build submittable when
  an artefact under `app/build/outputs` is older than the source it was
  supposedly built from. A stale bundle is now a failed check rather
  than a screenshot from a user.
* The runbook builds APK and bundle in one command, and says why they
  may not be built separately.

## [Unreleased]

### Fixed — the first two tester reports, both editions

**The sky view survives a screen lock** (GH#1). Locking the phone for
even a second made every satellite "reload": the receiver's first
reports after an unlock carry almost nothing, and the app replaced its
whole placement map on every report. It merges now -- a fresh report
always wins, a satellite absent from one is retained, and retention
ages out on the same four-hour trust the orbit cache gives an
ephemeris: one duration, every position source. The long window is safe
because the merge overwrites coasted entries the moment fresh reports
flow; it only ever bridges gaps.

**The connection cannot be edited out from under a run** (GH#2). A
run's settings are captured when it starts -- deliberately, so a run
cannot change subject halfway -- but the tile stayed a door, and an
edit mid-run changed only the *next* run while the tile named the new
caster under the old verdict. The tile now keeps its place and loses
its tap and its forward mark while a run is going, exactly as Browse
already withdraws; the settings dialog, still reachable from the menu,
shows read-only with one line saying why, and offers no Save -- a
button that writes back what it read would only claim an edit happened.

### Added — where the reference position stands, in the paid edition

How far the service's reference position is from where you stand, live
and in the usability colours, with the history of where it has *been*:
a card on the hub, and behind it the first screen a card has ever
drilled into -- the desktop's VRS monitor redrawn in the analysis
screens' own template. Rover at the centre, the reference position at
its true bearing, history dots joined so a hand-over reads as a jump,
and five minutes of distance as a strip chart. A network switching
stations under you mid-survey is precisely the event a stream of
corrections lets you miss.

The movement sentence is chosen by what the service *is*, on the
desktop's evidence rule -- a reference position within 150 m of the
position you send is a network answering you, and a resolved gate test
outranks the guess -- so the same count reads as a network doing its
job or a base that should worry you, never both.

Underneath it, two snapshot fields that had been declared, serialised
and **written by nothing** since they shipped -- `arp_drift_m` and
`arp_moves` -- are now filled in the core, where 1005/1006 lands. A
move is judged against the last *recorded* position, so a station
creeping 9 m per broadcast still gets counted. The CLI's JSON and the
daemon's CSV carry honest numbers for the first time; the release
checks compare the 10 m threshold and the 32-position history between
core, app and desktop, so the three cannot drift apart. Phase 2,
item 3 (`design/work-items/handover-on-the-phone.md`).

First field use corrected the rover end: the distance had used the
position you *send*, which tap-to-use fills with the station's own
coordinates -- so the card read 325 m while its user stood 23 km away,
measuring the sourcetable against the broadcast ARP. The display now
prefers the phone's own fix, which never leaves the device and so needs
no consent a transmission would; the reading says which end it used
("of this phone" / "of the set position"); what is transmitted stays
behind the agreement, unchanged.

### Added — the network-RTK check, in the paid edition

The eight checks grade a station, and on a network service they can
actively mislead: a VRS computes its reference position near *you*, so
the position moves, and a moving position is what check 3 exists to
distrust. The five network-RTK assertions and the gate test have been
in the shared core since `--check-vrs`; the phone -- the thing a
surveyor actually carries to a VRS -- now runs them too.

One tap on the hub. The bridge owns the whole workflow, because only
the sender knows the moment a GGA was accepted by the socket, which is
what A1 and A2 are timed from; the run is a verdict on one connection,
so it does not reconnect; and when the checks have held, it stops the
GGA and lets the caster's reaction classify the service. **"Not gated
(fixed base?)" is a classification, not a failure** -- a single base
ignoring GGA is correct for what it is, and the row shows amber, never
red.

Three tests carry it: the engine against synthetic snapshots, the
bridge against a loopback caster through the same JSON the app decodes,
and the whole path live against a real caster, where the automatic gate
entry fired and the service classified as the fixed base it is.
`check_release.py` pins the crossing into Kotlin -- the gate enum's
order, the app's reading of it, and the assertion count the matrix
quotes. Phase 2, item 2 (`design/work-items/vrs-on-the-phone.md`).

### Added — the Watch card says when the stream dropped

The app has always reconnected by itself after a drop — a second, then
two, up to a minute, for as long as a watch runs — because a phone walks
into buildings and hands over between masts. It never said so. A watch
that lost the stream fifteen times looked exactly like one that never
faltered, unless you opened the evidence under check 1.

The Watch card now carries the count when there is one, under the
degradations line it explains: three degradations *with* reconnects is a
link that dropped, three *without* is a station that faltered, and those
are different faults. The report says it too. Nothing is shown when
nothing happened.

The card's lines are also the size a KPI row's evidence is. They were a
step larger, for no reason beyond the order they were written in.

### Fixed — a long capture is now a long record

Both analysis plots accumulated in the screen that draws them, so a
rotation reset them and a screen that was off recorded nothing at all: a
nine-hour capture drew its arcs from the minutes its screen happened to
be on, and its C/N0 scatter from 25 000 samples where nine hours holds
a million. What the plots showed was true; it was true of far less than
the run.

The record now belongs to the run. Both accumulators live with the
service that decodes the stream, are cleared when a run starts rather
than when a screen re-enters, and are fed where the document is
published. Measured off screen: 45 samples a second, against nothing
before. Satellites only the handset can place are still recorded by the
screen, because that is the only side that has the handset's fixes --
one satellite, one source.

The trail cap goes from four hours to a day, the desktop's own number,
so a nine-hour capture is nine hours of arc; the arcs are built once a
document rather than once a frame, and drawn thinner than the markers
they sit behind.

### Added — satellite tracks, in the paid edition

Where each satellite has *been*, drawn behind where it is. One epoch
shows a gap in the sky; a session shows a **shadow** — which is how an
obstruction at 20 degrees in the south-west tells itself apart from a
satellite that merely happened to be missing when you looked.

The rules are the Windows GUI's, so a trail means the same thing in both
products: one point per satellite per minute, arcs broken where more
than five minutes separates two points, so a satellite that set and rose
again is two arcs rather than a chord across the plot. The trail carries
the constellation's own colour lightened toward white, because history
should not compete with the live position.

One rule differs, deliberately: the desktop keeps twenty-four hours per
satellite at 11 MB, and the phone keeps four at about 300 kB — enough
for a watch-mode session, on a device that has other things to hold.

Nothing was added to the core or to the bridge. A trail is not a
measurement; it is a record of positions already computed, kept by the
screen that draws them, exactly as the C/N0-against-elevation scatter
already accumulates. Phase 2, item 1
(`design/work-items/satellite-tracks.md`).

## [3.7.0] - 2026-08-22

### Changed — every screen is drawn in one frame

The two Android editions had drifted into three top bars that disagreed:
the station screen kept a menu in the slot reserved for going back, the
analysis screen grew a filled badge where nothing else has one, and each
titled itself differently. A review of the whole layout produced a
template, and this release builds to it
(`design/guiV3spec.md`, `design/guiV3rollout.md`).

**One bar, from one place.** It takes no title parameter at all — the
app's own name is read inside it — so no screen can disagree about what
the app is called. Four slots and nothing else: back where there is
somewhere to go back to, the name, share, and a `⋮` menu that is now on
every screen rather than on the station screen alone.

**The way into the plots is pinned to the bottom.** It used to be a
button inside a card, which meant it slid off the screen under eight KPI
rows the moment a run started — the control you reach for while watching
a run was the one the run pushed away. It is part of the frame now, grey
when there is nothing to look at and gone on every screen but the main
one.

**Every touchable row wears a mark, and every mark means one thing.**
`▶` leads somewhere, `▼` folds open, `▲` folds away, and a row with no
mark does nothing when you touch it. The mark comes from the panel
contract rather than from each card, so a capability added later is
marked because it exists; measured from the accessibility tree, every
mark on the station screen ends at the same pixel. The station screen
also has one vertical distance now, the same between two cards as
between two KPI rows: panels that draw nothing no longer leave a gap
where they would have been.

**The analysis screens are six fixed bands** — tabs, what this view is,
the numbers behind it, the plot, what the plot is of, and the key to its
colours. The three views had drifted into three orders, with the sky
view's legend at the top and the other two at the bottom. The tab strip
sits directly under the app bar; the plot still keeps its minimum height
on a short screen and lets the screen scroll rather than being squeezed.

**The orbit badge is retired into the sentence about the plot.** What it
knew is not lost: the sky view's summary line names the source, carries
its age, is coloured by the same judgement the badge made — green for a
working source, red for a file too stale to place anything, amber for
the phone — and opens the same page when tapped.

**The station screen gives its height to the station.** Three faults in
one place, found on an S23 and invisible on a handset whose window is
smaller. The hub was laid out with `fillMaxSize()` ahead of its scroll,
which handed it a minimum of the whole viewport: a hub with little in it
was centred in the slack, floating in the middle of the screen, and
could be dragged about inside it, leaving a strip of nothing under the
title and another above the analysis bar. And its margins sat outside
the scroll, so those strips were a frame the content could never fill —
on every screen, for ever.

The hub is now as tall as what it holds and scrolls only when that is
more than fits; its margins are the first and last thing in the list, so
they give breathing room at rest and scroll away when there is more to
read. What was frame is now information.

**The frame keeps clear of the system's own bars.** Targeting SDK 36 the
app is drawn behind the status and navigation bars and cannot opt out.
The analysis bar now takes the navigation bar's height into account —
without it, on a phone with three-button navigation, the word *Analysis*
read through the buttons — and the content no longer pays the insets a
second time, which showed as a band of nothing under the title. On the
screens that have no bar to absorb it, the content takes that height
itself: the sky view's station line and its colour key were drawn
underneath the same buttons. All three appeared on an S23 and on neither
of the other test devices, which is the argument for testing on more
than one.

**A folded-open KPI row stays open** across a rotation and across the end
of a run, and folds shut when a new run begins. It used to shut itself at
the moment the run finished, which is when a reader wants it.

### Added — a stream that will not open now says which thing is wrong

Until this release every way of failing to open a stream produced one
sentence. The Android app said *"Could not open the session."* whether
the host name was wrong, the port was wrong, the password was wrong or
the mountpoint did not exist — four different things to go and fix,
behind one message that named none of them.

The information was never missing. `getaddrinfo` had failed, `connect`
had set `ECONNREFUSED`, the `401` had been parsed and stored. It was
computed and thrown away: printed to a `stderr` that Android does not
read, or collapsed into one of two end reasons before anything could see
it.

**Twelve failures, classified once, in the core.** DNS, refused,
unreachable, timeout, not-a-caster, 401, 403, 404, busy, refused-without
-words, dropped, stalled. The `errno` and `WSAE*` numbers are reconciled
in one place in `src/net`, because a frontend that mapped them itself
would be a second opinion about what a connection failure is.

Two distinctions carry most of the value, and are the ones a user cannot
work out alone:

* **A name that does not resolve** versus **a port with nothing behind
  it** is wrong address versus wrong port. The first never reached a
  machine; the second reached one and was turned away at that door.
* **Rejected credentials** versus **credentials that are right for
  something else** is a wrong password versus an account without
  permission for that mountpoint. Casters differ in which they send, so
  both are mapped rather than assumed.

Two cases were added while writing the tests: a caster that refuses in
words this version has no sentence for is named as that rather than
guessed at, and a `200` with a web page behind it is *not a caster* —
believing the status there is how a run spends its length wondering why
the "RTCM" will not decode.

**Every frontend says it.** The CLI names the fault in KPI 1's row and
again under the `exit=` line, where `-q` cannot silence it; the daemon's
journal names it beside the reason number (`session ended (reason 3,
auth)`); the Windows GUI logs it. The Android app maps the code to its
own strings — which is what leaves room for a translated build — and
shows the sentence under the verdict, where the sustain countdown would
be. A run that could not connect has no window to count.

**And the row that is wrong is the one to tap.** The connection card
opens the settings with the cursor already in the field the fault points
at: the password for a `401`, the mountpoint for a `404`, the caster for
the five that never reached one. A message that names the fault and does
not offer the fix is half a message.

Pinned by `test/test_failure.c` — a caster on the loopback interface that
answers as told, plus a port that was listening a moment ago, which is
what a wrong port number looks like. Thirty-four assertions, no network,
including the one that must not fire: a caster answering `ICY 200 OK` is
not a failure.

### Changed — format: two columns at the end of the statistics CSV

`failure` and `failure_detail` are **appended** to the columns written by
the daemon and by the Windows GUI, so a reader that counts from the left
keeps working. `failure` is the numeric code above; `failure_detail` is
the sentence. Both are `0` and empty while a stream is healthy.

The JSON snapshot gains the same two fields. The Android app reads it
with unknown keys ignored, so a phone still running 3.6.0 reads a 3.7.0
snapshot without noticing them.

## [3.6.0] - 2026-08-20

### Changed — the Android app is one screen you scroll, not two you swipe between

**This release adds one capability and rearranges everything else.** The
eight checks, their thresholds and their verdicts are untouched: the same
`src/core/kpi.c` decides, and a station that passed in 3.5.0 passes here
with the same words. What changed is where the answers sit and how you
reach them.

**The station screen is a hub.** It is built from a list of panels rather
than a hand-written column — verdict, connection, stream chips, the eight
rows, ephemeris, watch — and the list *is* the layout. That sounds like
an internal detail and is the whole point of the release: a capability
now arrives as one file and one line, contributing a card, a screen
behind it and a section of the shared report, instead of an edit to a
2,143-line activity. `MainActivity.kt` is 638 lines now.

**Every move is a control and every way back is Back.** The swipe between
the station and the analysis views is gone in both directions. It was a
second, invisible way to navigate that existed only between two
particular screens, and it broke once already on an Android release that
changed how overscroll consumes a drag. The Analysis button opens the
views; Back, the app bar's and the system's alike, returns.

**Rotating no longer loses your place.** The open screen and the selected
analysis tab survive rotation and process death. They never did, which
was invisible while there was one level to lose — and that same
invisibility had hidden two layout faults nobody could reach: in
landscape the sky plot collapsed to a dot at the centre, and the
C/N0-against-elevation plot to a line about ten pixels tall. All three
analysis views now keep a readable plot and scroll instead of squeezing,
and their markers and labels scale with the plot rather than staying at
full size on a third-size drawing.

### Added — share the result

An app-bar action on the station screen sends the run as plain text to
mail, a notes app, a file manager, or anything else that takes text.

```
NTRIP Analyser 3.6.0 — station report
2026-08-19 22:12:24

Verdict
  STATION OK
  held 60 s of 60 required
  run lasted 120 s

Stream
  rfsee.net:2101/RFSEE01
...
```

The report is assembled from the panels that drew the screen, in the same
order, so it reads like the screen it came from. **It cannot carry a
username or a password**: the snapshot it is built from contains no
credentials at all, the one panel that reaches into the settings names
only the caster and the mountpoint, and `tools/check_release.py` fails
the build if any section mentions either. The phone's own position never
appears; a position you typed may.

**And the plot goes with it.** On the analysis screen the same action
sends the view you are looking at as a picture — the sky plot, the signal
bars, or C/N0 against elevation — captioned with the view and the
station. It is captured as drawn rather than re-rendered, so what leaves
is what was on screen.

A statistics file is the one attachment still to come; the provider it
needs now exists.

### Changed — the free edition names what the paid one adds, once

A single **More in Pro** card at the bottom of the station screen, below
the controls, listing what the paid edition does *today*. Not greyed-out
rows in place of the real cards: a disabled control is indistinguishable
from a broken one to somebody who has not paid. Its wording follows the
published *What the paid edition adds* page, so the card and the page
cannot drift.

Nothing paid is compiled into the free build, as before. The editions now
differ by a **list**: the framework is one implementation in `src/main`,
each edition supplies the panels it contains, and `checkEditionParity`
fails the build if a flavour carries anything else or shadows a shared
file.

## [3.5.0] - 2026-08-18

### Fixed — a stream can stop without anything closing, and nothing noticed

On 18 August 2026 a monitored mountpoint had delivered nothing for
**14 h 10 min** while reporting itself connected. Two independent
measurements agreed: the socket's own `lastrcv` stood at 51,058,080 ms,
and `uptime_s − stream_time_s` came to 50,986 s. The caster had gone
quiet on a live TCP connection without closing it.

Nothing in the session could see that. `recv` returns "nothing yet" for
an idle socket and for a dead one alike, so the connection stayed
established, `connected` stayed true, and the reconnect logic — which
only ever ran on a close — was never reached. A monitor that rides out
drops does not ride out this, because to it nothing happened.

Sessions now carry a dead-man's switch: `NsOptions::stall_timeout_s`,
60 seconds by default, `0` to wait forever, settable per mountpoint in
the daemon's config as `stall_timeout_s`. Expiry is treated exactly as
a drop — same path, so a session that reconnects from a close cannot
fail to reconnect from a stall — and is reported as `NS_END_STALLED`,
its own reason because nothing closed and nothing failed. The CLI says
`Caster stopped sending; the connection was still open` rather than
ending quietly.

Silence is counted in bytes, not frames: a stream sending something the
framer cannot use is a different fault and must not be blamed on this
one. The timer starts when the socket connects, so a caster that
accepts and then never sends is caught by the same rule.

**Tested by being the fault.** `test/test_stall.c` runs a real listener
on an ephemeral loopback port that accepts, answers `ICY 200 OK`, and
then behaves badly on purpose — because what distinguishes this from
every other failure is a socket that is open and idle, which only a
socket can be. Four cases: a stream that stops, one that never starts,
the timeout switched off, and — the half that keeps this from being a
timer that always fires — a caster that keeps sending, which must
survive three times its own timeout untouched. With the check removed
the first four assertions fail; with it, twelve tests pass.

The second half of the same failure — tier 2 publishing `STABLE over
1.7 h` throughout those fourteen hours — is the entry below.

### Fixed — a report that stood behind a window which had stopped moving

Through the fourteen hours above, tier 2 published:

```json
{"window_s":6120.000,"overall_name":"STABLE",
 "headline":"STABLE over 1.7 h"}
```

Every figure in it was true. None of it was current. Tier 2 measures in
*stream* time — the property that lets a replay reproduce a live run
exactly — so when the stream stopped, the window stopped with it. The
daemon kept sampling every ten seconds, every sample carried the same
stream time as the one before, and the report went on describing a
period that had ended. From inside that arithmetic a window 1.7 h long
and a window that ended 1.7 h into a session now half a day old are the
same thing.

They differ only against the clock on the wall, so the report now keeps
that clock beside the stream's — not to measure anything with, since
every window here stays stream time, but to answer the one question the
stream's own clock cannot: whether it is still running. After
`SR_STALE_S` (**120 s**, settable as `stale_s`) with no movement, the
verdict reverts to `INSUFFICIENT EVIDENCE` and says how long it has
been.

Three decisions inside that:

* **It reuses `INSUFFICIENT EVIDENCE` rather than inventing a fifth
  word.** In both cases the report is declining to judge, and the
  vocabulary on this tier is already one word longer than tier 1's.
  The headline carries the difference: `the stream clock has not
  advanced for 300 s; this window ended then`.
* **It is checked before the length test**, because a window that has
  stopped will never reach the length it is short of — `600 s needed`
  would be a promise nothing is going to keep.
* **Live only.** A replay's host clock measures how fast the disk is,
  which has nothing to say about the station; a capture read in four
  seconds would otherwise look stale from its first sample.

120 s sits deliberately above `stall_timeout_s` (60 s): a stream that
merely stopped is now caught by the session and reconnected long before
this fires. What reaches here is the case reconnecting cannot fix — a
caster that accepts, is reconnected to, and still sends no observations.

Four cases pin it: an hour of healthy stream is `STABLE` while the
clocks agree and `INSUFFICIENT EVIDENCE` once they do not, a pause
shorter than the limit leaves the verdict standing, a replay is judged
on its stream rather than on how fast it was read, and a snapshot
carrying no uptime is judged exactly as it always was. With the rule
removed, three assertions fail and the headline reads `STABLE over
1.0 h` — the shuttle2 wording, reproduced.

### Added — a second tier of measurement: has this station *been* fit?

The eight checks answer whether a station is fit **now**, in about ninety
seconds, and that bound is what makes the verdict worth having. Some
questions cannot be answered inside it at any price — a cycle-slip rate,
a coverage percentage, a latency distribution, and simply *is this
station staying the way it was*. Rather than lengthen the check or grade
an hour's question on a minute's data, those now belong to a second tier.

`--report` prints it after any timed run (`-t`, `-s`, `-d`, `--check`).
Six measurements, none of them new — availability, frame integrity,
signal level, satellites held, ionosphere and delivery rate — all derived
from the snapshot every frontend already holds and the daemon already
writes once an interval. The tier had to prove its shape before new
metrics were funded for it.

Four properties are deliberate, and each is pinned by a test rather than
left to erode:

* **"Not enough evidence yet" is a verdict.** Below ten minutes every
  line reads `INSUFFICIENT EVIDENCE` and the report says how much more it
  wants. KPI 8 failed healthy stations three times by judging too early;
  that lesson is built in from the start here.
* **It never borrows tier 1's words.** `STABLE` / `DEGRADED` /
  `UNSTABLE`, never `STATION OK`. A station can be fit right now and have
  been unstable all week — two true statements, and a user seeing one
  vocabulary twice would conclude one of them is broken.
* **It does not touch the exit code.** `--check` owns that.
* **Live-only measurements are marked `n/a`, never zero.** Availability
  counts reconnections, which a replay cannot observe, so a report built
  from a capture says so instead of showing a clean zero it did not earn.

Two figures are measured as *changes* rather than levels, which is what
makes them worth an hour: signal level reports how far mean C/N0 fell
from the window's best — a 7 dB drop to 41 dB-Hz is flagged although 41
is a fine level, because something changed — and frame integrity reports
the worst CRC rate seen rather than the average, because an average hides
a bad ten minutes inside a good six hours.

Windows are counted in **stream time**, so the same session produces the
same report at any replay speed.

### Added — a stream clock, taken from the data

`stream_time_s` on the snapshot reports how much stream has been
observed as the *observations themselves* measure it, rather than how
long the host was watching. It is what makes the tier's replay-equality
property demonstrable instead of only unit-tested: a six-hour capture
read in twenty seconds covers six hours, not twenty seconds.

An epoch field is not a timestamp, and three things had to be handled
for this to be worth trusting. The constellations do not share a clock —
GLONASS counts a day where GPS counts a week, and BeiDou's week is
offset fourteen seconds — so the clock locks onto one and ignores the
rest. The field wraps, at a week and at a GLONASS day, and a six-hour
capture started on a Saturday evening crosses the first. And a frame
that arrives late is a step backwards that is *not* a wrap. A dropout,
by contrast, is not smoothed at all: ten minutes of silence advances the
clock ten minutes, because the epochs on either side say so.

A stream that carries no observation epochs reports no clock rather than
zero, and the report then says it has no window.

It is also the better clock for a live run, where the two used to agree:
a host NTP correction steps the wall clock sideways mid-session, and
epoch counting cannot be stepped.

### Added — the free Android edition is on Google Play

[NTRIP Analyser](https://play.google.com/store/apps/details?id=nl.pe1mew.ntripanalyser.free)
is live: the eight-check verdict, the sky view, C/N0 per satellite and
C/N0 against elevation, free and without advertising or an account.

Google's twelve-testers-for-fourteen-days rule for a new developer
account still applies, so the
[tester opt-in](https://play.google.com/apps/testing/nl.pe1mew.ntripanalyser.free)
remains open and joining it genuinely helps.

### Changed — the GUI's capture is the session's capture

The GUI wrote captured frames itself, one `fwrite` per frame, although
the session layer it already drives can write the same frames on its
own. That duplicate is gone: the write, the `FILE*`, the byte counter,
the auto-close helper and the file-opening in both menu handlers,
replaced by `ns_capture_start` / `ns_capture_stop` / `ns_capture_status`.
The menu items and the Save dialog are unchanged.

**What the GUI gains by losing the code.** The session refuses to
overwrite an existing capture and refuses a second one over a running
one; it reports an unwritable path as an error instead of capturing
nothing silently; it honours a size cap; and a failed write ends the
session `NS_END_WRITE_ERROR`. The GUI's own version had none of that.

**Both menu refusals are now pinned**, and mostly by what they leave
behind. A capture refused because the file exists must leave the file at
its original fourteen bytes, start nothing, and let the stream run to
its own end — a menu action is not the session's purpose, unlike the
same refusal at open, which ends the run. A second capture refused over
a running one must leave the first byte-identical to its source, which
is where a refusal that closed it, reopened it, or dropped the frames
written while the dialog was up would show and nowhere else. Fourteen
assertions; with the two guards removed, twelve of them fail.

**Proved before it was deleted, by a test that could fail.** The
verification on the books — a GUI capture and a CLI capture of the same
stream, compared frame counts — had no failing mode: the GUI never
framed anything, it wrote frames the session had already framed and
CRC-checked, so it compared one code path with itself, and two live
captures can only be compared statistically. Replaced by writing **one**
stream through **both** paths at once:

    81c065e6...  v6.gui.rtcm3
    81c065e6...  v6.session.rtcm3

Identical SHA-256, 121,467 bytes, 435 frames, from one session. The
first menu-driven capture after the deletion replays byte-identical and
its size resolves exactly — 44 x 1626 + 2 x 25 = 71,594 — so every epoch
was written whole.

**Said once, by whoever knows.** Both layers announced the start and the
stop, so the log read them twice — and two lines about one event are two
lines that can disagree. The session's are kept, because they come from
the code that opens, writes and closes the file; the GUI's are gone. The
one exception carries something the session cannot know: when a capture
ends because the stream ended rather than because anyone asked, the GUI
says so, and leaves the totals to the session's line below it.

**A note on threads.** The session belongs to the worker, so the menu
leaves a request and the pump loop acts on it between pumps, as the GGA
uplink already does. Calling into the session from the UI thread while
frames are being written is not something any lock in this program
covers.

### Added — the keyboard reaches the connection fields

Tab now moves from Caster to Port to Mountpoint and on through the
form, arrow keys move within a group, space toggles Auto-reconnect, and
Enter opens the stream — but only while Open Stream is available, since
a key must not do what a click cannot. Escape is swallowed: there is
nothing on this window to cancel.

Every control has carried `WS_TABSTOP` since it was written. What was
missing is that all of this is the dialog manager's work, and a plain
window never sees it unless the message loop offers it the message
first, which is the one line this took. The detail, sky and report
windows are unaffected: they are top-level windows of their own, so
`IsDialogMessage` declines their messages.

### Fixed — the GUI's Log tab, empty whenever the program was started from Explorer

Verifying the change above meant reading the log, and the log was empty
of everything the worker had to say — no handshake, no capture line, not
one of the ~200 `printf` calls behind it. What did appear came from the
UI thread, or from the ephemeris worker, which posts its lines as
messages instead of printing them.

**Started from Explorer there is no console**, so `stdout` and `stderr`
have no descriptor: `_fileno` returns a negative number and every
`_dup2` in the redirect fails without saying so. The pipe was then
created and pumped faithfully for the whole session with nothing ever
written into it. Attaching the streams to the null device first gives
them something to redirect. Started from a terminal the streams already
have a descriptor, which is why every previous check of this window —
all of them launched from a shell — looked fine.

Making the pipe carry text exposed two more faults it had been hiding:

* **A session in one line.** An EDIT control breaks a line on CR LF and
  nothing else, while everything behind the pipe writes `\n`. The pipe
  does not bridge that — both its ends are text-mode, so the expansion
  the write side performs the read side folds straight back. Expanded
  now at the single point where pipe text enters the control.
* **Three sources of noise, drowning the events.** A per-frame message
  type (`1087 1097 1117 …`, which replay already refused to print), a
  `[GGA] Sent GGA` every few seconds, and the caster's version mismatch
  narrated by both the session and the GUI. All three gone: Msg Stats
  lists every type with a live count, Stream Health holds the GGA count
  and last-send time, a *change* of position still logs, and the version
  mismatch is left to the session that read the answer.

Six log strings also carried a UTF-8 em dash into an ANSI control and
arrived as `â€"`; they use `--` now, like the rest of the log.

### Fixed — a check that accused a station of never producing

One report, two lines, in direct contradiction:

```
1  Connected and producing  FAIL  0 B/s  Connected but no data arriving
2  RTCM 3.x format          PASS  289    CRC-valid RTCM 3.x frames decoded
```

Two hundred and eighty-nine frames had arrived, been CRC-checked and
counted, and check 1 described the stream as one that never delivered.
Seen twice on live casters — `HANESE` on 2026-08-16, Centipede's `NEAR`
on 2026-08-17 — and in both cases the station was healthy: the caster
allows one session per account, and a second check evicted the first.
This tool exists to say whether a **station** is fit, and *"no data
arriving"* is what a user takes to the station's owner.

The verdict was right and has not changed: `--check` disables reconnect
on purpose, so a session that dies inside a run is a finding. The
explanation is now four messages where it was two, separating states
that share one instant on the throughput meter and mean entirely
different things:

| State | Message |
|---|---|
| Never connected | `No connection to the caster` |
| Connected, nothing ever sent | `Connected, but the caster has sent nothing` |
| Delivered, then silent | `Data arrived for 15 s, then the stream stopped` |
| Delivered, then the socket went | `Connection lost after 15 s of data` |

The seconds are the **session's** clock, not the check's, so a check
begun an hour into a stream reports the stream's life rather than its
own.

Deliberately, the sentence names no culprit. The plan for this was *"the
caster closed the session or the link dropped"*, and that is a guess the
engine is not entitled to make — a base that stops feeding its caster
looks identical from here, and the same sentence would then be
exonerating the station instead of accusing it. The causes, ranked, are
on the Troubleshooting page, where a reader can weigh them.

`test/test_kpi_stopped.c` pins all four messages, the verdicts they
carry, the clock the number comes from, and that the longest of them
still fits the GUI's Detail column. Six of its thirteen assertions fail
against the engine as it was, and the rest pass — which is how it says
the fix changed the sentence and not the verdict.

Verified without touching a public caster: a local one that feeds a real
57 KB capture and then goes silent, sends nothing, or closes the socket
reproduces all three states, since reproducing the original sighting
would have meant evicting a live station's session on purpose again.

### Fixed — a finished check that said "RUNNING"

`--check-vrs` against Centipede's `NEAR` ended with `== RUNNING ==` as
the last line of its report. The run had ended because the gate test
answered, which it can do before the eight checks have held their
sustain window — so there was no verdict, and the live roll-up was being
printed as though it were one. `RUNNING` at the foot of a finished
report reads as though the program were still going.

It now says what happened, and why, in the terms the GUI has always
used:

```
== NO VERDICT ==  the gate test finished after 211 s  exit=6
The checks above are the last reading, not a conclusion: the verdict had not held for 60 s.
```

The five ways a run can end are each named: the verdict settled, a check
failed outright, the gate test finished, the stream closed, or the 300 s
limit was reached. The sustain figure comes from the policy in force, so
it is right even when a file has changed it.

### Added — thresholds you can disagree with

Every verdict rests on a number someone chose, and
[docs/thresholds.md](docs/thresholds.md) is candid about which of them
are well founded: four are conventional or checked against real streams,
three exist because a defect exposed their absence, and the rest —
including all twelve tier-2 values — are reasoned but never measured
against a population of stations. A control network and a hobby base
should not inherit each other's numbers by accident.

So the `#define`s became the **defaults** of a policy the run carries,
and the CLI can load another over them:

```sh
ntrip-analyser --thresholds-print                  # what am I judging by?
ntrip-analyser --thresholds survey.json --check    # judge by that instead
```

The file is **partial by design** — it carries only what you change, and
every key it omits keeps its built-in value, so it does not rot as
thresholds are added. `bin/exampleThresholds.json` is a worked example.

Four decisions are load-bearing:

* **A bad setting is refused and named**, never clamped: a warn level on
  the wrong side of its bad level, a window shorter than the evidence six
  metrics need, a percentage above a hundred. **Nothing is
  half-applied** — a partly accepted policy would produce a verdict
  belonging to no stated standard at all.
* **A run under a policy says so**, with a fingerprint over the effective
  values. The name cannot carry that alone: two people may both call a
  file "survey", and only the numbers decide whether their verdicts are
  comparable.
* **One table drives parsing, validation and printing**, so those three
  cannot drift apart — a field cannot gain a parser and no bounds, or be
  loadable and never shown.
* **Thresholds still live in `src/core/`.** A frontend cannot invent a
  number; it can only hand core a policy it was given.

Phase 1 of that work is invisible by design — the engines read a policy
instead of the macros, every caller passes the built-in one, and nothing
changes. `test/test_policy.c` asserts it rather than assuming: every
default equals the constant it replaced, a report built with no policy is
identical to one built with the defaults, and — the property those two
cannot show — a policy that differs actually moves the verdict, in both
directions.

**A published report names the standard that produced it.** The
daemon's `<mountpoint>.report.json` gains `"policy"` and
`"policy_fingerprint"`, and the daemon says the same at startup. Without
it, two report files from two hosts are not comparable and nothing in
them says so — and a fleet's Munin graphs would silently mix standards.
The snapshot and the CSV deliberately do *not* carry it: they publish
measurements rather than verdicts, and a policy stamp there would imply
the numbers had been judged.

**Two release checks keep the page honest.** Every threshold macro in
the headers must appear in `docs/thresholds.md` — a number that decides a
verdict and is never explained is one nobody can argue with — and every
field of both policy structs must appear in the table in
`thresholds.c`, or it would be silently unsettable by a file and absent
from `--thresholds-print`. The first check found `KPI_EXPECT_UNKNOWN`
undocumented on its first run, and the page naming the wrong macro for
KPI 5's fallback.

**Every program can now be given a policy.** The service takes a
`thresholds` key — a path, or the policy inline — and **refuses to
start** if it cannot be applied, naming the field: an operator who asked
for a standard must get it or be told why, rather than publish months of
graphs judged against something else. The GUI takes **File > Load
Thresholds...**, applies it over the built-in values rather than over
whatever was loaded before, and **remembers the path** under
`HKCU\Software\NTRIP-Analyser` — the first thing this program has ever
remembered. A file deleted or broken since last time is reported once and
then ignored, so the program starts with the built-in values rather than
silently judging by something else.

One file serves all three: the CLI, the service and the GUI compute the
same fingerprint for it, which is what makes a fleet's verdicts
comparable.

**The network-RTK assertions are overridable too**, under a `vrs`
section: `accept_s`, `rtcm_s`, `arp_max_km`, `hold_s`, `gate_s`. They
are the least likely of these thresholds to need changing — they
describe what casters actually do rather than what a station ought to
achieve — but a network with unusual keep-alive behaviour would
otherwise have no recourse, and "unlikely to need changing" is not a
reason to make something unarguable. Their detail lines stopped quoting
their deadlines for the same reason tier 1's did: *"Corrections flowing
within 10 s of the GGA"* becomes a lie the moment a policy says 25.

Android keeps the built-in values; the platform has no file to point at,
and that limit is stated rather than worked around.

### Added — every check now shows what it was judged against

A verdict without the number behind it cannot be argued with. *"Median
C/N0 — 45.7 — healthy"* invites the question *healthy compared with
what?* and answers nothing.

Both tiers now carry a **limit** column, in the CLI's `--check` and
`--report` and in the GUI's Station Check and Stability windows:

```
4   Satellites held    INSUFFICIENT EVIDENCE            38  min 25
5   Ionosphere         INSUFFICIENT EVIDENCE 0.00 TECU/min  max 0.50 TECU/min
```

`KpiResult` and `SrMetric` gained the figure and its direction, set
where the verdict is decided; `kpi_limit_text()` and
`sr_metric_limit_text()` format it in core, so every surface prints the
same sentence in the row's own units and precision, and no screen can
show a threshold the engine is not using.

It also states something a fixed string could not: KPI 5's expectation
is the sum over the constellations a station streams, so a GPS+GLONASS
base is shown `min 14` where a five-system one is shown `min 29` — the
number that station was actually held to. The structural checks and the
VRS assertions leave the column blank, being tests rather than
comparisons.

The Android app is not covered yet; its bridge carries verdict, value
and detail, and the limit is a JSON field plus Kotlin away.

See [docs/thresholds.md](docs/thresholds.md), which documents all of
them with a rationale, and how confident each one is.

### Fixed — frame integrity could not detect what it exists to detect

Tier 2's frame-integrity metric documents itself as reporting the worst
CRC rate rather than the average, "because an average hides a bad ten
minutes inside a good six hours". It was reading the rate off the
statistics snapshot, which is **cumulative since the session opened** —
so the number it kept the maximum of *was* an average, and it failed
both ways round:

* **The first seconds decided the verdict.** Two errors against a small
  early denominator read as 0.93 % and stayed the "worst" for the rest
  of the run, against a station that settled at 0.43 % and then ran
  clean.
* **Later damage was invisible.** Six hours in, a station has some
  130 000 frames; fifty corrupted ones move the cumulative rate by
  0.04 %, far below whatever was banked early, so the maximum never
  moves. The bad ten minutes was precisely what it could not see.

Both were found on a live stream, one in the same screenshot as the
other.

**Frame integrity is now the share of frames that passed CRC**, over a
window of stream time: 100 % is a clean stream and the figure falls as
frames fail, which is how the question is actually asked. Tier 1 reads
the last **60 s** — its own sustain window, because a check has to be
able to change within the run or the sustain clock is timing a number
that can no longer move — and tier 2 the last **600 s**, matching the
window at which it will first judge anything. The thresholds are one
pair for both tiers: 99.9 % to warn, 99.0 % to fail, which is the same
standard as tier 1's old one error per thousand frames.

Before a window has closed, the metric reports no reading rather than
the perfect score it would have if asked to grade what little it has.

Three test cases pin it, each failing against the previous
implementation: a bad ten minutes inside ten hours is caught although
the session-wide figure ends healthy; a station is not condemned by its
first two hundred frames; and an incomplete window reads as no
measurement rather than as 100 %.

### Fixed — a warning whose number read zero

The station check showed `Frame integrity (CRC)  WARN  0.00  Elevated
CRC error rate`. The value is a rate, every KPI was printed at two
decimals, and 0.0043 renders as `0.00`. `kpi_value_decimals()` now gives
each check the precision its value deserves — three decimals for
integrity, so `99.743 %` can be read against the 99.9 % it is judged by,
and none for the six that are counts, so a satellite total stops reading
`40.00`. Applied in the CLI, the GUI and the Android bridge from one
place in core.

### Added — the stability report in the GUI

`View > Stability`, beside `View > Station Check`. The check asks whether
a station is fit **now** and answers in ninety seconds; this asks whether
it has *been* fit, over hours.

There is nothing to start. It accumulates for as long as the stream is
open and reads `INSUFFICIENT EVIDENCE` until it has ten minutes to judge
on — the shape commissioning wants, where you connect, work on the
antenna, and look at the verdict afterwards. **Restart window** begins a
fresh window without touching the stream, for the moment after you have
changed something and want the next hour judged on its own.

It never borrows the check's vocabulary: `STABLE` / `DEGRADED` /
`UNSTABLE`, never `STATION OK`. Replaying a capture is judged over the
window the capture holds, and availability reads `n/a` there rather than
a zero it did not earn.

**Wiring it up found the replay path emitting no statistics at all.** The
GUI's replay worker had `stats_interval_s = 0.0`, so the window would
have stayed empty for ever; and turning it on was not enough, because the
emit gate paced itself on the wall clock — a six-hour capture read in two
seconds would have produced two snapshots where the live run produced
twenty-one thousand. The gate now runs on the observation clock, so
"once a second" means once a second of stream. Live behaviour is
unchanged, because live the two clocks are the same one.

### Added — the monitoring daemon publishes the stability report

`ntrip-monitord` now writes `<mountpoint>.report.json` beside each
snapshot, atomically, on the same interval — the tier-2 verdict over a
rolling window, in a flat single-line shape the existing shell plugin
can read without becoming a JSON parser.

Two documents rather than one, because they answer different questions.
The snapshot says what is true *now*; the report says whether the station
has *been* fit, over hours. A station can be healthy this second and have
been unstable all week, and one file with one vocabulary would make those
look like a contradiction. Every existing reader of the snapshot is
untouched.

**The window rolls, and it had to.** A session-scoped report keeps the
worst value it has ever seen, which is right for a run of an hour and
worthless for a process that runs for months — one bad afternoon in March
would still be the verdict in June. The daemon keeps two staggered
accumulators and always publishes the older, so the report covers between
one and two `report_window_s` (3600 by default) and never goes blank at a
boundary. The clock is stream time: a station that goes silent stops
advancing its own window instead of banking the silence as health.

A metric that cannot be measured is published as `null`, never as `0`, so
a graph cannot draw "not applicable" as "fine".

**Munin draws it**, as an eighth graph family per mountpoint: the six
verdicts on one 0–3 scale, with degraded warning and unstable critical.
Insufficient evidence never alerts — it is the honest state for ten
minutes after every restart, and a monitor that pages on an upgrade is
one people turn off.

The version key in the report is `report_schema_version` rather than
`schema_version`, which is what keeps the two halves independent. The
plugin finds snapshots by globbing `*.json` and keeping whatever carries
a `schema_version`; under that name every report would have looked like a
snapshot to any plugin older than it, and drawn a phantom graph family
per station full of undefined values. As it stands an old plugin skips
the reports and a new plugin omits the family when no reports exist, so
either half can be upgraded first. Both directions were tested rather
than reasoned about.

### Fixed — a report claimed things it had not measured

For the first thirty seconds of every session — the warm-up, before
anything is sampled — the report stated *"no C/N0 in this stream
(MSM1-3)"* and *"no dual-frequency pair to measure with"* about stations
sending both. Those are claims about the station; an empty accumulator
has grounds for neither, and now says "gathering" until it has sampled
something. Visible only once the daemon began writing the report to a
file, where a terminal had scrolled it past.

### Added — reporting over a captured stream

`--rtcm-stdin` now works with `-d`, `-t` and `-s`, not only `--sky`, so a
`.rtcm3` written by `--capture` can be read back through the same
framing, CRC, statistics and report as the live run that recorded it:

```sh
ntrip-analyser -t 3600 --report --rtcm-stdin < 20260815131024_RFSEE01.rtcm3
```

The window is the capture's, not the replay's — six hours read from disk
in a fraction of a second are judged over six hours — and the duration
bounds the stream analysed, so `-t 3600` means the same thing live and
offline. Availability reads `n/a`, because a file holds no arrival times.

Three faults underneath the flag had to be fixed for the result to be
worth reading, and each was invisible until a capture was reported on:

* **Every mode but `--sky` ignored `--rtcm-stdin`.** `-t 600
  --rtcm-stdin` opened a live connection to the configured caster and
  analysed that for ten minutes while the file it had been handed sat
  unread on stdin. Unsupported modes now reject the flag rather than
  drop it.
* **Staleness was measured against the host clock.** Satellite tracking
  and the ionosphere ask how long ago something was seen, and six hours
  of file arrive in milliseconds — so offline, every satellite looked
  current and every epoch interval read 0.000 s. A replay is now timed
  by the stream it holds. **Live behaviour is unchanged**, because live
  the two clocks are the same one.
* **A replay read 8 KB per cycle**, which sampled a 1 Hz station every
  six seconds offline against once a second live. Now a kilobyte, and
  the same capture yields 88 samples where the live run yielded 89.

**A live report and its replay may legitimately differ.** In one
120-second run the live report recorded 29 satellites at its worst and
the replay of that same session recorded 38 — because delivery stalled
for seven seconds while the station's epochs ran on unbroken. A live run
answers *what am I being given*; a replay answers *what did the station
send*.

### Fixed — the snapshot JSON was not valid JSON

A missing separator between `advertised_gnss` and `types_missing`
produced `"advertised_gnss":5"types_missing":2`, which a strict parser
rejects — affecting the daemon's status output, the GUI's JSON export
and the Android bridge alike. Found while adding a key beside it.

Both serialisations are now parsed by a test rather than read by eye:
`test/test_ns_stats.c` runs a strict JSON reader and an RFC 4180 field
splitter over the output, so a missing separator, a duplicated key, a
CSV column present in the header and absent from the row, or a caster's
`Server:` header ending a string or inventing a column, each fail the
build. It knows nothing about the individual fields, and so keeps
working as fields are added.

### Removed — a monitoring signal that had never been able to move

`frames_malformed` is gone, and with it `NS_BAD_MALFORMED`, the GUI's
Malformed frames row, and the Munin *malformed frames* series.
**Nothing ever raised that reason**, so the counter was structurally
zero: a `case` in the GUI that could not run, a JSON key and CSV column
that were always `0`, and a `DERIVE` graph documented in the service
manual as one of seven families a reader could watch, which could only
ever draw a flat line. A monitoring signal that cannot move is worse than
an absent one, because flat reads as good news.

It was not a missing increment either. The framer deliberately treats a
byte outside a frame as ordinary — a stream legitimately begins
mid-frame, and NMEA between frames is common — and an implausible length
as a framing re-sync. Nothing was left for "malformed" to mean, so the
concept was retired rather than given an invented producer.

**`NS_STATS_SCHEMA_VERSION` is now 2.** A field was removed, which is
exactly what that counter exists to announce: a Munin RRD, an installed
phone build or an archived CSV outlives the release that wrote it.

Found by a new release check that reads every field of
`NsStatsSnapshot` and fails if nothing outside `ns_stats.c` writes it —
added after `latency_s` and `sourcetable_offset_m` were each discovered
by accident. It found seven such fields; this is the first resolved, and
the check now carries the rest as a list that may only shrink.

### Changed — the documentation, and the site that serves it

**One privacy policy for the whole suite** instead of one for Android and
silence about the rest. The desktop programs differ in ways a user should
be told: credentials are stored in the clear, unlike the app's
Keystore-backed store, and captures, exports and snapshots describe a
real installation — a `.rtcm3` publishes where an antenna is. A second
document would have restated the same truth in different words and
drifted, which is what had already happened to two other pairs of files.

**`docs/licences.md` is a statement of position, not a task list.** Its
"actions this study produced" table is gone: two of its four warnings
described work finished on 13 August, and the history belongs in git.
The one genuinely unsettled item — the IGS terms — is now a condition on
a feature that does not exist rather than an open task.

**The published site was quietly broken in two ways.** Nineteen links
across eight files used `../`, which works when browsing the repository
and 404s for every visitor, because Pages serves `docs/` as the site
root. And the theme gives content a 500 px column, so ninety-column
architecture diagrams were clipped mid-line. Both fixed, the first with a
release check so it cannot return, the second by widening the column to
1080 px while leaving the theme's mobile layout alone.

Also: the two documentation indexes merged into one, the service added to
the readme as the third desktop program with its Munin graphs, a favicon
and a repository social preview generated from the same mark as the
application icons, and the readme's licence section corrected — it
claimed the project was CC BY-NC, which is the documentation licence, not
the code's.

### Fixed — a bumped version could still package the old one

`cmake --build build --target release` printed *"Packaging 3.3.0"* on a
tree that said 3.4.0. The version is read with `file(READ)` at configure
time, and CMake was never told the build depends on that file, so an
existing build directory kept the stale cache. Only the release tag check
noticed; **untagged, it would have produced 3.3.0-named assets from a
3.4.0 tree in silence.** CI never sees this because CI always configures
from scratch — the release machine is exactly where it bites.
`CMAKE_CONFIGURE_DEPENDS` on `src/core/version.h` now re-runs configure
whenever the version moves.

## [3.4.0] - 2026-08-15

### Added — the CLI can write the stream to a file

`--capture <path>` writes every CRC-valid frame to a `.rtcm3`, and
`--capture-max <MB>` closes it at a size without ending the run. With
`--reconnect`, one file spans a dropped link: the summary reports the
reconnect count, which is how many gaps to expect in it. A **directory**
argument produces `YYYYMMDDHHmmss_<mountpoint>.rtcm3`, the name the GUI
already proposes.

Until now only the Windows GUI could write a capture, so the CLI could
replay a format it could not produce — and the people who need a
multi-hour capture, anyone declaring a base station, are on a Pi or a
VPS. The new part is **where it went**: not a copy of the GUI's code but
the session layer, which [architecture.md §3.3](design/architecture.md)
had listed capture-to-file under since the layer was designed. Both
programs now write the same file from one implementation, the daemon
gains the capability for free, and the GUI's private version becomes a
duplicate to retire.

Verified on the GUI's own 206-frame capture: replayed through the CLI
with `--capture`, the output is byte-identical to the input. Thirty
seconds from a live caster gave 159 frames and 62,503 bytes, matching the
message census frame for frame, and that capture replays and re-captures
identically. A sixth test, `test_capture`, pins the identity, the
filtering of junk and bad CRCs, the frame-boundary stop at the size
limit, and the refusal to overwrite.

**A write failure is fatal, with exit 7**, outranking every other verdict
including `--check`'s caution: if the file the run existed to produce is
not there, the station's grade is not the news. For the same reason a
capture refuses to overwrite an existing file — deliberately unlike `-o`,
which overwrites the sky PNG, because a PNG costs a minute to redraw and
a capture can be a day of streaming.

### Added — continuous integration

The project had none, and the reason to add it now is that everything
built to protect this launch runs only when somebody remembers the
command: five tests, thirty-four release checks, an edition-parity task.
For the next fortnight, twelve strangers are running this code.

Three jobs. **Core and tests** builds `ntrip_core` on Linux, runs
`ctest`, then `check_release.py` — the tests link the core alone and
touch no platform headers, so they run anywhere. **Both editions**
builds free and pro, which also runs `checkEditionParity`. **Verified
claims** runs `verify_memory.py` weekly rather than per-push, because
several of its commands ask whether the site and the wiki are live, and
a check that fails on somebody else's outage teaches people to ignore
it.

**The Win32 GUI is deliberately not built.** It is Windows-only with a
second hand-written build path, and a MinGW toolchain on a runner would
report more about the runner than about the code. That gap is written
into the workflow and the runbook rather than left to be discovered.

The memory index said *"Known gaps: no CI"*, verified by
`test ! -d .github/workflows`. Adding the workflow made that sentence
false and `verify_memory.py` failed on it within the minute — which is
exactly what it is for. Corrected.

### Fixed — dragging out of the sky view stopped working on modern Android

Reported on the S23: from the sky view, dragging right no longer
returned to the station screen. The Back button and the system's own
back gesture still did, and every other swipe worked, so it read as one
gesture quietly dying.

The screen treats what the pager **cannot** consume as "leave this
screen". From Android 12 the stretch overscroll consumes exactly that
leftover in order to animate with it, so the nested-scroll parent saw
nothing at all; the older glow effect only draws, which is why the
gesture had always worked on the Android 10 handset it was built on.

The pager now runs with overscroll disabled — a stretch animation is a
fair price for a gesture that navigates. Verified on the S23 before and
after, and reported working by the author.

### Changed — the app targets Android 16 (API 36)

Play refuses new apps and updates below API 36 from **31 August 2026**,
and that deadline cannot be beaten: the closed test alone needs twelve
testers opted in for fourteen continuous days. So this is required work,
not housekeeping.

`compileSdk` and `targetSdk` are 36, on **AGP 8.11.2 / Gradle 8.13**.
AGP 8.7.3 did build against 36, but warned that it was untested — not a
thing to ship on.

**Edge-to-edge was the price, and it showed.** Android 16 will not let
an app targeting 36 draw inside the system bars. The layout held, but
the status bar's icons stayed light on our light background: on the S23
the clock, signal and battery were all but invisible. Those bars are
drawn over the app's own surface now, so their appearance is the app's
to set — `AppTheme` now sets it from the theme, verified in both
directions with `cmd uimode night`.

Re-verified on the S23 at target 36: a full check to a settled verdict,
the service released cleanly, parity, five tests, and two new release
checks — one refusing a `targetSdk` below Play's floor, one insisting
the native build still asks for 16 KB alignment.

### Verified — both editions on a Samsung S23, Android 16

The app grew up on an Android 10 handset, where the rules it now has to
obey do not exist. Run on **SM-S911B, Android 16, SDK 36**, installed
from the **app bundle** through `bundletool` as the exact split set Play
would deliver: `base`, `arm64_v8a`, `nl`.

- **No release-only failure**: no `UnsatisfiedLinkError`, no screen
  stuck at READY. R8, the bundle and the splits work together.
- **`POST_NOTIFICATIONS`** prompted on first launch and was granted —
  the Android 13+ path, which the old handset can never exercise.
- **The foreground service runs as `dataSync`** (`types=0x00000001`),
  accepted by Android 16.
- **Doze**: forced deep idle for three minutes in the middle of a watch.
  The service stayed foreground, tracked satellites went 49 → 52, and
  the sky kept updating. Six minutes of watch produced 11 380 C/N0
  samples and 1418 ephemerides off the station's own stream.
- The orbit badge showed **Station orbits** for the first time on
  hardware, Centipede NEAR broadcasting its own orbits.
- Pro parsed a **1212-mountpoint** sourcetable, which is the count-then-
  allocate fix holding on a fourth Android version.

**Only the signature is unverified**: these builds carry the debug key,
because the release keystore is the author's to create.


### Changed — the release plan covers three stores, and the readme recruits testers

**Free first, to Google Play, the Samsung Galaxy Store and F-Droid.**
One edition in three places is a smaller job than two editions in one,
and it puts the tool in front of the people who will find its faults.

**Play is the long pole and cannot be hurried.** The developer account
is in verification, and a new personal account must then run a closed
test with **twelve testers opted in for fourteen continuous days**
before it may apply for production access. So the invitation now sits at
the top of `readme.md`, with the three free screenshots, what a tester
is agreeing to, and the request that matters most: tell us about a
station where the verdict looks wrong.

**F-Droid is settled rather than studied.** Its inclusion policy
requires Free, Libre and Open Source Software, judged against the DFSG,
the FSF, GNU and the OSI — all four of which forbid restricting sale,
and `license.md` says "you may not sell it". The official repository and
the Commons Clause are mutually exclusive by definition, and no
packaging work would change it. **The free edition will be published
from a repository we host ourselves**: the same client, the same update
mechanism, added by URL, and no licence change — self-distribution was
never in tension with a clause that restricts others from selling.
Relicensing the free edition was rejected because the editions share one
core, so the Clause would have to come off the shared code too.

**Samsung still gets a rules study**, but nothing known blocks the free
edition there; it has no FLOSS requirement, and is in fact the
arrangement the Clause was written for. Selling **pro** on Samsung, and
whether pro belongs in a self-hosted repository at all, are deferred
until free is out.

The phase table was also brought up to date: phases 4, 5, 7 and 8 have
been done for a while and still read "not started".

### Changed — the password field is masked, and the station's position is not published

Two things a screenshot session turned up, both about what ends up on
someone else's screen.

**The password field showed the credential in clear text**, in both
editions. A credential on screen is a credential in every screenshot,
over every shoulder and in every screen recording -- one capture of it
was taken on the way to a store listing and deleted before framing.
It is masked by default now, with a **Show** toggle, because a password
typed on a phone keyboard and never checked is the other way this field
goes wrong. The toggle never persists: the dialog opens masked.

**The sky view's footer carries the station's ARP to six decimals** --
about a tenth of a metre, and this station stands where its owner lives.
The framing tool now replaces it with `52,xxxxxx, 5,xxxxxx`, keeping the
shape of what the app reports without the value, in both editions'
screenshots. Redaction boxes carry their own fill and ink colours now,
since this one sits on the page rather than on the connection tile.

### Added — the free edition's store screenshots

Four, shot on a release build against the author's own station: the
STATION OK verdict with its first three checks, C/N0 against elevation
(2984 samples from one 90 s check), the sky view placing 38 of 41
satellites from an imported navigation file, and signal quality.

Three things the shooting turned up, all now fixed rather than
remembered:

- **The redaction box is per edition.** Free's verdict banner grows two
  lines once a check finishes, pushing the connection tile 66 px down,
  so the single shared box would have painted over free's *mountpoint*
  instead of its caster. `REDACTIONS` is keyed by edition, and an
  unknown edition is now refused rather than framed unredacted.
- **The sky caption claimed something no capture showed.** "Placed from
  the station's own orbits" — but this station broadcasts none: pro's
  came from the ephemeris stream, free's from a navigation file. It now
  reads "placed from broadcast orbits, not from this phone", which is
  true of all three sources and is the distinction that matters. Pro's
  sky shot was re-taken to carry it, 39 of 39 from the stream.
- **The sourcetable shot was dropped** from both sets. Its dialog is
  titled with the caster's host, and this caster lists two mountpoints —
  thin evidence for a caption promising every mountpoint.


### Changed — the release work items and the security review moved out of the published folder

Enabling GitHub Pages made every file in `docs/` a web page, including
three that were never written for a reader: the store listing drafts,
the data-safety answers and the launch plan. They were already public —
the repository is — but a page is indexed and linked in a way a file in
a repository is not.

They now live in `design/work-items/`, beside the other internal
documents, which Pages never sees. `security-review.md` went with them:
it is an assessment written for the author, and while it discloses
nothing an attacker lacks — the two parser defects in it are fixed, and
the open item is a property of NTRIP that the app already states under
the password field — a page is read differently from a file.

It stays findable rather than hidden: `docs/index.md` and the docs
readme both link it in the repository, because a security document
nobody can locate is a problem of its own. `docs/` is for documents written to
be read by someone who is not us; `design/` is for the rest, and
`docs/_config.yml` says so where the decision is enacted.


### Fixed — a day-old GLONASS orbit could place a satellite

Found while testing the badge, on the handset. The orbit card read
*"newest orbit 58 min old"* over a plot drawing nothing from those
orbits, with a ten-hour-old file loaded.

GLONASS broadcasts **no week number**: its reference epoch is Moscow
seconds of day. Age can therefore only be computed modulo one day, so a
record from yesterday morning lands a few hours behind this morning and
passes every test — including the four-hour validity check that decides
whether a satellite may be *placed*. That is not a display fault: the
sky view would draw such a satellite from a twenty-four-hour-old orbit,
as confidently as from a fresh one.

`SvEphemeris` now carries `toe_utc`, an absolute date, filled wherever
one is known — which is any file, since RINEX records carry a full
calendar datetime. Where it is set it decides; where it is not, the wrap
still governs, which is correct for a live stream whose ephemerides are
seconds old by construction. `test/test_eph_validity.c` pins both halves,
and was checked to fail when the absolute-date branch is removed.

### Fixed — the orbit card counted orbits it could not use

*"41 of 41 tracked satellites have an orbit"* beside a sky view placing
none of them: `bridge_placeable` tested whether an ephemeris existed,
while placement additionally tests whether it is valid now. The same
number decides when pro opens its ephemeris stream and when it hangs up,
so a stale file made the cache look finished. It now counts what can
actually be used, and the age beside it is measured over usable orbits
only — a cache holding nothing current says so instead of naming an age.

Verified on the handset: with a ten-hour-old file the card reads *"0 of
38 tracked satellites have an orbit"* and *"no broadcast orbits — the
sky view shows this phone's satellites only"*, while pro on the same
station and the same stale file reads *37 of 37* and *0 min*, because
its ephemeris stream delivered current ones.


### Added — a badge saying where the sky view's positions came from

Free was drawing the sky from the phone's own GNSS while an imported
navigation file sat unused, and nothing on screen explained why. The file
was a day old: orbits are only used within four hours, so every one of
its 2131 records was outside the window. The card above it said *"41 of
41 tracked satellites have an orbit"* and *"newest orbit 4,5 h old"*,
both of which read as *everything is fine*.

**Top-right of the Analysis screen, in both editions**: white when there
are no orbits, green for a real source — the station's own broadcast, an
ephemeris stream, or a file still inside the window — red for a file too
old to place anything, amber when the phone's receiver is doing the work.
Amber is not an error, but it is this handset's sky rather than the
station's. Tapping any of them opens the wiki page that explains what
orbits are for and where to fetch a current file.

The badge and the sky view's own header now come from one `skySource()`.
Asked separately they would eventually disagree, and the screen would
contradict itself about its own data.

### Fixed — a navigation file could not say how old it really was

*"Newest orbit 4,5 h old"*, for a file whose newest record was thirty
hours old. Age came from the ephemeris cache, and the cache cannot
answer: a GLONASS record carries Moscow seconds-of-day and nothing else,
so one from yesterday afternoon wraps and lands a few hours behind now.
Freshest-wins, and that single wrapped record set the headline.

The file's true age now comes from the records' own calendar dates, read
while it is being parsed (`rinex_nav_newest_utc`) and stored at import.
An import that lands outside the four-hour window says so at the moment
it can still be fixed, and the orbit card carries the file's real age
beside its name.

### Changed — free no longer offers a setting it cannot honour

The ephemeris caster, port and mountpoint fields were shown in both
editions, unguarded, while `MonitorService` dials the stream only when
`Features.HAS_EPH_STREAM` — false in free. A free user could type a
caster, save it, and watch it be ignored; the sky falling back to the
phone then looks like a fault rather than the edition working as
designed. Free now says *"Ephemeris stream — available in the Pro
edition"* where the fields were, and names what it uses instead.

Values already stored are carried through untouched, because the
configuration file is shared with pro and with the desktop tools and
saving settings in free must not strip somebody's ephemeris mountpoint
out of it.


### Changed — the GUI's C/N0-vs-elevation plot counts cells, like the app

The Windows plot had the same two faults the Android view was fixed for,
and now has the same cure.

**It said "whole session" and meant the last quarter-hour.** The cloud
was drawn from a 32768-sample ring, which at ~38 SV/s holds about
fourteen minutes; the mean line underneath was computed from unbounded
sums, so the two halves of one picture spoke for different periods. It
now counts hits per cell instead, keeping every sample of a run of any
length in 202 kB — *half* what the ring cost, because a cell hit a
million times is still a cell.

**And it could not show density.** Ten thousand samples in one square
drew exactly like one, which flatters a station that sits at a single
elevation. Cells are now shaded logarithmically by their count. GDI
fills are opaque, so the weight is expressed by blending towards the
panel colour — the same picture the Android view draws with an alpha
channel.

Cells are one degree by **one decibel**, and filled from boundary to
boundary so neighbours meet. Both are load-bearing: MSM4 and MSM5 carry
C/N0 in six bits, whole decibels, so a finer cell leaves a blank row
between every filled one — the white lines that were diagnosed on the
phone as an edition difference and turned out to be the station.

The per-constellation mean lines are unchanged and still in 5° bins: a
mean over a one-degree slice is as noisy as the samples in it.

Verified against caster.centipede.fr/NEAR, which sends legacy 1004/1012
— quarter-decibel C/N0, a third quantisation again — with the cells and
the mean lines drawn together.

Two stale claims went with it: the window said *"waiting for MSM7
observations"* when C/N0 also comes from MSM4, 5 and 6 and from the
legacy messages, and `docs/gui.md` told readers this view needs MSM7.
The station used to verify it sends none.

### Fixed — an overnight watch would have ended as a crash on Android 15

The app targets API 35, where a `dataSync` foreground service may run
about six hours in a day. When that runs out the system calls
`Service.onTimeout()` and gives the service seconds to stop; one that
does not is killed with `ForegroundServiceDidNotStopException`. Watch
mode is exactly such a service and is sold on running for hours, so the
paid edition's headline feature would have produced a crash report
instead of a result on any current handset. Nothing shows this on the
Android 10 test phone, which is why it survived to now.

`MonitorService.onTimeout` winds the run up through the same path the
Stop button uses, and says which ending it was: `LIMIT_REACHED` —
*"stopped by Android's six-hour limit on background streaming"* — with a
notification carrying the same sentence, because a phone that has been
watching all night is in a pocket. Everything measured up to that point
is kept, so an interrupted overnight watch still yields the hours it
managed.

That outcome already existed for an abandoned free-edition watch cap and
was set nowhere; a system timeout is precisely what "cut short by a
limit that is not the station's fault and not the user's choice" means,
so it now has the meaning its name promised.

Both endings now run through one `endRun(outcome)`, so a third reason to
stop cannot acquire a different shutdown. Verified on the handset by
stopping a live run: *"Stopped after 62 s — measurement incomplete"*.
The timeout entry point itself cannot be provoked on Android 10 and is
marked as untested where it is defined.

Documented in the wiki's **Watch mode**, where somebody planning a
twelve-hour watch will meet it before the phone does.

### Fixed — three claims the app made about itself that were not true

Found by auditing everything that has to agree at submission time, and
none of them would have failed a build or a test:

- **The About dialog said the app checks "the seven RTK service KPIs".**
  It reports eight, and has since KPI 8 arrived — the CLI text and the
  documentation were corrected then, the Android string was not. It was
  also still wrong in `Features.kt` and in `design/architecture.md`.
- **About → Documentation opened `docs/readme.md`**, which is written
  for somebody building this repository. A phone user tapping
  *Documentation* wants *Getting started*, so it opens the wiki.
- **Nothing in the app linked the privacy policy.** Play carries the URL
  on the listing, but somebody who has already installed should not have
  to go back to the store to read what the app does with their position
  — least of all in the edition that asks for it. About → **Privacy
  policy**.

Also removed `NTRIP_ANDROID_VERSION_CODE` from `version.h`. Android's
version code is a *derivation* of the three version numbers, Gradle
computes it, and nothing read the constant; keeping it meant two
definitions of one rule, where a bump that moved the numbers but not the
constant would label a release with a code that disagrees with its
version.

### Added — `tools/check_release.py`

Because the three defects above are a class, not three accidents: each
is a claim made in one file about something that lives in another, and
nothing compares them. The script does, reading both sides — 25 checks
covering the version, the addresses the app can open, the check count on
every surface that states it, Play's title and length limits, and
whether the generated notices still match the dependencies they name.

Every check was verified to fail when its fact is broken: seven
deliberate mutations, seven caught. A check that has never failed has
not been tested.

### Added — whose caster it is, said where the user is choosing one

The app is a client: it connects where it is told, with the user's own
credentials, which keeps the relationship and the terms between the user
and the caster. That was already stated in the developer documentation
and nowhere a user would meet it.

It is now in the wiki's **Getting started**, next to the fields where a
caster is typed in — have permission to be there, public networks are
meant to be used and commercial ones are for subscribers, some casters
limit concurrent connections, and the data belongs to whoever operates
the station. It is repeated in one line on the privacy page, in the
store listing's privacy paragraph, and beside **Watch mode**'s data
figures, where a multi-hour watch is a multi-hour client on somebody
else's caster and not only megabytes on your own plan.

Ordinary for any NTRIP client, but a tool that makes connecting easy
should say it once, plainly.

### Decided — what a store screenshot may show

Two rules, written into `tools/make_store_shots.py` rather than into
someone's memory: capture against the author's own station where a
station is prominent, and redact the caster address regardless, to a
domain reserved for documentation (RFC 2606). The mountpoint name and
the measurements stay — they are what the screenshot is *for*, and a
public anonymous stream's name discloses nothing private. A listing seen
by thousands should not advertise a host that belongs to a person.

With this and the notices screen, **all seven licence actions that
blocked the free launch are closed**; the one still open concerns a PDF
that only matters if automatic RINEX download is ever reconsidered.

### Added — open-source notices, in the app and in the release archive

cJSON is MIT and is compiled into every artefact this project produces;
the AndroidX, Compose and kotlinx libraries are Apache 2.0. Both ask for
a notice to travel with the software, and none was travelling. This was
the last licence action blocking the free launch.

**In the app**: About → **Open-source notices**, in both editions,
scrollable and monospace, from a generated resource.

**In the desktop archive**: `THIRD-PARTY-NOTICES.txt` beside the
binaries, produced by the `release` target — verified by packaging 3.3.0
and finding it next to the two executables, the example configuration
and the checksums.

**The versions are read from the build's own
`android/gradle/libs.versions.toml`** rather than typed, because a legal
notice that quotes last year's version is worse than none.
`docs/licences.md` had drifted precisely that way — it named
`security-crypto 1.1.0-alpha06` weeks after the security assessment
moved the build to 1.0.0. Corrected, and no longer the source of the
notice.

The Google OSS-licenses Gradle plugin would have generated the screen
and added a Google dependency to an app that deliberately has none, so
`tools/make_notices.py` writes the text instead — wrapped for a phone in
the app's copy and for a terminal in the archive's, which changes
whitespace only.

### Added — store screenshots, and the tool that frames them

`tools/make_store_shots.py` turns ordinary device captures into Play
screenshots: a handset is 9:19.5 and Play wants 9:16, so each capture is
placed **whole** on a 1080x1920 canvas in the app's own navy with a
caption. Nothing is cropped to fit — the screen is shown as it is, and
the caption says what the reader is looking at rather than leaving them
to guess.

Four for pro, shot against the author's own caster, which **resolves
open question 6**: no third party's infrastructure is named in marketing
material. The caster's address is replaced with `ntrip.example.com:2101`
by a redaction table in the tool — a store listing is marketing, and a
real host does not belong in it. The mountpoint stays, because a
screenshot of a station with no name is a screenshot of nothing.

The one to lead with is the C/N0-versus-elevation view after a
24-minute watch: **50 325 samples**, the antenna curve climbing from the
horizon to zenith, and a visible dent in GLONASS around 30-45 degrees.
That is the whole argument for the view in one image, and it could not
have been staged.

### Added — the Play data-safety declaration, per edition

`design/work-items/play-data-safety.md` answers every question in the
console's form for both listings, records what each answer rests on, and
lists the ways an answer could quietly stop being true — a new
dependency, TLS landing, anything new leaving the device.

**Free declares nothing.** The only data that leaves the device goes to
the caster the user typed in, at the moment they tap Run, which is
Play's user-initiated-transfer exemption.

**Pro declares precise location as shared, optional, app functionality**
— on the same reasoning that would have allowed it to declare nothing.
The live position is arguably exempt too; it is declared anyway, because
a tool that measures other people's infrastructure should be the last
thing on a phone to take an exemption for transmitting a location.
*Collected: no, shared: yes* is the honest shape of it — there is no
server for it to be collected into.

Both answer **No** to encryption in transit, because NTRIP sends the
position and the credentials over a plain connection. That is the
protocol rather than a shortcut, the app says so where the password is
typed, and it becomes **Yes** the day TLS lands in both editions.

### Added — user documentation for the paid edition

Five pages covering only what differs, each pointing back to the free
pages for everything shared: **what Pro adds**, **watch mode**, **saved
connections** and the configuration file, **reporting where you are**,
and **orbits and the ephemeris stream**.

Two of them exist because the behaviour is easy to misread. The
ephemeris page states the dialling policy in numbers — not opened while
the station's own stream delivers, opened after twenty seconds of
nothing filling the cache, held at least twenty seconds and at most two
minutes, no redial for fifteen minutes — so a user who watches a second
connection appear and vanish knows it was designed rather than broken.
The live-position page states what is transmitted, that it goes to that
caster and nowhere else, that the configured position is the fallback
rather than a zero, and that the phone stops learning its position while
the app is off screen.

Watch mode gets the reading it needs: availability is *of judged time*,
a degradation is an event rather than a duration, and the three numbers
worth quoting in a report are availability, degradations and worst
state.

**No free page links to a pro page** — checked mechanically, with every
internal link — so the free wiki still stands alone for a user who will
never see these.

### Added — user documentation for the free edition

Seven wiki pages in `docs/wiki/`, written for someone who has just
installed the app and knows nothing about it: a first run in five
minutes, what each of the eight checks means **and what to do when it is
not green**, how to read the three analysis views, the failures a user
is most likely to meet, what the paid edition adds, and how privacy and
support actually work here.

Kept in this repository rather than typed into the wiki, so they are
reviewed and versioned beside the code they describe; publishing is a
copy into `NTRIP-Analyser.wiki.git`.

Every threshold and every quoted message comes from `src/core/kpi.c` and
the app's own strings — 40 dB-Hz, one CRC error in a thousand, 0.5 Hz,
the thirty-second ARP allowance, the sixty-second sustain — because a
wiki that describes the tool inaccurately is worse than no wiki. The
pages also carry the two things a user would otherwise report as bugs:
that a station's message format decides the resolution of the C/N0
views, and that a satellite with no known orbit is counted but
deliberately not drawn.

The support posture is stated rather than implied: the wiki answers a
question once, issues go to the public tracker, and the contact address
Play requires is not a help desk.

### Fixed — the C/N0-versus-elevation plot

Three faults in one view, found by looking at it on a handset beside a
second station.

**It belonged to no run in particular.** Samples from every station
tested since the app was opened were drawn into one scatter, under a
header reading "this session": two casters, two antennas, two skies, one
curve, and nothing to say which was which. It is cleared when a run
starts — a first run ended at 2863 samples, and twenty-five seconds into
the next the plot read 1040 rather than 3900.

**It drew dots inside larger cells**, so the plot showed the grid's own
quantisation as gapped columns. Each cell is now drawn as the rectangle
it stands for, and neighbours meet.

**Its cells were finer than the data.** MSM4 and MSM5 carry C/N0 in six
bits — whole decibels, nothing between them — so with half-decibel cells
every second row could never be filled, and the plot drew a blank row
between every filled one: **horizontal white lines**, on the station
rather than in the renderer. On MSM7, at a sixteenth of a decibel, there
was nothing to see, which made identical code look like an edition
difference. Cells are a whole decibel now, the coarsest any stream
delivers, so every message family fills the rows it touches.

Compared on the handset afterwards: free on `caster.centipede.fr/NEAR4`
(MSM4) and pro on `ntrip.kadaster.nl/APEL00NLD0` (MSM7) render the same
plot, the MSM7 one denser because a finer stream spreads its samples
over more rows. Neither is more correct, and
`android/design/views.md` now carries the table of what each message
family delivers and how that reaches the screen.

### Fixed — the constellation legend squeezed its last entry

With more constellations than fit a line — six on
`rtk2go.com/Mirmenhof` — the last was crushed into whatever space
remained. It wraps now, breaking between entries and never inside one,
because a colour swatch without its name beside it says nothing. All
three analysis views share the legend, so all three gained it: verified
reading GPS, GLONASS, Galileo, BeiDou, SBAS on one line with NavIC on
the next.

### Added — the build refuses to let the editions diverge

`android/design/editions.md` has always said the editions differ by
flags and never by implementation, and until now only a person checked.
**`checkEditionParity` now fails the build if anything but `Features.kt`
appears in an edition's source set**, naming the file. Resources are
exempt: the icon and the app name are what an edition is allowed to
differ in.

It runs as part of every build. Verified by putting a stray file in
`src/free` and watching the build stop:

    An edition may only carry Features.kt; everything else is shared
    (android/design/editions.md). Found:
      free: nl\pe1mew\ntripanalyser\Stray.kt

### Fixed — an imported navigation file suppressed the ephemeris stream

The sky view reported "navigation file" on a station with an ephemeris
stream configured, and the stream was never dialled. A loaded RINEX
fills the orbit cache on the first pump, so the policy that opens the
stream only when nothing is filling the cache saw a full cache and
stayed shut: the plot was drawn from a file read off a disk while a live
source sat unused.

**The file is a fallback, not a substitute.** Where the station
broadcasts its own orbits nothing is dialled, as before -- that remains
the best case. Where it broadcasts none and a stream is configured, the
stream is now used even with a file loaded, and given at least twenty
seconds to deliver before it is returned, since the cache being complete
would otherwise close it on the pump after it opened.

On `rfsee.net/HANESE` with a 2131-record navigation file loaded: **456
frames off the ephemeris stream, closed after 20 s at 40 of 40
placeable**, and the sky view now reads "39 of 40 satellites shown ·
ephemeris stream" where it read "navigation file" before.

### Fixed — an MSM1-3 station reported no satellites at all

`msm_extract_prns()` refused any MSM whose number did not end in 4 to 7,
so a station streaming MSM1, 2 or 3 was graded with **zero satellites in
view** — and failed on arithmetic while its frames passed CRC and its
ARP arrived. Measured on `rtk2go.com/CASISA`, which streams 1073, 1083,
1093 and 1123: **FAILED in fifteen seconds, 0 SV**.

The refusal had no basis. **The satellite mask is a header field** —
bit 73, 64 bits — identical in all seven MSMs, and reading it needs none
of the per-cell layout that separates them. What MSM1-3 genuinely lack
is C/N0, which `msm_extract_cnr()` still refuses, and KPI 6 now says so
in the same words it uses for a legacy station.

The same station now reads **30 satellites, four constellations at rate,
CAUTION** — the caution being KPI 6, honestly reporting that this stream
carries no signal strength to judge. On the handset: 31 satellites where
it counted none.

`test/test_msm_cnr.c` gained the case: an MSM3 frame yields its
satellites and no C/N0, which is the distinction that was missing.

The dead `msm_flowing()` went with it — nothing has called it since
KPI 4 was rewritten to judge every constellation a station streams.

### Added — every legacy observation message can be read on screen

1001, 1002, 1003, 1009, 1010 and 1011 join 1004 and 1012, so a station
sending any pre-MSM observation message can be inspected field by field
rather than appearing as a type number with nothing behind it.

**Written once, not eight times.** The eight messages differ in four
things — whether there is a second band, whether C/N0 is carried at all,
GLONASS's frequency channel number, and two field widths — so they are
one printer parameterised from the same table `rtcm_legacy_extract()`
reads. That is not tidiness: of the two that had been written by hand,
one was misaligned for years and the other did not exist. A ninth copy
would have been a ninth chance to be wrong.

The refactor is provably behaviour-preserving where behaviour could be
checked: printing 1004 and 1012 from a live capture of
`rtk2go.com/Mirmenhof` gives **byte-identical output** before and after.
The six new ones have no live station to point at — they are the rarer
shapes, L1-only or without C/N0 — so they are verified by construction:
frames built with known values print those values back, including
"C/N0 not carried by this message" for the four types that carry none,
and GLONASS's channel number decoded from DF040's 0..20 to -7..+13.

### Added — the 1004 decoder, which never existed

1012 was dispatched and 1004 was not, so a station sending both showed
its GLONASS observations in the message detail and **nothing at all for
GPS**. `decode_rtcm_1004()` is the GPS counterpart of the 1012 decoder
fixed alongside it, reading the layout confirmed against
`rtk2go.com/Mirmenhof`: 64 header bits, then 125 per satellite.

Checked against `rtcm_legacy_extract()` on the same frames, satellite by
satellite — PRN 11 at 43.00 L1 and 26.75 L2 where the reader reports
43.00 as the better of the two, PRN 21 at 49.00 and 45.00, PRN 8 at
36.00 and 26.00; ten satellites in both, no extras either way. Values
are scaled and carry units, and the *not computed* markers are named
rather than printed, exactly as in 1012.

Still without a decoder: 1001, 1002, 1003, 1009, 1010 and 1011. They are
rarer -- a station sending L1 only, or without C/N0 -- and every one of
them is already **measured** correctly, since `rtcm_legacy_extract()`
covers the whole family. What they lack is the field-by-field display.

### Fixed — the 1012 decoder printed fields that were not there

`decode_rtcm_1012()` read the satellite count as 6 bits where DF035 is
5, and never read DF040, the 5-bit frequency channel number. Between
them, every field after the code indicator was displaced: the
pseudoranges, phase ranges and C/N0 values shown for a real station were
plausible numbers belonging to nothing. It had been wrong since it was
written, and stayed wrong because nobody had measured the layout — the
same reason the C/N0 gap survived.

Now it reads the layout confirmed against `rtk2go.com/Mirmenhof`, and
its output agrees with `rtcm_legacy_extract()` satellite by satellite:
slot 8 at 47.00 L1 and 48.50 L2 where the reader reports 48.50 as the
best of the two, slot 10 with no L2 at all where the reader reports its
L1 45.00.

Two further corrections while there. Values are **scaled and given
units** — a C/N0 printed as "178" is not a reading anybody can use —
and the standard's *not computed* markers are now named rather than
printed: this station has two satellites in every frame with no L2, and
they used to appear as a pseudorange of −163.84 m and a phase range of
−262.1440 m, which is a measurement invented out of a sentinel.

### Fixed — an old station is measured, not failed

A station streaming the legacy 1002/1004/1010/1012 was reported
**FAILED**. Not because anything was wrong with it: `sv_track_feed()`
accepted only MSM message types, so **not one of its satellites was ever
counted**, and three of the eight verdicts described this tool's
coverage rather than the station. Kadaster's `APEL0` — a working RTCM
3.1 reference station at 1 Hz — failed in fifteen seconds.

It now reads **STATION OK**, eight of eight, 23 satellites, median C/N0
46.40 dB-Hz.

**The judgement changed with the decoding, and had to.** Legacy messages
exist only for GPS and GLONASS; there is no legacy Galileo. KPI 4 asked
for *"GPS and Galileo MSM at 0.5 Hz"*, so decoding the messages without
touching it would only have moved the false verdict from KPI 5 to
KPI 4 — failing a station for something its format forbids.

- **KPI 4 is now "Observations flowing"**: every constellation the
  station streams must stream at rate, counted across legacy and MSM
  alike. Whether an *advertised* constellation is missing stays KPI 8's
  question, so one fault still produces one failing verdict.
- **KPI 5 judges against the advertisement**, not a flat 25 satellites,
  which a GPS+GLONASS station cannot reach whatever its health. The
  expectation is now per constellation (`KPI_EXPECT_SATS`), summed over
  what the sourcetable advertises, falling back to what is actually
  streaming when no sourcetable could be read — so a caster we cannot
  interrogate never costs a station its verdict.

**The layout was measured, not assumed.** `rtk2go.com/Mirmenhof` sends
1004 and 1012 beside a full MSM6 set, which makes it its own reference:
the satellite lists match exactly — 10 of 10 GPS, 8 of 8 GLONASS, no
extras either way — and every legacy L1 C/N0 lands within the 0.25 dB-Hz
quantisation of the same satellite's L1 in MSM6. The frame lengths agree
too (64 + n×125 bits for 1004, 61 + n×130 for 1012, on every frame of a
64-second capture), which is what settled two details this repository's
own 1012 *printer* has had wrong since it was written: a 6-bit satellite
count, and no frequency channel number.

Counting is by (constellation, PRN), so a station sending both
generations is counted once: `NEAR` reads 43 satellites, not 86, and
Centipede's legacy and MSM7 satellite lists match to the satellite.

`test/test_legacy_obs.c` pins the layout by construction — synthetic
1002/1003/1004/1010/1012 frames whose values no other reading could
produce, verified to fail when a record size is deliberately broken.

### Fixed — the ionospheric monitor sees MSM6 stations

`iono_feed()` accepted only MSM7, and `iono.h` explained why: *"MSM4/5/6
carry no extended phase resolution"*. That is true of MSM4 and MSM5,
whose phase range is 22 bits at a coarser scale, and **false of MSM6**,
which carries the same 24-bit DF406 fine phase range as MSM7 and differs
only in a Doppler field this monitor does not read.

The one real difference is the satellite block ahead of the signal
arrays — 8 + 4 + 10 + 14 bits per satellite in MSM7 against 8 + 10 in
MSM6 — so reading an MSM6 frame at MSM7's offset lands eighteen bits
into the pseudorange array and yields nonsense. That offset now follows
the message.

Measured on captures of `rtk2go.com/Mirmenhof`, which sends the full
MSM6 set at 1 Hz on two frequencies:

| 64-second capture | before | after |
|---|---|---|
| MSM frames offered | 448 | 448 |
| satellite updates accepted | **0** | **1890** |
| dual-frequency satellites | **0** | **30** |

Over five minutes the whole path runs, not just the frame gate: 1923
frames offered, **8754 satellite updates**, 33 dual-frequency satellites,
and a verdict where there was none — **QUIET, ROTI median 0.037
TECU/min**. Before the change that station produced no ionospheric
measurement at all.

Found while widening C/N0 to MSM4/5/6, and the same kind of mistake: a
limit of the reader written down as a property of the format.

### Fixed — C/N0 is read from every MSM that carries it, not only MSM7

A station sending MSM4, MSM5 or MSM6 was told its C/N0 could not be
measured. That was never true of the stream: **MSM4 and MSM5 carry C/N0
in six bits (whole dB-Hz) and MSM6 in the same ten-bit field as MSM7**
(1/16 dB-Hz). Only the reader was MSM7-only, and it said so as though it
were a property of the station — KPI 6 warned, the signal-quality view
stood empty, and the C/N0-versus-elevation plot could never accumulate a
sample.

All four are read now. The two readers are one algorithm parameterised
by a small table of field widths, because the families differ only in
those numbers: the satellite block is 18 bits per satellite for MSM4 and
MSM6 and 36 for MSM5 and MSM7, which shifts every signal array after it.

Measured against live stations, one per family:

| Mountpoint | Messages | Median C/N0 |
|---|---|---|
| `caster.centipede.fr/NEAR4` | MSM4 | **44.45 dB-Hz**, 40 SV |
| `caster.centipede.fr/NEAR` | MSM7 | **44.60 dB-Hz**, 40 SV |
| `rtk2go.com/Mirmenhof` | MSM6 | **45.01 dB-Hz**, 52 SV |

The first two are the same nearest-base service asked for the same
position two minutes apart, so they are the same station in two message
sets: **0.15 dB apart**, inside MSM4's own 1 dB quantisation. A wrong
bit offset does not produce that; it produces a plausible number from
the middle of a phase range, which is exactly why the layout is now
pinned by `test/test_msm_cnr.c` as well — synthetic frames per family,
with values no other reading could yield, and verified to fail when the
layout is deliberately broken.

KPI 6's wording follows the truth: it now says C/N0 is read from MSM4,
5, 6 and 7, so a stream that gets no measurement is one carrying MSM1-3
or only the legacy 1002/1004/1010/1012 — whose 8-bit C/N0 is still not
read, and which is scheduled work rather than a property of those
stations.

### Fixed — orbits are decoded from the observation stream, wherever a station sends them

Many stations broadcast ephemerides beside their observations, and the
app ignored orbits it was already receiving. Measured on
`caster.centipede.fr/NEAR`: **1020 ×16, 1042 ×8 and 1046 ×23 in fifteen
seconds**; Kadaster's APEL00NLD0 advertises
`1019,1020,1042,1044,1045,1046` in its sourcetable. The Android bridge
decoded those seven types only on the dedicated ephemeris side-stream,
so the free edition — which has no side-stream — showed an empty sky
view on a station that was supplying everything needed to fill it, and
the paid edition opened a connection it did not need.

**The observation handler now decodes them into the same cache**
(`bridge_on_event`, `sky_on_event`), with the decoders' output sunk as
the side-stream handler already did. The seven-type switch, which had
been copied into four handlers and was about to be copied into two
more, is now one function in the core: `rtcm_decode_eph()`.

Measured, Android, pro edition, five minutes on NEAR: **603 ephemerides
off the observation stream, 41 orbits cached, 40 of 40 tracked
satellites placed, 0 frames off the ephemeris stream** — it was never
opened. The free edition draws the same plot. On the desktop, `--sky`
against NEAR with no ephemeris source configured at all: **67
ephemerides, 1136 sector updates, PNG written**; before this the run was
refused before it connected.

**The ephemeris stream is now dialled only when nothing is filling the
cache.** Asking "is the cache complete?" alone opened a connection on
the first pump, before a single frame had arrived, and again at 40 of 41
placeable for one satellite that had just risen — measured at 179 s on a
station broadcasting orbits throughout. The condition is now that
neither coverage nor the count of ephemerides off the observation stream
has moved for twenty seconds, against NEAR's twelve-second broadcast
cycle.

**`--sky` no longer refuses to start without a configured source.** It
cannot be known before connecting whether a station carries its own
orbits, so the run goes ahead and the cache is checked afterwards: a
station that turns out to send none still exits `EXIT_NO_EPH`, having
had to look first. The `[OBS]` summary and the `--json` ticks carry an
`eph` count, and a run that got its orbits free says so.

**`bridge_eph_count()` stopped reporting zero for a full cache.** It was
gated on a side-stream being open, so orbits from an imported RINEX
file, from the observation stream, or from a side-stream already closed
because the cache was full all read as "0 ephemerides" beneath a sky
view plainly drawing a constellation.

The Windows GUI already decoded obs-side ephemerides, through
`analyze_rtcm_message()` on its detail-window path; it is unchanged
apart from using the shared decoder.
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
(`design/work-items/play-listing.md`). One answer is worth stating here:
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
