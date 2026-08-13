# NTRIP-Analyser — working notes for AI agents

Analyses NTRIP RTCM 3.x streams from GNSS base stations: is this station
fit to serve RTK, and if not, why? One measurement core in C99 serves
**four programs** — a CLI, a Windows GUI, a Linux monitoring daemon, and
an Android app in two editions.

Author: Remko Welling (PE1MEW). Apache 2.0 with Commons Clause.

## Hard constraints

- **Never run a git write command.** No commit, push, merge, rebase, tag.
  Stage changes, propose a message, stop. Git is the user's to drive.
- **Never put a threshold, verdict or measurement rule in a frontend.**
  They live in `src/core` — `kpi.c`, `vrs_check.c`, `iono.c`. A station
  that passes in one program must pass in all of them; a KPI that
  disagreed by frontend would make every number untrustworthy.
- **Never gate a measurement by edition.** Android free and pro show
  *more* or *less*, never *different*. Gating happens in the UI layer.
- **A remembered or inferred value must never satisfy the KPI that asks
  for it.** The sky view may draw from a remembered station position;
  KPI 3 still waits for a real 1005/1006. Forging evidence for a test is
  worse than failing it.
- **Never add a GUI source file to only one build.** `CMakeLists.txt`
  *and* `build-gui.bat` both list every `gui/*.c` by hand.
- **Never document a function in both header and `.c`.** Doxygen merges
  them and reports nonsense. See `memory/doxygen-in-headers-only.md`.
- **Verify against a live caster before claiming something works.** This
  project measures the real world; a clean build proves nothing.

## Architecture

```
src/core/     parsing, orbits, KPIs, statistics — no I/O, no platform headers
src/net/      NTRIP protocol + socket client
src/session/  the stream loop every frontend drives (ns_open/ns_pump/ns_stats)
   ├── src/cli/    ntrip-analyser        (Windows + Linux)
   ├── gui/        ntrip-analyser-gui    (Win32, Windows only)
   ├── service/    ntrip-monitord        (UNIX only, Munin)
   └── android/    two editions from one flavor-split codebase
```

`android/app/src/main/cpp/ntrip_bridge.c` is plain C99 holding all
Android logic; `jni_glue.c` only marshals. JNI cannot be compiled
without an NDK, so nothing that could be tested on a desktop belongs
there.

## Before you start

| When | Read |
|---|---|
| Changing anything in `src/core` | It is shared by four programs — rebuild and re-run **all** of them (`docs/RUNBOOK.md`) |
| Architecture, layering, why the session layer exists | `design/architecture.md` |
| Picking up work, or asking "is X done?" | `design/todo.md` — shipped vs planned, stable item numbers |
| Anything Android | `android/design/editions.md` (free/pro split, payment, profiles), `android/design/views.md` (what each view is for) |
| Android KPI/VRS decisions | `android/design/design-review.md` §8 — decisions D1–D7, dated, cited in code |
| Windows GUI internals | `design/gui-design.md` — §13 is the station check |
| Configuration files | `docs/jsonConfigs.md` — one format, passwords in the clear |
| Building, testing, deploying, adding a file | `docs/RUNBOOK.md` |
| Stuck, or something behaves impossibly | `memory/gotcha-log.md` |
| Subsystem knowledge that is not in the code | `memory/MEMORY.md` |

## Key files

| Path | What |
|---|---|
| `src/core/kpi.{c,h}` | The eight KPIs, sustain clock, roll-up verdict |
| `src/core/vrs_check.{c,h}` | Five network-RTK assertions A1–A5 |
| `src/core/rtcm3x_parser.{c,h}` | RTCM decode, CRC-24Q, MSM, ARP extraction |
| `src/core/sv_ephemeris.{c,h}`, `sv_orbit.c` | Orbit cache and propagators |
| `src/core/rinex_nav.c` | RINEX 3 NAV loader — see `test/test_rinex_nav.c` |
| `src/core/config.c` | The shared JSON config format |
| `src/session/ntrip_session.c` | Stream loop, statistics snapshot |
| `gui/gui_state.h` | `AppState` — everything the GUI knows |
| `android/app/src/main/cpp/ntrip_bridge.c` | All Android logic, plain C |
| `android/app/src/main/java/.../MainActivity.kt` | The whole UI |
| `test/test_rinex_nav.c` | The only test; `--target test_all` |

## Domain facts that look like bugs if you do not know them

- **Constellation ids are 1-based**: 1 GPS, 2 GLONASS, 3 Galileo, 4 QZSS,
  5 BeiDou, 6 SBAS, 7 NavIC.
- **C/N0 must be averaged in linear power**, not in dB — averaging dB
  overstates by ~2 dB (measured).
- **RTCM 1005/1006 has indicator bits for GPS, GLONASS and Galileo only.**
  It cannot express BeiDou, so constellation claims are judged against
  the sourcetable's NavSys field instead.
- **A satellite with no orbit must be absent from the plot**, never drawn
  at 0,0 — the horizon due north is a lie the plot cannot distinguish
  from a fact.
- **Advertising a constellation that is not currently streamed is
  normal** (QZSS is advertised across Europe, visible from none of it).
  Only streaming something never advertised is a finding.

## How to work here

- Build, test and deploy: `docs/RUNBOOK.md`.
- Comments explain *why*, in prose, at the density of the surrounding
  file. Match the existing voice rather than adding banner comments.
- `changelog.md` entries carry the measurement behind the claim, not
  just the change.
- Design decisions are recorded in `design/` and `android/design/` and
  cited from code (`design-review D1`). Update the doc when the decision
  changes; do not leave the code as the only record.
