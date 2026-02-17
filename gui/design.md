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
│  lib/cjson/cJSON   .c/.h  — JSON parser                      │
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
| CLI (existing) | `bin/ntripanalyse.exe` | "Build NTRIP-Analyser (CodeBlocks MinGW)" |
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

- Real-time message rate graph (GDI+ or Direct2D).
- Map widget showing base station and rover positions.
- Multi-connection support (connect to multiple casters simultaneously).
- Export analysis results to CSV / JSON.
- Dark mode / theme support.
- Tray icon for background monitoring.
- Auto-reconnect on connection drop.

---

## 13. Reference

- Win32 API: [Microsoft Learn — Desktop Win32 Apps](https://learn.microsoft.com/en-us/windows/win32/)
- Common Controls: [Microsoft Learn — About Common Controls](https://learn.microsoft.com/en-us/windows/win32/controls/common-controls-intro)
- NTRIP Protocol: [BKG NTRIP Documentation](https://igs.bkg.bund.de/ntrip/about)
- RTCM Standard: RTCM 10403.3 — Differential GNSS Services Version 3
