# Satellite tracks on the phone — plan

Phase 2, item 1 ([guiV2rollout.md](../guiV2rollout.md)), the first paid
capability built on the v3 template. Pro only.

**What it is.** Where a satellite has been, drawn behind where it is: a
trail per SV across the sky plot, so a reader sees arcs rather than a
scatter of dots. It answers a question the current plot cannot — *is
this station's horizon clear, or does something cut satellites off at 20°
in the south-west?* — because one epoch shows a gap and a session shows
a shadow.

## The desktop already does this, and its rules are the rules

`gui/gui_sky_window.c` has drawn trails since before either app existed,
and `gui/gui_state.h` documents every decision behind them. Android
matches rather than invents, so that a screenshot of one product means
the same as a screenshot of the other:

| Rule | Value | Why it is that value |
|---|---|---|
| Sampling interval | **60 s** | At GLONASS speed a 60 s step lands ~7 px apart on an 800 px plot, which reads as continuous |
| Split a run when samples are apart by | **300 s** | A satellite that sets and rises again is two arcs, not a chord across the plot |
| Split on azimuth wrap | 359° → 1° | Same reason: no long chord across north |
| Trail colour | the constellation hue, lightened **30 % toward white** | History should not compete with the live marker |
| A dot at each sample, line between | yes | An ephemeris-update step then shows as a straight line between two visibly spaced dots, rather than hiding in a smooth curve |

**One rule deliberately differs.** The desktop keeps 1440 points per SV —
24 hours, 11.3 MB. A phone gets **240 points, four hours**, about 300 kB
across every slot. Four hours covers a watch-mode session; a desktop
running for a day is a different kind of instrument.

## Where the history lives: in the app

Not in the core, and not across the bridge. Two precedents say so:
`ElevationAccumulator` already accumulates a session's C/N0-against-
elevation samples in Kotlin, and the GUI accumulates its trails in its
own state. A trail is not a measurement — it is a record of positions the
core has already computed, kept for as long as one screen wants to draw
them.

That also keeps this change small: no C, no JNI, no snapshot field.

## Edition

`Features.HAS_TRACKS` — `true` in pro, `false` in free. Free's sky view
draws exactly what it draws today.

Not a `Panel`, so not a registry entry: tracks are *inside* the sky
canvas, which is what `guiV3spec.md` §6 says a capability of this shape
does. It is the first test of that claim.

## Steps

### T1 — the accumulator

`TrackAccumulator` beside `ElevationAccumulator`: per `(gnss, prn)`, a
ring of `(azDeg, elDeg, tSeconds)`, one sample per 60 s per SV, capped at
240. Fed where the sky's satellites are read, so it sees exactly what the
plot draws.

**Verify.** A run of a few minutes leaves points for the satellites on
screen and none for anything else; the count per SV stops growing at the
cap.

### T2 — drawing

`SkyCanvas` takes the accumulator and draws each SV's trail under the
markers, by the table above. Nothing is drawn when `HAS_TRACKS` is off.

**Verify.** Pro after a few minutes: arcs behind the dots, in the
constellation's own hue, broken where a satellite set and rose again.
Free: pixel-identical to today.

### T3 — the edition holds

`checkEditionParity`, `check_release.py`, and a look at free's sky view
to confirm nothing paid leaked into it.

### T4 — say so

`changelog.md`, the feature matrix row (⋯ → ●), and
`docs/wiki/The-analysis-views.md`, which describes the sky view to
readers of both editions and must say which one draws trails.

## Decided

1. **No fade with age.** The trail is one colour, the constellation's
   hue lightened 30 % toward white, exactly as the desktop draws it. A
   fade would read slightly better on a small screen and would be one
   more difference to explain between two products whose screenshots are
   meant to mean the same thing.
2. **Free sees nothing.** No greyed arc as an advertisement: the rule so
   far is that free's screens never show a disabled paid control, and
   *More in Pro* is where the capability is named instead.

## What 3.7.1 shipped, and what a night with it showed

T1-T4 are in, and a forty-minute run draws exactly what the plan asked
for: arcs behind the markers, in the constellation's own hue. The arcs
are confirmed.

A nine-hour run is not. Three faults, reported 2026-08-23 from two
captures of the same station:

1. **Nine hours produced minutes of track.** The elevation scatter told
   the same story from the other side: 25 412 samples where nine hours
   at forty satellites a second is millions.
2. **Rotating the phone reset every analysis screen** -- tracks, scatter
   and all -- and started from zero.
3. The trails are drawn **thicker than they need to be**.

### One cause under the first two

The run lives in `MonitorService`'s companion, process-scoped, and
survives anything the activity does. The accumulators do not: both are
`remember { }` inside the composition, and `LaunchedEffect(running)`
clears them whenever it re-enters. So a rotation destroys them, and the
clear then wipes what a fresh one might have held.

Worse for the long run: the document is collected with
`collectAsStateWithLifecycle`, which stops at STOPPED. With the screen
off **no document reaches the UI at all**, so nothing accumulates. A
nine-hour capture accumulated the minutes its screen happened to be on,
which is what both plots were honestly showing.

The mistake was putting a record of the run in the thing that draws it.
The C/N0 scatter has had it since it was written; tracks inherited it by
copying the precedent, which is how a wrong precedent spreads.

## Steps

### T5 -- accumulate where the run lives

Both accumulators move to `MonitorService`'s companion, cleared when a
run starts rather than when a composition re-enters, and fed where the
document is published (`MonitorService.kt:203`) rather than where it is
drawn. The service decodes with the screen off, so accumulation
continues with the screen off.

Satellites the orbits place are the service's to record. Those only the
phone can place stay with the UI, which is the only side that has the
handset's positions -- one satellite, one source, so nothing is counted
twice. Both accumulators become synchronised, because two threads now
reach them.

**Verify.** Rotate on each of the three analysis screens: the sample
count and the arcs are unchanged. Screen off for ten minutes: the count
has risen when it comes back.

### T6 -- a long run is a long track

The cap goes from 240 points to **1440**, the desktop's own number: a
day per satellite, so a nine-hour capture is nine hours of arc. Drawing
57 000 points a frame is not free, so the runs are built once per
revision -- once a minute -- instead of on every frame.

Thinner lines while there: the trail was `markerR * 0.35`, near enough
the marker's own weight to compete with it.

**Verify.** A long run draws to its start; a rotation mid-run keeps it.

## What the verification showed (2026-08-23, Huawei SNE-LX1)

Read off the C/N0 view's own sample counter, which is the accumulation
made visible:

| Step | Samples | |
|---|---|---|
| Watch run, 60 s | 2 385 | ~40/s, one document a second across ~40 satellites |
| Rotated to landscape | 1 786 -> 2 947 | rose across the rotation; before this change it restarted at 0 |
| Backgrounded 150 s | 2 385 -> 9 121 | ~45/s while off screen, where nothing at all was recorded before |

The trail dots are visibly finer than the markers they sit behind.

**A trial is running.** Started 2026-08-23 15:0x on the Huawei
(SNE-LX1, Android 10, so no six-hour ceiling), pro 3.7.1 from `cd78871`,
watch mode on HANESE with placement from the ephemeris stream. What to
read from it: the elapsed line, the sample count on C/N0 against
elevation -- about 1.3 million if the service keeps 40/s through the
night -- and whether the arcs run back the whole session. **Do not
install anything on that handset while it runs.**

**One thing is not proven here.** Those figures are with the app off
screen but the *screen on*. With the screen off the CPU may suspend
between packets, and a foreground service holds no wake lock -- the
service pumps every 200 ms, and nothing keeps it running against a
suspend. A stream arriving continuously wakes it often, so it may not
matter; the next overnight run is what says. If that run comes back
sparse, a partial wake lock held for the length of a watch run is the
lever, and the cost is battery on a phone that is usually charging
while it does this.
