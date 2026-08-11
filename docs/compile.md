# Compiling

The code is setup for compilation in Windows and Linux using *#DEFINES*.

## CLI Application

The CLI executable links these `src/` modules:

| Module | Purpose |
|---|---|
| `cli/main.c` | Entry point, argument parsing, sky-mode dispatcher |
| `cli/cli_stream.c` | The `-d`/`-t`/`-s` modes and the `--sky` eph stream, as session event handlers |
| `session/ntrip_session.c` | The NTRIP stream loop, shared by CLI, GUI and service |
| `net/ntrip_proto.c` | NTRIP request construction and response parsing |
| `net/ntrip_handler.c` | Sourcetable fetch and socket helpers |
| `core/ns_stats.c` | Statistics snapshot and its JSON/CSV serialisers |
| `core/rtcm3x_parser.c` | RTCM 3.x decoder (1005/1006/1019/1020/1041/1042/1044/1045/1046/MSM4/MSM7, etc.) |
| `core/sv_ephemeris.c` | Per-(GNSS,PRN) ephemeris cache, TOW-only validity |
| `core/sv_orbit.c` | Keplerian + GLONASS RK4 orbit propagators |
| `core/rinex_nav.c` | RINEX 3 multi-GNSS NAV loader (used by both CLI `-R` and GUI) |
| `core/sky_collect.c` | Per-MSM sector accumulator for the heatmap (`-s --sky`) |
| `core/sky_render.c` | Portable polar heatmap renderer + embedded PNG encoder |
| `core/config.c` | JSON config load/save |
| `cli/cli_help.c` | Help text + verbose-config table |
| `core/nmea_parser.c` | NMEA GGA sentence generation |

`src/` is layered: `core/` is pure computation with no I/O and no
platform headers, so it compiles for the Android NDK unchanged; `net/`
and `session/` add the NTRIP protocol and the shared stream loop; `cli/`
holds only what is CLI-specific.  The GUI and the monitoring service link
the same libraries.

### CMake (all artefacts)

The whole project also builds with CMake, which is what the Android NDK
will use:

```bash
cmake -B build && cmake --build build
```

On Windows with the Code::Blocks MinGW toolchain, name the generator and
the compiler explicitly -- a bare `cmake -B build` looks for MSVC:

```batch
cmake -B build -G "MinGW Makefiles" ^
      -DCMAKE_C_COMPILER="C:/Program Files/CodeBlocks/MinGW/bin/gcc.exe"
cmake --build build -j4
```

It produces `ntrip_core` and `ntrip_session` as static libraries and
whichever executables the platform supports: the CLI everywhere, the GUI
on Windows, `ntrip-monitord` on UNIX.  The project version is parsed out
of `src/core/version.h`, so the build system cannot disagree with the
binaries about the release number.

The hand-written commands below remain for a quick compiler-only build.

### Windows
This code was originally developed on Windows using the Mingw compiler that comes with Code::Blocks. For this the primary compiler was configured in Visual Studio Code. See [tasks.json](../.vscode/tasks.json).

For Windows: install Code::Blocks with Mingw compiler and Visual Studio Code. In VSC type `ctrl-shift-b` to compile the code.

Direct command line:
```batch
gcc -g -o bin/ntripanalyse.exe src/cli/main.c lib/cJSON/cJSON.c src/core/rtcm3x_parser.c src/core/ns_stats.c src/net/ntrip_proto.c src/session/ntrip_session.c src/cli/cli_stream.c src/net/ntrip_handler.c src/core/config.c src/cli/cli_help.c src/core/nmea_parser.c src/core/sv_ephemeris.c src/core/sv_orbit.c src/core/sky_collect.c src/core/sky_render.c src/core/rinex_nav.c -Isrc -Ilib/cJSON -lws2_32 -lm -Wall
```

### Linux
For linux install the following:
```bash
sudo apt install build-essential manpages-dev
```

Make sure the `bin` directory exists:
```bash
mkdir -p bin
```

To compile, in the root of the repository execute: 
```bash
gcc -g -o bin/ntripanalyser src/cli/*.c src/core/*.c src/net/*.c src/session/*.c lib/cJSON/cJSON.c -Isrc -Ilib/cJSON -Wall -lm -lpthread
```

This command will:
- Compile all C source files in `src/` and its `core/`, `net/` and
  `session/` subdirectories — the subdirectory globs are required, since
  `src/*.c` alone does not descend into them
- Include the cJSON library from `lib/cJSON/cJSON.c`
- Add the cJSON headers to include path (`-Ilib/cJSON`)
- Enable debug symbols (`-g`) and all warnings (`-Wall`)
- Link the math library (`-lm`)
- Link POSIX threads (`-lpthread`) — required by the CLI `-s --sky`
  mode, which spawns a parallel eph NTRIP worker
- Output the executable to `bin/ntripanalyser`

## GUI Application (Windows Only)

The GUI links additional source modules and a couple of extra libraries
on top of the CLI core:

| GUI module | Purpose |
|---|---|
| `gui/gui_main.c` | `WinMain`, message loop, window class |
| `gui/gui_layout.c` | Control creation, sizing, layout |
| `gui/gui_events.c` | Menu / button handlers, Stream Health checks, station classification, session-history sampling |
| `gui/gui_thread.c` | Worker threads (obs stream, eph stream, replay) |
| `gui/gui_log.c` | Log redirect (printf → log panel) |
| `gui/gui_parsers.c` | Sourcetable rows, advertised message types, NTRIP handshake parsing |
| `gui/gui_detail.c` | RTCM message detail viewer |
| `gui/gui_sky_window.c` | Floating Sky Plot window (rose, markers, heatmap, footer, snapshot) |
| `gui/gui_signal_window.c` | Floating Signal Quality window (C/N0 bars, C/N0-vs-elevation scatter) |
| `gui/gui_hist_window.c` | Floating Session History window (six strip charts on a shared time axis) |
| `gui/gui_vrs_window.c` | Floating VRS Monitor window (distance, polar plot, rolling chart) |
| `gui/gui_snapshot.c` | GDI+ PNG export helper and the shared save-with-prompt flow |
| `gui/gui_sv_detail.c` | Per-SV detail popup (PRN, az/el, per-band CNR) |
| `gui/resource.rc` | Menu bar, version info, manifest |
| `src/core/rinex_nav.c` | RINEX 3 multi-GNSS NAV loader |

Required link libraries: `-lws2_32 -lcomctl32 -lcomdlg32 -lgdiplus -lm`.
`-lgdiplus` is needed for the PNG snapshot support in the Sky Plot,
Signal Quality and Session History windows.

`build-gui.bat` holds the authoritative source list; treat any table or
command elsewhere in the docs as a description of it rather than a second
source of truth.

For detailed GUI compilation instructions, build methods, and troubleshooting, see the **[GUI Documentation](gui.md#building-the-gui)**.

**Quick build:**
```batch
build-gui.bat
```

## Shell Completion (CLI)

Tab-completion files for bash and zsh ship under `share/`:

```
share/
├── bash-completion/completions/ntripanalyse   # bash function _ntripanalyse_complete
└── zsh/site-functions/_ntripanalyse           # zsh _arguments definition
```

### Bash

System-wide:
```bash
sudo cp share/bash-completion/completions/ntripanalyse \
    /usr/share/bash-completion/completions/
```

Per-user:
```bash
mkdir -p ~/.local/share/bash-completion/completions
cp share/bash-completion/completions/ntripanalyse \
    ~/.local/share/bash-completion/completions/
```

Or just source it directly in your `~/.bashrc`:
```bash
. /path/to/share/bash-completion/completions/ntripanalyse
```

The completion script triggers on `ntripanalyse`, `ntripanalyser`, and
`ntripanalyse.exe` so it works for both Linux and Windows binary names.

### Zsh

System-wide:
```bash
sudo cp share/zsh/site-functions/_ntripanalyse /usr/share/zsh/site-functions/
```

Per-user (no root):
```bash
mkdir -p ~/.zsh/completions
cp share/zsh/site-functions/_ntripanalyse ~/.zsh/completions/
# Then in ~/.zshrc:
#   fpath=(~/.zsh/completions $fpath)
#   autoload -Uz compinit && compinit
```

Both files complete all long and short options, file-path arguments
(`-c`, `-R`, `-o`), and config-field overrides.


