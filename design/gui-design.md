# NTRIP-Analyser Windows GUI — Design Document

## 1. Overview

This document describes the design for a native Windows GUI version of NTRIP-Analyser,
built entirely in **C (C99)** using the **Win32 API**. The GUI wraps the existing CLI
core logic — networking (`ntrip_handler`), RTCM parsing (`rtcm3x_parser`), configuration
(`config`), and NMEA generation (`nmea_parser`) — without modifying those modules.

### 1.1 Goals

- Provide a user-friendly Windows desktop application for NTRIP stream analysis.
- Reuse 100 % of the existing core C library code (no rewrites).
- Zero external GUI dependencies — only Win32 API + standard C library.
- Run all blocking network I/O on a background thread so the UI stays responsive.
- Match the feature set of the CLI: connect, fetch mountpoints, decode streams,
  analyze message types, and count satellites.

### 1.2 Non-Goals

- Cross-platform GUI (Linux/macOS) — this design is Windows-only.
- Replacing the CLI — both targets will coexist and share the same core source.
- Advanced charting or map visualization (future enhancement).

---

## 2. Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                        GUI Layer (new)                       │
│  gui/gui_main.c   — WinMain, message loop, window creation  │
│  gui/gui_layout.c — Control creation, sizing, DPI-awareness │
│  gui/gui_events.c — Button handlers, menu commands           │
│  gui/gui_thread.c — Worker thread, UI ↔ core bridge          │
│  gui/gui_log.c    — Log-redirect (capture printf → listbox)  │
│  gui/resource.h   — Resource IDs (controls, menus, icons)    │
│  gui/resource.rc  — Menu bar, dialog templates, icon, version│
└────────────────────────┬─────────────────────────────────────┘
                         │  calls ↓         ↑ posts WM_APP+n
┌────────────────────────┴─────────────────────────────────────┐
│                   Existing Core (unchanged)                   │
│  src/ntrip_handler .c/.h  — NTRIP client, socket, analysis   │
│  src/rtcm3x_parser .c/.h  — RTCM decoding, CRC, geodetic    │
│  src/config        .c/.h  — JSON config load / generate      │
│  src/nmea_parser   .c/.h  — GGA sentence generation          │
│  src/cli_help      .c/.h  — (not used by GUI)                │
│  lib/cJSON/cJSON   .c/.h  — JSON parser                      │
└──────────────────────────────────────────────────────────────┘
```

### 2.1 Key Design Principles

| Principle | How |
|---|---|
| **Separation** | All new GUI code lives under `gui/`. Core `src/` files are never modified. |
| **Threading** | Every network operation runs on a dedicated Win32 worker thread (`_beginthreadex`). The thread communicates results back to the UI thread via `PostMessage` with custom `WM_APP+n` messages. |
| **Output redirection** | Core functions use `printf`/`fprintf`. The GUI intercepts these by redirecting `stdout`/`stderr` through a pipe (`_pipe` + `_dup2`) and pumping pipe output into a GUI log control on a timer. |
| **No global state in GUI** | All GUI state lives in a single `AppState` struct passed via `SetWindowLongPtr(GWLP_USERDATA)`. |

---

## 3. Window Layout

```
┌─ NTRIP-Analyser ─────────────────────────────────────────── [_][□][X] ─┐
│ File  Connection  Analysis  Help                                        │
├─── Connection Settings ─────────────────────────────────────────────────┤
│  Caster:  [_________________]  Port: [____]  Mountpoint: [___________]  │
│  User:    [_________________]  Password: [***********]                   │
│  Lat:     [_________]          Lon: [_________]                         │
│                                                                         │
│  [Load Config]  [Save Config]  [Generate Template]                      │
├─── Actions ─────────────────────────────────────────────────────────────┤
│  [Get Mountpoints]  [Decode Stream]  [Analyze Types]  [Analyze Sats]    │
│  Analysis Time (s): [___60__]   Filter types: [_______________]         │
│                                                [Stop]                   │
├─── Mountpoint Table ────────────────────────────────────────────────────┤
│ ┌─ ListView (Report mode) ────────────────────────────────────────────┐ │
│ │ Mountpoint │ City │ Country │ Lat │ Lon │ Format │ Nav-Sys │ ...   │ │
│ │ ABC01      │ ...  │ ...     │ ... │ ... │ ...    │ ...     │       │ │
│ └─────────────────────────────────────────────────────────────────────┘ │
├─── Stream Output / Analysis Results ────────────────────────────────────┤
│ ┌─ Tab Control ───────────────────────────────────────────────────────┐ │
│ │ [Log]  [Message Stats]  [Satellites]                                │ │
│ │                                                                     │ │
│ │  (Log tab) — scrolling text log of decoded messages / debug output  │ │
│ │  (Message Stats tab) — ListView table identical to CLI -t output    │ │
│ │  (Satellites tab) — ListView table identical to CLI -s output       │ │
│ └─────────────────────────────────────────────────────────────────────┘ │
├─── Status Bar ──────────────────────────────────────────────────────────┤
│  Connected to rtk2go.com:2101 / NEAR01  │  Elapsed: 42 s  │  Msgs: 371│
└─────────────────────────────────────────────────────────────────────────┘
```

### 3.1 Control Inventory

| Control | Win32 class | Purpose |
|---|---|---|
| Caster, Port, Mountpoint, User, Password, Lat, Lon, Analysis Time, Filter | `EDIT` | Input fields for NTRIP config |
| Load Config, Save Config, Generate, Get Mountpoints, Decode, Analyze Types, Analyze Sats, Stop | `BUTTON` | Action triggers |
| Mountpoint Table | `SysListView32` (Report) | Display parsed sourcetable |
| Tab strip | `SysTabControl32` | Switch between Log / Stats / Sats |
| Log output | `EDIT` (multiline, read-only, `ES_AUTOVSCROLL`) | Captured stdout/stderr |
| Message Stats table | `SysListView32` (Report) | Message type / count / min / max / avg |
| Satellites table | `SysListView32` (Report) | GNSS / count / satellite IDs |
| Status bar | `msctls_statusbar32` | Connection state, elapsed time, message counter |
| Menu bar | `HMENU` via resource | File, Connection, Analysis, Help |

---

## 4. File Structure

```
gui/
├── design.md           ← This document
├── resource.h          ← #define IDs for all controls, menus, dialogs
├── resource.rc         ← Menu bar, accelerators, version info, icon
├── gui_main.c          ← WinMain, RegisterClass, CreateWindow, message loop
├── gui_layout.c        ← CreateControls(), ResizeControls(WM_SIZE), DPI helpers
├── gui_events.c        ← WM_COMMAND / WM_NOTIFY dispatch, button handlers
├── gui_thread.c        ← Worker thread entry points, PostMessage bridge
├── gui_log.c           ← stdout/stderr pipe redirection, timer-based UI pump
├── gui_state.h         ← AppState struct definition, shared constants
└── gui_parsers.c       ← Sourcetable string → ListView rows, stats → ListView
```

**This section describes the original design.** The implementation has
since grown several modules that this document does not cover — the
floating Sky Plot, Signal Quality, Session History, VRS Monitor and
per-SV detail windows, the GDI+ snapshot helper, and the RTCM detail
viewer — plus two that it now does: `gui_check_window.{c,h}` (§13) and
`gui_report_window.{c,h}` (§14). For the current file inventory see
[`docs/gui.md`](../docs/gui.md); `build-gui.bat` is authoritative. The
design rationale below is retained as a record of the decisions that
shaped the codebase, not as a description of its present shape.

### 4.1 File Responsibilities

| File | Responsibility |
|---|---|
| **gui_main.c** | `WinMain()`, `InitCommonControlsEx`, WSAStartup, registers window class, creates main window, runs `GetMessage`/`DispatchMessage` loop. Calls `CreateControls()`. |
| **gui_layout.c** | Creates all child controls (edits, buttons, list views, tab, status bar). Handles `WM_SIZE` to reflow layout. Provides DPI-awareness helpers (`GetDpiForWindow` on Win10+). |
| **gui_events.c** | Central `WndProc` lives here. Dispatches `WM_COMMAND` (button clicks, menu items), `WM_NOTIFY` (list view column clicks, tab selection), and custom `WM_APP+n` messages from the worker thread. |
| **gui_thread.c** | Contains `DWORD WINAPI WorkerThread(LPVOID)` entry points for each operation. Each function: (1) copies config from UI controls into an `NTRIP_Config`, (2) calls the existing core function, (3) posts results back via `PostMessage`. Provides a `volatile BOOL g_stop_requested` flag for cancellation. |
| **gui_log.c** | Before starting a worker, redirects `stdout` and `stderr` to a Win32 anonymous pipe via `_pipe()`/`_dup2()`. A `SetTimer`-based poller reads from the pipe's read-end and appends text to the log `EDIT` control. Restores original file descriptors when the worker finishes. |
| **gui_state.h** | Defines `AppState` (HWNDs of all controls, worker thread handle, running flag, config snapshot, analysis results buffers). Defined as a struct allocated once and stored via `SetWindowLongPtr`. |
| **gui_parsers.c** | `ParseMountTable(const char *raw, HWND listview)` — splits the NTRIP sourcetable string by `;` fields and populates ListView rows. `PopulateMsgStats(MsgStats *stats, int max, HWND listview)` — fills the stats ListView. `PopulateSatSummary(SatStatsSummary *summary, HWND listview)` — fills the satellites ListView. |
| **resource.h** | `#define IDC_EDIT_CASTER 1001`, `IDC_BTN_CONNECT 1020`, `IDM_FILE_EXIT 9001`, etc. |
| **resource.rc** | Menu tree, accelerator table, `VS_VERSION_INFO`, application icon reference. |

---

## 5. Threading Model

### 5.1 Problem

All existing core functions (`receive_mount_table`, `start_ntrip_stream_with_filter`,
`analyze_message_types`, `analyze_satellites_stream`) are **blocking**. They open a
socket, loop on `recv()`, and return only when complete. Calling them on the UI thread
would freeze the window.

### 5.2 Solution

```
 UI Thread                              Worker Thread
 ─────────                              ─────────────
 [User clicks "Get Mountpoints"]
       │
       ├─ Disable buttons
       ├─ Snapshot NTRIP_Config from edit controls
       ├─ Set g_stop_requested = FALSE
       ├─ _beginthreadex(WorkerGetMountpoints, &config_copy)
       │                                    │
       │                                    ├─ Call receive_mount_table()
       │  ← WM_APP_MOUNT_RESULT ──────────┤  (with char* mount_table)
       │                                    └─ Thread exits
       ├─ ParseMountTable() → populate ListView
       ├─ Re-enable buttons
       ▼
```

### 5.3 Custom Window Messages

| Message | wParam | lParam | Meaning |
|---|---|---|---|
| `WM_APP_MOUNT_RESULT` (WM_APP+1) | 0=success, 1=error | `char*` heap pointer (or NULL) | Mount table received |
| `WM_APP_STREAM_DONE` (WM_APP+2) | 0=normal, 1=error | 0 | Decode/analysis stream ended |
| `WM_APP_STATS_READY` (WM_APP+3) | 0 | `MsgStats*` heap pointer | Message type stats available |
| `WM_APP_SATS_READY` (WM_APP+4) | 0 | `SatStatsSummary*` heap pointer | Satellite stats available |
| `WM_APP_LOG_LINE` (WM_APP+5) | 0 | `char*` heap pointer | Single log line for log panel |
| `WM_APP_STATUS_UPDATE` (WM_APP+6) | elapsed_sec | msg_count | Status bar update tick |

### 5.4 Cancellation

The **Stop** button sets `volatile BOOL g_stop_requested = TRUE`. The worker thread
checks this flag inside its recv loop (between iterations). Because the existing core
functions do not check such a flag today, the worker thread will need to either:

- **Option A (preferred)**: Wrap the core functions at the worker level. Instead of
  calling `analyze_message_types()` directly, replicate the socket+recv loop in
  `gui_thread.c`, calling only the lower-level `analyze_rtcm_message()` and
  `extract_satellites()` functions, and checking `g_stop_requested` between iterations.
  This avoids modifying core code.

- **Option B (future)**: Add a `volatile bool *cancel` parameter to core functions.
  This would require modifying `src/` — acceptable in a later phase if desired.

---

## 6. stdout/stderr Capture (gui_log.c)

### 6.1 Mechanism

```c
// Before worker start:
int pipe_fds[2];
_pipe(pipe_fds, 4096, _O_TEXT);
int saved_stdout = _dup(_fileno(stdout));
int saved_stderr = _dup(_fileno(stderr));
_dup2(pipe_fds[1], _fileno(stdout));
_dup2(pipe_fds[1], _fileno(stderr));

// Timer callback (every 50 ms):
char buf[4096];
DWORD bytes = 0;
PeekNamedPipe(pipe_read_handle, NULL, 0, NULL, &bytes, NULL);
if (bytes > 0) {
    int n = _read(pipe_fds[0], buf, sizeof(buf) - 1);
    buf[n] = '\0';
    // Append to log EDIT control via EM_SETSEL + EM_REPLACESEL
}

// After worker finishes:
_dup2(saved_stdout, _fileno(stdout));
_dup2(saved_stderr, _fileno(stderr));
_close(pipe_fds[0]);
_close(pipe_fds[1]);
```

### 6.2 Considerations

- The multiline `EDIT` control has a default text limit of 32 KB. Call
  `SendMessage(hLog, EM_SETLIMITTEXT, 0x100000, 0)` to raise it to 1 MB.
- For very long-running streams, periodically truncate the log (remove the
  oldest half of the text) to prevent memory growth.
- `setvbuf(stdout, NULL, _IONBF, 0)` is set before redirection to ensure
  printf output is flushed line-by-line into the pipe.

---

## 7. Detailed Component Designs

### 7.1 Configuration Panel

**Load Config**: Calls `GetOpenFileName()` (OPENFILENAME struct with `*.json` filter),
then `load_config(filename, &config)`, then populates all edit controls from the
`NTRIP_Config` struct fields.

**Save Config**: Reads all edit controls into an `NTRIP_Config`, serializes to JSON
using cJSON, writes to file via `GetSaveFileName()`.

**Generate Template**: Calls `initialize_config("config.json")` and reports success
in the log panel.

**Edit → Config sync**: A helper function `GuiToConfig(HWND hwnd, NTRIP_Config *cfg)`
reads all edit controls via `GetWindowText` and `atoi`/`atof` into the struct. Called
before every operation.

### 7.2 Mountpoint Table

`receive_mount_table()` returns a raw string. The GUI parser splits it:

1. Skip HTTP header (everything before first `STR;` or `CAS;` or `NET;`).
2. Split by `\n` into rows.
3. For each row starting with `STR;`, split by `;` into fields.
4. Map fields to ListView columns:

| Col # | Field Index | Header |
|---|---|---|
| 0 | 1 | Mountpoint |
| 1 | 2 | Identifier |
| 2 | 3 | Format |
| 3 | 4 | Format Details |
| 4 | 5 | Carrier |
| 5 | 6 | Nav System |
| 6 | 7 | Network |
| 7 | 8 | Country |
| 8 | 9 | Latitude |
| 9 | 10 | Longitude |

Double-clicking a mountpoint row auto-fills the Mountpoint edit control.

### 7.3 Stream Decode Tab (Log)

When the user clicks **Decode Stream**:

1. `GuiToConfig()` populates config.
2. Worker thread opens socket and loops, calling `analyze_rtcm_message()` with
   `suppress_output = false`. All output goes to redirected stdout → pipe → log panel.
3. If a filter is specified, the worker parses the filter edit control into an
   `int[]` array (same logic as CLI `main.c` line 104-116).
4. The **Stop** button breaks the recv loop.

### 7.4 Message Type Analysis Tab

When the user clicks **Analyze Types**:

1. Worker thread replicates the `analyze_message_types()` recv loop.
2. For each decoded message, it updates a local `MsgStats[]` array.
3. On timer or completion, the worker `PostMessage(WM_APP_STATS_READY)` with a
   heap-allocated copy of the stats array.
4. UI thread populates the Message Stats ListView:

| Column | Width | Data |
|---|---|---|
| Message Type | 100 | `msg_type` |
| Count | 70 | `stats[i].count` |
| Min Δt (s) | 100 | `stats[i].min_dt` |
| Max Δt (s) | 100 | `stats[i].max_dt` |
| Avg Δt (s) | 100 | `stats[i].sum_dt / count` |

### 7.5 Satellite Analysis Tab

When the user clicks **Analyze Sats**:

1. Worker thread replicates the `analyze_satellites_stream()` recv loop.
2. Calls `extract_satellites()` for each MSM message.
3. On completion, posts `WM_APP_SATS_READY` with heap-allocated `SatStatsSummary`.
4. UI thread populates the Satellites ListView:

| Column | Width | Data |
|---|---|---|
| GNSS | 90 | `gnss_name_from_id()` |
| Sats Seen | 80 | `gnss[i].count` |
| Satellites | 400+ | RINEX IDs via `rinex_id_from_gnss()` |

### 7.6 Status Bar

Three-part status bar updated via `WM_APP_STATUS_UPDATE`:

- **Part 0**: Connection state — `"Connected to {caster}:{port} / {mount}"` or `"Disconnected"`.
- **Part 1**: Elapsed time — `"Elapsed: {n} s"`.
- **Part 2**: Message counter — `"Msgs: {n}"`.

Updated by the worker thread posting `WM_APP_STATUS_UPDATE` every second.

---

## 8. Menu Structure

```
File
├── Load Configuration...     Ctrl+O
├── Save Configuration...     Ctrl+S
├── Generate Template Config
├── ─────────────
└── Exit                      Alt+F4

Connection
├── Get Mountpoints           Ctrl+M
├── Connect Stream            Ctrl+D
├── ─────────────
└── Disconnect / Stop         Ctrl+Q  (or Escape)

Analysis
├── Analyze Message Types     Ctrl+T
├── Analyze Satellites        Ctrl+L
├── ─────────────
├── Set Analysis Time...
└── Set Message Filter...

Help
├── About NTRIP-Analyser
└── View on GitHub
```

**This is the original design and no longer the menu.** There is no
Analysis menu — those became tabs — and there are now View and Tools
menus the sketch never had. `gui/resource.rc` is authoritative;
[`docs/gui.md`](../docs/gui.md) describes the menus a user sees. What
has been added since, and where it is designed:

| Menu item | Section |
|---|---|
| View → Station Check… | §13 |
| View → Stability… | §14 |
| File → Load Thresholds… | §15 |
| File → Start/Stop RTCM Capture, Replay RTCM File… | `docs/gui.md` |
| View → Sky Plot, VRS Monitor, Signal Quality, Session History, Ionosphere, Ionosphere Sky | `docs/gui.md` |

---

## 9. Build System — Code::Blocks MinGW

The project already uses the **Code::Blocks MinGW** toolchain at
`C:/Program Files/CodeBlocks/MinGW/bin/` (GCC 14.2.0, MinGW-W64 x86_64-ucrt-posix-seh).
The GUI build follows the same pattern as the existing CLI build task in
`.vscode/tasks.json` — direct `gcc.exe` invocation, no CMake required.

### 9.1 Toolchain

| Tool | Path | Purpose |
|---|---|---|
| `gcc.exe` | `C:/Program Files/CodeBlocks/MinGW/bin/gcc.exe` | C compiler |
| `windres.exe` | `C:/Program Files/CodeBlocks/MinGW/bin/windres.exe` | Resource compiler (.rc → .o) |

### 9.2 Build Command (two steps)

**Step 1 — Compile the Windows resource file:**

```bash
"C:/Program Files/CodeBlocks/MinGW/bin/windres.exe" gui/resource.rc -o gui/resource.o
```

**Step 2 — Compile and link everything:**

> **Outdated.** The command below is the original minimal build. The
> project now links a dozen further modules and needs `-lcomdlg32` and
> `-lgdiplus` as well. Use `build-gui.bat`, or see
> [`docs/compile.md`](../docs/compile.md) for the current command.

```bash
"C:/Program Files/CodeBlocks/MinGW/bin/gcc.exe" -g -mwindows -std=c99 -D_USE_MATH_DEFINES \
    gui/gui_main.c gui/gui_layout.c gui/gui_events.c \
    gui/gui_thread.c gui/gui_log.c gui/gui_parsers.c \
    src/ntrip_handler.c src/rtcm3x_parser.c src/config.c src/nmea_parser.c \
    lib/cJSON/cJSON.c \
    gui/resource.o \
    -Isrc -Ilib/cJSON -Igui \
    -lws2_32 -lcomctl32 -lm \
    -Wall \
    -o bin/ntrip-analyser-gui.exe
```

Key flags:

| Flag | Purpose |
|---|---|
| `-mwindows` | Link as a Windows GUI app (no console window, links `user32`, `gdi32`, `kernel32`) |
| `-g` | Include debug symbols (matches existing CLI build) |
| `-std=c99` | Match the project's C standard |
| `-D_USE_MATH_DEFINES` | Required for `M_PI` constant used by core `rtcm3x_parser.c` under strict C99 |
| `-lws2_32` | Winsock2 (networking, same as CLI) |
| `-lcomctl32` | Common Controls v6 (ListView, TabControl, StatusBar) |
| `-lm` | Math library |
| `-Wall` | All warnings (matches existing CLI build) |
| `-Isrc -Ilib/cJSON -Igui` | Include paths for core headers, cJSON, and GUI headers |

### 9.3 VS Code Build Task

A new task is added to `.vscode/tasks.json` alongside the existing CLI task.
It chains the `windres` step and the `gcc` step sequentially:

```json
{
    "label": "Build NTRIP-Analyser GUI (CodeBlocks MinGW)",
    "type": "shell",
    "command": "C:/Program Files/CodeBlocks/MinGW/bin/gcc.exe",
    "args": [
        "-g",
        "-mwindows",
        "-o",
        "bin/ntrip-analyser-gui.exe",
        "gui/gui_main.c",
        "gui/gui_layout.c",
        "gui/gui_events.c",
        "gui/gui_thread.c",
        "gui/gui_log.c",
        "gui/gui_parsers.c",
        "src/ntrip_handler.c",
        "src/rtcm3x_parser.c",
        "src/config.c",
        "src/nmea_parser.c",
        "lib/cJSON/cJSON.c",
        "gui/resource.o",
        "-Isrc",
        "-Ilib/cJSON",
        "-lws2_32",
        "-lcomctl32",
        "-lm",
        "-Wall"
    ],
    "options": {
        "cwd": "${workspaceFolder}"
    },
    "group": "build",
    "problemMatcher": ["$gcc"],
    "detail": "Builds the NTRIP-Analyser Windows GUI using CodeBlocks MinGW",
    "dependsOn": "Compile GUI Resources"
}
```

With a dependent pre-task for the resource compiler:

```json
{
    "label": "Compile GUI Resources",
    "type": "shell",
    "command": "C:/Program Files/CodeBlocks/MinGW/bin/windres.exe",
    "args": [
        "gui/resource.rc",
        "-o",
        "gui/resource.o"
    ],
    "options": {
        "cwd": "${workspaceFolder}"
    },
    "problemMatcher": [],
    "detail": "Compiles gui/resource.rc into gui/resource.o"
}
```

### 9.4 VS Code Launch Configuration

Added to `.vscode/launch.json` for debugging the GUI:

```json
{
    "name": "Run NTRIP-Analyser GUI (Windows)",
    "type": "cppdbg",
    "request": "launch",
    "program": "${workspaceFolder}/bin/ntrip-analyser-gui.exe",
    "args": [],
    "stopAtEntry": false,
    "cwd": "${workspaceFolder}",
    "environment": [],
    "MIMode": "gdb",
    "miDebuggerPath": "C:/Program Files/CodeBlocks/MinGW/bin/gdb.exe",
    "setupCommands": [
        {
            "description": "Enable pretty-printing for gdb",
            "text": "-enable-pretty-printing",
            "ignoreFailures": true
        }
    ],
    "preLaunchTask": "Build NTRIP-Analyser GUI (CodeBlocks MinGW)"
}
```

### 9.5 Output

| Target | Executable | Build task |
|---|---|---|
| CLI (existing) | `bin/ntrip-analyser.exe` | "Build NTRIP-Analyser (CodeBlocks MinGW)" |
| GUI (new) | `bin/ntrip-analyser-gui.exe` | "Build NTRIP-Analyser GUI (CodeBlocks MinGW)" |

Both targets coexist in `bin/` and share no output files.

---

## 10. Implementation Plan (Phased)

### Phase 1 — Skeleton Window (Est. 1–2 days)

- [ ] Create `gui/` directory and all file stubs.
- [ ] `gui_main.c`: WinMain, register class, create main window, message loop.
- [ ] `gui_layout.c`: Create all controls with hardcoded positions; handle WM_SIZE.
- [ ] `resource.h` / `resource.rc`: IDs, menu bar, application icon.
- [ ] Verify it compiles and runs as an empty window with controls.

### Phase 2 — Configuration I/O (Est. 1 day)

- [ ] Wire Load/Save/Generate buttons to `load_config()`/`initialize_config()`.
- [ ] `GuiToConfig()` and `ConfigToGui()` helper functions.
- [ ] File dialogs via `GetOpenFileName` / `GetSaveFileName`.

### Phase 3 — Mountpoint Retrieval (Est. 1–2 days)

- [ ] `gui_thread.c`: `WorkerGetMountpoints()` thread function.
- [ ] `gui_parsers.c`: `ParseMountTable()` → ListView population.
- [ ] `gui_events.c`: Handle `WM_APP_MOUNT_RESULT`.
- [ ] Double-click row → auto-fill mountpoint edit.

### Phase 4 — Log Redirect + Stream Decode (Est. 2–3 days)

- [ ] `gui_log.c`: Pipe creation, stdout/stderr redirect, timer-based pump.
- [ ] `gui_thread.c`: `WorkerDecodeStream()` — replicate recv loop with
      `analyze_rtcm_message()` calls and `g_stop_requested` check.
- [ ] Wire Stop button to cancel.
- [ ] Verify log output appears in the Log tab in real time.

### Phase 5 — Analysis Tabs (Est. 2 days)

- [ ] `WorkerAnalyzeTypes()` — collect `MsgStats[]`, post `WM_APP_STATS_READY`.
- [ ] `WorkerAnalyzeSats()` — collect `SatStatsSummary`, post `WM_APP_SATS_READY`.
- [ ] `gui_parsers.c`: Populate respective ListViews.
- [ ] Status bar updates during analysis.

### Phase 6 — Polish (Est. 1–2 days)

- [ ] DPI awareness (Per-Monitor V2 manifest or runtime detection).
- [ ] Keyboard accelerators (Ctrl+O, Ctrl+M, etc.).
- [ ] Error dialogs (`MessageBox`) for connection failures, parse errors.
- [ ] Window minimum-size enforcement (`WM_GETMINMAXINFO`).
- [ ] About dialog with version, author, license info.
- [ ] Application icon (`.ico` embedded via resource).
- [ ] Testing on Windows 10 and Windows 11.

---

## 11. Risk Assessment

| Risk | Impact | Mitigation |
|---|---|---|
| Core functions use `printf` directly — hard to capture structured data | Medium | Phase 4 pipe redirect captures text; Phase 5 workers call lower-level APIs (`analyze_rtcm_message`, `extract_satellites`) directly for structured data. |
| Blocking `recv()` inside worker can't be cancelled instantly | Low | Use `select()` with timeout before each `recv()`, checking `g_stop_requested` each iteration. Alternatively `closesocket()` from the UI thread to force `recv()` to return. |
| Log EDIT control may become slow with very large text | Low | Limit text to 1 MB; periodically truncate oldest half. Consider switching to a virtual-mode `RichEdit` in a future version. |
| MinGW `windres` quirks with `.rc` files | Low | Use `#include <winres.h>` in `.rc`; test early in Phase 1. |
| `_dup2` / `_pipe` MinGW-specific behavior | Low | Use MinGW-w64 CRT functions (`_pipe`, `_dup2`, `_fileno`); verify in Phase 1 with the Code::Blocks MinGW toolchain (GCC 14.2.0). |

---

## 12. Future Enhancements (Out of Scope)

Moved to the project-wide feature backlog: [`design/todo.md`](../design/todo.md).

---

## 13. Station Check

The CLI has `--check` and the Android app has a station mode; the GUI has
neither, and never linked `kpi.c`. This section is the design that
closed that gap, as built.

### 13.1 What it is

A **bounded acceptance test**: the user starts it explicitly, it watches
the open stream for about ninety seconds, and it ends with a verdict that
stops moving. That last part is the point. A live dashboard cannot be
quoted in a handover — a verdict that keeps changing is not a sign-off.
The engine already enforces this: `kpi_update()` holds a candidate
verdict until it has survived `KPI_SUSTAIN_S` unchanged, so a station
that flickers cannot pass by being briefly healthy at the right moment.

**A refusal says which refusal it was** (3.7.0). Where KPI 1 used to
read *"No connection to the caster"* for a wrong host, a wrong port, a
wrong password and a missing mountpoint alike, it now carries the
classification the session made: *"Nothing is listening on that port"*,
*"User name or password rejected"*, and so on. The words come from
`ns_failure_short()` in `src/core/ns_failure.c` — short deliberately,
because this column is about sixty characters wide — and the fuller
sentence, the one with the advice in it, arrives in the log as the
session's own error line. Neither is written here: a second wording of
one judgement is how two descriptions of the same fault start
disagreeing.

Scope is the **eight KPIs, plus the five VRS assertions when the station
is a VRS** — matching `--check-vrs`. The GUI already classifies station
type in Stream Health, so the classification exists before the check
starts and no guesswork is involved.

### 13.2 Where it lives

A **floating window opened from View → Station Check**, in the manner of
Sky Plot and VRS Monitor: `gui_check_window.{c,h}`, its own class, its
own paint path, resizable, and screenshotable whole. The bottom tab strip
was the cheaper option and was rejected for a reason worth recording: a
check is watched *alongside* Stream Health and Msg Stats while it runs,
and a tab makes the two mutually exclusive.

### 13.3 Where the run state lives

`KpiRun` and `VrsRun` are accumulators with sustain clocks, so they must
outlive a repaint — and they belong in `AppState`, not in the window.
Closing the window must not silently abandon a ninety-second test the
user started, and a check in progress is a property of the session, not
of a piece of chrome.

```c
/* in AppState */
KpiRun    checkRun;        /* accumulator; sustain clocks live here     */
KpiReport checkReport;     /* last evaluation, for the window to paint  */
VrsRun    checkVrs;        /* only advanced when the station is a VRS   */
BOOL      checkActive;     /* a bounded run is in progress              */
BOOL      checkSettled;    /* verdict has stopped moving; frozen        */
double    checkStartedAt;  /* GetTickCount64()/1000.0 at start          */
```

### 13.4 What drives it

**The statistics event, not a timer.** `NS_EV_STATS` already refreshes
`AppState::lastStats` once a second, and that struct is exactly what
`kpi_update()` consumes. Evaluating there keeps the KPI clock in step
with the data that justifies it; a window timer would sometimes judge a
snapshot twice and sometimes skip one. The window's own `WM_TIMER` does
nothing but repaint.

### 13.5 The one piece of plumbing that is missing

KPI 8 compares what the sourcetable advertises against what arrives, and
it reads that from the **session**: `ns_set_advertised()` and
`ns_set_advertised_gnss()`. The CLI and the Android bridge both call
them. **The GUI calls neither.** Its Stream Health row computes the same
comparison independently, by counting rows in its own Msg Stats
ListView — a UI-derived answer to a question the session already
answers.

So the GUI must feed the session from the sourcetable entry it already
fetches on connect (`AppState::advAutoFetched`), exactly as the other two
front-ends do. Without it, KPI 8 would evaluate against an empty
advertisement and quietly pass everything.

That also removes a duplicate: once the session holds the comparison,
the Stream Health row should read it from there rather than counting
ListView rows, and the two can no longer disagree.

### 13.6 The gate test needs consent

Assertion A5 asks whether the service is gated: `vrs_begin_gate_test()`
stops the keep-alive GGA and watches for the caster to drop the stream,
which a real network service does and a fixed base does not. So the test
ends the session it is testing. That is an action, not an observation,
and it must not happen because someone pressed "Run Check". It gets its
own checkbox in the window, off by default, labelled with what it will
do, and it starts only once the passive assertions have their answer.

The other four assertions are passive and run whenever the station is a
VRS. Note that A5 is a *classification*, not a failure: a station that
keeps streaming is a fixed base, which is a fact about what it is rather
than a fault in what it does.

(The rover-position shift is a different thing entirely and stays where
it is — the manual N/E/S/W controls in the VRS Monitor window. An
earlier draft of this section confused the two.)

### 13.7 Preconditions

- **The stream must be open.** The Run Check button is disabled
  otherwise, and says why.
- **The sourcetable decides whether KPI 8 can be judged at all.** The
  GUI fetches it when a stream opens on a mountpoint it does not already
  have an entry for, so in practice it is there. When it is not — a
  mountpoint the caster does not list — KPI 8 reports *no sourcetable
  entry to compare against* and stays pending, deliberately: "we could
  not check" and "we checked and it was fine" are different statements.

  A pending KPI keeps the roll-up at RUNNING, so such a run can never
  settle. It is bounded instead by the same **300-second ceiling the
  CLI applies**, after which the window reports *no verdict inside the
  time limit* and stops. A bounded test that cannot end is not bounded.

### 13.7a How a run ends

Three ways, and the window distinguishes them, because rows left on
screen from an unfinished test read exactly like a finished one:

| End | Header |
|---|---|
| The verdict held for `KPI_SUSTAIN_S` | `settled after 91 s, held 60 s` |
| The user pressed Stop | `RUNNING (stopped)` — `stopped after 20 s -- no verdict` |
| The stream closed, or the ceiling was reached | `... the stream closed after 42 s -- no verdict` |

### 13.8 Work

| # | Change | Files |
|---|---|---|
| 1 | Feed the session its advertisement on connect | `gui_events.c` (sourcetable handler) |
| 2 | Point the Stream Health row at the session's comparison | `gui_events.c` |
| 3 | Run state in `AppState` | `gui_state.h` |
| 4 | Advance the check on `NS_EV_STATS` | `gui_events.c` |
| 5 | The window: rows, verdict, elapsed, Run/Stop, gate-test checkbox | `gui_check_window.{c,h}` (new) |
| 6 | Menu item and command id | `resource.rc`, `resource.h`, `gui_events.c` |
| 7 | Build entries | `CMakeLists.txt`, `build-gui.bat` |

`kpi.c` and `vrs_check.c` are already in both build paths, so nothing new
is compiled — only called.

### 13.9 Deliberately not in this round

- **A saved report file.** The window can be screenshotted and
  `File → Export Statistics` already writes the underlying numbers. A
  report serialiser is worth adding once the on-screen shape has settled,
  not before.
- **Continuous evaluation.** Rejected as the primary model above; if it
  is wanted later it is the same engine with the bound removed, and the
  live and settled states must then be visually distinct.

---

### 13.10 What changed after it shipped

The check window as designed above is intact; three things were added to
it later, all consequences of the work in §15.

- **A `Limit` column**, between Value and Detail. `KpiResult` gained
  `limit` and `limit_dir`, set where the verdict is decided, and
  `kpi_limit_text()` formats them in core. A verdict without the number
  behind it cannot be argued with: *"Median C/N0 — 45.7 — healthy"*
  invites the question *healthy compared with what?* Check 5 shows the
  figure **this** station was held to, which is the sum over the
  constellations it streams — a static string could not have said that.
  The structural checks and the VRS assertions leave the column blank,
  being tests rather than comparisons.
- **Units and per-check precision on the value**, from
  `kpi_value_unit()` and `kpi_value_decimals()`. The window printed
  every value at two decimals, so an elevated CRC rate — then a
  fraction — read `0.00` beside its `WARN`. A warning whose number says
  nothing is worse than no number at all.
- **The run takes a policy.** `kpi_run_start()` and `vrs_run_start()`
  are handed `state->thresholds.kpi` and `.vrs`, so a check is conducted
  under whichever standard is loaded and cannot change standard midway.

---

## 14. Stability (tier 2)

The Station Check answers whether a station is fit **now**. This answers
whether it has *been* fit, over hours — the question no ninety-second
window can reach at any price. Same engine as the CLI's `--report` and
the daemon's `<mountpoint>.report.json` (`src/core/station_report.c`),
over the stream the GUI already has open.

`gui_report_window.{c,h}`, class `NtripStabilityClass`, opened from
**View → Stability**.

### 14.1 Why it is not a second Station Check

The check is a **bounded run**: the user starts it, it ends, and the
verdict freezes so it can be quoted. This is the opposite shape, and
deliberately so:

| | Station Check | Stability |
|---|---|---|
| Started | explicitly, by button | never — it accumulates with the session |
| Ends | at a settled verdict, or a bound | not at all; it is read whenever |
| Silent until | ~90 s | 10 minutes (`SR_MIN_WINDOW_S`) |
| Vocabulary | `STATION OK` / `CAUTION` / `FAILED` | `STABLE` / `DEGRADED` / `UNSTABLE` |

**The vocabularies never overlap**, which is load-bearing rather than
stylistic. A station can be fit this second and have been unstable all
week; both statements are true, and a user seeing one set of words twice
would conclude that one of them is broken.

The one button is **Restart window**, which begins a fresh window of
evidence without touching the stream. It exists for one moment in
particular: you re-seat the antenna and want the next hour judged on its
own rather than averaged with the hour that prompted the change. It
preserves `reportFromCapture`, so restarting a replay cannot quietly
turn it into a live run and begin inventing availability figures.

### 14.2 Where the state lives

In `AppState`, for the same reason `KpiRun` is there — an hour of
evidence must survive its window being closed, and `SrState` cannot be
rebuilt from a repaint.

```c
/* in AppState */
SrState       reportRun;         /* the accumulator                     */
StationReport reportOut;         /* last build, for the window to paint */
BOOL          reportHave;
BOOL          reportFromCapture; /* a replay: availability is n/a       */
double        reportLastSample;  /* stream time of the last sr_feed     */
```

### 14.3 What drives it, and on which clock

`NS_EV_STATS`, beside `CheckOnStats()` — the data justifies the verdict,
so the window steps once per snapshot rather than on a timer that knows
nothing about whether anything arrived.

**Samples are paced and stamped by `NsStatsSnapshot::stream_time_s`, not
by the host clock.** A dropout therefore counts as window, an NTP
correction cannot distort a running window, and a replayed capture is
judged over the window the *capture* holds rather than the seconds the
disk took. A stream carrying no observation epochs has no clock at all,
and the window says it cannot measure rather than guessing.

### 14.4 Two session-layer defects this exposed

Wiring the window up found faults that had nothing to do with the
window, and both were invisible until something consumed the events:

1. **The replay worker set `stats_interval_s = 0.0`**, so `NS_EV_STATS`
   never fired for a capture at all. The window would have stayed empty
   for ever.
2. **`maybe_emit_stats()` paced itself on the wall clock.** Turning the
   interval on was not enough: six hours of capture arrive in
   milliseconds, so it would have emitted *two* snapshots where the live
   run emitted twenty-one thousand. The gate now runs on `obs_clock()` —
   arrival time live, stream time on a replay — which fixes it for every
   future consumer of that event, not only this window.

### 14.5 Defects found by looking at it

Three, all of one family: the screen asserting something it had not
measured.

- **A partial first epoch became the window's minimum.** The first live
  run reported *"fewest held: 9"* against a station that never dropped
  below 39, because `sats_total` describes the last five seconds and the
  first sample lands mid-epoch. Fixed with `SR_WARMUP_S` (30 s).
- **Claims about the stream, made from a clock.** *"No dual-frequency
  pair to measure with"* at twenty-five seconds, against a station
  streaming MSM7 on two frequencies — ROTI needs phase arcs that take
  minutes to form. That wording and the C/N0 one now wait until the
  window is judgeable at all.
- **The header said the same thing three times.** Banner, evidence line
  and headline each carried the verdict. The evidence line now carries
  the number that was buried — how much more stream it wants — and the
  third line is the culprit clause alone, absent for a clean `STABLE`.

### 14.6 Column widths are measured, not guessed

Twice a fixed width clipped `INSUFFICIENT EVIDENCE`, which every session
shows for its first ten minutes and is therefore the worst of the four
to truncate. The verdict column is now measured with
`GetTextExtentPoint32()` over the strings it will hold, in the list's
own font — which meant setting that font **before** creating the
columns, since the control is born with the stock system font. `Detail`
then takes whatever width remains, so widening the window widens the
column that actually varies.

---

## 15. Thresholds in the GUI

Every verdict rests on a number someone chose, and
[`docs/thresholds.md`](../docs/thresholds.md) is candid about which of
them are well founded and which are starting points. **File → Load
Thresholds…** lets a user disagree.

### 15.1 What it does

Takes a JSON policy (`src/core/thresholds.h`) and applies it to both the
Station Check and the Stability window from their next run.

- **Applied over the built-in values, never over whatever was loaded
  before.** Two policies half-merged would be a standard nobody wrote.
- **A bad setting is refused with the field named**, in a message box,
  and the previous thresholds stay in force — nothing is half-applied.
- The file is **partial by design**: it carries only what the user
  changes, so it does not rot as thresholds are added.

### 15.2 The first thing this program remembers

The path is kept under `HKCU\Software\NTRIP-Analyser`, value
`ThresholdsPath`, and restored by `GuiThresholdsInit()` before any
window can judge anything.

**This is a departure from the design as it stood**: until now the GUI
persisted nothing at all — not window positions, not the auto-reconnect
toggle. It is justified because an installer working to one standard
should not reload it every morning, and it is deliberately only a
*path*, so the policy itself stays one file that the CLI, the service
and this program read and fingerprint identically.

A remembered file that has since been deleted or edited badly is
reported once on the log and then ignored, and the program starts on the
built-in values. It must not stop the program starting; it must not
silently become a different standard either.

### 15.3 What is not here

- **No editor.** Twenty fields, each needing its own bounds checking,
  and the validation belongs in core where it already is. Loading is the
  whole feature for anyone who has a policy; an editor is a separate
  piece of UI and a later decision.
- **No per-window policy.** One standard per process, for the same
  reason a run has one standard: two windows judging by different
  numbers would be indefensible in a screenshot.

---

## 16. Reference

- Win32 API: [Microsoft Learn — Desktop Win32 Apps](https://learn.microsoft.com/en-us/windows/win32/)
- Common Controls: [Microsoft Learn — About Common Controls](https://learn.microsoft.com/en-us/windows/win32/controls/common-controls-intro)
- NTRIP Protocol: [BKG NTRIP Documentation](https://igs.bkg.bund.de/ntrip/about)
- RTCM Standard: RTCM 10403.3 — Differential GNSS Services Version 3
