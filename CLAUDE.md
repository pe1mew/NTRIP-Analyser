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

## Before You Start

| When | Read |
|------|------|
| Picking up where the last session left off | `memory/MEMORY.md` — the index itself, not the topic files it lists |
| Starting any session (framework drift) | Compare the `framework: agent-ready-projects vX.Y.Z` line above against https://github.com/ducroq/agent-ready-projects/blob/master/CHANGELOG.md. If this project is behind, surface it before starting work — adopting the change is the engineer's call |
| Changing anything in `src/core` | It is shared by four programs. Rebuild and re-run **all** of them — `docs/RUNBOOK.md` |
| Building, testing, deploying, adding a file | `docs/RUNBOOK.md` |
| Asking "does X already exist?" | `design/todo.md` — shipped vs planned, stable item numbers |
| Making architectural decisions | `design/architecture.md`; Android decisions D1–D7 in `android/design/design-review.md` §8, cited from code as `design-review Dn` |
| Any Windows GUI work | `design/gui-design.md` — §13 is the station check |
| Any Android work | `android/design/editions.md` (free/pro split, payment, profiles, GGA sources) and `android/design/views.md` (what each view is for) |
| Reading or writing configuration files | `docs/jsonConfigs.md` — one format everywhere, passwords in the clear |
| Stuck, or something behaves impossibly | `memory/gotcha-log.md` — problem→root cause→fix archive |
| Writing or moving documentation comments | `memory/doxygen-in-headers-only.md` |
| Ending a session | Run `/curate` — review the gotcha log, promote patterns, update the memory index |
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
- **Never add a GUI source file to only one build.** `CMakeLists.txt`
  *and* `build-gui.bat` both list every `gui/*.c` by hand.
- **Never document a function in both header and `.c`.** Doxygen merges
  them and reports nonsense.
- **Verify against a live caster before claiming something works.** A
  clean build proves nothing here.

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
| `gui/gui_state.h` | `AppState` — everything the GUI knows |
| `gui/gui_events.c` | Command dispatch, Stream Health, station classification |
| `android/app/src/main/cpp/ntrip_bridge.c` | All Android logic, plain C |
| `android/app/src/main/java/.../MainActivity.kt` | The whole Android UI |
| `android/app/src/{free,pro}/.../Features.kt` | Compile-time edition gates |
| `test/test_rinex_nav.c` | The only test |
| `changelog.md` | Entries carry the measurement behind each claim |

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
