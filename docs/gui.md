# NTRIP-Analyser Windows GUI

The Windows GUI application (`ntrip-analyser-gui.exe`) provides a user-friendly desktop interface for analyzing NTRIP streams without requiring command-line knowledge.

## Features

- **Interactive connection management** - Easy configuration of NTRIP caster settings
- **Real-time stream monitoring** - Live display of received RTCM messages
- **Message analysis** - Comprehensive statistics on message types and intervals
- **Satellite tracking** - Per-constellation satellite visibility analysis
- **Detailed message viewer** - Decode and inspect individual RTCM messages
- **Configuration management** - Save and load connection profiles

## Architecture

The GUI is built using native Win32 API in pure C (C99 standard), with no external dependencies beyond the Windows SDK. It shares the same core library with the CLI application:

```
┌──────────────────────────────────────────────────────────────┐
│                        GUI Layer                             │
│  gui/gui_main.c   — WinMain, message loop, window creation  │
│  gui/gui_layout.c — Control creation, sizing, DPI-awareness │
│  gui/gui_events.c — Button handlers, menu commands           │
│  gui/gui_thread.c — Worker thread, UI ↔ core bridge          │
│  gui/gui_log.c    — Log-redirect (capture printf → listbox)  │
│  gui/gui_parsers.c— Message parsing for GUI display          │
│  gui/gui_detail.c — Detailed message viewer window           │
│  gui/resource.rc  — Menu bar, dialog templates, icon, version│
└────────────────────────┬─────────────────────────────────────┘
                         │  calls ↓         ↑ posts WM_APP+n
┌────────────────────────┴─────────────────────────────────────┐
│                   Shared Core Library                         │
│  src/ntrip_handler .c/.h  — NTRIP client, socket, analysis   │
│  src/rtcm3x_parser .c/.h  — RTCM decoding, CRC, geodetic     │
│  src/config        .c/.h  — JSON config load / generate      │
│  src/nmea_parser   .c/.h  — GGA sentence generation          │
│  lib/cJSON/cJSON   .c/.h  — JSON parser                      │
└──────────────────────────────────────────────────────────────┘
```

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
    src/ntrip_handler.c src/rtcm3x_parser.c src/config.c src/nmea_parser.c ^
    lib/cJSON/cJSON.c gui/resource.o ^
    -Isrc -Ilib/cJSON -Igui ^
    -lws2_32 -lcomctl32 -lcomdlg32 -lm -Wall
```

**Compile flags explained:**
- `-mwindows` — GUI subsystem (no console window)
- `-std=c99` — Use C99 standard
- `-D_USE_MATH_DEFINES` — Enable math constants (M_PI, etc.)
- `-lws2_32` — Windows Sockets 2 (networking)
- `-lcomctl32` — Common Controls (modern UI widgets)
- `-lcomdlg32` — Common Dialogs (file open/save dialogs)
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

**Supported message types:** 1005, 1006, 1007, 1008, 1012, 1013, 1019, 1033, 1045, 1074, 1077, 1084, 1087, 1094, 1097, 1117, 1124, 1127, 1137, 1230

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

| Shortcut | Action |
|----------|--------|
| `Ctrl+O` | Open/Load configuration file |
| `Ctrl+S` | Save current configuration |
| `Ctrl+Q` | Quit application |
| `F1` | Show About dialog |

### Menu Bar

**File Menu:**
- **Load Config** — Open saved settings
- **Save Config** — Save current settings
- **Exit** — Close application

**Edit Menu:**
- **Clear Log** — Empty the log window

**Help Menu:**
- **About** — Show application information

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
├── gui_thread.c       — Background worker thread for network I/O
├── gui_log.c          — Redirectable output (printf → log window)
├── gui_parsers.c      — Parse message types from raw RTCM data
├── gui_detail.c       — Detailed message viewer dialog
├── gui_state.h        — AppState structure, constants, function prototypes
├── resource.h         — Resource ID definitions
└── resource.rc        — Windows resources (menus, dialogs, version info)
```

### Key Functions

**gui_main.c:**
- `WinMain()` — Application entry point
- `MainWndProc()` — Main window message handler

**gui_layout.c:**
- `CreateControls()` — Create all UI controls
- `LayoutControls()` — Position controls based on window size

**gui_events.c:**
- Button and menu handlers
- Configuration load/save logic
- User input validation

**gui_thread.c:**
- `WorkerThreadFunc()` — Background network operations
- `PostStatusMessage()` — Send updates to UI thread

**gui_log.c:**
- `gui_log_redirect()` — Redirect stdout to log window
- `gui_log_write()` — Append text to log

**gui_detail.c:**
- `CreateDetailWindow()` — Open detailed message viewer
- Decode and display individual RTCM messages

## Future Enhancements

Potential improvements for future versions:

- **Graphical satellite view** — Sky plot showing satellite positions
- **Message timeline** — Visual chart of message transmission patterns
- **Export functionality** — Save analysis results to file
- **Multiple streams** — Monitor several mountpoints simultaneously
- **Real-time plotting** — Graph signal strength, satellite count over time
- **Dark theme** — Alternative color scheme for low-light environments

## See Also

- [Main README](../readme.md) — Project overview and CLI information
- [Compilation Guide](compile.md) — Detailed build instructions
- [RTCM Parser Documentation](../src/rtcm3x_parser.h) — Message decoder API
- [GUI Design Document](../gui/design.md) — Detailed architecture and design decisions
