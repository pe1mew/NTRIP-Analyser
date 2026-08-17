# NTRIP-Analyser CLI manual

`ntrip-analyser` is the command-line application for connecting to NTRIP casters, retrieving mountpoint tables, and analyzing or decoding RTCM 3.x streams. It is designed for GNSS enthusiasts who need to inspect or debug NTRIP data streams.

---

## Getting Started

### 0. General operation description

The program will emulate a rover using preconfigured (static) coordinates. At connection the program will authenticate and either request the mountpoint list of a NTRIP stream. When a NTRIP stream is opened, the program will send an GGA NMEA sentense with the configured coordinates at a 1 second interval to the NTRIP caster.

### 1. Configuration File (`config.json`)

A ready-to-edit example ships as [`bin/exampleConfig.json`](https://github.com/pe1mew/NTRIP-Analyser/blob/main/bin/exampleConfig.json)
(and beside the binaries in each release). It points at the Dutch
Kadaster open caster, including its ephemeris mountpoint for the sky
plot. Its username and password are empty because those streams are
served anonymously — it runs as shipped. `--generate` writes a template
too. For a caster that does need credentials, they are yours to obtain;
the analyser supplies none of its own.

Before using the program, you need a configuration file: it holds the connection and authentication details, and the coordinates of the emulated rover. *The program will not work without one.*

> **`config.json` is a default, not a requirement.** It is simply the
> name the program looks for when `-c` is not given. `-c` takes any
> path, so name files after the stations they describe —
> `ntrip-analyser -c rfsee.json --check` — and keep a directory of them.
> Nothing requires the word "config", and a file outside the working
> directory is often the better choice: credentials are stored in the
> clear, so `/etc/ntrip/` with `chmod 600` beats a file beside the binary.

Generating a default `config.json` with dummy values is done by using the `-g` argument when running the program:

```sh
ntripanalysis.exe -g
```

**Example `config.json`:**
```json
{
    "mountpoints": [
        {
            "caster": "your.caster.example.com",
            "port": 2101,
            "mountpoint": "MOUNTPOINT",
            "username": "your_username",
            "password": "your_password",
            "send_gga": false,
            "latitude": 0.0,
            "longitude": 0.0
        }
    ]
}
```
Set the various parameters
- **caster**: Hostname or IP address of the NTRIP caster.
- **port**: TCP port of the NTRIP caster (usually 2101).
- **mountpoint**: The mountpoint to request from the caster.
- **username**, **password**: HTTP Basic Authentication credentials, held
  **in the clear** — see [jsonConfigs.md](jsonConfigs.md).
- **latitude**/**longitude**: position of the rover being emulated, sent
  in the GGA uplink.

The file is a **list** even when it holds one connection, because it is
the same file the monitoring daemon and the Android app use. The CLI
analyses one stream at a time, so it takes the **first** entry and says
so when there are more:

```
[CONFIG] config.json lists 3 connections; using the first (RFSEE01) and ignoring the other 2.
```

An optional `eph_caster` / `eph_port` / `eph_mountpoint` block adds a
second connection for ephemerides, for stations that do not broadcast
their own on the observation stream. Files written by
earlier releases, with `NTRIP_CASTER` and friends at the top level, are
still read. The full description of the format is in
**[jsonConfigs.md](jsonConfigs.md)**.

---

### 2. Command-Line Arguments

Run the program from the command line:

```sh
ntrip-analyser [options]
```

#### **Options**

| Short | Long         | Argument         | Description                                                                 |
|-------|--------------|------------------|-----------------------------------------------------------------------------|
| -c    | --config     | [file]           | Specify config file (default: `config.json`)                                |
| -m    | --mounts     |                  | Show mountpoint (sourcetable) list and exit                                 |
| -d    | --decode     | [types]          | Decode RTCM stream, optionally filter by comma-separated message numbers    |
| -s    | --sat        | [seconds]        | count satellites received for N seconds (default: 60)                       |
| -t    | --types      | [seconds]        | Analyze message types for N seconds (default: 60)                           |
| -v    | --verbose    |                  | Print configuration and action details before running                       |
| &nbsp; | --check      | &nbsp;           | Station acceptance test: eight KPIs, ~90 s (exit 0/6/1)                     |
| &nbsp; | --check-vrs  | &nbsp;           | As --check plus the network-RTK assertions and GGA gate test                |
| -g    | --generate   |                  | Geerate default config.json with dummy values and exit.                     |
|       | --latitude   | value            | Override latitude in config                                                 |
|       | --longitude  | value            | Override longitude in config                                                |
|       | --lat        | value            | Same as `--latitude`                                                        |
|       | --lon        | value            | Same as `--longitude`                                                       |
| -h    | --help       |                  | Help information                                                            |
| -i    | --info       |                  | Information about the program, repository, an author                        |
| &nbsp; | --capture    | path             | Write every CRC-valid frame to a `.rtcm3` file (see below)                  |
| &nbsp; | --capture-max | MB              | Close the capture at this size and keep streaming; default no limit         |
| &nbsp; | --reconnect  | &nbsp;           | Reconnect with backoff if the stream drops                                  |

---

### 2a. The stability report (`--report`)

The eight checks ask whether a station is fit **now** and answer in about
ninety seconds. `--report` asks a different question — has it *been* fit,
and is it staying that way — which no ninety-second window can answer at
any price.

```sh
ntrip-analyser -t 3600 --report
```

Six measurements over the run: availability (reconnects per hour), frame
integrity, signal level, satellites held, ionosphere and delivery rate.
Each is graded on its own, and the report ends with the worst finding
stated with its evidence, rather than a bare word:

```
== DEGRADED over 6.0 h -- Frame integrity: worst CRC error rate 0.140 % ==
```

Four things about it are deliberate:

- **It needs ten minutes before it will say anything.** Below that, every
  line reads `INSUFFICIENT EVIDENCE` and the report says how much more it
  wants. That is the honest answer, and the alternative — grading an
  hour's question on a minute's data — is a mistake the eight checks
  already made three times before their sustain window was added.
- **It never uses the check's words.** `STABLE`, `DEGRADED`, `UNSTABLE`,
  never `STATION OK`. A station can be fit right now and have been
  unstable all week; both statements are true and neither contradicts the
  other.
- **It does not change the exit code.** `--check` owns that, because two
  verdicts competing for one exit status makes an automation surface
  unusable.
- **Some measurements are marked `n/a`, not zero.** Availability counts
  reconnections, which only a live session can observe; a report built
  from a capture says so instead of showing a clean zero it did not earn.

Two numbers are measured as changes rather than levels, which is what
makes them useful over hours. **Signal level** reports how far the mean
C/N0 *fell* from the best the window saw — a station that drops 7 dB to
41 dB-Hz is flagged, though 41 is a perfectly good level, because
something changed. **Frame integrity** reports the *worst* CRC rate
observed, not the average, because an average hides a bad ten minutes
inside a good six hours.

**The window is measured in stream time, not by this computer's clock.**
The report counts seconds from the observation epochs in the stream
itself, which has three consequences worth knowing:

- A dropout counts. Ten minutes of silence is ten minutes of window,
  because the epochs on either side of it say so.
- A clock correction on this machine cannot distort a running report.
- A replayed capture is judged over the window the *capture* holds, so a
  six-hour recording read from disk in twenty seconds covers six hours.

A stream carrying no observations at all — station and antenna messages
only — has no clock to measure with, and the report says so rather than
guessing.

### 2b. Capturing the stream to a file

`--capture` writes the stream to disk so that a converter can read it
hours later, or so the analyser can replay it offline. It works with any
mode that opens an observation stream — `-d`, `-t`, `-s`, `-S/--sky`,
`--check`, `--check-vrs` — and is refused, rather than ignored, by the
modes that do not.

```sh
ntrip-analyser -t 86400 --reconnect --capture /var/spool/gnss/ -q
```

That is the unattended form: a day of stream, drops ridden out, into a
directory. A **directory** argument gets the name the GUI proposes,
`YYYYMMDDHHmmss_<mountpoint>.rtcm3`, so a folder of captures sorts by
capture time and cron needs no unique name invented for it. A file path
is used as given.

What lands on disk is **frames only**: no handshake, nothing that failed
its CRC, none of the bytes between frames. So a capture is clean input to
[RTKLIB](https://www.rtklib.com/)'s `convbin` by construction, and one
written by the CLI is byte-for-byte the same as one written by the GUI.

Four behaviours worth knowing before leaving one running overnight:

- **It will not overwrite an existing file.** Deliberately unlike `-o`,
  which overwrites the sky PNG: a PNG costs a minute to redraw, and a
  capture can be a day of streaming that cannot be had again.
- **With `--reconnect`, one file spans the outage.** A drop leaves a gap
  in the epochs, not a truncated file. RTCM carries no wall clock, so the
  gaps are invisible in the bytes — the summary line reports the
  reconnect count, which is how many to expect.
- **A write failure stops the run**, with **exit 7**. Twenty hours of
  capture that silently stopped at hour three is the outcome this is
  built to make impossible. Exit 7 outranks every other verdict,
  including `--check`'s caution.
- **`--capture-max` is not an error.** The file closes on a frame
  boundary and the analysis continues to its normal end.

#### Running it unattended, over SSH

A day-long capture outlives the SSH session that starts it. `screen` or
`tmux` will do, but systemd is better here for one reason: **stopping a
unit sends SIGTERM, and the capture catches SIGTERM while capturing**, so
the file is closed and flushed rather than abandoned. One command, no
unit file:

```sh
sudo systemd-run --unit=ntrip-capture \
  --property=StandardOutput=null --property=StandardError=journal \
  ntrip-analyser -c /etc/ntrip/config.json \
  -t 86400 --reconnect --capture /var/spool/gnss/ -q
```

Then `journalctl -u ntrip-capture -f` to look in on it, and
`sudo systemctl stop ntrip-capture` to end it early with the file intact.
`-t 86400` stops by itself after a day in any case.

`StandardOutput=null` is not decoration. In `-t` mode the message-type
stream goes to **stdout** and is not silenced by `-q` — some five tokens
a second, which over a day fills the journal with `1077 1087 1097 1127`
to no purpose. The capture summary and every error are on stderr, so
nothing is lost.

**`StandardError=journal` is what keeps that true.** systemd's
`StandardError=` defaults to `inherit`, which means *whatever
`StandardOutput` is* — so setting the one to `null` discards the other
too. A six-hour capture run without it completed perfectly and reported
nothing at all: no frame count, no byte count, no reconnect count, and no
error had there been one. The two properties belong together.

Two figures for planning a long run: roughly **180 MB a day** from a
four-constellation MSM7 station at 1 Hz, and one reconnect per drop, each
of which is a gap in the file rather than a truncation.

The summary prints even under `-q`, because the file is the result of the
run rather than chatter about it:

```
[INFO] Capture: 159 frames, 62503 bytes -> /var/spool/gnss/20260815131024_RFSEE01.rtcm3
```

To turn one into a RINEX observation file — for a base-station
declaration, say — see
[Declaring a base station](base-declaration.md).

### 2c. Reading a capture back

`--rtcm-stdin` reads the stream from standard input instead of opening a
socket, so a capture goes back through the same framing, CRC, statistics
and report as the live run that recorded it. It works with `-d`, `-t`,
`-s` and `-S/--sky`.

```sh
ntrip-analyser -t 3600 --report --rtcm-stdin < 20260815131024_RFSEE01.rtcm3
```

Two things make that worth doing rather than merely possible:

- **The window is the capture's, not the replay's.** Six hours read from
  disk in a fraction of a second are judged over six hours, because the
  report is paced and stamped by the stream's own clock. The duration
  (`-t 3600`) bounds the *stream* analysed, so it means the same thing
  live and offline; a shorter capture simply ends at its end.
- **A capture keeps its verdict available.** Thresholds change, and a
  `.rtcm3` on disk can be re-judged years later against ones that did not
  exist when it was recorded.

**Availability reads `n/a`.** A file holds no arrival times and never
drops, so a reconnection count taken from one would be an invention.

**Where a replay and the live run disagree, the difference is the
network** — and that is information rather than error. In one 120-second
run against a healthy station the live report recorded 29 satellites at
its worst while the replay of that same session recorded 38, because the
delivery stalled for seven seconds and the analyser could not see what
had not yet arrived. The capture's epochs are consecutive: the station
never lost a satellite. A live run answers *what am I being given*; a
replay answers *what did the station send*.

Modes that cannot honour the flag reject it rather than ignore it —
`--check` among them, because tier 1 is a live acceptance test and a
capture cannot answer questions about delivery.

---

### 3. Usage Examples

- **Show mountpoint table:**
  ```sh
  ntrip-analyser -m
  ```

- **Decode all RTCM messages from the configured mountpoint:**
  ```sh
  ntrip-analyser -d
  ```

- **Decode only specific RTCM message types (e.g., 1005 and 1074):**
  ```sh
  ntrip-analyser -d 1005,1074
  ```

- **Analyze message types for 120 seconds:**
  ```sh
  ntrip-analyser -t 120
  ```

- **Count seen satellites for 120 seconds:**
  ```sh
  ntrip-analyser -s 120
  ```

- **Generate a template config file:**
  ```sh
  ntrip-analyser -i
  ```

- **Use a different config file:**
  ```sh
  ntrip-analyser -c myconfig.json -d
  ```

- **Verbose output (show config and action):**
  ```sh
  ntrip-analyser -v -d
  ```

---

## Using NTRIP analyser

### 1. Using -v (Verbose)


```sh
>ntrip-analyser.exe -t -v
=== NTRIP-Analyser Configuration ===
  Config file: config.json
  NTRIP_CASTER: somecaster.net
  NTRIP_PORT: 2101
  MOUNTPOINT: SOMEMOUNTPOINT
  USERNAME: SOMEUSERNAME
  PASSWORD: SOMEPASSWORD
  LATITUDE: 0.000
  LONGITUDE: 0.000
  Analysis time: 60
  Show mount table: no
  Decode stream: no
  Action: Analyze message types for 60 seconds
====================================
```


### 2. Test a NTRIP stream


```sh
>ntrip-analyser.exe -t
[INFO] Analyzing message types for 60 seconds...
1137 1077 1087 1097 1117 1127 1127 1137 1077 1087 1097 1117 1127 1127 1137 1077 1087 1097 
...
1127 1137 1077 1087 1097 1117 1127 1127 1137
[INFO] Message type analysis complete. Statistics:
+-------------+-------+---------------+---------------+---------------+
| MessageType | Count |  Min-DT (S)   |  Max-DT (S)   |  Avg-DT (S)   |
+-------------+-------+---------------+---------------+---------------+
| 1005        |     2 |        29.993 |        29.993 |        14.997 |
| 1077        |    60 |         0.985 |         1.017 |         0.983 |
| 1087        |    60 |         0.986 |         1.017 |         0.983 |
| 1097        |    60 |         0.986 |         1.016 |         0.983 |
| 1117        |    60 |         0.985 |         1.016 |         0.983 |
| 1127        |   120 |         0.001 |         0.988 |         0.496 |
| 1137        |    61 |         0.899 |         1.130 |         0.984 |
+-------------+-------+---------------+---------------+---------------+
```

## Acceptance testing

`--check` answers one question: **does this station meet the basic KPIs for RTK service?**  It
watches the stream for about ninety seconds and prints an eight-row verdict.

```
ntrip-analyser --check
```

```
1 Connected and producing    PASS   1695.40  Authenticated, connected, data flowing
2 RTCM 3.x format            PASS    543.00  CRC-valid RTCM 3.x frames decoded
3 Reference position (ARP)   PASS      1.00  1005/1006 received with non-zero coordinates
4 Observations flowing       PASS      5.00  Every constellation streaming at 0.5 Hz or faster
5 Satellites in view         PASS     46.00  At or above what this station advertises
6 Median C/N0                PASS     45.02  Antenna and LNA chain healthy
7 Frame integrity (CRC)      PASS      0.00  Fewer than 1 error per 1000 frames
8 Advertised versus actual   WARN      1.00  Streaming a constellation the sourcetable omits

== CAUTION ==  exit=6
```

KPI 8 compares what the sourcetable promises against what arrives: message types, their
rates, and the constellations in the NavSys field. Streaming something never advertised is
an observation, not a fault — it warns rather than fails, because the data is right and only
the metadata is wrong. Advertising a constellation that is not currently streamed is not even
that: QZSS is advertised across Europe and visible from none of it.

The verdict, not each row, is what must hold: it has to stay unchanged for sixty continuous
seconds before it is reported, so a station that flickers cannot pass by being briefly healthy
at the right moment.

The exit code makes it scriptable — an installer's sign-off, or a cron check:

| Exit | Meaning |
|---|---|
| 0 | STATION OK — all eight held for the full window |
| 6 | CAUTION — a marginal reading, or a soft KPI (satellite count, C/N0) failing |
| 1 | FAILED — a hard KPI failed: connectivity, format, ARP, multi-GNSS, or CRC |

`--check-vrs` adds the network-RTK layer for mountpoints that expect the rover to upload its
position.  It sends a GGA every ten seconds from the config's `LATITUDE`/`LONGITUDE` and adds five
assertions: the caster accepts the GGA, corrections start within ten seconds of it, the broadcast
reference position is near the rover, the stream holds at that cadence, and finally the **gate
test** — the uplink stops and the caster's reaction classifies the service:

```
V5  GGA gating   warn   91.00  Still streaming 90 s after GGA stopped: fixed base
== STATION OK ==  [service: not gated (fixed base?)]  exit=0
```

A physical base ignores GGA and keeps streaming, which is correct behaviour for what it is, so the
gate result is reported as a classification rather than a failure.

The same verdict engine runs on every frontend (`src/core/kpi.c`, `src/core/vrs_check.c`), so a
station cannot pass here and fail elsewhere.

## Notes

- The program will abort if `config.json` is missing or invalid.
- If you use `-i` and a `config.json` already exists, it will not overwrite the file.
- For decoding, if you specify a filter list, only those RTCM message types will be shown; all others will be indicated by a dot (`.`) in the output.
- *Always keep your credentials secure.*

---

## License

The code is under the [Apache License 2.0 with the Commons
Clause](https://github.com/pe1mew/NTRIP-Analyser/blob/main/LICENSE); this
documentation is under
[CC BY-NC 4.0](https://github.com/pe1mew/NTRIP-Analyser/blob/main/license.md).
[`docs/licences.md`](licences.md) sets out
the full position, including what the project depends on and what it
connects to.
