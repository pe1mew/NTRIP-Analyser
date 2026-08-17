# Work item — user-supplied thresholds

**Status:** decision 3's first half is **built** — every row on every
desktop screen now shows the limit it was judged against. The rest is
proposed, and four questions at the end are the author's to answer.

The numbers that decide every verdict are `#define`s in `src/core/`, and
[docs/thresholds.md](https://github.com/pe1mew/NTRIP-Analyser/blob/main/docs/thresholds.md)
now lists all of them with a rationale. Writing that page made the
problem plain: of roughly twenty thresholds, three came from fixing a
defect, four are conventional or checked against real streams, and the
rest are **reasoned but not measured**. They are defensible. They are
not authoritative, and the programs currently present them as though
they were.

Two consequences follow, and the second is the one that matters.

**A user cannot disagree.** A control network and a hobby base are not
held to the same standard. Neither is a station on domestic broadband
and one on fibre: `SR_RECONNECTS_WARN_PER_H` = 1.0 will mark the first
`DEGRADED` for behaving exactly as its owner expects.

**A user cannot see what they are disagreeing with.** The check says
`Median C/N0 — 45.73 — Antenna and LNA chain healthy`. Healthy compared
with what? The number that decided it is not on the screen.

---

## Decision 1 — thresholds become data, not constants

The `#define`s become the **defaults** of a policy structure that the
engines take as an argument:

```c
typedef struct {
    double min_bytes_per_s;      /* KPI_MIN_BYTES_PER_S  */
    double arp_deadline_s;       /* KPI_ARP_DEADLINE_S   */
    /* ... */
    int    expect_sats[8];       /* KPI_EXPECT_SATS      */
} KpiPolicy;

void kpi_policy_defaults(KpiPolicy *p);
```

and the same for tier 2 (`SrPolicy`). `kpi_update()` and `sr_feed()` /
`sr_build()` gain a `const KpiPolicy *` / `const SrPolicy *`.

**The rule that thresholds live in `src/core/` is unchanged and is the
reason for this shape.** A frontend still cannot invent a number; it can
only hand core a policy it was given. The defaults stay exactly where
they are, so a program that loads no file behaves as it does today.

Cost: every call site gains a parameter, and `AppState`, `CliCtx` and
the daemon each gain a policy to hold. That is the whole cost — the
grading logic itself does not change.

## Decision 2 — one file, separate from the connection config

```json
{
  "schema_version": 1,
  "name": "RFSEE domestic",
  "tier1": { "min_cnr_median": 38.0, "expect_sats": { "gps": 8, "beidou": 6 } },
  "tier2": { "reconnects_warn_per_h": 3.0, "sats_warn": 20 }
}
```

**Not inside `config.json`.** That file describes *one connection*; a
policy describes *acceptance criteria* and applies to many. The service
already proves the point: `monitord.json` holds a list of mountpoints,
and one policy should cover all of them rather than being repeated per
entry.

**Partial by design.** A file carries only the keys it changes;
everything absent keeps its built-in default. A complete file would rot
the moment a threshold is added — and it would silently pin a user to
the values of the release they first wrote it under.

**`schema_version`**, for the same reason the statistics snapshot has
one: a file written today outlives the release that read it.

**Names, not macro spellings**, in the file: `min_cnr_median`, not
`KPI_MIN_CNR_MEDIAN`. The file is a user interface.

### Where it is found

In order, first hit wins, and **the chosen path is printed**:

1. `--thresholds <path>` (CLI, service), or the GUI's file dialog
2. `thresholds.json` beside the configuration file in use
3. the platform's per-user config directory
4. built-in defaults

Silent discovery is how two machines end up disagreeing about a station
and nobody can say why, so the effective source belongs in the same
place the effective config already goes: `--check-config` output, the
GUI's status line, the daemon's startup line.

## Decision 3 — every verdict states what produced it

This is the part that must not be traded away for schedule.

**On screen, beside each row — built.** Every row of the CLI's
`--check` and `--report`, and of both GUI windows, now carries a
**limit** column:

```
4   Satellites held    INSUFFICIENT EVIDENCE     38  min 25      fewest held: 38
5   Ionosphere         INSUFFICIENT EVIDENCE 0.00 TECU/min  max 0.50 TECU/min  gathering
```

`KpiResult` and `SrMetric` gained `limit` and `limit_dir`, set where the
verdict is decided, and `kpi_limit_text()` / `sr_metric_limit_text()`
format them in core so every surface prints the same sentence in the
row's own units and precision. Two consequences worth the shape:

- A screen **cannot** show a threshold the engine is not using, because
  there is no second copy of the number to drift.
- KPI 5 shows what *this* station was held to. Its expectation is the
  sum over the constellations the station streams, so a GPS+GLONASS
  base reads `min 14` where a five-system one reads `min 29` — a static
  string could not have said that, and the flat table value would have
  misstated the test.

The structural checks — whether RTCM decodes at all, whether an ARP has
arrived — and the VRS assertions leave the column blank. They are not
comparisons against a number, and inventing one would be worse than the
gap.

**Not yet on the phone.** The Android bridge serialises verdict, value
and detail; adding the limit is a JSON field plus Kotlin, and is the
remaining half of "any screen".

**In the header, when the policy is not the built-in one.** A verdict
produced under custom criteria must say so, or a screenshot of it is
misleading:

```
STATION OK   settled after 91 s, held 61 s   [policy: RFSEE domestic]
```

**In every machine-readable output.** `ns_stats_to_json`,
`sr_to_json` and the CSV export gain a policy name and a fingerprint
(a short hash over the effective values). Without it, two `.report.json`
files from two hosts are not comparable and nothing in them says so —
and the Munin graphs of a fleet would silently mix standards.

**And a way to print the effective policy**, with provenance per field:

```
$ ntrip-analyser --thresholds-print
min_cnr_median        38.0   thresholds.json
max_crc_rate         0.001   built-in
sats_warn               20   thresholds.json
```

That command is also what makes the requirement testable: a release
check can assert that every threshold named in `docs/thresholds.md`
appears in it, so the document cannot drift from the code.

## Decision 4 — refuse nonsense loudly

A policy file can express contradictions the `#define`s cannot: a warn
level worse than the bad level, a negative rate, a satellite count of
500, `min_window_s` below what any metric can measure.

Validate on load, name the field, and **fail rather than clamp**. A
clamped value produces a verdict the user did not ask for and will not
be able to reproduce. The daemon already does the right thing in
miniature: `report_window_s` below `SR_MIN_WINDOW_S` is refused, because
a setting whose every value means `INSUFFICIENT EVIDENCE` is a trap
rather than a choice.

Floors that must survive any policy, because below them the measurement
stops meaning anything:

| Field | Floor | Why |
|---|---|---|
| `min_window_s` | 600 s | Six of the six tier-2 metrics need it |
| `min_samples` | 10 | A window with three samples is an anecdote |
| `sustain_s` | 30 s | Below this a flickering station passes on a lucky moment |
| `warmup_s` | 0 s, but warn | Setting it to zero re-opens the "fewest held: 9" defect |

## Decision 5 — per program

| Program | How the policy arrives | Notes |
|---|---|---|
| CLI | `--thresholds <path>`, or discovery | Prints the source in the check header |
| Service | `"thresholds": "<path>"` or an inline object in `monitord.json` | One policy for all mountpoints; the fingerprint goes into every published document |
| GUI | File > Load thresholds, remembered across restarts; the two windows show it | An editor dialog is a later step — loading a file is the whole feature |
| Android free | Built-in defaults only | No file picker for this; the platform limit is real and should be stated rather than worked around |
| Android pro | Loading a policy is a reasonable paid feature | Within what the platform allows, per the editions rule |

## Open questions — for the user

1. **Named policies, or one file?** A `--thresholds survey.json` /
   `hobby.json` pair is more useful than one file people edit back and
   forth, and it makes the name in the header meaningful. Recommend:
   plain files, any name, with the name carried in the output.
2. **Should the GUI edit them, or only load them?** Recommend loading
   first. An editor is a dialog with twenty fields and needs the
   validation above to be finished anyway.
3. **Should a custom policy be allowed to make a verdict *stricter*
   only, or looser too?** Looser is the honest answer — a hobby base
   held to a control-network standard is the complaint that prompted
   this — but it does mean `STATION OK` stops being comparable across
   users, which is exactly why decision 3 exists.
4. **Do the VRS assertions get the same treatment?** They are
   protocol-behaviour tests rather than quality bars, and their values
   are about what casters actually do. Recommend: overridable, but
   documented as the least likely to need it.

## What this is worth

The measurement engine has been proved twice over — the CLI, the daemon
and the GUI produce identical verdicts from identical snapshots, and a
replay reproduces a live run. All of that rests on numbers this project
chose and, in the largest group, has not measured. Making them visible
is the smaller half of the work; making them **arguable, and stamped
into every artefact** so that two verdicts can be compared, is the
half that decides whether the tiers are trustworthy outside this
project.
