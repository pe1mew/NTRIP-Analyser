# The Windows GUI: its own queue

## What & Why

The third parallel track, alongside [release-to-play.md](release-to-play.md)
(Android) and [cli-track.md](cli-track.md). The reasoning is the same:
the GUI has its own users and its own defects, and none of them should
wait behind a store review. A fourth,
[measurement-tiers.md](measurement-tiers.md), crosses all three — the
GUI's part in it is commissioning: an hour on a new station to judge
whether it is worth a week of the daemon's attention.

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
| 4 | Retire the GUI's private capture | **built 2026-08-18**, once V6 was made a test that could fail |

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

## Phase 4 — Retire the GUI's private capture — **built 2026-08-18**

[cli-track.md](cli-track.md) Phase 1 put capture-to-file in the session
layer — where [architecture.md §3.3](../architecture.md) has always said
it belongs — which made the GUI's own version a duplicate. It is now
gone: the `fwrite` in `gui/gui_thread.c`, the `FILE*`, its byte counter,
`close_rtcm_capture_if_active`, and the file-opening in both menu
handlers. The menu items and the Save dialog are unchanged, exactly as
this phase said; only the plumbing beneath them is
`ns_capture_start()` / `ns_capture_stop()` / `ns_capture_status()`.

**The gate held, but not in the shape written here.** The rule was: do
not start before V6 passes, V6 being a GUI capture and a CLI capture of
the same stream in the same minute, agreeing frame for frame. Attempting
it showed the test could not have failed — the GUI has no framing of its
own, it writes frames the session already framed and CRC-checked, so two
clients compare one code path with itself, and two live captures can
only ever be compared statistically. V6 was rewritten to write **one**
stream through both paths at once and compare byte for byte; that is
what passed, and it is a test that could have failed. The reasoning is
in cli-track.md, "V6, and why the test had to change".

**Two threads, one session.** The session belongs to the worker thread,
so the menu cannot call `ns_capture_start` itself. It leaves a request
behind and the pump loop acts on it between pumps — the pattern the GGA
uplink already uses. The alternative was two threads inside one session
while frames are being written, which nothing in this program locks.

**What the GUI gained by losing code**: the session refuses to overwrite
an existing capture and refuses a second one over a running one, reports
a bad path as an error rather than silently not capturing, honours a
size cap, and ends the session `NS_END_WRITE_ERROR` when a write fails.
The GUI's own version had none of that.

## Wish list — parked

### Tab navigation into the output tabs — parked 2026-08-18

Tab reaches every input field and button (built 2026-08-18: `WS_TABSTOP`
was always there, the message loop simply never offered the message to
`IsDialogMessage`). It does **not** reach the output half of the window:
the tab control and all four ListViews are created without
`WS_TABSTOP`, so the keyboard cannot switch tabs, cannot get into the
mountpoint list, and cannot pick a mountpoint at all — that is
double-click only. The list already handles Ctrl+A and Ctrl+C, so it
expects keyboard use it cannot be given.

Three parts, deliberately separate:

| | What | Cost |
|---|---|---|
| 1 | `WS_TABSTOP` on the tab control | Nearly free. Left/Right then change tabs on their own and fire `TCN_SELCHANGE`, which is already handled. |
| 2 | `WS_TABSTOP` on the four ListViews | Adds exactly **one** stop, whichever tab is open: `OnTabSelChange` hides the other three and the dialog manager skips hidden controls. |
| 3 | Enter on a highlighted mountpoint row | The reason this is not a one-line change — see below. |

**The trap, which is why this is written down rather than just done.**
`IsDialogMessage` takes Enter before the message is dispatched, and a
ListView asks for arrows and characters, not all keys — so Enter never
reaches it and becomes `IDOK`, which the window maps to Open Stream.
The moment the lists become tab stops: arrow down to a station, press
Enter, and the GUI opens a stream on whatever mountpoint was already in
the field, ignoring the highlighted row. Plausible-looking and wrong.

The fix belongs in the `IDOK` case, not in the ListView: if focus is on
the mountpoint list, do what double-click does — copy the row's
mountpoint into the field — otherwise open the stream. One place
decides, and Enter goes on meaning "act on what I am looking at".

Not included, and a separate ~10 lines if ever wanted: **Ctrl+Tab** to
cycle tabs from anywhere. A standalone tab control is not a property
sheet, so nothing implements that for you; it needs intercepting in the
message loop ahead of `IsDialogMessage`.

## Open questions

- Is this order right? Phase 2 is nearly finished and Phase 1 is
  cosmetic, which argues for swapping them.
- Does the GUI want the daemon's rolling capture, or is
  [cli-track.md](cli-track.md) Phase 1 enough for everyone who needs a
  file?

## Outcome

Phase 4 landed on 2026-08-18: the GUI's private capture is gone and the
session writes the file, which is also what made the keyboard work
possible to check — the same live run showed the Log tab had never
carried a line when the program was started from Explorer. Phases 1, 2
and 3 are still open, and the wish list above is parked.
