# Measurement: KPI 9, and the stability report

*The fourth track, and the first that is not a product. The other three
follow an artefact — [release-to-play.md](release-to-play.md) the Android
app, [cli-track.md](cli-track.md) the CLI, [gui-track.md](gui-track.md)
the GUI. This one follows a capability across all of them, because a
measurement that means different things in different products is worth
less than one that means the same thing everywhere.*

## What & Why

[design/kpi-candidates.md](../kpi-candidates.md) studied four proposed
KPIs — cycle-slip rate, code and phase RMS, latency, sky visibility — and
found that only one of them is a KPI at all. The other three answer a
different question, and the answer was **two tiers**:

- **Tier 1**, the ninety-second acceptance check: *is this station fit
  now?* Bounded, in every product, and the reason a free verdict is worth
  a paid one.
- **Tier 2**, the stability report: *has it been fit, and is it staying
  that way?* Measured over hours, and not expressible in ninety seconds
  at any price.

This track builds both: the one addition tier 1 earns, and the tier that
does not exist yet.

## Current status

| Phase | What | State |
|---|---|---|
| 0 | Declared position versus broadcast ARP — a second tier-1 candidate | specified 2026-08-16, evidence-backed |
| 1 | Latency, and KPI 9 | specified; **the block has changed shape** — see below |
| 2 | The report skeleton, from metrics that already exist | **built 2026-08-16** |
| 2a–2e | The CLI, the stream clock, replay, the daemon, the GUI | **all built 2026-08-16/17** |
| 3 | Sky visibility as a number | after phase 2 |
| 4 | Cycle-slip rate from lock time | after phase 2 |
| 5 | Observable retention → multipath RMS **and** a RINEX writer | deferred by decision |

**Tier 2 is now in every desktop program.** The report is built in
`src/core/station_report.c`, shown by the CLI's `--report`, published by
the daemon as `<mountpoint>.report.json`, and drawn by the GUI's
Stability window. Android has tier 1 only.

**Phase 1's block has changed shape, not lifted.** It was "wait until
free clears closed testing", so that the published claim of *eight
checks* was not moving during review. The free edition
[went live on 2026-08-17](https://play.google.com/store/apps/details?id=nl.pe1mew.ntripanalyser.free),
so the question is no longer about review risk: adding a ninth check now
means updating a store listing and the five other surfaces that state
the number. That is a decision to take deliberately rather than a
condition to wait for.

---

## Phase 0 — Declared position versus broadcast ARP

Not part of the original study, and a stronger candidate than any of the
four it examined: **it earned its place twice in one afternoon.**

### The evidence

Two coordinate faults in one day on the same caster, both of which
**passed all eight KPIs with STATION OK**:

1. `RFSEE01`'s sourcetable longitude read `5.937061` where the station
   broadcasts `5.9855` — the same digits rotated, a typing slip that put
   the declared position **3.3 km** from the antenna. It had been there
   long enough that nobody knew.
2. Correcting it, HANESE's coordinates were pasted into RFSEE01's entry,
   moving its declared position **25 km** to another town. That also
   passed eight of eight.

Both were caught by reading the sourcetable by hand. Neither would have
been caught by the tool, and the tool's whole purpose is to catch exactly
this class of thing before a rover does.

### Why it belongs in tier 1

Unlike the four candidates in [kpi-candidates.md](../kpi-candidates.md),
this settles **instantly**. `--check` already fetches the sourcetable to
judge KPI 8, and the ARP arrives inside KPI 3's thirty-second allowance;
the moment both exist the comparison is a subtraction. No window, no
accumulation, no waiting.

### Feasibility: the field exists and nothing fills it

`NsStatsSnapshot.sourcetable_offset_m` is declared, documented as
"declared vs broadcast", and serialised into both the JSON and the CSV.
The only assignment in the tree is `ns_stats_init()` setting `NS_UNSET`.

**That is the third instance of this pattern in one day** — the ARP
fields, `latency_s`, and now this. The GUI does perform the comparison,
in its own code, leaving the shared field empty: the arithmetic exists,
it simply is not in the place every frontend reads. Filling it is most of
the work, and it is small.

### What the check must be careful about

- **A network mountpoint legitimately disagrees.** A VRS advertises a
  network centroid and computes a virtual station near the rover; the two
  can be tens of kilometres apart and both be correct. The project
  already classifies VRS mountpoints, and this check must not run on
  them — the same reasoning that made KPI 8 judge only the direction that
  misleads.
- **Neither number is authoritative.** The ARP is what a rover computes
  *with*; the sourcetable is what it chooses *by*. A disagreement says
  one of them is wrong without saying which, and the wording must say
  exactly that rather than accusing the station of being in the wrong
  place. Today the sourcetable was wrong; tomorrow it will be the
  receiver, which is the case still open on both these stations.
- **The threshold is a decision, not a constant to guess at.** A base's
  declared position is used to pick the nearest station, so tens of
  metres are harmless and a kilometre is not. It belongs in `src/core`
  with the other thresholds, and wants a warn level and a fail level
  rather than one line in the sand.

### What it costs

Two tier-1 candidates now exist — latency and this — so the published
claim moves from **eight checks to ten**, across the six surfaces
`tools/check_release.py` verifies. That is one documentation pass for
both, which argues for doing them together rather than a KPI 9 now and a
KPI 10 later, and for doing them after free clears closed testing for the
same reason latency waits.

## The unfilled fields — found by machine, 2026-08-16

After `latency_s` and `sourcetable_offset_m` were each found by accident,
`tools/check_release.py` gained a check that reads every field of
`NsStatsSnapshot` and asserts it is written somewhere outside
`ns_stats.c` — whose job is to declare, initialise and serialise, never
to measure. It found **seven**, not two.

| Field | What it promises | Where it stands |
|---|---|---|
| `latency_s` | corrections' age | phase 1 |
| `sourcetable_offset_m`, `sourcetable_pos_valid` | declared position against the broadcast ARP | phase 0 |
| `station_type` | physical base, VRS, or computed | the GUI classifies stations in its own code; the shared field is empty |
| `arp_drift_m`, `arp_moves` | a fixed base that moves mid-session | same: implemented in the GUI, never published |
| `frames_malformed` | frames rejected as malformed | **retired 2026-08-16 — see below** |

The first six share one shape: the measurement exists, in the GUI, in
GUI-private code, so the CLI, the daemon and Android publish `null` for
something the project already knows how to compute. Moving each into the
session layer is the same work as phase 0, and phase 0 should establish
the pattern for the rest.

### `frames_malformed` is worse than unfilled

`NS_BAD_MALFORMED` is **declared in `ntrip_session.h` and never emitted**.
Nothing raises it, so:

- `gui/gui_thread.c:527` has `case NS_BAD_MALFORMED:` that can never run,
  incrementing a counter that is always zero;
- `frames_malformed` in the snapshot is never written;
- the Munin plugin graphs *malformed frames* as a `DERIVE` series that
  can only ever be flat;
- `docs/service.md` documents it as one of the seven graph families a
  reader can watch.

**A monitoring signal that has never been able to move is worse than an
absent one**, because a flat zero reads as good news. And it is not an
oversight in the framer so much as a category that lost its meaning: the
framing state machine deliberately routes a bad preamble to "keep
hunting" (bytes between frames are legitimate) and a runt or implausible
length to `NS_BAD_LENGTH`, counted as a framing re-sync. There is nothing
left for "malformed" to mean.

**Decided 2026-08-16: retired.** The alternative was to give it a
producer, which would have meant inventing a distinction between
"malformed" and "re-synced" that the framer does not make and nobody had
asked for. Removed in one change:

| Where | What went |
|---|---|
| `src/session/ntrip_session.h` | the `NS_BAD_MALFORMED` enumerator |
| `src/core/ns_stats.h` | the `frames_malformed` field, and `NS_STATS_SCHEMA_VERSION` moved to **2** |
| `src/core/ns_stats.c` | its JSON key, its CSV column and the header naming it |
| `gui/gui_thread.c` | the `case` that could never run |
| `gui/gui_state.h`, `gui/gui_events.c` | the counter, its two resets, and the Stream Health row — the rows after it renumbered |
| `service/munin/ntrip_monitor` | the series, its value line and its stale-case placeholder |
| `docs/service.md`, `design/architecture.md`, `design/feature-matrix.md` | the descriptions that promised it |

**The schema version moved because a field was removed**, which is what
that counter is for: a Munin RRD, an installed phone build or an archived
CSV outlives any one release, and a consumer reading `schema_version: 2`
now knows the column is gone rather than finding it missing and guessing.

Frame integrity is now two numbers that can both move — CRC failures and
framing re-syncs — and the integrity graph means what it says.

`NsStatsSnapshot.latency_s` exists, is documented, is serialised to JSON
and CSV and is displayed on the Android tile. **Nothing computes it**;
the only assignment is `ns_stats_init()` setting `NS_UNSET`. Every
consumer has published "not measured" since the schema was written — the
same defect the ARP fields carried until a live run tripped over them.

Computing it is `msm_get_epoch()` against the system clock. The care is
in the time bases: milliseconds of week for GPS, Galileo and BeiDou but
of *day* for GLONASS; GPS running 18 s ahead of UTC and BeiDou 14 s
behind GPS; and the fact that **it measures the local clock as much as
the caster**, so a host without NTP would report its own drift and blame
the station.

**Why it is blocked.** "Eight checks" is a published claim in the wiki,
the store listing, the About blurb, the manifest and the free edition's
flags, and `tools/check_release.py` verifies the count on six surfaces.
Renaming it to nine while free is in closed testing edits a live listing
mid-review for no operational gain. It waits.

## Phase 2 — The report skeleton — **built**

`src/core/station_report.{h,c}`, fed from `NsStatsSnapshot`, which every
frontend already has and the daemon already writes once an interval. Six
metrics, none of them new: availability (reconnects per hour), frame
integrity (the *worst* CRC rate the window saw, because a mean hides a
bad ten minutes inside a good six hours), signal level (the *fall* in
mean C/N0 from the window's best, since an absolute level says more about
the site than the station), satellites held, ionosphere (on `iono.h`'s
own ROTI thresholds — space weather does not mean something different
here), and delivery rate.

`test/test_station_report.c` pins the three decisions that would
otherwise erode quietly, and each case is written as the decision rather
than as an assertion about a number:

- **"Not enough evidence yet" is a verdict.** Ninety seconds of data
  yields `INSUFFICIENT EVIDENCE`, not a grade — the mistake tier 1 made
  three times before its sustain window existed.
- **Live-only metrics are absent from a replay, not zero.** Built from a
  capture, availability reports *"not available from a capture — it holds
  no arrival times"*, and does not drag the roll-up down.
- **Windows are stream time.** The same snapshots at any replay speed
  produce a byte-identical report.

Two smaller properties are pinned with them: a 7 dB fall in C/N0 is
`UNSTABLE` even though the level it fell *to* is perfectly good, and a
stream carrying no C/N0 at all (MSM1–3) is left unjudged rather than
condemned, with the reason named.

**No frontend shows it yet**, which is deliberate — the skeleton exists
to prove the shape before new metrics are funded. Wiring it up is the
next step, and the CLI is the natural first consumer: it already has the
snapshot, a session clock and an offline replay path.

## Phase 2a — Show it somewhere — **built in the CLI**

`--report` rides on the modes that already run for a duration — `-t`,
`-s`, `-d`, `--check` — rather than becoming a mode of its own, because
tier 2 is a second reading of the same session rather than a different
activity. Snapshots are sampled once a second into an `SrState`, and the
report prints when the run ends.

Two decisions worth keeping:

- **It never changes the exit code.** `--check` owns that. Two verdicts
  competing for one exit status is how an automation surface becomes
  unusable, and a script that must distinguish them can read stdout.
- **It prints beneath `--check` too**, where it will almost always say
  `INSUFFICIENT EVIDENCE` — which is the point. Seeing the two tiers
  disagree about how much they know, in one output, teaches the
  distinction better than any wording could.

Next consumers, in order: the daemon, which runs for months and is the
natural home; then the GUI, for commissioning.

## Phase 2b — A stream clock, so a replay can be reported — **built**

`NsStatsSnapshot::stream_time_s` publishes elapsed time as the *data*
measures it, accumulated in `ntrip_session.c` at the point epochs are
already decoded, and the CLI now stamps every tier-2 sample with it
instead of seconds-since-start. Sampling stays on the wall clock, which
is a cadence and not a measurement; the timestamp is the stream's.

The raw material needed no new decoding — `msm_get_epoch()` has run on
every valid frame since epochs were counted, and works for the legacy
1001–1012 families too. What it needed was care, because an epoch field
is not a timestamp:

- **The constellations do not share a clock.** GPS, Galileo, QZSS, SBAS
  and NavIC count milliseconds of week; BeiDou counts the same week
  offset by fourteen seconds; GLONASS counts milliseconds of *day*, in
  Moscow time, packed as a 3-bit day above a 27-bit millisecond in the
  same 30-bit field. So the clock locks onto one constellation and
  ignores the rest. Accumulating across two would have added fourteen
  invented seconds at every GPS↔BeiDou alternation.
- **The field wraps** — a week at 604 800 000 ms, a GLONASS day at
  86 400 000 — and a six-hour capture started on a Saturday evening
  crosses the first. A negative delta larger than half the modulus is a
  rollover; a smaller one is a frame that arrived late, and adding a
  week to *that* would be a spectacular way to lie. This is the class of
  fault that once made a day-old GLONASS orbit read as an hour old.
- **A day-scale lock trades up** to a week-scale one when a week-based
  constellation appears, because fewer wraps is fewer chances to be
  wrong. It costs one inter-frame delta and happens at most once.

A **dropout is deliberately not** smoothed: a ten-minute gap advances
the clock ten minutes because the epochs on either side say so, and
counting it is exactly what makes a live run and its replay agree.

A stream carrying no observation epochs at all — 1005/1008/1033 and
nothing else — leaves the field `NS_UNSET`, and the report says it has
no window rather than falling back to the host. Silent fallback is how
replay equality would die without anyone noticing.

`test/test_stream_clock.c` builds frames with chosen epochs and replays
them, so a week boundary is constructed rather than waited for: ten
cases covering the rollovers, the mixed-constellation sum, the late
frame, an epoch split across frames, the dropout, and the stream with no
clock at all.

**It is the better clock live, too.** A host NTP correction steps
`uptime_s` sideways mid-session; epoch counting cannot be stepped.

That also makes the offline path the interesting one: a `.rtcm3` on disk
becomes a station's history, re-judgeable years later against thresholds
that did not exist when it was recorded.

## Phase 2c — Reporting over a capture — **built**

`--rtcm-stdin` now reaches `-d`, `-t` and `-s`, not only `--sky`, and
`cli_run()` opens stdin through `ns_open_stream()` instead of a socket.
Everything downstream is the live code path. `sr_reset(…, true)` marks
the run as a replay, so availability reads `n/a` rather than a clean
zero.

Wiring the flag was the small part. Three things were wrong underneath
it, and each was invisible until a capture was actually reported on:

- **Every mode but `--sky` ignored the flag.** `-t 600 --rtcm-stdin`
  opened a live connection to the configured caster and analysed *that*
  for ten minutes while the file it had been handed sat unread on stdin
  — a wrong answer delivered with total confidence. Unsupported modes
  now reject it, the way `--capture` already rejects modes that cannot
  capture.
- **Staleness was measured against the host clock.** `sv_track` and
  `iono` ask "how long ago was this seen", and the session passed wall
  time. Six hours of file arrive in milliseconds, so every satellite
  looked current and every epoch interval read 0.000 s — the
  message-type table was empty of meaning and the delivery-rate metric
  could not fail. `obs_clock()` now supplies arrival time live and the
  stream clock on a replay, in one place: **live behaviour is byte for
  byte what it was**, because there the two clocks are the same one.
- **The replay read 8 KB a pump**, and statistics are recomputed once a
  pump — so a station sending 1.6 KB an epoch was sampled every six
  seconds offline against once a second live. An analysis must not be
  coarser offline than live; the replay now reads a kilobyte at a time,
  and the same capture yields 88 samples where the live run yielded 89.

### A live report and its replay may legitimately differ

The same 120-second HANESE session: live reported 29 satellites at its
worst, the replay 38. Neither is wrong. The live run's message-type
table shows a 7.4 s arrival gap; the capture's epochs are consecutive at
1.000 s. The caster stalled and then delivered a burst, and for those
seven seconds the analyser could not see satellites that had not yet
arrived — while the station had not lost one.

So the property to claim is the one the test pins: *the same snapshots*
produce the same report at any speed. A live run additionally measures
delivery, which no file can hold — which is exactly why availability is
marked live-only, and is worth stating rather than leaving for a user to
discover by comparing two reports and doubting both.

## Phase 2d — The daemon publishes it — **built**

`ntrip-monitord` writes `<mountpoint>.report.json` beside the snapshot,
atomically, on the same interval. A second document rather than more
keys in the first: a snapshot is a point in time and a report is a
window over many of them, they have different lifetimes, and every
existing reader of the snapshot — the Munin plugin above all — keeps
working untouched. `sr_to_json()` lives in core, flat and single-line,
so the shell plugin can read it with the `scalar()` helper it already
has rather than becoming a JSON parser.

**The window has to roll here.** `SrState` keeps the worst value it has
ever seen, which is right for a run of an hour and worthless for a
process that runs for months: one bad afternoon in March would still be
the verdict in June, and the graph could never recover.

The daemon keeps **two staggered accumulators** rather than a ring of
samples. Slot 0 starts with the stream, slot 1 one window later, each is
retired and restarted at two windows, and the published report is always
the older — so it always holds between one and two windows, and never
goes blank at a boundary the way a tumbling window does. Two `SrState`s
and two timestamps.

`report_window_s` defaults to 3600 and is floored at `SR_MIN_WINDOW_S`:
a setting whose every value produces `INSUFFICIENT EVIDENCE` is a trap
rather than a choice.

The clock is `stream_time_s` throughout. A window measured against the
host would age while a station sat silent — counting an hour of silence
as an hour of health — and would be stepped sideways by an NTP
correction on a machine expected to run unattended for months.

### What publishing it found

The report was published every ten seconds from the moment a session
opened, and for the first thirty of those — the warm-up, when nothing
has been sampled — it said *"no C/N0 in this stream (MSM1-3)"* and *"no
dual-frequency pair to measure with"* about a station sending both.
Those are claims about the station, and an empty accumulator has no
grounds for either. Both now say "gathering" until something has been
sampled, with a case in `test_station_report.c`.

It only became visible because a file on disk shows what a terminal
scrolls past.

### Munin draws it — **built**

`ntrip_stability_<mount>`: the six verdicts on one 0–3 scale, GAUGE,
`warning 0:1` and `critical 0:2` so degraded warns and unstable pages
while insufficient evidence never does. Deployed to the monitoring host
and verified there under dash, against both live stations.

Adding it found a hazard the daemon had introduced two phases earlier.
The plugin discovers stations by globbing `*.json` and keeping whatever
carries a `schema_version` — so `<mount>.report.json` would have looked
like a snapshot to any plugin older than the daemon, and drawn a phantom
graph family per station, full of undefined values, on every host
upgraded in the wrong order. Fixed from both sides: the plugin skips
`*.report.json` explicitly, and the report's version key is
`report_schema_version`, so an old plugin skips it without knowing why.
The new plugin omits the family when no report file exists, so the other
order is safe too. **Both directions were tested** — the previous
plugin, taken from git, against a new-format report — rather than
reasoned about.

## Phase 2e — The GUI shows it — **built**

`View > Stability`, beside `View > Station Check`, and shaped by the
same rule that separates the two tiers: the check is a bounded run with
a Run button, and this is not a run at all. It accumulates for the life
of the session and says `INSUFFICIENT EVIDENCE` until it has ten minutes
— which is what commissioning wants, where the installer connects, works
on the antenna, and looks back at the verdict later.

`gui_report_window.c`, a sibling of `gui_check_window.c` down to the
custom-draw severity colours, so the two read as one family. The
accumulator lives in `AppState`, not in the window: an hour of evidence
must survive its window being closed, exactly as the check's sustain
clocks must.

`Restart window` exists for one moment in particular — you re-seat the
antenna and want the next hour judged on its own rather than averaged
with the hour that prompted the change. It keeps `reportFromCapture`, so
restarting a replay cannot quietly turn it into a live run and begin
inventing availability figures.

### Wiring it up found the replay path emitting nothing

The GUI's replay worker set `stats_interval_s = 0.0`, so `NS_EV_STATS`
never fired for a capture and the window would have stayed empty
for ever. Setting it to 1.0 was not enough on its own, either:
`maybe_emit_stats()` paced itself on the wall clock, so a six-hour
capture read in two seconds would have emitted **two** snapshots where
the live run emitted twenty-one thousand.

So the emit gate now runs on `obs_clock()` too — the same rule as
everything else this work touched: arrival time live, stream time on a
replay, and live behaviour byte for byte what it was because there the
two are one clock. That fixes the same defect for every future consumer
of `NS_EV_STATS`, not just this window.

Next: nothing on this track is blocked. KPI 9 (latency) and KPI 10
(position agreement) remain gated on the free edition clearing Play
closed testing, since "eight checks" is published on six surfaces.

**What it does not give is a date.** An epoch is a time *within* a week
or a day with no week number, so it measures spans, never instants.
Dating a capture absolutely needs the week from an ephemeris message or
the modified Julian day from 1013 — which is why `convbin -tr` remains
mandatory for a station that broadcasts neither, and why a station that
broadcasts either could have its capture stamped automatically. That is
a separate item on the CLI track.

Build the tier before funding new metrics for it. A first report needs
none of the four candidates: reconnects, CRC rate, C/N0 trend, ROTI,
per-type message rates and epoch gaps are **already in the snapshot the
daemon has published since it existed**. Aggregating those over a stated
window produces something real, and proves the shape is worth having.

What the skeleton must establish, because retrofitting any of it is
expensive:

- **Every value carries its window and its evidence.** "Slips per
  satellite-hour over 6 h" is a measurement; "slip rate: 0.3" is a
  rumour. The orbit badge already applies this discipline to placement.
- **A verdict that can say "not enough evidence yet"** and mean it. The
  report spends its first minutes in exactly that state.
- **Thresholds in `src/core`**, beside `kpi.h`, never in a frontend.
- **Per-metric replay derivability** — see the decision below.

## After the tiers shipped — three changes to the tiers themselves

Recorded here because each altered what a tier *means*, not merely where
it is shown.

### Frame integrity was redefined, twice, and the second time by measurement

As built, tier 2's frame integrity took the **maximum of the snapshot's
cumulative CRC rate**. Its own documentation said it reported the worst
rate rather than the average "because an average hides a bad ten minutes
inside a good six hours" — but the cumulative rate *is* an average, and
the extreme of a running mean fails in both directions:

- **The first seconds decide it.** Two errors against a small early
  denominator read 0.93 % and stayed the "worst" for the rest of the run,
  against a station that settled at 0.43 % and then ran clean.
- **Later damage is invisible.** Six hours in, fifty corrupted frames
  move a 130 000-frame denominator by 0.04 % — far below whatever was
  banked early, so the maximum never notices. The bad ten minutes the
  metric exists to catch was precisely what it could not see.

Both were observed on one live stream, in the same screenshot.

It is now the **share of frames that passed CRC**, over a window of
stream time: 100 % is clean and the figure falls as frames fail, which
is how the question is actually asked. Tier 1 reads the last 60 s — its
own sustain window, because a check must be able to change within the
run or the sustain clock is timing a number that can no longer move —
and tier 2 the last 600 s. One pair of thresholds serves both: 99.9 % to
warn, 99.0 % to fail, the same standard as tier 1's old one-error-in-a-
thousand.

The window has a cost, stated in `docs/thresholds.md` rather than
discovered: a bad *minute* inside a good ten is diluted tenfold, so it
is caught as `DEGRADED` rather than `UNSTABLE`.

**On the name**: this is deliberately not called FER. Consistent with
BER that would be Frame *Error* Rate — the complement of what is shown —
and in codec usage an erasure is a frame that never arrived, which here
is a dropout and is reported by availability and delivery rate instead.
Neither RTCM 10403.x nor NTRIP defines such a metric; the field's term
for the complement is *CRC error rate*, which is what the detail lines
say when they describe the failing side.

### Both tiers' thresholds became data

Every figure in `kpi.h`, `station_report.h` and `vrs_check.h` is now the
**default** of a policy the run carries, loadable from a file by the
CLI, the service and the GUI. This does not change any verdict by
itself — the test suite asserts that a report built with no policy is
identical to one built with the defaults — but it changes what a tier
*is*: a standard a user can disagree with, rather than a number the
program asserts. Design, the five decisions and the four build phases
are in [thresholds-track.md](thresholds-track.md).

Two consequences land inside this track:

- **The evidence rules are policy too** — `warmup_s`, `min_window_s`,
  `min_samples`, `sustain_s`. They are floored, because below them a
  verdict stops meaning anything, and the floors are refused rather than
  clamped.
- **Detail strings stopped quoting thresholds.** *"Fewer than 1 error per
  1000 frames"* and *"Corrections flowing within 10 s of the GGA"* were
  true only while the built-in numbers were in force.

### Every row shows the limit it was judged against

`KpiResult` and `SrMetric` carry `limit` and `limit_dir`, set where the
verdict is decided, formatted in core, and shown by the CLI and both GUI
windows. A verdict without the number behind it cannot be argued with,
and this is the half of that promise that does not depend on a user
loading anything.

It also states something no fixed string could: KPI 5's expectation is
the sum over the constellations a station streams, so a GPS+GLONASS base
reads `min 14` where a five-system one reads `min 29` — the number that
station was actually held to.

---

## Phase 3 — Sky visibility as a number

`SkyRenderSector` already holds `observed` and `expected` per sector,
filled from the orbit cache and the station's position. A coverage
percentage is arithmetic over a populated structure; the diagnostic form
is a per-azimuth horizon profile — *blocked from 040° to 110° below 25°*
tells the owner where the tree is. Must report its own evidence base: a
station with a stale RINEX has a small `expected` and would otherwise
look perfect.

## Phase 4 — Cycle-slip rate from lock time

`iono.c` already breaks phase arcs on slips and publishes `iono_slips`,
but those arcs serve ROTI: MSM6/7 only, dual-frequency only, GLONASS
excluded because FDMA has no fixed pair. That counts slips among the
satellites ROTI could use, not the station's slip rate.

The honest metric is the **lock-time indicator**, which every observation
family carries — four bits in MSM4/5, ten in MSM6/7, its own field in the
legacy messages. A lock time that decreases between epochs is a slip, per
signal per satellite, with no exclusions. `msm_layout()` already records
`lock_bits` and the C/N0 extractor navigates past the lock array to reach
C/N0, so the offsets are computed already: this is reading at a known
position, not new layout work in the riskiest file in the project.

## Phase 5 — Observable retention — deferred

The session layer retains no pseudorange and no carrier phase; `SvTrack`
keeps last-seen, C/N0 and a power sum, because that is all the eight
checks needed. That single fact blocks **both** multipath RMS and a RINEX
observation writer — the wall this project met from the other direction
when asked whether it could produce a `.obs` for a base-station
declaration.

One change unlocks both, and would remove the RTKLIB dependency from
[docs/base-declaration.md](../../docs/base-declaration.md) entirely.
Deferred by decision until phase 2 shows the tier earns its keep: it is
the largest change proposed here and it touches the session layer's
memory profile, which is what shaped the current design.

## Decisions

Taken 2026-08-16, from the study.

| Decision | Why |
|---|---|
| **Two tiers, not a longer KPI list** | Fitness now and stability over time are different questions. A verdict from 90 s of evidence about an hour-long quantity is worse than no verdict. |
| **Latency becomes KPI 9, after free clears review** | It is instantaneous, so it meets the bounded contract — the only candidate that does. The delay is about a live listing, not the code. |
| **Tier 2 is bounded by the platform on Android, not withheld** | Pro carries what fits inside a six-hour foreground service; the CLI, GUI and daemon go further. A ceiling Android imposes, not capability held back — this line withholds convenience, never protection or capability. Pro's proposition and listing stand unchanged. |
| **Skeleton before metrics** | The snapshot already carries enough for a real report. Proving the shape is cheaper than funding multipath first and discovering the tier was wrong. |
| **Tier 2 spans CLI, GUI and daemon** | The GUI's role is commissioning — an hour to decide whether a station is worth a week of the daemon's attention. |
| **Per-metric replay derivability, not a timestamped capture** | The report from a live stream must equal the report from a capture of the same bytes. Latency and reconnects cannot satisfy that — a capture holds no arrival times and a replay never drops. Rather than timestamp the capture (which would break the format's byte-identity) or add a sidecar (rejected when capture was specified), each metric states whether a replay can reproduce it, and equality is asserted over the derivable subset. |
| **Observable retention deferred, and paired** | Multipath RMS and the RINEX writer are one piece of work with two justifications. Neither alone was worth the memory profile; together they might be. |
| **Session-scoped windows, measured in stream time** | A wall-clock window makes a replayed report differ from the live one that recorded it, which kills replay equality. Stream time makes a capture replay identically at any speed. The daemon adds one rolling window because it runs for months. |
| **Tier 2 judges per metric, and rolls up in its own vocabulary** | Bare numbers push thresholds onto the reader; `STATION OK` in two tiers produces two verdicts for one station. STABLE / DEGRADED / UNSTABLE, plus INSUFFICIENT EVIDENCE as a first-class state. |

## The window: session-scoped, in stream time

Both open questions were settled 2026-08-16. This one first, because it
constrains everything the skeleton stores.

**The report is session-scoped by default.** Its window runs from the
first epoch to the last, and that is a fact about the run rather than a
setting: a capture converted offline reports "6 h 00 m" because that is
what the capture holds; the GUI commissioning an installation reports the
hour it has been watching; pro reports its watch session.

**The daemon additionally publishes a rolling window**, because it runs
for months and "since restart" stops meaning anything after the first
week — an average over forty days hides last night's outage completely.
One rolling window, configurable, an hour by default. The long view is
already Munin's job: it holds the RRD, and the daemon's role is to give
it something true every interval.

**Windows are measured in stream time, never wall-clock time.** This is
the part that is easy to get wrong and expensive to fix. A window defined
by the host's clock makes a replay produce different windows from the
live run that recorded it — a six-hour capture replayed in twenty seconds
would report a twenty-second window — and the replay-equality property
dies on the spot. Defined over the epochs in the data, a capture replayed
at any speed yields the identical report, which is what makes an archived
`.rtcm3` a durable record rather than a souvenir.

The live-only metrics are the stated exception, and they are already
marked as such: latency and reconnects have no meaning in a replay
because a capture holds no arrival times and a replay never drops.

## The verdict: yes, but not in tier 1's words

**Per-metric verdicts, against thresholds in `src/core`.** A report of
bare numbers pushes the judgement onto the reader, who then invents
thresholds of their own — which is exactly what keeping thresholds out of
the frontends exists to prevent. Every metric is judged, in one place,
for every product.

**A roll-up, with its own vocabulary.** Alerting needs something
boolean-shaped, and a person wants an answer rather than a table. But it
must not be `STATION OK`: two verdicts for one station, in the same
words, is a support question we would deserve. Tier 2 says **STABLE /
DEGRADED / UNSTABLE**, and **INSUFFICIENT EVIDENCE** while it is still
filling its window — a first-class state, not a placeholder.

**The two tiers can disagree, and that is not a contradiction.** A
station can be fit right now and have been unstable all week; it can also
have been stable for a month and be failing this minute. When the report
disagrees with the check, it says so explicitly and names the windows,
because a user who sees `STATION OK` beside `UNSTABLE` will otherwise
conclude that one of them is broken.

**The headline is the worst finding with its evidence**, not the word
alone: *"DEGRADED over 6 h — 3 reconnects, and median C/N0 fell 4 dB
between 02:00 and 03:00."* The word is for alerting; the sentence is what
makes it actionable.

## Open questions

None outstanding. The next decisions belong to phase 2's design.

## Outcome

**Tier 2 exists, in every desktop program, from one engine.** Built
2026-08-16 and 17: the report in `src/core/station_report.c`, the CLI's
`--report`, the daemon's `<mountpoint>.report.json` over a rolling
window, and the GUI's Stability window. Six measurements, none of them
new — every one derived from fields the snapshot already carried, which
was the point: prove the shape before funding new measurement.

What it cost, and what it found:

| | |
|---|---|
| New core modules | `station_report.{c,h}`, `thresholds.{c,h}` |
| New session capability | `stream_time_s` — elapsed time from the observation epochs |
| Tests | `test_station_report.c`, `test_stream_clock.c`, `test_ns_stats.c`, `test_policy.c` — the suite went from 6 to 10 |
| Release checks | 42 → 47: three that no snapshot field goes unfilled and untracked, two that every threshold is documented and every policy field settable |
| Defects found *by* building it | the warm-up minimum, two premature claims about a stream, a frame-integrity metric that could not see what it existed to catch, a replay path that emitted no statistics, a wall-clock emit gate, an invalid snapshot JSON, and a check that said `RUNNING` at the end of a finished run |

**The property that makes it worth having** is that a replay reproduces
a live run: the window comes from the stream's own clock, so a six-hour
capture read from disk in a fraction of a second is judged over six
hours. Verified against a real capture — 117 s of HANESE replayed in
2 ms reporting a 117-second window, and a live run and its replay both
reporting an 87 s window from the same session.

**What is still open**: phase 0 (declared position versus broadcast ARP,
specified and evidence-backed), phase 1 (latency and KPI 9, now a
listing decision rather than a wait), phases 3 and 4, and phase 5 which
was deferred by decision. Android has tier 1 only.
