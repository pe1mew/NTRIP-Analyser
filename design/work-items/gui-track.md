# The Windows GUI: its own queue

## What & Why

The third parallel track, alongside [release-to-play.md](release-to-play.md)
(Android) and [cli-track.md](cli-track.md). The reasoning is the same:
the GUI has its own users and its own defects, and none of them should
wait behind a store review.

This file starts as a queue rather than a plan. Its phases are seeded
from [design/todo.md](../todo.md) — the items there that are **not**
shipped and belong to the Win32 program — and from one gap that no item
records yet. The order below is a proposal, not a decision.

## Current status

| Phase | What | State |
|---|---|---|
| 1 | Sky-view labels collide | open, cause understood |
| 2 | Finish and land the VRS monitor | mostly done, partly untracked |
| 3 | The GUI is outside CI | known and deliberate; still a risk |

---

## Phase 1 — Sky-view labels overwrite each other — open

Recorded as [todo.md §1.5](../todo.md). An elevation-ring label and a PRN
label can land in the same place; observed live on `APEL00NLD0`, where
`15°` was drawn over `R17`.

The cause is understood and is the second half of a fix already
half-made: ring labels are drawn last over a halo, so they now win every
collision instead of losing every one. The fix is to *place* colliding
labels rather than reorder them — offset a ring label along its own
circle, or move a PRN label to the other side of its marker.

It touches `gui/gui_sky_window.c` **and** `Views.kt`, which draws the
same furniture — so this is one of the few items that should be done on
both tracks in one go, or the two sky views diverge.

Cosmetic, and it bites exactly where the sky is crowded, which is where
somebody is most likely to be hunting one specific PRN.

## Phase 2 — Finish and land the VRS monitor — mostly done

Recorded as [todo.md §2.4](../todo.md). The floating VRS Monitor
(View → VRS Monitor) plots rover GGA against the broadcast ARP: live
distance to the virtual station, a polar direction plot, a rolling
five-minute strip chart, and accumulated ARP dots that reveal
hand-overs. Supporting state is in `gui/gui_state.h`.

Two things to settle before it can be called done:

- **`gui/gui_vrs_window.c` is untracked.** A feature that exists only in
  a working directory is one `git clean` from gone.
- The CLI's half shipped as `--check-vrs`. The two should agree on what
  makes a stream a VRS, and that judgement belongs in the core, not in
  either frontend — the project's standing rule that no threshold lives
  in a frontend.

## Phase 3 — The GUI is outside CI — known, deliberate, still a risk

[.github/workflows/ci.yml](../../.github/workflows/ci.yml) says so
plainly: the Win32 GUI and the UNIX daemon are not built there, because
the GUI has a second hand-written build path (`build-gui.bat`) and a
MinGW toolchain on a Linux runner would fail in ways that say more about
the runner than about the code. It is verified by hand on the machine
that ships it, per [docs/RUNBOOK.md](../../docs/RUNBOOK.md).

That is an honest gap rather than a hidden one, but it is still the case
that a core change can break the GUI and no machine will notice until
somebody builds it. The cheap middle option — worth costing before
committing to it — is a **compile-only** job on a Windows runner
invoking `build-gui.bat`, with no test and no artefact: enough to catch
a missing include or a changed core signature, which is the class of
breakage that actually happens.

## Phase 4 — Retire the GUI's private capture — blocked on the CLI track

Once [cli-track.md](cli-track.md) Phase 1 puts capture-to-file in the
session layer — where [architecture.md §3.3](../architecture.md) has
always said it belongs — the GUI's own version becomes a duplicate: the
`fwrite` in `gui/gui_thread.c`, the `FILE*` and its critical section in
`gui/gui_state.h`. The menu items and the Save dialog stay; only the
plumbing beneath them changes, to `ns_capture_start()` / `ns_capture_stop()`.

**Do not start this before V6 of that phase passes** — a GUI capture and
a CLI capture of the same stream, in the same minute, agreeing frame for
frame. The GUI is the reference implementation here, and deleting the
reference before the replacement is proved against it would leave nothing
to compare with.

## Open questions

- Is this order right? Phase 2 is nearly finished and Phase 1 is
  cosmetic, which argues for swapping them.
- Does the GUI want the daemon's rolling capture, or is
  [cli-track.md](cli-track.md) Phase 1 enough for everyone who needs a
  file?

## Outcome

Nothing landed from this track yet.
