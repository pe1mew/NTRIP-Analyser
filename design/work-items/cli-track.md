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
| 1 | Capture the stream to a file, with reconnect | **built 2026-08-15; V5 passed 2026-08-16.** Only V6's live half remains |
| 2 | `--rtcm-stdin` beyond `--sky` | **built 2026-08-17** |
| 3 | Capture the ephemeris stream | not scheduled |
| 4 | KPI 1 blames the station when the stream stops | **built 2026-08-17**; observed 2026-08-16 and again 2026-08-17 |
| 5 | `--report` — tier 2 in the CLI | **built 2026-08-16**, see [measurement-tiers.md](measurement-tiers.md) §2a |
| 6 | `--thresholds`, `--thresholds-print` | **built 2026-08-17**, see [thresholds-track.md](thresholds-track.md) |
| 7 | A run that ends without a verdict says so | **built 2026-08-17** |

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

## Phase 2 — `--rtcm-stdin` beyond `--sky` — **built 2026-08-17**

`--rtcm-stdin` was `--sky`-only, so `-s` silently connected to the live
caster instead of replaying the file it was handed — recorded as a
caution in [todo.md §0.4](../todo.md) after it made an early comparison
look like agreement by coincidence. Silently doing something other than
what the arguments say is the worst class of defect this tool has.

It now reaches `-d`, `-t` and `-s`: `cli_run()` opens stdin through
`ns_open_stream()` and everything downstream is the live code path. The
duration bounds the **stream** analysed, so `-t 3600` means the same
thing live and offline.

**Rejected, not ignored.** Modes that cannot honour the flag now refuse
it with exit 2 rather than dropping it — `--check` among them, because
tier 1 is a live acceptance test and a capture holds no arrival times.
This was written the moment the defect was confirmed: `-t 600
--rtcm-stdin` opened a live connection and analysed *that* for ten
minutes while the file sat unread on stdin.

### What the flag alone did not fix

Wiring it up was the small part. Two faults underneath it made the first
replayed reports meaningless, and neither was visible until a capture
was actually reported on:

- **Staleness was measured against the host clock.** `sv_track` and
  `iono` ask how long ago something was seen, and six hours of file
  arrive in milliseconds — so offline, every satellite looked current
  and every epoch interval read `0.000 s`. `obs_clock()` in
  `ntrip_session.c` now supplies arrival time live and the stream clock
  on a replay. **Live behaviour is unchanged**, because live the two are
  the same clock.
- **A replay read 8 KB a pump**, and statistics are recomputed once a
  pump — so a station sending 1.6 KB an epoch was sampled every six
  seconds offline against once a second live. An analysis must not be
  coarser offline than live; the replay now reads a kilobyte at a time,
  which took the same capture from 19 samples to 88.

### A live report and its replay may legitimately differ

Measured on one 120-second HANESE session: the live report recorded 29
satellites at its worst, the replay of that same session recorded 38.
Neither is wrong. The live message-type table shows a 7.4 s arrival gap
while the capture's epochs are consecutive at 1.000 s — the caster
stalled and then delivered a burst, and for those seven seconds the
analyser could not see satellites that had not yet arrived.

A live run answers *what am I being given*; a replay answers *what did
the station send*. Stated in `docs/cli.md` §2c rather than left for a
user to discover by comparing two reports and doubting both.

## Phase 4 — KPI 1 blames the station when the stream stops — **built**

Observed against `HANESE` on 2026-08-16, and the two lines are from one
report:

```
1  Connected and producing  FAIL  0.00    Connected but no data arriving
2  RTCM 3.x format          PASS  102.00  CRC-valid RTCM 3.x frames decoded
```

**They contradict each other.** A hundred and two frames had arrived, and
were decoded and counted; the stream then stopped fifteen seconds in, so
the throughput KPI 1 measures fell to zero. Its `detail` string describes
the instantaneous state as though it were the session's history, and the
run reads as a station that never delivered.

**Seen again on 2026-08-17**, against Centipede's `NEAR`, in exactly the
same shape:

```
1  Connected and producing  FAIL  0 B/s  Connected but no data arriving
2  RTCM 3.x format          PASS  289    CRC-valid RTCM 3.x frames decoded
```

Same cause as the first sighting, too: three connections to that caster
within a few minutes, the last of which was starved after thirty
seconds. Two independent recurrences, both of them the analyser's own
traffic rather than a station fault, is the argument for fixing the
wording rather than filing it as cosmetic — it has now twice produced a
report that reads as an accusation against a healthy station.

The verdict itself is right and should not change. `--check` disables
auto-reconnect on purpose — "a drop is a finding here, not a nuisance to
paper over" — so a session that dies *is* a failure. What is wrong is the
explanation, and the explanation is what a user acts on.

**Why it matters more than a wording nit.** This tool exists to say
whether a *station* is fit. A station that streams perfectly and a caster
that drops the session produce the same six words, and the user takes the
first answer to the station's owner. In this case the truth was neither:
the caster allows one session per account, and three checks run in quick
succession — each of which opens a second connection to fetch the
sourcetable — evicted one another. The station was healthy throughout,
and gave 45 of 45 epochs a minute later.

**What it should say.** Distinguish three states that currently share one
message:

| State | Evidence available | Message |
|---|---|---|
| Nothing ever arrived | `bytes_total == 0` | "Connected, but the caster sent nothing" |
| Data arrived and stopped | `bytes_total > 0`, no bytes in the window | "The stream stopped after N s — the caster closed the session or the link dropped" |
| Data arriving too slowly | bytes in the window, below the floor | the current wording, which is accurate for this case |

Everything needed is already in the snapshot: `bytes_total`, the session
clock, and the end reason the session layer emits (`NS_END_EOF` against
`NS_END_NET_ERROR`). Nothing new has to be measured.

**Where the fix lives.** `src/core/kpi.c`, not the CLI — so it reaches
the GUI's station check and both Android editions at the same time. It is
logged on this track because this is where it was found; whoever picks it
up should expect to touch the shared engine and to check the string does
not overflow the GUI's column.

### Built, 2026-08-17

The three states are told apart exactly as the table above specifies,
with two departures from it, both deliberate.

**The wording does not name a culprit.** The plan's message was *"the
caster closed the session or the link dropped"*, and that is a guess this
engine is not entitled to make: a base station that stops feeding its
caster produces precisely the same evidence, and then the sentence would
be exonerating the station rather than accusing it — the same error in
the opposite direction. What KPI 1 can see is that data arrived, for how
long, and that it stopped, so that is all it says. The
[Troubleshooting](../../docs/wiki/Troubleshooting.md) page carries the
causes, ranked, with the one-session-per-account eviction first, because
that is where a reader can weigh them.

**The `!connected` branch was fixed too**, though the plan only listed
the three connected states. *"No connection to the caster"* against a
session that had delivered for an hour is the same defect wearing a
different coat, and it reads as *"we never got in"*.

Four messages now, where there were two:

| State | Message |
|---|---|
| Never connected | `No connection to the caster` |
| Connected, nothing ever sent | `Connected, but the caster has sent nothing` |
| Delivered, then silent | `Data arrived for 15 s, then the stream stopped` |
| Delivered, then the socket went | `Connection lost after 15 s of data` |

**The number is the session's clock, not the check's** — `uptime_s` at
the last byte, so a check begun an hour into a stream says the stream ran
an hour, not that it ran the minute the check has been watching.

**Two things it needed that the snapshot could not give.** When data was
last seen has to be remembered between updates, so `KpiRun` gained
`bytes_seen` and `bytes_up_s`; and a message carrying a number cannot be
a string literal, so it gained an 80-byte buffer that `KpiResult::detail`
points into. That is the first non-literal detail in the engine, and
`kpi.h` now says what its lifetime is: valid until the next
`kpi_update` on that run. Every caller holds its run and its report
together — the GUI in `AppState`, the CLI on one stack frame, Android in
`struct NtripBridge` — so no caller had to change.

**Verified against a caster that misbehaves on purpose.** Reproducing
this live was what made it a two-sighting bug rather than a fixed one:
both sightings were single-session casters evicting the analyser's own
earlier connection, and deliberately doing that again to a public caster
is antisocial. So the three states are produced locally instead —
`test/tools/stub_caster.py` now takes a mode (`flow`, `silent`, `stop`,
`drop`) and feeds a real 57 KB capture before it goes quiet, sends
nothing at all, or closes the socket. In the repository rather than in a
scratch file, because a state that cannot be reproduced next month is a
state that will regress unnoticed; the modes and the sentence each must
produce are in `docs/RUNBOOK.md`.

```
1  Connected and producing  FAIL  0 B/s  Data arrived for 1 s, then the stream stopped
2  RTCM 3.x format          PASS  206    CRC-valid RTCM 3.x frames decoded
```

The two lines no longer contradict each other, which was the whole
complaint. The silent caster gives `Connected, but the caster has sent
nothing` with KPI 2 failing beside it — agreeing, this time — and the
closed socket gives `Connection lost after 1 s of data` and ends the run
`== NO VERDICT ==  the stream closed after 1 s`, which is phase 7 doing
its job on the same run.

`test/test_kpi_stopped.c` pins all four messages, that the verdicts did
not move, that the number comes from the session's clock, and that the
longest of them still fits the GUI's Detail column. Six of its thirteen
assertions fail against the engine as it was; the rest pass, which is
what says the fix changed the sentence and not the verdict.

**Confirmed in the GUI**, 2026-08-17, against the same stub with a 60 s
feed: the station-check window shows the new sentence and the Detail
column does not clip it. Worth doing rather than assuming, since the
column width was the one thing the engine could not know about itself —
and the fix reaching all four programs from one place is the claim this
track keeps making.

## Phase 3 — Capture the ephemeris stream — not scheduled

The `EPH_CASTER` block opens a second session, and nothing captures it.
It would want its own path (`--capture-eph`), because merging two streams
into one file would produce something neither program can replay.

## Phases 5–7 — what the CLI gained afterwards

Three surfaces landed after this track was written. Each is designed on
another track; recorded here so the CLI's own surface is described in
one place.

### Phase 5 — `--report` (tier 2), built 2026-08-16

Rides on the modes that already run for a duration — `-t`, `-s`, `-d`,
`--check` — rather than becoming a mode of its own, because tier 2 is a
second reading of the same session rather than a different activity. It
**never changes the exit code**: `--check` owns that, and two verdicts
competing for one exit status is how an automation surface becomes
unusable. Design in [measurement-tiers.md](measurement-tiers.md) §2a.

### Phase 6 — `--thresholds` and `--thresholds-print`, built 2026-08-17

A policy file judges the run instead of the built-in numbers, and
`--thresholds-print` says what is in force field by field, with
provenance, needing no config and no network. A run under a non-default
policy names it and its fingerprint above the verdict, because once
verdicts can come from different standards, `STATION OK` means nothing
between two people unless each says which standard produced it. Design
and the five decisions behind it in
[thresholds-track.md](thresholds-track.md).

### Phase 7 — a run that ends without a verdict says so, built 2026-08-17

Found by running `--check-vrs` against a live nearest-base service: the
gate test can answer *before* the eight checks have held their sustain
window, so the run ended with `== RUNNING ==` as the last line of a
finished report. There was no verdict, and the live roll-up was being
printed as though it were one.

```
== NO VERDICT ==  the gate test finished after 211 s  exit=6
The checks above are the last reading, not a conclusion: the verdict had not held for 60 s.
```

Five endings are each named — the verdict settled, a check failed
outright, the gate test finished, the stream closed, the 300 s limit was
reached — and the sustain figure comes from the policy in force, so it
is right even when a file has changed it. The GUI had worded this case
since it was built (§13.7a of [gui-design.md](../gui-design.md)); the
CLI had not, which is the sort of divergence that only shows up when
someone runs both.

---

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

Phases 1, 2, 5, 6 and 7 built. Phase 1's verification table is below;
the later phases were each verified against a live caster or a real
capture as they landed, and the evidence is in their sections above.

**The CLI's surface now**, in the order a user meets it: `--capture` and
`--capture-max` to record, `--rtcm-stdin` to read a recording back
through the identical code path, `--check` / `--check-vrs` for tier 1,
`--report` for tier 2, and `--thresholds` / `--thresholds-print` to
judge by a standard of one's own and to ask what that standard is.

Phase 1 built 2026-08-15. What the plan said would happen, and what did:

| # | Test | Result |
|---|---|---|
| V1 | capture-of-replay is the identity | **pass** — and on real data: the GUI's own 206-frame capture, replayed through the CLI with `--capture`, comes out byte-identical (56,898 bytes) |
| V2 | junk and bad CRCs are filtered | **pass** — NMEA between frames and a deliberately corrupted frame are both absent from the output |
| V3 | a capture that cannot be written is fatal | **pass** — refusing to overwrite and an unopenable path both end `NS_END_WRITE_ERROR`; the CLI returns 7 |
| V4 | binary mode is what makes V1 work | not run as a build variant; the `"wb"` is in one place and commented |
| V5 | 6 h live → `convbin` → CSRS-PPP | **passed 2026-08-16.** See below |
| V6 | GUI and CLI agree | **half done.** Offline: identical on the GUI's file. Live, side by side on one stream: outstanding, and it gates the GUI track's Phase 4 |

Live behaviour, 30 s against the Kadaster caster: 159 frames, 62,503
bytes, and the frame count matches the message census type for type
(2×1006, 2×1008, 1×1013, 2×1033, 30×1077, 30×1087, 30×1097, 60×1127,
2×1230 = 159). That capture then replays and re-captures identically.

**V5, the acceptance test, 2026-08-16.** Six hours of `RFSEE01` under
`systemd-run`, 18:13:28–00:13:28 UTC: 35.7 MB, and **720 of a possible
720 epochs** at 30 s decimation — not one lost, so the link held for the
whole session. `convbin` produced a RINEX 3.04 observation file whose
header carried everything the run had established, and CSRS-PPP returned
a static solution: σ95 of 4 mm east, 6 mm north, 16 mm up, with the
antenna's NGS calibration recognised and applied.

The feature justified itself on that first real run. The solution put the
station **1.92 m below the position it broadcasts**, an error every rover
taking RTK from it had been inheriting, unnoticed. A capture, a
converter and six hours of patience found it; ninety seconds of KPIs
never could, because the station is perfectly healthy — it is simply in
the wrong place.

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
