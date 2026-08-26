---
stack: C99 (core, CLI, Win32 GUI, UNIX daemon), Kotlin/Compose + NDK (Android), CMake + Gradle
status: Production (desktop, v3.3.0) · Pre-release (Android, two editions)
repo: github.com/pe1mew/NTRIP-Analyser
framework: agent-ready-projects v1.25.0
---

# NTRIP-Analyser

Analyses NTRIP RTCM 3.x streams from GNSS base stations and answers one
question: is this station fit to serve RTK, and if not, why? For station
operators, surveyors and installers signing off a base.

One measurement core in C99 serves **four programs** — a CLI, a Windows
GUI, a Linux monitoring daemon, and an Android app in two editions.
Author: Remko Welling (PE1MEW). Apache 2.0 with Commons Clause.

**A learning project, held to professional standards.** Revenue is not
the goal and there is no growth to optimise, so proposals resting on
commercial reasoning — analytics, funnels, engagement mechanics, support
commitments, release cadence — are off-target.

That is about motive, never about rigour. *"It is only a learning
project"* is not a reason to skip a test, a review, a design note or a
verification against a live caster, and never a reason to ship something
known to be wrong. The bar is what a professional instrument must meet,
because people will point it at real stations and believe what it says.

## Before You Start

| When | Read |
|------|------|
| Picking up where the last session left off | `memory/MEMORY.md` — the index itself, not the topic files it lists |
| Starting any session (framework drift) | Compare the `framework: agent-ready-projects vX.Y.Z` line above against https://github.com/ducroq/agent-ready-projects/blob/master/CHANGELOG.md. If this project is behind, surface it before starting work — adopting the change is the engineer's call |
| Changing anything in `src/core` | It is shared by four programs. Rebuild and re-run **all** of them — `docs/RUNBOOK.md` |
| Building, testing, deploying, adding a file | `docs/RUNBOOK.md` |
| Asking "does X already exist?" | `design/todo.md` — shipped vs planned, stable item numbers |
| Making architectural decisions | `design/architecture.md`; Android decisions D1–D7 in `android/design/design-review.md` §8, cited from code as `design-review Dn` |
| Any Windows GUI work | `design/gui-design.md` — §13 station check, §14 stability, §15 thresholds, all as built |
| Any Android work | `android/design/editions.md` (free/pro split, payment, profiles, GGA sources) and `android/design/views.md` (what each view is for) |
| Reading or writing configuration files | `docs/jsonConfigs.md` — one format everywhere, passwords in the clear |
| Stuck, or something behaves impossibly | `memory/gotcha-log.md` — problem→root cause→fix archive |
| Writing or moving documentation comments | `memory/doxygen-in-headers-only.md` |
| Ending a session | Run `/curate` — review the gotcha log, promote patterns, update the memory index. It runs `python tools/verify_memory.py`, which re-checks every claim in this file and the index against the repository |
| Monthly, or after major restructuring | Run `/audit-context` — duplication, wrong-layer placement, broken references |

<!-- "Active work" section deliberately absent: this tool has auto-memory,
     so the in-progress list lives in memory/MEMORY.md "Current State".
     Keeping both is how the two copies start disagreeing. -->

## Hard Constraints

- **Never run a git write command.** No commit, push, merge, rebase or
  tag. Stage changes, propose a message, stop.
- **Never put a threshold, verdict or measurement rule in a frontend.**
  They live in `src/core` — `kpi.c`, `vrs_check.c`, `iono.c`. A station
  that passes in one program must pass in all of them.
- **Never gate a measurement by edition.** Android free and pro show
  *more* or *less*, never *different*; gating happens in the UI layer.
- **A remembered or inferred value must never satisfy the KPI that asks
  for it.** The sky view may draw from a remembered station position;
  KPI 3 still waits for a real 1005/1006.
- **The desktop has one source list**: `CMakeLists.txt`.
  `build-gui.bat` and the hand-listed VS Code tasks retired with the
  TLS rollout (2026-08-25); `service/Makefile` builds by wildcard.
- **Never document a function in both header and `.c`.** Doxygen merges
  them and reports nonsense.
- **Cite the basis, or say there isn't one.** A recommendation that
  sounds researched must be researched: "the comparison set sits at
  €5-20" was an impression, and the author was one click from pricing
  a listing on it (2026-08-26). The same discipline the memory index
  applies to state claims applies to advice — evidence, or an explicit
  "I haven't checked".
- **Verify against a live caster before claiming something works.** A
  clean build proves nothing here. The same rule for anything published:
  fetch the page, do not trust the deploy status.
- **Read the artefact, not the toolchain's promise.** What ships is the
  `.aab`, the `.so`, the page — open them. NDK 27 was assumed to align
  the library to 16 KB pages and had not; the bundle was assumed to
  carry both ABIs and the signature was assumed to be the release key.
  All three were one command away from being known.
- **Measure the way the build measures, or report no number.**
  `-fsyntax-only` cannot see the truncation warnings (they need `-O2`),
  and `-std=c99` hides `M_PI` where the build uses `gnu99` — both gave a
  confident, wrong count in one session. Same flags, same optimisation,
  same standard as the thing you are describing.
- **Never rewrite a file with a script that re-encodes it.** Escapes and
  line endings are both mangled that way. Use the file-editing tools; if
  a script is unavoidable, read and write with `newline=''` and convert
  once.
  **`sed -i` has no legitimate use in this repository** — Grep to search,
  Edit to change. Four incidents: mangled escapes, doubled carriage
  returns, a literal newline, and a `sed -i` written to *test a match*
  that deleted a table row in the very file recording the other three.
  **A python edit script never travels through a heredoc** — the tool
  layer halves backslashes before the shell sees them (`\\0` arrived as
  a NUL byte, 2026-08-25, four times). Write the script to the
  scratchpad with the Write tool and run the file.

## Architecture

```
src/core/     parsing, orbits, KPIs, statistics — no I/O, no platform headers
src/net/      NTRIP protocol + socket client
src/session/  the stream loop every frontend drives (ns_open/ns_pump/ns_stats)
   ├── src/cli/    ntrip-analyser        CLI, Windows + Linux
   ├── gui/        ntrip-analyser-gui    Win32, Windows only
   ├── service/    ntrip-monitord        UNIX only, writes Munin snapshots
   └── android/    two editions (free/pro) from one flavor-split codebase
```

`android/app/src/main/cpp/ntrip_bridge.c` holds all Android logic in
plain C99; `jni_glue.c` only marshals. JNI cannot be compiled without an
NDK, so nothing testable on a desktop belongs there.

## Key Paths

| Path | What it is |
|------|-----------|
| `src/core/kpi.{c,h}` | The eight KPIs, sustain clock, roll-up verdict |
| `src/core/vrs_check.{c,h}` | Five network-RTK assertions A1–A5 |
| `src/core/rtcm3x_parser.{c,h}` | RTCM decode, CRC-24Q, MSM, ARP extraction |
| `src/core/sv_ephemeris.{c,h}` | Orbit cache; `sv_orbit.c` propagates |
| `src/core/rinex_nav.c` | RINEX 3 NAV loader — pinned by `test/test_rinex_nav.c` |
| `src/core/config.c` | The one JSON config format, plus the legacy reader |
| `src/session/ntrip_session.c` | Stream loop and statistics snapshot |
| `src/core/ns_failure.{c,h}` | The twelve ways a stream fails to open, and the words for each. The `errno`/`WSAE*` half lives in `src/net/ntrip_handler.c`, which has the platform headers |
| `gui/gui_state.h` | `AppState` — everything the GUI knows |
| `gui/gui_events.c` | Command dispatch, Stream Health, station classification |
| `android/app/src/main/cpp/ntrip_bridge.c` | All Android logic, plain C |
| `android/app/src/main/java/.../MainActivity.kt` | The Android shell: state, service binding, permissions |
| `android/app/src/main/java/.../Shell.kt` | The frame every screen is drawn in: four bar slots, the overflow menu, the analysis bar. **No title parameter**, so no screen can disagree about the app's name |
| `android/app/src/main/java/.../Failure.kt` | `NsFailure` in the app's own words, and which field each fault points at. Numbers checked against the C enum by `tools/check_release.py` |
| `android/app/src/main/java/.../{Panel,HubPanels}.kt` | The panel contract and the ten panels that implement it. A panel owns its card, its detail screen and its slice of the shared report |
| `android/app/src/main/java/.../Navigation.kt` | `Dest` and the hand-rolled `NavStack`. No `navigation-compose` |
| `android/app/src/{free,pro}/.../Registry.kt` | **The list is the layout** — what an edition shows, in order, hub and report alike |
| `android/app/src/{free,pro}/.../Features.kt` | Compile-time edition gates |
| `test/` | Sixteen tests: RINEX loader, hostile RTCM frames, MSM C/N0 layout, legacy observations, ephemeris validity, stream capture, station report, stream clock, snapshot serialisation, threshold policy, KPI 1's stopped-stream wording, stall detection, failure classification, the network-RTK assertions, the bridge's VRS workflow over a loopback socket, TLS against a loopback caster with deliberately bad certificates |
<!-- verify: test "$(ctest --test-dir build -N 2>/dev/null | sed -n 's/^Total Tests: //p')" = 16 -->
| `changelog.md` | Entries carry the measurement behind each claim |
| `.github/workflows/ci.yml` | Core, tests, release checks, the daemon's own Makefile and both Android editions, per push; claims weekly. **Not** the Win32 GUI |
| `.github/workflows/release-linux.yml` | On a `v*` tag: build, test, package, attach the Linux assets. `ubuntu-22.04` deliberately — its glibc 2.35 is the floor the binaries then require |

## Domain facts that look like bugs if you don't know them

- Constellation ids are 1-based: 1 GPS, 2 GLONASS, 3 Galileo, 4 QZSS,
  5 BeiDou, 6 SBAS, 7 NavIC.
- C/N0 must be averaged in **linear power**, not dB — dB overstates by
  about 2 dB (measured).
- RTCM 1005/1006 carries indicator bits for GPS, GLONASS and Galileo
  only. It cannot express BeiDou, so constellation claims are judged
  against the sourcetable's NavSys field.
- A satellite with no orbit must be **absent** from the plot, never
  drawn at 0,0 — the horizon due north is a lie the plot cannot
  distinguish from a fact.
- Advertising a constellation that is not currently streamed is normal
  (QZSS across Europe). Only streaming something never advertised is a
  finding.

## How to Work Here

```bash
# Desktop: build CLI + GUI into bin/  (compiler: CodeBlocks MinGW)
export PATH="/c/Program Files/CodeBlocks/MinGW/bin:$PATH"
cmake --build build

# Tests — no network, no caster
cmake --build build --target test_all

# Live check against a caster: exit 0 OK / 6 caution / 1 failed
cd bin && ./ntrip-analyser.exe --check
```

```powershell
# Android: both editions
$env:JAVA_HOME = 'C:\Program Files\Eclipse Adoptium\jdk-17.0.20.8-hotspot'
cd android; .\gradlew.bat assembleFreeDebug assembleProDebug
```

Full detail, including the GUI's second build path and the device
workflow: `docs/RUNBOOK.md`.

## Commit Conventions

Imperative subject describing the effect, not the mechanism. The body
explains why, and carries the measurement behind any claim — the way
`changelog.md` entries do. The agent prepares the message; the user
commits.
