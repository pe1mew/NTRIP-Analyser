# Android app GUI review — plan

The author's review of the Android app's screens, collected item by item
as the author gives them, and implemented only once the author says the
list is complete. Verified on the Huawei P smart 2019 (Android 10), in
both editions.

Started 2026-10-17. **State: built** (collection closed 2026-10-20;
steps 1–6 built and checked on the Huawei 2026-10-20 and -21, each
committed). **Open:** the reference position on a real drive — hand-overs,
a second lettered station, the rover's coloured track — which a desk
cannot produce; the bar's restart reason, built but not yet seen; and
the listing's pending wording, for the author.

**Scope: the Android app only** (author, 2026-10-20). With R10
withdrawn and R11's check giving no verdict, nothing here changes
`src/core`, the CLI, the Windows GUI or the daemon. The core's rules
stay the same for every program; only what the app shows, and where,
changes. **One exception, decided 2026-10-21 (author, step 3, option
A):** one additive call in `src/session`, `ns_set_auto_reconnect()`,
which only the bridge calls; the other programs behave as before, and
were rebuilt and run because the session layer is shared.

## How this plan is used

- Each finding goes in as the author states it, with a stable number
  (R1, R2, …), the screen and edition it concerns, and the author's own
  words for what should change. Where the author gives a reason it is
  kept; where none is given, none is invented.
- Questions an item raises are written under it as **Open**, for the
  author to settle before implementation, not answered by assumption.
- Rules from elsewhere still hold: no threshold, verdict or measurement
  rule in the UI layer, and editions differ by showing more or less,
  never by measuring differently (`CLAUDE.md`); what each view is for is
  in `android/design/views.md`.
- When the author says the list is ready, each item gets its files and a
  check on the device, and the order of work is settled then.

## Findings

### R1 — Explanations move behind an info button, on every analysis screen

**Screens:** every Analysis tab — Sky view, C/N0, C/N0 vs elevation
(both editions), Self-position and Reference position (pro). *(The
tabs as `AnalysisTab` in `MainActivity.kt` lists them.)*

**The author:** the information and description text on each analysis
screen moves off the page. At the top of each page, on the far right,
an info/help button. Pressing it shows the help and explanation as a
pop-up, like the mountpoint list's table; the pop-up is closed when the
reader is done.

**Example:** the Self-position tab, pro, 62 minutes into a run
(screenshot, 2026-10-17): the explanation paragraph ("…by metres as
satellites rise and set, so the scatter is an upper bound on the
station's, not a measure of it. A still base reads tens of mm/s.")
sits between *Analysing for 62 min* and *SOLVED — no verdict*, with
the button marked at the top right, level with the status line.

**Seen in the same screenshot:** the paragraph's first line is drawn
under *Analysing for 62 min*. The two overlap, and the paragraph's
opening is cut off. Moving the text into the pop-up removes the
overlap on this tab; whether other tabs show it is checked at
implementation.

**Settled (author, 2026-10-20):**
1. **On the page:** the status line, the summary line (S2), the verdict
   line, the figures, the plot, the latest-epoch line and **the legend,
   which stays with the plot**. **Behind the button:** the prose that
   explains.
2. **The button sits in the content, top right**, in the row of the
   status and summary lines, as marked on the screenshot — not in the
   app bar, which keeps its back arrow, name, Share and menu.
3. **A dialog, like the mountpoint list**: a title, the text scrolling
   inside it, a *Close* button; a tap outside or Back closes it too, and
   the screen behind keeps its place.

4. **Every analysis screen gets the button and help text**, the
   missing ones written for this change (R1 texts, below, for the
   author's review).

**Added from S2 (author, 2026-10-17):** the row the info button sits in
carries the screen's existing one-line summary, at the same height —
on the Reference position tab today "Where the service's reference
position stands, and where it has been this run." Only the longer
explanation goes behind the button. Which line is each tab's summary
is read per tab at implementation.

5. **The hub gets the info button and a summary line too** (author,
   2026-10-20). The summary is the store's short description; the help
   is built from the store listing's text (R1 texts, *Hub*).

6. **The status line keeps its place**, the full-width top row under
   the tabs (the author's marking on the Sky view screenshot,
   2026-10-20; the arrow only pointed at the box). Below it, the
   summary and the ⓘ share a row (S2). So, top to bottom: status line,
   summary row with ⓘ, the screen's own header, the plot.

**Seen in the same screenshot, not raised by the author:** an empty
band of about 200 px (screenshot pixels) between the header ("40 of 40
satellites shown…") and the plot, where the plot is centred in the
height left over. Removing the explanation text (R1) adds to that
space; how the plot uses it is checked with R5 at implementation.

### R1 texts — summaries and help, draft for the author's review

**Decided (author, 2026-10-20):** every analysis screen gets the button
and its help text (R1.4 a); the missing summaries and help are written
here and reviewed line by line before anything is built. **Reviewed
and accepted by the author, 2026-10-20**, as below. Sources: the
strings the screens show today (`res/values/strings.xml`),
`android/design/views.md` and the core's own definitions. *Existing*
marks text the app already shows, moved and not rewritten; *new* marks
text written for this change. Screen names, verdict words and units
stay in English, as in the app.

**Hub** (both editions) — *added 2026-10-20 (R1.5); reviewed and
accepted by the author the same day*
- Summary (*existing*, the store's short description,
  `play-listing.md`): "Is this GNSS base station fit for RTK? Measure
  it, and see why."
- Help (*existing*, the store listing's text, shortened; the run
  buttons' lines are the app's own button subtitles):
  - NTRIP Analyser connects to an NTRIP caster and answers one question
    about a GNSS base station: is it fit to serve RTK, and if not, why?
  - It does not steer a rover and it does not compute a position. It
    measures what the station actually delivers -- message types and
    their rates, satellites and their signal strength, the reference
    position it broadcasts, and how steadily it holds all of that --
    and states a verdict you can act on or hand to whoever runs the
    station.
  - A check ends with STATION OK, CAUTION or FAILED; a watch adds
    STABLE, DEGRADED or UNSTABLE -- and each comes with the evidence,
    so the verdict can be argued with. *(Reworded from the listing,
    author, 2026-10-20.)*
  - *Run the check*: the eight checks (tier 1) — a verdict in about two
    minutes.
  - *Watch station* (pro): checks plus stability (tier 2) — needs ten
    minutes of evidence, runs for hours.
  - *Network-RTK check* (pro): checks plus the network at your
    position — answer in ~2 min, then runs until stopped.
  - The app is a client: it connects where you tell it to, with your
    own credentials. Having permission to use a caster, and observing
    its terms, is yours.

**Sky view**
- Summary (*new*): Where the satellites the station tracks stand in its
  sky.
- Help (*new*):
  - Each dot is a satellite the station is tracking, drawn where it
    stands as seen from the station: the centre is straight up, the
    edge is the horizon, with rings every 15° of elevation. Its colour
    is its constellation; its label is its PRN.
  - Positions come from orbits: the station's own stream where it
    broadcasts them, a RINEX navigation file you import, or, in the
    paid edition, an ephemeris mountpoint opened while it is needed.
    Satellites no orbit covers are placed from this phone's own GNSS
    where it sees them. The header names the source in use.
  - A satellite with no position is counted in the header and not
    drawn. An empty part of the sky is not a missing satellite.
  - *Pro only:* each line is that satellite's track over the run.

**Signal quality**
- Summary (*existing*): "Live value, this epoch" (pro) / "Mean over the
  capture, averaged in power" (free).
- Help (*new*):
  - One bar per satellite: its C/N0, the strength of its signal at the
    station's receiver, in dB-Hz. The header gives the count, the mean
    and the range.
  - Means are averaged in power, not in decibels: averaging the dB
    numbers would understate a satellite that fades and recovers.
  - The station's message type sets the resolution: MSM4 and MSM5
    carry whole dB-Hz, MSM6 and MSM7 a sixteenth, MSM1 to MSM3 none at
    all, which leaves this screen empty. When two stations differ,
    compare their message types first.

**C/N0 vs elevation**
- Summary (*existing*): "A healthy antenna climbs smoothly from the
  horizon to zenith; a flat or dented curve points at the antenna or
  its siting."
- Help (*new*):
  - Every sample of the run: a satellite's C/N0 against its elevation
    at that moment, gathered into cells of one degree by one dB-Hz and
    coloured by constellation. The more samples in a cell, the more
    solid it is drawn.
  - A healthy installation rises smoothly from about 35 dB-Hz at the
    horizon to about 50 dB-Hz at zenith. A flat or dented curve points
    at the antenna, its siting or an obstruction rather than at the
    receiver.
  - Cells are a whole dB-Hz high because that is the coarsest any
    station sends; a finer grid would leave empty rows on an MSM4
    station. Read the shape of the curve, not how full the cells are.
  - Elevation comes from the same orbits as the sky view; satellites
    it cannot place add nothing to the plot.

  *Checked against the code 2026-10-20 (`ElevationView`,
  `ElevationAccumulator` in `Views.kt`): cells with logarithmic opacity, no
  trend line. `android/design/views.md` §3 still says "every sample as a
  point, with a per-constellation trend"; corrected at implementation.*

**Self-position** (pro)
- Summary (*new*): Where the station's own GPS observations place it,
  against the position it broadcasts.
- Help: the paragraph shown today (*existing*, `sp_explainer`):
  "The station's own GPS observations against the position it
  broadcasts (ARP), from broadcast orbits. Offset and scatter both
  include this solution's own errors: satellite code biases it does not
  correct move it by metres as satellites rise and set, so the scatter
  is an upper bound on the station's, not a measure of it. A still base
  reads tens of mm/s." Followed by (*new*):
  - Each epoch is solved on its own, from GPS satellites seen on two
    frequencies, by the same solver the Windows GUI and the monitoring
    service use.
  - The plot is east and north around the broadcast position, which
    stays at the centre. The figures above it have no verdict: no
    threshold for a station's own scatter has been established.

**Reference position** (pro, the Network-RTK check's screen, R11)
- Summary (*existing*, `ho_explainer`): "Where the service's reference
  position stands, and where it has been this run."
- Help (*new*):
  - The reference position is the antenna position the stream says its
    corrections are for, sent in RTCM 1005/1006.
  - The rover is at the centre: this phone where it has a fix,
    otherwise the position you set. The reference position is drawn at
    its true bearing and distance, with where it has been this run and
    the distance over the last five minutes beneath.
  - A network service can hand you to another station as you move:
    that is its job. A fixed base should never move; if it does, its
    corrections cannot be trusted. Which of the two a mountpoint is, is
    decided from its sourcetable entry, the Network-RTK check's gate
    test, and where its reference position stands against yours; the
    screen names the evidence that decided (`station_class.h`).
  - The caster is sent this phone's position where you allowed it, or
    the position you set. With the screen off the phone's position is
    not updated, so on the move the caster serves where the phone was.

### R2 — A smaller app name in the bar, on every screen

**Screens:** every screen, both editions — **decided 2026-10-17
(author): one size everywhere**, so the bar stays the one frame
`Shell.kt` draws and still takes no title parameter.

**The author:** the app name in the bar ("NTRIP Analyser Pro") shall be
smaller (R1's screenshot).

**Seen in that screenshot:** at its present size the name wraps to two
lines, "NTRIP Analyser" / "Pro", and the bar takes about a tenth of the
screen's height above the tabs.

**Settled (author, 2026-10-17):**
1. Every screen, one size.
2. The aim is the name on one line.

**Check:** on the Huawei, both editions, "NTRIP Analyser Pro" being the
longer name. On a phone as narrow as this one, whether it fits also
depends on the system font size, so it is checked at the default font
size and at the largest. If it fits at the default but not at the
largest, that is reported back for the author's decision rather than
solved by cutting the name short.

### R3 — The Network-RTK check button appears and disappears inconsistently

**Settled 2026-10-20 through R11, decision 3:** the button stays at the
bottom, after *Watch station*, at all times — greyed out during a run,
never replaced by the result card, which is drawn separately. Its
subtitle is R11's open 5. The questions below are answered by that.

**Screen:** the hub, pro (free has no Network-RTK check).

**The author:** the "Network-RTK check" button appears and disappears
inconsistently (screenshot of the hub after a check, 2026-10-17).

**What decides it today** (`VrsPanel` in `HubPanels.kt`, read
2026-10-17, not yet tried on a device):
- no run going, and no network-RTK result in the last report:
  the button;
- a run going, of any kind: nothing at all, because the button would
  start a second run;
- the last report carries a network-RTK result: the result card in the
  button's place, and no button until a report without one replaces it.

So after an ordinary check the button is there; during any run it is
gone; after a network-RTK check it is replaced by its card. Each case
has a reason, but nothing on screen gives it. That fits "appears and
disappears", though the author's exact sequence is not yet known.

**Seen in the screenshot:** the button sits among the results, between
check 8 and the Stability card, while the other run buttons (*Watch
station*, *Run again*) are grouped further down. It is also the only
run button without a subtitle saying what it does.

**Asked at the time (answered by R11, decision 3, and its subtitle):**
1. The sequence that showed it. Which screens, in which order, made it
   appear or disappear unexpectedly? Or is it the rule above itself?
2. What it should do: always present when no run is going, beside the
   other run buttons, with the result card shown elsewhere? And during
   a run, shown but disabled, so the layout does not jump?

### R4 — Watch station below Run again

**Screen:** the hub, pro. Free's registry has no `WatchControlPanel`,
so free shows *Run again* alone and is unaffected.

**The author:** the *Watch station* button (tier 2) shall be below
*Run again* (tier 1).

**Today** (pro's `Registry.kt`): `WatchControlPanel` comes before
`RunControlsPanel`, so *Watch station* is drawn above *Run again*. The
registry's order is the layout, so the change is the order of those
two lines.

**Settled 2026-10-20 (R11, decision 4):** the Network-RTK check comes
last, after the tier 1 and tier 2 buttons: *Run again*, *Watch
station*, *Network-RTK check*.

**Also seen in the screenshot (not raised by the author):** the last
card, *Satellite orbits*, scrolls under the pinned *Analysis* bar, with
its first line cut through ("39 of 40 tracked satellites have an orbit
(95…"). Probably only the scroll position at the moment of the
screenshot; checked at implementation that the last card can scroll
fully clear of the bar.

### R5 — The Reference position plot as wide as the screen, like the other plots

**Screen:** Analysis → Reference position, pro.

**The author:** the polar plot shall take the screen's full width, the
same as the other plots (screenshot, 17 min into a run on `NEAR`,
2026-10-17).

**Why it is narrower today** (`HandoverDetail` in `Views.kt`): the plot
band holds the polar plot and the five-minute distance strip one above
the other, three parts to one of the height. A circle is as wide as it
is tall, so the polar plot gets the width its three quarters of the
height allow, not the screen's width. On the screenshot it spans about
half the width.

**Settled (author, 2026-10-20):**
1. **The distance strip stays below the circle**; the circle takes the
   full width, and the screen scrolls to reach the strip.

### R6 — One style of scale label on every polar plot, on a line from the centre up

**Screens:** every polar plot — Sky view (elevation rings), Self-position
(metre rings), Reference position (kilometre rings), both editions where
they exist — **and the mountpoint map**.

**The author:** all polar plots use the same type of legend or scale
label, preferably like the mountpoint map's. Every legend or scale
number stands on a vertical line from the centre to the top. This also
applies to the mountpoint map.

**Today** (screenshots R1 and R5, code read 2026-10-17):
- Self-position: ring values along the east axis, right of centre
  ("5.00 m", "10.0 m"), beside the "E".
- Reference position: one label for the whole plot, "outer ring
  39,7 km", above it at the left.
- Sky view: elevation numbers on every ring with a degree sign
  (`SkyCanvas`), placement to be read on the device.
- Mountpoint map: distance rings around the phone (`MountMap.kt`),
  labels to be read on the device.

**The mountpoint map's style, read 2026-10-20 (`MountMap.kt`):** every
ring labelled with its distance (1-2-5 steps), each label on a small
semi-opaque patch, placed where the ring crosses the north-east
diagonal. The sky view already places its elevation numbers on the
north line (R10's screenshot), without a patch.

**Settled (author, 2026-10-20):**
1. **Every ring labelled, where it crosses the north line, just right
   of it**, so the line and the "N" stay clear; **each label on the
   map's semi-opaque patch**. Self-position, Reference position and the
   mountpoint map move their labels there; Self-position and Reference
   position label every ring (today: the east axis, and one "outer
   ring" label).
2. **Sky view keeps its numbers where they are** (15° to 75° on the
   north line, 75° nearest the centre) and gains the patch.

### R7 — Reference position: no line between reference positions; the current one joined to the rover in red

**Screen:** Analysis → Reference position, pro.

**The author:** no connection line between the reference position in
use and the previous ones. The one in use is connected with a red line.

**Today:** the trail's dots are joined in order by one faint line ("a
hand-over reads as a jump in the dots", `HandoverDetail`'s comment), so
the line on the screenshot runs from the old reference position (pink)
to the new one (red), and passes near the rover only by chance.

**Settled (author, 2026-10-20):**
1. **A red line from the reference position in use to the rover** at
   the centre — the baseline the corrections come over. Earlier
   reference positions are drawn unconnected.

### R8 — Reference position: the rover's track, coloured by the reference position it used

**Screen:** Analysis → Reference position, pro.

**The author:** the rover gets a track of the positions where it was,
showing its history. Each stretch of the track has the colour of the
reference position used while the rover was there.

**Today:** the plot has the rover at its centre and nothing of where it
has been. `ArpTrail` (`Views.kt`) keeps the reference positions'
latitude and longitude, and a ring of distances for the strip, but no
rover position and no time. The run's state lives in `MonitorService`,
as a record of a run must (MEMORY, *a record of a run outlives the
screen that draws it*), so the track is recorded there too.

**Settled (author, 2026-10-20):**
1. **Each station keeps the colour it gets when it first appears, for
   the whole run**; its dot and its stretch of the rover's track share
   it, and nothing on screen changes colour at a hand-over. The station
   in use is shown by R7's red line and a larger dot, not by its
   colour. The colours are a fixed set of 6–8 distinct ones, without red
   (R7's line) or the rover's blue (`HO_ROVER`), reused in order if a
   drive passes more stations. Today's scheme (`HO_ARP` red for the one
   in use, `HO_HIST` pale red for every earlier one, `Views.kt`) goes.

2. **The rover stays at the centre** (author, 2026-10-20), so the rings
   keep measuring the baseline to the station in use; the track trails
   behind it, and the scale grows to fit everything drawn.

3. **The whole run** (author, 2026-10-20). A point is recorded only
   when the rover has moved, so a parked phone adds nothing, with a
   generous cap as a safety net; 12 h at motorway speed, one point a
   second, is about 43 000 points. The thinning distance and the cap
   are set at implementation and measured on the Huawei.

4. **A rover set by hand draws no track** (author, 2026-10-20): it does
   not move, and the footer already says "rover at the set position".

### R9 — Reference position: a small label at each reference position

**Screen:** Analysis → Reference position, pro.

**The author:** the mountpoints get a small label.

**Settled (author, 2026-10-20):**
1. **A letter in order of appearance: A, B, C, …** — not the station ID,
   because not every station's 1005/1006 carries a number (author), and
   not the hand-over time, which is longer and busier on a small plot.
   The letter pairs with the station's fixed colour (R8). Where an ID
   or a takeover time is known, it can be named beside the letter
   outside the plot, e.g. "B: from 14:02".

### R10 — A change of base invalidates the plots: results from before and after are joined — **withdrawn 2026-10-20 (R11)**

*Withdrawn by the author's decision under R11: the static screens are
for a base that is not moving and do not react to hand-overs; the
moving case is the new reference-position check's. Kept below as the
record of what was decided and why it no longer applies. The
measurement it proposed — what the plots' inputs do across a
hand-over — is not needed for this review; the question of whether
the sky view briefly computes from a wrong position at a hand-over
remains a possible defect of its own, not tracked here.*

**Screens:** every plot that accumulates over a run — Sky view tracks,
C/N0 vs elevation, Self-position's trail, Reference position's trail
and strip, and the hub's Stability figures, which summarise the same
run. Both editions where they exist.

**The author:** when the base changes, the plots become invalid,
because results from before and after are concatenated, with
artefacts. Example: the Sky view, pro, `NEAR`, 62 minutes into a run
(screenshot, 2026-10-17).

**Seen in the screenshot:** several tracks end some way from their
satellite's dot (R09, G08, G18 among them), and some tracks run as
straight segments where the others curve.

**What the code does today** (read 2026-10-17): every accumulator —
`tracks`, `elevation`, `arpTrail`, `selfPos` — is cleared when a run
starts (`MonitorService.start`), and **at no other moment**. A hand-over
on a network service (the reference position moving to another
station, R5–R9's subject) happens inside a run, so everything before
and after it is drawn as one series.

**Not yet known — measured on the device before anything is built:**
whether the artefacts in the screenshot come from the hand-over itself
or from something that coincides with it. Satellites' azimuth and
elevation hardly change between stations tens of kilometres apart
(a few tenths of a degree at most, against the plot's scale of about
four pixels a degree), so the jumps on the screenshot are probably not
geometry. More likely candidates: the new station tracking a different
set of satellites, leaving gaps that a track then bridges with a
straight line; and self-position, whose offsets are measured from the
broadcast reference position and so change meaning when it moves.
Measured by recording a run on `NEAR` across a hand-over and comparing
each series before and after it.

**Decided 2026-10-17 (author):**
1. *Changing base* is the hand-over: on Centipede's `NEAR` the caster
   assigns a new base by distance, and in this run it did so several
   times. The author reads the breaks in the sky view's tracks as the
   new base's different point of view.
2. **Segment**: keep the history, never join across a hand-over, and
   show which station each part belongs to (as R8 does for the
   rover's track).
3. Whether a hand-over restarts the stability window is the core's
   decision, for every frontend, not the app's — agreed.
4. **The mark (author's proposal, 2026-10-17):** where a segment ends,
   a thin black line drawn through the track, across it. On the sky
   view colour already means constellation, so the change is marked by
   that stroke rather than by colour; the parts on either side are not
   joined.

**Settled on the mark (author, 2026-10-17):**
1. The stroke is drawn in the theme's foreground colour: black in the
   light theme, near-white in the dark one.
2. The stroke shows *where* the base changed, not which station each
   part came from; that is enough on the sky view. R8's colours stay on
   the Reference position plot.
3. **Not every plot segments.** *C/N0 vs elevation* and *Self-position*
   are **cleared and restarted** at a hand-over, **with a notice** on
   the screen saying so — a scatter or an offset mixed from two
   stations describes neither. The sky view's tracks segment with the
   stroke; the Reference position plot shows the change through R7–R9.

**Where the restart lives:** the Self-position *plot* (the trail) is
kept by the app (`SelfPosition.kt`), and so is C/N0 vs elevation's
scatter; clearing those is the app's. The Self-position *figures*
(epochs solved, mean offset, scatter, worst residual, apparent motion)
come from the core's station report (`selfpos_scatter_m` and its
siblings, `src/core/station_report.c`), as the GUI, CLI and daemon
show them too. Restarting those at a hand-over is the same kind of
decision as decision 3: made in the core, for every frontend, or the
figures under the plot would describe both stations while the plot
above them shows one.

**Settled (author, 2026-10-17):**
1. The notice reads **"Restarted 14:02: handover to another station"**
   (the time of the hand-over), on C/N0 vs elevation and Self-position,
   and stays until the next hand-over replaces it.
2. *Signal quality* is **not** restarted; it stays as it is. The
   restart applies to C/N0 vs elevation and Self-position only.
3. The Self-position figures restart in the core, and the GUI, CLI and
   daemon get it in the same change.

**Still to measure, because it decides more than the tracks:** how far
a base's change of position can move a satellite on the plot.
Displacing the observer by *d* tilts the local horizon by *d / R⊕*:
for the largest jump in that run, 57.9 km (R5's screenshot), that is
57.9 / 6371 ≈ 0.52°, and the direction to a satellite 20 000 km away
turns by less. On this plot, about four pixels per degree, that is
roughly two pixels. Several breaks in the screenshot are tens of
pixels. Either something besides the viewpoint moves at a hand-over —
for example, the position the sky view computes from being briefly
another one (the ARP absent between stations, or a remembered one) —
or the arithmetic above is wrong. Segmenting hides both, so the
recording across a hand-over (above) comes first. If the sky view does
compute from a wrong position for a while, that is a defect of its own,
and the same position feeds self-position and Reference position.

### R11 — Reference position leaves the static analysis and joins the Network-RTK check

**Screens:** Analysis (pro loses its Reference position tab); the hub
(a new check); the Reference position screen itself, which moves.

**The author (2026-10-20):** the way the reference position is analysed
changes. Its screen is removed from the analysis screens and is not
part of the tier 1 or tier 2 check. It becomes a **separate check**,
because it tests a specific part of the RTK suite. Sky view, Signal
quality, C/N0 vs elevation and Self-position are for **static** analysis
of the base. When moving, only the reference position analysis
matters, and those screens are not part of it.

**What this changes in the items above:**
- **R1** (info button on every analysis screen): no longer covers
  Reference position as an analysis tab. The new check's screen gets
  the same button and summary row? (Open 4.)
- **R5–R9** (the Reference position plot: full width, labels, the red
  line, the rover's track, station labels) are about the plot itself.
  Read as carried over to the new check's screen. (Open 3.)
- **R10** (hand-overs during a run): the static screens are meant for a
  base that is not moving. How R10's restart decisions read now is
  Open 5.

**Today, for reference** (code read 2026-10-17 and 2026-10-20):
- The tab is `AnalysisTab.REFPOS`, shown where `Features.HAS_HANDOVER`
  is set (pro); the screen is `HandoverDetail` in `Views.kt`.
- ~~The hub's `HandoverPanel` shows the reference-position watch during
  any run.~~ **Corrected 2026-10-20:** `HandoverPanel` draws nothing on
  the hub (`Content` is empty since the screen became an analysis tab,
  H8, 2026-10-12). It contributes the Analysis tab (`View`) and its
  share-report section, nothing else.
- The screen colours the distance to the reference position in the
  verdict colours, green under 5 km, amber under 50 km, red beyond
  (`hoDistColour`, `StationCards.kt`): "26,3 km SW of this phone" is
  amber in R5's screenshot. These are thresholds in a frontend, which
  `CLAUDE.md` forbids, and they read as a verdict where R11 decides
  there is none. Decided under 6 below: A3's own verdict.
- The Network-RTK check is already a separate run on the hub
  (`VrsPanel`, R3), with its own card and its own end.
- Whether a station is a fixed base or a network service, and so what a
  hand-over means, is decided in the core (`station_class.c`), as are
  the network-RTK assertions (`vrs_check.c`).
- Pro's store listing names "Reference-position watch" among what the
  edition adds; it is reworded when this ships (`play-listing.md`).

**Revised later on 2026-10-20 (author), replacing decisions 1 and 2
below:** reference position becomes **part of the Network-RTK check**.
During a Network-RTK check, **Analysis** (the bar at the bottom) opens
**only** the Reference position screen; during every other run it opens
the static screens (Sky view, Signal quality, C/N0 vs elevation,
Self-position) and not Reference position. There is no separate
reference-position check and no fourth run button.

**Decided 2026-10-20 (author), as first given:**
1. ~~Its own run button on the hub, running until stopped.~~
   *Replaced by the revision above.*
2. ~~Separate from the Network-RTK check.~~ *Replaced by the revision
   above.*
3. **No verdict.** It shows and records; nothing in it judges, so it
   needs no change in the core.
4. **R1 and R2 apply** to its screen: the info button with the summary
   row, and the one-line app name.
5. **(b): R10 is withdrawn.** Sky view, Signal quality, C/N0 vs
   elevation and Self-position are for a static base and do not react
   to hand-overs. With it goes R10's one core change (restarting the
   Self-position figures), so **nothing in this review now touches
   `src/core`**.
6. During the Network-RTK check the static screens are **hidden**:
   Analysis opens the Reference position screen alone (revision above).
7. The reference-position card (`HandoverPanel`) **moves into the
   Network-RTK check only**; ordinary runs no longer show it on the hub.
   *Premise corrected 2026-10-20: there is no such card on the hub (see
   Today). What moves is the Analysis tab (6) and the share-report
   section (below).*

**Settled with the revision (author, 2026-10-20):**
- The share report today carries a *Reference position* section in
  every pro run with a valid ARP (`HandoverPanel.shareSection`); it
  will be carried by the Network-RTK check's report only.
- The listing and wiki wording ("Reference-position watch") is changed
  in the same release.

**Decided on the revision (author, 2026-10-20):**
1. **The Network-RTK check shows its result when the gate answers, then
   keeps watching the reference position until stopped**, and shows
   that it is still running. Today it ends when the gate answers
   (`VrsPanel`'s comment); that end becomes the moment the result
   appears, not the end of the run.
2. **Its verdict is unchanged**: the five assertions and the gate
   (`vrs_check.c`); reference position adds none.
4. **Button order: the Network-RTK check last**, after the tier 1 and
   tier 2 buttons, at the bottom: *Run again*, *Watch station*,
   *Network-RTK check*.

3. **One card for the whole check (option A, author, 2026-10-20).**
   - *During:* the Network-RTK result card fills in A1–A5; at the
     gate's answer it shows the service's classification and a line
     saying the run goes on, e.g. "Watching the reference position ·
     Stop to end". *Stop ■* ends it, as for any run.
   - *After:* the same card, with a one-line reference-position summary
     added (e.g. "2 hand-overs · largest jump 57.9 km"); all detail
     stays in Analysis.
   - *The button* stays at the bottom, after *Watch station*, always:
     greyed out during a run, never replaced by the card. **This settles
     R3** (the button appearing and disappearing).
   - Rejected: the result card alone (B), and a second card for the
     reference position (C), which would suggest two checks.
   - **After the check is stopped, Analysis keeps showing the Reference
     position screen until the next run starts**; then it follows that
     run's kind.

**From the revision — reconsidered and decided (author, 2026-10-20):**

5. Item 4's subtitle: the Network-RTK button gains one like the others
   (R3)? **Decided (author, 2026-10-20):** "Checks plus the network at
   your position — an answer in about two minutes, then follows the
   reference position until stopped", following the other buttons'
   pattern (what it runs — what to expect in time) and naming no tier,
   because tiers 1 and 2 judge the station and this judges the service
   where the rover is. **Measured on the Huawei (font scale 1.0,
   1080×2340), 2026-10-20: it does not fit two lines.** The subtitle
   area is 876 px wide and holds about 53 characters a line ("Checks
   plus stability (tier 2) — needs ten minutes of" fits, "…about two
   minutes", 60, does not); the text above is 126 characters, three
   lines. **Final (author, 2026-10-20): "Checks plus the network at
   your position — answer in ~2 min, then runs until stopped"** (86
   characters; two lines by the same count, breaking after "answer
   in" — confirmed rendered on the Huawei at implementation). It keeps
   the one fact a user cannot learn without running it, that this run
   never ends by itself; the reference position is introduced by the
   screen it opens and its help text.
6. **The distance colour. Decided (author, 2026-10-20): coloured with
   A3's own verdict, read from the core's report** — PASS, WARN or FAIL
   as the Network-RTK card's A3 row says, neutral while A3 is PENDING
   (no position sent yet). `hoDistColour`'s own boundaries go. Read
   2026-10-20: they contradicted the core (A3, `vrs_check.c`: PASS to
   `arp_max_km` = 50 km, WARN to twice that, FAIL beyond; the app:
   green under 5 km, amber to 50 km, red beyond — so 26 km read amber
   beside an A3 PASS), held a private 5 km boundary with no basis, and
   copied the 50 km where the core reads it from the threshold policy.
   No core change: A3's verdict is already in the report, refreshed
   with each GGA, so the colour follows the drive. *Original question,
   kept:* drawn in one
   neutral colour, since reference position has no verdict? If a
   distance ever should be judged, that is a rule for `src/core`, which
   this review otherwise leaves untouched. *For the reconsideration
   (read 2026-10-20):* the 50 km amber/red boundary is a copy of the
   core's A3 ceiling (`VRS_ARP_MAX_KM`, `src/core/vrs_check.h`), held a
   second time in the app; the 5 km green boundary has no counterpart in
   the core at all. `android/design/views.md` §5 calls them "A3's
   usability colours".

### R12 — Every polar plot the same size and layout, the sky view's

**Screens:** Sky view, Self-position, Reference position (the polar
plots; the mountpoint map is a pannable map, not one of them, and keeps
only R6's labels).

**The author (2026-10-21):** all polar plots have a different size.
They shall be the same size, with the same legend, as specified for
this review (R6), using the sky plot's size and layout as the reference.

**Measured on the author's screenshots (923 px wide, Huawei, 10:21):**

| Plot | Circle diameter | Cardinals | Ring labels | Legend |
|---|---|---|---|---|
| Sky view | ~720 px | N, S, E, W outside the circle | on the north line | dot and label row under the footer |
| Self-position | ~850 px, full width | N and E only, inside the edge | along the east axis | dot and label row |
| Reference position | ~490 px | none | one "outer ring" label, top left | dot and label row |

**What it means for the plan:** one polar frame, drawn by one
composable that all three use, with the sky view's geometry: the same
square, the same margin for the four cardinal letters outside the
circle, the same ring lines and R6's labels on the north line, and the
legend in the sky view's row under the plot. Each plot draws only its
own content inside the frame. R5 ("as wide as the other plots") is
read as "as large as the sky view": smaller than full width, and the
same on all three. Done in step 4 for Reference position and step 5
for Sky view and Self-position, the frame written once in step 4.

## Suggestions to discuss — not decided

From another reviewer, passed on by the author on 2026-10-17 in Dutch.
Each is discussed with the author before it becomes a finding
(R-number) or is set aside. Numbered S1, S2, … so a decision can name
them. The original wording is kept under each.

**Decided 2026-10-17 (author):**

| | Decision |
|---|---|
| S1 | **Covered by R1.** The relevant information goes behind the info button: a balance between information and accessibility that does not cost the tool much of its working room. |
| S2 | **Adopted, into R1.** The summary each screen already has goes at the same height as the info button, in the row it shares. No new sentence is written. |
| S3 | **Dropped.** The app is an engineering tool; without the field's knowledge it cannot be used, and its terms are that knowledge. |
| S4 | **Dropped where the findings address it**; the two parts left over settled 2026-10-20 (under S4 below). |
| S5 | **Adopted 2026-10-20**, specified by the author (under S5 below); details open. |

### S1 — Explain more: what the app is for, and how to find one's way

The app does not explain what it is for, and its layout and navigation
make it hard to use without prior knowledge.

> Ik zou meer uitleg geven: de app legt niet uit waarvoor hij is, en
> layout en navigatie maken hem lastig zonder voorkennis.

**Bears on:** R1, which moves explanation *off* the page and behind a
button. The two pull in opposite directions unless the pop-ups carry
it; to be settled together.

### S2 — State the purpose at the top, in one sentence

One sentence at the top: what the app is for and for whom — and that it
does not make the phone accurate to the centimetre.

> Doel ontbreekt imo, mss bovenaan in 1 zin waarvoor de app is en voor
> wie (en dat je telefoon er niet cm-nauwkeurig van wordt (afaik)).

**Bears on:** the store listings already open with that sentence ("is it
fit to serve RTK, and if not, why?") and say "It does not steer a
rover and it does not compute a position" (`play-listing.md`). The
hub does not say it (screenshot R3/R4); where else the app says it,
About included, is checked before deciding. R2 makes room in the bar, but the bar takes no title
parameter, so a sentence would be a hub element, not the bar.

### S3 — Less jargon, or a glossary

Much jargon and many abbreviations. Can it be simpler? Otherwise a list
of explanations.

> Veel jargon en afkortingen. Kan het eenvoudiger? Anders een lijst
> met uitleg (kan AI zo maken).

**Bears on:** the terms are the field's own (RTCM, ARP, C/N0, PDOP,
VRS, MSM); the audience is operators, surveyors and installers
(`CLAUDE.md`), who use them. A glossary would be a natural home in R1's
pop-ups or in the wiki, which the app already links to. Any glossary is
written and checked by hand against the code, not generated and
trusted.

### S4 — The hub's layout: READY says nothing; the main button first; explain Network-RTK

The *READY* bar on the start screen says nothing. The main button,
*Run the check*, at the top, the rest below. The Network-RTK check
lacks an explanation.

> Layout: Startscherm: de READY-balk zegt niks. Hoofdknop 'Run the
> check' bovenaan, rest eronder. Network-RTK check mist uitleg.

**Bears on:** R3 (the Network-RTK button's placement, and that it is
the one run button without a subtitle) and R4 (the order of the run
buttons). Moving the run buttons to the top is a change to both
registries' order, which is the layout.

**What the findings address, and what they do not:**
- *Network-RTK check lacks an explanation* — R3 (its subtitle). Dropped.
- *The READY bar says nothing* — **settled (author, 2026-10-20): the bar
  is hidden in the READY state only** (set up, never run). Before a
  first run the hub opens with R1.5's summary row and ⓘ; the bar appears
  when a run starts. The grey *not configured* state stays, because it
  says what to do (`VerdictBadge`, `StationCards.kt`).
- *Run the check at the top, the rest below* — **settled (author,
  2026-10-20): the run buttons stay where they are**, below the results,
  **with the tiers in order, 1 before 2**: *Run again* (tier 1), *Watch
  station* (tier 2), then *Network-RTK check* (R4, R11). The same order
  holds for the results above them: the eight checks (tier 1) before
  the Stability card (tier 2), as pro's registry has them today. Checked
  on the Huawei at implementation that, with the READY bar hidden before
  a first run (above), *Run the check* is on screen without scrolling.

### S5 — No verdict before the check is done; the progress bar is wrong; a way back

*STATION OK* already shows while the test is still running, and the
progress bar is wrong. Show the verdict only when everything is done.
And it is not clear how to get back to the main screen, so a menu or
something to go back with would help.

> 'Station OK' verschijnt al terwijl er nog getest wordt, en de
> voortgangsbalk klopt niet. Oordeel pas tonen als alles klaar is. (en
> ik weet ook niet hoe ik terugkom in hoofdmenu dus menuutje zou handig
> zijn of iets waarme ik terug kan)

**Bears on:**
- *When* a verdict may be shown is presentation and can live in the
  app; *what* the verdict is stays the core's (`kpi.c`, hard
  constraint). Showing nothing until the run ends must not change the
  verdict itself, and the CLI and GUI would still show it their way.
- "The progress bar is wrong" needs the reviewer's case: which run,
  what it showed and when. Not yet reproduced.
- The way back from Analysis is the ← in the bar and the phone's Back.
  If that was not found, it is a finding about how visible it is.
**What the code does, read 2026-10-20** (the roll-up in
`src/core/kpi.c`, lines 545–590; `VerdictBadge`, `StationCards.kt`):
- **The verdict word is the core's.** *STATION OK* is withheld until
  every check has passed continuously for 60 s (`KPI_SUSTAIN_S`);
  until then the word is *RUNNING*. *CAUTION* and *FAILED* are shown
  at once, deliberately ("a warning the user cannot see yet helps
  nobody"). So in a plain check, *STATION OK* cannot appear early.
- **But some runs go on after tier 1 has settled**: a watch (tier 2
  needs `SR_MIN_WINDOW_S`, 600 s, of evidence and then runs for hours)
  and, after R11, the Network-RTK check (gate test, then the reference
  position until stopped). In both, the bar says *STATION OK* while the
  run is still testing something else. That is the likeliest reading of
  "STATION OK while still testing".
- **The progress bar is not the run's progress.** In a check it shows
  how long the *current* verdict has held, as a fraction of 60 s
  (`sustainFraction`). It stays empty while any check is still
  gathering evidence (*RUNNING*, e.g. the ARP's 30 s allowance), and
  **starts again from empty whenever the verdict changes**, OK to
  CAUTION or back. In a watch, before tier 2 has a verdict, it shows
  tier 2's evidence against 600 s instead, then switches back to tier
  1's bar. Read as "how far along is the run", each of these looks
  wrong.

**Decided (author, 2026-10-20):**
- **S5.1 (b): no verdict word until the run's own window is complete.**
  A check (tier 1) shows its verdict after 60 s of success; a watch
  (tier 2) shows its verdict after 600 s, and, since a watch then
  continues tier 2, keeps showing it from there on. Until then the bar
  says the run is running.
- **S5.2 (a): the bar is the run's progress**, towards the 60 s (tier 1)
  or 600 s (tier 2) window. Before that window starts — while the
  checks are still preparing — it does not advance; in its place, **8
  small indicators, one per check, show each check's state by its
  colour**, filling as the checks become ready.
- **S5.3 (b): a *Back to checks* bar on Analysis**, in the bottom bar's
  place, the counterpart of the *Analysis* bar that led there.

**Settled (author, 2026-10-20):**
1. **After 600 s a watch shows tier 2's own word**: STABLE, DEGRADED or
   UNSTABLE.
2. **CAUTION and FAILED are shown at once**, as the core has them today;
   only *STATION OK* (and tier 2's word) wait for the window.
3. **When the verdict changes inside the window, the bar restarts, and
   says why** — e.g. "restarted: CAUTION at 0:42" — so a bar going back
   to empty is explained, not mysterious.
4. **The Network-RTK check is an extension of tier 2**, as the button
   order already says (R4, R11): tier 1, tier 2, then the network-RTK
   part. *Open 6* below follows from this.
5. **The 8 indicators are extra**, beside the bar, and show exactly
   what each check's own card shows (PASS, WARN, FAIL, PENDING), drawn
   from the same report so they cannot disagree with the cards.

6. **Reference-position monitoring starts at the start of the
   Network-RTK check**, runs continuously until the user stops it — not
   ending after 60 s or at the gate's answer — and is **offered nowhere
   else**: not in a check (tier 1), not in a watch (tier 2) (author,
   2026-10-20). "An extension of tier 2" (4) is about the order of the
   run buttons and what the Network-RTK run contains, not about putting
   reference position into Watch station.

**Decided (author, 2026-10-20), on the agent's proposal with its
rationale:**
- **6a. The Network-RTK check does not contain tier 2.** It runs tier 1
  (60 s), then the gate test, then reference-position monitoring until
  stopped. Tier 2 asks whether a station has been stable over a long
  still observation; in a check made for driving its window would span
  hand-overs and describe several stations at once, and freezing it
  after 600 s would mean ten minutes standing still before every drive.
  *Watch station* remains the tier 2 tool, run on the network's
  mountpoint while still. "An extension of tier 2" (4) therefore means
  the widest-scope check, after tier 2 in the button order — not one
  that contains it.
- **6b. The gate test runs after tier 1**, as today (about 2 min in):
  A1–A5 and the gate are built on the eight checks having held, the
  bridge already works so (`test_bridge_vrs.c`), and the answer arrives
  while the rover is still at its start. The one bridge change: the run
  continues after the gate instead of ending (R11, 1).
- **7. The eight indicators show through the whole run**: they show
  which check changed when the bar restarts (3), cost one row, and
  mirror the cards (5), so they are never a second opinion.

**Superseded by 6a–6b above (kept as the record):**
6a. *Tier 2 inside the Network-RTK check, while driving.* If the
   check's tier 2 part is still gathering its 600 s when the rover
   drives off, on a network service its window spans hand-overs and the
   stability word describes several stations at once — R10's objection,
   now inside this check. Proposed: tier 2's word inside the
   Network-RTK check is frozen when its 600 s window completes and
   labelled "measured at the start position" (a display choice; the
   core is unchanged); or the check has no tier 2 part at all.
6b. *The gate test's place:* after tier 1, as today, or after tier 2's
   window?
(Superseded by 6 above:) *What "an extension of tier 2" means for the run.* Today the
   Network-RTK check enters its gate test as soon as **tier 1** has
   held (`VrsPanel`'s comment; the bridge's VRS workflow). As an
   extension of tier 2, does it first gather tier 2's 600 s, show tier
   2's word, and only then run the gate test, before watching the
   reference position until stopped (R11)? That moves the gate test
   from about two minutes into the run to about eleven, and changes the
   bridge's workflow (`ntrip_bridge.c`, `test_bridge_vrs.c`), though
   not the core.
7. *When the 8 indicators show:* through the whole run, or only until
   the window starts?

**Noted for S5 (read 2026-10-20):** during a watch, the bar's
  progress indicator and its line count towards the tier 2 evidence
  floor as a literal **600** in the app (`VerdictBadge`,
  `StationCards.kt`), a number the core holds as `SR_MIN_WINDOW_S`
  (`src/core/station_report.h`); the app does not
  read it from the core, so the two could drift. Whether this is the
  "progress bar is wrong" the reviewer saw is not known.

## Implementation

Planned 2026-10-20, after the author closed the collection. Six steps,
each ending with both editions built and its own check on the Huawei;
nothing is staged as done until that check has passed. Paths are under
`android/app/src/main/` unless they say otherwise (`java/…` is
`java/nl/pe1mew/ntripanalyser/`).

**Two findings from reading the code, which shape steps 2 and 3:**
- **The gate test stops the position reports** (A5, `vrs_check.h`:
  "after GGA stops, the stream drops within 90 s"), and a Network-RTK
  run has auto-reconnect switched off on purpose, so a silent reconnect
  cannot hide that drop (`ntrip_bridge.c`, bridge open). "Keep watching
  the reference position after the gate" (R11, 1) therefore needs a
  **monitoring phase** in the bridge: once the gate has answered,
  resume the position reports and reopen the connection with reconnect
  on. ~~The bridge can do this with the session calls it already uses,
  so `src/session` and `src/core` stay unchanged.~~ *Wrong, found
  2026-10-21:* a session's reconnect is fixed at open, and a session
  that ended on the gate's drop cannot be revived with any existing
  call. The author chose one additive session call over a fresh session
  after the gate, which would have restarted every count mid-run (step
  3). While the gate test runs (up to 90 s) the caster is sent no
  position, and the screen says so.
- **The core's 600 s reaches the app without a core change**: the
  bridge adds the report policy's `min_window_s` to the document it
  already publishes, beside `sr_to_json`'s output, and the app stops
  holding its own 600 (S5).

### Step 1 — The frame: name, info button, summaries, way back

*Items:* R2; R1 (1–6) and R1 texts; S2; S5.3.

- The app name at one smaller size in the bar, every screen
  (`java/Shell.kt`, `TopAppBar`'s title). Size chosen by measuring on
  the Huawei: "NTRIP Analyser Pro" on one line at font scale 1.0.
- The analysis template (`AnalysisBands`, `java/Views.kt`) gains the
  layout of R1.6: status line, then the summary row with ⓘ at its
  right, then the screen's own header, then the plot. The explanatory
  prose moves out of each tab into a help dialog styled like the
  mountpoint list (`java/Dialogs.kt`): title, scrolling text, *Close*;
  a tap outside and Back close it too.
- Self-position's paragraph moves into its dialog
  (`java/SelfPosition.kt`).
- The hub gets the same row at its top — the store's short description
  and ⓘ — as a panel in `java/HubPanels.kt`, named in both editions'
  `Registry.kt` (an edition carries no code of its own, so the panel
  lives in `main`).
- All summaries and help texts from *R1 texts* go into
  `res/values/strings.xml`, word for word as reviewed.
- Analysis gets a bottom *Back to checks* bar in the place of the hub's
  *Analysis* bar (`java/Shell.kt`'s analysis-bar slot,
  `java/AnalysisScreen.kt`).

*Check:* both editions; every analysis tab and the hub show the row
and open the right text; the dialog closes by *Close*, a tap outside
and Back, and the screen keeps its scroll position; *Back to checks*
returns to the hub; the name is on one line at font scale 1.0, and is
looked at on the largest font size and reported (R2).

**Done 2026-10-20, Huawei, `.dev` builds of both editions** (installed
19:37–19:38; before-shots taken first from the Play apps):
- Hub: the summary row with ⓘ at the top, both editions. The help is
  the edition's own: pro carries the watch and Network-RTK lines and
  "a watch adds…", free has neither.
- Every analysis tab, pro's five and free's three: the summary row
  with ⓘ, and the dialog with the screen's name and its reviewed text.
  Sky view's tracks sentence in pro, absent in free.
- The dialog closes by *Close*, Back and a tap outside, the app keeping
  focus each time. *Back to checks* returns to the hub.
- **R2, measured:** with the real name built into a dev build for the
  test (the dev and bench builds carry shorter names) and reverted after:
  at font scale 1.0, one line everywhere; **at the largest, 1.45, one
  line on the hub (608 px) but two lines on the analysis screens**,
  where the back arrow leaves 600 px. Reported for the author's
  decision, as R2 says, rather than cut short. **Accepted (author,
  2026-10-20):** one line at every size but the system's largest, where
  wrapping is what large-text users expect; the size stays
  `titleMedium`.
- Not tested: the help text already describes the Network-RTK check as
  running until stopped, which arrives in step 3.

### Step 2 — The hub: order, verdict bar, progress

*Items:* R3, R4, S4.1, S4.2, S5.1–S5.7, R11 (5), R11 (4).

- Pro's run buttons in the order *Run again*, *Watch station*,
  *Network-RTK check*, below the results, tiers 1 then 2
  (`src/pro/java/…/Registry.kt`). The Network-RTK button leaves
  `VrsPanel`'s card and joins the run buttons: always present, greyed
  during a run, with its subtitle (R11, 5). `VrsPanel` keeps the result
  card only.
- The verdict bar (`VerdictBadge`, `java/StationCards.kt`):
  - hidden in the READY state; the grey *not configured* state stays;
  - *STATION OK* only once the 60 s window has held (the core already
    withholds it), tier 2's word (STABLE, DEGRADED, UNSTABLE) once the
    600 s window is complete in a watch, *CAUTION* and *FAILED* at once;
  - the progress bar is the run's progress towards its window: 60 s
    from the core's `sustainTargetS`, 600 s from the bridge's new
    `min_window_s` (`cpp/ntrip_bridge.c`, `java/Model.kt`); when the
    verdict changes inside the window it restarts and says why
    ("restarted: CAUTION at 0:42");
  - the eight indicators, beside the bar through the whole run, drawn
    from the same report as the cards.
- Free: one run button and tier 1 only; the same bar rules, without the
  watch's.

*Check:* both editions, on a live caster: READY hidden before a first
run and *Run the check* on screen without scrolling; *STATION OK* not
before 60 s; a watch shows tier 2's word at 600 s; the bar restarts
with its reason when a check turns amber (a station or mountpoint that
warns, chosen on the day); the indicators match the cards at every
step; the Network-RTK button's subtitle on two lines (R11, 5). Desktop:
`test_all` still 26/26.

**Results 2026-10-20, Huawei, pro `.dev` on `NEAR`:**
- READY hidden; the hub opens with the summary row; *Run the check*,
  *Watch station*, *Network-RTK check* in that order, *Run the check*
  on screen without scrolling. The Network-RTK subtitle renders on two
  lines (120 px at 60 px a line).
- A check: *RUNNING* and the bar at 0 while checks wait ("5 pass, 3
  waiting"); *CAUTION* at once when check 8 warned (~30 s); the bar
  counting 9 → 26 → 44 → held 60 s; "Finished after 90 s". The eight
  indicators matched the cards at every sample.
- During a watch the Network-RTK button is disabled (`enabled=false`),
  *Stop* enabled.
- A watch: evidence counted against the core's floor, now sent by the
  bridge (`sr_min_window_s`): "42/600 s" → "549/600 s", then
  "stability: STABLE over 0.2 h".
- **Decided on the way (author, 2026-10-20):** after the floor a watch's
  headline is tier 2's word whatever tier 1 says (NEAR held CAUTION over
  STABLE as first built); tier 1 stays visible in the indicators and on
  its card. **Verified** on a second watch: CAUTION with "evidence
  597/600 s", then **STABLE** at 19 min ("stability: STABLE over 0.3
  h"), the indicators still showing tier 1's one warning.
- Free, `RFSEE01`: READY hidden, *Run the check* the only run button;
  *RUNNING* through the waiting checks and the 60 s window (10 → 25 →
  42 → 58 s), **STATION OK only once it held**, "Finished after 120 s",
  "8 pass".
- **The restart reason is built but not observed.** A Wi-Fi drop
  inside the window does not restart the bar: the stream stops, check 1
  fails, and the core settles FAILED at once ("Finished after 46 s"),
  which is right. A restart needs the verdict to change without a hard
  failure (OK to CAUTION or back), which a station does or does not do
  on its own. Recorded as not observed, by the author's agreement, and
  watched for in the runs of steps 3 and 4; the app has no Kotlin unit
  tests to pin it on the desktop.
- Desktop, after the bridge change: 0 warnings, 26/26 tests.

### Step 3 — The Network-RTK check runs on after the gate

*Items:* R11 (1, 3, 6, 7, the revision), S5.6, S5.6a, S5.6b.

- **Bridge** (`cpp/ntrip_bridge.c`): after the gate's answer, a
  monitoring phase — position reports resume, the connection is reopened
  with reconnect on, and the check's result (A1–A5, the gate, tier 1's
  word) is kept as the check's, not recomputed from the new connection.
  The reference position, A3 and the hand-over count carry on.
  `test/test_bridge_vrs.c` gains the phase: after the gate's drop on
  the loopback caster, a new connection, positions sent again, the
  check's result unchanged. Written and falsified before the Kotlin
  side changes.
- **Service** (`java/MonitorService.kt`): a Network-RTK run no longer
  ends when the gate answers; *Stop* ends it. The run's kind (check,
  watch, Network-RTK) is recorded in `RunState` and kept after the run
  ends, until the next one starts.
- **Analysis** (`java/MainActivity.kt`'s `analysisTabs`,
  `java/AnalysisScreen.kt`): during and after a Network-RTK check, only
  Reference position; during and after any other run, the static
  screens without it.
- **Hub**: the Network-RTK card shows the classification, then
  "Watching the reference position · Stop to end", and after the run a
  one-line reference-position summary (hand-overs, largest jump). The
  *Reference position* share section only in a Network-RTK run's report
  (`HandoverPanel.shareSection`).

*Check:* on the Huawei, pro, `NEAR`: the gate answers at about two
minutes, the stream comes back with positions sent, the run continues
until *Stop*; Analysis shows only Reference position, and keeps it
after *Stop* until the next run; a check or a watch shows the static
screens without it. On a fixed base, the gate answers *not gated* and
the run continues the same way. Hand-overs without driving: moving the
set position on `NEAR` far enough that the caster assigns another
base.

**Results 2026-10-21:**
- *Session* (`src/session`): `ns_set_auto_reconnect(s, on)`, documented
  in the header only; a live session ended on a drop is revived, one
  stopped by the caller or replaying a file stays ended.
- *Bridge*: the monitoring phase begins at the gate's answer, either
  way. Reconnect on, GGA resumes at once, the eight checks and A1–A5
  held as they stood; A3 alone re-evaluated, by the core's rule. Held
  because the core reads any disconnect after the gate began as the
  answer: unheld, a fixed base's *not gated* turned *gated* on the
  first drop of a drive.
- *`test_bridge_vrs.c`*, 39 checks: after a gated drop the same session
  reconnects, the reconnect is counted, GGA reaches the caster again,
  the result stands; a fixed base's *not gated* survives a drop.
  **Falsified three times**, each property broken in its own copy of
  the bridge, each failing its own checks and no others (no reconnect
  → 3 failures; no GGA after the gate → 1; result not held → 1).
- *Four programs*, because the session layer is shared: desktop build
  0 warnings, 26/26 tests; the CLI's live `--check` on `RFSEE01`:
  STATION OK, exit 0; the daemon by CI.
- *Huawei, pro `.dev`, `NEAR`*: CAUTION at ~40 s, held 60 s at ~93 s,
  the gate's answer at ~6 min ("not gated (fixed base?)": Centipede
  keeps streaming when GGA stops; the service is still classed a
  network by its name), and **the run went on**: "Watching the reference
  position · Stop to end", *Stop* offered, data still arriving (2140 →
  2490 B/s), the verdict held at "CAUTION · Held for 2 min". Analysis:
  Reference position alone, during the check and after *Stop*; the next
  run, a check, showed Sky view, Signal quality, C/N0 vs elevation and
  Self-position and no Reference position. After *Stop* the card's line
  became "No hand-over: one reference position throughout".
- Not yet seen on the device: a hand-over during the monitoring phase
  (step 4 forces one by moving the set position), and the *Reference
  position* share section's absence from a check's report (read in the
  code, `HandoverPanel.shareSection`).

### Step 4 — The Reference position plot

*Items:* R5, R6 (for this plot), R7, R8, R9, R11 (6).

- **The shared polar frame (R12)**, written here once: the sky view's
  size, margin, cardinal letters, ring lines, R6's north-line labels on
  a patch, and its legend row. Reference position draws in it, the
  distance strip below, the screen scrolling (`HandoverDetail`,
  `java/Views.kt`).
- A red line from the station in use to the rover; earlier stations
  unconnected.
- The rover's track for the whole run, recorded in `MonitorService`
  (a record of a run outlives the screen), a point only when the rover
  has moved, with a cap; thinning distance and cap set by measurement,
  and the memory the track uses measured on the Huawei over a long run.
- Each station a fixed colour from first appearance (6–8, no red, no
  rover blue), shared by its dot and its stretch of track; the one in
  use drawn larger; a letter A, B, C… beside each. `ArpTrail` records
  which station each point belongs to.
- Ring labels on the north line, on a patch (R6).
- The distance's colour is A3's verdict from the report, neutral while
  pending; `hoDistColour` and its boundaries go.

*Check:* on the Huawei, by moving the set position on `NEAR` to force
hand-overs: colours never change, letters in order, the red line to the
station in use, the track's stretches in their station's colours, the
distance coloured as the A3 row says. A rover set by hand: no track.

**Results 2026-10-21, Huawei, pro `.dev`, `NEAR`:**
- **The shared frame, `PolarCanvas`**, carries all three polar plots:
  Sky view and Self-position moved onto it in this step too, at the
  author's direction (R12 brought forward from step 5). Measured on the
  side-by-side screenshots: one diameter and one horizontal place for
  all three, N E S W outside the circle on each, ring labels on the
  north line on a patch ("15°…75°", "2.50 m / 5.00 m", "93,4 km /
  186,9 km"), legends in the sky view's dress. The vertical place still
  follows the text above each plot.
- Reference position: station **A** in its colour with its letter, the
  red baseline to the rover, the distance strip at its own height, the
  legend "rover · station in use · stations A, B, … and the track near
  each".
- **R11.6 refined (author, 2026-10-21, option a):** A3 judges the
  distance from the position *sent*, the screen shows the distance from
  the *rover*; the colour applies only when they are one distance (live
  GGA, or the rover is the set position). At a desk with a set position
  near the station, a 155,7 km phone distance had come out in A3's
  green; it is now neutral.
- **Not seen at the desk, and why:** hand-overs, a second letter, the
  track and its colours. Settings are locked during a run, so the set
  position cannot be moved mid-run, and a new run clears the trail;
  only a moving phone shows them. Left for the author's drive.

### Step 5 — Scale labels on the other plots

*Items:* R6 (Sky view, Self-position, mountpoint map).

- Sky view: moved onto the shared frame of step 4 (R12), unchanged in
  size and layout, gaining the patch behind its elevation numbers
  (`SkyCanvas`, `java/Views.kt`).
- Self-position: moved onto the shared frame (R12), so its circle
  shrinks from full width to the sky view's size and gains all four
  cardinal letters; every ring labelled on the north line, on a patch
  (`java/SelfPosition.kt`).
- The mountpoint map: labels from the north-east diagonal to the north
  line (`java/MountMap.kt`), both editions.

*Check:* each plot on the Huawei, labels readable over tracks and
points, the "N" and the north line clear.

**Results 2026-10-21:** Sky view and Self-position went onto the shared
frame in step 4 (R12, at the author's direction), so step 5 was the
mountpoint map alone: its ring labels moved from the north-east diagonal
to where each ring crosses the north line, just right of it and just
inside the ring, on the map's existing patch (`MountMap.kt`). Seen on
the Huawei in both editions: pro on `caster.centipede.fr`, 1308 drawn,
"50,0 km", "100,0 km", "150,0 km"; free on `rfsee.net`, "10,0 km" to
"40,0 km".

### Step 6 — What the project says about itself

- `android/design/views.md`: §3 (cells, not points and a trend), §4 and
  §5 (Reference position inside the Network-RTK check, its colours from
  A3), the run-button order and the verdict bar.
- `docs/wiki/`: the pages that describe these screens and runs
  (The-analysis-views, Network-RTK-check, Watch-mode, Pro-edition,
  What-the-paid-edition-adds), checked by `tools/check_release.py`'s
  link check; published by the author after release.
- `design/work-items/play-listing.md`: the pending change for pro's
  listing ("Reference-position watch" becomes part of the Network-RTK
  check), for the author to enter in the console with the release.
- `changelog.md` `[Unreleased]`, with the measurements behind each
  claim; `design/feature-matrix.md` where a row changes.

*Check:* `python tools/check_release.py`, `python
tools/verify_memory.py`, CI on the pushed commit.

**Done 2026-10-21:** `android/design/views.md` (§1's ephemeris stream
corrected to pro only, §3's cells, §4's plot, §5 rewritten for the
network-RTK check, and two new sections: the polar frame, and every
screen's summary and ⓘ); the wiki's Network-RTK check, analysis views,
getting started, watch mode, pro edition and paid-edition pages;
`design/feature-matrix.md`'s two rows; `changelog.md`'s `[Unreleased]`;
and in `play-listing.md` a **pending** change to pro's bullets, English
and Dutch, for the author to review before the next release.
`check_release.py`: only the 3.9.0 bundles predating these sources
fail; the wiki's links resolve. The wiki is published by the author,
after the release.

## Verification on the Huawei

Per step, as written under each above, on the Huawei P smart 2019
(Android 10, font scale 1.0, 1080×2340), **both editions**, installed
as `.dev` builds beside the Play apps (`docs/RUNBOOK.md`). Before any
tap, the focused window is read (`mCurrentFocus`), per the runbook.
Screens are compared against screenshots taken before step 1. Live
casters: `NEAR` (Centipede, network service), `RFSEE01` (fixed base),
and whichever mountpoint is needed to make a check warn. A hand-over is
forced by moving the set position, not by driving; a real drive is the
author's, after step 4.

Nothing in this review changes `src/core`, so the four-program rebuild
rule does not apply; `test_all` is still run after step 3 because the
bridge's test runs there.
