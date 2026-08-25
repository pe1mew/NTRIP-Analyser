# What each product does

Five programs are built from this repository, and the question "does X
exist, and where?" has until now been answered by reading four different
places. This is that answer in one table.

**It is a record of what is built, not of what is intended.** The
intent lives elsewhere: [`android/design/editions.md`](../android/design/editions.md)
argues the free/paid split, [`design/todo.md`](todo.md) tracks ideas and
their status, and the work-item tracks say what is being built next. When
this file and one of those disagree, this one is wrong until checked
against the code — and `tools/check_release.py` enforces the one part of
it that can be checked mechanically (see the foot of the file).

Compiled from the defining sources on 2026-08-15: `src/cli/cli_help.c`,
`gui/resource.h` and the GUI's command dispatch, both editions'
`Features.kt` and the Android composables, and `service/ntrip-monitord.c`.

**Legend** — ● built · ◐ partial · ○ absent · ⋯ planned, not built

## Connection and configuration

| Feature | What it is, and why | CLI | GUI | Free | Pro | Daemon |
|---|---|---|---|---|---|---|
| NTRIP v1/v2 client, Basic auth | Connects, authenticates, reports which protocol the caster answered. The handshake is itself a diagnostic: a v1 ICY answer from a caster claiming v2 is a finding. | ● | ● | ● | ● | ● |
| Sourcetable retrieval | Lists what the caster advertises — the only statement of intent to judge delivery against, which is what KPI 8 does. | ● | ● | ● | ● | ○ |
| Sourcetable as a picker | Tap a row to use it, taking its position and `nmea` flag along. The information is free; the workflow is the paid proposition. | ○ | ◐ | ○ | ● | ○ |
| Saved connections | The CLI reads `mountpoints[]` and uses the first, saying how many it ignored. Free keeps one, pro sixteen, the daemon holds all of them at once. | ◐ | ◐ | ◐ | ● | ● |
| Encrypted credential store | Android encrypts profile credentials at rest. Desktop configs hold passwords in the clear, by design and documented — `docs/jsonConfigs.md`. | ○ | ○ | ● | ● | ○ |
| Config file load / save | One JSON format across all five programs, so a configuration moves between desktop, daemon and phone unchanged. The daemon reads and never writes — a service that rewrites its own configuration is a service you cannot reason about. | ● | ● | ○ | ● | ◐ |
| Field overrides (CLI / env) | `--caster`, `NTRIP_PASSWORD=…`: credentials and targets without editing a file, for CI and one-off runs. | ● | ○ | ○ | ○ | ○ |
| Config dry-run | `--check-config` applies overrides, resolves DNS, prints the result and exits. Fail-fast before a long run. | ● | ○ | ○ | ○ | ○ |
| Position from map / clipboard | Hands off to a map application and takes the answer back, so no map SDK is embedded and no map licence follows the product. | ○ | ● | ● | ● | ○ |
| GGA uplink, fixed position | Emulates a rover at a set position. A VRS mountpoint sends nothing until it receives one. | ● | ● | ● | ● | ● |
| GGA uplink, live phone position | Answers for where the user actually stands — the field question. Behind a one-time consent, and falls back to the configured position without a fix, because a GGA of 0,0 puts the rover in the Atlantic and a VRS will answer it. | ○ | ○ | ○ | ● | ○ |

## Measurement — the shared core

Everything here is computed in `src/core`. No frontend holds a threshold
or forms a verdict; that rule is what makes the next row true.

| Feature | What it is, and why | CLI | GUI | Free | Pro | Daemon |
|---|---|---|---|---|---|---|
| Eight-KPI acceptance test | A bounded ~90 s verdict: connected, RTCM 3, ARP, observations, satellites, C/N0, CRC, advertised-versus-actual. Every KPI must hold for 60 continuous seconds, so a lucky second cannot pass a station. **Identical in the four products that have it** — a free STATION OK means exactly what a paid one means. The daemon links `kpi.c` but never calls it, and no KPI appears in the snapshot schema: it reports measurements and leaves the verdict to whoever reads them. | ● | ● | ● | ● | ○ |
| VRS / network-RTK assertions | Five further checks and the gate test: stop the GGA and classify the service by how the stream reacts. Single-base checks actively mislead on a VRS, where a moving 1005 is correct operation. | ● | ● | ○ | ● | ○ |
| Stability over hours (tier 2) | The question ninety seconds cannot reach at any price: has this station *been* fit? Six metrics — availability, frame integrity, signal level, satellites held, ionosphere, delivery rate — over a window of **stream** time, so a replay reproduces a live run exactly. Its own vocabulary, never tier 1's: `STABLE` / `DEGRADED` / `UNSTABLE`, with `INSUFFICIENT EVIDENCE` a real verdict rather than a failure — below ten minutes, and again once the stream clock stops moving. It never touches the exit code; `--check` owns that. In pro since watch mode gave a phone the runtime to out-wait the evidence floor; free's two-minute check never can, so free deliberately has no card to shrug with. | ● | ● | ○ | ● | ● |
| Thresholds as data, not constants | Every limit either tier judges by, settable from a policy file, with a name and an FNV-1a fingerprint printed above the verdict. Once verdicts can come from different standards, `STATION OK` means nothing between two people unless each says which standard produced it. `--thresholds` and `--thresholds-print` in the CLI, `File → Load thresholds` in the GUI, a `thresholds` key in the daemon's config. Android judges by the built-in numbers only — `thresholds.c` is not in that build either, so there is no policy to load and no limit to show beside a row. | ● | ● | ○ | ○ | ● |
| Message-type census | Counts and per-epoch intervals per type. Epoch-based, so an MSM message split across frames is not reported at a multiple of its true rate. | ● | ● | ○ | ◐ | ● |
| Satellites per constellation | Unique SVs, from MSM1–7 and the legacy 1001–1012 families both. | ● | ● | ● | ● | ● |
| C/N0 per satellite and band | Resolution follows the message family — 6-bit whole dB in MSM4/5, 1/16 dB in MSM6/7, ¼ dB in the legacy messages. That property has twice been reported as an application defect. | ○ | ● | ● | ● | ● |
| Frame integrity accounting | CRC-24Q failures and framing re-syncs, as a rate. A stream can look healthy while losing two frames in a hundred. | ◐ | ● | ◐ | ◐ | ● |
| ARP decode | 1005/1006 reference position. Pro adds station ID, ITRF year, reference-versus-receiver, oscillator and raw ECEF. | ● | ● | ◐ | ● | ● |
| Ephemeris decode from the observation stream | 1019/1020/1042/1044/1045/1046 through a single seven-type switch. Lets a station place its own satellites with nothing configured. The daemon decodes them with the rest of the session layer but surfaces nothing: it publishes no orbit and draws no sky. | ● | ● | ● | ● | ◐ |
| Ionospheric ROTI | Geometry-free dual-frequency combination from MSM6/7, per satellite and as a polar heatmap. No other free NTRIP tool does this. | ○ | ● | ⋯ | ⋯ | ● |
| Session history time-series | Six metrics across the session. Min/max/average hide dropouts — a 45 s gap and a steady stream can average alike. | ○ | ● | ○ | ⋯ | ◐ |
| Rover-to-ARP distance and hand-over | Live distance and direction, with accumulated ARP dots that reveal a network switching stations under you. | ◐ | ● | ○ | ● | ○ |

## Visualisation

| Feature | What it is, and why | CLI | GUI | Free | Pro | Daemon |
|---|---|---|---|---|---|---|
| Sky plot / coverage heatmap | Where the station actually delivers signal, by azimuth and elevation — the obstruction survey a photograph cannot give. The CLI writes a PNG; the others draw it live. | ● | ● | ● | ● | ○ |
| Satellite tracks on the sky plot | Where each satellite has *been*, drawn behind where it is. One epoch shows a gap; a session shows a shadow -- which is how an obstruction at 20° in the south-west tells itself apart from a satellite that happened to be missing. Same rules in every product: a point a minute, arcs broken where a satellite set and rose again. | ○ | ● | ○ | ● | ○ |
| C/N0 against elevation | The antenna-and-LNA fingerprint: a healthy chain rises smoothly with elevation, and a sick one does not. | ○ | ● | ● | ● | ○ |
| Orbit-source badge | States where the placement came from: green for a real orbit source, red for a navigation file too old to place anything, amber for the phone's own receiver. A stale file used to read as a full cache. | ○ | ○ | ● | ● | ○ |
| Live decoded stream log | The message stream as text, for reading a station rather than judging it. | ● | ● | ○ | ○ | ○ |

## Data in and out

| Feature | What it is, and why | CLI | GUI | Free | Pro | Daemon |
|---|---|---|---|---|---|---|
| RTCM capture to file | CRC-valid frames only, so a capture is clean converter input by construction and byte-identical whichever program wrote it. Lives in the session layer since 3.4.0, which is why the daemon has it without a flag to reach it. | ● | ● | ○ | ○ | ◐ |
| Offline replay of a capture | The same code path as a live stream, so a capture is analysed exactly as the stream was. Reproducible bug reports, and regression tests that need no caster. | ● | ● | ○ | ○ | ○ |
| RINEX 3 NAV import | User-supplied orbits. The product never downloads a navigation file, so the licence relationship stays the user's. | ● | ● | ● | ● | ○ |
| Sky plot export (PNG) | Timestamped and scriptable, so a cron job can produce a nightly coverage plot. | ● | ● | ○ | ○ | ○ |
| Statistics export | The GUI writes CSV and JSON; the CLI emits one JSON object per tick on stderr. The app exports the document as JSON or the daemon's CSV, mid-run or after. | ◐ | ● | ○ | ● | ● |
| Snapshot publishing | Atomic per-mountpoint JSON — written to a temporary file and renamed, so a reader never sees half a snapshot. Munin reads it; so can anything else. | ○ | ○ | ○ | ○ | ● |

## Operation

| Feature | What it is, and why | CLI | GUI | Free | Pro | Daemon |
|---|---|---|---|---|---|---|
| Auto-reconnect with backoff | A dropped link leaves a gap rather than a truncated run. Off by default on finite analyses, which should fail loudly instead of quietly extending their window. | ● | ● | ○ | ○ | ● |
| Unattended long runs | `-t 86400 --reconnect --capture`, closing cleanly on SIGTERM so `systemctl stop` does not abandon the file. | ● | ◐ | ○ | ○ | ● |
| Watch mode | Continuous monitoring until stopped. Spot-checking is unlimited and free; "does it *keep* passing?" takes hours of measurement and is the paid proposition. | ● | ● | ○ | ● | ● |
| Foreground service and notification | Survives backgrounding, with Android 15's six-hour service timeout handled explicitly. | ○ | ○ | ◐ | ● | — |
| Tray minimise, layout reset | Desktop ergonomics for a program left running for days. | ○ | ● | ○ | ○ | ○ |
| Scripting surface | `-q`, `--json`, and documented exit codes 0–7 — including 7, capture failed, which outranks every other verdict because a missing artefact is the news. The daemon's `--oneshot` writes a single snapshot and exits, which is the same idea for a cron job that wants one sample. | ● | ○ | ○ | ○ | ◐ |
| Telemetry | None, anywhere: no SDK, no endpoint, no consent flow. Install figures come from Play Console. | ○ | ○ | ○ | ○ | ○ |
| TLS to the caster | Shipped 2026-08-25 (TLS rollout, `tls` branch; releases as 3.8.0): mbedTLS behind the `ns_transport` seam, verification mandatory against the embedded Mozilla roots, hostname checked, chunked NTRIP 2 decoded. An explicit per-connection flag — the stream, the sourcetable fetch and the ephemeris side-stream each carry their own — in **both editions the same day**, ungated: the paid edition withholds convenience, never protection. | ● | ● | ● | ● | ● |

## The shape of the split

**Free against pro.** Free is a *spot checker* with no limit on what it
will judge; pro is a *monitor* that answers over time and in the field.
Every paid capability is more of something, never a different verdict.
The entitlement is the installed APK rather than an in-app unlock,
because that works in a field with no signal.

That intent is now the build (2026-08-25): answering *over time* is
tier 2, shipped in pro as the Stability card the day the runtime
argument expired — watch mode holds a ten-minute run, so the evidence
floor is reachable. It is the clearest paid/free line the product has,
precisely because it leaves the ninety-second verdict untouched: the
free `STATION OK` stays exactly as free and exactly as trustworthy,
and free deliberately has no card to shrug with.

**CLI against GUI.** They diverge by role rather than by rank. The GUI is
the analysis instrument — ionosphere, history, VRS monitor and signal
quality are GUI-only — and the CLI is the automation surface, with exit
codes, JSON output and unattended capture. That is why the CLI has no
ionosphere view and the GUI has no exit codes, and neither is a gap.

**The daemon** has the whole core and no interactive surface at all. It
exists because munin-node expects an answer in seconds, while rates need
a persistent session and dropouts are invisible to a probe that connects
briefly per poll. Its own section follows.

## `ntrip-monitord` — the unattended product

The only program here with no user in front of it. It answers a different
question from the other four: not "is this station fit?" — it forms no
verdict at all — but "what has this station been doing, continuously,
for months?"

### Its surface

| Part | What it is, and why |
|---|---|
| `--config <path>` | The one JSON format, with `output_dir` and `interval_s` added. Read at start, never written back: a service that rewrites its own configuration is one you cannot reason about. |
| `--oneshot` | Write one snapshot and exit. For a cron job that wants a sample rather than a resident process, and for testing the configuration without installing anything. |
| `--version`, `--help` | Same version as every other artefact, from `src/core/version.h`. |
| Many mountpoints at once | One thread round-robins `ns_pump()` across every configured session. Adequate to roughly a dozen; beyond that it needs revisiting, and `design/architecture.md` §10 says so. |
| `<output_dir>/<mountpoint>.json` | One line of JSON per mountpoint, the same `NsStatsSnapshot` schema the GUI exports. Written to a temporary file and `rename()`d into place, so a reader never observes half a snapshot — the failure that makes a monitoring system lie. |

### What it publishes

The Munin plugin in `service/munin/ntrip_monitor` turns each snapshot into
seven graphs per mountpoint. They are the measurements that decay slowly
and matter over months, which is a different set from what a spot check
looks at:

| Graph | Why it is worth a year of RRD |
|---|---|
| Throughput | The first thing to move when a station degrades, and the easiest to attribute. |
| Frame integrity | CRC errors and framing re-syncs, separately — a link fault and a receiver fault look different here. |
| Satellites tracked | A slow decline is an antenna or a horizon changing, not a bad day. |
| Mean C/N0 | The antenna and LNA chain, which ages. |
| Ionosphere (ROTI) | Median and worst satellite. Space weather is the one cause of poor RTK that is nobody's fault and needs proving. |
| Availability | Connected state and the reconnect count — the dropouts a per-poll probe cannot see. |
| Message rate per type | A station that silently stops sending 1005, or halves its MSM rate, shows up here and nowhere else. |

### How it is shipped

Packaged as a tarball rather than a bare binary, because a daemon without
its unit file is about a quarter of a product: the executable, a hardened
`systemd` unit, a `sysusers` fragment for its own unprivileged account,
the Munin plugin, an example configuration and `docs/service.md`.

The unit is deliberately confined — `User=ntrip-monitor`,
`ProtectSystem=strict`, `ProtectHome=yes`, `PrivateTmp=yes`,
`NoNewPrivileges=yes`, `RestrictAddressFamilies=AF_INET AF_INET6`, with
`StateDirectory` and `ConfigurationDirectory` doing the permissions. It
is a long-lived network client parsing hostile input from a caster it
does not control, which is exactly the thing to keep in a box; the
reasoning is in `design/security-review.md`.

## Edition gates

Every flag in the two `Features.kt` objects, and the row above it
governs. `tools/check_release.py` asserts that each of these names
appears in this file, so a new gate cannot be added without the matrix
gaining a row for it.

| Flag | Free | Pro | Gates |
|---|---|---|---|
| `IS_PRO` | false | true | Extended ARP detail, the received-types list, config load/save |
| `HAS_WATCH` | false | true | Watch mode and the unbounded foreground service |
| `HAS_EPH_STREAM` | false | true | The on-demand ephemeris side-stream |
| `HAS_TRACKS` | false | true | Trails on the sky plot: where each satellite has been |
| `HAS_VRS_CHECK` | false | true | The network-RTK check: five assertions and the gate test on the hub |
| `HAS_HANDOVER` | false | true | Rover-to-ARP distance and the hand-over history |
| `HAS_EXPORT` | false | true | Statistics export: the snapshot as JSON or the daemon's CSV |
| `HAS_TIER2` | false | true | The stability report: six metrics over stream time, ten minutes of evidence or an honest shrug |
| `MAX_MOUNTPOINTS` | 1 | 16 | Saved connection profiles and the switcher |
| `SOURCETABLE_SELECTABLE` | false | true | Tap-to-use in the sourcetable browser |
| `HAS_LIVE_GGA` | false | true | The phone's live position in the GGA uplink |
