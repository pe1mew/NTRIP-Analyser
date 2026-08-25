# Tier 2 on the phone — plan

Phase 2, item 5 (`design/guiV2rollout.md`: Tracks → VRS → hand-over →
export → **tier 2** → TLS). **Pro only** — and for once the edition
argument is arithmetic rather than policy, below.

**What it is.** The stability report: has this station *been* fit, and
is it staying that way — six metrics over stream time (availability,
frame integrity, signal level, satellites held, ionosphere, delivery
rate), with its own vocabulary. `STABLE / DEGRADED / UNSTABLE`, never
tier 1's words, because *fit now* and *stable lately* are different
questions whose answers must not read as one — and `INSUFFICIENT
EVIDENCE` is a real verdict, not a failure.

## The blocker named in the matrix is gone

Row 46 says why Android never had this: *"the measurement exists, the
runtime to sustain a ten-minute run on a phone does not."* That was
written before watch mode. Pro sustains runs for hours — nine, on the
record — so the stated reason expired the day watch shipped, and this
item is the collection.

## The engine is finished, tested, and not in the build

`station_report.{c,h}`: the six metrics, the policy struct, the
stream-time windows, the staleness guard — built, and defended by
`test_station_report` ("Tier 2's three load-bearing decisions... each
would erode quietly without a test that fails when it does"). The CLI
and the daemon already run it. The file is simply **not in the NDK
source list**, which `check_source_lists` will notice the moment it is
added to one list and not the other.

The load-bearing decisions travel with it, none renegotiable here:

| Rule | Why |
|---|---|
| Windows are **stream time**, fed at 1 Hz of it | a replay reproduces a live run exactly; NTP cannot step it |
| `INSUFFICIENT` below **600 s / 10 samples** | a verdict without evidence is worth less than none |
| The report **declines to judge** when stream time stalls 120 s against the host clock | one station published "STABLE over 1.7 h" for fourteen hours after its last observation |
| Integrity is judged **per window**, worst kept | the cumulative mean hides exactly the bad ten minutes this tier exists to catch |
| Engine words travel whole | label, detail, value, and the limit that decided it — a verdict you cannot argue with is not a verdict |

## Division of labour

The bridge is the caller, as it is for the eight and the five:

- `SrState` beside `KpiRun`; `sr_reset` where the KPI run starts;
  fed in `bridge_pump` **when `stream_time_s` has advanced ≥ 1 s**
  (the CLI's own pacing, `CLI_REPORT_SAMPLE_S`) — not per pump, and
  never on wall time.
- `sr_evaluate` at the publish; an `"sr"` object in the document:
  `overall`, `overall_name`, `headline`, `window_s`, `samples`, and
  six rows shaped as the KPI rows are (verdict, verdict_name, value,
  label, detail) plus each row's `limit`, `limit_text` and
  `available` — the last because latency is live-only and absence
  must not read as zero.
- Kotlin decodes and draws. Nothing is computed on the phone; the
  thresholds stay in the core, which is the rule that makes any
  edition's verdict worth the same as another's.

## Edition — arithmetic, not policy

`Features.HAS_TIER2` — pro true, free false. Free's only run is the
station check, which settles in about two minutes; `SR_MIN_WINDOW_S`
is ten. **Free could never see anything but INSUFFICIENT EVIDENCE** —
shipping the card there would be shipping a permanent shrug. The
capability follows watch mode because it is arithmetic downstream of
it. Free's *More in Pro* wording grows once, as usual.

## The panel

`Tier2Panel` in the registry slot reserved for it since P1.2 (the
comment names it), after `HandoverPanel`. **A card, not a screen** —
one deviation from the rollout's sketch, argued now rather than
discovered later: the rollout listed "stability" among the cards that
drill down, but the report is six rows and a headline, exactly a hub
card's shape, and the KPI rows' expand-in-place pattern already shows
six rows with evidence without leaving the screen. A `Dest` detail
would be a screen with nothing on it the card lacks. If a metric later
grows a plot (ROTI over time, say), the detail contract is one
override away — that is what it is for.

Drawn like the eight: verdict chip in tier 2's own colours (an
`INSUFFICIENT` grey distinct from PENDING's, `STABLE` green,
`DEGRADED` amber, `UNSTABLE` red), engine label, engine detail,
expandable to the value-against-limit line. The headline sentence
under the title while the run is short: *"Insufficient evidence —
10 minutes of stream needed, 3 so far"* is the honest state and the
engine already words it.

Shown while a watch runs **and after it ends**, like every run-scoped
reading; hidden entirely when there has never been a run.

## Steps

### T2.1 — the engine joins the build, and the bridge feeds it

`station_report.c` into the NDK list (`check_source_lists` collects
either way); `SrState` in the bridge, reset with the KPI run, fed on
stream-time advance, evaluated at publish; the `"sr"` object. The
loopback harness gains shape assertions — the object is present, the
rollup reads INSUFFICIENT, six rows carry engine labels — and honestly
**cannot** reach a settled verdict: junk frames carry no observation
epochs, so stream time never advances there. The engine's verdicts are
`test_station_report`'s job and already done; the live rollup is
T2.3's.

### T2.2 — the model and the card

`SrDoc`/`SrRow` (rows reuse the KPI item shape where fields align, as
`VrsDoc` did); `HAS_TIER2` in both editions; `Tier2Panel` in pro's
registry; the card as above; share section: the headline, then six
`label: VERDICT (value against limit)` lines — the sign-off numbers a
report exists for.

### T2.3 — the live verify

A watch on HANESE from the Huawei, past the ten-minute window: the
card goes from INSUFFICIENT (with the honest countdown detail) to a
settled rollup, expected STABLE with six green rows. Then the
staleness guard's branch if cheaply reachable — stop the caster side
(not the app) and watch the report decline to judge — otherwise
recorded as covered by `test_station_report` alone.

### T2.4 — the guards

`HAS_TIER2` in the matrix gate table (the check will demand it);
row 46 Pro ○ → ●; the `SrVerdict` enum order pinned against Kotlin's
reading of it, exactly as the VRS gate enum is, if the card colours by
ordinal — and it will.

### T2.5 — say so

Changelog under `[Unreleased]`. Wiki: **Watch mode** is this page's
natural home — its own opening question is *does it stay healthy* —
so the stability card is documented there (the vocabulary rule, the
ten-minute floor, why INSUFFICIENT is an answer), with the Pro tables
gaining the row.

## Open, and worth an answer before T2.2

1. **Card only, no drill-in screen** — the deviation from the
   rollout's sketch, recommended above. Cheap to reverse later.
2. **Does the check (non-watch) feed tier 2 in pro?** A two-minute
   check always ends INSUFFICIENT. Recommendation: yes, feed it anyway
   — the card then appears with the honest countdown and teaches what
   the watch unlocks, and the alternative is a card that pops into
   existence only in watch mode, which is a mode-dependent hub.
3. **Policy editing** stays out of scope — `thresholds-track.md` owns
   it; the phone ships the defaults as every frontend does today.
