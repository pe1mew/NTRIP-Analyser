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

### T2.1 — the engine joins the build, and the bridge feeds it  *(done 2026-08-25)*

As planned, with one sharpening the header itself suggested: the
document embeds **`sr_to_json()` verbatim** -- the daemon's flat
Munin-frozen dialect -- rather than a third nested shape, exactly as
`"stats"` embeds the snapshot serialiser. Labels are deliberately
absent from that dialect; the frozen *keys* cross to Kotlin, which
maps them to its own strings (the `Failure.kt` precedent, and the
translatable half), while details and the headline stay engine words.
The statistics export inherits the server-identical report object for
free.

The first falsification attempt failed honestly: removing `sr_feed`
changed nothing the harness could see, because a junk-frame loopback
never advances the stream clock and the feed never fires there. The
harness therefore pins the *emission* (falsified red by renaming the
key), and the feeding is observable only where stream time moves --
which is T2.3's live run, and now said here rather than assumed.

*(As planned:)*

`station_report.c` into the NDK list (`check_source_lists` collects
either way); `SrState` in the bridge, reset with the KPI run, fed on
stream-time advance, evaluated at publish; the `"sr"` object. The
loopback harness gains shape assertions — the object is present, the
rollup reads INSUFFICIENT, six rows carry engine labels — and honestly
**cannot** reach a settled verdict: junk frames carry no observation
epochs, so stream time never advances there. The engine's verdicts are
`test_station_report`'s job and already done; the live rollup is
T2.3's.

### T2.2 — the model and the card  *(done 2026-08-25)*

As planned, shaped by T2.1's dialect decision: `SrDoc` decodes the
daemon's flat keys directly -- eighteen per-metric scalars with a
`rows` view over them -- rather than the nested rows the plan first
sketched. Verdicts that arrive null (live-only, absent) draw the
insufficient grey, never a zero. One consequence accepted and named:
the flat dialect carries no per-row limit text, so the expandable
value-against-limit line the plan sketched is dropped -- the engine's
detail sentences already carry the deciding figures, and inventing
limit text app-side would break the one-vocabulary rule the dialect
exists for. The card is therefore six plain rows, no per-row fold.

*(As planned:)*

`SrDoc`/`SrRow` (rows reuse the KPI item shape where fields align, as
`VrsDoc` did); `HAS_TIER2` in both editions; `Tier2Panel` in pro's
registry; the card as above; share section: the headline, then six
`label: VERDICT (value against limit)` lines — the sign-off numbers a
report exists for.

### T2.3 — the live verify  *(done 2026-08-25)*

A watch on HANESE from the Huawei, and the whole account observed on
the screen: at 100 s the card said **"INSUFFICIENT EVIDENCE -- 98 s of
600 needed, 99 sample(s)"** -- the countdown in the engine's words --
and at the floor it settled to **"STABLE over 0.2 h"** with six green
rows, each carrying its deciding figure: 0.0 reconnects/h, worst
100.000 % CRC in 10 min, fell 0.4 dB-Hz from 46.5, fewest held 39,
worst median ROTI 0.05 TECU/min, off-rate 0 %. The staleness branch
was not exercised live (no way to stop the caster's side without
stopping the app's); it stays covered by `test_station_report`, as the
plan allowed.

**The verify's side-catch**: the author noticed the sky header crediting
a day-old navigation file for placements the ephemeris stream had made
-- the source label is decided by run-scoped flags while the orbit
cache outlives runs in the process. Filed and fixed separately; see
the changelog entry it produced.

*(As planned:)*

A watch on HANESE from the Huawei, past the ten-minute window: the
card goes from INSUFFICIENT (with the honest countdown detail) to a
settled rollup, expected STABLE with six green rows. Then the
staleness guard's branch if cheaply reachable — stop the caster side
(not the app) and watch the report decline to judge — otherwise
recorded as covered by `test_station_report` alone.

### T2.4 — the guards  *(done 2026-08-25)*

The gate row landed during T2.2, demanded by the existing check. T2.4
added row 46's collection -- pro ●, and the "absent from Android"
sentence replaced by why it is present now and why free deliberately
is not -- and `check_tier2_parity()`: the `SrVerdict` enum order
pinned (Kotlin colours and words rows by these ordinals; reorder the
enum and every card recolours silently), the app's ordinal reading
checked, and the no-redefined-strings rule for `sr_`. Falsified by
bending the app's mapping to ordinal 9: red by name. **98 checks.**

*(As planned:)*

`HAS_TIER2` in the matrix gate table (the check will demand it);
row 46 Pro ○ → ●; the `SrVerdict` enum order pinned against Kotlin's
reading of it, exactly as the VRS gate enum is, if the card colours by
ordinal — and it will.

### T2.5 — say so  *(done 2026-08-25; wiki prepared, push is the author's)*

Landed together with run-flow F4, as both plans promised: one story on
the Watch-mode page. Running-one rewritten around the hub's verb
column and the banner's evidence narration; a Stability section
carrying the vocabulary rule, the ten-minute floor, why INSUFFICIENT
is an answer, and the stream-time and staleness rules in a reader's
words. The changelog entry leads with the question ninety seconds
cannot reach and ends with the expired blocker this item collected on.

*(As planned:)*

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
