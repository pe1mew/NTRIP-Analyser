# Rover-to-ARP distance and hand-over — plan

Phase 2, item 3 (`design/guiV2rollout.md`: Tracks → VRS → **hand-over**
→ export → tier 2 → TLS). **Pro only**, per feature-matrix row 56:
`◐ CLI | ● GUI | ○ Free | ⋯ Pro`.

**What it is.** How far the reference position is from where you
stand, live, with the history of where it has *been* — because a
network that computes its virtual station near you will move it, and a
network that hands you between physical stations moves it in jumps. A
jump mid-survey is precisely the event a surveyor wants to know about
and cannot see in a stream of corrections that keeps flowing as though
nothing happened.

It pairs with the network-RTK check the way watch pairs with the eight:
the check answers *what kind of service is this* in three minutes;
this answers *what is it doing to me over time*.

## What planning it found: two snapshot fields nothing fills

`NsStatsSnapshot` declares `arp_drift_m` and `arp_moves`, serialises
both in JSON and CSV — and **no code writes either**. The session
stores each 1005/1006's position and never compares two. Every JSON
document ever published has carried `"arp_drift_m": null,
"arp_moves": 0` as facts about nothing.

That is the promoted gotcha *a snapshot field nothing fills is worse
than a missing one*, found a fourth time, and it decides the first
step: before any UI, the core must make its own fields true. The
daemon's CSV and Munin plots get honest columns out of it for free.

## The desktop's rules are the rules

`gui_vrs_window.c` has drawn this since before either app existed;
`gui_state.h` and `gui_events.c` document the decisions:

| Rule | Value | Why |
|---|---|---|
| A "move" is | a new ARP > **10 m** from the last recorded one | GPS wander in a fixed ARP re-broadcast is millimetres; 10 m separates re-broadcast noise from an actual change |
| History kept | **32 positions** | A session that hands over 32 times has told you everything 33 would |
| Hand-overs | recorded positions − 1 | The first position is not a move |
| Distance readout colour | green < 5 km, amber < 50 km, red beyond | The same usability bands A3 judges by |
| Distance history | 300 s ring at 1 Hz | Five minutes of strip chart says "drifting" vs "jumped" |
| The verdict's words | by station type | *N hand-overs — expected for a network service* (info) vs *a fixed base should not move; corrections are unreliable* (bad): the same number, opposite meanings |

That last row is the heart of it: **movement is not a fault, movement
is a fact whose meaning the station type decides.** The wording ships
with the panel, not a judgement engine — the snapshot carries
`station_type` already.

## Division of labour

- **Core** (`ntrip_session.c`, where `arp_valid` is already set): track
  the last recorded ARP, fill `arp_moves` and `arp_drift_m` (from the
  *first* position of the run, as the header documents). The 10 m
  threshold becomes a named constant beside the fields it serves.
  Serves every frontend at once — the CLI's ◐ becomes honest, the
  daemon's CSV too.
- **The app**: distance and bearing rover→ARP are computed in Kotlin —
  it already holds both ends (`stats.arpLat/arpLon`, and the GGA
  position including the live fix, which never leaves the phone). The
  **ARP dot history** is a run-scoped accumulator beside
  `TrackAccumulator` in `MonitorService`'s companion — *a record of a
  run outlives the screen that draws it* (active decision, 2026-08-23).
- **Not in the bridge, not in JSON**: nothing here needs a C-side
  clock, and the fewer fields cross the boundary the fewer parity
  checks exist to keep them honest.

## Edition

`Features.HAS_HANDOVER` — pro true, free false. A `HandoverPanel` in
pro's registry, in the slot reserved beside VrsPanel. **The first
panel with its own screen**: the card drills into a `Dest` detail —
the polar plot with the accumulated dots and the strip chart — which
exercises the hub's `openDetail` path that nothing uses yet.

Free's check 3 keeps saying what it says today. The *fact* that an ARP
moved is measurement and `arp_moves` now travels to both editions in
the snapshot; whether free's check-3 evidence should quote it is left
as an open question below rather than smuggled in.

## Steps

### H1 — the core fills its own fields  *(done 2026-08-24)*

`ns_stats_note_arp()` in `ns_stats.c`, called from the session where
1005/1006 already lands; `NS_ARP_MOVE_M 10.0` beside it with the
desktop's reasoning. One decision the writing settled: a move is
judged against the last **recorded** position, not the last broadcast
-- a station creeping 9 m per message would otherwise never move at
all, however far it got -- and the test pins that with a creep case.
Eight assertions in `test_ns_stats`; falsified by tripling the
threshold (the 15 m jump stops counting), restored, 15 of 15 suite
tests. Both Android editions rebuild on the changed session.

*(As planned:)*

`ns_stats_note_arp()` (or inline where 1005/1006 lands): first
position remembered, drift from it maintained, a move counted past the
threshold. A test drives it with synthetic positions: no move at 5 m,
a move at 15 m, drift measured from the first, and the CSV/JSON
carrying the numbers. Falsify by halving the threshold in the test.

**Verify.** `test_ns_stats` (or a sibling) red/green; the daemon's CSV
header already names the columns, so nothing there changes.

### H2 — the accumulator and the Kotlin model  *(done 2026-08-24)*

`ArpTrail` beside the other two accumulators: dots past the 10 m rule
capped at 32, the 300-slot distance ring, all `@Synchronized`,
run-scoped in the service's companion and fed at the publish. The
rover end of each distance sample follows the GGA uplink's own order:
the live fix where consent was given, the configured position
otherwise -- and the fix never leaves the phone. `Stats` gained
`arpDriftM`/`arpMoves`.

The planning claim about `check_snapshot_fields` was **wrong in an
instructive way**: the check would not have gone red, because the fill
lives in `ns_stats.c` -- the one file its search excludes, since the
serialisers there name every field. The two entries would have sat on
the known-gaps list forever, "tracked" and filled. The checker now
reads `ns_stats_note_arp`'s body back in past the exclusion, the two
entries are retired, and the gate guard demanded `HAS_HANDOVER` in the
matrix before going green. 86 checks.

*(As planned:)*

`ArpTrail` beside the other two in `MonitorService`'s companion:
(lat, lon) recorded past the same 10 m rule, capped at 32, cleared at
run start, fed where the document is published. `Stats` gains
`arpDriftM`/`arpMoves` (nullable/default, tolerant decode). A 300-slot
distance ring for the strip chart, fed at the 1 Hz publish.

**Verify.** Unit-testable in Kotlin? No test harness exists for the
app — verify on device in H4, and by the parity check in H5.

### H3 — the card

`HandoverPanel` in pro's registry beside `VrsPanel`: live distance and
bearing in the usability colour, the hand-over count in the station
type's own words (the desktop's two sentences), and the affordance
mark saying it leads somewhere. Share section: distance, moves, worst
jump — the sign-off numbers.

### H4 — the screen

The `Dest` drill-in, first of its kind — and **laid out as an analysis
screen, using the analysis screens as its template** (author's
direction, 2026-08-24). Concretely: the same `AppScaffold` frame, and
the six fixed bands of `guiV3spec.md` §4 through the shared
`AnalysisBands`/`PlotLayout` composables rather than a layout of its
own — explainer, summary, plot, footer, legend, in that order and no
other. The bands fill as: summary = distance, bearing and the
hand-over count in the station type's words; plot = the polar plot
(rover centred, ARP at true bearing, scaled radius, history dots
joined faintly in order) with the five-minute strip chart beneath it;
footer = the mountpoint and the rover position in use; legend = what a
dot, the rover mark and the line mean.

Reusing `PlotLayout` is not just consistency: it is what already
solved landscape — a plot squeezed into what the words left over is
how the sky view became a dot — so this screen inherits that fix
instead of rediscovering it. Verified on a real network mountpoint if one is
available; otherwise on RFSEE01, where the honest picture is one dot
and a flat line — and the words must say that is *good*.

### H5 — the guards

`HAS_HANDOVER` documented in the matrix gate table; row 56 Pro ⋯ → ●;
`check_snapshot_fields` already compares C and Kotlin, so the two new
`Stats` fields must land on both sides or it goes red — that is the
guard working, not a step to perform. The 10 m threshold quoted
anywhere (wiki, matrix) gets a parity line beside the five-assertions
count.

### H6 — say so

Changelog under `[Unreleased]`. Wiki: a **Hand-over** section on the
[Network-RTK check](Network-RTK-check) page rather than a new page —
one page per subject, and the subject is *reading a network service*;
the sidebar entry stays one. Check 3's network paragraph gains the
pointer. The Pro pages' tables gain the row.

## Open, and worth an answer before H3

1. **Should free's check-3 evidence quote `arp_moves`?** It is
   measurement, and the desktop shows its stability line to everyone —
   but free's check 3 has never mentioned movement, and adding it is a
   free-edition change during the freeze. Recommendation: not now;
   note it for the TLS release, which unfreezes free anyway.
2. **Bearing as degrees, compass point, or arrow?** The desktop draws
   an arrow on the polar plot and prints nothing. A card wants text;
   recommendation: compass point + km ("4.2 km NE"), degrees in the
   detail screen.
3. **Does the strip chart earn H4, or does the polar plot alone carry
   it?** The desktop has both; a phone screen is smaller.
   Recommendation: both, stacked in the band template — the chart is
   what distinguishes drift from a jump at a glance.
