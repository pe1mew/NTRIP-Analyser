# Memory

<!-- NOT loaded automatically. A task-triggered pointer in CLAUDE.md is what
     brings it into a session; without that row nothing here is ever read,
     and the failure is silent.

     Keep it lean — it is read in full whenever it is reached, and it is an
     INDEX: deep knowledge belongs in topic files, and most of this project's
     deep knowledge already lives in design/ and docs/. Point at those rather
     than restating them, or the copy drifts from the original.

     END-OF-SESSION CURATION (/curate):
     1. Review gotcha-log for recurring patterns — promote them here or to a topic file
     2. Retire entries that are fixed, refactored away, or now encoded in code
     3. Update "Current State" to reflect what shipped or changed
     Monthly (/audit-context): prune as much as you add.

     VERIFIED CLAIMS:
     A claim about the state of things decays the moment it is written —
     "two tests" survived here for a day after there were five. So each
     one that can be checked carries the command that checks it:

         <!-- verify: <shell command, exit 0 = still true> -->
         <!-- verify: manual — why no command can settle it -->

     `python tools/verify_memory.py` runs every one of them and reports
     PASS / FAIL / ERROR. A FAIL means the sentence above it is now
     false: fix the sentence, not the command. Add a command to any new
     claim, or say plainly that it is manual — an unmarked claim is one
     nobody will ever re-check. -->

## Topic Files

| File | When to load | Key insight |
|------|-------------|-------------|
| `memory/gotcha-log.md` | Stuck, or something behaves impossibly | Problem→root cause→fix archive; six entries are resolved history |
| `memory/doxygen-in-headers-only.md` | Writing or moving documentation comments | Doxygen merges header and `.c` blocks — document the declaration only |
| `design/architecture.md` | Touching `src/core`, `src/session`, or adding a frontend | Why the session layer exists and what the snapshot guarantees |
| `design/todo.md` | Asking "does X already exist?" | Shipped vs planned, stable item numbers, and rejected ideas with reasons |
| `design/feature-matrix.md` | Asking "which product has X?" | Every feature against CLI, GUI, free, pro and the daemon, with the rationale for each split |
| `design/work-items/{release-to-play,cli-track,gui-track,measurement-tiers}.md` | Picking up work | Four parallel tracks — Android, CLI, GUI, and one that follows a capability rather than an artefact |
| `design/kpi-candidates.md` | Proposing a new KPI | Why only one of four candidates was a KPI, and the two-tier answer that came out of it |
| `design/work-items/measurement-tiers.md` | Anything about KPIs or long-run measurement | Two tiers: the 90-second fitness check, and a stability report over hours. Only latency earns a ninth KPI |
| `design/gui-design.md` | Any `gui/` work | Window patterns; §13-§15 are the check, the stability window and threshold loading, as built |
| `android/design/editions.md` | Any Android product decision | Free/pro split, payment model, profiles, GGA position sources |
| `android/design/views.md` | Android UI or sky-plot work | What each view answers, and where orbits actually come from |
| `android/design/design-review.md` | Changing anything cited as `design-review Dn` | Decisions D1–D7, dated, referenced from seven code sites |
| `docs/jsonConfigs.md` | Config reading, writing or interop | One format everywhere; passwords in the clear |
| `design/security-review.md` | Touching a parser, the socket layer, or anything a caster feeds | What a hostile caster can do; six findings closed, TLS scheduled |
| `design/tls.md` | Implementing TLS, or asked why it is not there yet | The decision, the measured surface, and what actually costs |
| `design/legacy-observations.md` | Touching KPI 4, KPI 5, or anything that decides which messages count | Delivery is judged against the sourcetable, so an old GPS+GLONASS station passes. Built 2026-08-13 |
| `docs/wiki/` | Changing anything a user sees, or wondering what they were told | Twelve published pages; the app links into them, so a claim here is a claim in the product |

## Current State

<!-- 2026-08-25 -->

- **Phase 2 is five of six: only TLS remains.** VRS check, hand-over,
  <!-- verify: grep -c "done 2026-08" design/work-items/tier2-on-the-phone.md -->
  statistics export and tier 2 all shipped to `main` (pro-gated) in
  one day each, every plan in `design/work-items/*-on-the-phone.md`
  recording what its steps actually did. **Pro still does not go to
  Play until TLS lands** -- the author's hold stands, and TLS also
  unfreezes free.
- **3.7.3 released** (2026-08-25, free to Play + GitHub with both
  <!-- verify: grep -q "## .3.7.3." changelog.md -->
  platforms' assets): the run-flow corrections from tier 2's first
  field use -- the hub owns every run verb with a tier hint, the
  analysis screen starts and stops nothing (author's one-place rule),
  the banner counts the stability evidence floor, a second start
  cannot break a running measurement -- plus the sky-header source
  fix. 3.7.2 (same day) shipped GH#2/GH#3; both issues closed.
- **The issue tracker is live**: #1 is a PR; issues start at #2.
  Backlog items carry `[GH#n]` tags (`design/todo.md`).
- **check_release.py is at 98 checks**; the gate-table guard has
  <!-- verify: python tools/check_release.py > /dev/null 2>&1 || test $? -eq 1 -->
  collected on every new Features flag (tracks, VRS, hand-over,
  export, tier 2) before the commit that introduced it went green.
- **Fifteen C tests**, including the desktop-built bridge harness
  <!-- verify: test "$(ctest --test-dir build -N 2>/dev/null | sed -n 's/^Total Tests: //p')" = 15 -->
  (`test_bridge_vrs.c`) that drives the phone's own plumbing over a
  loopback socket with a synthetic clock.

<!-- 2026-08-23 -->

- **v3.7.1 released** (2026-08-23) to Play and to GitHub, both platforms'
  <!-- verify: grep -q '^## .3.7.1.' changelog.md -->
  assets attached. It fixes the sky view's legend being drawn behind the
  navigation buttons on a phone with three-button navigation -- **a fault
  whose fix was already inside the v3.7.0 tag**. What reached Play was a
  bundle built four and a half hours earlier: the Android release is
  built by two commands, one for the APK and one for the bundle, and only
  the first was re-run. `tools/check_release.py` now compares every
  artefact under `app/build/outputs` with the sources it came from and
  with this tree's version, and is at **79 checks**; the runbook builds
  APKs and bundles in one command. Expect those four checks to be red
  between an edit and a rebuild -- that is what they are for.
  <!-- verify: grep -q 'def check_artefacts' tools/check_release.py -->
- **The analysis plots belong to the run, not to the screen** (2026-08-23).
  <!-- verify: grep -q 'val tracks = TrackAccumulator()' android/app/src/main/java/nl/pe1mew/ntripanalyser/MonitorService.kt -->
  Both accumulators live in `MonitorService`'s companion, cleared at run
  start and fed where the document is published. Before this a rotation
  reset them and a stopped activity fed them nothing at all, so a
  nine-hour capture held the minutes its screen happened to be on.
  Satellites only the handset can place are still fed by the UI -- one
  satellite, one source. Measured: 45 samples/s off screen, unchanged
  across a rotation.
- **Satellite tracks (pro)**: trails on the sky plot, a point a minute,
  <!-- verify: grep -q 'HAS_TRACKS' android/app/src/pro/java/nl/pe1mew/ntripanalyser/Features.kt -->
  a **day** per satellite, arcs broken at a five-minute gap and at the
  azimuth wrap, drawn thinner than the markers and built once a document
  rather than once a frame. Confirmed on hardware at 40 minutes.
  Unreleased: **pro does not go to Play until the last feature lands.**
- **The Watch card says when the stream dropped** (2026-08-23): the app
  <!-- verify: grep -q 'watch_reconnects' android/app/src/main/res/values/strings.xml -->
  has always reconnected by itself (1 s doubling to 60 s, both editions,
  set in `ntrip_bridge.c`) and never said so. Read the count *with* the
  degradations line: degradations with reconnects is a link that dropped,
  without is a station that faltered.

<!-- 2026-08-22 -->

- **v3.7.0 released** (2026-08-22): the Android app is drawn in one
  <!-- verify: grep -q '^## .3.7.0.' changelog.md -->
  template, and a stream that will not open says which thing is wrong.
  The frame is `design/guiV3spec.md`, built to `design/guiV3rollout.md`,
  from the author's own review in `design/work-items/guiReview/`. One app
  bar with **no title parameter**, one overflow menu, an analysis bar
  pinned to the hub, a mark on every touchable row drawn from the panel
  contract, and six fixed bands on the analysis screens.
  **Twelve failure codes** classified once in `src/core/ns_failure.c` --
  wrong address versus wrong port, wrong password versus wrong
  mountpoint -- carried in the snapshot, said by all four frontends, and
  in the app leading to the field at fault. `err_open`, the one sentence
  that stood for every fault, is gone. Thirteen tests; `check_release.py`
  is at 70 checks, up from 51.
- **Both editions' Play screenshots show the v3 layout** (2026-08-22),
  <!-- verify: manual — the images are what they are; a checksum proves nothing about what they show -->
  re-taken from runs that pass, and `tools/make_store_shots.py` now
  refuses to write when a redaction box has drifted off the line it
  hides. It had drifted: the first framing exposed the caster address
  and the station's ARP.

Older entries: [sessions up to 2026-08-20](sessions-to-2026-08-20.md).

## Recently Promoted

<!-- Format: "if [situation], then [what to do] — promoted from gotcha-log YYYY-MM-DD"
     Retire an entry as soon as it appears in its destination. -->

- If a **script must rewrite a file**, read and write with `newline=''` and
  convert line endings once — never let the script re-encode what it wrote
  (escapes 2026-08-12, doubled carriage returns 2026-08-14) —
  promoted from gotcha-log 2026-08-14 to `CLAUDE.md` hard constraints.
- If a **rendering fault comes from the data's own resolution**, fix every
  frontend, not the one it was reported in (Android 2026-08-13, GUI
  2026-08-14) — promoted from gotcha-log 2026-08-14 to Active Decisions
  above.
- If you are about to **report a count from a build** — warnings, tests,
  symbols — produce it the way the build produces it: same standard, same
  optimisation (`-fsyntax-only` is blind to the truncation warnings;
  `-std=c99` hides `M_PI`, 2026-08-15) — promoted from gotcha-log
  2026-08-15 to `CLAUDE.md` hard constraints.
- If you **add a field to the snapshot schema**, fill it in the same
  change or do not add it (ARP fields, then `latency_s` 2026-08-16) —
  promoted from gotcha-log 2026-08-16 to Active Decisions above.
- If a defect is **reported against a deployed artefact**, ask that
  artefact its version before reading any code (pro's APK 2026-08-14,
  the VPS binary 2026-08-16) — promoted from gotcha-log 2026-08-16,
  generalising the Android-only wording in Active Decisions above.

## Key File Paths

Supplementing CLAUDE.md's list with paths found during work:

- `src/core/ns_stats.{c,h}` — the snapshot every frontend renders; `null` in
  its JSON means "not measured", so Kotlin models must be nullable.
- `src/net/ntrip_handler.c` — derives Basic auth when `AUTH_BASIC` is empty;
  a sourcetable fetch fails without it.
- `gui/gui_parsers.c` — sourcetable rows, advertised types, handshake parsing.
- `android/app/src/main/java/nl/pe1mew/ntripanalyser/Settings.kt` — encrypted
  profile store and the migration from the pre-profiles preferences file.
- `service/monitord.example.json`, `bin/exampleConfig.json` — the shared
  config format, daemon side and desktop side.
- `tools/make_icons.py` — the only place the icon exists; the `.ico`, the
  launcher bitmaps, the adaptive vectors and the store assets are output.
- `tools/check_release.py` — compares the claims the project makes about
  itself against the things they claim about (version, in-app links, the
  check count, Play's limits, generated notices). Run before submitting.
- `tools/publish_wiki.sh` — copies `docs/wiki/` to the GitHub wiki, which
  is a second repository and does not exist until its first page is saved
  in the browser.
- `tools/make_notices.py` — the open-source notices, from the versions the
  build resolves rather than from a table anyone maintains.
- `tools/verify_memory.py` — runs the `<!-- verify: -->` command under every
  claim in this file and `CLAUDE.md`. A FAIL means the sentence is stale.
- `cmake/CheckReleaseTag.cmake` — refuses to package when the git tag and
  `version.h` disagree. It has now caught both faults it can: a tag ahead of
  the header, and a header ahead of an uncommitted bump.
- `test/test_capture.c` — the capture's identity property, on frames it
  builds itself: no network, no caster, no recorded station's coordinates
  in a public repository.
- `tools/make_feature_graphic.py` — the Play feature graphic for both
  editions, drawn from `make_icons.py`'s mark and palette so the listing
  and the launcher icon cannot drift apart.
- `android/app/proguard-rules.pro` — what R8 must not rename: the JNI
  entry points and the serializers. Both failures are release-only.
- `design/work-items/play-listing.md` — listing text and the data-safety
  answers, with the reasoning behind each.

## Active Decisions

- **A flag's lifetime must match the thing it describes** — and a
  record of a run outlives the screen that draws it. Two faces of one
  rule, both paid for: run-scoped accumulators in composables lost a
  nine-hour capture (2026-08-23), and a per-run provenance flag over a
  process-lived cache credited a stale file for the stream's orbits
  (2026-08-25).
- **Runs start and stop in one place** — the hub owns every run verb;
  the analysis screen is the viewing room (author, 2026-08-25). The
  service door enforces single occupancy with a logged refusal; the
  UI's hidden buttons are a promise, the door is the wall.
- **A record of a run outlives the screen that draws it.** Anything that
  accumulates across a session -- trails, the C/N0 scatter, counters --
  belongs where the run lives (`MonitorService`'s companion), never in a
  composition. A composition dies on a rotation and is fed nothing while
  the activity is stopped, so a plot kept there quietly describes the
  minutes its screen was on rather than the run (2026-08-23; the scatter
  had done this since it was written, and tracks inherited it by copying
  the precedent).
- **A learning project, held to professional standards.** No revenue goal
  and no growth to optimise, so commercially-argued proposals are
  off-target — but the engineering bar is a professional one, and "only a
  learning project" is never a reason to skip a test, a review or a
  verification (`CLAUDE.md`).
- **One measurement core, four frontends** — no threshold or verdict in any
  UI layer (`design/architecture.md`).
- **Verify a GUI from the launcher its users use.** Started from Explorer a
  Win32 GUI has no console, so `stdout` has no descriptor and anything built
  on it fails silently; started from a shell the same binary behaves
  differently in every respect touching stdio. The Log tab had never carried
  a worker line in normal use, and no shell-launched check could have found
  it (2026-08-18).
- **A dead channel hides every bug downstream of it.** Fixing the log pipe
  immediately surfaced two more faults it had been masking — bare LF against
  an EDIT control, and traces nobody had ever seen. Expect a queue when a
  broken channel starts working; they are not new regressions (2026-08-18).
- **Android ships as two Play listings, not one app with an in-app unlock** —
  entitlement is the installed APK, which works in the field with no signal
  (`android/design/editions.md`).
- **One JSON exchange format everywhere**, a `mountpoints[]` list; analysers
  read the first entry and say how many they ignored (`docs/jsonConfigs.md`).
- **The app never downloads a navigation file** — the user supplies it, so the
  licence relationship stays theirs (`android/design/views.md`).
- **KPI 8 judges constellations by the sourcetable's NavSys field**, never by
  the 1005/1006 indicator bits, which cannot express BeiDou.
- **The app collects no telemetry** — no SDK, no endpoint, no consent flow.
  Installs and daily-use figures come from Play Console; the shared report
  is for the user to hand to their caster operator, not a line to the
  author. Support is deliberately minimal: the product and the wiki answer
  the questions, issues go to GitHub (`design/telemetry.md`).
- **A data property shows up in every renderer.** C/N0 arrives quantised
  by message type, and the same striping appeared in Android and then in
  the Windows GUI; a fix in one frontend leaves the others wrong. Bin at
  the coarsest resolution any stream delivers.
- **What is installed is not what was built.** Two defects fixed in
  shared code were reported as live in pro, whose APK was an hour older
  than the fix; a VPS then ran 3.3.0 for an hour after its tree was
  rebuilt to 3.4.0, because build and install were two commands and only
  one was run. **Ask the artefact its version before reading any code**
  — `lastUpdateTime` on Android, `--version` everywhere else
  (`docs/RUNBOOK.md`).
- **A field in the snapshot schema that nothing fills is worse than a
  missing one**, because it looks like an answer. The ARP fields
  published `arp_valid:false` for every station until a live run tripped
  over it, and `latency_s` has been serialised, exported and displayed as
  "not measured" since the schema was written. Fill a field in the change
  that declares it, or do not declare it
  (`design/work-items/measurement-tiers.md`).
- **F-Droid: our own repository, never the official one** — it requires
  FLOSS, the Commons Clause forbids sale, and relicensing the free
  edition would strip the Clause from the shared core as well. Self-
  hosting costs nothing and keeps the Clause doing its job
  (`design/work-items/release-to-play.md`).
- **A passing verdict says nothing about a station's registration.** No
  KPI compares the sourcetable's declared position against the broadcast
  ARP, so a base advertising coordinates 3.3 km — and then 25 km — from
  its antenna returned STATION OK both times (2026-08-16). Read the
  sourcetable directly after any registration change. The check itself is
  phase 0 of `design/work-items/measurement-tiers.md`.
- **Measurement runs in two tiers** (2026-08-16). Tier 1 is the
  ninety-second acceptance check — *is this station fit now* — in every
  product, unchanged. Tier 2 is a stability report over hours — *has it
  been fit, and is it staying that way* — which no ninety-second window
  can answer at any price. Tier 2 lives in the CLI, the GUI and the
  daemon, and on Android only as far as a six-hour foreground service
  allows: a ceiling the platform imposes, not capability withheld.
  Windows are counted in **stream time**, so a capture replays to the
  identical report; tier 2 says STABLE / DEGRADED / UNSTABLE and never
  borrows tier 1's words (`design/work-items/measurement-tiers.md`,
  `design/kpi-candidates.md`).
- **Where two build systems describe one source set, CI must run both.**
  `service/Makefile` lists its sources by hand and had silently stopped
  linking, because CMake keeps its own list and never noticed. The Win32
  GUI still carries the same exposure through `build-gui.bat`, which no
  runner builds — that gap is stated in `.github/workflows/ci.yml` rather
  than hidden, and closing it is Phase 3 of `design/work-items/gui-track.md`.
- **TLS is coming, after the free launch**, as a bundled library behind a
  transport abstraction — chosen over per-platform native APIs to keep one
  code path for four frontends. It ships in **both editions** on the same
  day: the paid edition withholds convenience, never protection
  (`design/tls.md`).
- **The list is the layout, and both editions share the framework.** A
  capability is a `Panel` -- card, detail screen, share section -- named
  once in a per-flavour `Registry.kt`; the hub renders that list in order
  and the report is assembled from it in the same order. Free differs by
  what its registry omits and by six compile-time flags, not by having
  different machinery, so a phase-2 panel is a file plus a line rather
  than an edit in three places. The back stack is hand-rolled (`Dest`,
  `NavStack`) because the app has three destinations and
  `navigation-compose` would add a dependency to model them
  (`design/guiV2rollout.md`, `design/gui-v2-study.html`).
- **Share is the socket, not a feature.** Each panel contributes its own
  `shareSection`, so a capability joins the report by existing. Nothing
  in a report may carry a credential -- guaranteed by the snapshot
  holding none, and enforced by `tools/check_release.py`, which reads
  every `shareSection` and fails on `username` or `password`. The phone's
  own fix never leaves the device.
- **The device under test holds the author's data.** A handset here is
  not scratch space: `pm clear` wiped pro's caster and credentials in
  one command, and nothing could restore them. Reach for the state
  rather than the reset -- force-stop clears a run, a spare profile
  gives a blank hub, a release-signed build upgrades in place where a
  debug build demands an uninstall -- and where only a destructive route
  exists, hand it to the author (2026-08-22).
- **Pro does not go to Play until the last feature is in** (decided
  2026-08-22). Free ships on its own cadence -- 3.7.0 went up the day it
  was released -- while pro's bundle is built and verified each release
  and kept back. So a pro release means a tag, Windows assets and an
  APK, not a store upload, and the paid listing stays a release behind
  by intention rather than by oversight. What "the last feature" is
  belongs to the author; the phase-2 list it is drawn from is in
  `design/guiV3rollout.md`.
- **One frame, and the rules live in it.** The app bar takes no title
  parameter, the analysis bar is absent because no other screen passes
  one, and a row's mark comes from `Panel.affordance(state)` rather than
  from each card. Every one of those is a rule two editions cannot break
  separately, which is why the framework is shared and only the registry
  differs (`design/guiV3spec.md`).
- **A refusal names the field at fault.** The core classifies twelve
  ways a stream can fail to open and writes the sentence; the CLI, GUI
  and daemon print it, and the app maps the code to its own strings so a
  translated build stays possible. The verdict vocabulary did not move,
  so exit codes are unchanged (`design/guiV3spec.md` §5).
- **The shell between you and the device edits what passes through it.**
  Git Bash rewrites Unix paths in `adb shell` arguments (`MSYS_NO_PATHCONV=1`),
  and `adb shell` allocates a PTY that turns every `LF` into `CRLF`, which
  silently corrupts any binary read through it (`adb exec-out`). Both
  failures look like success: the command exits 0 and a file appears.

---
