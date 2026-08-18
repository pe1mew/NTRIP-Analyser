# NTRIP-Analyser Windows GUI

The Windows GUI application (`ntrip-analyser-gui.exe`) provides a user-friendly desktop interface for analyzing NTRIP streams without requiring command-line knowledge.

## Features

- **Interactive connection management** - Easy configuration of NTRIP caster settings
- **Real-time stream monitoring** - Live display of received RTCM messages
- **Station check** - The acceptance test as a bounded run: eight KPIs
  over about ninety seconds, ending in a verdict that stops moving, plus
  the VRS assertions when the mountpoint is a network service. The same
  engine as the CLI's `--check` and the Android app, so a station cannot
  pass on one and fail on another
- **Message analysis** - Per-type statistics, compared against what the
  mountpoint's sourcetable entry advertises
- **Stream health** - Caster handshake, CRC-24Q integrity, advertised-vs-
  observed message types, and reference-station position checks, all on
  one tab with severity colouring
- **Satellite tracking** - Per-constellation satellite visibility analysis
- **Detailed message viewer** - Decode and inspect individual RTCM messages
- **Configuration management** - Save and load connection profiles
- **Live polar sky plot** - Floating window showing every tracked satellite
  at its azimuth / elevation, with markers + trails or an Onocoy-style
  observed/expected heatmap; PNG snapshot export
- **Signal quality** - C/N0 bars per satellite plus a C/N0-versus-elevation
  scatter over the session, which is what actually reveals antenna and
  siting problems
- **Session history** - Six metrics plotted over time on a shared axis, so
  dropouts and reconnects are visible instead of averaged away
- **VRS monitor** - Rover-to-virtual-station distance, direction plot and
  rolling distance chart for network mountpoints
- **Multi-GNSS ephemerides** - GPS / GLONASS / Galileo / QZSS / BeiDou
  orbits propagated from either a second NTRIP stream (RTCM
  1019/1020/1042/1044/1045/1046) or a RINEX 3 NAV file
- **RTCM capture and replay** - Save the live stream to a `.rtcm3` file
  and feed it back through the same UI pipeline for offline analysis
- **Per-SV detail popup** - Left-click any satellite marker for PRN,
  az/el, best CNR, and per-band CNR table

## Architecture

The GUI is built using native Win32 API in pure C (C99 standard), with no
external dependencies beyond the Windows SDK and GDI+ (for PNG export).
It shares the same core library with the CLI application:

```
┌──────────────────────────────────────────────────────────────────────┐
│                             GUI Layer                                │
│  gui/gui_main.c          — WinMain, message loop, window creation    │
│  gui/gui_layout.c        — Control creation, sizing, DPI-awareness   │
│  gui/gui_events.c        — Handlers, health checks, classification   │
│  gui/gui_thread.c        — Worker threads (obs / eph / replay)       │
│  gui/gui_log.c           — Log redirect (printf → log panel)         │
│  gui/gui_parsers.c       — Sourcetable, advertised types, handshake  │
│  gui/gui_detail.c        — RTCM message detail viewer (double-click) │
│  gui/gui_sky_window.c    — Floating Sky Plot window                  │
│  gui/gui_signal_window.c — Floating Signal Quality window            │
│  gui/gui_hist_window.c   — Floating Session History window           │
│  gui/gui_vrs_window.c    — Floating VRS Monitor window               │
│  gui/gui_snapshot.c      — GDI+ PNG snapshot + shared save dialog    │
│  gui/gui_sv_detail.c     — Per-SV detail popup (left-click marker)   │
│  gui/resource.rc         — Menu bar, manifest, icon, version         │
└────────────────────────┬─────────────────────────────────────────────┘
                         │  calls ↓         ↑ posts WM_APP+n
┌────────────────────────┴─────────────────────────────────────────────┐
│                      Shared Core Library                             │
│  src/session/ntrip_session — the stream loop, shared by all four     │
│  src/net/ntrip_proto       — NTRIP request/response text             │
│  src/net/ntrip_handler     — sourcetable fetch, socket helpers       │
│  src/core/ns_stats         — the statistics snapshot schema          │
│  src/core/rtcm3x_parser    — RTCM decoding, CRC, geodetic, az/el     │
│  src/core/sv_ephemeris     — Per-(GNSS,PRN) eph cache, TOW validity  │
│  src/core/sv_orbit         — Kepler + GLONASS RK4 propagators        │
│  src/core/rinex_nav        — RINEX 3 multi-GNSS NAV loader           │
│  src/core/config           — JSON config load / generate             │
│  src/core/nmea_parser      — GGA sentence generation                 │
│  lib/cJSON/cJSON           — JSON parser                             │
└──────────────────────────────────────────────────────────────────────┘
```

### Thread topology

- **UI thread** — owns all `HWND`s, runs the message loop, paints the
  Sky Plot.
- **Obs worker** — `WorkerOpenStream`, drives an `NtripSession` and
  translates its events into `WM_APP_*` updates.  The transport itself
  (connect, framing, CRC) lives in `src/session/`, shared with the CLI
  and the monitoring service.
- **Eph worker** — `WorkerOpenEphStream`, reads 1019/1020/1042/1044/
  1045/1046 from the optional second NTRIP mountpoint, fills the
  shared ephemeris cache. Logs to the UI via `WM_APP_LOG_LINE`.
- **Replay worker** — `WorkerReplayRtcm`, reads a captured `.rtcm3`
  file from disk and feeds each frame through the same UI-update
  pipeline.

All three workers post Windows messages; no worker touches an `HWND`
directly. The ephemeris cache is serialised with a `CRITICAL_SECTION`,
as is the capture request the menu leaves for the worker — the session
writes the capture file, and the session belongs to the worker thread.

## Building the GUI

### Prerequisites

- **Windows OS** (Windows 7 or later)
- **Code::Blocks with MinGW compiler** (or any GCC-compatible compiler)
- **windres** (Windows Resource Compiler, included with MinGW)

### Build Methods

#### 1. Automated Build Scripts (Recommended)

**Batch script:**
```batch
build-gui.bat
```

**PowerShell script:**
```powershell
.\build-gui.ps1
```

Both scripts automatically:
1. Compile GUI resources (`resource.rc` → `resource.o`)
2. Build the executable with all required source files
3. Link against necessary Windows libraries
4. Report build status

#### 2. Visual Studio Code

Press `Ctrl+Shift+B` and select **"Build NTRIP-Analyser GUI (CodeBlocks MinGW)"** from the task list.

The VS Code task automatically handles resource compilation as a dependency.

#### 3. Manual Build

**Step 1: Compile resources**
```batch
windres gui/resource.rc -o gui/resource.o
```

**Step 2: Build executable**
```batch
gcc -g -mwindows -std=c99 -D_USE_MATH_DEFINES -o bin/ntrip-analyser-gui.exe ^
    gui/gui_main.c gui/gui_layout.c gui/gui_events.c gui/gui_thread.c ^
    gui/gui_log.c gui/gui_parsers.c gui/gui_detail.c ^
    gui/gui_sky_window.c gui/gui_snapshot.c gui/gui_sv_detail.c ^
    gui/gui_vrs_window.c gui/gui_signal_window.c gui/gui_hist_window.c ^
    src/session/ntrip_session.c src/net/ntrip_proto.c src/net/ntrip_handler.c ^
    src/core/ns_stats.c src/core/rtcm3x_parser.c src/core/config.c ^
    src/core/nmea_parser.c src/core/sv_ephemeris.c src/core/sv_orbit.c ^
    src/core/rinex_nav.c ^
    lib/cJSON/cJSON.c gui/resource.o ^
    -Isrc -Ilib/cJSON -Igui ^
    -lws2_32 -lcomctl32 -lcomdlg32 -lgdiplus -lm -Wall
```

`build-gui.bat` is the authoritative source list — if this command and that
script ever disagree, the script is right.

**Compile flags explained:**
- `-mwindows` — GUI subsystem (no console window)
- `-std=c99` — Use C99 standard
- `-D_USE_MATH_DEFINES` — Enable math constants (M_PI, etc.)
- `-lws2_32` — Windows Sockets 2 (networking)
- `-lcomctl32` — Common Controls (modern UI widgets)
- `-lcomdlg32` — Common Dialogs (file open/save dialogs)
- `-lgdiplus` — GDI+ flat C API (used for the Sky Plot PNG snapshot)
- `-lm` — Math library

## Using the GUI

### Getting Started

1. **Launch the application**
   ```
   bin\ntrip-analyser-gui.exe
   ```

2. **Configure connection settings:**

   | Field | Description | Example |
   |-------|-------------|---------|
   | **Caster** | NTRIP caster hostname | `caster.example.com` |
   | **Port** | TCP port number | `2101` |
   | **Mountpoint** | Stream mountpoint name | `RTCM3_MSM` |
   | **Username** | Your NTRIP username | `user@example.com` |
   | **Password** | Your NTRIP password | `••••••••` |
   | **Latitude** | Rover latitude (decimal degrees) | `52.1234` |
   | **Longitude** | Rover longitude (decimal degrees) | `4.5678` |

3. **Click "Open Stream"** to start receiving the NTRIP stream

### Main Window Features

#### 🌐 Mountpoint List
**Purpose:** Retrieve and browse available streams from the caster

**How to use:**
1. Enter caster hostname and port
2. Click **"Get Mountpoints"**
3. Browse the table: mountpoint, identifier, format, details, carrier,
   nav systems, network, country, latitude, longitude, and distance from
   your configured position
4. **Double-click** a row to copy its mountpoint into the Mountpoint field

The list also fills in automatically when you open a stream for a
mountpoint you typed by hand — the sourcetable is fetched anyway to
support the advertised-vs-observed comparison, so it is displayed rather
than discarded.

Right-click the list for **Select All** / **Copy**; `Ctrl+A` and `Ctrl+C`
work too. Click any column header to sort.

#### 📡 Open Stream
**Purpose:** Start receiving and decoding RTCM data

**How to use:**
1. Fill in the connection fields
2. Click **"Open Stream"**
3. The view switches to the **Msg Stats** tab and statistics populate live
4. Click **"Close Stream"** to stop

The status bar shows connection state and data rate, detected stream
format, total bytes received (with a CRC error count if any occur), and
the rover-to-reference distance.

### Output Tabs

The lower half of the window is a four-tab panel. All four update live.

#### 📝 Log
Connection progress, the caster's handshake headers, decoded message
output, and warnings. Auto-scrolls; scroll back at any time.

#### 📊 Msg Stats
One row per RTCM message type, updated as frames arrive.

| Column | Meaning |
|--------|---------|
| **Message Type** | RTCM message number (e.g. 1005, 1077, 1087) |
| **Count** | Frames received |
| **Min / Max / Avg dt** | Interval between epochs, in seconds |
| **Advertised** | Interval the sourcetable promises for this type, if any |
| **Status** | Verdict of the advertised-vs-observed comparison |
| **Description** | Human-readable message name |

**Status values**, colour-coded so problems stand out:

| Status | Colour | Meaning |
|--------|--------|---------|
| `ok` | default | Advertised and arriving at roughly the advertised rate |
| `missing` | red | Advertised but never received — the row exists with count 0 so its absence is visible |
| `slow N.Nx` / `fast N.Nx` | amber | Arriving, but not near the advertised interval |
| `extra` | blue | Received but not advertised — the sourcetable is out of date |
| `unknown` | default | No sourcetable entry, so no comparison is possible |

**Note on rates:** intervals are measured **per epoch, not per frame**. MSM
splits a single epoch across several frames when the observations do not
fit in one, so a base sending 1127 once per second in two parts would
otherwise look like it were sending twice per second. Where a type is
split, the status appends `N frames/ep`.

**Double-click** a row to open a live detail window for that message type.

#### 🛰️ Satellites
Unique satellites seen per constellation, with the RINEX IDs listed:
GPS (G), GLONASS (R), Galileo (E), BeiDou (C), QZSS (J), SBAS (S),
NavIC (I).

#### 🩺 Stream Health
Fourteen rows answering "is this mountpoint healthy", in connection order.
Rows are colour-coded: red for real faults, amber for advisories, blue for
informational.

**Caster handshake**

| Row | Shows |
|-----|-------|
| **NTRIP version** | 1.0 (ICY) or 2.0 (HTTP). A caster answering ICY despite our `Ntrip-Version: Ntrip/2.0` request is simply an NTRIP 1.0 caster — informational, not a fault |
| **Response** | Status code and reason, plus the raw status line |
| **Caster software** | The `Server:` response header, i.e. which caster implementation you are talking to |

**Frame integrity**

| Row | Shows |
|-----|-------|
| **Frames checked** | Complete RTCM 3.x frames that had a CRC-24Q test applied |
| **Frames OK** | Passed CRC and reached the decoders |
| **CRC-24Q errors** | Failures — the best single indicator of a flaky link between receiver and caster |
| **CRC error rate** | Failures as a share of frames checked |
| **Malformed frames** | Bad preamble or runt frame, rejected before the CRC test |
| **Framing re-syncs** | Implausible length field; framing re-acquired from the next byte |

**Message types**

| Row | Shows |
|-----|-------|
| **Advertised types** | Roll-up of the Msg Stats comparison: how many arrive as advertised, are missing, are off-rate, or are unadvertised |

**Reference station**

| Row | Shows |
|-----|-------|
| **Station type** | `fixed base` or `VRS / network`, and which signal decided it |
| **Broadcast ARP** | Position from RTCM 1005/1006 with altitude |
| **Sourcetable match** | Distance between the declared and broadcast positions. A large gap usually means the base was registered with the caster using wrong coordinates |
| **ARP stability** | Whether the reference point moved during the session, and the largest jump |

**Why classification matters:** on a VRS the reference point legitimately
follows the rover, so the sourcetable comparison reads `n/a` and movement
is reported as hand-overs. Applying fixed-base checks to a network
mountpoint would produce nothing but false alarms.

#### 🔍 Detailed Message Viewer
**Purpose:** Deep-dive into individual RTCM message structure

**How to use:**
1. **Double-click** any row in the **Msg Stats** tab
2. A window opens showing that message type decoded, refreshed live as
   new frames of that type arrive
3. Several detail windows can be open at once

**What's decoded:**
Depending on message type, you'll see:
- Header information (station ID, epoch time, flags)
- Satellite data (PRNs, ranges, phase rates)
- Signal data (pseudorange, carrier phase, CNR, Doppler)
- Antenna information (coordinates, descriptors)
- Ephemeris data (orbital parameters)

**Supported message types:** 1005, 1006, 1007, 1008, 1012, 1013, 1019,
1020, 1033, 1042, 1044, 1045, 1046, 1074, 1077, 1084, 1087, 1094, 1097,
1117, 1124, 1127, 1137, 1230

The detail window has a **Copy** button that copies the full decoded
text to the clipboard.

#### 🛰 Sky Plot
**Purpose:** Visualise every tracked satellite at its azimuth /
elevation as seen from the reference-station ARP.

**How to open:** Menu **View → Sky Plot...**, or it auto-opens when the
worker first posts a sky update if you opened it during a prior run.
The window remembers its position and size between sessions.

**Layout:**
- Centre = zenith, outer ring = horizon. Elevation rings at 0°, 15°,
  30°, 45°, 60°, 75°.
- Dotted N–S and E–W axes; N is at the top.
- Live legend strip with per-GNSS marker colours: G = green (GPS),
  R = red (GLONASS), E = blue (Galileo), J = magenta (QZSS),
  C = orange (BeiDou).
- Footer (right-aligned): ARP lat / lon / alt, mountpoint name, and a
  1 Hz UTC clock. PNG snapshots are therefore self-contained.

**Two render modes** (press **M** to toggle, or use the title-bar hint):

1. **Markers mode** — each SV is a coloured dot whose brightness scales
   with its best-signal CNR (≈ 20 dB-Hz → dim, ≈ 45 dB-Hz → full
   saturation). A 120-point ring buffer per SV draws a desaturated
   trail of past positions (≈ 1 h of motion at the typical update
   rate). **Left-click** a marker (or its PRN label, with a generous
   hit tolerance) to open a per-SV detail popup — see below.
2. **Heatmap mode** — the sky is divided into 150 sectors (9 elevation
   bands × variable azimuth bins, more bins near the horizon, fewer at
   the zenith). For each sector the worker tracks how many SVs were
   observed vs. how many were expected from the loaded ephemerides.
   The ratio is drawn on a red → yellow → green ramp; the polar hole
   (above the highest elevation band) is rendered light grey.

**Keyboard:**

| Key | Action |
|-----|--------|
| `M` | Toggle markers ↔ heatmap |
| `S` | Save Sky Plot snapshot as PNG (default filename `YYYYMMDDHHmmss_<TrackedSats|ARP-EPG>.png`) |

You can also use **File → Save Sky Plot as PNG...** from the main
window menu.

#### 🛰 Per-SV detail popup
**Purpose:** Inspect a single satellite without leaving the sky plot.

**How to open:** Left-click any marker in the Sky Plot.

**What's shown:**
- PRN (e.g. `G07`, `E12`, `R15`, `J04`, `C24`)
- Live azimuth / elevation
- Best-signal CNR
- Per-band CNR table — one row per RTCM signal label observed on this
  SV (L1C, L2W, L5Q, E1C, E5Q, R1C, R2P, B1I, B2I, J1C, ...)
- Last-refresh timestamp + reference-station mountpoint

The window has a **Copy** button that pushes the full block to the
clipboard for inclusion in tickets or notes.

The popup auto-refreshes once per second; close it any time without
disturbing the sky plot.

#### 📶 Signal Quality window
**Purpose:** Judge the antenna and siting quality of the reference station,
which is what C/N0 primarily indicates.

**How to open:** Menu **View → Signal Quality...**

**Two views, stacked:**

1. **C/N0 by satellite (this epoch)** — one bar per tracked satellite,
   coloured by constellation using the same hues as the Sky Plot so a
   satellite reads identically in both. Hover any bar for a tooltip giving
   the satellite, its C/N0 and its elevation.
2. **C/N0 vs elevation (whole session)** — the session's observations
   counted into cells of one degree by one decibel, each cell shaded by
   how often it was hit, with a per-constellation mean overlaid in 5°
   bins. **This is the diagnostic view**: a clean installation rises
   monotonically from horizon to zenith, while obstructions and
   multipath show as a dip at particular elevations — which no snapshot
   view can reveal. Bins with fewer than five samples are omitted rather
   than drawn, so the mean line never implies more confidence than the
   data supports.

   *Whole session* is literal: the counting grid is fixed in size, so it
   keeps every sample of a run of any length. Shading is logarithmic,
   because a cell hit ten thousand times and one hit once are different
   findings and used to draw identically.

**What the stream does to this picture.** C/N0 arrives quantised, and
how coarsely depends on the message: MSM4 and MSM5 carry six bits —
whole decibels — while MSM6 and MSM7 carry ten, a sixteenth of a
decibel, and the legacy observation messages a quarter. The cell is a
whole decibel high for that reason: it is the coarsest any stream
delivers, so every stream fills the rows it touches. Drawn any finer, an
MSM4 station would leave a blank row between every filled one and the
plot would show horizontal white lines that belong to the station, not
to the renderer.

All of those messages carry C/N0, so any of them fills this view: MSM4,
5, 6 and 7, and the legacy 1002/1004/1010/1012. Only MSM1, 2 and 3 carry
none, and a station sending nothing else leaves the view empty and says
so.

#### 📈 Session History window
**Purpose:** See metrics over time. The Msg Stats min/max/avg columns hide
the faults that matter — a 45-second dropout and a steady stream can
average alike.

**How to open:** Menu **View → Session History...**

**Six stacked strip charts on one shared time axis:**

| Panel | Reveals |
|-------|---------|
| **Throughput** (kB/s) | Dropouts, bursts, reconnects |
| **Message rate** (frame/s) | The same, per frame rather than per byte |
| **CRC-24Q errors** (per s) | Link degradation, visible as spikes |
| **Satellites tracked** | Constellation loss, or ephemerides arriving |
| **Mean C/N0** (dB-Hz) | Gradual signal degradation |
| **Reference drift** (m) | A fixed base wandering from where it started |

Sharing one time axis is the point: a reconnect appears as a simultaneous
trough in throughput *and* message rate, while a bad link shows CRC spikes
with throughput unchanged. Vertical gridlines mark the same instants in
every panel so a feature can be traced across metrics.

Sampling is once per second for four hours, after which the oldest samples
are overwritten and the header says so. Each pixel column shows the
**peak** of the samples it covers, not their mean, so a one-second CRC
spike stays visible however long the session runs.

The C/N0 panel uses a fixed 20–60 dB-Hz band; the others are zero-based,
because for those a trough to zero is exactly the fault being looked for.

#### 🛰 VRS Monitor window
**Purpose:** Analyse a network (VRS / MAC / nearest-base) mountpoint, where
the reference point follows the rover.

**How to open:** Menu **View → VRS Monitor...**

**Shows:** live rover-to-virtual-station distance, a polar plot giving
direction and distance, a rolling five-minute distance chart, and
accumulated ARP dots that reveal hand-overs between physical bases.

The **Tools → Toggle auto-send GGA** item and the in-plot N/E/S/W shift
buttons let you move the reported rover position and watch whether the
virtual station follows — the conclusive test of whether a mountpoint is
really a VRS.

#### 🛰 Ephemeris stream (optional second NTRIP connection)
**Purpose:** Without ephemerides the sky plot cannot compute azimuth
or elevation from a PRN. The GUI supports two ways to feed the cache;
this one is a parallel NTRIP connection.

**How to use:**

1. Fill in the **Ephemeris Stream (optional)** group on the main
   window. It mirrors the Connection Settings layout. Defaults are
   pre-populated for BKG (`products.igs-ip.net : 2101 / BCEP00BKG0`)
   when you generate a template config; the
   [Kadaster](https://www.nsgi.nl/) `BCEP00KAD0` mountpoint works the
   same way.
2. Fill in credentials **if that caster asks for them** — BKG's
   `BCEP00BKG0` does, Kadaster's `BCEP00KAD0` is served anonymously and
   works with the fields left empty.
3. Open the main stream; the GUI will open the eph stream in parallel.

**Caster registration:**
- BKG IGS-IP: https://register.rtcm-ntrip.org/cgi-bin/registration.cgi
- Kadaster NSGI: contact the operator for an account

**What you'll see:** `[EPH] type=1019 SV=G05` lines in the log as new
ephemerides arrive. Once enough SVs are cached, the Sky Plot starts
populating.

**Note:** Some casters (e.g. Onocoy) reject a second concurrent
connection on the same account with HTTP 403. Use a different caster
account for the eph stream, or use the RINEX file loader (below).

#### 📂 Load ephemerides from a RINEX 3 NAV file
**Purpose:** Offline workflow, or fallback when a parallel NTRIP eph
stream is not available.

**How to use:**

1. Menu **File → Load Ephemerides (RINEX)...**
2. Select a multi-GNSS RINEX 3 NAV file (G / E / R / J / C records).
3. The loader walks every record and populates the same cache the
   NTRIP eph worker writes to. The log reports how many SVs were
   loaded per constellation.

Once loaded, the Sky Plot uses these ephemerides immediately. Note
that RINEX records have a finite validity window (typically a few
hours) — reload a fresh file for long sessions.

#### 💽 RTCM capture and replay
**Purpose:** Save a live NTRIP session to disk and replay it later
without needing the caster.

**Capture:**
1. With a stream open, menu **File → Start RTCM Capture...**
2. Choose a filename (default: `YYYYMMDDHHmmss_<mountpoint>.rtcm3`).
3. Every CRC-valid frame's raw bytes are written to the file — by the
   session layer, the same code the CLI's `--capture` uses, so a file
   from either program is the same file.
4. **File → Stop RTCM Capture** closes it and reports the frame and byte
   count. Closing the stream closes an open capture too.

An existing filename is **refused, not overwritten**, as is a second
capture over a running one; a path that cannot be written says so in the
log rather than capturing nothing silently.

**Replay:**
1. Menu **File → Replay RTCM File...** and pick a `.rtcm3` file.
2. The replay worker reads the file and feeds every frame back through
   the same UI-update pipeline as the live obs worker: message stats,
   satellites, sky plot, detail windows all behave identically.
3. Replay runs as fast as disk + CPU allow; there is no real-time
   pacing.

#### 💾 Configuration Management
**Purpose:** Save and load connection profiles

**Save configuration:**
- Menu: **File → Save Config** (or `Ctrl+S`)
- Choose location and filename (`.json` format)
- All current settings are saved

**Load configuration:**
- Menu: **File → Load Config** (or `Ctrl+O`)
- Select a saved configuration file
- Settings are automatically filled in

**Configuration format:** JSON file with all connection parameters and rover coordinates.

#### 📝 Log Window
**Purpose:** Display real-time events and decoded messages

**Features:**
- **Auto-scroll:** Automatically shows latest messages
- **Scrollable:** Review previous output
- **Copyable:** Right-click to copy selected text
- **Clear:** Use Edit menu to clear log content

**What appears in log:**
- Connection status messages
- Decoded RTCM messages (when enabled)
- Analysis results
- Error messages and warnings
- Mountpoint sourcetable

#### ✅ Station Check (View > Station Check)

**Purpose:** Answer one question about the mountpoint you are connected
to — *is this station fit to serve RTK?* — and answer it in a way you
can put in a handover.

Open a stream first, then press **Run Check**. It watches for about
ninety seconds and reports eight KPIs:

| # | KPI | Fails when |
|---|---|---|
| 1 | Connected and producing | nothing is arriving |
| 2 | RTCM 3.x format | no CRC-valid RTCM 3 frames decode |
| 3 | Reference position (ARP) | no 1005/1006, or zero coordinates |
| 4 | Observations flowing | a constellation it streams arriving slower than 0.5 Hz |
| 5 | Satellites in view | below what its sourcetable advertises *(soft)* |
| 6 | Median C/N0 | below 40 dB-Hz *(soft)* |
| 7 | Frame integrity (CRC) | more than 1 error per 1000 frames |
| 8 | Advertised versus actual | advertised message types never arrive |

Every row shows **the limit it was judged against** — `min 40.0 dB-Hz`,
`min 99.900 %` — so a verdict can be argued with rather than merely
read. Check 5 shows the figure *this* station was held to, which is the
sum over the constellations it streams: a GPS+GLONASS base is asked for
fewer satellites than a five-system one. The structural checks, and the
VRS assertions, leave that column blank because they are not
comparisons against a number.

KPI 8 compares the sourcetable's promise against delivery. A type that
is advertised but missing is a failure — a rover configured from that
sourcetable will not get what it was told to expect. Sending types or
constellations that were *never* advertised is an observation instead: a
warning, because the data is right and only the metadata is wrong.

**The verdict is what has to hold**, not each row: it must stay unchanged
for sixty continuous seconds before it is reported, so a station that
flickers cannot pass by being healthy at the right moment.

A run ends in one of three ways, and the header says which:

| End | Header reads |
|---|---|
| The verdict settled | `STATION OK` / `CAUTION` / `FAILED` — *settled after 91 s, held 60 s* |
| You pressed Stop | *stopped after 20 s — no verdict* |
| The stream closed, or 300 s passed | *the stream closed after 18 s — no verdict* |

The 300-second ceiling matters for one case in particular: a mountpoint
the caster does not list in its sourcetable leaves KPI 8 unable to judge
— deliberately, since "could not check" is not a pass — and a run with an
unjudgeable KPI would otherwise never conclude.

**VRS assertions** are added automatically when the station is
classified as a network service (see Stream Health → Station type): the
caster accepts the GGA, corrections start within 10 s of it, the ARP is
near the rover, and the stream holds at the GGA cadence.

The fifth assertion, the **gate test**, is opt-in via the checkbox
because it ends the session it is testing: it stops the keep-alive GGA
and waits for the caster to drop the stream. A drop means a live network
service; continued streaming means a fixed base. That is a
classification, not a fault.

#### 📉 Stability (View > Stability)

**Purpose:** the other question. The Station Check asks whether a
station is fit **now**, and answers in ninety seconds. This asks whether
it has *been* fit — over hours — which no ninety-second window can
reach at any price.

There is nothing to start. It accumulates for as long as the stream is
open and reads `INSUFFICIENT EVIDENCE` until it has ten minutes to judge
on, which is the shape commissioning wants: connect, get on with the
work, and the verdict is there when you next look at it.

Six measurements, each graded on its own, each showing **the limit it
was judged against** beside its value, with the worst finding stated in
the header along with the evidence behind it:

| # | Measurement | Reads |
|---|---|---|
| 1 | Availability | reconnections per hour |
| 2 | Frame integrity | the **worst** CRC error rate the window saw |
| 3 | Signal level | how far mean C/N0 **fell** from the window's best |
| 4 | Satellites held | the fewest held at any moment |
| 5 | Ionosphere | the worst median ROTI |
| 6 | Delivery rate | share of samples with a type arriving off-rate |

Two of those are deliberately *changes* rather than levels, which is
what makes them worth an hour. A 7 dB drop to 41 dB-Hz is flagged though
41 is a perfectly good level, because something changed; and the worst
CRC rate is reported rather than the average, because an average hides a
bad ten minutes inside a good six hours.

**It never uses the check's words.** `STABLE`, `DEGRADED`, `UNSTABLE` —
never `STATION OK`. A station can be fit right now and have been
unstable all week; both statements are true and neither contradicts the
other, so they do not share a vocabulary.

**The window is stream time**, counted from the observation epochs
rather than from this computer's clock. Three consequences: a dropout
counts, because the epochs on either side of it say so; a clock
correction on this machine cannot distort a running window; and a
**replayed capture** is judged over the window the *capture* holds, so
six hours read from disk in a moment are judged over six hours. Over a
replay, availability reads `n/a` — a file holds no arrival times and
never drops, so a clean zero would be an invention.

**Restart window** begins a fresh window without touching the stream.
That is for the moment you change something: having re-seated the
antenna, you want the next hour judged on its own rather than averaged
with the hour that prompted the change.

Closing the window does not stop it. The evidence belongs to the
session, so an hour of it survives a window being closed and reopened.

#### Judging by your own thresholds

**File > Load Thresholds...** takes a JSON policy and applies it to both
the Station Check and this window from their next run. The file is
partial — it carries only what you disagree with — and a bad setting is
refused with the field named, leaving the previous thresholds in force
rather than half-applying a standard nobody wrote.

The choice is **remembered**: the path is kept under
`HKCU\Software\NTRIP-Analyser`, so an installer working to one standard
does not reload it every morning. If that file has since been deleted or
edited badly, the program says so once and starts with the built-in
values rather than silently judging by something else.

The same file works with the CLI's `--thresholds` and the monitoring
service's `thresholds` key, and all three compute the same fingerprint
for it. [thresholds.md](thresholds.md) lists every threshold, what it
means, and how well founded it is.

### Keyboard Shortcuts

**Main window:**

| Shortcut | Action |
|----------|--------|
| `Ctrl+O` | Load Configuration |
| `Ctrl+S` | Save Configuration |
| `Ctrl+M` | Get Mountpoints |
| `Ctrl+D` | Open Stream |
| `Esc`    | Close Stream |
| `Alt+F4` | Exit |

**Mountpoint list:**

| Shortcut | Action |
|----------|--------|
| `Ctrl+A` | Select all rows |
| `Ctrl+C` | Copy selected rows as tab-separated text |

**Floating chart windows:**

| Window | Shortcut | Action |
|--------|----------|--------|
| Sky Plot | `M` | Toggle markers ↔ heatmap |
| Sky Plot | `S` | Save as PNG |
| Signal Quality | `Ctrl+S` or `S` | Save as PNG |
| Session History | `Ctrl+S` or `S` | Save as PNG |

Snapshots are named `YYYYMMDDHHmmss_<view>.png`, so a folder of them sorts
by capture time.

### Menu Bar

**File Menu:**
- **Load Configuration...** (`Ctrl+O`) — a ready-to-edit example to
  test with ships as `bin/exampleConfig.json`, targeting the Kadaster
  open caster with its ephemeris stream; it needs no account, and its
  credential fields are empty because those streams are anonymous
- **Save Configuration...** (`Ctrl+S`)
- **Generate Template Config** — write a default `template_config.json`
- **Load Ephemerides (RINEX)...** — populate the eph cache from a file
- **Save Sky Plot as PNG...** — snapshot the floating Sky Plot
- **Start RTCM Capture...** / **Stop RTCM Capture** — record raw frames
- **Replay RTCM File...** — feed a captured file back through the UI
- **Exit** (`Alt+F4`)

**Connection Menu:**
- **Get Mountpoints** (`Ctrl+M`)
- **Open Stream** (`Ctrl+D`) — also opens the eph stream if configured
- **Close Stream** (`Esc`)

**View Menu:**
- **Station Check...** — the acceptance test over the open stream
- **Stability...** — tier 2: has it *been* fit, over hours of stream

Under **File**: **Load Thresholds...** — judge by a policy of your own
- **Sky Plot...** — floating polar sky-visibility window
- **VRS Monitor...** — network-mountpoint analysis
- **Signal Quality...** — C/N0 bars and C/N0-vs-elevation scatter
- **Session History...** — six metrics plotted over time

**Tools Menu:**
- **Toggle auto-send GGA** — stop or resume the periodic GGA uplink, used
  to test how a VRS mountpoint reacts

**Help Menu:**
- **About NTRIP-Analyser**
- **View on GitHub**

## Troubleshooting

**Start with the Stream Health tab.** Most of the questions below are
answered directly there: whether the caster accepted the connection and
with which protocol version, whether frames are arriving intact, whether
the mountpoint is sending what it advertises, and whether the reference
station is where it claims to be.

| Symptom | Where to look |
|---------|---------------|
| Connection rejected | **Response** row gives the actual status code and reason; the Log holds the full response headers |
| Data arrives but rovers do not fix | **Advertised types** — a missing or off-rate correction type is the usual cause |
| Position looks wrong or jumps | **Sourcetable match** and **ARP stability** |
| Intermittent corruption | **CRC-24Q errors** and its rate; a non-zero rate points at the link between receiver and caster |
| Poor accuracy despite a healthy stream | **Signal Quality** window — check whether C/N0 rises with elevation |
| Gaps you cannot pin down | **Session History** — dropouts show as simultaneous troughs |

### Connection Issues

**Problem:** "Connection refused" or "Cannot connect to caster"
- **Check:** Caster hostname is correct (no `http://` prefix)
- **Check:** Port number is correct (usually 2101)
- **Check:** Network/firewall allows outbound TCP connections
- **Try:** Ping the caster hostname to verify it's reachable

**Problem:** "Authentication failed" or "401 Unauthorized"
- **Check:** Username and password are correct
- **Check:** Account has access to the requested mountpoint
- **Try:** Test credentials with different mountpoint

**Problem:** "Mountpoint not found" or "404 Not Found"
- **Check:** Mountpoint name is spelled correctly (case-sensitive)
- **Try:** Get mountpoint list to verify available streams

### Application Issues

**Problem:** GUI doesn't start or crashes
- **Check:** All required DLLs are present (comctl32.dll, ws2_32.dll)
- **Check:** Windows version is 7 or later
- **Try:** Run from command prompt to see error messages

**Problem:** Messages not appearing in log
- **Check:** Stream is actually connected (look for connection messages)
- **Check:** Mountpoint is sending data (some wait for GGA)
- **Try:** Wait 10-30 seconds for first messages to arrive

**Problem:** Satellite count shows zero
- **Check:** Stream contains MSM messages (1074-1137 range)
- **Check:** Enough time has passed (run for at least 30-60 seconds)
- **Try:** Message analysis to see which message types are present

### Performance Notes

- The GUI runs network operations on a background worker thread, keeping the interface responsive
- Message decoding happens asynchronously to avoid blocking the UI
- Large amounts of output may slow down the log window display
- For very high-rate streams, consider using CLI for better performance

## Technical Details

### Threading Model

- **Main thread (UI):** Handles all window messages and user interactions
- **Worker thread:** Manages TCP socket connection and receives RTCM data
- **Communication:** Worker posts custom Windows messages to main thread
- **Synchronization:** Critical sections protect shared state

### Memory Management

- Dynamic string buffers auto-expand as needed for log output
- RTCM messages copied to heap before posting between threads
- Configuration loaded into temporary buffers, validated, then applied
- Proper cleanup on disconnect and application exit

### Error Handling

- All network operations check return values
- Socket errors reported with descriptive Windows error codes
- RTCM parsing errors logged but don't crash application
- Invalid user input validated before use

## Source Code Structure

```
gui/
├── gui_main.c          — Entry point (WinMain), window class registration
├── gui_layout.c        — UI layout, control positioning, DPI scaling
├── gui_events.c        — Event handlers, Stream Health checks, station
│                         classification, session-history sampling
├── gui_thread.c        — Worker threads (obs / eph / replay)
├── gui_log.c           — Redirectable output (printf → log window)
├── gui_parsers.c       — Sourcetable rows, advertised message types,
│                         NTRIP handshake parsing
├── gui_detail.c        — RTCM message detail viewer (double-click)
├── gui_sky_window.c    — Floating Sky Plot (rose, markers, heatmap, footer)
├── gui_signal_window.c — Floating Signal Quality (C/N0 bars + scatter)
├── gui_hist_window.c   — Floating Session History (six strip charts)
├── gui_vrs_window.c    — Floating VRS Monitor (distance, polar, chart)
├── gui_check_window.c  — Floating Station Check (KPI rows, verdict, VRS)
├── gui_report_window.c — Floating Stability (tier-2 rows, rolling verdict)
├── gui_snapshot.c      — GDI+ PNG snapshot + shared save-with-prompt flow
├── gui_sv_detail.c     — Per-SV detail popup (left-click on marker)
├── gui_state.h         — AppState structure, constants, function prototypes
├── resource.h          — Resource ID definitions
└── resource.rc         — Windows resources (menus, dialogs, version info)

src/  (shared with CLI, additions for the Sky Plot)
├── sv_ephemeris.{c,h} — Per-(GNSS,PRN) eph cache, TOW-only validity
├── sv_orbit.{c,h}     — Keplerian + GLONASS RK4 propagators, sv_to_ecef
└── rinex_nav.{c,h}    — RINEX 3 multi-GNSS NAV loader (also CLI -R,
                          and the Android app's file import)
```

### Key Functions

**gui_main.c:**
- `WinMain()` — Application entry point
- `MainWndProc()` — Main window message handler

**gui_layout.c:**
- `CreateControls()` — Create all UI controls (incl. Eph Stream group)
- `LayoutControls()` — Position controls based on window size

**gui_events.c:**
- Button and menu handlers (incl. Sky Plot, Load Eph, Save PNG,
  Start/Stop Capture, Replay)
- Configuration load/save logic
- User input validation

**gui_thread.c:**
- `WorkerOpenStream()` — Obs worker: reads MSM1–7, the legacy
  observation messages and 1005/1006,
  posts `WM_APP_SKY_UPDATE`, `WM_APP_STAT_UPDATE`, `WM_APP_SAT_UPDATE`
- `WorkerOpenEphStream()` — Eph worker: reads 1019/1020/1042/1044/
  1045/1046, fills the shared eph cache, logs via `WM_APP_LOG_LINE`
- `WorkerReplayRtcm()` — Reads a `.rtcm3` file and replays every
  frame through the same UI pipeline as `WorkerOpenStream`

**gui_log.c:**
- `gui_log_redirect()` — Redirect stdout to log window
- `gui_log_write()` — Append text to log

**gui_detail.c:**
- `CreateDetailWindow()` — Open RTCM message detail viewer
- Copy-to-clipboard button

**gui_sky_window.c:**
- `CreateSkyWindow()` — Create the floating polar plot
- `DrawSkyMarkers()` / `DrawSkyHeatmap()` — Two render modes
- `sky_compute_geometry()` — ARP → ENU → az/el geometry helper
- `sky_save_png()` — Snapshot via `gui_snapshot.c`

**gui_sv_detail.c:**
- `CreateSvDetailWindow()` — Per-SV popup with PRN, az/el, CNR table
- 1 Hz refresh timer + Copy button

**gui_signal_window.c:**
- `CreateSignalWindow()` — C/N0 bars + C/N0-vs-elevation scatter
- Hover hit-testing for the per-bar tooltip

**gui_hist_window.c:**
- `CreateHistWindow()` — six stacked strip charts on a shared time axis
- Peak-per-column compression so short spikes survive a long session

**gui_vrs_window.c:**
- `CreateVrsWindow()` — rover-to-virtual-station distance, polar plot,
  rolling distance chart, ARP hand-over dots

**gui_snapshot.c:**
- `save_window_as_png()` — GDI+ flat C API wrapper that grabs a window
  bitmap and saves it as a PNG file
- `SaveWindowPngWithPrompt()` — shared save flow (timestamped default
  name, overwrite prompt, log line) used by every chart window

## Future Enhancements

The planned and considered work lives in the project-wide backlog:
**[design/todo.md](https://github.com/pe1mew/NTRIP-Analyser/blob/main/design/todo.md)**.
It records what has shipped and why, so the same ideas are not
re-proposed.

Still open at the time of writing: structured export of analysis results
(CSV / JSON — only PNG snapshots and `File > Export Statistics` exist
today), a saved station-check report for handover, monitoring several
mountpoints at once, a dark theme, and optional realtime / Nx pacing for
the replay worker, which currently runs as fast as disk and CPU allow.

## See Also

- [Main README](https://github.com/pe1mew/NTRIP-Analyser/blob/main/readme.md) — Project overview and CLI information
- [Compilation Guide](compile.md) — Detailed build instructions
- [RTCM Parser Documentation](https://github.com/pe1mew/NTRIP-Analyser/blob/main/src/core/rtcm3x_parser.h) — Message decoder API
- [GUI Design Document](https://github.com/pe1mew/NTRIP-Analyser/blob/main/design/gui-design.md) — Detailed architecture and design decisions
