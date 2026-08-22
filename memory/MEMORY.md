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

<!-- 2026-08-20 -->

- **The Android UI is a registry of panels, and 3.6.0 carries the first
  <!-- verify: grep -q '^## \[3.6.0\]' changelog.md -->
  edition of it** (2026-08-20): GUI v2 phase 1, built to
  `design/guiV2rollout.md` from the study in `design/gui-v2-study.html`.
  A hand-rolled `NavStack` replaced the swipes -- every move is a control
  and every way back is Back -- the station screen renders
  `hubPanels` in order, and **the list is the layout**: one per-flavour
  `Registry.kt` decides what an edition shows, and adding a capability in
  phase 2 means a file plus one line. Sharing was built as the socket
  rather than a feature: the hub sends the report assembled from each
  panel's own `shareSection` in hub order, the analysis screen sends the
  plot as a PNG through a new `FileProvider` that statistics export will
  inherit. **Both editions run 3.6.0 side by side** on the test phone;
  free is the same framework with six compile-time flags off and one
  *More in Pro* card. Not tagged, not released, not uploaded to Play.
- **A shared plot has an opaque background because the layer paints one**
  (2026-08-20): a subtree capture has no background of its own, and the
  screen cannot tell you so -- see the gotcha log.

<!-- 2026-08-18 -->

- **v3.5.0 tagged and built** (2026-08-18): a stream that stops without
  <!-- verify: grep -q '^## \[3.5.0\]' changelog.md -->
  closing is now noticed. `NsOptions::stall_timeout_s` (60 s, `0` disables,
  per-mountpoint in the daemon's config) treats silence on an open socket
  as a drop and reports `NS_END_STALLED`; tier 2 stops standing behind a
  window whose stream clock has not moved for `stale_s` (120 s) and reverts
  to `INSUFFICIENT EVIDENCE`. Both came from one live fault: a monitored
  mountpoint delivering nothing for 14 h 10 min while every status it
  published said it was fine. Twelve tests, `test_stall.c` among them,
  built on a real loopback caster that misbehaves on purpose.
  **Published 2026-08-18** with eight assets — both Windows binaries and
  <!-- verify-net: test "$(gh release view v3.5.0 --json isDraft --jq .isDraft)" = false -->
  the example config by hand, the Linux binaries, the daemon tarball, both
  SHA256SUMS and the notices by CI.
- **The daemon on shuttle2 runs 3.5.0** (2026-08-18), installed from a
  <!-- verify: manual — needs ssh to shuttle2; check sha256 of /usr/local/sbin/ntrip-monitord -->
  clean `git archive` of the tagged commit, with the previous binary kept
  beside it as `ntrip-monitord.3.4.0-aug17`. Its source checkout at
  `~/NTRIP-Analyser` was brought up to the 3.5.0 tree afterwards, so the
  two agree — but the checkout is not what was installed and never proves
  what runs; the sha256 of the binary does.
- **v3.4.0 released** (2026-08-15), the first release whose Linux assets were
  <!-- verify: grep -q '^## \[3.4.0\]' changelog.md -->
  built and attached by CI from the tag rather than by hand. Verified from
  the packaged binary, not the build tree: `--version`, a live capture, and
  `--check` returning STATION OK against `ntrip.kadaster.nl/APEL00NLD0`.
- **The documentation site is published, checked and its own thing**
  <!-- verify: test -f docs/favicon.ico -a -f docs/assets/css/style.scss -a ! -e docs/readme.md -->
  (2026-08-16): one privacy policy covering all four programs rather than
  four; `docs/licences.md` rewritten as a statement of position instead
  of a task list; the two documentation indexes merged into `index.md`;
  a favicon and a repository social preview generated from the same mark
  as the app; and the theme's 500 px column widened, because a landing-
  page theme clipped ninety-column diagrams. Nineteen links that worked
  in the repository and 404'd on the site are fixed, and
  `tools/check_release.py` fails on `](../` in `docs/` so they cannot
  return.
- **The capture paid for itself on its first real use** (2026-08-16):
  six-hour captures of the author's two stations, converted with
  `convbin` and submitted to CSRS-PPP, showed both broadcasting positions
  **metres** from truth — 1.9 m high at RFSEE01, 2.8 m out at HANESE.
  Both stations pass all eight KPIs and always did: they are healthy and
  in the wrong place, which is precisely what a bounded check cannot see
  and what tier 2 exists for.
  **Correcting them is blocked on a third party** (2026-08-16): the 1005
  is composed by **NTRIP-X**, which exposes no way to set the broadcast
  position, so the fix needs the manufacturer rather than a
  configuration change. The sourcetable now carries the PPP coordinates;
  the receivers still broadcast the survey-in ones, and every rover
  inherits that until the vendor responds. **The request went to the
  vendor on 2026-08-20** -- both offsets tabulated, each backed by its
  six-hour CSRS-PPP solution -- so this is now waiting on a reply rather
  than on anything in this repository.
- **The stream can be captured to a file from every frontend.** `--capture`
  <!-- verify: ctest --test-dir build -R capture --output-on-failure -->
  and `--capture-max` on the CLI, the File menu in the GUI, and the
  capability sits in the session layer where `design/architecture.md` §3.3
  had always assigned it — so the daemon has it for free and the GUI's
  private copy is now a duplicate awaiting retirement. Frames only, so a
  capture is clean input to RTKLIB's `convbin` and byte-identical whichever
  program wrote it (proved against a GUI-written capture). A failed write
  ends the run with **exit 7**, outranking even `--check`'s verdict.
  `docs/base-declaration.md` is the chain from a capture to a PPP solution.
- **Shipped recently**: KPI 8 (advertised versus actual) across all frontends;
  the station check in the Windows GUI; RINEX GLONASS fixes plus the project's
  first regression test; Android saved connection profiles with encrypted
  credentials; one shared JSON config format everywhere.
  The GGA uplink now follows the sourcetable's `nmea` flag in both editions,
  <!-- verify: grep -q 'wants_gga = e\[i\].nmea' src/cli/cli_stream.c -->
  with pro reporting the phone's own position after a one-time consent and
  falling back to the fixed one; positions are picked from the station's
  sourcetable entry or handed off to the user's map app, and no map SDK is
  embedded (`android/design/editions.md`).
- **Verifying an uplink needs no caster**: `test/tools/` drives the Android
  bridge on the desktop against a stub that timestamps what arrives — no
  public caster advertises an `nmea` mountpoint to test against
  (`docs/RUNBOOK.md`).
- **Every RTCM 3 observation format is now measured** — legacy
  <!-- verify: ctest --test-dir build -R 'msm_cnr|legacy_obs' --output-on-failure -->
  1001-1004/1009-1012 and MSM1-7 for satellites, MSM4-7 and legacy for
  C/N0 — and KPIs 4 and 5 judge a station against what its sourcetable
  advertises rather than against a fixed multi-GNSS expectation
  (`design/legacy-observations.md`). A station's own message format also
  sets the resolution of the C/N0 views, which is documented in
  `android/design/views.md` because it has twice looked like an app
  defect.
- **Free is live on Google Play** (2026-08-17), listing at
  `play.google.com/store/apps/details?id=nl.pe1mew.ntripanalyser.free`,
  with the tester opt-in at `play.google.com/apps/testing/…`. Google's
  twelve-testers-for-fourteen-days rule for a new developer account
  still applies, so the readme still asks for testers. The signed bundle
  is `app-free-release.aab`, 3.3.0 / 30300, **1.35 MB to install**
  because Play sends one ABI split and one language split. Pro's bundle
  is built and signed but not submitted: its data-safety answers differ
  and two reviews at once is two chances to be asked the same question.
  <!-- verify: grep -q "targetSdk = 36" android/app/build.gradle.kts -->
- **The release keystore exists**, outside the tree, and both editions
  are signed with it — one key, two listings.
  <!-- verify: manual — keystore.properties and the .jks are git-ignored
       by design, so the repository cannot see them -->
- **What the launch required, and now has**: target **API 36** (Play's
  floor from 31 August 2026) on AGP 8.11.2 / Gradle 8.13; the native
  library laid out for **16 KB pages**; store assets including a
  generated feature graphic; a foreground-service declaration with a
  demonstration video. See `design/work-items/release-to-play.md`.
- **The website and the wiki are live** (2026-08-14): GitHub Pages serves
  <!-- verify-net: gh api repos/pe1mew/NTRIP-Analyser/pages --jq .status | grep -q built && git ls-remote -q https://github.com/pe1mew/NTRIP-Analyser.wiki.git HEAD | grep -q . -->
  `docs/` — the privacy-policy URL Play requires — and twelve wiki pages
  are published from `docs/wiki/` by `tools/publish_wiki.sh`. The app
  links into both, so an unpublished page is a broken button.
- **Where a document lives decides who reads it**: `docs/` is served as a
  <!-- verify: test ! -e docs/work-items -a ! -e docs/security-review.md -->
  website, so it holds what is written for someone who is not us;
  working documents (the release plan, the listings, the security
  assessment) live in `design/`, which Pages never sees.
- **The sky view and the C/N0-elevation plot now say where their
  <!-- verify: ctest --test-dir build -R eph_validity --output-on-failure -->
  positions came from** — a badge on the Analysis screen, green for a
  real orbit source, red for a navigation file too old to place
  anything, amber for the phone's own receiver. Orbits are counted and
  aged by whether they can be *used*, not by whether they exist, which
  is what let a stale file read as a full cache
  (`docs/wiki/Orbits-and-the-ephemeris-stream.md`).
- **Release plumbing is in place**: version parsed from
  <!-- verify: grep -q 'version.h' android/app/build.gradle.kts && grep -q 'isMinifyEnabled = true' android/app/build.gradle.kts && test -f tools/make_icons.py -->
  `src/core/version.h` by Gradle (`versionCode` = MMmmpp), signing from a
  git-ignored `keystore.properties` with a debug-key fallback that says
  so, R8 on and verified against a live caster, one generated icon in
  every form (`tools/make_icons.py`), privacy policy and listings drafted.
  The keystore itself is the author's to create.
  <!-- verify: manual — whether a release build is signed with the real key
       cannot be seen from the repository; keystore.properties is git-ignored -->
- **In progress**: getting the app onto **three stores, free first** —
  Google Play, Samsung Galaxy Store and F-Droid
  (`design/work-items/release-to-play.md`, twelve phases). Play is the
  long pole and cannot be hurried: a new developer account must run a
  closed test with **12 testers opted in for 14 continuous days** before
  production access, so recruitment runs from the top of `readme.md`.
  Samsung gets a rules study before packaging; nothing known blocks free
  there. **F-Droid is settled** (2026-08-14): its official repository
  requires FLOSS and our Commons Clause forbids sale, so the two are
  mutually exclusive — free will be published from **a repository we
  host ourselves**, which needs no licence change. Pro's place on either
  store is deferred until free is out.
- **Orbits now come off the observation stream** wherever a station
  broadcasts them, in every frontend. `rtcm_decode_eph()` is the single
  copy of the seven-type switch. The free Android edition draws a sky
  view with nothing configured; the paid edition dials its ephemeris
  side-stream only when nothing has reached the cache for 20 s
  (`android/design/views.md`).
- **CI, since 2026-08-14**: `.github/workflows/ci.yml` builds the core
  and runs the test suite on Linux, builds the daemon **through both of its
  <!-- verify: test -f .github/workflows/ci.yml && test -f .github/workflows/release-linux.yml -->
  build paths** (CMake and `service/Makefile`), builds both Android
  editions (which runs `checkEditionParity` as a preBuild dependency), and
  weekly runs the verify commands under the claims in this file. It
  compiles with `-Wall -Wextra`; the tree is warning-free, so a new warning
  means something. **It does not build the Win32 GUI** — Windows-only,
  second hand-written build path, still a manual step in `docs/RUNBOOK.md`.
  A `v*` tag additionally runs `release-linux.yml`, which packages and
  attaches the Linux assets.
- **Known gaps**: the GUI station check has no saved report;
  seven pro rows in the editions table are marked *planned*, not built.
  <!-- verify: manual — counting rows in a prose table, and "planned" is a
       judgement about the table's own honesty -->

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
