---
name: self-position-decisions
description: Active decisions behind the station self-position solve (P3-P7): what it solves, from what, and how it was proved
metadata:
  type: project
---

Moved from MEMORY.md Active Decisions on 2026-10-14 to bring the index
under its 35k-character target. Still current; still checked:
`tools/verify_memory.py` reads this file too.

- **Self-position works in all three desktop frontends**, verified
  against live stations and an independent solver (2026-10-10; branch
  `self-position`, PR #8). APEL00NLD0 in the GUI: 83 epochs, offset
  E −3.5 / N +3.7 / U +3.0 m, **scatter 0.231 m**, worst residual
  2.26 m, 9 satellites at PDOP 1.3. RTKLIB's `rnx2rtkp` on RFSEE01 with
  the same ephemerides: E +0.06 / N +1.13 / U +8.27 m, scatter 0.449 m.
  The GUI and the daemon side by side for 43 min on one stream:
  scatter 2.17 m and **2.18 m**, median motion 32.5 mm/s in both — so
  differing scatter between runs is the station's hour, not a frontend.
  **Eight defects stood between the closure test passing and this**,
  every one found on a live station and none visible to the suite at
  the time: the solve never called; six constellations under one clock
  unknown; an epoch destroyed by a split constellation; the epoch dated
  on BeiDou time; a station whose DF393 never clears; RFSEE01's
  reversed rates; and the daemon and the CLI ignoring orbits a station
  broadcasts itself.
  <!-- verify: grep -q "selfpos_solve(s)" src/session/ntrip_session.c && grep -q "test_selfpos_session" CMakeLists.txt -->
- **RFSEE01's receiver sends the phase-range rate with the sign
  reversed** (2026-10-10) — every satellite the exact negative of what
  its orbit predicts. The solver now fits both conventions and keeps
  the one that fits; the snapshot reports `selfpos_rate_sign` −1 and
  the window shows `REVERSED`. Five or more rates decide it; four
  cannot, and claim nothing. **A field defined by a standard is still a
  claim made by somebody's firmware** — where the data can say which way
  round it is, ask the data.
  <!-- verify: grep -q "rate_sign" src/core/spp.h && grep -q "case_reversed_rates" test/test_spp.c -->
- **An observation epoch is complete when DF393 clears *or* when the
  next one demonstrably begins** — whichever comes first, compared
  **within one system** because the epoch fields are not comparable
  across them (2026-10-10, `src/session/ntrip_session.c`). Three live
  stations in two days broke three different assumptions: a
  constellation split across two frames, a bundle closed by BeiDou (on
  BDT, 14 s behind GPS), and a station whose DF393 stopped clearing
  entirely when NavIC dropped out of view. **A stream's shape is a
  property of the sky that hour, not of the station** — anything
  waiting for a particular frame needs a way to proceed without it.
  <!-- verify: grep -q "sys_epoch" src/session/ntrip_session.c && grep -q "DF393 never clears" test/test_selfpos_session.c -->
- **The single-point solve is GPS only** (2026-10-09), because each
  constellation needs a clock unknown of its own — the argument the
  module had already written down for GLONASS and applied to GLONASS
  alone. Admitting all six under one unknown put a real station 1.8 km
  out, with 2.6 km residuals and **829 m/s** of apparent motion on a
  concrete pillar. Two lessons worth more than the fix: **when a refusal rests
  on a general principle, ask which other cases that principle covers**,
  and **a closure test cannot discover that the world has more clocks
  than the model** — `test_spp` built its measurements from the same
  single-clock model it inverted, against one synthetic constellation.
  <!-- verify: grep -q "gnss_id != 1" src/core/spp.c && grep -q "case_gps_only" test/test_spp.c -->
- **A test that starts below the wiring cannot see the wiring.** The
  self-position solve was defined and never called for two whole steps
  of its plan — the call site discarded the `obs_feed()` return value
  that says an epoch closed — under a suite of nineteen green tests
  that each began *below* `ntrip_session`: the solver with its own
  epoch, the report with its own snapshots, the serialisers with
  theirs. Every surface handled "nothing solved" correctly, so the
  absence looked like a property of the station. Found by connecting a
  real one. `test/test_selfpos_session.c` now replays RTCM through
  `ns_open_file()` and asks only whether a closed epoch reaches the
  solve. **When a feature spans layers, one test must cross all of
  them.**
- **`-Wall` is on our targets as of 2026-10-09** — the build until then
  passed `-O3 -DNDEBUG -std=gnu99` and nothing else, so a static
  function nobody called drew no warning and "the build is
  warning-free" was measured with an instrument that asks nothing.
  Check what the build asks before quoting it:
  `grep C_FLAGS build/CMakeFiles/<target>.dir/flags.make`.
  <!-- verify: grep -q -- "-Wall" CMakeLists.txt -->
