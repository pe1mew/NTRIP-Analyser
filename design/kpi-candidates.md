# Four candidate KPIs: what each would cost, and where it belongs

A feasibility study of cycle-slip rate, code and phase RMS, latency and
sky visibility. Written 2026-08-16, against the code as it stands at
3.4.0.

The conclusion in one line: **one of the four is a KPI and three are
monitor metrics**, and mixing those up would break the thing that makes
the KPI set worth having.

## The decision this study led to (2026-08-16)

Reviewing it produced a better framing than the one it was written with,
and the rest of the file should be read through it.

These are not slow KPIs. **They are a different kind of measurement.**
The eight checks ask *is this station fit right now* — a fitness test,
deliberately bounded. Slip rate, coverage, multipath and a latency
*distribution* all ask *has this station been fit, and is it staying that
way* — a stability question, and stability is not observable in an
instant at any price.

So the answer is not a ninth, tenth and eleventh KPI. It is a **second
tier**: a station report over hours, with its own metrics, its own
thresholds in `src/core` beside `kpi.h`, and its own verdict — standing
next to the acceptance test rather than inside it. Two questions, two
instruments, one core.

That maps onto the products already: free spot-checks fitness without
limit, and **stability is what the daemon and pro's watch mode are for**.
It turns the paid proposition from "the same thing for longer" into "a
question only time can answer", which is a better thing to sell and a
truer description of the work. The CLI gets the report offline, over a
captured `.rtcm3`, which is what 3.4.0's capture made possible.

One consequence for latency below: it belongs in **both** tiers. The spot
value is a KPI — corrections arriving late now is a fact about now. The
*distribution* over hours, with its 95th percentile and its spikes around
reconnects, belongs in the report. Same number, two questions.

## The constraint that decides most of this

The eight-KPI check is a *bounded* test. It reaches a verdict in about
ninety seconds, every KPI must hold for sixty continuous seconds, and the
same verdict means the same thing in the CLI, the GUI and both Android
editions. That contract is why a free STATION OK is worth as much as a
paid one.

A measurement that cannot settle inside that window is not a KPI, however
useful it is. Forcing one in would either lengthen the check for everyone
or produce a verdict from too little evidence — and the project has been
here before: KPI 8 failed healthy stations three times, and twice the
cause was judging too early.

So each candidate is assessed twice: *can we compute it at all*, and
*does it settle in ninety seconds*.

## 1. Latency — **build it, as KPI 9**

**What it is.** The delay between the epoch stamped in a message and its
arrival. The single most operationally relevant number for RTK after the
data itself: corrections that arrive late age the solution, and a rover
notices before any other symptom appears.

**Feasibility: the highest of the four, because most of it exists.**
`NsStatsSnapshot.latency_s` is already declared, documented as "newest
MSM epoch vs system clock", serialised into the daemon's JSON, written
into the CSV export, and displayed on the Android station tile.

**Nothing computes it.** The only assignment in the tree is
`ns_stats_init()` setting it to `NS_UNSET`. Every consumer therefore
publishes and displays "not measured", and has since the schema was
written.

This is the same defect the ARP fields carried — the comment at
`src/session/ntrip_session.c` records that they "existed since the schema
was written but nothing filled them", and the daemon published
`arp_valid:false` for every station until a live run tripped over it. A
declared-but-unfilled field is worse than a missing one: it looks like an
answer.

**What the work is.** `msm_get_epoch()` already extracts the epoch. The
computation is that value against the system clock, and the difficulty is
entirely in the time bases:

- MSM epochs are milliseconds **of week** for GPS, Galileo and BeiDou,
  and milliseconds **of day** for GLONASS — the same trap that made a
  day-old GLONASS orbit read as an hour old (`test_eph_validity.c`).
- GPS time runs 18 seconds ahead of UTC today, and BeiDou 14 behind GPS.
  A latency that ignores this is wrong by a constant nobody notices,
  because it looks plausible.
- **It measures the local clock as much as the caster.** On a host with
  no NTP, this KPI reports the clock drift and blames the station. It
  must state that dependency where the user can see it, and probably
  refuse to judge when the clock is obviously unsynchronised.

**Where it belongs.** Everywhere, as KPI 9 — it is instantaneous, so it
meets the bounded contract, and it is the one candidate that does.

**The cost nobody thinks of.** "Eight checks" is a published claim: it
appears in the wiki, the store listing, the About blurb, the manifest and
the free edition's feature flags, and `tools/check_release.py` verifies
the count across all six surfaces. Adding a ninth is a documentation
change in six places plus a store-listing update. The checker will find
them, which is the point of it, but the work is not just `kpi.c`.

## 2. Sky visibility — **build it, as a monitor metric**

**What it is.** How much of the sky the station actually delivers signal
from, as a number rather than a picture: the fraction of the sectors that
*should* have been observed which *were*, and the horizon profile that
falls out of the same data.

**Feasibility: high, and the accounting already exists.**
`SkyRenderSector` holds exactly two integers — `observed` and `expected`
— per sector of the polar grid, filled by `sky_collect.c` from the orbit
cache and the station's own position. A coverage percentage is arithmetic
over a structure that is already populated in the CLI, the GUI and both
Android editions.

**What stops it being a KPI.** Time. A satellite sweeps the sky slowly;
ninety seconds populates a sliver of the grid, and "62% of the sky
unobserved" after a minute means only that the minute was short. The
number becomes meaningful over hours and definitive over a day — the
sidereal period after which the GPS constellation repeats.

**Where it belongs.** The daemon and pro's watch mode, where sessions run
for hours; the GUI for the diagnostic form, which is a per-azimuth
horizon profile rather than a single percentage — "blocked from 040° to
110° below 25°" tells the owner where the tree is.

**A caveat worth stating in the UI.** Expected counts depend on the orbit
cache. A station with no ephemerides and a stale RINEX has a small
`expected`, so its coverage looks perfect. The metric must report its own
evidence base, exactly as the orbit-source badge already does.

## 3. Cycle-slip rate — **build it, but wider than what exists**

**What it is.** How often carrier-phase tracking breaks. Every slip
forces a rover to re-fix ambiguities; a station with a high slip rate
gives fixes that keep dropping to float for no visible reason.

**Feasibility: partly built, and the built part is narrower than it
looks.** `iono.c` already manages continuous phase arcs and breaks them
on a slip, and `iono_slips` is already published in the CSV export. But
those arcs exist to compute ROTI, which constrains them:

- **MSM6/7 only** — the extended fine phase range is what the arcs read.
  A station streaming MSM4 has no slip count at all.
- **GLONASS is excluded by design**, because FDMA gives each satellite
  its own frequency and the geometry-free combination needs a fixed pair.
- Only satellites with a usable **dual-frequency** pair are tracked.

So the existing counter answers "slips among the satellites ROTI could
use", not "this station's slip rate".

**What the work is.** The honest metric comes from the **lock-time
indicator**, which every observation family carries: four bits in MSM4/5,
ten in MSM6/7, and its own field in the legacy 1001–1012 messages. A
lock time that decreases between epochs is a slip, per signal per
satellite, with no dual-frequency requirement and no exclusions.

The parser already knows where those bits are: `msm_layout()` records
`lock_bits` per MSM type and the CNR extractor navigates *past* the lock
array to reach C/N0. Reading it is an offset that is already computed,
not new layout work — the cheapest possible extension of a decoder that
is otherwise the risky part of this project.

**What stops it being a KPI.** A rate needs a denominator. On a healthy
station slips are rare, so ninety seconds of zero slips is the expected
observation on a good station *and* a mediocre one; only a catastrophically
broken receiver would show up. As slips per satellite-hour it wants an
hour.

**Where it belongs.** The daemon first, since Munin already graphs the
things that decay slowly. The GUI second, as a per-satellite table beside
the existing ionosphere view — the two share a cause often enough that
seeing them together is the diagnosis: slips that follow ROTI are space
weather, slips that don't are the receiver or the antenna.

## 4. Code and phase RMS — **the expensive one, and the one to pair**

**What it is.** The observable-domain noise of the station: how much
scatter is on its pseudoranges and carrier phases. The standard form is
the multipath combination — MP1 and MP2, what TEQC reports — which is
code minus a linear combination of the two phases.

**Feasibility: computable in principle, blocked in practice.** The
combination is geometry-free and clock-free, so it needs no orbits, no
station position and no external products: dual-frequency code and phase
are sufficient. That is the good news.

The blocker is the one this project has already met from the other
direction. **The session layer retains no pseudorange and no carrier
phase.** `SvTrack` keeps last-seen, C/N0 and a power sum, because that is
all the eight checks ever needed. The full decoders extract both
observables and *print* them; `iono.c` keeps phase differences for MSM6/7
alone.

This is exactly the wall the RINEX question hit at the start of this
month: the project cannot write a RINEX observation file for the same
reason it cannot compute MP1 — the observables are decoded and dropped.

**So pair them.** One change — carrying observables through the session
layer, per satellite per signal per epoch — unlocks:

- code and phase RMS, and the multipath combinations;
- a **RINEX observation writer**, which would remove the RTKLIB
  dependency from `docs/base-declaration.md` entirely;
- and, incidentally, the arcs that a proper slip rate wants.

That is a much better argument than any of the three make alone, and it
is also the largest change proposed here: it touches the session layer's
memory profile on a phone, which is the constraint that shaped the
current design.

**Further requirements once the data is there.** MP needs continuous
arcs, so it depends on slip detection; it needs the arc mean removed to
kill the ambiguity, so it needs minutes per satellite, not seconds; and
an *absolute* phase RMS is not computable without a position and orbits —
what is computable is short-term scatter, which is what ROTI already
does.

**Where it belongs.** The CLI and the daemon. Note the new capture makes
this attractive offline: `.rtcm3` on disk plus a replay path means MP RMS
can be computed after the fact, over a six-hour file, without holding
anything in memory during the run. The phone should be last, if ever.

## Summary

| Candidate | Computable now | Settles in 90 s | Verdict | First home |
|---|---|---|---|---|
| Latency | field exists, nothing fills it | **yes** | **KPI 9** | everywhere |
| Sky visibility | counts already accumulated | no — hours | monitor metric | daemon, watch, GUI |
| Cycle-slip rate | partly, MSM6/7 dual-frequency only | no — an hour | monitor metric | daemon, GUI |
| Code / phase RMS | **no** — observables not retained | no — minutes | monitor metric | CLI offline, daemon |

**Recommended order.** Latency first: it is the smallest change, it fills
a hole that is already visible to users as a blank field, and it is the
only one that belongs in the check. Sky visibility second, as arithmetic
over an existing structure. Slip rate third, extending a notion that
exists. Observable retention last and deliberately — as one piece of work
with the RINEX writer, justified by both.

**What not to do.** Do not add any of the last three to the eight-KPI
check to make the check look more thorough. A verdict formed from
ninety seconds of evidence about a quantity that needs an hour is worse
than no verdict, and the project has the scar tissue to prove it.

## Where the two tiers live — decided 2026-08-16

| | Tier 1: the 90-second check | Tier 2: duration measurement and verification |
|---|---|---|
| Asks | is this station fit *now* | has it been fit, and is it staying that way |
| CLI | ● | ● — including offline, over a capture |
| GUI | ● | ● — preliminary analysis and commissioning |
| Daemon | ● | ● — the permanent case it exists for |
| Android free | ● | ○ |
| Android pro | ● | ◐ — what the platform allows |

**Tier 2 is bounded by the platform, not withheld from it.** Pro may
carry the tier-2 metrics that fit inside a phone's constraints, and the
CLI, the GUI and the daemon continue past the point where those
constraints bite. That is a ceiling imposed by Android, not a feature
held back — which matters, because this product line withholds
convenience and never capability.

Where the ceiling is: Android 15 caps a foreground service at six hours,
so a watch session is bounded there whatever we do. Doze and vendor
battery managers interrupt long runs regardless of our intent; phones
throttle thermally; mobile data costs money by the megabyte; and the
device leaves with its owner. A metric that needs a day of uninterrupted
streaming therefore cannot live on the phone, while one that settles in
an hour can.

Read against the four candidates, that ceiling is legible rather than
arbitrary: a **latency distribution** and a **coverage percentage** over
a few hours are within a watch session; a **slip rate** in slips per
satellite-hour is marginal; **multipath RMS**, which wants long arcs and
retained observables, is not a phone measurement at all.

So pro's proposition stands as written — watch mode still answers "does
it *keep* passing?", now with a report behind it rather than only a live
view. `android/design/editions.md` and the pro listing need no retreat.

**The phone is the field instrument; the desktop and the server are the
laboratory.** The GUI's tier-2 role is commissioning rather than
monitoring: an hour on a newly installed station, on the machine already
in the van, to judge whether it is worth handing to the daemon for a
week. Same code, same report, shorter window — which works only because
every value states the window it came from.

## The equality property, and the one metric that breaks it

**Requirement**: the report computed live from a stream and the report
computed from a capture of those same bytes must be identical. That makes
reports reproducible, gives the tier a `test_capture`-shaped regression
test, and turns an archived `.rtcm3` into a durable record — a six-hour
session yields its report again in five years.

**Latency cannot satisfy it.** Latency is epoch time against *arrival*
time, and a capture holds no arrival times: 3.4.0's capture is
CRC-valid frames and nothing else, deliberately, which is what makes it
byte-identical across programs and clean input to a converter. Reconnect
counts have the same problem — a replay never drops.

Three ways out, and the third is the one to take:

1. Timestamp the capture. **No**: it breaks the format's one strong
   property and makes our files something no other tool reads.
2. A sidecar timing file. **No**: already rejected when the capture was
   specified, for the same reason — a `.rtcm3` plus a companion is no
   longer the format the GUI reads.
3. **Mark each metric as derivable or live-only.** The report states,
   per value, whether a replay can reproduce it; a replayed report shows
   live-only metrics as *not available from a capture* rather than
   silently omitting them or inventing them. Equality is then asserted
   over the derivable subset — which is testable, honest, and leaves both
   the capture format and the property intact.

That distinction is worth designing in from the first metric rather than
retrofitted, because the temptation later will be to quietly let a
replayed report show a latency of zero.

## What the second tier would need

Sketched here so the decision above has somewhere to land; none of it is
designed yet.

- **A window, stated with every number.** "Slips per satellite-hour over
  6 h" is a measurement; "slip rate: 0.3" is a rumour. Every value in the
  report carries the period it was measured over and how much evidence
  backed it — the same discipline the orbit badge already applies to
  placement.
- **Thresholds in the core**, beside `kpi.h`, never in a frontend. The
  rule that makes a free verdict worth a paid one applies here or the
  second tier is worth less than the first.
- **Metrics that already exist and are simply not aggregated**:
  reconnects, CRC rate, C/N0 trend, ROTI, per-type message rates, epoch
  gaps. A first report could be assembled largely from the snapshot the
  daemon has published all along, before any of the four candidates
  land.
- **A verdict that can say "not enough evidence yet"**, and mean it.
  Ninety seconds of a six-hour metric is the state the report will spend
  its first minutes in, and saying so is better than extrapolating.
- **An offline path**: the report computed from a `.rtcm3` should equal
  the report computed live from the same stream. That is testable, in
  the way `test_capture` is, and it is the property that makes a
  captured session a durable record rather than a screenshot.
