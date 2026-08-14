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

[design/todo.md](../todo.md) keeps its job: the cross-cutting idea list,
where a thought is recorded before anyone has decided to build it. A
track file is the narrower thing — what is being built, in what order,
and what "done" will mean.

## Current status

| Phase | What | State |
|---|---|---|
| 1 | Capture the stream to a file, with reconnect | **specified 2026-08-14** |
| 2 | `--rtcm-stdin` beyond `--sky` | open |
| 3 | Capture the ephemeris stream | not scheduled |

---

## Phase 1 — Capture the stream to a file, with reconnect — **specified 2026-08-14**

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
  is ~100 MB; rotation invites questions about naming, gaps and
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
| V1 | Replay `bin/20260528135838_RFSEE01.rtcm3` through `--sky --rtcm-stdin --capture out.rtcm3`; assert `out` is **byte-identical** to the input and the frame count matches | ctest, both platforms |
| V2 | Same, with random junk injected between frames; assert the output is the clean file — capture is a filter | ctest |
| V3 | Capture to an unwritable path, and to a path that fills mid-run; assert exit 7 and that the message names the path | ctest |
| V4 | Windows: confirm V1 passes on a build where the output handle is `"wb"`, and fails on `"w"` — the guard is the point | manual, once |
| V5 | A live 6 h run with `--reconnect`, then `convbin`, then a CSRS-PPP submission that returns a solution | manual, acceptance |

V1 is the strongest test available and it is nearly free: for an input
that is already all-valid frames, capture-of-replay is the identity
function. V5 is the one that proves the feature was worth building —
it is the whole chain in
[docs/base-declaration.md](../../docs/base-declaration.md), end to end.

### Files this touches

| File | Change |
|---|---|
| `src/cli/main.c` | two `getopt_long` entries, validation against the chosen mode, open before connect, close on every exit path |
| `src/cli/cli_stream.c` | the frame-event write, the counters, the status line and the JSON fields |
| `src/cli/cli_stream.h` | the capture state shared with `main.c`, documented as `cli_auto_reconnect` is |
| `src/cli/cli_help.c` | the two options, the new exit code, one example |
| `test/test_capture.c` | V1–V3 (new; sixth test) |
| `docs/cli.md` | the options, and the unattended recipe |
| `docs/base-declaration.md` | step 3 becomes CLI-first, GUI as the alternative |
| `design/todo.md` §2.2 | correct "Shipped" to name the GUI, and point here |
| `changelog.md` | one line |

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

| Decision | Why |
|---|---|
| **Frames, not raw bytes** | Byte-identity with the GUI; clean `convbin` input; a capture that replays through the same code path that made it |
| **`--capture` is allowed with `--rtcm-stdin`** | It makes V1 possible — the strongest test here — and doubles as a filter that strips junk from a dirty capture before conversion |
| **Fail at open, not at first write** | An unattended run whose purpose is the file must not discover a bad path after twenty hours |
| **A size cap, but no rotation** | The cap prevents a full root filesystem on a Pi, which is the failure this audience actually hits; rotation raises questions nobody has asked |
| **A distinct exit code (7)** | A cron job must be able to tell "the disk filled" from "the caster refused" |
| **`--capture` does not imply `--reconnect`** | Flags that quietly turn on other flags are how a tool stops being predictable. The documentation pairs them; the code does not |

## Open questions

- Should the monitoring daemon capture too? It is the natural host for a
  permanent rolling capture, and that is a different feature (rotation,
  retention) from this one.
- Is `--capture-max` in megabytes right, or should it be a duration? A
  duration is what an operator thinks in, but `--duration` already exists
  for the run itself and two time limits would confuse.

## Outcome

Not built yet. This section gets the measured result — file sizes,
reconnect counts across a real overnight run, and whether V5's PPP
solution came back clean.
