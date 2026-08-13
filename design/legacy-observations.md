# Legacy observation messages — design note

**Status:** decided, not built. Backlog item `design/todo.md` §1.7.
**Decision taken:** 13 August 2026.

## The problem, measured

Kadaster's `APEL0` is a working RTCM 3.1 reference station: 1004 and
1012 at 1 Hz, 1006, 1008, 1013, 1033, 1230. The tool reports it
**FAILED** in fifteen seconds.

```
1 Connected and producing  PASS   344.70  Authenticated, connected, data flowing
2 RTCM 3.x format          PASS    32.00  CRC-valid RTCM 3.x frames decoded
3 Reference position (ARP) PASS     1.00  1005/1006 received with non-zero coordinates
4 Multi-GNSS observations  FAIL     0.00  Neither GPS nor Galileo MSM at rate
5 Satellites in view       FAIL     0.00  Fewer than half the expected satellites
6 Median C/N0              WARN     0.00  Not measured: C/N0 is read from MSM4, 5, 6 and 7
== FAILED ==
```

Three of the eight verdicts describe this tool's coverage, not the
station. A user would take that verdict to an operator, and the operator
would be right to dismiss it.

The cause is one line. `sv_track_feed()` accepts only 1071–1137:

```c
if (msg_type < MSM_TYPE_MIN || msg_type > MSM_TYPE_MAX) return 0;
```

so on a legacy stream **no satellite is ever counted** — KPI 5 fails at
zero, KPI 4 finds no MSM epochs, and KPI 6 has no C/N0 to take a median
of.

## How common

| Caster | RTCM 3 mountpoints | legacy-only |
|---|---|---|
| `ntrip.kadaster.nl` | 35 | 5 |
| `caster.centipede.fr` | 1218 | 1 |

And mixed streams are commoner still: `rtk2go.com/Mirmenhof` sends
1004 and 1012 *beside* its MSM6, and Centipede's `NEAR` sends 1004/1012
beside MSM7. Those are measured correctly today only because the MSM
half carries the measurement.

Small in percentage, absolute in effect: for a legacy-only station the
answer is wrong, not partial.

## What the messages carry

| Type | System | Carries C/N0 | Fields |
|---|---|---|---|
| 1001 | GPS | no | L1 only |
| 1002 | GPS | **yes** | L1, DF015 8 bits, 0.25 dB-Hz |
| 1003 | GPS | no | L1 + L2 |
| 1004 | GPS | **yes** | L1 + L2, DF015 and DF020 |
| 1009 | GLONASS | no | L1 only |
| 1010 | GLONASS | **yes** | L1, DF045 |
| 1011 | GLONASS | no | L1 + L2 |
| 1012 | GLONASS | **yes** | L1 + L2, DF045 and DF051 |

Satellite identity is per observation rather than in a mask: DF009, a
6-bit GPS satellite ID, and DF038, a 5-bit GLONASS slot. Both map onto
the core's 1-based constellation ids (GPS 1, GLONASS 2) with no
ambiguity.

**Legacy messages cannot express Galileo, BeiDou, QZSS, SBAS or NavIC.**
There is no legacy message for them; a station carrying those must use
MSM. That single fact is what forces the decision below.

## The decision

**Delivery is judged against what the sourcetable advertises.** A
station advertising `GPS+GLO` and delivering exactly that is a **pass** —
an old station, not a broken one.

This is not a new principle here: **KPI 8 already judges the stream
against the sourcetable**, deliberately, because the 1005/1006
indicator bits cannot express BeiDou. Extending the same rule to KPIs 4
and 5 makes the set consistent rather than adding an exception.

The alternative — decode legacy observations but leave KPI 4 demanding
*"GPS and Galileo MSM at 0.5 Hz"* — would only move the false verdict
from KPI 5 to KPI 4. The station would still be failed for not doing
something its format cannot do.

## What changes, concretely

### KPI 4 — observations flowing

Today (`src/core/kpi.c`):

```c
bool gps = msm_flowing(s, 1070, 1079, &dt_gps);
bool gal = msm_flowing(s, 1090, 1099, &dt_gal);
if (gps && gal) PASS;
```

Two constellations are hard-coded, and only MSM counts. The rule becomes:
**every constellation that is streaming must stream at rate**, counted
across legacy and MSM alike, with at least one system present.

Whether an *advertised* constellation is absent stays KPI 8's business.
Keeping that boundary matters: if KPI 4 also failed for a missing
advertised system, one fault would produce two failing verdicts, and a
user reading two failures would look for two problems.

### KPI 5 — satellites in view

Today: a flat `KPI_MIN_SATS` of 25, which assumes a multi-constellation
station. A GPS+GLONASS station cannot reach it — roughly 8–12 GPS plus
6–10 GLONASS above the mask — so it fails by arithmetic, not by fault.

The threshold becomes **the sum of a per-constellation expectation over
the advertised set**, keeping the existing shape (at or above expected →
pass, at least half → caution, below → fail). The table itself is an
open question below.

### KPI 6 — C/N0

Legacy C/N0 is 8 bits at 0.25 dB-Hz, per signal. The rules already in
force apply unchanged: take the **strongest signal per satellite**, as
the MSM path does, and average **in linear power**, never in decibels.

### Nothing in the frontends

All four read the same snapshot, so the CLI, the GUI, the daemon and
both Android editions gain this at once. That is the architecture
working, and it is why this note is only about `src/core`.

## Implementation sketch

1. `rtcm_legacy_extract()` in `src/core/rtcm3x_parser.c`, beside the MSM
   extractors: walk 1001–1004 / 1009–1012, return (PRN, best C/N0) pairs
   and the constellation id.
2. `sv_track_feed()` accepts those types and feeds the same table. It is
   keyed by (constellation, PRN), so **a station sending both legacy and
   MSM for the same satellite is counted once** — which `Mirmenhof` and
   `NEAR` will exercise directly.
3. `kpi.c`: KPIs 4 and 5 as above, driven by `advertised_gnss` (already
   in the snapshot, already populated from the sourcetable's NavSys).
4. No new JSON fields are needed. `cnr_mean`, `sats_total` and the
   per-constellation rows fill in by themselves.

## Verification plan

- **`APEL0`** (Kadaster, legacy-only): today FAILED in 15 s; must become
  a judged verdict, with satellites counted and a C/N0 median.
- **`Mirmenhof`** (rtk2go, legacy + MSM6) and **`NEAR`** (Centipede,
  legacy + MSM7): satellite counts must not change when legacy decoding
  is added — the dedupe test.
- **A fixture**, in the style of `test/test_msm_cnr.c`: synthetic 1004
  and 1012 frames with C/N0 values no other reading could produce, and
  verified to fail when the layout is deliberately broken.
- **A station that advertises more than it sends**, to confirm KPI 8
  still reports the omission and KPI 4 does not double-count it.

## Boundaries

- **RTCM 2.x is out of scope.** It is a different protocol, not an
  extension of this one. Kadaster publishes one such mountpoint
  (`APEL00NLD2`); it stays unmeasurable, by decision rather than by
  oversight.
- **MSM1–3 keep no C/N0**, because they carry none. Their satellites are
  already counted.

## Open questions

1. **The expected-satellite table.** Per constellation, above a 10°
   mask, at mid latitude: GPS 8, GLONASS 6, Galileo 6, BeiDou 8, QZSS 1,
   SBAS 2, NavIC 2 are plausible starting values, and every one of them
   should be checked against our own captures before it decides a
   verdict.
2. **KPI 4's label.** *"Multi-GNSS observations"* stops being accurate
   when a single-system station can pass. *"Observations flowing"* is
   truer, but a KPI's name is a user-visible contract and appears in
   every report already written.
3. **Legacy-only and advertised-only-GPS.** A station advertising `GPS`
   alone, delivering 1004 alone, would pass every KPI. Correct by this
   decision — but worth stating in the wiki, so nobody reads a pass as
   "suitable for RTK with your multi-constellation rover".

## Adjacent finding, not part of this work

The ionospheric monitor gates on `(msg_type % 10) != 7` (`iono.c:138`)
and its header states that *"MSM4/5/6 carry no extended phase
resolution"*. That is wrong for **MSM6**, which carries the same 24-bit
DF406 fine phase range as MSM7 and differs only in having no Doppler.
An MSM6 station — `rtk2go.com/Mirmenhof` is one — is therefore excluded
from the iono monitor for no reason. Same family of error as the C/N0
gap, and worth its own item.
