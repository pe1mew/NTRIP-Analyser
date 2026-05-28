# Compiling

The code is setup for compilation in Windows and Linux using *#DEFINES*.

## CLI Application

The CLI executable links these `src/` modules:

| Module | Purpose |
|---|---|
| `main.c` | Entry point, argument parsing |
| `ntrip_handler.c` | NTRIP client + TCP socket I/O |
| `rtcm3x_parser.c` | RTCM 3.x decoder (1005/1006/1019/1020/1042/1044/1045/1046/MSM4/MSM7, etc.) |
| `sv_ephemeris.c` | Per-(GNSS,PRN) ephemeris cache, TOW-only validity |
| `sv_orbit.c` | Keplerian + GLONASS RK4 orbit propagators |
| `config.c` | JSON config load/save |
| `cli_help.c` | Help text |
| `nmea_parser.c` | NMEA GGA sentence generation |

`rinex_nav.c` is GUI-only (the RINEX 3 NAV loader feeds the same eph
cache); the CLI does not need it.

### Windows
This code was originally developed on Windows using the Mingw compiler that comes with Code::Blocks. For this the primary compiler was configured in Visual Studio Code. See [tasks.json](.vscode/tasks.json).

For Windows: install Code::Blocks with Mingw compiler and Visual Studio Code. In VSC type `ctrl-shift-b` to compile the code.

Direct command line:
```batch
gcc -g -o bin/ntripanalyse.exe src/main.c lib/cJSON/cJSON.c src/rtcm3x_parser.c src/ntrip_handler.c src/config.c src/cli_help.c src/nmea_parser.c src/sv_ephemeris.c src/sv_orbit.c -Ilib/cJSON -lws2_32 -Wall
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
gcc -g -o bin/ntripanalyser src/*.c lib/cjson/cJSON.c -Ilib/cjson -Wall -lm
```

This command will:
- Compile all C source files in the `src/` directory using wildcard (`src/*.c`)
  — this automatically picks up `sv_ephemeris.c`, `sv_orbit.c`, and
  `rinex_nav.c` even though the CLI itself does not call into the RINEX
  loader (the symbols are unused and discarded at link time)
- Include the cJSON library from `lib/cjson/cJSON.c`
- Add the cJSON headers to include path (`-Ilib/cjson`)
- Enable debug symbols (`-g`) and all warnings (`-Wall`)
- Link the math library (`-lm`)
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


