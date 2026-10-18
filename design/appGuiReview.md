# Android app GUI review — plan

The author's review of the Android app's screens, collected item by item
as the author gives them, and implemented only once the author says the
list is complete. Verified on the Huawei P smart 2019 (Android 10), in
both editions.

Started 2026-10-17. **State: collecting** — nothing here is built yet.

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

**Open:**
1. What stays on the page. The proposal: status (*Analysing for 62
   min*), the verdict line (*SOLVED — no verdict*), the figures, the
   plot, the latest-epoch line and the legend stay; only prose that
   explains goes into the pop-up. Is the legend data or explanation?
2. The button: an ⓘ icon in the row of the status line, as marked,
   rather than in the app bar beside Share and the menu?
3. "Like the mountlist table": a dialog over the screen with its own
   close button, as the mountpoint browser is, rather than a sheet
   from the bottom?
4. Where a tab has no explanatory text today, it gets the button
   anyway, with one written, or no button?

**Added from S2 (author, 2026-10-17):** the row the info button sits in
carries the screen's existing one-line summary, at the same height —
on the Reference position tab today "Where the service's reference
position stands, and where it has been this run." Only the longer
explanation goes behind the button. Which line is each tab's summary
is read per tab at implementation.

**Open from S2:**
5. The hub too? S2 asked for the app's purpose on the start screen; R1
   covers the analysis screens. Does the hub get an info button and a
   summary line, and if so, which existing summary?
6. Where the status line (*Analysing for 62 min*) goes: above the row,
   or in it beside the summary?

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

**Open:**
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

**Open:**
1. If R3 moves the Network-RTK button among the run buttons, where does
   it go: after *Run again*, after *Watch station*, or between them?

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

**Open:**
1. The distance strip: stays below the plot, with the screen scrolling
   to reach it, or moves elsewhere?

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

**Open:**
1. On the line due north, each value sits where its ring crosses it,
   just right of the line, so it does not cover the line or the "N"?
2. Sky view's rings are elevation, 90° at the centre: the same placement
   reads 0° at the outer ring and up towards the centre. Same style
   there too?

### R7 — Reference position: no line between reference positions; the current one joined to the rover in red

**Screen:** Analysis → Reference position, pro.

**The author:** no connection line between the reference position in
use and the previous ones. The one in use is connected with a red line.

**Today:** the trail's dots are joined in order by one faint line ("a
hand-over reads as a jump in the dots", `HandoverDetail`'s comment), so
the line on the screenshot runs from the old reference position (pink)
to the new one (red), and passes near the rover only by chance.

**Open:**
1. Joined to the rover, at the centre? (Read so; to be confirmed.)

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

**Open:**
1. Colours: each reference position its own colour, and the same
   colour for its dot and its stretch of track? Then today's scheme,
   red for the one in use and pink for every earlier one, gives way to
   a colour per station. Is the one in use still red (R7's line), with
   the others in a set of distinct colours?
2. The centre: with a track, does the plot stay centred on the rover
   now, or on the area the track and the stations cover?
3. How long: the whole run, or a window like the phone's
   self-position trail (`CAP = 7200` points in `SelfPosition.kt`,
   about two hours at one a second)?
4. When the phone's fix does not move (a rover set by hand, *rover at a
   set position*), there is no track to draw: nothing, as now?

### R9 — Reference position: a small label at each reference position

**Screen:** Analysis → Reference position, pro.

**The author:** the mountpoints get a small label.

**Open:**
1. What the label says. On a network service the mountpoint is the
   same for every point (`NEAR` here); what changes is the station
   behind it. The core reads each one's station ID from 1005/1006
   (`ArpInfo.stationId`), so the label could be that ID, a letter in
   order (A, B, …), or the time it took over. Which?

### R10 — A change of base invalidates the plots: results from before and after are joined

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
| S4 | **Dropped where the findings address it.** What is left over is under S4 below, for the author. |
| S5 | **Open.** The author specifies it later. |

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
- *The READY bar says nothing* — no finding covers it. **Open.**
- *Run the check at the top, the rest below* — R4 orders the run
  buttons among themselves but leaves them below the results. **Open.**

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

## Implementation

*Planned once collection is closed.*

## Verification on the Huawei

*Planned once collection is closed.*
