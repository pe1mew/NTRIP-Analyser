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
| 1 | Latency, and KPI 9 | specified; **blocked** until free clears closed testing |
| 2 | The report skeleton, from metrics that already exist | next |
| 3 | Sky visibility as a number | after phase 2 |
| 4 | Cycle-slip rate from lock time | after phase 2 |
| 5 | Observable retention → multipath RMS **and** a RINEX writer | deferred by decision |

---

## Phase 1 — Latency, and KPI 9 — blocked

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

## Phase 2 — The report skeleton — next

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

## Open questions

- What window does the report default to, and does it roll or accumulate?
  A daemon wants both — the last hour *and* since restart.
- Does the report get a verdict at all, or only measurements with
  thresholds? A single word invites the comparison with STATION OK that
  tier 1 owns.

## Outcome

Nothing built yet.
