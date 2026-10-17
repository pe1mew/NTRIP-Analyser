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
| `memory/gotcha-log.md` | Stuck, or something behaves impossibly — **and before removing anything that looks out of place** | Problem→root cause→fix: 45 open entries here and 92 resolved ones in `memory/gotcha-log-archive.md` (counted 2026-10-17, after every resolved entry was archived) — grep both. Triaged 2026-10-14; each retirement names its evidence |
| `memory/self-position-decisions.md` | Touching the self-position solve or its views | Six decisions behind it: GPS only, epoch closure, reversed rates; still verified |
| `memory/doxygen-in-headers-only.md` | Writing or moving documentation comments | Doxygen merges header and `.c` blocks — document the declaration only |
| `design/architecture.md` | Touching `src/core`, `src/session`, or adding a frontend | Why the session layer exists and what the snapshot guarantees |
| `design/todo.md` | Asking "does X already exist?" | Shipped vs planned, stable item numbers, and rejected ideas with reasons |
| `design/feature-matrix.md` | Asking "which product has X?" | Every feature against CLI, GUI, free, pro and the daemon, with the rationale for each split |
| `design/work-items/{release-to-play,cli-track,gui-track,measurement-tiers}.md` | Picking up work | Four parallel tracks — Android, CLI, GUI, and one that follows a capability rather than an artefact |
| `design/work-items/pro-to-play.md` | Anything in the Play Console, for either edition | Pro's store rollout, and every console mechanic that cost a step: price and regions, licence testing as the tester sees it, reviewer credentials and where the form lives |
| `design/kpi-candidates.md` | Proposing a new KPI | Why only one of four candidates was a KPI, and the two-tier answer that came out of it |
| `design/work-items/measurement-tiers.md` | Anything about KPIs or long-run measurement | Two tiers: the 90-second fitness check, and a stability report over hours. Only latency earns a ninth KPI |
| `design/gui-design.md` | Any `gui/` work | Window patterns; §13-§17 are the check, the stability window, threshold loading, self-position and the mountpoint map, as built |
| `design/work-items/mountpoint-map.md` | The mountpoint map, map tiles, the location picker, or sourcetable classification | M1–M5 as built with every measurement: 8471 sourcetable records behind the kinds, why there is no default tile source, five faults found by opening the page. M6–M8 (the phone, both editions) as built, with frame times and onocoy's stalls |
| `android/design/editions.md` | Any Android product decision | Free/pro split, payment model, profiles, GGA position sources |
| `android/design/views.md` | Android UI or sky-plot work | What each view answers, and where orbits actually come from |
| `android/design/design-review.md` | Changing anything cited as `design-review Dn` | Decisions D1–D7, dated, referenced from seven code sites |
| `docs/jsonConfigs.md` | Config reading, writing or interop | One format everywhere; passwords in the clear |
| `design/security-review.md` | Touching a parser, the socket layer, or anything a caster feeds | What a hostile caster can do; F3 narrowed 2026-08-25 — TLS shipped, the disclosure survives for plain-text casters |
| `design/tls.md` + `design/work-items/archive/tls-rollout.md` | Anything TLS — shipped in 3.8.0 | The decision and its surface; the rollout record carries every deviation, falsification and live find (chunked, entropy, SIGPIPE) |
| `design/legacy-observations.md` | Touching KPI 4, KPI 5, or anything that decides which messages count | Delivery is judged against the sourcetable, so an old GPS+GLONASS station passes. Built 2026-08-13 |
| `docs/wiki/` | Changing anything a user sees, or wondering what they were told | Fifteen pages and the sidebar, last published 2026-10-16 (`tools/publish_wiki.sh --push`); the app links into them, so a claim here is a claim in the product |

## Current State

<!-- 2026-10-16 -->

- **3.9.0 is released everywhere** (tag 2026-10-15; both editions live
  on Play 2026-10-16, "Updated on Oct 15"; GitHub release with 8
  assets, checksums checked from the downloads). It carries everything
  in the 2026-10-14 entry below, P7 with the scatter called an **upper
  bound** (no satellite code biases; measured against CAS's DCBs in
  `design/work-items/station-self-position.md`), and the listing
  sentence unchanged by the author's decision. **GH#5 verified between
  the Play installs** on the Huawei 2026-10-16 (signer pins both ways,
  password carried, STATION OK on the imported connection) and closed.
  Wiki published 2026-10-16, identical to `docs/wiki/`. `v3.8.1` tagged
  on `a598a91`; every merged branch deleted, local and remote.
  **Waiting:** both rewritten store listings (free's closed-test text,
  free's category moved to Tools, pro's Dutch) are in Play's review;
  when live, copy them into `design/work-items/play-listing.md` and
  clear its *Pending since 3.9.0* note. Free's 3.9.0 has no release
  notes (author's call, left as is).
  <!-- verify: git rev-parse v3.9.0 && git rev-parse v3.8.1 && grep -q "## \[3.9.0\]" changelog.md -->

<!-- 2026-10-14 -->

- **Main carried the next pro release** (2026-10-14; released as 3.9.0, above): PRs
  #9 (P7 self-position, Reference position tab), #10 (mountpoint map,
  M6–M8) and #11 (3.8.1) merged; GH#5 (pro imports free's connection
  by consent — free's consent screen is the security control, each
  side checks the other's package and signer, Play pin in release; the
  pins can only be checked once both editions ship it from Play;
  refused during a run; keeps pro's ephemeris stream and live GGA;
  todo 2.5, `android/design/editions.md` *Upgrade path*); GH#13 (each self-position point carries `sats`, `pdop`,
  `sv_mask`, `eph_id`; export v2; PDOP now of the satellites used);
  the watch's per-drop record (`KpiDrop`, `test_kpi_watch`); a cured
  failure cleared on reconnect; AGP 9.4.1 / Gradle 9.8.1 (GH#12). All
  verified on the S23 and the Huawei. P7 then waited on the two-cluster
  finding and the listing sentence; both settled 2026-10-15 (above).
  <!-- verify: test -f android/app/src/main/java/nl/pe1mew/ntripanalyser/SelfPosition.kt && test -f android/app/src/main/java/nl/pe1mew/ntripanalyser/MountMap.kt && grep -q "KpiDrop" src/core/kpi.h && grep -q 'agp = "9.' android/gradle/libs.versions.toml && grep -q 'object Handover' android/app/src/main/java/nl/pe1mew/ntripanalyser/Handover.kt && grep -q 'ShareConnectionsActivity' android/app/src/free/AndroidManifest.xml -->
- **Every frontend takes whole sourcetables only, and says why not**
  (`receive_mount_table_whole`, `src/net`): asked once more after a
  silence or a list cut short before `ENDSOURCETABLE`; onocoy did both,
  and once drew 12 of ~5900 mountpoints as all. `NRBY` is a network
  keyword. Plan: `design/work-items/mountpoint-map.md`, M6c.
  <!-- verify: grep -q "receive_mount_table_whole" src/net/ntrip_handler.h && grep -q '"NRBY"' src/core/sourcetable.c -->
- **Measure on a bench build** (`assembleProBench`, `….pro.bench`):
  release settings, debug key, beside the Play app. Panning 5900 marks:
  median 24 ms debug, 16 ms bench. Overnight Watch on bench, 8 h 08 min
  unplugged: phone 479 mAh (12.8 %), app 10 min 34 s CPU (2.2 % of a
  core), never suspended — Wi-Fi kernel wakelocks, not the app's.
  The self-position trail's two clusters ~5–6 m apart (scatter 6.8 m)
  were closed 2026-10-15: largely the solver's uncorrected satellite
  code biases, measured against CAS's DCBs
  (`design/work-items/station-self-position.md`, *The two clusters*).
  <!-- verify: grep -q 'create("bench")' android/app/build.gradle.kts -->

<!-- 2026-10-12 -->

- **The mountpoint map is complete on the desktop, on `main`**
  (2026-10-11/12, plan steps M1–M5, a commit each). View → Mountpoint
  map opens the loaded sourcetable in the browser over built-in Natural
  Earth outlines, and **Use** selects a row through a clipboard token
  the core builds and checks. Tools → Map tiles takes the user's own
  tile key; the **Map** button no longer shows CARTO watermarks. On the
  way: sourcetable classification moved into the core (measured on
  8471 records), the GUI's ARP cross-check stopped using a 0.01°
  position, and a stream no longer inherits the previous station's
  ARP. M6–M8 for the phone: see 2026-10-13.
  <!-- verify: test -f gui/gui_mountmap.c && test -f src/core/sourcetable_map.c && grep -q "Mountpoint map" design/feature-matrix.md -->

<!-- 2026-10-10 -->

- **Self-position is complete on the desktop and merged to `main`**
  (pe1mew/NTRIP-Analyser#8, 2026-10-10, merge `38a1e0e`). GUI window with S export, the
  CLI's `--report`, and the daemon's snapshot and report; orbits from
  the station's own stream, a configured side-stream, or `-R`. Plan
  steps P1–P6 and the desktop half of P8 are done; **P7** (pro on
  Android) waits for pro's production and the listing sentence, and
  **multi-GNSS** needs its own design. Plan:
  `design/work-items/station-self-position.md`.
  <!-- verify: grep -q "Self-position" design/feature-matrix.md && test -f gui/gui_selfpos_window.c -->

<!-- 2026-09-17 -->

- **Free is public on Google Play** (confirmed 2026-09-19). Production
  access was granted 2026-09-16; the same day an automated review
  refused 30800 for missing login credentials -- the Caster dialog's
  *Username*/*Password* read as a login wall, the 2026-08-14 problem
  recurring -- and the resubmission with a reviewer account on the
  author's own caster was published. The readme thanks the testers;
  the closed track stays open for early builds. A daily check on the
  author's server keeps the reviewer login honest (`docs/RUNBOOK.md`).
  <!-- verify-net: test "$(curl -s -o /dev/null -w '%{http_code}' --max-time 20 -A Mozilla/5.0 'https://play.google.com/store/apps/details?id=nl.pe1mew.ntripanalyser.free&hl=en')" = 200 -->
- **Pro is public on Google Play** (2026-10-14): production access
  granted that day, 3.8.0 -- the build the testers ran -- accepted and
  published; the store page answered HTTP 200 with €5.99 and 3.8.0.
  **3.8.1** (H9 only, branch `release-3.8.1`, PR #11) was accepted and
  **published on the open track** 2026-10-14 (submission 8, *Open
  test*, *Gepubliceerd*); 3.9.0 replaced it on production
  2026-10-16, and `v3.8.1` was tagged on `release-3.8.1`'s tip
  (`a598a91`) the same day. Pre-registration declined (S5).
  <!-- verify-net: test "$(curl -s -o /dev/null -w '%{http_code}' --max-time 20 -A Mozilla/5.0 'https://play.google.com/store/apps/details?id=nl.pe1mew.ntripanalyser.pro&hl=en')" = 200 -->
  Price €5.00 base, **€5,99 in NL**. The closed test that got it
  there — licence testers, the price table, the reviewer account
  (dedicated, watched daily) — is recorded step by step in
  `design/work-items/pro-to-play.md` S3–S5.
  <!-- verify: manual — the Play Console is the only source for track state -->

<!-- 2026-08-25 -->

- **3.8.0 released; phase 2 is complete** (2026-08-25). TLS in every
  <!-- verify: grep -q "## .3.8.0." changelog.md -->
  product and both editions -- mbedTLS behind the `ns_transport` seam,
  verification mandatory against the embedded Mozilla roots, chunked
  NTRIP 2 decoded, an explicit per-connection flag. Developed on the
  `tls` branch, accepted by PR #4's merge, released same day: GitHub
  (all platforms' assets) published, free submitted to Play, and every
  accumulated paid capability public for the first time. **The pro
  hold is lifted** -- `design/work-items/release-to-play.md` owns what
  comes next. The rollout record with every deviation and live find is
  `design/work-items/archive/tls-rollout.md`.
  <!-- verify: git rev-parse v3.8.0 -->
- **Phase 2's earlier five**: VRS check, hand-over, statistics export
  <!-- verify: grep -c "done 2026-08" design/work-items/archive/tier2-on-the-phone.md -->
  and tier 2 shipped pro-gated in one day each, every plan in
  `design/work-items/*-on-the-phone.md` recording what its steps
  actually did.
- **The issue tracker is live**: #1 is a PR; issues start at #2.
  Backlog items carry `[GH#n]` tags (`design/todo.md`).
- **check_release.py is at 114 checks with release artefacts present** (fewer report when no artefact exists to judge; 106 until P7 added the SppStatus comparison, 2026-10-12); the gate-table guard has
  <!-- verify: python tools/check_release.py > /dev/null 2>&1 || test $? -eq 1 -->
  collected on every new Features flag (tracks, VRS, hand-over,
  export, tier 2) before the commit that introduced it went green.
- **Twenty-six C tests**, including two desktop-built bridge harnesses:
  <!-- verify: test "$(ctest --test-dir build -N 2>/dev/null | sed -n 's/^Total Tests: //p')" = 26 -->
  `test_bridge_vrs.c`, which drives the phone's own plumbing over a
  loopback socket with a synthetic clock, and
  `test_bridge_sourcetable.c`, the sourcetable document the app decodes.

Older entries: [sessions up to 2026-08-20](sessions-to-2026-08-20.md), [2026-08-21 to 08-26](sessions-2026-08-21-to-08-26.md).

## Recently Promoted

<!-- Format: "if [situation], then [what to do] — promoted from gotcha-log YYYY-MM-DD"
     Retire an entry as soon as it appears in its destination. -->

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
- If a **falsification leaves a check green**, the test is the defect:
  feed it the input the defence exists for, not the input a cooperating
  caller would send (synthetic ephemerides sharing one `af1`, so a
  removed clock-drift term was common-mode and invisible, 2026-10-09;
  a snapshot that politely blanked `selfpos_speed_mms`, so a removed
  `has_vel` gate was never consulted, 2026-10-09; the same mistake again
  in the snapshot serialiser's own test hours later, 2026-10-09) —
  promoted from gotcha-log 2026-10-09 to Active Decisions below.
- If you are about to **write down what a frontend does** — in the
  feature matrix, a doc, a brief for another session — **test it on that
  frontend first** (the daemon "solved stations broadcasting their own
  orbits" and solved none; `-t --report` was documented the same and did
  not either; *Read the wiring, missed the gate* before them) —
  promoted from gotcha-log 2026-10-10 to Active Decisions below.
- If you **hand the author a command**, put everything it depends on
  **in the command itself** — the shell it is written for and, for git,
  `git -C <checkout>`, and for a commit the message as
  `commit -F <file>` — never in a sentence beside it (a Bash heredoc
  at a PowerShell prompt, 2026-10-09; a bare `git commit` in the wrong
  checkout, 2026-10-10; a bare `git commit` whose editor closed empty,
  2026-10-12) — promoted from gotcha-log 2026-10-10 to Active
  Decisions below.

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
- `android/app/src/main/java/nl/pe1mew/ntripanalyser/Dialogs.kt` — `AppMenu`
  is where the overflow rows are **gated by edition**; `Shell.kt` only
  wires them. An edition's capability is settled here.
- `src/session/ns_transport.{c,h}` — the transport seam: plain TCP and
  TLS behind one set of calls, OS entropy, `MSG_NOSIGNAL`.

## Active Decisions

- **A claim about what a frontend does is tested on that frontend
  before it is written** — in the feature matrix, a doc, or a brief for
  another session. The matrix said the daemon "solved stations
  broadcasting their own orbits" and it solved none; `docs/cli.md` said
  the same of `-t --report` and it did not either. Both assumed a shared
  layer decoded ephemerides, and **the session layer decodes nothing** —
  each frontend decodes what it needs. Promoted 2026-10-10 (three
  occurrences, with *Read the wiring, missed the gate*).
- **A command handed to the author carries all its context in the
  command itself**: the shell it is written for, for git
  `git -C <checkout>`, and for a commit the message in a file,
  `commit -F <file>`. Never in a sentence beside it. A Bash heredoc
  met a PowerShell prompt (2026-10-09); a bare `git commit` met the
  wrong checkout and committed another session's staged work under this
  one's message, already pushed before anyone noticed (2026-10-10); a
  bare `git commit` met an editor that closed empty and was reported as
  committed (2026-10-12). **Check a claimed commit before building on
  it** — `git log`, and the worktrees — which is what caught the third.
- **The map underneath is the program's own** (2026-10-12). Both GUI
  map pages draw over Natural Earth 1:50m, public domain, compiled in;
  tiles only from a source the user sets up with their own key, kept in
  HKCU and never in a config file or the log. CARTO had silently started
  watermarking every tile for everyone (about 2026-09-23), breaking the
  shipped **Map** button for three weeks, and the OSM Foundation's
  server refuses a page opened from a file. **A dependency on someone
  else's free service is a claim about their terms, and terms change.**
  Credited with Natural Earth's own long line in the readme, the
  notices, the data file and on the maps (`docs/licences.md`).
- **What a sourcetable entry *is* was measured, not assumed**
  (2026-10-11, 8471 records from seven casters, `sourcetable_kind` in
  the core). The STR solution field is filled everywhere and wrong both
  ways; `nmea` is set on every RTK2go base; services name themselves in
  the mountpoint name, not the field the GUI's rule read. Keywords
  anywhere decide *network*; advertising no station messages decides
  *product*; the field's 0 is believed and its 1 is not. Stream Health
  keeps its own rule — whether a *name* should gate the VRS check is
  the station check's decision, not a side effect.
- **Text from outside reaches a page as text, at both boundaries.** The
  core escapes caster and user strings once for the page source
  (`sourcetable_map.c`); the page hands them to Leaflet only as elements
  built with `textContent`, because Leaflet sets a *string* tooltip or
  attribution as HTML — a hostile mountpoint name ran script on hover
  until it did (2026-10-12). Test a page with a hostile sourcetable and
  a `window.pwned` canary; reading the code missed it.
- **A screen may only say what its evidence supports, and "not yet" is
  evidence of nothing.** Four times a window has stated a finding about
  a station that only described how long it had been watched: "fewest
  held: 9" from a partial first epoch, "no dual-frequency pair" at 25 s
  (both 2026-08-18), then **NOT COMPUTABLE** at 32 s and ring labels of
  "0.125 m" on a plot with no points (both 2026-10-09, in a window
  added to the same document that records the first two). A state that
  is merely early gets words that say so — *gathering*, naming what is
  being waited for — and only a property of the **stream itself**, like
  a single-frequency station, may be stated as a finding.
  `design/gui-design.md` §14.5 and §16.5. **Writing the lesson down did
  not transfer it; looking at the window did** — `PrintWindow` to a PNG
  and read it, with `SetProcessDPIAware()` called first.
- **A falsification that stays green accuses the test, not the code.**
  Three times now the input was too cooperative to reach the defence:
  synthetic ephemerides sharing one `af1` made a deleted
  satellite-clock-drift term common-mode, absorbed exactly by the
  receiver's clock unknown; a snapshot that blanked `selfpos_speed_mms`
  beside `has_vel = false` meant a deleted `has_vel` gate was never
  consulted, because the sentinel rejected the sample first; and then,
  hours after writing this entry, a snapshot left at its `NS_UNSET`
  initialisers hid a deleted status gate in the snapshot serialiser —
  **knowing the pattern did not stop it; running the falsification
  did** (all 2026-10-09, `design/work-items/station-self-position.md`
  P4-P6). **Feed a test the input its defence exists for** — wrong where
  it should be wrong, stale where a caller would have blanked it,
  different where uniformity would hide the fault. And **falsify every
  new check by name**, because that is what catches this.
- **A flag's lifetime must match the thing it describes** — and a
  record of a run outlives the screen that draws it. Three faces of one
  rule, all paid for: run-scoped accumulators in composables lost a
  nine-hour capture (2026-08-23), a per-run provenance flag over a
  process-lived cache credited a stale file for the stream's orbits
  (2026-08-25), and the Intent that copies settings into a run forgot
  the TLS field, so the run silently went plain text (2026-08-25 —
  a field the copy forgets is a field the run does without).
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
- **Where two build systems describe one source set, CI must run both —
  better, have one list.** `service/Makefile` fell behind by hand and now
  builds by wildcard; `build-gui.bat` and the hand-listed VS Code tasks
  retired outright with the TLS rollout (2026-08-25), leaving
  `CMakeLists.txt` as the only desktop source list. The GUI is still not
  CI-built (Windows-only); that gap stays stated in
  `.github/workflows/ci.yml`, and a compile-only Windows job remains
  Phase 3 of `design/work-items/gui-track.md`.
- **TLS is a bundled library behind a transport seam** (mbedTLS 3.6 LTS,
  `src/session/ns_transport.c`) — chosen over per-platform native APIs to
  keep one code path for four frontends. Shipped in 3.8.0 in **both
  editions**: the paid edition withholds convenience, never protection
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
- **Editions share a release by default** (one tag, one version code,
  two bundles), but a fix may go to one: 3.8.1 was pro only.
- **Store reviewers get a real credential set on our own caster.** The
  app has no accounts, but Play's automation reads the Caster dialog's
  password field as a login wall, and a release-note hint for a human
  did not survive production review. A dedicated reviewer account on
  the author's caster -- always up, reachable from anywhere, rate
  limiting kept on but loose, never rotated during a review, with a
  backup mountpoint -- sits in
  *App-toegang* on both editions (2026-09-16, `pro-to-play.md` S5).
- **Pro picks up where free left off by consent, not by signature**
  (GH#5, 2026-09-13). Play re-signs each app with its own key, so the
  installed editions share no signing identity; the user's approval in
  free is the security control. Each side still pins the other's Play
  signing certificate in release builds; shipped in 3.9.0 and verified
  between the two Play installs 2026-10-16.
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
  With `MSYS_NO_PATHCONV=1` set, local paths must be given in Windows
  form (`C:/...`) — the switch stops *every* conversion (2026-09-13).

---
