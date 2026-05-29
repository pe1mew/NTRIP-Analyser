# Compiling

The code is setup for compilation in Windows and Linux using *#DEFINES*.

## CLI Application

The CLI executable links these `src/` modules:

| Module | Purpose |
|---|---|
| `main.c` | Entry point, argument parsing, sky-mode dispatcher |
| `ntrip_handler.c` | NTRIP client + TCP socket I/O; `run_eph_stream()` worker |
| `rtcm3x_parser.c` | RTCM 3.x decoder (1005/1006/1019/1020/1041/1042/1044/1045/1046/MSM4/MSM7, etc.) |
| `sv_ephemeris.c` | Per-(GNSS,PRN) ephemeris cache, TOW-only validity |
| `sv_orbit.c` | Keplerian + GLONASS RK4 orbit propagators |
| `rinex_nav.c` | RINEX 3 multi-GNSS NAV loader (used by both CLI `-R` and GUI) |
| `sky_collect.c` | Per-MSM sector accumulator for the heatmap (`-s --sky`) |
| `sky_render.c` | Portable polar heatmap renderer + embedded PNG encoder |
| `config.c` | JSON config load/save |
| `cli_help.c` | Help text + verbose-config table |
| `nmea_parser.c` | NMEA GGA sentence generation |

The CLI no longer has any "GUI-only" sources; `rinex_nav.c` is now
shared, and the new `sky_*.c` modules together with the embedded PNG
encoder in `sky_render.c` mean the CLI can generate the same
heatmap-snapshot PNG as the GUI without GDI+ or libpng.

### Windows
This code was originally developed on Windows using the Mingw compiler that comes with Code::Blocks. For this the primary compiler was configured in Visual Studio Code. See [tasks.json](.vscode/tasks.json).

For Windows: install Code::Blocks with Mingw compiler and Visual Studio Code. In VSC type `ctrl-shift-b` to compile the code.

Direct command line:
```batch
gcc -g -o bin/ntripanalyse.exe src/main.c lib/cJSON/cJSON.c src/rtcm3x_parser.c src/ntrip_handler.c src/config.c src/cli_help.c src/nmea_parser.c src/sv_ephemeris.c src/sv_orbit.c src/sky_collect.c src/sky_render.c src/rinex_nav.c -Ilib/cJSON -lws2_32 -lm -Wall
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
gcc -g -o bin/ntripanalyser src/*.c lib/cjson/cJSON.c -Ilib/cjson -Wall -lm -lpthread
```

This command will:
- Compile all C source files in the `src/` directory using wildcard (`src/*.c`)
  — automatically picks up `sv_ephemeris.c`, `sv_orbit.c`, `rinex_nav.c`,
  `sky_collect.c`, and `sky_render.c`
- Include the cJSON library from `lib/cjson/cJSON.c`
- Add the cJSON headers to include path (`-Ilib/cjson`)
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
| `gui/gui_events.c` | Menu / button handlers (incl. Sky Plot, RINEX load, RTCM capture/replay) |
| `gui/gui_thread.c` | Worker threads (obs stream, eph stream, replay) |
| `gui/gui_log.c` | Log redirect (printf → listbox) |
| `gui/gui_parsers.c` | Message parsing for GUI display |
| `gui/gui_detail.c` | RTCM message detail viewer |
| `gui/gui_sky_window.c` | Floating Sky Plot window (rose, markers, heatmap, footer, snapshot) |
| `gui/gui_snapshot.c` | GDI+ PNG export helper |
| `gui/gui_sv_detail.c` | Per-SV detail popup (PRN, az/el, per-band CNR) |
| `gui/resource.rc` | Menu bar, version info, manifest |
| `src/rinex_nav.c` | RINEX 3 multi-GNSS NAV loader |

Required link libraries: `-lws2_32 -lcomctl32 -lcomdlg32 -lgdiplus -lm`.
`-lgdiplus` is new in this release and is needed for the Sky Plot PNG
snapshot.

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


