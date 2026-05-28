# NTRIP-Analyser Windows GUI

The Windows GUI application (`ntrip-analyser-gui.exe`) provides a user-friendly desktop interface for analyzing NTRIP streams without requiring command-line knowledge.

## Features

- **Interactive connection management** - Easy configuration of NTRIP caster settings
- **Real-time stream monitoring** - Live display of received RTCM messages
- **Message analysis** - Comprehensive statistics on message types and intervals
- **Satellite tracking** - Per-constellation satellite visibility analysis
- **Detailed message viewer** - Decode and inspect individual RTCM messages
- **Configuration management** - Save and load connection profiles
- **Live polar sky plot** - Floating window showing every tracked satellite
  at its azimuth / elevation, with markers + trails or an Onocoy-style
  observed/expected heatmap; PNG snapshot export
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
│                              GUI Layer                                │
│  gui/gui_main.c       — WinMain, message loop, window creation       │
│  gui/gui_layout.c     — Control creation, sizing, DPI-awareness      │
│  gui/gui_events.c     — Button handlers, menu commands               │
│  gui/gui_thread.c     — Worker threads (obs / eph / replay)          │
│  gui/gui_log.c        — Log redirect (printf → listbox)              │
│  gui/gui_parsers.c    — Message parsing for GUI display              │
│  gui/gui_detail.c     — RTCM message detail viewer (double-click)    │
│  gui/gui_sky_window.c — Floating Sky Plot window                     │
│  gui/gui_snapshot.c   — GDI+ PNG snapshot helper                     │
│  gui/gui_sv_detail.c  — Per-SV detail popup (left-click on marker)   │
│  gui/resource.rc      — Menu bar, manifest, icon, version            │
└────────────────────────┬─────────────────────────────────────────────┘
                         │  calls ↓         ↑ posts WM_APP+n
┌────────────────────────┴─────────────────────────────────────────────┐
│                       Shared Core Library                             │
│  src/ntrip_handler  .c/.h — NTRIP client, socket, analysis           │
│  src/rtcm3x_parser  .c/.h — RTCM decoding, CRC, geodetic, az/el      │
│  src/sv_ephemeris   .c/.h — Per-(GNSS,PRN) eph cache, TOW validity   │
│  src/sv_orbit       .c/.h — Kepler + GLONASS RK4 propagators         │
│  src/rinex_nav      .c/.h — RINEX 3 multi-GNSS NAV loader (GUI only) │
│  src/config         .c/.h — JSON config load / generate              │
│  src/nmea_parser    .c/.h — GGA sentence generation                  │
│  lib/cJSON/cJSON    .c/.h — JSON parser                              │
└──────────────────────────────────────────────────────────────────────┘
```

### Thread topology

- **UI thread** — owns all `HWND`s, runs the message loop, paints the
  Sky Plot.
- **Obs worker** — `WorkerOpenStream`, reads MSM4/MSM7 + 1005/1006 from
  the primary NTRIP mountpoint, posts `WM_APP_*` updates.
- **Eph worker** — `WorkerOpenEphStream`, reads 1019/1020/1042/1044/
  1045/1046 from the optional second NTRIP mountpoint, fills the
  shared ephemeris cache. Logs to the UI via `WM_APP_LOG_LINE`.
- **Replay worker** — `WorkerReplayRtcm`, reads a captured `.rtcm3`
  file from disk and feeds each frame through the same UI-update
  pipeline.

All three workers post Windows messages; no worker touches an `HWND`
directly. The ephemeris cache and RTCM-capture file handle are
serialised with `CRITICAL_SECTION`s.

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
    src/ntrip_handler.c src/rtcm3x_parser.c src/config.c src/nmea_parser.c ^
    src/sv_ephemeris.c src/sv_orbit.c src/rinex_nav.c ^
    lib/cJSON/cJSON.c gui/resource.o ^
    -Isrc -Ilib/cJSON -Igui ^
    -lws2_32 -lcomctl32 -lcomdlg32 -lgdiplus -lm -Wall
```

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

3. **Click "Connect"** to start receiving the NTRIP stream

### Main Window Features

#### 🌐 Mountpoint List
**Purpose:** Retrieve and browse available streams from the caster

**How to use:**
1. Enter caster hostname and port
2. Click **"Get Mountpoints"** button
3. View the sourcetable in the log window
4. Find desired mountpoint and copy its name to the Mountpoint field

**What you'll see:** Complete sourcetable listing all available streams, their formats, locations, and capabilities.

#### 📡 Connect to Stream
**Purpose:** Start receiving and decoding RTCM data

**How to use:**
1. Fill in all connection fields
2. Click **"Connect"**
3. Watch real-time messages appear in the log window
4. Click **"Disconnect"** to stop

**Status indicators:**
- Connection progress messages in log
- Real-time message decoding output
- Error messages if connection fails

#### 📊 Message Statistics
**Purpose:** Analyze message types and transmission patterns

**How to use:**
1. Connect to a stream
2. Let it run for desired duration (default: 60 seconds)
3. Click **"Analyze Messages"**
4. Review statistics in the log window

**Statistics displayed:**
- **Message Type:** RTCM message number (e.g., 1005, 1077, 1087)
- **Count:** Total number of messages received
- **Min Interval:** Minimum time between messages (seconds)
- **Avg Interval:** Average transmission interval (seconds)
- **Max Interval:** Maximum time between messages (seconds)

Results are automatically sorted by message frequency (most common first).

#### 🛰️ Satellite Analysis
**Purpose:** Count and categorize visible satellites

**How to use:**
1. Connect to a stream
2. Let it run for desired duration (default: 60 seconds)
3. Click **"Count Satellites"**
4. View satellite breakdown by constellation

**Information shown:**
- **GPS** satellites (G01-G32)
- **GLONASS** satellites (R01-R24)
- **Galileo** satellites (E01-E36)
- **BeiDou** satellites (C01-C37)
- **QZSS** satellites (J01-J07)
- **SBAS** satellites (S20-S58)
- **Total** unique satellites across all constellations

#### 🔍 Detailed Message Viewer
**Purpose:** Deep-dive into individual RTCM message structure

**How to use:**
1. **Double-click** any message in the log window
2. A new window opens with complete message details
3. Review all decoded fields and values
4. Close window when done

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
2. Make sure the credentials are filled in (most public broadcast-eph
   casters require a free registration).
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
3. Every parsed frame's raw bytes are written to the file under a
   `CRITICAL_SECTION`-guarded handle.
4. **File → Stop RTCM Capture** closes the file. The file is also
   flushed automatically when the stream is closed.

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

**Sky Plot window:**

| Shortcut | Action |
|----------|--------|
| `M` | Toggle markers ↔ heatmap |
| `S` | Save Sky Plot as PNG |

### Menu Bar

**File Menu:**
- **Load Configuration...** (`Ctrl+O`)
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
- **Sky Plot...** — open the floating polar sky-visibility window

**Help Menu:**
- **About NTRIP-Analyser**
- **View on GitHub**

## Troubleshooting

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
├── gui_main.c         — Entry point (WinMain), window class registration
├── gui_layout.c       — UI layout, control positioning, DPI scaling
├── gui_events.c       — Event handlers (button clicks, menu commands)
├── gui_thread.c       — Worker threads (obs / eph / replay)
├── gui_log.c          — Redirectable output (printf → log window)
├── gui_parsers.c      — Parse message types from raw RTCM data
├── gui_detail.c       — RTCM message detail viewer (double-click)
├── gui_sky_window.c   — Floating Sky Plot (rose, markers, heatmap, footer)
├── gui_snapshot.c     — GDI+ PNG snapshot helper
├── gui_sv_detail.c    — Per-SV detail popup (left-click on marker)
├── gui_state.h        — AppState structure, constants, function prototypes
├── resource.h         — Resource ID definitions
└── resource.rc        — Windows resources (menus, dialogs, version info)

src/  (shared with CLI, additions for the Sky Plot)
├── sv_ephemeris.{c,h} — Per-(GNSS,PRN) eph cache, TOW-only validity
├── sv_orbit.{c,h}     — Keplerian + GLONASS RK4 propagators, sv_to_ecef
└── rinex_nav.{c,h}    — RINEX 3 multi-GNSS NAV loader (GUI-only)
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
- `WorkerOpenStream()` — Obs worker: reads MSM4/MSM7 + 1005/1006,
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

**gui_snapshot.c:**
- `SnapshotHwndToPng()` — GDI+ flat C API wrapper that grabs a window
  bitmap and saves it as a PNG file

## Future Enhancements

Potential improvements for future versions:

- **Message timeline** — Visual chart of message transmission patterns
- **Export functionality** — Save analysis results / message stats to file
- **Multiple streams** — Monitor several mountpoints simultaneously
- **Real-time plotting** — Graph signal strength, satellite count over time
- **Dark theme** — Alternative color scheme for low-light environments
- **Replay pacing** — Optional realtime / Nx playback for the RTCM replay
  worker (currently as-fast-as-possible)

## See Also

- [Main README](../readme.md) — Project overview and CLI information
- [Compilation Guide](compile.md) — Detailed build instructions
- [RTCM Parser Documentation](../src/rtcm3x_parser.h) — Message decoder API
- [GUI Design Document](../gui/design.md) — Detailed architecture and design decisions
