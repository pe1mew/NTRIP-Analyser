# NTRIP-Analyser

A tool for analyzing NTRIP RTCM 3.x data streams, available as both a command-line interface (CLI) and a Windows graphical user interface (GUI).

## About this project

The primary goal of this project is to deepen my understanding of NTRIP streams, a field where open-source tools and public information are limited. By building this analyser, I aim to explore and learn the structure and content of RTCM 3.x messages transmitted over NTRIP.

A secondary goal is to practice and experiment with programming, leveraging AI tools such as GitHub Copilot. Please note that, while AI assistance has accelerated development, I cannot guarantee the originality or accuracy of all code segments, as the sources used by large language models are not always transparent or verifiable. The results and information presented here have not been exhaustively validated. As such, I advise caution: **do not rely on this code or its output for critical applications without independent verification.** The included disclaimer applies in full.

## Applications

### Command-Line Interface (CLI)
The CLI application provides full analysis capabilities via command-line arguments, ideal for automation, scripting, and remote operation.

**Executables:**
- Windows: `bin/ntripanalyse.exe`
- Linux: `bin/ntripanalyser`

### Windows GUI Application
The GUI application provides a user-friendly desktop interface with real-time monitoring, message analysis, satellite tracking, and detailed message decoding.

**Executable:** `bin/ntrip-analyser-gui.exe`

**Note:** The GUI is Windows-only and built with native Win32 API. See [GUI documentation](docs/gui.md) for detailed information.

## Core Functionalities

The analyser can perform the following operations on NTRIP streams:

1. **Retrieve mountpoint list** from a caster and display available streams
2. **Connect to NTRIP stream** and receive RTCM data with:
   - Real-time message decoding for all implemented RTCM message types
   - Message statistics (count, minimum/average/maximum transmission intervals)
   - Filtered message decoding (show only specific message types)
   - Satellite analysis (count unique satellites per GNSS constellation)
3. **Live polar sky plot** (GUI) — floating window showing every tracked
   satellite at its azimuth / elevation as seen from the reference-station
   ARP, with two render modes:
   - **Markers** — per-GNSS coloured dots shaded by CNR, with a 120-point
     trail of past positions and a left-click SV detail popup
     (per-band CNR table, PRN, az/el)
   - **Heatmap** — Onocoy-style observed-vs-expected sector coverage map
     (150 sectors, red → yellow → green ramp)
4. **Multi-GNSS ephemeris** (GPS / GLONASS / Galileo / QZSS / BeiDou) sourced
   from one of:
   - A second NTRIP connection that feeds RTCM 1019 / 1020 / 1042 / 1044 /
     1045 / 1046 frames into the eph cache (e.g. BKG `BCEP00BKG0` or
     Kadaster `BCEP00KAD0`), or
   - A RINEX 3 multi-GNSS NAV file loaded from disk
5. **RTCM capture and replay** (GUI) — save the live stream to a `.rtcm3`
   file and feed it back through the same UI pipeline for offline analysis
6. **PNG snapshot** of the sky plot, self-contained with ARP / mountpoint
   / clock footer

## Documentation

- **[Compilation Guide](docs/compile.md)** — Build instructions for Windows and Linux
- **[GUI User Guide](docs/gui.md)** — Complete Windows GUI documentation
- **[General Documentation](docs/readme.md)** — Additional project documentation

## Quick Start

### Building

**Windows CLI:**
```batch
gcc -g -o bin/ntripanalyse.exe src/main.c lib/cJSON/cJSON.c src/rtcm3x_parser.c src/ntrip_handler.c src/config.c src/cli_help.c src/nmea_parser.c src/sv_ephemeris.c src/sv_orbit.c src/sky_collect.c src/sky_render.c src/rinex_nav.c -Ilib/cJSON -lws2_32 -lm -Wall
```

**Windows GUI:**
```batch
build-gui.bat
```

**Linux CLI:**
```bash
mkdir -p bin
gcc -g -o bin/ntripanalyser src/*.c lib/cjson/cJSON.c -Ilib/cjson -Wall -lm -lpthread
```

See [compilation guide](docs/compile.md) for complete build instructions.

## License and disclaimer. 

Please note the license at the end of this document. 

# License
This project is free: You can redistribute it and/or modify it under the terms of a Creative Commons Attribution-NonCommercial 4.0 International License (http://creativecommons.org/licenses/by-nc/4.0/) by Remko Welling (https://ese.han.nl/~rwelling) E-mail: remko.welling@han.nl

<a rel="license" href="http://creativecommons.org/licenses/by-nc/4.0/"><img alt="Creative Commons License" style="border-width:0" src="https://i.creativecommons.org/l/by-nc/4.0/88x31.png" /></a><br />This work is licensed under a <a rel="license" href="http://creativecommons.org/licenses/by-nc/4.0/">Creative Commons Attribution-NonCommercial 4.0 International License</a>.

# Disclaimer
This project is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.