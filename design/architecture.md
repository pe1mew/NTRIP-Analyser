# NTRIP-Analyser — Architecture for four frontends

## Context

NTRIP-Analyser is becoming four programs over one problem domain:

| Frontend | Status | Consumes the stream to… |
|---|---|---|
| **CLI** | Shipped | print decoded messages and statistics to a console |
| **Windows GUI** | Shipped | paint live tabs and chart windows |
| **Monitoring service** | Proposed | maintain counters and feed Munin |
| **Android app** | Planned (`android/design/sandBox.md`) | show pass/fail KPIs on a phone |

They differ only in *presentation*. Every one of them needs the same
underlying work: connect to a caster, reassemble RTCM frames, validate
CRC, decode, and accumulate statistics.

This document proposes the structure that lets the third and fourth
frontends be written without re-implementing that work a third and fourth
time — because the first two already implement it twice.

---

## 1. The problem today

### 1.1 The stream loop exists ten times

The RTCM framing loop — scan for the `0xD3` preamble, read the 10-bit
length, accumulate the frame, test CRC-24Q, dispatch — is written out at
**ten** separate sites:

| File | Sites | Lines |
|---|---|---|
| `src/main.c` | 2 | 444, 749 |
| `src/ntrip_handler.c` | 5 | 389, 623, 815, 1033, 1309 |
| `gui/gui_thread.c` | 3 | 578 (obs), 1057 (eph), 1165 (replay) |

The NTRIP request builder exists at **six** sites: five in
`src/ntrip_handler.c` and one in `gui/gui_thread.c`. Per-message-type
statistics accumulation exists in four files.

Adding the service the obvious way writes the eleventh framing loop and
the seventh request builder. Adding Android writes the twelfth and eighth.

### 1.2 The core API is procedure-shaped, not session-shaped

`src/ntrip_handler.h` exposes functions like:

```c
void start_ntrip_stream(const NTRIP_Config *config);
void analyze_message_types(const NTRIP_Config *config, int analysis_time);
void analyze_satellites_stream(const NTRIP_Config *config, int analysis_time);
```

Each opens a connection, runs a loop, prints to stdout, and returns when
done. There is no way to *consume* a stream — to receive frames and
decide yourself what to do with them. That is precisely what a GUI, a
daemon and a phone app all need, and it is why `gui/gui_thread.c` opens
its own socket and writes its own request rather than calling any of
these.

Output redirection exists (`rtcm_set_output_buffer()`, `RtcmStrBuf`) and
the GUI uses it, but it redirects *text*. A service needs numbers, not
captured console output.

### 1.3 The analysis lives in the GUI, where nothing else can reach it

The checks built most recently — CRC error rate, advertised-versus-
observed message types, station classification, reference-position
cross-check, C/N0 aggregation — are implemented in `gui/gui_events.c` and
`gui/gui_thread.c`.

Compare that against the Android plan's seven "Normal mode" KPIs
(`android/design/sandBox.md`): stream producing, format is RTCM 3.x, ARP
broadcast, multi-GNSS observations, SV count, median CNR, no CRC spike.
**Six of the seven are already implemented** — inside a Win32 GUI, in
code that cannot be compiled for Android or a Linux daemon.

### 1.4 Platform portability is already handled

One thing that is *not* a problem: `src/ntrip_handler.c` already brackets
its socket code with `#ifdef _WIN32` / POSIX branches, and the CLI builds
on Linux today. The networking layer does not need redesigning, only
relocating.

---

## 2. Target layering

```
┌──────────┬──────────────┬──────────────┬──────────────┐
│   CLI    │ Windows GUI  │   Service    │ Android(JNI) │   frontends
│  prints  │    paints    │ serves Munin │  shows KPIs  │
└────┬─────┴──────┬───────┴──────┬───────┴──────┬───────┘
     └────────────┴──────────────┴──────────────┘
                         │ typed events
┌────────────────────────┴──────────────────────────────┐
│  src/session/  — the stream session                   │
│  connect · frame · CRC · decode · statistics · health │
└────────────┬─────────────────────────┬────────────────┘
             │                         │
┌────────────┴───────────┐  ┌──────────┴────────────────┐
│  src/core/             │  │  src/net/                 │
│  parser, ephemeris,    │  │  NTRIP client, sourcetable│
│  orbits, geodesy       │  │  (already portable)       │
│  no I/O, no printf     │  │                           │
└────────────────────────┘  └───────────────────────────┘
```

### 2.1 Directory move

| From | To | Why |
|---|---|---|
| `src/rtcm3x_parser.*`, `sv_ephemeris.*`, `sv_orbit.*`, `rinex_nav.*` | `src/core/` | Pure computation. Must build for Android and a Linux daemon with no changes. |
| `src/ntrip_handler.*` (socket + sourcetable parts) | `src/net/` | Already portable; separating it makes the dependency explicit. |
| `src/main.c`, `src/cli_help.*` | `src/cli/` | CLI-only. Today Android and the service would have to drag `cli_help.c` into their build. |
| `src/sky_collect.*`, `src/sky_render.*` | `src/core/` | Computation + PNG rendering, already shared by CLI and GUI. |
| *(new)* | `src/session/` | The shared loop described below. |
| *(new)* | `service/` | `ntrip-monitord` and the Munin plugin. |

`src/config.*` and `src/nmea_parser.*` go to `src/core/`; both are pure
and already shared.

### 2.2 The rule that keeps this honest

**`src/core/` must not call `printf` and must not include a platform
header.** Anything that violates that will not compile for Android, and
the compiler is the only reliable enforcement. The current
`rtcm_printf` indirection is the seam to build on: it should become a
caller-supplied sink rather than a `printf` wrapper with a buffer mode.

---

## 3. The session layer

One implementation of the loop, driven by callbacks. The frontend owns
the thread; the session owns the protocol.

### 3.1 Sketch

```c
/* src/session/ntrip_session.h */

typedef enum {
    NS_EV_CONNECTING,      /* attempt started                      */
    NS_EV_HANDSHAKE,       /* NtripHandshake available             */
    NS_EV_STREAMING,       /* first valid frame decoded            */
    NS_EV_FRAME,           /* one RTCM frame, CRC valid            */
    NS_EV_FRAME_BAD,       /* CRC failure or malformed frame       */
    NS_EV_STATS,           /* periodic statistics snapshot         */
    NS_EV_DISCONNECTED,    /* with a reason code                   */
    NS_EV_LOG,             /* human-readable line, severity-tagged */
} NsEventType;

typedef struct {
    NsEventType type;
    double      t_rel;              /* seconds since session start */
    union {
        struct { const unsigned char *data; int len; int msg_type;
                 uint32_t epoch; } frame;
        struct { int reason; } bad;         /* CRC / malformed / short */
        const NtripHandshake *handshake;
        const NsStatsSnapshot *stats;
        struct { int severity; const char *text; } log;
    } u;
} NsEvent;

typedef void (*NsEventFn)(const NsEvent *ev, void *user);

typedef struct {
    NTRIP_Config config;
    double       stats_interval_s;  /* 0 = no periodic NS_EV_STATS */
    bool         send_gga;
    bool         auto_reconnect;
    int          reconnect_backoff_max_s;
} NsOptions;

NtripSession *ns_open(const NsOptions *opt, NsEventFn cb, void *user);
int           ns_pump(NtripSession *s, int timeout_ms);  /* one iteration */
void          ns_stop(NtripSession *s);
void          ns_close(NtripSession *s);

/* Offline: same event stream from a capture file, so replay and live
 * analysis cannot diverge. */
NtripSession *ns_open_file(const char *path, const NsOptions *opt,
                           NsEventFn cb, void *user);
```

### 3.2 Why `ns_pump()` rather than a blocking `ns_run()`

Each frontend has a different idea of who owns the thread:

- the **GUI** runs a worker thread and posts Windows messages;
- the **CLI** blocks in `main()`;
- the **service** multiplexes several sessions, ideally in one thread;
- **Android** runs a background service and marshals to the UI thread.

A pump the caller drives suits all four. A convenience `ns_run()` that
loops `ns_pump()` until stopped covers the CLI case in three lines.

### 3.3 What moves into it

From `gui/gui_thread.c`, which has the most complete implementation:
format detection, epoch-aware statistics, CRC and malformed-frame
counting, framing re-sync counting, GGA uplink, capture-to-file.

From `gui/gui_events.c`: station classification, advertised-versus-
observed comparison, the position cross-check, C/N0 aggregation, and the
session-history sampler. These are analysis, not presentation, and all
four frontends want them.

What stays in the GUI: everything that touches an `HWND`.

---

## 4. The statistics snapshot

**This is the highest-leverage piece of the whole proposal**, because
three separate consumers need exactly the same thing:

- the **service** publishes it to Munin;
- the **Android** app renders it as the seven KPIs;
- backlog item **4.4** exports it as CSV / JSON.

Define it once, in `src/core/`, with a serialiser next to it.

```c
typedef struct {
    /* identity */
    char     mountpoint[64];
    char     caster[128];
    double   t_start_unix;
    double   uptime_s;

    /* connection */
    int      ntrip_version;         /* 1 or 2                        */
    int      http_status;
    char     caster_software[96];
    int      reconnects;

    /* volume and integrity */
    uint64_t bytes_total;
    double   bytes_per_s;
    uint64_t frames_ok;
    uint64_t frames_crc_error;
    uint64_t frames_malformed;
    uint64_t framing_resyncs;
    double   crc_error_rate;        /* share of frames checked       */

    /* per message type */
    struct {
        int      msg_type;
        uint64_t count;
        uint64_t epochs;
        double   min_dt, max_dt, avg_dt;
        float    advertised_interval;   /* 0 = not advertised, -1 = no rate */
        int      verdict;               /* ok / missing / rate / extra    */
    } types[NS_MAX_TYPES];
    int n_types;

    /* satellites and signal */
    struct {
        int   gnss_id;
        int   sats_tracked;
        float cnr_mean, cnr_median, cnr_min, cnr_max;
    } gnss[NS_MAX_GNSS];
    int n_gnss;
    int   sats_total;
    float cnr_mean_all;

    /* reference station */
    bool   arp_valid;
    double arp_lat, arp_lon, arp_alt;
    double arp_drift_m;             /* from the first ARP this session */
    int    arp_moves;
    int    station_type;            /* unknown / fixed / VRS           */
    bool   sourcetable_pos_valid;
    double sourcetable_offset_m;

    /* timeliness */
    double latency_s;               /* newest MSM epoch vs system clock */
} NsStatsSnapshot;

int ns_stats_to_json(const NsStatsSnapshot *s, char *out, size_t cap);
int ns_stats_to_csv_row(const NsStatsSnapshot *s, char *out, size_t cap);
```

**Versioning.** Include a `schema_version` integer from day one. Munin
graphs, phone builds and archived CSV files will outlive any given
release, and a silent field change breaks them all.

---

## 5. The monitoring service

### 5.1 Why a plain Munin plugin will not work

Munin invokes a plugin every five minutes and expects it to print values
and exit within seconds. An NTRIP monitor cannot work that way:

- rates (bytes/s, frames/s) need a session that persists across samples;
- **dropouts are the thing you most want to catch**, and a plugin that
  connects for two seconds every five minutes cannot see them;
- reconnecting every interval hammers the caster and distorts what it is
  measuring.

### 5.2 Proposed shape

```
ntrip-monitord ──holds N persistent sessions──┐
      │                                       │
      │ every N seconds, writes atomically    │
      ▼                                       │
/var/lib/ntrip-monitor/<mountpoint>.json  ◄───┘
      │
      │ read
      ▼
munin plugin (tiny) ── prints field.value ──► munin-node
```

The daemon writes a snapshot per mountpoint (write to a temporary file,
then `rename()`, so a reader never sees a half-written file). The plugin
reads and prints.

**Why a file rather than a socket:** no IPC protocol to design, test or
version; the plugin is a few lines and needs no library; and the state is
inspectable with `cat` when something is wrong at 2 a.m. The snapshot is
the same JSON from §4, so the file doubles as the export format.

The plugin must report staleness: if the file's timestamp is older than
about twice the write interval, the daemon is not running and the plugin
should emit `U` (undefined) rather than the last known values, which
would otherwise draw a flat line that looks like a healthy stream.

### 5.3 Munin graphs

One plugin per mountpoint using Munin's **multigraph** protocol:

| Graph | Fields | Type |
|---|---|---|
| `throughput` | bytes/s | GAUGE |
| `msgrate` | one per RTCM type | GAUGE |
| `integrity` | crc_errors, malformed, resyncs | DERIVE |
| `satellites` | one per constellation | GAUGE |
| `cnr` | mean, median, min, max | GAUGE |
| `latency` | epoch vs system clock | GAUGE |
| `availability` | uptime %, reconnects | GAUGE / DERIVE |

Munin thresholds map directly onto the Android KPIs in §1.3 — the same
numbers, a different renderer. `warning`/`critical` levels give alerting
for free.

### 5.4 Deployment

Ship a systemd unit and a config file listing mountpoints. The daemon
should drop privileges, retry with backoff rather than exiting on a dead
caster, and log to stderr for journald.

---

## 6. Android

Android consumes the same session layer through JNI. `src/core/` and
`src/session/` compile with the NDK provided §2.2 is respected. The seven
Normal-mode KPIs become threshold checks over `NsStatsSnapshot` — the
same evaluation the Munin plugin does.

The one genuinely Android-specific piece is the GGA position source
(phone GNSS via Fused Location), which belongs in the app, not the
session layer. The session already accepts a configured position.

---

## 7. Versioning

Before this restructuring the repository carried three disagreeing
versions: `src/cli_help.h` said `0.3.0-dev`, `gui/resource.rc` said
`0.1.0.0`, and the only git tag was `0.1`. With four artefacts that
becomes untenable, so the scheme below starts at **2.0.0**.

### 7.1 One product version for all artefacts

`src/core/version.h` is the single source of truth. The CLI, the Windows
GUI, the monitoring service and the Android app all report the same
number, because they are built from one commit — so "2.0.0" identifies
the exact source of every binary in a release.

The alternative, versioning each artefact independently, requires a
compatibility matrix saying which CLI works with which service. Nobody
maintains those, and they answer a question that does not arise when
everything ships together.

```c
#define NTRIP_VERSION_MAJOR   2
#define NTRIP_VERSION_MINOR   0
#define NTRIP_VERSION_PATCH   0
#define NTRIP_VERSION_STRING  "2.0.0"
#define NTRIP_VERSION_RC      2,0,0,0      /* Win32 VERSIONINFO   */
#define NTRIP_ANDROID_VERSION_CODE 20000   /* MAJOR*10000+MINOR*100+PATCH */
```

The header is deliberately free of `#include`s and types so `windres` can
consume it when compiling `gui/resource.rc`, and so build tooling (CMake,
Gradle) can parse it textually. **Do not add an include to it.**

Note this means `windres` needs `-Isrc`; `build-gui.bat` and the VS Code
resource task pass it.

### 7.2 What each component means

| Bump | When |
|---|---|
| **MAJOR** | A user-visible contract breaks: a CLI option removed or given a new meaning, an incompatible config schema, a field removed or repurposed in the statistics snapshot, a renamed Munin field. |
| **MINOR** | New capability, backward compatible. |
| **PATCH** | Fixes only, no new capability. |

Release tags are `v2.0.0`. The existing `0.1` tag predates the scheme and
is left alone.

### 7.3 Contracts are versioned separately

Binaries ship together; **contracts outlive them**. A Munin graph, an
installed phone build or a CSV file archived last year may be read by
software of a quite different vintage, so the things that cross those
boundaries carry their own integer version, checked independently of the
product version:

| Contract | Version constant | Lives in |
|---|---|---|
| Statistics snapshot (JSON / CSV) | `NS_STATS_SCHEMA_VERSION` | `src/core/ns_stats.h` |
| `config.json` layout | `NTRIP_CONFIG_SCHEMA_VERSION` | `src/core/version.h` |
| Munin field names | *(the names themselves)* | `service/` |

These are plain integers, not semver: a consumer only needs to know
whether it understands the format. Adding a field at the end is backward
compatible and does not require a bump; removing or repurposing one does,
and also forces a MAJOR product bump.

Munin has no version negotiation at all — a renamed field silently
becomes a new graph and the history is orphaned. Treat Munin field names
as frozen once published.

### 7.4 Identifying a build

Each artefact reports `<name> <version>`, and should also carry the git
short SHA once the build system can supply it, so a report from a
development build is traceable to a commit rather than to a version
number shared by everything between two releases.

```
$ ntripanalyse --version
ntrip-analyser 2.0.0
```

## 8. Build system

Four targets across two platforms outgrows `build-gui.bat` and
hand-written `gcc` lines. `CMakeLists.txt` exists but covers only the
CLI.

Proposed: one CMake project with `ntrip_core` and `ntrip_session` as
static libraries, and `ntripanalyse`, `ntrip-analyser-gui` and
`ntrip-monitord` as executables that link them. The GUI target guards on
`WIN32`; the service target guards on `UNIX`. Android builds the two
libraries through the NDK's own CMake support, which is why this
matters more than convenience.

Keep `build-gui.bat` working during migration — see §9.

---

## 9. Migration path

**All of this happens on a `refactoring` branch; `main` stays stable and
releasable throughout.**

```sh
git switch -c refactoring
```

Merge back only when the CLI and GUI have been verified to behave as they
do on `main` — the point of the exercise is that they should be
indistinguishable to a user. Two consequences worth planning for:

- **`main` keeps moving.** Feature work from `design/todo.md` may land
  there meanwhile. Rebase or merge `main` into `refactoring` regularly
  rather than letting them diverge for weeks; step 6 (the directory move)
  turns every stale branch into a conflict.
- **Steps 1–3 are pure additions** and could land on `main` directly if
  you would rather have the service sooner. Only steps 4–6 genuinely need
  the branch. Doing 1–3 on `main` also shrinks the eventual merge.

The ordering below is deliberate: **the new consumer validates the
extraction before any working code is touched.**

| Step | Work | Risk |
|---|---|---|
| **1** | ~~Define `NsStatsSnapshot` and its JSON serialiser in `src/core/`.~~ **Done** — `src/core/ns_stats.{h,c}`, plus `src/core/version.h` unifying the version at 2.0.0. | None — pure addition |
| **2** | ~~Add `src/session/`.~~ **Done** — `src/session/ntrip_session.{h,c}` with `src/net/ntrip_proto.{h,c}` beneath it. Validated by replaying a capture: the session reproduces the independently measured statistics exactly (206 frames, per-type counts, CRC accounting). Existing code untouched. | None — nothing calls it |
| **3** | Build `ntrip-monitord` and the Munin plugin on it. First real consumer. | Low — greenfield |
| **4** | Move the GUI obs worker onto the session layer; `gui_thread.c` becomes an adapter posting `WM_APP_*` from `NsEvent`. | Medium — behaviour must be compared before and after |
| **5** | Move the CLI onto it, collapsing the five `ntrip_handler` entry points into one. | Medium |
| **6** | Directory move (§2.1) and CMake (§7), once callers are stable. | Low but noisy in `git log` |
| **7** | Android JNI wrapper. | Independent of the rest |

Steps 1–3 add nothing to the existing binaries and cannot regress them.
Step 4 is where care is needed: the GUI is the most-tested consumer, and
the session layer is being extracted *from* it, so any behavioural
difference is a bug in the extraction.

**Do not do the directory move first.** It touches every include in the
project and would make the real changes unreviewable in the same diff.

---

## 10. Decisions still open

1. **Threading inside the daemon.** One thread per mountpoint is simplest;
   a `select()` loop over all sessions scales better. Only matters beyond
   roughly a dozen mountpoints.
2. **Retention.** Munin keeps its own RRDs, so the daemon probably needs
   no history of its own — but the GUI's session history and item 4.4's
   export suggest a shared ring-buffer type would be reused. Decide
   whether history belongs in the session layer or above it.
3. **Sourcetable polling.** The advertised-versus-observed check needs the
   sourcetable. Re-fetching periodically would catch a caster changing its
   metadata mid-session, at the cost of extra requests.
4. **Config format.** The CLI and GUI share `config.json` for one
   connection. The daemon needs a list. Either extend the schema with an
   optional array or give the daemon its own file.
5. **Whether the CLI keeps its current output verbatim.** Step 5 is far
   easier if small formatting differences are acceptable; scripts parsing
   CLI output would disagree.

---

## 11. What this buys

- One framing loop instead of ten; one request builder instead of six.
- The analysis already written for the GUI becomes available to the
  service and the phone without being written again.
- A fault found in decoding is fixed once — as with the MSM7 C/N0
  bit-layout bug, which had to be fixed in two functions and could
  equally have been missed in a third copy.
- New frontends cost a renderer, not a protocol implementation.

## Reference

- [`design/todo.md`](todo.md) — feature backlog
- [`docs/gui.md`](../docs/gui.md) — current GUI structure
- [`android/design/sandBox.md`](../android/design/sandBox.md) — Android KPI plan
- [Munin plugin protocol](https://guide.munin-monitoring.org/en/latest/plugin/protocol.html)
