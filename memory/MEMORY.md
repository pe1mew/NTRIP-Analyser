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
| `design/gui-design.md` | Any `gui/` work | Window patterns; §13 is the station check as built |
| `android/design/editions.md` | Any Android product decision | Free/pro split, payment model, profiles, GGA position sources |
| `android/design/views.md` | Android UI or sky-plot work | What each view answers, and where orbits actually come from |
| `android/design/design-review.md` | Changing anything cited as `design-review Dn` | Decisions D1–D7, dated, referenced from seven code sites |
| `docs/jsonConfigs.md` | Config reading, writing or interop | One format everywhere; passwords in the clear |
| `design/security-review.md` | Touching a parser, the socket layer, or anything a caster feeds | What a hostile caster can do; six findings closed, TLS scheduled |
| `design/tls.md` | Implementing TLS, or asked why it is not there yet | The decision, the measured surface, and what actually costs |
| `design/legacy-observations.md` | Touching KPI 4, KPI 5, or anything that decides which messages count | Delivery is judged against the sourcetable, so an old GPS+GLONASS station passes. Built 2026-08-13 |
| `docs/wiki/` | Changing anything a user sees, or wondering what they were told | Twelve published pages; the app links into them, so a claim here is a claim in the product |

## Current State

<!-- 2026-08-14, second session -->

- **v3.3.0 released** on the desktop; substantial unreleased work in `changelog.md`.
  <!-- verify: grep -q 'NTRIP_VERSION_STRING  "3.3.0"' src/core/version.h -->
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
- **Free is submitted to Google Play** (2026-08-14), closed testing track,
  awaiting review. The signed bundle is `app-free-release.aab`, 3.3.0 /
  30300, **1.35 MB to install** because Play sends one ABI split and one
  language split. Pro's bundle is built and signed but not submitted:
  its data-safety answers differ and two reviews at once is two chances
  to be asked the same question.
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
  <!-- verify: gh api repos/pe1mew/NTRIP-Analyser/pages --jq .status | grep -q built && git ls-remote -q https://github.com/pe1mew/NTRIP-Analyser.wiki.git HEAD | grep -q . -->
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
  and runs the five tests on Linux, builds both Android editions (which
  runs `checkEditionParity` as a preBuild dependency), and weekly runs
  the verify commands under the claims in this file. **It does not build
  the Win32 GUI** — that is Windows-only, has a second hand-written
  build path, and stays a manual step in `docs/RUNBOOK.md`.
  <!-- verify: test -f .github/workflows/ci.yml -->
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
- **The phone is not the build.** One codebase and two editions means the
  *installs* diverge even when the code cannot: two defects fixed in
  shared code were reported as live in pro, whose APK was an hour older
  than the fix. Compare `lastUpdateTime` before reading code
  (`docs/RUNBOOK.md`).
- **F-Droid: our own repository, never the official one** — it requires
  FLOSS, the Commons Clause forbids sale, and relicensing the free
  edition would strip the Clause from the shared core as well. Self-
  hosting costs nothing and keeps the Clause doing its job
  (`design/work-items/release-to-play.md`).
- **TLS is coming, after the free launch**, as a bundled library behind a
  transport abstraction — chosen over per-platform native APIs to keep one
  code path for four frontends. It ships in **both editions** on the same
  day: the paid edition withholds convenience, never protection
  (`design/tls.md`).

---
