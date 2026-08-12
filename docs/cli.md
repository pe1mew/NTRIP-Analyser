# NTRIP-Analyser CLI manual

`ntrip-analyser` is the command-line application for connecting to NTRIP casters, retrieving mountpoint tables, and analyzing or decoding RTCM 3.x streams. It is designed for GNSS enthusiasts who need to inspect or debug NTRIP data streams.

---

## Getting Started

### 0. General operation description

The program will emulate a rover using preconfigured (static) coordinates. At connection the program will authenticate and either request the mountpoint list of a NTRIP stream. When a NTRIP stream is opened, the program will send an GGA NMEA sentense with the configured coordinates at a 1 second interval to the NTRIP caster.

### 1. Configuration File (`config.json`)

A ready-to-edit example ships as [`bin/exampleConfig.json`](../bin/exampleConfig.json)
(and beside the binaries in each release). It points at the Dutch
Kadaster open caster, including its ephemeris mountpoint for the sky
plot — replace the username and password with your own free Kadaster
registration. `--generate` writes a template too.

Before using the program, you must create a configuration file named `config.json` in the working directory. This file contains all necessary connection and authentication details as well as teh coordinates of the rover emulated. *The program will not work without it.* 

Generating a default `config.json` with dummy values is done by using the `-g` argument when running the program:

```sh
ntripanalysis.exe -g
```

**Example `config.json`:**
```json
{
    "NTRIP_CASTER": "your.caster.example.com",
    "NTRIP_PORT": 2101,
    "MOUNTPOINT": "MOUNTPOINT",
    "USERNAME": "your_username",
    "PASSWORD": "your_password",
    "LATITUDE": 0.0,
    "LONGITUDE": 0.0
}
```
Set the various parameters 
- **NTRIP_CASTER**: Hostname or IP address of the NTRIP caster.
- **NTRIP_PORT**: TCP port of the NTRIP caster (usually 2101).
- **MOUNTPOINT**: The mountpoint to request from the caster.
- **USERNAME**: Username for HTTP Basic Authentication.
- **PASSWORD**: Password for HTTP Basic Authentication.
- **LATITUDE**/**LONGITUDE**: latitude and longitude of the rover ocation being emulated.

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
| &nbsp; | --check      | &nbsp;           | Station acceptance test: seven KPIs, ~90 s (exit 0/6/1)                     |
| &nbsp; | --check-vrs  | &nbsp;           | As --check plus the network-RTK assertions and GGA gate test                |
| -g    | --generate   |                  | Geerate default config.json with dummy values and exit.                     |
|       | --latitude   | value            | Override latitude in config                                                 |
|       | --longitude  | value            | Override longitude in config                                                |
|       | --lat        | value            | Same as `--latitude`                                                        |
|       | --lon        | value            | Same as `--longitude`                                                       |
| -h    | --help       |                  | Help information                                                            |
| -i    | --info       |                  | Information about the program, repository, an author                        |

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
watches the stream for about ninety seconds and prints a seven-row verdict.

```
ntrip-analyser --check
```

```
1 Connected and producing    PASS   1665.39  Authenticated, connected, data flowing
2 RTCM 3.x format            PASS    633.00  CRC-valid RTCM 3.x frames decoded
3 Reference position (ARP)   PASS      1.00  1005/1006 received with non-zero coordinates
4 Multi-GNSS observations    PASS      3.00  GPS and Galileo MSM at 0.5 Hz or faster
5 Satellites in view         PASS     41.00  At or above the 25-SV threshold
6 Median C/N0                PASS     45.73  Antenna and LNA chain healthy
7 Frame integrity (CRC)      PASS      0.00  Fewer than 1 error per 1000 frames

== STATION OK ==  exit=0
```

Every KPI must hold PASS for sixty continuous seconds before the verdict is STATION OK, so a
station that flickers cannot pass by being briefly healthy at the right moment.

The exit code makes it scriptable — an installer's sign-off, or a cron check:

| Exit | Meaning |
|---|---|
| 0 | STATION OK — all seven held for the full window |
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

See the LICENSE file for details.
