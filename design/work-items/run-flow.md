# Run flow and progress — plan

Not a phase-2 feature: a correction from first field use of tier 2
(author, 2026-08-25). Three findings, one root — **the watch outgrew
its home**. When watch mode was built its product was the analysis
views, so its start button lives on the analysis screen. Watch now
feeds the Watch card, the hand-over history and the stability report —
all *hub* products — while starting one requires knowing to leave the
hub, and the verdict banner narrates only tier 1 throughout.

The findings, verbatim in spirit:

1. The Stability card shows during a plain check and **never
   satisfies** — a card counting to 600 on a run that ends at 120 is a
   promise the run cannot keep. (The tier-2 plan's open question 2
   chose "feed the check anyway — it teaches". Field use says it
   nags.)
2. Starting a watch via Analysis → *Start analysis* is **vague** — the
   name says analysis, the products live on the hub, and nothing on
   the hub says the mode exists.
3. The banner's progress bar shows **only tier 1's sustain window**;
   during a watch it says nothing about the watch, and nothing about
   the stability evidence the user is actually waiting on.

## The corrections, in the template's own grammar

### F1 — runs start on the hub, all three

The hub already grew a second run type without ceremony: *Network-RTK
check ▶* above *Run the check*. A **Watch station ▶** row completes
the pattern: three verbs, one place, each naming what it starts.

- `WatchControlPanel` in **pro's registry** beside the run controls;
  an outlined transport row like the VRS one, enabled on
  `settings.isComplete && !running`. No new feature gate:
  `HAS_WATCH` already names this capability and the registry gates
  the row.
- Wired to `MonitorService.start(context, settings, watch = true)` —
  the same call the analysis screen makes today.
- The analysis screen's button **stays** as a convenience alias but is
  relabelled: `action_analyse` "Start analysis" → **"Start watch"**,
  so one mode has one name everywhere. (The wiki already calls it
  watch; the button was the last holdout.)

### F2 — the banner narrates the run that is going

`VerdictBadge` already holds the whole document; it just never looks
past `kpi`. The subtitle becomes run-aware:

- **Check** (no `doc.watch`): unchanged — `22 of 60 s sustained`, bar
  on the sustain fraction. Nothing a free user sees moves.
- **Watch, evidence gathering** (`doc.watch != null`, `sr.overall`
  INSUFFICIENT): subtitle
  `watch 4 min · stability evidence 98 of 600 s`, and **the bar
  tracks the 600-second floor** — tier 1 settled long ago, and the
  floor is the number the user is waiting on.
- **Watch, evidence sufficient**: subtitle
  `watch 3.2 h · stability: STABLE over 3.1 h` — the engine's own
  headline, prefixed — and the bar retires.

The vocabulary wall stands: the banner's **headline** remains tier 1's
word alone. Tier 2 appears only in the subtitle, named as itself
("stability: …"), which is the same separation the hub's cards keep.
Free never has `sr` in a running document's practical life (no watch),
so free's banner is untouched by construction; the code still guards
on the fields, not the edition.

### F3 — the Stability card stops promising what a check cannot keep

The card stays visible in both run types — a hub whose cards come and
go by mode was rejected once, rightly — but during a **check** it adds
one app-side line under the engine's headline:

> *A check ends before ten minutes — watch the station to gather
> evidence.*

On a watch the line disappears and the engine's countdown stands
alone, now echoed by the banner. The card learns which run it is in
from `doc.watch != null`, the same fact the banner uses — no new
state, no bridge change.

### Deliberately not done

- Hiding the card during checks (mode-dependent hub).
- Tier-2 words in the banner headline (the wall is load-bearing).
- A second progress bar (two bars is a dashboard, not a verdict).
- Any bridge or document change: this is presentation over facts the
  document already carries.

## Steps

### F1 — the hub row and the rename

Panel, registry line, string ("Watch station"), the relabel on the
analysis screen. **Verify** on the Huawei: the row starts a watch from
the hub (Watch card appears, Stop takes over); free's hub shows no
row; `checkEditionParity` passes.

### F2 — the run-aware banner

Subtitle branches and the bar's second duty, strings in `main/`.
**Verify** on the Huawei across one watch: subtitle shows the
evidence countdown with the bar climbing to the ten-minute floor,
then flips to the stability headline; a plain check's banner is
pixel-identical to today's.

### F3 — the card's check-hint

One conditional string. **Verify**: during a check the hint shows;
during a watch it does not.

### F4 — say so

Changelog (with the field-finding as the reason); wiki **Watch mode**
"Running one" section rewritten — start from the hub, the analysis
screen as the viewing room; the tier-2 section (T2.5, landing
together) describes the banner countdown. Screenshots for the store
are *not* retaken here: pro's listing refresh belongs to pro's
release, one item, not each.

## Built 2026-08-25 — F1-F3 plus four directives from live review

The author drove the build on the device as it happened, and four
directives landed alongside the plan:

* **Tier hints on the run verbs** — each button says which tier
  answers and what it costs in time, so the choice is made on the hub
  rather than learned by running the wrong one.
* **The analysis screen lost start/stop entirely** — one place for
  every verb; the plan's "alias" is gone, only the status line stays.
  The orphaned string and import went with it.
* **The verbs share the banner's 12 dp corners** — Material's pill
  grew tall and ugly under two-line labels; one shape family now.
* **The banner's evidence line was shortened** after wrapping with a
  lone "s" on its own line — the reconnect-wrap fault's second coming,
  caught by the author as "a rollover in the status badge".

And one wall: the author started a network-RTK check over a running
test and broke it. `startRun` had a silent `worker != null` guard; it
is now `worker?.isAlive` with a logged refusal — a dead thread cannot
wedge the door shut, and a second start cannot break a run. The
service is not exported, so the door is only reachable from inside the
app; the author's own route retried on this build is the closing
verification.

All seen on the Huawei: the three verbs with hints in one shape, the
un-wrapped banner counting evidence with its bar, the analysis screen
with only its status line, the check-hint present on a check and
absent on a watch.

## Relationship to T2.5

F4 and T2.5 landed together on 2026-08-25, one story in one wiki push:
what the watch is, how it starts from the hub, what the banner says
while it runs, what stability means when it settles. The changelog
carries the run-flow change with the field-finding as its reason and
the door-guard with its reproduction.
