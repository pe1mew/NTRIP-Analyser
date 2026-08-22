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

## What is built, and what is still to see

T1, T2 and T3 are in: the accumulator, the drawing, the edition gate,
and the docs. Both editions build, `checkEditionParity` passes, and
`check_release.py` is at 71 checks -- it gained one when it caught
`HAS_TRACKS` arriving undocumented.

**Points are confirmed on the device; arcs are not yet.** A trail draws
its dots as soon as there are two, and those are visible inside the live
markers -- but a satellite moves only a few pixels in the minutes a test
run lasts, so the *line* between them has nothing to span. Shortening
the interval does not help: closer samples are closer dots. The line
needs a run long enough for a satellite to travel, which is the next
thing to look at, on a capture rather than by shortening the clock.
