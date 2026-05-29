# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/), and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added — CLI sky-heatmap mode

- **`-S` / `--sky`** — new CLI mode that opens both the observation
  NTRIP stream and (in parallel) an ephemeris NTRIP stream, accumulates
  observed/expected per-sector counts on the same 150-sector grid the
  GUI uses, and on **Ctrl-C** saves a heatmap PNG named
  `YYYYMMDDHHmmss_ARP-EPG.png` — the same convention as the GUI
  snapshot.  Prints "Collecting heatmap data: Ctrl-C to save PNG and
  exit, Ctrl-A to abort without saving" at start and a per-second
  status line (always visible) of the form:
  ```
  [/] frames=21  MSM=21  obs+exp updates=0  rate=  9.6 kbit/s  total=12 KB
  ```
  showing spinner, frame counters, current NTRIP rate, and total
  bytes received.  **Ctrl-A** aborts immediately without writing a
  PNG (uses raw-mode `_kbhit`/`_getch` on Windows and termios
  ICANON-off + `select()` on POSIX so the keystroke needs no Enter).
  All RTCM decoder chatter (per-frame ephemeris blocks, station
  1005/1006 ARP info) is suppressed by default to keep the console
  clean; pass `-v` to also dump the full `decode_rtcm_*()` output of
  each frame.  Pre-flight rejects the mode if neither an EPH stream
  is configured nor a RINEX file is provided.
- **`-R` / `--RINEX <file>`** — RINEX 3 NAV file to preload the
  ephemeris cache before the live EPH stream takes over.  Useful for
  offline analysis of a captured stream, or as a fallback when no
  parallel eph NTRIP source is available.
- **New CLI module `src/sky_collect.{c,h}`** — distilled per-MSM
  sector accumulator (mirrors `gui_thread.c` obs-worker logic but
  threadless / GUI-less).
- **New CLI module `src/sky_render.{c,h}`** — portable polar
  heatmap renderer with an embedded PNG encoder (CRC32 + Adler32 +
  zlib stored-deflate).  No GDI+, no libpng, no zlib dependency.
- **New helper `run_eph_stream()`** in `ntrip_handler.{c,h}` — the
  CLI counterpart of the GUI's `WorkerOpenEphStream`.
- The lowercase `-s` / `--sat` short option keeps its original meaning
  (analyze unique satellites for N seconds); the new sky-heatmap mode
  uses capital `-S` / `--sky` to avoid the collision.

### Added — CLI scripting ergonomics

- **`--duration N`** — auto-stop `--sky` collection after N seconds and
  save the PNG normally.  Lets cron jobs / CI runs collect a fixed
  window without a manual Ctrl-C.
- **`-o <path>` / `--output <path>`** — write the `--sky` PNG to a
  caller-specified path instead of the timestamped default name.
  Scripts can pin the output location.
- **`-q` / `--quiet`** — suppress all informational chatter
  (`[OBS]` / `[EPH]` / `[RINEX]` / status line / `[SAVE]` lines).
  Errors still go to stderr; the saved PNG path still prints to
  stdout, so `OUTPUT=$(./ntripanalyse -S --duration 60 -q)` Just Works.
- **`--no-progress`** — keep informational lines but suppress the
  per-second status line specifically (useful for log files).
- **TTY-aware status line** — when stdout is a pipe / file instead of
  a terminal, the status line switches from carriage-return refresh
  to one-line-per-tick so log captures stay readable.
- **Final stdout line is the saved path** — `--sky` always prints the
  PNG filename on its own line as the last stdout output (or only
  stdout output under `-q`), making it trivially captureable.
- **Documented exit codes**:
  | Code | Meaning |
  |---|---|
  | 0 | Success |
  | 1 | Generic / runtime / connect failure |
  | 2 | Bad command-line arguments |
  | 3 | Could not open or parse config file |
  | 4 | `--sky` pre-flight: no ephemeris source configured |
  | 5 | Aborted by user (Ctrl-A) |
- **`--version`** — print `ntrip-analyser X.Y.Z` and exit.
- **Stderr / stdout separation** — all informational chatter (`[OBS]`,
  `[EPH]`, `[RINEX]`, `[SAVE]`, `[DEBUG]`, the status line) now goes
  to **stderr**.  Stdout is reserved for **data**: the sourcetable
  (`-m`), the decoded RTCM dumps (`-d`), stats tables (`-t`/`--sat`),
  the `--check-config` field listing, and the saved `--sky` PNG path.
  Scripts can do `OUTPUT=$(./ntripanalyse -S -q)` without filtering.
- **Per-field config overrides** (precedence: CLI > env > file):
  `--caster`, `--port`, `--mountpoint`, `--user`, `--password`,
  `--eph-caster`, `--eph-port`, `--eph-mountpoint`, `--eph-user`,
  `--eph-password`.  Same names as the `NTRIP_*` config keys, kebab-
  cased.  Lets one config file serve many mountpoints.
- **Env-var fallback** for every override flag above
  (`NTRIP_CASTER`, `NTRIP_PORT`, ..., `NTRIP_EPH_CASTER`,
  `NTRIP_EPH_PORT`, ...) — handy for CI secrets so credentials don't
  live in the JSON file.
- **`--check-config`** — dry-run mode that loads the config, applies
  env-var + CLI overrides, prints every resolved field on stdout
  (passwords masked as `(set)` / `(empty)`), does DNS lookups for
  both casters, and exits 0 on success or 1 if any field is missing
  / DNS fails.  Good for CI fail-fast checks.
- **`-v` verbose config dump now shows every field** — split into an
  `[Obs stream]` block (NTRIP_*, LATITUDE/LONGITUDE) and an
  `[Eph stream]` block (EPH_*); the eph block clearly labels
  itself `configured` vs `(not configured)`.  Passwords are masked as
  `(set)` / `(empty)` matching `--check-config`.

### Added — CLI Tier 3 ergonomics

- **`--json`** — emit one JSON status object per second on stderr in
  `--sky` mode instead of the human-readable line, plus a final
  `{"event":"stop","reason":"sigint|abort|duration|eof|error","saved":...}`
  summary.  Source-specific start event too:
  `{"event":"start","source":"ntrip|stdin",...}`.  Drop-in for `jq`.
- **`--rtcm-stdin`** — read obs RTCM bytes from stdin instead of
  opening the obs NTRIP socket; auto-stops at EOF.  Enables offline
  replay of captured `.rtcm3` files:
  ```
  ntripanalyse --sky --rtcm-stdin -R brdc.rnx -q -o out.png \
      < capture.rtcm3
  ```
  Same parsing pipeline as the live socket; sets stdin to binary mode
  on Windows to avoid CRLF mangling.
- **Action-flag conflict detection** — combinations like `-d -S` or
  `-m --sat` now error out with `[ERROR] Cannot combine action flags:
  ... and ...` (exit 2) instead of silently letting the last verb win.
- **Shell completion** — bash and zsh completion files under
  `share/bash-completion/completions/ntripanalyse` and
  `share/zsh/site-functions/_ntripanalyse`.  Covers all long/short
  options, file-path args (`-c`, `-R`, `-o`), and field overrides.
  See `docs/compile.md` for install paths.

### Changed

- **`src/rinex_nav.c` is no longer GUI-only** — it now compiles into
  the CLI build as well, so the same RINEX 3 loader feeds both
  applications.

### Added — Sky Plot and multi-GNSS

- **Floating Sky Plot window** (View -> Sky Plot...) showing each
  tracked satellite at its azimuth / elevation as seen from the
  reference-station ARP. Compass rose with 0/15/30/45/60/75 deg
  elevation rings, dotted N-S / E-W axes.
- **Two render modes** (M = toggle):
  - **Markers** — coloured per-GNSS dots (G green, R red, E blue,
    J magenta, C orange) shaded by CNR.  120-point ring buffer per
    SV draws a trail of past positions (~1 h of motion).  Left-click
    a marker for a live SV detail popup.
  - **Heatmap** — Onocoy-style observed-vs-expected coverage map.
    150 sectors (9 elevation bands x variable azimuth bins), red ->
    yellow -> green ramp on the ratio, light grey for polar hole.
- **Live ARP + clock footer** so PNG snapshots are self-contained.
- **Per-SV detail window** with PRN, az, el, best CNR, per-band CNR
  table (L1C/L2W/L5Q/E1C/E5Q/R1C/R2P/B1I/B2I/J1C/...), refresh
  timestamp, station context.  Copy button to clipboard.
- **Snapshot to PNG** (S on the sky window, or File -> Save Sky Plot
  as PNG).  Default filename `YYYYMMDDHHmmss_<TrackedSats|ARP-EPG>.png`.

### Added — Multi-GNSS ephemeris support

- **GLONASS** (RTCM 1020) state-vector decoder + RK4 numerical
  propagator with J2 zonal harmonic and luni-solar perturbations.
- **BeiDou** (RTCM 1042 D1 ephemeris) decoder with BDS-specific
  scale factors (2^3 s toc/toe, 2^-6 m harmonics).
- **QZSS** (RTCM 1044) decoder.
- **Galileo I/NAV** (RTCM 1046) decoder.
- Shared `sv_to_ecef` dispatcher routes Keplerian vs numerical
  propagator per GNSS.  GPS / Galileo / QZSS / BeiDou use the
  Keplerian path with per-GNSS gravitational parameters.

### Added — Ephemeris sources

- **Path A: dual-stream NTRIP** (already in prior commit; cleaned
  up here).  Optional second NTRIP connection feeds 1019/1020/1042/
  1044/1045/1046 frames into the eph cache while the primary
  obs stream carries MSM7.  Defaults to BKG products.igs-ip.net /
  BCEP00BKG0; works equally with Kadaster `BCEP00KAD0`.
- **Path B: RINEX 3 nav file loader** (File -> Load Ephemerides...).
  Reads multi-GNSS RINEX 3 nav files (G/E/R/J/C records) and
  populates the same ephemeris cache.  Useful offline.

### Added — RTCM capture and replay

- **Start/Stop RTCM Capture** in the File menu.  Worker writes
  every parsed frame's raw bytes to a `.rtcm3` file with a
  CRITICAL_SECTION-guarded handle.  Default filename
  `YYYYMMDDHHmmss_<mountpoint>.rtcm3`.  Auto-flushes on stream close.
- **Replay RTCM File** reads a captured file and feeds every frame
  back through the same UI-update pipeline (Msg Stats, satellites,
  sky plot, detail).  No pacing -- replay runs as fast as disk + CPU.

### Changed

- **Marker rendering**: brightness scales with the SV's best-signal
  CNR (~20 dB-Hz = dim, ~45 dB-Hz = full saturation).  Track trails
  drawn beneath the live marker in a desaturated GNSS colour.
- **Per-MSM7 frame**: CNR per (sat, signal) cached for the SV detail
  window.  Worker thread also extracts the best-of-signal CNR per
  PRN and forwards it as part of WM_APP_SKY_UPDATE.
- **Ephemeris Stream UI** now matches Connection Settings (same
  field widths, label names, two-row layout).  Fields start empty;
  populated only when a config file is loaded or the user types.

### Fixed

- **MSM7 verbose-decode bug**: `decode_rtcm_msm7_full` started
  reading at bit 0 where the 12-bit message number actually lives,
  shifting every later field by 12 bits and producing nonsense in
  the Msg Stats double-click detail of 1077/1087/1097/1117/1127/1137.
  Now consumes the message-number bits correctly.  Labels SVs with
  the right GNSS letter (E for Galileo, R for GLONASS, J for QZSS,
  C for BeiDou) and proper RTCM signal labels (L1C, L2W, E5Q, B2I,
  ...) instead of "S<NN>" placeholders.
- **`extract_satellites`** in ntrip_handler.c read the MSM sat-mask
  at bit 34 instead of 73 -- now delegates to the corrected
  `msm_extract_prns` helper.  Satellites tab shows the right PRNs.
- **`rtcm_printf`** g_rtcm_strbuf made thread-local so worker-thread
  decoder calls don't race the UI thread's detail-window string
  capture.
- **GPS week rollover** in `sv_eph_is_valid_at` and
  `kepler_to_ecef`: validity / propagation now compare in
  time-of-week only (with half-week wrap) so the 10-bit GPS-week
  field in RTCM 1019 doesn't cause every eph to be rejected.

### Build

- New source files: `src/sv_ephemeris.{c,h}`, `src/sv_orbit.{c,h}`,
  `src/rinex_nav.{c,h}`, `gui/gui_sky_window.{c,h}`,
  `gui/gui_snapshot.{c,h}`, `gui/gui_sv_detail.{c,h}`.
- GUI now links `-lgdiplus` for the PNG snapshot.
- CLI build line updated to include `sv_ephemeris.c` + `sv_orbit.c`
  (CLI doesn't need rinex_nav.c or the GUI-only files).
- Linux build globs `src/*.c` so it picks up the new modules
  automatically.

## [0.2] - 2026 (preceding the above)

### Added
- **Windows GUI Application** - Native Win32 GUI with real-time RTCM stream analysis
  - Interactive connection management and configuration
  - Real-time message monitoring and decoding
  - Live message detail windows with double-click support
  - Message statistics analysis (count, min/avg/max intervals)
  - Satellite counting and constellation breakdown
  - Configuration save/load (JSON format)
  - Clipboard support for copying data
  - Multi-format stream detection
  - Automated build scripts (`build-gui.bat`, `build-gui.ps1`)
- **MSM7 Full Decoder** - Complete rewrite with comprehensive structure
  - Reference Station ID and Epoch Time display
  - Multiple Message Flag, IODS, Clock Steering indicators
  - Divergence-free Smoothing and Smoothing Interval
  - Satellite data (rough range, extended info, phase-range rate)
  - Signal data (fine pseudorange, carrier phase, lock time, CNR, Doppler)
  - Support for GPS, GLONASS, Galileo, QZSS, BeiDou, SBAS (types 1077, 1087, 1097, 1117, 1127, 1137)
- **MSM4 Decoder** - Generic decoder for all GNSS constellations (types 1074, 1084, 1094, 1124)
- **Doxygen Documentation** - Comprehensive API documentation in header files
- **Distance and Heading Calculation** - From rover to base station for RTCM 1005/1006 messages
- **CRC Validation** - RTCM message CRC-24Q verification
- **CMake Support** - CMakeLists.txt for cross-platform builds
- **CI/CD Pipeline** - GitHub Actions workflow for automated builds
- **Debug Mode** - Server response display in ASCII and HEX format with `-v` flag

### Changed
- **RTCM 1013 Decoder** - Fixed System Parameters decoding, corrected field extraction
- **Documentation Structure** - Reorganized with simplified README and detailed GUI guide
- **Build System** - Added VS Code tasks for both CLI and GUI builds
- **Timing Measurement** - Fixed for Linux compilation

### Fixed
- RTCM 1013 message structure and field decoding
- Linux compilation timing measurement issues
- RTCM parser warnings (unused variables now displayed)

### Removed
- Leap seconds field from RTCM 1013 decoder (not part of specification)

## [0.1] - 2025-06-18

### Added
- Initial release with CLI application
- NTRIP client functionality (connect, authenticate, receive streams)
- RTCM 3.x message parsing foundation
- Basic message decoders (1005, 1006, 1007, 1008, 1012, 1013, 1019, 1033, 1045, 1230)
- Message statistics collection (count, intervals)
- Mountpoint list retrieval
- NMEA GGA sentence generation for rover positioning
- JSON configuration support
- Linux and Windows compatibility
- Basic documentation
