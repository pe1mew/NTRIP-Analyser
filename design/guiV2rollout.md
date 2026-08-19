# GUI v2 — rollout plan

The Android app has two screens and a pager of three plots. Six roadmap
items want room, and three of them want a view of their own. This plan
takes the app from that shape to a hub with drill-down, ships the result
as the free edition, and then adds the paid capabilities one at a time
behind it.

The reasoning, the mockups and the four arrangements that were considered
are in **[gui-v2-study.html](gui-v2-study.html)** — a study drawn from the
code at 3.5.0, not a specification. This file is the decision and the
order of work.

## What is already decided

| Decision | Where it came from |
|---|---|
| **Layout D** — station screen as hub, everything else a drill-down | The study's four options; D is the only one that degrades cleanly between editions, and the only one whose cards can say *not yet* |
| **M2 for anything geographic** — tile-free, local metres, no basemap | Keeps the position recorded in `MapPick.kt`: no tile requests from this process, no OSM tile-policy exposure, no attribution, and Play data safety stays "no data collected or shared" |
| **Phase 1 is an overhaul, not a feature release** | The free edition gets the new layout and one new capability (share); everything else in phase 1 is the same measurement in a different frame |
| **Free is frozen after phase 1 until TLS** | TLS ships in both editions on the same day — [feature-matrix.md](feature-matrix.md), *the paid edition withholds convenience, never protection* |
| **Phase 2 order** | Tracks → VRS → hand-over → export → tier 2 → TLS, as listed by the author |
| **Free names what pro adds, once** — a single *More in Pro* card, no greyed rows | Chosen over five disabled cards: free advertises without shipping UI it cannot run |
| **One framework, two registries** — the shell is identical in both editions and only the panel *list* differs | Maintainability: a navigation or share fix is made once in `main/`, and the editions have nothing to drift apart with. Recorded in [android/design/editions.md](../android/design/editions.md) |

## Open decisions

Each blocks the step named. Recommendations are the plan's assumption if
no answer is given; where the plan proceeds on an assumption, it says so
at the step.

| # | Decision | Blocks | Recommendation |
|---|---|---|---|
| **D2** | What does share emit, and does it carry position? | P1.6 | Plain text + the current plot as PNG; **position only when the user typed it**, never the phone's |
| **D3** | Back stack: hand-rolled, or `androidx.navigation-compose`? | P1.1 | Hand-rolled list of destinations — no new dependency |
| **D4** | Does the phase-1 free build go through the running closed test? | P1.8 | Yes, same track; a layout overhaul is exactly what testers should see |
| **D5** | Does "frozen" allow crash and security fixes? | The freeze | Yes — freeze means *no features*, not *no maintenance* |
| **D6** | Is share built as the plug-in socket, or a one-off? | P1.2 | Socket. It is first on the roadmap, which is what makes it cheap to make it the integration point |
| **D7** | Version for the overhaul: minor or major? | P1.8 | Minor. `version.h` is shared by four programs and only one of them changes |

### How free and pro differ, given that the framework does not

The two requirements sound opposed — *free must not ship paid screens*
and *there must be one framework to maintain* — and the arrangement that
satisfies both is to make the **only** per-edition UI file a list.

```
  main/   panel contract · shell · navigation · hub · share socket
  free/   Registry.kt  ->  6 panels + MoreInPro
  pro/    Registry.kt  -> 11 panels
          VrsPanel.kt, HandoverPanel.kt, Tier2Panel.kt, ...
```

A pro panel's file lives in `src/pro/`, so the free APK contains neither
its screen nor its strings — the position in
[editions.md](../android/design/editions.md) is unchanged. Everything
that *arranges* panels is in `main/`, so a navigation bug or a change to
the share format is fixed once and both editions have it.

The one thing free says about paid capability is a **More in Pro** card:
a single entry at the bottom of the hub, registered like any other panel.
No greyed rows and no disabled controls — a disabled control is
indistinguishable from a broken one to somebody who has not paid, and
five of them turn the free app into a demo of the paid one.

The practical test of this arrangement, and it should be run at P1.3: a
diff of the free and pro builds' UI sources should show **one file
differing**. If a second one has to differ, the framework has leaked into
a flavor and the leak is the bug.

## Phase 1 — the overhaul

The output is the **free edition on the new layout**, with pro slots
prepared but not implemented. Pro is built from the same source set and
differs only by what its registry contains.

### P1.1 — A shell that can hold more than two screens

Split `MainActivity.kt` (2,147 lines: shell, both screens, seven cards)
so the shell owns navigation and nothing else. Introduce an explicit
destination stack — today `Screen` is an enum of two and back is a single
`BackHandler`, which cannot express *hub → detail → back*.

*Assumes D3.* A hand-rolled `List<Destination>` in state is about thirty
lines and adds no dependency; `navigation-compose` is more capable and
brings deep links, which nothing here needs yet.

**Done when** the app behaves exactly as before, with the pager and its
swipe-to-leave gesture intact, and a detail destination can be pushed and
popped.

### P1.2 — The panel contract

One file per capability, each contributing up to three things: a **hub
card**, a **destination**, and a **share section**. A registry lists the
panels; the source set decides which lines it contains.

*Assumes D6.* This is what makes phase 2 additive: a feature is one file
and one registry line, and it appears in the share output without editing
a share function.

**Done when** the existing cards — verdict, config summary, chips, KPI
rows, ephemeris, watch — are registered panels rather than inline
composables, and the screen is unchanged to the eye.

### P1.3 — The hub

Rebuild the station screen from the registry. Everything that ships today
keeps its position and order; the rule labelled *beyond the eight checks*
marks where phase-2 cards will land, so nothing above it moves later.

**Done when** a side-by-side against the current build shows no
difference above the rule.

### P1.4 — Analysis unchanged

Re-parent the pager as a destination. Three tabs, same order, same exit
gesture. No tab is added in phase 1 and none is added in phase 2 either —
tracks are drawn inside the sky canvas.

**Done when** the Android 12+ stretch-overscroll gesture still leaves the
screen on a modern handset. That gesture broke once before; it is the
first thing to re-test, not the last.

### P1.5 — The registries, and the More in Pro card

Two `Registry.kt` files, one per flavor, listing the panels that flavor
contains. Free's list ends with the **More in Pro** card; pro's does not
contain that card at all.

**Done when** building both flavors produces two apps whose UI sources
differ in exactly one file, and the free app names the paid capabilities
in one place and nowhere else.

### P1.6 — Share

An app-bar action on the hub and on every detail screen, emitting via
`ACTION_SEND` so mail and everything else receive it.

*Assumes D2.* The share output is assembled from the registered panels'
sections: verdict and the eight rows, stream identity, and — from a plot
screen — the plot as a PNG. **Position is a privacy decision, not a
formatting one**: the app's data-safety answer is "no data collected or
shared", which is about what *the app* transmits, but a share sheet is
the user handing a document to another app. The plan's assumption is that
a configured position may appear because the user typed it, and the
phone's live position never does.

**Done when** a shared report opens legibly in a mail client and in a
text editor, and contains nothing the user did not put in.

### P1.7 — Test (the gate)

"Extensively" needs a definition, or it means "until we are bored":

* **Regression**: every free capability behaves as it did on 3.5.0 — a
  station check reaching a verdict, the sourcetable readable but not
  selectable, RINEX import, settings, config summary, about.
* **Navigation**: hub → each detail → back, back out of analysis, system
  back at the hub, rotation and process death on every destination.
* **Small screens**: the hub scrolls and the cards read at 360 dp; the
  tab strip is untouched but must still fit.
* **Both themes**, since the mockups assume the app's own palette.
* **The gesture**, on the newest Android available — Android 12 changed
  overscroll once already.
* **Share**, into at least mail, a notes app and a file manager.

**Done when** all of the above pass on two handsets, one of them recent.

### P1.8 — Release free

*Assumes D4 and D7.* A minor version bump, the same closed-test track,
and the release notes describing a layout change rather than a feature
list.

### P1.9 — Freeze

*Assumes D5.* No new features in free until TLS lands. Crash and security
fixes continue.

## Phase 2 — the paid capabilities

Each lands as one panel: a card, a destination, a share section. The
order is the author's; the notes are what each needs beyond its own code.

| # | Capability | Lands as | Needs first |
|---|---|---|---|
| 1 | **Satellite tracks** | Inside the sky canvas — no card, no destination | Orbit history per SV; the ephemeris cache already exists |
| 2 | **VRS assertions** | Card *VRS · 5 of 5* → list-shaped detail | The five checks and the gate test exist in `vrs_check.c`; this is a bridge and a screen, not new measurement |
| 3 | **Rover-to-ARP, hand-over** | Card *Hand-over · N* → **M2** plot in local metres | ARP history across the session; a scale bar and north arrow; no map SDK |
| 4 | **Statistics export** | Extends the share sections into a file | Share must already be the socket (P1.2) — otherwise this is where the socket gets built anyway |
| 5 | **Tier 2 + thresholds** | Card *Stable · N h* → six metrics under their own verdict | `station_report.c` and `thresholds.c` are **not in the Android build** today; both must be added to `android/app/src/main/cpp/CMakeLists.txt`, and the foreground service must survive hours rather than minutes |
| 6 | **TLS** | No UI at all | [tls.md](tls.md): a bundled library behind a transport abstraction in the session layer. **Ships in both editions the same day, and ends the free freeze** |

### Two notes on the order

**Tier 2 is a second reader, not a second connection.** Watch mode already
holds the session open and `MonitorService` already keeps it alive. Tier 2
adds a consumer of the same snapshots — which is why the
foreground-service work belongs *with* it rather than after it: doze,
battery-optimisation exemption and a notification that stays meaningful
over hours are the same piece of work seen from the other end.

**Tier 2 also brings a fourth card state.** A verdict can be *withdrawn*:
since 3.5.0 the tier reverts to `INSUFFICIENT EVIDENCE` when the stream
clock stops for `stale_s`, after a monitored station published `STABLE
over 1.7 h` for fourteen hours past its last observation. The hub must be
able to show a verdict being taken back, not only reached — the four card
states are drawn in the study.

## Risks

* **The hub grows too long.** The claim the layout rests on is that
  nothing shipping today moves. If three more cards make the station
  screen unusable, the fallback is the study's option B — a separate
  report destination absorbing VRS, tier 2 and hand-over. Deciding that
  late is cheap *because* of the panel contract; deciding it late without
  the contract is a rewrite.
* **The framework leaks into a flavor.** The whole maintainability
  argument rests on one file differing between editions. The pressure to
  add "just this one free-only tweak" to a flavor will come, and each
  time it is accepted the editions gain a way to drift. The P1.5 check —
  diff the flavors' UI sources, expect one file — is what catches it, and
  it is worth running on every later phase-2 addition too.
* **The Android build's source list is hand-written.** Adding
  `station_report.c` and `thresholds.c` is a link-time change, so it fails
  loudly — but it is the same shape as the drift that once broke
  `service/Makefile`.
* **The closed test is running now.** Twelve testers for fourteen
  consecutive days is about testers staying opted in, not about which
  build they run; a new build mid-test does not reset the clock, but a
  broken one costs testers.

## Not in this plan

Bottom navigation (study option C) is the fallback if the hub fails its
own test, not a parallel track. A basemap (M3) is out until someone
decides the privacy position should change, on purpose and in
`MapPick.kt`. Keyboard-style navigation additions to the desktop GUI are
on [work-items/gui-track.md](work-items/gui-track.md) and unrelated.
