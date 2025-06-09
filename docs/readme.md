# NTRIP-Analyser

NTRIP-Analyser is a command-line tool for connecting to NTRIP casters, retrieving mountpoint tables, and analyzing or decoding RTCM 3.x streams. It is designed for GNSS enthusiasts who need to inspect or debug NTRIP data streams.

---

## Getting Started

### 0. General operation description

The program will emulate a rover using preconfigured (static) coordinates. At connection the program will authenticate and either request the mountpoint list of a NTRIP stream. When a NTRIP stream is opened, the program will send an GGA NMEA sentense with the configured coordinates at a 1 second interval to the NTRIP caster.

### 1. Configuration File (`config.json`)

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
ntripanalyse [options]
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
  ntripanalyse -m
  ```

- **Decode all RTCM messages from the configured mountpoint:**
  ```sh
  ntripanalyse -d
  ```

- **Decode only specific RTCM message types (e.g., 1005 and 1074):**
  ```sh
  ntripanalyse -d 1005,1074
  ```

- **Analyze message types for 120 seconds:**
  ```sh
  ntripanalyse -t 120
  ```

- **Count seen satellites for 120 seconds:**
  ```sh
  ntripanalyse -s 120
  ```

- **Generate a template config file:**
  ```sh
  ntripanalyse -i
  ```

- **Use a different config file:**
  ```sh
  ntripanalyse -c myconfig.json -d
  ```

- **Verbose output (show config and action):**
  ```sh
  ntripanalyse -v -d
  ```
---

## Using NTRIP analyser

### 1. Using -v (Verbose)


```sh
>ntripanalyse.exe -t -v
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
>ntripanalyse.exe -t
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

## Notes

- The program will abort if `config.json` is missing or invalid.
- If you use `-i` and a `config.json` already exists, it will not overwrite the file.
- For decoding, if you specify a filter list, only those RTCM message types will be shown; all others will be indicated by a dot (`.`) in the output.
- *Always keep your credentials secure.*

---

## License

See the LICENSE file for details.
