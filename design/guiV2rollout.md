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
| **A hand-rolled back stack**, not `androidx.navigation-compose` | D is a stack two deep, not a graph. See below for the two conditions attached and the one thing that would reverse it |
| **Share is a socket**, not a one-off — each panel contributes its own section, text first | Statistics export is already phase 2 item 4, so the second consumer is committed rather than hypothetical |

## The rest, settled 2026-08-18

Nothing in this plan is waiting on an answer. These four were carried as
open questions and were accepted as proposed; they are recorded here
rather than folded away, because each is a judgement someone could
reasonably have made differently.

| Question | Settled as | Why that way |
|---|---|---|
| **What share emits, and whether it carries a position** | Plain text in phase 1, the plot as PNG once a `FileProvider` exists. A **configured** position may appear; the **phone's** position never does | The user typed the configured one and can see it on screen. The live fix is something the app was trusted to read for the sky view, and it stays on the device |
| **Whether the phase-1 free build goes to the running closed test** | Yes, the same track | A layout overhaul is precisely what testers are for. The twelve-for-fourteen-days clock counts testers staying opted in, not builds, so a new build does not reset it |
| **What "frozen" means** | No features. Crash and security fixes continue | A freeze that blocks a crash fix is not a freeze, it is an outage waiting for a schedule |
| **Version for the overhaul** | Minor | `version.h` is shared by the CLI, the GUI, the daemon and both editions; one of the five changes, and semver describes the repository, not the excitement |

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

**A hand-rolled stack, decided 2026-08-18.** What D needs is five
destinations, two deep, with no arguments beyond *which detail*, no deep
links and one back stack. That is a stack, not a graph, and it is about
thirty lines:

```kotlin
@Serializable sealed interface Dest { ... }    // Hub, Analysis, Vrs, Handover, Tier2
var stack by rememberSaveable(stateSaver = DestListSaver) {
    mutableStateOf(listOf<Dest>(Dest.Hub))
}
BackHandler(enabled = stack.size > 1) { stack = stack.dropLast(1) }
```

`androidx.navigation-compose` was weighed and set aside. It is **not** a
dependency-count objection — the library makes no network requests, so
the reasoning that keeps a map SDK out (`MapPick.kt`) does not transfer,
and R8 would shrink most of its size away. It is that its three real
advantages do not apply yet: deep links, arguments, and multiple back
stacks. It also sits outside the Compose BOM, so its version becomes one
more thing to track.

Two conditions come with the decision, and they are the price of it:

* **`rememberSaveable` from the first commit.** The app uses it nowhere
  today, so rotation already loses the current screen — invisible at one
  level, a filed bug the moment a detail screen can be open. This is
  fixing an existing defect, not building a feature.
* **Predictive back is deferred, and recorded as deferred.** At
  `targetSdk = 36` a plain `BackHandler` still works but shows no back
  preview, which on a current handset reads as an app nobody has
  maintained. `PredictiveBackHandler` plus a preview animation is the
  eventual answer; skipping it silently is not.

**What would reverse this:** deciding that bottom navigation (study
option C) is likely rather than a fallback. Per-destination back stacks
are exactly what `navigation-compose` is for and exactly what is
miserable by hand. The switch stays cheap because destinations are
registry entries — swapping the mechanism touches the shell only.

**Done when** the app behaves exactly as before, with the pager and its
swipe-to-leave gesture intact; a detail destination can be pushed and
popped; and the open destination survives rotation and process death.

### P1.2 — The panel contract

One file per capability, each contributing up to three things: a **hub
card**, a **destination**, and a **share section**. A registry lists the
panels; the source set decides which lines it contains.

```kotlin
data class ShareSection(val title: String, val lines: List<String>)

interface Panel {
    fun card(doc: BridgeDocument): @Composable () -> Unit
    fun destination(): Dest?
    fun shareSection(doc: BridgeDocument): ShareSection?   // null = nothing to say
}
```

**Share is a socket, decided 2026-08-18.** The usual objection to a
plug-in point — one consumer does not justify a contract — does not hold
here, because the second consumer is already committed: *statistics
export* is item 4 of phase 2. The alternative is a `shareReport()` in the
shell that each of the five phase-2 panels edits in turn, which is how
`MainActivity.kt` reached 2,147 lines in the first place.

It costs perhaps forty lines more than the one-off and buys a property
that is otherwise hard to get: sections come out in registry order, which
is hub order, **so the emailed report reads in the same order as the
screen it came from**.

`ShareSection` is deliberately stupid — a title and lines of text. Three
of its five producers do not exist yet, and a dumb type is cheap to be
wrong about; anything richer (units, severity, structure) waits until a
second consumer actually asks for it.

Three consequences, all decided here:

* **Only the human-readable report needs panels.** A machine-readable
  export is `bridgeJson.encodeToString(doc)` — `BridgeDocument` is
  already a complete `@Serializable` snapshot — so data export needs no
  per-panel work at all. The socket exists for the prose, which is the
  part no model field knows how to write.
* **Share means *what you are looking at*.** On the hub, every section;
  on a detail screen, that panel's section alone. That is what the
  roadmap's wording asks for and what a user pressing share on the VRS
  screen expects.
* **Text first; attachments when something needs one.** There is no
  `FileProvider` in the manifest — `ConfigFile.kt` writes through SAF,
  which `ACTION_SEND` cannot reuse — so a PNG or a file attachment means
  adding a provider entry, `file_paths.xml`, a cache write and a URI
  permission grant. Plain-text share needs none of it. Phase 1 ships
  text; the provider arrives with the plot PNG or with export, whichever
  comes first.

**The build already had an opinion about this.** `checkEditionParity`
allowed an edition to carry `Features.kt` and nothing else, so the two
`Registry.kt` files failed the build on their first compile — the guard
doing exactly its job. It was widened in step with the decision rather
than switched off: `Features.kt`, `Registry.kt`, a `*Panel.kt` in pro,
`MoreInProPanel.kt` in free, and a new rule that catches the original
sin directly — **a flavor file that shadows a name in `src/main` is
always a stray**, whatever it is called. Both rejections were re-proved
against the widened task before it was accepted.

**Done when** the existing cards — verdict, config summary, chips, KPI
rows, ephemeris, watch — are registered panels rather than inline
composables, the screen is unchanged to the eye, and share emits a report
whose order matches the hub.

### P1.3 — The hub — **done 2026-08-19**

Rebuild the station screen from the registry. Everything that ships today
keeps its position and order; the rule labelled *beyond the eight checks*
marks where phase-2 cards will land, so nothing above it moves later.

**Verified twice, and the second one is the one that counts.**

*Structurally*: the order of card calls in the station body at `v3.5.0`
was extracted from the tag and compared against pro's `Registry.kt`.
Identical, all ten, in sequence — verdict, connection, browse, chips,
error, watch, KPI, run controls, ephemeris, hint.

*Visually*: the pre-P1.2 commit was built from a `git archive` export,
installed, photographed, then the current build was installed over it and
photographed again — same handset, same app data, same screen. The two
PNGs are **pixel-identical**; `ImageChops.difference` returns no bounding
box at all. A screenshot pair proves what a structural comparison cannot:
that the spacing survived the move of `Arrangement.spacedBy(12.dp)` and
the modifier chain from the old `Column` to the `StationHub` call site.

**The rule itself is deferred to the first phase-2 panel**, deliberately.
A separator above nothing is noise, and both registries are the same
today — there is nothing yet for *beyond the eight checks* to be beyond.
Pro's `Registry.kt` records the insertion point in a comment instead, so
the position is fixed without drawing a line that divides one thing from
nothing. It lands with `VrsPanel`.

*Method note for later phases*: build the reference from a short path.
The scratchpad's own path is long enough that ninja fails inside the
native build on Windows before it reaches a compiler.

### P1.4 — Analysis unchanged — **done 2026-08-19, one check outstanding**

Re-parent the pager as a destination. Three tabs, same order, same exit
gesture. No tab is added in phase 1 and none is added in phase 2 either —
tracks are drawn inside the sky canvas.

Re-parenting was already true after P1.1 (`Dest.Analysis`, pushed and
popped), so the work here was the other half of the shell reduction: the
pager moved out of `MainScreen` into `AnalysisScreen.kt`, taking what it
draws as parameters instead of closing over the shell's state. It no
longer knows about `MonitorService`, the settings or the back stack — it
is handed `onToggleWatch` and `onLeave` and decides nothing.

`MainActivity.kt` is now **638 lines**, from 2,143 before GUI v2: the
shell, the theme and `MainScreen`'s state.

The 96 dp swipe threshold moved to `SwipeThreshold` in `Navigation.kt`,
because it was written twice — once for the swipe into analysis and once
for the drag that leaves it — and a gesture that must feel the same at
both edges should not have its distance stated in two places.

**Verified on the device**: the screen renders as before, and the
right-swipe on the first page still leaves it. A full run then passed
end to end through the refactored path — `STATION OK`, held 60 s,
finished after 180 s, all eight rows — which is the evidence that
matters, because it exercises the hub, the registry, the analysis screen
and the service together rather than one of them at a time.

An earlier attempt in the same session failed with `Data arrived for
63 s, then the stream stopped`. That was a poor wifi link, not a
regression and not the caster: the same build passed on the next run,
and the 180 s to settle is the sustain window declining to pass a
station while the link wobbled. Worth recording because the failure
looked exactly like something the refactor had broken.

**Outstanding, and it needs the S23.** The handset here runs Android 10,
where the old overscroll glow only draws and the gesture always worked.
The case that broke before — Android 12+ consuming the leftover drag
with the stretch effect — cannot be reproduced on this device at all.
Until that is checked, this step is done but not proved.

### P1.4a — The landscape defect P1.1 exposed

Rotating in Analysis now keeps the user there, and the first thing that
does is show a bug nobody could reach before: **the sky plot collapses
to a dot in landscape**, header and legend taking the full width while
the polar plot is squeezed into what vertical space is left. Reproduced
on an SNE-LX1 at 2340x1080 with a live stream, 2026-08-19.

It is not a regression — `SkyView` has always sized itself from the
available height — but it becomes visible the moment the state survives
rotation, which is the same shape as the Log tab and the stdout pipe
earlier this month: a dead channel hides every bug downstream of it, and
fixing it delivers them all at once.

Two candidate fixes, to be chosen against the device rather than in the
abstract: give the plot a minimum size and let the screen scroll, or lay
the header and legend beside the plot when the viewport is short and
wide. `SignalBars` and `ElevationView` want checking in landscape at the
same time — nobody has seen those either.

**Done when** all three analysis views are usable in landscape on a
handset, and P1.7's landscape pass covers them.

### P1.5 — The registries, and the More in Pro card — **done 2026-08-19**

Two `Registry.kt` files, one per flavor, listing the panels that flavor
contains. Free's list ends with the **More in Pro** card; pro's does not
contain that card at all.

The card names what pro does **today** — several saved connections, a
mountpoint chosen by tapping it, watch mode, the ephemeris side-stream,
per-message statistics, config import and export — and says the eight
checks are identical in both. Not what is planned: advertising a
capability that does not exist yet is how a listing stops being
believed. The wording answers to
`docs/wiki/What-the-paid-edition-adds.md`, so the card and the page
cannot drift; `PRO_URL` leads there and lives in the card's own file,
because the paid build should not carry an address for advertising
itself.

It sits at the bottom, below the run controls. Somebody using the free
app came to grade a station; the advert should be what they find after
that is done, not what greets them.

**The gate had to be restated, and this is the correction.** It said
"the two flavors' UI sources differ in exactly one file". That was
already false once free gained the card, and it would have been false
again at every phase-2 panel — the real invariant is not *one file*, it
is *no two implementations of the same thing*:

```
free: Features.kt  MoreInProPanel.kt  Registry.kt
pro:  Features.kt                     Registry.kt
      ^^^^^^^^^^^ same name in both, and both are lists or flags
```

So the gate is now: **`checkEditionParity` passes**, which enforces
exactly that — a flavor file must be `Features.kt`, `Registry.kt`, a
`*Panel.kt` in pro or `MoreInProPanel.kt` in free, and may never shadow
a name in `src/main`. Everything that *arranges* panels stayed shared,
which is the property the original wording was reaching for.

**Done**: both flavors build, the guard passes, and `PRO_URL` is checked
against the published wiki page like every other link the app can open
(48 release checks now, up from 47).

### P1.6 — Share

An app-bar action on the hub and on every detail screen, emitting via
`ACTION_SEND` so mail and everything else receive it.

The output is assembled from the registered panels' sections, in registry
order (P1.2), and emitted as `text/plain` in phase 1.

**Credentials cannot leave, and that is structural rather than careful.**
`BridgeDocument` carries no credentials at all — they live in
`CasterSettings` — so a report built from the document cannot contain a
password by construction. The one panel that reaches outside it is the
config summary, which needs the caster and mountpoint names. The rule is
therefore: a section may name **caster and mountpoint, never username or
password**, and one test asserts it of the assembled output. With a
socket that test has one place to sit; with five hand-edited additions it
would need five.

**Position is a privacy decision, not a formatting one**: the app's
data-safety answer is "no data collected or shared", which is about what
*the app* transmits, while a share sheet is the user handing a document
to another app. A **configured** position may appear in a report, because
the user typed it and can see it on screen. The **phone's** position
never does: it is read for the sky view under a permission granted for
that, and it stays on the device.

**Done 2026-08-19.** Verified by sharing a real run out of the app and
reading what arrived:

```
NTRIP Analyser 3.5.0 — station report
2026-08-19 20:27:46

Verdict            FAILED · held 0 s of 60 required · run lasted 70 s
Stream             rfsee.net:2101/RFSEE01
Measured           0 B/s · 44 satellites · 404 frames · ARP 52,211516, 5,983710
The eight checks   1..8, each in the engine's own words
Orbits             140 cached · navigation file: APEL00NLD_R_...rnx.gz
```

In hub order, legible in a text editor, and the Stream section names the
caster and mountpoint and nothing else — from the one panel that holds
the settings and therefore had every opportunity.

**The credentials rule is enforced statically, not by a test, and that
is a compromise worth naming.** The plan said "one test asserts it of
the assembled output". The Android module has **no test infrastructure
at all** — no JUnit, no `src/test` — and adding it is a dependency
decision this plan never took. So `tools/check_release.py` reads every
`shareSection` in all three source sets and fails if one mentions a
username, a password, or the phone's own fix. It was proved by injecting
`"as ${s.username} / ${s.password}"` into `ConnectionPanel` and watching
the check fail, then removing it.

Static beats runtime here in one respect and loses in another: it cannot
be skipped, and it runs in CI on every push — but it reads source rather
than output, so a section that assembled a credential from parts would
pass it. **If JUnit is added later, this becomes a real assertion on
`buildReport`'s output**, and the static check stays as the cheap
backstop. 51 release checks now, up from 48.

**Deferred, as decided**: attachments. The plot as a PNG needs a
`FileProvider` the app does not have, and it arrives with whichever
needs one first — the sky plot or statistics export. Analysis therefore
has no share action yet: its natural artefact is an image, and offering
a button that silently emits text instead would be worse than not
offering one.

### P1.7 — Test (the gate)

"Extensively" needs a definition, or it means "until we are bored":

* **Regression**: every free capability behaves as it did on 3.5.0 — a
  station check reaching a verdict, the sourcetable readable but not
  selectable, RINEX import, settings, config summary, about.
* **Navigation**: hub → each detail → back, back out of analysis, system
  back at the hub, rotation and process death on every destination.
* **Small screens**: the hub scrolls and the cards read at 360 dp; the
  tab strip is untouched but must still fit.
* **Landscape**, on every destination and all three analysis views —
  newly reachable since P1.1, and newly worth testing for that reason
  (P1.4a).
* **Both themes**, since the mockups assume the app's own palette.
* **Rotation and process death on every destination** — the hand-rolled
  stack has to restore where you were, and nothing in the app does that
  today. *Developer options → Don't keep activities* is the check.
* **The gesture**, on the newest Android available — Android 12 changed
  overscroll once already.
* **Share**, into at least mail, a notes app and a file manager — from
  the hub *and* from a detail screen, since they emit different things.
* **No credentials in the share output**, asserted by a test over the
  assembled report and not by reading it once (P1.6).

**Done when** all of the above pass on two handsets, one of them recent.

### P1.8 — Release free

A **minor** version bump — `version.h` is shared by four programs and one
of them changes — onto the **same closed-test track** the free edition is
already in. A new build does not restart the twelve-testers-for-fourteen-
days clock, which counts testers staying opted in; a broken one costs
testers, which is what P1.7 is for.

Release notes describe a layout change, not a feature list. The one new
capability is share; everything else is the same measurement in a
different frame, and saying otherwise invites testers to look for
features that are not there.

### P1.9 — Freeze

No new features in free until TLS lands. **Crash and security fixes
continue** — a freeze that blocks a crash fix is not a freeze, it is an
outage waiting for a schedule.

The freeze has a definite end rather than a vague one: TLS ships in both
editions on the same day ([feature-matrix.md](feature-matrix.md)), so
phase 2's last item is also what releases free from the freeze. If TLS
slips, the freeze slips with it, and that is the cost to weigh when
ordering phase 2 — not a reason to let free drift out of step with pro
in the meantime.

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
