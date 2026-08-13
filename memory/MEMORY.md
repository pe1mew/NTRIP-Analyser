# Memory index

Knowledge that is not obvious from reading the code. This file is an
**index**, not a store: most of what an agent needs is already written
down in `design/`, and this points at it. Add a topic file here only for
knowledge that has no home in the design docs.

## Where knowledge lives

| Topic | File | Load it when |
|---|---|---|
| Layering, session loop, statistics snapshot | `design/architecture.md` | Touching `src/core`, `src/session`, or adding a frontend |
| What is shipped, planned, or rejected | `design/todo.md` | Asking "does X exist?" before building it |
| Windows GUI structure; §13 station check | `design/gui-design.md` | Any `gui/` work |
| Free/pro split, payment model, saved profiles, GGA sources | `android/design/editions.md` | Any edition or Android product decision |
| What each Android view is for, and where orbits come from | `android/design/views.md` | Android UI or sky-plot work |
| Android KPI/VRS decisions D1–D7, dated | `android/design/design-review.md` | Changing anything the code cites as `design-review Dn` |
| The one JSON config format, and its plain-text passwords | `docs/jsonConfigs.md` | Config reading, writing, or interop |
| Build, test, deploy, extension points | `docs/RUNBOOK.md` | Any build or device work |
| Non-obvious failures and their root causes | `memory/gotcha-log.md` | Stuck, or something behaves impossibly |
| Doxygen goes in headers only | `memory/doxygen-in-headers-only.md` | Writing or moving documentation comments |

## Current state (2026-08-13)

- **Version 3.3.0**; substantial unreleased work in `changelog.md`.
- **Shipped recently**: eight KPIs including advertised-versus-actual;
  the station check in the Windows GUI; the RINEX GLONASS fixes and the
  first regression test; Android saved connection profiles with
  encrypted credentials; one shared JSON config format everywhere.
- **Decided, not yet built**: GGA position sources — sending follows the
  sourcetable's `nmea` flag, free sends a fixed position prefilled from
  the sourcetable, pro sends the phone's live position with consent
  (`android/design/editions.md`).
- **Next**: release plumbing for Android (signing, version wiring to
  `version.h`, icons, store listings, privacy policy), then testing on a
  Samsung S23.
- **Known gaps**: the Windows GUI has no station-check report file;
  `versionName` is hand-maintained; no CI; one test.

## Facts an agent should not have to rediscover

- Four programs are built from one core; `src/core` changes reach all of
  them. The GUI is listed in **two** build files.
- The Android free and pro editions differ only in the UI layer
  (`Features.kt` per flavor). Measurements are identical by design.
- The Android app's own log tag is `ntrip_android`; settings use
  `ntrip_settings`.
- Verification in this project means a live caster: `rfsee.net/RFSEE01`
  and `HANESE`, `ntrip.kadaster.nl/APEL00NLD0` and `AMEL00NLD0`, with
  `BCEP00KAD0` for ephemerides.
