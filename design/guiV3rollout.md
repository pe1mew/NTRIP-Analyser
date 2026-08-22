# GUI v3 — rollout plan

What to build, in what order, to take the app from 3.6.0 to the template
in [`guiV3spec.md`](guiV3spec.md), in both editions at once, plus the
failure reporting the same review asked for.

The specification says what the screens are. This file says what gets
changed, in which file, and how each step is proved before the next one
starts — the shape that worked for v2 in [`guiV2rollout.md`](guiV2rollout.md).

## What this changes about an earlier decision

v2 recorded: **"Free is frozen after phase 1 until TLS."** This plan
re-opens free — the whole point of the review is that the two editions
must look like one app, and a template applied to pro alone would make
that worse, not better. The freeze stands for *capabilities*: free gains
no measurement here, and the eight checks and their verdicts stay
identical in both editions. What free gains is the frame, and the
failure sentences, which are a correction rather than a feature.

Target version: **3.7.0**. It is a MINOR bump — new failure fields in the
snapshot, no behaviour removed.

## Open decisions

Nothing below is blocked on these; each has a recommendation, and the
plan assumes the recommendation until told otherwise.

| # | Decision | Recommendation |
|---|---|---|
| **D1** | Where the orbit-source link goes when the `Phone GNSS` badge is removed (spec §7.1) | Make the **summary line tappable** where it names the source. The badge exists to explain provenance; the line already states it, and a link on the words is where a reader looks |
| **D2** | Whether the failure sentence lives in the verdict card's sub-line or in its own row under it (spec §5.5) | **Verdict sub-line.** One place to look after a run, and the sub-line is empty when a run fails, since there is no sustain count to show |
| **D3** | Does a folded-open KPI row stay open across rotation and across a new run? | **Across rotation yes, across a run no.** `rememberSaveable` for the first; a new run resets, because the row's content changed underneath |
| **D4** | The daemon's CSV gains a `failure` column | **Append at the end.** Additive for every positional reader; note it in `changelog.md` under a *format* heading so a Munin user reads it |
| **D5** | Do the CLI and Win32 GUI adopt the new sentences in 3.7.0, or later? | **Same release.** The wording lives in the core; a frontend that does not print it is the odd one out, and the CLI's `--check` output is where most of this project's own debugging happens |
| **D6** | `Affordance` as an enum on `Panel`, or a `Boolean expandable` plus the existing `destination()`? | **Enum.** Four states exist in the slides (`▶ ▼ ▲` and none) and three of them are not derivable from `destination()` |

---

# Phase 1 — the shell

The frame first, empty. Nothing about a panel changes in this phase; if
it does, the phase has leaked.

### P1.1 — One top bar, three callers — **done 2026-08-21**

**Goal.** A single `AppBar` composable in `main/`, used by the hub, the
analysis screen and every detail screen, with a leading slot that is
empty or `←`.

**Files.** New `Shell.kt`; `MainActivity.kt` (hub), `AnalysisScreen.kt`,
`Panel.kt` (`DetailScreen` owns a back bar today and stops owning one).

**Why one composable rather than three that agree.** Three that agree
drift. The review that produced this plan exists because they already
did: the hub grew a `☰`, the analysis screen grew a `Phone GNSS` pill,
and neither knew about the other.

**Verify.** Every screen shows the app name as title; the analysis screen
shows `←` where it showed `Back`; the hub's leading slot is empty. Prove
the sharing is real: change the title string once and see all three
screens change.

**Done.** `Shell.kt` holds `AppScaffold`, and it takes **no title
parameter** — the app's name is read inside it, so a caller cannot pass
a different one. The hub, the analysis screen and `DetailScreen` all go
through it.

The falsification ran on the device: `app_name` was changed to
`TITLE TEST PRO` in the pro flavour alone, and both reachable screens
came up under that name — the hub and, mid-run, the analysis screen.
Reverted, rebuilt, both editions reinstalled.

Three things fell out of it that the step did not plan for:

- **`DetailScreen` had no live caller.** No panel overrides
  `destination()` at 3.6.0; it is the socket phase 2 fills. So the third
  caller compiles and is not yet observable, and the marker for it in
  P2.3 is where it first will be.
- **The detail screen loses its own title**, because the bar now carries
  the app's name on every screen. `panel.detailTitle()` becomes the first
  line of the content instead, which is where a reader looks anyway.
- **The orbit badge had nowhere to go.** The template's bar has four
  slots and none of them is a badge, so it moved into the content,
  right-aligned, until P3.2 folds what it says into the summary line.
  Detail screens gained share as well: they send the report, since
  whoever asks for one section wants the run it came from.

### P1.2 — The overflow menu — **done 2026-08-21**

**Goal.** `⋮` on the right of the top bar, on every screen, carrying
Settings, Import RINEX, Load/save configuration (pro), About/notices.
`☰` is deleted.

**Files.** `Shell.kt`, `MainActivity.kt`, both `Registry.kt` files if the
menu is registry-driven — see the note below.

**Note, and a decision inside the step.** The menu is short and static.
Do **not** build a second registry for it: four rows, one of which is
`Features.IS_PRO`-gated, is a `when` in one file. A registry earns its
keep when the list is open-ended, and this one is not.

**Verify.** Both editions: the pro row is absent in free, present in pro.
`checkEditionParity` still passes.

**Done.** The menu is not a slot in the end. `AppScaffold` takes a
`MenuActions` — five lambdas saying what the rows *do* — and builds the
`⋮` itself, so a screen cannot offer a different menu from the next one,
and cannot forget to offer one. The hub owns the state and the file
pickers behind those lambdas and hands the same object to the analysis
and detail screens. The menu's own open/closed flag moved into
`OverflowMenu`: three screens showing one menu should not each carry a
boolean for it.

On the device, pro's menu reads *Caster settings…, Import RINEX
navigation file…, Load configuration…, Save configuration…, About and
help*; free's reads the same list without the two configuration rows.
The analysis screen's accessibility tree now answers **Back, Share,
Menu** — the template's four slots, on a second screen.
`checkEditionParity` passes; `check_release.py` still agrees on 51
checks.

### P1.3 — The analysis bar — **done 2026-08-21**

**Goal.** A bar pinned to the bottom of the hub, grey when there is
nothing to look at, emphasised with `▶` when a run is live or results
exist, and absent on every other screen.

**Files.** `Shell.kt`, `MainActivity.kt`, `HubPanels.kt`
(`RunControlsPanel` loses the **Analysis** button and keeps Run/Stop).

**Verify.** With no run: grey, inert. During a run: emphasised, opens the
plots. On the analysis screen and on a detail screen: not drawn. Scroll
the hub to the bottom during a run — the bar does not move.

**Done.** `AppScaffold` takes an `AnalysisBar` -- enabled, and what to
open -- and draws it in the `Scaffold`'s bottom slot. Passing `null`,
which every screen but the hub does, is how the template's *the bar
disappears on any other screen* is expressed: not a flag to remember,
but a parameter no other screen fills. `RunControlsPanel` keeps Run and
Stop, now full width, and has given up the **Analysis** button it used
to hold.

Seen on the device in all three states: muted with `▶` and inert on a
hub with nothing measured; black-outlined and live during a run, with
KPI rows scrolling under an opaque bar that does not move; and gone on
the analysis screen it opens, whose controls are Back, Share and Menu
alone.

The permission dance stayed where it was. The bar calls
`hubActions.openAnalysis()`, which is the only thing that knows the sky
view needs a position before it can draw one.

---

# Phase 2 — the affordance grammar

### P2.1 — `Affordance` in the contract — **done 2026-08-21**

**Goal.** `enum class Affordance { NONE, FORWARD, EXPAND, COLLAPSE }`,
`Panel.affordance(state)` with a default derived from `destination()`,
and the marker drawn by `StationHub` — not by any card.

**Files.** `Panel.kt`, `HubPanels.kt`, both `Registry.kt` (no change
expected: that is the test of the default).

**Verify.** Falsifiable: give a panel `NONE` and watch its triangle
vanish without touching the card's own code. Screenshot the hub at
`READY` and at `RUNNING` and compare against Dia2 and Dia4 row by row.

**Done.** `Affordance { NONE, FORWARD, EXPAND, COLLAPSE }` with
`Panel.affordance(state)` defaulting to `FORWARD` where a panel has a
screen behind it, and `StationHub` drawing the mark in the same place it
already adds the tap — right-aligned, vertically centred, `12.dp` in,
with no touch target of its own.

It takes the state, and that turned out to matter immediately:
`BrowsePanel` disappears while a run is going, and a mark that did not
know would hover over the gap where its row used to be.

The falsification ran on the device and left a pixel proof. With
`ConnectionPanel` returning `NONE` its triangle is gone while Browse
keeps its own; restoring `FORWARD` gives a screenshot **pixel-identical**
to the first, in the region where the mark sits. `ConfigSummary`, which
draws that card, was never opened.

Three marks were deliberately *not* set here:

- **The KPI rows** carry their own `▼` inside one panel, and one mark for
  eight rows would be a lie. Per-row marks are P2.2.
- **Run and Stop** wear transport marks in the author's Dia4 — `▶` to
  start, `■` to stop — which is a different vocabulary from the
  affordance grammar and belongs on the button's own label. P2.3.
- **More in Pro** has a link *inside* its card rather than a tappable
  card, so it is honestly unmarked until P2.3 decides whether the whole
  card should lead to the listing.

### P2.2 — The rows that fold — **done 2026-08-21**

**Goal.** `▼` / `▲` behaviour on the KPI rows, Satellite orbits and (pro)
Watch: fold open in place, marker flips, state survives rotation and
resets on a new run (D3).

**Files.** `HubPanels.kt`, `StationCards.kt`.

**Verify.** Rotate with three rows open; run again and see them closed.

**Done.** `FoldableCard` gives the watch card and the orbits card a
header that always shows and a body that folds; the KPI rows keep their
own fold and now wear the same mark as everything else, from the same
renderer.

**Where the fold state lives changed twice, and the second answer is the
one that works.** A row's own `rememberSaveable` dies with the row, and
rows do leave: when a run ends the hub is rebuilt around a finished
document, and every fold the reader had opened while watching the run
shut itself at the moment they wanted to read it. `FoldState` — a map
above the rows, keyed by row identity, cleared from an effect when a new
run starts — survives that, and survives rotation for free.

Proved on the device: one row open, watch the run end, still open;
**Run again**, and it is shut.

Three defects were found on the way, two of them mine from P2.1:

- **The hub stacked every panel that drew more than one card.** P2.1
  wrapped each panel's content in a `Box` to hang the mark on, and a Box
  stacks its children: all eight KPI rows landed on the same spot with
  row 8 on top. It survived P2.1's review because that screenshot was
  taken at `READY`, where every panel draws exactly one card. The
  content now goes in a `Column` inside that Box.
- **The hub's rhythm depended on which panels had something to say.** An
  empty panel still occupied a slot, and `spacedBy` put a gap around it.
  `StationHub` is now a small `Layout` that places only children with
  height and puts one `HUB_GAP` between neighbours — the same gap the
  KPI rows use, so the screen keeps a single vertical beat.
- **The marks did not line up.** `AffordanceMark` carried its own inset,
  and two of its three callers sit inside a padded row already, so those
  marks sat 12.dp further in. The inset moved to the hub's call site:
  every card mark now ends at the same x, measured from the
  accessibility tree.

### P2.3 — The rows that lead — **done 2026-08-22**

**Goal.** `▶` on Connection, Browse, Run/Stop, More in Pro — each already
does something; this makes it say so.

**Files.** `HubPanels.kt`, `StationCards.kt`.

**Verify.** Every card in both editions either carries a marker or is
provably not clickable. A card with a click target and no marker is a bug
this step exists to remove.

**Done.** Connection and Browse were marked in P2.1; this step finished
the two that were left and made the whole column line up.

- **Run and Stop** wear the template's transport marks — `▶` to start,
  `■` to stop, from Dia2 and Dia4 — inside the button, at its right
  edge. These are not affordances and are not drawn by the hub: a
  transport mark says what the control *does*, where an affordance says
  where the row *goes*.
- **More in Pro** became a row that leads. The link was a `TextButton`
  buried in the card, which is a row whose mark would have been a lie;
  the whole card opens the listing now and carries `▶` like any other
  row that leads somewhere.

**The marks did not share a column, twice over, and the author caught
both.** A Material button pads its content by 24.dp, so the transport
mark sat 12.dp inside every other mark; the analysis bar's own inset put
its mark somewhere third. Both now end where the card marks do.

Measured from the accessibility tree, free's hub at `READY`: the right
edge of **every** mark on the screen — connection, browse, run, More in
Pro and the analysis bar — is `996`, a single value across the whole
tree.

**The audit.** Seven clickable regions on free's hub: four cards, each
marked; the two glyph controls in the top bar, which are controls rather
than rows; and the analysis bar, marked. During a run the orbits card
and every KPI row are marked `▼`. No card has a click target and no
mark.

---

# Phase 3 — the analysis screens

### P3.1 — Six bands

**Goal.** Tabs, explainer, summary, plot, footer, legend, in that order,
with the plot taking the slack (spec §4).

**Files.** `AnalysisScreen.kt`, `Views.kt` (a shared `AnalysisBands`
frame so the three views cannot each invent their own).

**Verify.** All three tabs against Dia6, Dia7, Dia8. The tab row is
directly under the app bar in every one.

### P3.2 — The badge retires

**Goal.** `Phone GNSS` pill removed; provenance in the summary line;
the wiki link on the words (D1).

**Files.** `AnalysisScreen.kt`, `Views.kt`, `strings.xml`.

**Verify.** Sky view with orbits from the stream, from RINEX and from the
phone: three different summary lines, each naming its source, each
leading to the orbits page.

### P3.3 — Small screens

**Goal.** The `PlotLayout` exception from v2's P1.4a survives the
re-banding: below the height threshold the screen scrolls rather than
crushing the plot.

**Verify.** The 5.4-inch profile, all three tabs, portrait and landscape.

---

# Phase 4 — the core learns why

No UI in this phase. It is C, it is testable without a phone, and every
frontend gets it at once.

### P4.1 — `NsFailure` and the mapping

**Goal.** The enum (spec §5.2), and one `errno`/`WSAE*` → `NsFailure`
mapping in `src/net`.

**Files.** `src/session/ntrip_session.h` (enum), `src/net/ntrip_handler.c`
(detection, replacing four `fprintf(stderr, …)` sites that currently
discard it), `src/net/ntrip_proto.c` (status → failure).

**Verify.** New `test/test_failure.c`, built on the loopback caster that
`test_stall.c` already uses: refuse the connection, answer with 401, with
403, with 404, with an `ENDSOURCETABLE`, and with a plain HTTP page.
Six assertions, no network.

### P4.2 — Carrying it

**Goal.** `NsStatsSnapshot::failure` and `failure_detail`, in the JSON
and appended to the CSV (D4); the code on the `NS_EV_DISCONNECTED` event
beside the existing `NsEndReason`.

**Files.** `src/core/ns_stats.{c,h}`, `src/session/ntrip_session.c`.

**Verify.** `test_ns_stats`'s round-trip covers the new fields. The stale
danger here is a field nothing fills — the gotcha log has that one three
times — so the test asserts a **non-zero** failure for each of P4.1's six
cases, not merely that the field serialises.

### P4.3 — KPI 1 says it

**Goal.** Where KPI 1 says `No connection to the caster`, it says which
of the eleven it was. Verdict words unchanged.

**Files.** `src/core/kpi.c`.

**Verify.** `test_kpi_stopped` extended; the CLI's exit codes are
re-checked against a caster that refuses, to prove the verdict vocabulary
did not move.

### P4.4 — The other three frontends

**Goal.** CLI, Win32 GUI and daemon print the sentence (D5).

**Files.** `src/cli/cli_stream.c`, `gui/gui_events.c`, `service/`.

**Verify.** `ntrip-analyser --check` against a wrong port, a wrong
password and a wrong mountpoint: three different messages, three
identical exit codes.

---

# Phase 5 — the app says why

### P5.1 — Across the bridge

**Goal.** The failure code reaches Kotlin — through the snapshot JSON,
which the app already parses, rather than through a second channel.

**Files.** `android/app/src/main/cpp/ntrip_bridge.c`, `Model.kt`.

**Verify.** Wrong password against the author's own caster: the parsed
document carries `failure = 6`.

### P5.2 — Rendering it

**Goal.** Eleven strings in `strings.xml`; the sentence in the verdict
sub-line (D2); the failing row marked and tappable straight to the field
at fault (spec §5.5).

**Files.** `strings.xml`, `HubPanels.kt` (`VerdictPanel`, `ErrorPanel`),
`Dialogs.kt` (open focused on a given field).

**Verify.** Five deliberate faults on the device — bad host, bad port,
bad password, bad mountpoint, caster stopped mid-run — five different
sentences, each leading somewhere useful. Screenshot each; they belong in
the wiki page.

### P5.3 — `err_open` retires

**Goal.** Delete the string and its call site. Nothing may fall back to
*Could not open the session.*

**Verify.** `grep -r err_open android/` returns nothing. If a path exists
with no failure code, it is a missing mapping, not a reason to keep a
catch-all.

---

# Phase 6 — parity, docs, release

| Step | What |
|---|---|
| **P6.1** | `checkEditionParity` and `tools/check_release.py`: the new strings exist in both editions; no `shareSection` gained a credential; the check count is still eight |
| **P6.2** | Wiki: **Troubleshooting** gets the eleven sentences and what to do about each — it is the page a user reaches from a failure; **The analysis views** and **Getting started** get the new screenshots |
| **P6.3** | `changelog.md` 3.7.0: the frame, the failure taxonomy, and the CSV column under a *format* heading |
| **P6.4** | `design/gui-design.md` and `android/design/editions.md` updated so the next reader finds the template, not v2's hub description |
| **P6.5** | Release: tag, Windows assets, Play closed test — the author's, per `docs/RUNBOOK.md` |

---

## Order, and what can run beside what

Phases 1–3 are the app; phase 4 is the core; they do not touch the same
files and can be worked in either order. Phase 5 depends on 4, and on 1
for the top bar it renders into.

The one sequencing rule worth stating: **do not start phase 5 before
phase 4's tests are green.** A failure sentence that the phone shows and
the core cannot reproduce is a sentence with no test behind it, and the
gotcha log's oldest entry is about exactly that.

## Effort, honestly

| Phase | Size | Risk |
|---|---|---|
| 1 — shell | small | low; mechanical, high reward |
| 2 — affordances | small | low; the contract does the work |
| 3 — analysis bands | medium | the small-screen exception is fiddly and already bit once |
| 4 — core failures | medium | the platform error mapping needs both Windows and POSIX proved |
| 5 — app failures | small | depends entirely on 4 |
| 6 — parity and docs | small | mostly writing |

---

*Written against 3.6.0 (`19d362e`). The specification it implements is
[`guiV3spec.md`](guiV3spec.md); the template it implements is in
[`work-items/guiReview/`](work-items/guiReview/).*
