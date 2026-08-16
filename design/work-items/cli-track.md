# The command-line tool: its own queue

## What & Why

Until now this repository has had one work queue, and it was an Android
queue: [release-to-play.md](release-to-play.md) runs from telemetry
through licences, security, the API 36 bump and closed testing. That is
the right shape for a store launch, and the wrong shape for everything
else — a Play review costs a fortnight of calendar time and no
engineering, and the CLI's users should not spend that fortnight waiting.

So the work items now run as **parallel tracks**, one file each:

| Track | File | Product |
|---|---|---|
| Android to the stores | [release-to-play.md](release-to-play.md) | the app, free and pro |
| The command-line tool | **this file** | `ntrip-analyser` |
| The Windows GUI | [gui-track.md](gui-track.md) | `ntrip-analyser-gui` |
| Measurement tiers | [measurement-tiers.md](measurement-tiers.md) | *all of them* — KPI 9 and the stability report |

The fourth follows a capability rather than an artefact, because a
measurement that means different things in different products is worth
less than one that means the same everywhere.

[design/todo.md](../todo.md) keeps its job: the cross-cutting idea list,
where a thought is recorded before anyone has decided to build it. A
track file is the narrower thing — what is being built, in what order,
and what "done" will mean.

## Current status

| Phase | What | State |
|---|---|---|
| 1 | Capture the stream to a file, with reconnect | **built 2026-08-15**; V5 (a 6 h run to a PPP solution) outstanding |
| 2 | `--rtcm-stdin` beyond `--sky` | open |
| 3 | Capture the ephemeris stream | not scheduled |

---

## Phase 1 — Capture the stream to a file, with reconnect — **built**

*Specified 2026-08-14; planned and decided 2026-08-15; built the same
day. Steps 1–3 are in; step 4 is half done and step 5 belongs to the
GUI track. What was measured is under "Outcome" at the foot of this
file.*

### Why

The GUI can write a stream to disk and the CLI cannot, and the
consequences of that asymmetry are larger than they look:

- **The CLI can replay captures it cannot create.** `--rtcm-stdin` reads
  a `.rtcm3` file through the session layer, and every such file in
  existence was made by the Windows GUI. A tool that reads a format it
  cannot write is half a feature.
- **A long capture belongs on the machine that is already up.** The GUI
  is Windows-only and needs a desktop awake with the app open. The people
  who need multi-hour captures — anyone declaring a base station to
  Centipede-RTK, per [docs/base-declaration.md](../../docs/base-declaration.md)
  — are running a Pi or a VPS, and today have no capture path from this
  project at all.
- **It forces a second client.** Capturing with RTKLIB's `str2str` while
  analysing with this tool means two sessions against the caster, and
  some casters allow one per account. One process that captures *and*
  reports means the analysis describes the exact bytes in the file.

There is also a bookkeeping defect to correct: [todo.md §2.2](../todo.md)
records "Raw stream capture and offline replay — **Shipped**", which is
true of the GUI and false of the CLI. The item reads as closed and is
half open.

### The baseline: what the GUI does

Parity means these behaviours, each verified in the code rather than
remembered:

| # | Behaviour | Where |
|---|---|---|
| G1 | Writes **CRC-validated frames only**, from the frame handler | [gui_thread.c:242](../../gui/gui_thread.c:242) |
| G2 | Opens the file in **binary** mode (`"wb"`) | [gui_events.c:2626](../../gui/gui_events.c:2626) |
| G3 | Default name `YYYYMMDDHHmmss_<mountpoint>.rtcm3`, local time, `capture` when the mountpoint is empty | [gui_events.c:2662](../../gui/gui_events.c:2662) |
| G4 | Refuses to start a second capture over a running one | [gui_events.c:2650](../../gui/gui_events.c:2650) |
| G5 | Requires an open stream before it will start | [gui_events.c:2655](../../gui/gui_events.c:2655) |
| G6 | Counts bytes written, and reports the total on stop | [gui_events.c:2821](../../gui/gui_events.c:2821) |
| G7 | Closes the file when the stream closes, and says so | [gui_events.c:1520](../../gui/gui_events.c:1520) |
| G8 | Reports an open failure and continues without capturing | [gui_events.c:2691](../../gui/gui_events.c:2691) |
| G9 | **Survives a reconnect**: the session emits no end event while auto-reconnect is on, so one file spans the outage | [ntrip_session.c:542](../../src/session/ntrip_session.c:542) |

G1 is the load-bearing one. Frames-only means the file contains no
handshake bytes, no junk between frames and nothing that failed its
CRC — so it is clean input to `convbin` by construction, and a capture
made by either program is byte-identical to one made by the other.

### The CLI surface

| Option | Argument | Default | Meaning |
|---|---|---|---|
| `--capture` | `<path>` | off | Write CRC-validated RTCM frames to `<path>`. If `<path>` is an existing **directory**, write `YYYYMMDDHHmmss_<mountpoint>.rtcm3` inside it (G3). |
| `--capture-max` | `<MB>` | `0` | Stop capturing cleanly once the file reaches this size. `0` means no limit. |

Nothing else. No format choice, no rotation, no compression — see
*Deliberately out of scope*.

**Which modes accept it**: every mode that opens an observation stream —
`-d`, `-t`, `-s`, `-S/--sky`, `--check`, `--check-vrs` — and also
`--sky --rtcm-stdin`, where it acts as a filter (see the decisions
below). Modes that open no observation stream (`-m`, `-g`,
`--check-config`, `-i`, `--version`, `-h`) reject it with exit 2 rather
than accepting a flag they will silently ignore.

Combined with the existing `--reconnect`, this is the unattended form:

```bash
ntrip-analyser -t 86400 --reconnect --capture /var/spool/gnss/ -q
```

### Behaviour

1. **Open before connecting.** The file is created and opened `"wb"`
   *before* the socket, so a bad path fails in the first second rather
   than after twenty hours of streaming. This is a deliberate divergence
   from G8: the GUI is interactive and can carry on without the capture,
   whereas an unattended run whose only purpose was the file must fail
   loudly and immediately. Exit 7.
2. **Binary mode is not optional.** On Windows a text-mode handle
   translates CRLF pairs inside RTCM payloads; the same capture read back
   yields **1** frame instead of 206 ([todo.md §0.5](../todo.md)). The
   input side already guards this with `_setmode`; the output side must
   use `"wb"` and be tested on Windows.
3. **Frames only** (G1), written from the session's frame event, so live
   and replayed sources behave identically and the CLI's output matches
   the GUI's byte for byte.
4. **The file spans reconnects** (G9). A drop leaves a gap in the epochs,
   not a truncated file. The gap is invisible in the bytes — RTCM carries
   no wall-clock time — so the run must **report the reconnect count on
   exit**, giving the operator the number of gaps to expect. That number
   is already kept in `NsStats.reconnects`.
5. **A short write is fatal.** `fwrite` returning less than asked for
   means the disk is full or the volume vanished. Close the file, name
   the path and the byte count on stderr, stop the run, exit 7. Twenty
   hours of capture that silently stopped at hour three is the worst
   outcome this feature can produce, and the one that must be impossible.
6. **`--capture-max` stops cleanly**, at a frame boundary: close the
   file, log the total, and *continue the analysis run* to its normal end
   and its normal exit code. Reaching a self-imposed cap is not an error.
7. **Every exit path closes the file** — normal end, `--duration`
   expiry, EOF, Ctrl-C, and Ctrl-A. Ctrl-A abandons the sky PNG by
   design; it must not abandon the capture, because the bytes already
   written are valid RTCM and may be hours of them.

   *Found while building this: SIGINT was handled in `--sky` and nowhere
   else, so Ctrl-C in `-t` or `-d` killed the process outright — no
   `ns_close`, no `fclose`. A handler is now installed **while capturing
   only**, so the stream modes keep the behaviour they have always had
   when they are not. Whether all of them should stop gracefully is an
   open question below, not this change's to decide.*
8. **Progress is visible.** The periodic status line gains the captured
   byte and frame count, so an unattended run can be audited by looking
   at it. Under `--json` the tick object gains `capture_bytes` and
   `capture_frames`, and the final stop object gains
   `"capture": {"path": …, "bytes": …, "frames": …, "reconnects": …}`.
   Under `-q` the capture summary still prints on exit: it is the result,
   not chatter.

### Errors and exit status

| Condition | Message | Exit |
|---|---|---|
| `--capture` in a mode with no obs stream | `--capture needs a stream mode (-d, -t, -s, -S, --check)` | 2 |
| Path cannot be opened for writing | `Cannot open capture for writing: <path>: <errno>` | 7 |
| Short write during the run | `Capture write failed after N bytes: <path>: <errno>` | 7 |
| Cap reached | `[INFO] capture: cap of N MB reached; file closed` | unchanged |

Exit 7 is new; 0–6 are taken ([cli_help.c:96](../../src/cli/cli_help.c:96)).
It must **override** the `--check` verdict, including the caution code 6:
if the artefact you were collecting does not exist, the KPI verdict is
not the news.

### Deliberately out of scope

Each of these is a real feature and none of them is this one:

- **File rotation and time-tagged filenames.** A day of multi-GNSS MSM7
  is ~180 MB (measured, 2 kB/s); rotation invites questions about naming, gaps and
  reassembly that no user has asked yet. `--capture-max` covers the
  failure mode that actually bites — a full SD card on a Pi — and is the
  single addition beyond GUI parity for that reason.
- **Capturing the ephemeris stream.** Phase 3.
- **Sidecar metadata** (start time, caster, mountpoint). Tempting,
  because `convbin -tr` needs the start time, but a `.rtcm3` with a
  companion file is no longer the format the GUI reads. If this is
  wanted, it is a format decision for both programs at once.
- **Compression.** `gzip` exists.

### Verification

| # | Test | Kind |
|---|---|---|
| V1 | Replay `bin/20260528135838_RFSEE01.rtcm3` through a session with capture on; assert the output is **byte-identical** to the input and the frame count matches | ctest, both platforms |
| V2 | Same, with junk injected between frames; assert the output is the clean file — capture is a filter | ctest |
| V3 | Capture to an unwritable path, and to a stream that fails mid-write; assert the session ends `NS_END_WRITE_ERROR` and the message names the path | ctest |
| V4 | Windows: confirm V1 passes with the output handle opened `"wb"` and **fails** with `"w"` — the guard is the point, so prove it guards | manual, once |
| V5 | A live 6 h run with `--reconnect`, then `convbin`, then a CSRS-PPP submission that returns a solution | manual, acceptance |
| V6 | A GUI capture and a CLI capture of the same stream, same minute, compared frame-count and type-histogram | manual, once |

V1 is the strongest test available and it is nearly free: for an input
that is already all-valid frames, capture-of-replay is the identity
function. It needs no network, no config file and no child process —
the test links the session layer and drives `ns_open_file` directly.
V5 is the one that proves the feature was worth building: the whole
chain in [docs/base-declaration.md](../../docs/base-declaration.md),
end to end.

### Where the code goes — and why not in the CLI

[architecture.md §3.3](../architecture.md) already answers this. Among
the things it lists as moving out of `gui/gui_thread.c` and into the
session layer: format detection, epoch-aware statistics, CRC and
malformed-frame counting, framing re-sync counting, GGA uplink, and
**capture-to-file**. This phase is that line item, arriving late.

The code confirms the shape. The CLI has three frame handlers —
`cli_on_event` for `-d`/`-t`/`-s` ([cli_stream.c:59](../../src/cli/cli_stream.c:59)),
`check_on_event` ([:435](../../src/cli/cli_stream.c:435)) and
`sky_on_event` ([main.c:357](../../src/cli/main.c:357)) — so a CLI-level
capture means the same write in three places, and a fourth if the daemon
ever wants it. In the session there is exactly one site: where the frame
has just passed CRC and is about to be emitted
([ntrip_session.c:452](../../src/session/ntrip_session.c:452)).

Putting it there also means the ephemeris session (Phase 3) and the
daemon get the capability without new code, and the GUI's hand-written
copy becomes a duplicate that can be retired on its own track.

### Implementation plan

Five steps, each one independently verifiable. Nothing in step *n*
depends on step *n+1* having been designed differently.

**Step 1 — the session layer owns the capture.** In
`src/session/ntrip_session.{h,c}`:

| Addition | Shape |
|---|---|
| `NsOptions.capture_path` | `const char *`, NULL = off |
| `NsOptions.capture_max_bytes` | `uint64_t`, 0 = unlimited |
| `ns_capture_start(NtripSession *, const char *path)` | start mid-session; returns 0 or an errno |
| `ns_capture_stop(NtripSession *)` | close and flush; safe when not capturing |
| `ns_capture_status(const NtripSession *, uint64_t *bytes, uint64_t *frames)` | returns the path, or NULL when not capturing |
| `NS_END_WRITE_ERROR` | new `NsEndReason` |

The counters go on the **session**, not into `NsStatsSnapshot`, and that
is not a style preference. That struct is serialised by
`ns_stats_to_json()` and `ns_stats_to_csv_row()`
([ns_stats.c:188](../../src/core/ns_stats.c:188),
[:404](../../src/core/ns_stats.c:404)) — the daemon's Munin output and
the GUI's CSV export. A field added there appears in both, silently
changing formats that other people's tooling already reads, to describe
something that is not a property of the stream at all. `NsStatsSnapshot`
measures what arrived; a capture is what we did with it.

The option is the convenience form — `ns_open()` calls
`ns_capture_start()` for you. The functions exist because the GUI starts
and stops a capture from a menu *during* a session, and an option fixed
at open cannot express that; designing only the option would make the
GUI migration impossible without a second redesign.

Write at the post-CRC emit site, before the event goes out. Flush on the
existing stats tick, not per frame. On a short write: close the file,
emit `NS_LOG_ERROR` naming path and byte count, end the session
`NS_END_WRITE_ERROR`. On reaching the cap: close, log at `NS_LOG_INFO`,
and **let the session continue** — a self-imposed limit is not a fault.

Tests V1–V3 land here, in `test/test_capture.c`.

**Step 2 — the CLI passes a path.** `--capture` and `--capture-max` in
`src/cli/main.c`; validation that the mode opens an observation stream;
the directory-argument case that builds `YYYYMMDDHHmmss_<mountpoint>.rtcm3`;
`NS_END_WRITE_ERROR` mapped to exit 7 in each mode's return, overriding
`--check`'s verdict. `src/cli/cli_stream.c` gains the capture counters in
the status line and the `--json` tick and stop objects. `cli_help.c` gains
two options, the exit code and one unattended example.

No `fclose` bookkeeping in `main.c`: `ns_close()` already runs on every
exit path, including the Ctrl-C and Ctrl-A paths, and closing the capture
is now its job.

**Step 3 — the documents that make claims about this.** `docs/cli.md`
(the options, the unattended recipe), `docs/base-declaration.md` (step 3
becomes CLI-first with the GUI as the alternative, and the two-connection
warning goes away), `design/todo.md` §2.2 (Phase 1 is no longer open),
`changelog.md`.

**Step 4 — acceptance.** V5, and V6 against a GUI capture of the same
stream: the two programs must produce the same frames from the same
minute, or "byte-identical" was a claim rather than a fact.

**Step 5 — retire the duplicate.** Once V6 passes, the GUI's private
capture in `gui/gui_thread.c` and the `FILE*` and critical section in
`gui/gui_state.h` are dead weight over the session's version. That is a
[gui-track.md](gui-track.md) item, not this one, and it must not be
started until this phase has shipped and been used.

## Phase 2 — `--rtcm-stdin` beyond `--sky` — open

`--rtcm-stdin` is `--sky`-only, so `-s` silently connects to the live
caster instead of replaying the file it was handed — recorded as a
caution in [todo.md §0.4](../todo.md) after it made an early comparison
look like agreement by coincidence. Silently doing something other than
what the arguments say is the worst class of defect this tool has.

Phase 1 makes this sharper rather than softer: once the CLI can create
captures, more people will feed them back in.

## Phase 3 — Capture the ephemeris stream — not scheduled

The `EPH_CASTER` block opens a second session, and nothing captures it.
It would want its own path (`--capture-eph`), because merging two streams
into one file would produce something neither program can replay.

## Decisions

All taken 2026-08-15. Nothing below is left for the implementer to
decide; where a choice was close, the losing option is named.

| Decision | Why | Rejected |
|---|---|---|
| **It lives in the session layer** | [architecture.md §3.3](../architecture.md) already assigned capture-to-file there; the CLI would need the same write in three handlers, the daemon a fourth | A `src/cli/cli_capture.{h,c}` unit — smaller diff, but it would have to be undone to migrate the GUI |
| **An option *and* `ns_capture_start/stop`** | The GUI starts a capture mid-session from a menu; an option fixed at open cannot express that, and designing only the option would force a second redesign | Option only |
| **Frames, not raw bytes** | Byte-identity with the GUI; clean `convbin` input; a capture that replays through the code path that made it | Raw bytes — captures the handshake and junk, and would make the two programs' files differ |
| **Allowed with `--rtcm-stdin`** | Makes V1 possible, and doubles as a filter that strips junk from a dirty capture before conversion | Rejecting the combination as pointless, which was my first instinct and was wrong |
| **A write failure ends the session** (`NS_END_WRITE_ERROR` → exit 7) | Twenty hours that silently stopped at hour three is the worst outcome this feature can produce | Logging and continuing, as the GUI does — right for an interactive program, wrong for an unattended one |
| **Fail at open, not at first write** | An unattended run whose purpose is the file must not discover a bad path after twenty hours | GUI parity (G8) |
| **Refuse to overwrite an existing file** | A twenty-hour capture is not cheap to regenerate. This is deliberately *unlike* `-o`, which overwrites the sky PNG — a PNG costs a minute to redraw | Overwriting for consistency with `-o`; consistency is not worth the file |
| **Flush on the one-second stats tick** | A power cut or a `kill -9` then costs at most a second of stream, at no measurable throughput cost | Per-frame flush (needless syscalls); relying on `fclose` (loses the tail exactly when you most want it) |
| **A size cap in megabytes, no rotation** | A full SD card on a Pi is the failure this audience hits. Megabytes because `--duration` already means the run, and two time limits would confuse | Duration; rotation |
| **The cap is not an error** | Reaching a limit you set yourself is the feature working | Non-zero exit |
| **A distinct exit code (7), overriding `--check`'s verdict** | A cron job must tell "the disk filled" from "the caster refused"; and if the artefact does not exist, the KPI verdict is not the news | Reusing 1 |
| **`--capture` does not imply `--reconnect`** | Flags that quietly turn on other flags are how a tool stops being predictable. The documentation pairs them; the code does not | Implying it |
| **The daemon gets the capability but no flag** | It comes free once the session owns it; a permanent rolling capture is a different feature (retention, rotation) and needs its own thinking | Wiring it now |
| **The GUI keeps its own capture until V6 passes** | Byte-identity must be demonstrated against the thing it claims parity with, not assumed, before the reference implementation is deleted | Migrating in the same change |
| **Counters on the session, not in `NsStatsSnapshot`** | That struct is serialised to the daemon's Munin JSON and the GUI's CSV export; a field added there changes formats other tooling reads, to report something that is not a property of the stream | Adding two fields where the other counters live, which is where they look like they belong |

### What this is not

It is not a refactor, and it does not remove duplication: only the GUI
has capture today, so there is none to remove. This is a new feature
placed where it will not *create* duplication, plus one small
consolidation at the end. Net lines across the tree go **up**.

Nor do all five artefacts gain. The CLI gets the feature; the daemon gets
optionality; the GUI gets a smaller codebase after Phase 5 and migration
risk before it; **Android gets a kilobyte of code that never runs**. One
winner, one maintenance win, one option, two neutral — still the right
trade against a CLI-local copy, which would buy the same benefit and then
charge for it again at the GUI migration and once more at the daemon, but
not the five-way win it can look like from the dependency graph.

And it adds one genuine complexity rather than removing it: the session
layer acquires an I/O side effect, so a session can now end for a reason
that has nothing to do with the network. Every frontend inherits that
possibility whether or not it captures. What simplifies is conceptual —
one definition of what a `.rtcm3` contains, one behaviour across a
reconnect, one place to fix a bug in either.

### Blast radius, measured

The shared layer is compiled by five artefacts, so "additive" had to be
checked rather than assumed:

| Change | Who it reaches | Effect |
|---|---|---|
| `NsOptions` +2 fields | 10 construction sites — daemon 1, CLI 5, Android 3, GUI 2 | **None.** Every one calls `ns_options_default()` first, so all ten get `NULL`/`0` and behave exactly as now |
| `NsEndReason` +1 value | `NS_END_` appears in **4 files**: the session's own `.h`/`.c`, `src/cli/main.c`, and this plan | One frontend file. The GUI, the Android bridge and the daemon never inspect the end reason — they react to `NS_EV_DISCONNECTED` generically |
| New enumerator vs. warnings | no `-Werror` in this project's builds (only vendored cJSON has it, for itself) | At worst a `-Wswitch` note in the one switch that exists, in the file being edited anyway |
| New code in the session | Android free, Android pro, GUI, daemon, CLI | Compiles everywhere; inert everywhere the path is `NULL`. No Kotlin change, so edition parity is untouched |

The residual risk sits in one place, and it is the ironic one: a
session-layer change is compiled in CI by the CLI, the daemon, the tests
and both Android editions — but **not the GUI**, per
[ci.yml](../../.github/workflows/ci.yml). The single artefact that today
owns a capture implementation is the one CI will not check while its
replacement is built. That argues for doing
[gui-track.md](gui-track.md) Phase 3's compile-only job **before**
Phase 4, not after.

## Open questions

None blocking. Three to revisit after it ships:

- Should **all** stream modes stop gracefully on Ctrl-C, not only while
  capturing? `-t 300` interrupted at 290 s currently prints no table at
  all, which is a worse ending than the one capture now gets. It is a
  behaviour change for people who have scripts around the current one.


- Whether the daemon should host a permanent rolling capture, with the
  retention policy that implies.
- Whether a sidecar with the start time is worth breaking format
  compatibility for, given `convbin -tr` needs exactly that value and
  currently gets it from the operator's memory.

## Outcome

Phase 1 built 2026-08-15. What the plan said would happen, and what did:

| # | Test | Result |
|---|---|---|
| V1 | capture-of-replay is the identity | **pass** — and on real data: the GUI's own 206-frame capture, replayed through the CLI with `--capture`, comes out byte-identical (56,898 bytes) |
| V2 | junk and bad CRCs are filtered | **pass** — NMEA between frames and a deliberately corrupted frame are both absent from the output |
| V3 | a capture that cannot be written is fatal | **pass** — refusing to overwrite and an unopenable path both end `NS_END_WRITE_ERROR`; the CLI returns 7 |
| V4 | binary mode is what makes V1 work | not run as a build variant; the `"wb"` is in one place and commented |
| V5 | 6 h live → `convbin` → CSRS-PPP | **outstanding.** Needs a day, not a command |
| V6 | GUI and CLI agree | **half done.** Offline: identical on the GUI's file. Live, side by side on one stream: outstanding, and it gates the GUI track's Phase 4 |

Live behaviour, 30 s against the Kadaster caster: 159 frames, 62,503
bytes, and the frame count matches the message census type for type
(2×1006, 2×1008, 1×1013, 2×1033, 30×1077, 30×1087, 30×1097, 60×1127,
2×1230 = 159). That capture then replays and re-captures identically.

Two things the plan did not foresee, both now in the code:

- **An open failure had to count as a capture failure.** The plan had
  `ns_capture_failed()` meaning "a write failed", so a run that could not
  create the file at all would have exited 0. It now covers both, while a
  failed `ns_capture_start()` call still does not — that is a menu item
  reporting a bad path, not a run losing its purpose.
- **The three mode functions had to start returning `int`.** They were
  `void`, and `main()` returned 0 for them regardless, so there was no
  path for exit 7 to travel. That is API churn the plan should have
  named.
- **Ctrl-C was handled in `--sky` alone**, so the mode documented as the
  unattended one would have been killed mid-buffer. Handled now while
  capturing; see behaviour 7.

The flush design was tested the hard way, by accident: a capture whose
process was **terminated outright**, with no clean close, left 374,705
bytes that replay to 936 frames and re-capture to exactly the same size.
A killed run loses at most the last second and leaves a file every
consumer can read. What is *not* verified is the graceful path itself —
a real console Ctrl-C cannot be delivered to a native Windows process
from this shell, so that wiring is by inspection until somebody presses
the key.

Cost: the shared layer grew ~120 lines, the CLI ~60, the test 260. The
GUI's duplicate — about 40 lines — is still there by design until V6
completes, so the tree is temporarily one implementation heavier, exactly
as [What this is not](#what-this-is-not) said it would be.
