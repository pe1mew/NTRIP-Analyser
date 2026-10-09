# The station's own position — a solution computed from the station's own data

Two panels on Onocoy's station dashboard prompted this: **antenna
position** as a north/east/up offset, and **antenna velocity** in
millimetres per second, both with a satellite count beside them
(`design/kpi-candidates.md`, *Prior art*, 2026-10-02). The author wants
them, **first in the Windows GUI, then in the paid Android edition**.

This item specifies what they are, what it costs to compute them
honestly, and the one decision that has to be taken before the second
half can ship.

## What these two numbers actually measure

Onocoy's example reads **N 1.26, E 0.90, U 0.13 m** against a station
whose position is known to millimetres. That is not an antenna that
moved a metre. It is the **error of a single-point solution computed
from broadcast ephemeris**, which is metre-level by construction. The
information is in the **scatter and the drift over time**, never in the
absolute offset of one epoch.

The velocity panel is the sharper instrument. A static base must read
zero; the 0.0 / −0.1 / 0.4 mm/s shown is the noise floor of the solve.
A sustained non-zero reading means an antenna that is moving, or a
solution that is broken — and either is worth knowing.

So the metric this item adds is: **how well does this station's own
data place itself, and does it sit still?**

**It does not duplicate the ARP checks.** KPI 3, the sourcetable-versus
-ARP comparison and pro's reference-position watch all catch *a station
claiming a position it is not at*. This catches the complementary
fault: *an antenna that moved while the station kept broadcasting the
coordinates it was surveyed at*. Neither sees the other's failure.

## The gate: it contradicts a published sentence

Both Play listings say, in text that is live for free and in review for
pro:

> It does not steer a rover and it does not compute a position.

Computing a position is precisely what this item does. The sentence
would have to change, deliberately, on both listings — and
`docs/base-declaration.md` separately chooses to send RINEX to
**CSRS-PPP** rather than solve in-house, which stays the right answer
for *surveying a base*. This item does not touch that: a metre-level
broadcast solution is a quality metric, not a coordinate.

**Nothing here may change either listing while pro is in review.** Pro's
fortnight ends about 2026-10-12 (`pro-to-play.md` S4).

**Decided 2026-10-09 (author): the desktop first, and no listing
change.** P1–P6 ship in the GUI, the CLI and the daemon, which carry no
store text, so nothing published becomes false. P7, the paid Android
panel, waits behind a deliberate decision about that sentence, taken
when pro is in production and not before. The work is nevertheless
**built for the phone from the first commit** — decision 8.

## The decisions this plan adds (open until the author confirms)

1. **Static stations only.** One receiver, no rover, no baseline, no
   ambiguity resolution. The solve answers a question about a base.
2. **Broadcast ephemeris only** — no precise products, no SSR, no
   network. Expected accuracy is one to two metres horizontally, and
   **the report says so beside the number**, so nobody reads the offset
   as a survey result.
3. **Dual-frequency, iono-free, or nothing.** A single-frequency
   station gets an explicit *not computable* rather than a number
   biased by tens of metres of ionosphere. Tier 2 already has the
   vocabulary for this: INSUFFICIENT EVIDENCE is a real verdict.
4. **Velocity from the phase range rate** already decoded in MSM7,
   not from time-differenced carrier phase. The data is there; the
   simpler estimator is the one that can be checked.
5. **Thresholds in the core**, beside `kpi.h`, never in a frontend —
   the project's standing rule, and this item is no exception.
6. **The reference for north/east/up is the broadcast ARP** (1005/1006),
   falling back to the sourcetable position, and **the report states
   which was used**. An offset against an unstated reference is a
   rumour.
7. **A tier-2 monitor metric, not a ninth KPI.** It needs minutes to
   hours of evidence, so it does not join the ninety-second check and
   **the "eight checks" count is untouched** — the constraint
   `kpi-candidates.md` is firmest about.
8. **Built for the phone from the first commit, shipped there later**
   (author, 2026-10-09). Shipping the desktop first must not mean
   porting afterwards, so every step obeys the constraints the NDK
   build imposes, and P7 becomes a panel plus a list entry rather than
   a second implementation:

   - **The solver is core C99** in `src/core/spp.{c,h}`: no I/O, no
     platform headers, no threads, no allocation the core does not
     already do. The architecture rule — one measurement core, four
     frontends — is the portability plan; there is no second one.
   - **Results reach every frontend through the snapshot**
     (`ns_stats`), never through a GUI-side calculation. A number the
     Stability window can show that the bridge cannot is a number that
     will be reimplemented on the phone.
   - **Fields are absent until computed, never zero-filled.** The
     snapshot's own rule: a field nothing fills is worse than a
     missing one, so the Kotlin model takes them as nullable when P7
     comes.
   - **`src/core/spp.c` joins both source lists in the same commit** —
     `CMakeLists.txt` *and*
     `android/app/src/main/cpp/CMakeLists.txt`, which names each core
     file by hand. CI builds both editions on every push, so a file
     added to one list and not the other is caught the day it is
     written rather than at P7. This is the project's oldest
     two-build-systems trap, and the only defence is the same commit.
   - **Bounded and cheap enough for a handset**: one epoch of
     observables, a few hundred operations per satellite, no history
     beyond what tier 2 already keeps. If any step needs unbounded
     memory or heavy iteration, that is a design fault to fix on the
     desktop, not a phone problem to discover later.
9. **The word is "self-position"** (author, 2026-10-09), everywhere:
   the GUI window, the report fields, the snapshot keys and the
   documentation. Not "antenna position" — that is Onocoy's label for
   a quantity measured differently, and borrowing it would promise
   their numbers. The velocity half is the *self-position velocity*.
   A name chosen once here is a name `check_release.py` can hold the
   surfaces to later.

## What already exists, so the estimate is honest

- **The observables are decoded.** The MSM7 path reads rough range,
  fine pseudorange, fine phase and **fine phase rate**
  (`src/core/rtcm3x_parser.c`). They are parsed and then dropped.
- **Orbits are solved for already.** `SvEphemeris` carries the whole
  Keplerian set *and* `af0/af1/af2`; `sv_orbit.c` propagates GPS and
  Galileo by Kepler and GLONASS by numerical integration, for the sky
  plot.
- **The geometry is there**: `ecef_to_geodetic`, `geodetic_to_ecef`,
  `ecef_to_enu`, `azel_from_ecef`.
- **Capture and replay** exist, and with them the property that makes
  any of this testable offline.
- **Tier 2 exists** — `src/core/station_report.c`, the CLI's `--report`,
  the daemon's `<mountpoint>.report.json`, the GUI's Stability window.

What is missing: retention of the observables, satellite clock and
group-delay handling, a troposphere model, Earth-rotation correction,
two least-squares solves, the report fields, and the two frontends.

## Steps

### P1 — the observables survive the epoch  *(done 2026-10-09)*

Phase 5 of `measurement-tiers.md`, deferred there by decision and
unblocked here. A bounded per-epoch observation set owned by the
session: satellite, signal, pseudorange, carrier phase, phase rate,
lock, C/N0. Bounded by construction — one epoch, not a history.

**What reading the parser showed, before a line is written
(2026-10-09):**

- **The numbers already exist, and are printed and dropped.**
  `decode_rtcm_msm7_full` computes `pr_m`, `ph_m` and `phrate_ms` in
  metres and sends them to the message-detail text
  (`rtcm3x_parser.c:1304`). This is the largest instance yet of the
  parsed-field-nobody-consumed pattern, and it means P1 is wiring, not
  decoding.
- **The printed pseudorange is only the *fine* part.** The full range
  is the satellite-level rough range (`rough_range_int` plus
  `rough_range_mod/1024`, in milliseconds) combined with the cell-level
  fine value. Retaining the printed column alone would be wrong by
  roughly twenty thousand kilometres and would still look like a
  plausible float. **The test asserts an absolute range in the
  18 000–27 000 km band**, which no fine-only bug can satisfy.
- **MSM7 is not the whole population.** MSM7 has a full decoder and
  MSM4 has `decode_rtcm_msm4_generic`; MSM5 and MSM6 are decoded for
  C/N0 but not for observables. So P1 must state which message types
  it retains from, and a station outside that set gets *not
  computable* for the same reason a single-frequency one does
  (decision 3). Doppler is the sharper limit: **MSM5 and MSM7 carry
  phase rate, MSM4, MSM6 and legacy 1004/1012 do not**, so those
  stations can have a self-position and no velocity — which the
  report must say rather than show zero.

**Verify.** The existing suite green unchanged; a capture replays to a
byte-identical report; memory per epoch measured and stated, not
estimated. A new test builds MSM frames whose ranges are known by
construction, as `test_msm_cnr.c` already does for C/N0, and asserts
the retained values including the rough-plus-fine combination.

**Built 2026-10-09.** `src/core/ns_obs.{c,h}` holds one epoch;
`msm_extract_obs` in the parser reads a frame's cells as the sibling of
`msm_extract_cnr`; the session accumulates across frames and closes the
set on the multiple-message bit. `ns_obs.c` went into both source lists
in the same commit, as decision 8 requires.

*Measured, not estimated:* a cell is **40 bytes**, the set **7 704**,
one instance per session. The whole build is warning-free and the suite
is **17 of 17**, the new `observables` test included.

*Falsified before being believed.* Dropping the rough range makes every
pseudorange 2.2 m and the band assertion fires; keeping the rough range
but using the detail decoder's `0.0001` scale passes the band and fails
the exact value by 3.7 km. The second is why the test asserts both.

**Three things P1 found that the plan had not:**

- **The message-detail decoder's metre conversions are wrong.** It
  applies the phase rate's `0.0001` to pseudorange and phase as well,
  where RTCM gives 2^-29 ms and 2^-31 ms for the extended fields:
  **5.6x out on pseudorange, 1.4x on phase**, in the GUI's detail view
  and the CLI's verbose output since they were written. Nobody saw it
  because the measurement path never read those columns. The new
  extractor has its own correct scales; **fixing the display decoder is
  a separate change** and is not part of this branch.
- **`test_all`'s dependency list is hand-maintained**, so a new test
  executable is not rebuilt before `ctest` runs — which let a stale
  binary report a pass during this very step. `test_observables` has
  been added to the list; the next test will hit the same trap.
- **The epoch is not comparable across constellations**: GPS counts
  milliseconds of week, GLONASS milliseconds of day. The session
  therefore closes a set on *difference*, never on arithmetic between
  two epoch values.

### P2 — satellite position, velocity and clock at transmit time  *(done 2026-10-09)*

Signal transmit time from the pseudorange, satellite clock from
`af0/af1/af2` with the relativistic term, group delay applied as the
chosen combination requires, Earth rotation during flight. Velocity by
differentiating the propagator — numerically, at ±0.5 s, unless the
analytic form proves cheaper.

**Verify.** Satellite positions compared against RTKLIB's for the same
epoch and the same navigation data, with a stated tolerance; a sanity
assertion on orbital radius and speed per constellation. This step is
checkable on its own, and it is the one everything downstream inherits.

**Built 2026-10-09** as `src/core/sv_state.{c,h}`: `sv_state_at` returns
position, velocity and clock, and `sv_state_derotate` turns a transmit-
time position into the frame the measurement was made in. Velocity is
the central difference of `sv_to_ecef` at ±0.5 s — the propagator the
sky plot already trusts, rather than a second implementation of the same
orbit that could disagree with it. The relativistic correction is
`-2 (r·v) / c²`, which needs nothing the propagator does not already
return; group delay is deliberately absent, because the broadcast clock
refers to the iono-free combination P3 will use.

*Verified against RTKLIB, 2026-10-09.* RTKLIB 2.4.3 b34 was built from
source at `C:\Apps\rtklib` and `test/manual/sv_state_vs_rtklib.c` feeds
the *same* ephemeris to `eph2pos` and to `sv_state_at` across a ±2 hour
fit interval: **position agrees to 0.0000 m, clock to 0.04 ns** (1.2 cm
— the residue of taking the relativistic term as `-2(r·v)/c²` where
RTKLIB takes `-2√(μa)·e·sin E/c²`, which are the same quantity by
different routes). The harness is a bench tool, not a suite member:
RTKLIB is not a dependency and the CI runner has none. `docs/RUNBOOK.md`
carries the build line.

*The first run of that comparison disagreed by 1 800 km, and the
harness was wrong, not the propagator* — RTKLIB keeps `toe` twice, as a
time and as `toes` in seconds of week, and the longitude-of-node term
uses the second. Zero there drops `-ωₑ·toe` and rotates the orbit.

What runs in the suite, needing no RTKLIB, are checks that do not share
the propagator's arithmetic: **vis-viva**
(|v|² = μ(2/r − 1/a)) and **angular momentum** (|r×v| = √(μa(1−e²))),
which constrain the velocity's size *and* direction; radius and speed
bands per constellation; the relativistic term's magnitude in
nanoseconds; and the Earth rotation against ωrt computed in the test.
Falsified two ways: removing the relativistic term reddens three checks,
reversing the rotation direction reddens the one that names it.

**Two things P2 found:**

- **GLONASS had an orbit and no time.** `tau_n` and `gamma_n` are
  decoded by the 1020 path and were discarded, and the RINEX loader
  took them as arguments and `(void)`-ed them. Both now store them, with
  the sign convention written down: RTCM carries *TauN*, RINEX carries
  *−TauN*. Without this, a GLONASS satellite would have entered P3's
  solve with a clock of exactly zero — the worst kind of wrong, because
  it looks like an answer.
- **The velocity is Earth-fixed, not inertial.** Differencing ECEF
  positions gives 2.9 km/s for a GPS satellite where the inertial speed
  is 3.9; the difference is ω×r. That is the right quantity for a
  velocity solve, and the same thing RTKLIB produces, but the first run
  of the test failed vis-viva by 25% because the *test* assumed inertial.
  The header now says which frame it returns, and the test converts
  before applying inertial laws — so getting the frame wrong later fails
  loudly rather than quietly.

### P3 — the position solve

Iono-free combination, Saastamoinen troposphere with a standard
atmosphere, elevation weighting, least squares over the epoch, with
the receiver clock as the fourth unknown. Output: the ECEF position,
the north/east/up offset against the reference of decision 6, the
number of satellites used, and the **residual RMS of code and of
phase**.

Those last two are what Onocoy labels *code RMS* and *phase RMS*. The
report must say they are **solve residuals**, because the same words
mean the multipath combination in `kpi-candidates.md` §4. One label,
two quantities, and this is where they would be confused.

**Verify.** The same capture converted with `convbin` and solved by
RTKLIB's `rnx2rtkp` in single-point mode; agreement within a stated
tolerance, per epoch and in the mean. The route is proven —
`cli-track.md` V5 already took a capture through `convbin` to a
CSRS-PPP solution.

**Built 2026-10-09** as `src/core/spp.{c,h}`: iono-free combination of
the two widest carriers, Saastamoinen troposphere on a standard
atmosphere, a ten-degree elevation mask, and least squares with the
receiver clock as the fourth unknown. Refusals are named rather than
approximated — `SPP_SINGLE_FREQ`, `SPP_NO_EPHEMERIS`,
`SPP_TOO_FEW_SATS`, `SPP_DIVERGED` — and GLONASS stays out by design
even though P2 gave it a clock, because FDMA inter-frequency biases
will not fit under one receiver-clock unknown. The frequency table
moved from `static` in `iono.c` to a declared `msm_signal_freq_hz`,
one table for the ionosphere and the solver both.

*Verified by closure, not by bounds.* `test_spp.c` builds the
pseudoranges a receiver at a known point would measure — geometry,
satellite clocks, troposphere, a receiver clock bias — inverts them,
and demands the point back: **0.0000 m, clock exact, residuals
0.0000**, and the same from a cold start at the Earth's centre. The
troposphere is implemented a second time in the test, so the solver's
copy is compared rather than trusted. Falsified twice: flipping the
satellite-clock sign throws the answer 6 730 m, removing the
troposphere 4.7 m.

**The closure test caught a real defect before any data did.** The
first run recovered to 0.14 m with 2 cm residuals on noiseless input,
which should be exact. A term-by-term diagnostic showed the transmit
time 101 µs early: **a pseudorange carries the satellite's own clock
offset** — af0 alone is routinely 100 µs, or 30 km — so `tow − P/c`
is not the transmit time, and the satellite has moved 0.29 m by the
time it is. The solver now reads the clock, subtracts it, and reads
the state again. A second pass then re-reads every satellite with the
receiver clock the first pass found, which is what makes the answer
independent of how badly that clock is steered: a **1 ms** receiver
bias — 300 km of range — now costs **0.0000 m** of position, where one
pass cost metres.

*Still outstanding, and P3 is not closed without it:* the comparison
against `rnx2rtkp` on a **real capture**. Closure proves the solver
inverts its own model; only real data proves the model. RTKLIB is
installed and `docs/RUNBOOK.md` carries the route.

### P4 — the velocity solve  *(done 2026-10-09)*

Phase range rate against satellite velocity, receiver clock drift as
the fourth unknown, the same weighting.

**Verify.** A static station reads zero within the noise, and the
threshold for "zero" is derived from measured scatter rather than
chosen. Cross-checked against `rnx2rtkp`'s Doppler velocity on the same
capture.

**Built 2026-10-09**, inside `spp_solve` rather than beside it: the
position solve already holds the geometry and the satellite states, and
a second entry point would have recomputed both. The measurement is the
iono-free combination of the phase rates, the model is
`(v_sat − v_rx)·u − c·dts/dt + c·dtr/dt`, and the unknowns are the three
velocity components and the receiver's clock drift — the same 4×4 the
position uses.

*Checked by closure, both ways.* A still antenna reads **0.0000 mm/s**
with zero drift; an antenna moving 12 mm/s east is recovered as
**12.0000 mm/s east, 0.0000 north and up**, with its 2.5 m/s clock
drift. And a stream whose messages carry no rates — MSM4, MSM6, legacy
— yields `has_velocity = false` rather than a velocity of zero, which
is the failure mode that would read as *the antenna is perfectly
still*.

**A falsification found the test weak before it found the code wrong.**
Removing the satellite clock-drift term from the model changed nothing:
every synthetic ephemeris shared one `af1`, so the error was common to
all satellites and the receiver's own drift unknown absorbed it
exactly. Giving each satellite its own `af1` makes the same
falsification fail loudly — 1.0 mm/s of phantom motion. **A test whose
inputs are too uniform cannot see a common-mode mistake**, and
common-mode is precisely what a clock unknown hides.

### P5 — into the report  *(done 2026-10-09)*

New tier-2 fields beside the six that exist, each carrying its window
and its evidence count, as every tier-2 number already does. The
verdict vocabulary stays tier 2's own: STABLE / DEGRADED / UNSTABLE /
INSUFFICIENT EVIDENCE.

**Verify.** `--report` on a replayed capture equals the live report from
the same stream; `check_release.py` sees no claim it cannot verify.

**Built 2026-10-09, and one sentence of the step above was not kept.**
Self-position is reported as figures with **no verdict**: no
`SR_SELFPOS` row, `SR_METRIC_COUNT` still 6. Grading it would have
meant choosing a number for "abnormal scatter at a station" today, from
no measurements of any real station — the invented threshold this
project keeps catching itself at, and `docs/thresholds.md` exists
precisely so that every limit can name its evidence. The fields are
published, the decision is recorded on the struct, and a test asserts
`SR_METRIC_COUNT == 6` so that adding the row later has to be
deliberate rather than accidental. It becomes a metric when real
stations have said what normal looks like.

Where it lives: nine snapshot fields (`NsStats.selfpos_*`), six
accumulated in `SrState`, six published in `StationReport`, printed by
`cli_report_print` below the table and outside its numbering, and
serialised under `selfpos_*`. `SR_JSON_SCHEMA_VERSION` goes to **2** —
purely additive, so a reader written against 1 keeps working; the bump
is what lets a reader tell a document from an older build from one
whose station solved nothing.

*The mean is published beside the scatter because they measure
different things.* The offset is mostly the solution's own bias, metres
of it, and says nothing about the station; the scatter about that mean
is the half that measures something. Computed as
`sqrt(E[|x|²] − |E[x]|²)` and clamped at zero, since the difference of
two sums can go a hair negative when the scatter is far smaller than
the offset.

*An unsolved epoch publishes `null`, never zero* — the rule the six
metrics already follow, one level further on. A graph fed `0.000`
scatter would draw a station standing perfectly still when what
happened is that nothing was measured. The three means are the
exception that proves the rule: an offset is **signed**, so `NS_UNSET`
(−1.0) is a value it could legitimately hold and cannot serve as its
sentinel. `sp_samples` is the only thing that says whether they mean
anything, and the serialiser keys the nulls off it.

**A falsification caught a passing test that was protecting nothing.**
Removing the `has_vel` gate from the accumulation left "a stream
without rates reports no speed" green: the test's snapshot had left
`selfpos_speed_mms` at its `NS_UNSET` initialiser, so the comparison
rejected it for the wrong reason and the flag was never consulted. The
case now puts a speed of 7 mm/s in the snapshot *beside*
`has_vel = false` — a snapshot carrying a number it has no right to —
and the gate is what has to stop it. **A test written politely tests
the caller's manners, not the code's defences.**

*Falsified by name, each restored:* dropping the bias subtraction from
the scatter reddens "the scatter about it is free of that bias" and
"sixty identical epochs scatter by nothing at all"; removing the
`has_vel` gate reddens "but a stream without rates reports no speed"
(once that case was made hostile); emitting the scatter unconditionally
reddens "and its scatter is null, not a flattering zero".

**19/19 tests pass; `check_release.py` stays at 106 checks**, the four
stale-artefact failures being the known ones. The *`--report` equals
live* check is **outstanding**, for the same reason P3's `rnx2rtkp`
comparison is: it needs a real `.rtcm3` capture from the author's
caster, and nothing in the repository replays an observation stream.
Smoke-checked meanwhile against an empty replay, which prints
`Self-position: nothing solved -- no observations in this epoch` rather
than a clean zero.

### P6 — the GUI: a Self-position window

**Its own window** (author, 2026-10-09), beside the Stability window
rather than inside it — `design/gui-design.md` §14 is the neighbour to
follow for patterns, and the new section documents this one. It shows
the scatter over the run, not one epoch, because one epoch means
nothing here: the north/east/up offset against the stated reference,
its spread, the velocity with its own scale in mm/s, the satellites
used, and the code and phase residual RMS.

A single-frequency station shows *not computable* with the reason,
per decision 3 — an empty window with no explanation is the failure
this project keeps writing down.

**Verify.** Started from Explorer, not a shell — the GUI's own rule,
and the one that found a whole class of stdio faults.

**Built 2026-10-09.** `gui/gui_selfpos_window.{c,h}`, class
`NtripSelfPosClass`, **View → Self-position**, designed in
`design/gui-design.md` §16. The left half is an east/north scatter about
the broadcast reference — one dot per solved epoch, latest in orange, a
red cross where the run centres, rings at round distances — and the
right half is the figures: mean offset E/N/U, scatter about it, worst
code residual, fastest apparent motion, then the latest epoch's
satellites, PDOP, code residual, rate residual and clock drift. No
verdict and no fourth column, per P5's decision, with the header's third
line saying why rather than leaving a reader to wonder.

*The figures are core's, the plot is the window's.* Everything stated in
words comes from `AppState::reportOut` — the same accumulation, over the
same window of stream time, that the Stability window shows, so the two
cannot disagree about one stream. The window owns only a 7 200-point
ring (two hours at an epoch a second, 115 KB) for drawing, and nothing
computed from it is presented as a measurement. It is emptied inside
`ReportReset()` rather than at each caller, so one reset verb covers
both: a plot of one hour beside a mean of another is a disagreement
nobody can read.

*Two refusals worth naming.* The plot never self-centres — the origin
stays the position the station claims, because a cloud scrolled under
its own middle would hide the offset. And an epoch that did not solve
adds no dot, since a dot at the origin is exactly what a station sitting
on its declared coordinates looks like.

**P5 had left the snapshot's own serialisers behind**, found while
wiring this window: `NsStats` carried the nine `selfpos_*` fields and
`ns_stats_to_json()` published none of them, though the field block's
own comment said consumers would read them by key. So the daemon's
snapshot, `File > Export Statistics` and the CSV row all described a
stream without ever mentioning where it put itself. Fixed here —
`selfpos_*` in both serialisers under the same null-not-zero rule, and
`selfpos_rate_rms_ms` added so the window can state what the *velocity*
was measured to, which the code residual does not say. New cases in
`test_ns_stats` cover both directions: nulls when nothing solved, and
values when something did, because a null is only honest if the key can
carry a figure at all.

**And the polite test was written a third time.** The first version of
the "nothing solved" case used a freshly initialised snapshot, so when
the falsification removed the status gate from the serialiser the check
stayed green: the null came from the `NS_UNSET` initialiser and the gate
was never consulted. This is the same mistake P5 had just recorded and
promoted to `memory/MEMORY.md` — **knowing the pattern did not prevent
it; running the falsification did.** The case now carries real figures
beside a status that says nothing solved, which is also the honest
model: a caller is free to leave stale numbers behind, and the gate is
what has to stop them. *Falsified by name, each restored:* removing the
solved gate reddens "and no offset at all, rather than an offset of
zero"; removing the velocity gate reddens "nor a speed, which a zero
would read as a still antenna" and the solved-epoch case beside it.

**Verified the way the rule requires**, from a launcher with no console:
the class registers, the window is created, titled and visible, and a
second **View → Self-position** raises it rather than making another —
driven by the same `WM_COMMAND` the menu item sends. 19/19 tests, build
warning-free. **What that cannot verify is how it looks** against a real
station: the plot's scale, the spacing of the figures, whether the
caption reads at the default size. That is the author's eye on a live
stream, and it is what this step is waiting on.

*Two traps in the harness, not the program, both now in the gotcha log:*
this host enumerates windows on a different desktop, so `FindWindow`
returns zero for a class `GetClassName` reads straight off the live
handle; and inside a callback used as a delegate, PowerShell's
`Write-Output` becomes the delegate's **return value**, so the lines
never appear and the enumeration stops on the first window. Both looked
exactly like "the window was never created".

### P7 — the paid Android edition  *(not in this branch: after pro is in production, and after the listing decision)*

A `Panel` in pro's `Registry.kt`, as tier 2 on the phone already is,
reading snapshot fields the bridge already carries. Decision 8 is what
makes this a panel and a list entry rather than a port: if P7 turns
out to need changes in `src/core`, decision 8 was not kept.

**Verify.** On hardware, against the desktop's result for the same
station over the same window. And before any of it: the sentence in
`play-listing.md` changed on both listings, deliberately.

### P8 — say so

Split by the decision above.

**With P1–P6, desktop only:** the GUI's own documentation
(`docs/gui.md`), the CLI's `--report` wording, the feature matrix's
desktop columns, and a changelog entry carrying the measurement behind
the claim. **No listing text changes here**, because nothing published
about the Android editions becomes false.

**With P7, later:** the listing sentence on both editions, the wiki's
pro pages, the feature matrix's phone columns.

## What this will not claim

- Not a survey-grade coordinate, and not a replacement for the
  CSRS-PPP route in `docs/base-declaration.md`.
- Not a rover, not RTK, no ambiguity resolution, no baseline.
- Not a ninth KPI, and not part of the ninety-second check.
- Not available where the data cannot support it: single-frequency
  stations get *not computable*, stated.

## Out of scope, stated

- Precise orbits and clocks, SSR streams, any network processing.
- The free Android edition — this lands with tier 2, which is pro's.
- Reproducing Onocoy's other panels; `kpi-candidates.md` sorts those.

## Open, and worth the author's word before P1

1. ~~The listing sentence, or desktop only?~~ **Answered 2026-10-09:
   desktop only, no listing change, and built for reuse in pro from
   the first commit** — the gate above and decision 8.
2. ~~Single-frequency stations?~~ **Answered 2026-10-09: refuse.**
   Decision 3 stands as written — a single-frequency station gets
   *not computable*, never a number carrying tens of metres of
   ionosphere.
3. ~~GUI placement?~~ **Answered 2026-10-09: its own window**, not a
   section of the Stability window. See P6.
4. ~~The name?~~ **Answered 2026-10-09: self-position.** Not
   "antenna position", which is Onocoy's word for a quantity measured
   differently. The window, the report fields and the snapshot keys
   all use it, and the velocity half is the *self-position velocity*
   rather than an antenna velocity.

**Every open question is now answered; P1 may start.**

## Process

On a branch, as the TLS rollout was: this adds a solver to the core
that four programs link, and a half-landed state would strand them all.
Step commits, each with its verification run, and the branch merges
only when P1–P6 are green together.
