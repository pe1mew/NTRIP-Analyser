---
name: sessions-to-2026-08-20
description: Archived Current State entries, 2026-08-13 to 2026-08-20 (superseded by later releases)
metadata:
  type: project
---

Archived from MEMORY.md Current State on 2026-08-25. Superseded by
3.7.x; kept for provenance.

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

