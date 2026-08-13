# Android design review — sandBox.md against the 3.3.0 codebase

**Reviewed 2026-08-11.** [sandBox.md](sandBox.md) was written on 2026-05-31,
against the pre-refactoring tree. Every claim it makes about what is
portable was re-verified against `main` at 3.3.0. The headline: **the
design is more buildable now than when it was written** — most of what it
budgeted as "a 60-second aggregator to write" already exists in the
session layer, proven in production by the monitoring daemon.

Decision slots below are marked **DECIDE**. Nothing here is committed to
until they are filled in.

---

## 1. The seven Normal-mode KPIs against the snapshot

The design assumed Android would compile `rtcm3x_parser.c` +
`ntrip_handler.c` and build its own aggregation. Since then the session
layer (`ntrip_core` + `ntrip_session`, CMake-built, no platform headers)
produces a per-second `NsStatsSnapshot` that already carries the raw
material for **all seven**:

| # | KPI | Snapshot source | State |
|---|---|---|---|
| 1 | Connected and producing | `connected`, `bytes_per_s` | ready |
| 2 | Format is RTCM 3.x | `frames_ok > 0` within the window | ready |
| 3 | ARP broadcast | `arp_valid`, `arp_lat/lon/alt` | ready |
| 4 | Multi-GNSS MSM at rate | `types[]` per-type epochs + `avg_dt`; `gnss[]` | ready |
| 5 | SV count ≥ 25 | `sats_total` (5 s in-view window) | ready |
| 6 | Median L1 CNR ≥ 40 | `gnss[].cnr_median` — **all-band**, not L1-only | near (see D4) |
| 7 | CRC error rate | `crc_error_rate`, `frames_crc_error` | ready |

What remains for Normal mode is therefore **thresholding and verdict
only** — no parsing, no aggregation, no timers.

## 2. Extended KPIs — most now exist, one is new since the design

| Extended KPI (sandBox) | Now | Notes |
|---|---|---|
| Latency | `latency_s` in the snapshot | ready |
| Sourcetable conformance | `advertised_known/count`, `types_missing/offrate/extra` | ready — the whole 1.1 feature |
| Per-GNSS SV thresholds | `gnss[].sats_tracked` | ready |
| Per-band CNR breakdown | `get_sv_per_band_cnr()` + `msm_signal_label()` | core call, per SV |
| Sky-sector coverage | `sky_collect` / `sky_render` (RGB buffer + own PNG encoder) | as designed |
| Ephemeris age per GNSS | `sv_eph_get()` per PRN; no "newest per GNSS" query | small core addition |
| Per-PRN tracking continuity | `sv_track` has `last_seen` per PRN; no gap history | small core addition |
| ARP plausibility vs phone GNSS | phone-side only | Android-new, trivial (Haversine exists) |
| **Ionosphere (ROTI)** — *not in the design* | `iono_verdict/roti_median/...` in the snapshot | free — see **D5** |

## 3. The network-RTK / VRS test against the desktop implementation

The design's VRS test was written before the desktop grew one. Since
then the GUI shipped `ClassifyStation()` (keyword + behavioural ARP-near-
GGA test), the VRS Monitor with position-shift buttons, GGA auto-send
toggling, and ARP hand-over history — and the session layer carries
`send_gga` / `gga_interval_s` / `ns_send_gga()` for caller-driven cadence.

Mapping the design's VRS KPIs:

| VRS KPI (sandBox) | Desktop equivalent today |
|---|---|
| GGA accepted (no disconnect ≤ 5 s) | observable via session events; not asserted anywhere |
| RTCM starts ≤ 10 s after GGA | same |
| Virtual ARP near rover | `ClassifyStation()` behavioural test (150 m band) |
| Station ID sane / changes on shift | ARP hand-over history (`vrsArpHist*`); ID check open |
| Keep-alive holds | GGA auto-send + reconnect machinery |
| Drop on GGA stop | the GUI's "GGA-gated?" manual test, not asserted |
| Position-shift response | VRS Monitor shift buttons, manual |

So the *mechanics* all exist; what does not exist anywhere is the
**assertion layer** that turns them into pass/fail. That is the same gap
backlog item **2.4** records for the desktop (hand-over logging, GGA
uplink compliance, baseline sanity). Building the assertions once in
shared code would close 2.4 and power the Android test — see **D2**.

## 4. What the design gets right unchanged

- Two modes, Normal-first, ~60 s to a verdict — matches the KPI plan the
  architecture referenced all along.
- The staged delivery (Normal → Advanced+VRS → persistence/alerting).
- `sky_render` to `Bitmap.createBitmap()`; `nmea_parser` for GGA;
  `rinex_nav` for offline preload. All verified still true, and now they
  arrive via two CMake static libraries instead of hand-picked files.

## 5. Corrections to the design

- **"ntrip_handler.c + a thin JNI wrapper of the C parsing core"** — the
  unit to compile is `ntrip_core` + `ntrip_session` (the NDK builds
  through CMake, which already builds both). `ntrip_handler`'s stream
  loop no longer exists; the session layer replaced it.
- **"a 60-second aggregator"** — already exists (`stats_refresh`, 1 Hz).
- The design predates: the ionospheric monitor, `sv_track`, the
  advertised-vs-observed machinery in the *session* (not just the GUI),
  auto-reconnect as a session option, and `ns_open_stream()` for replay
  of a captured file on the phone.

---

## 6. Open decisions — **DECIDE** before Phase 1 starts

### D1 — Where does the KPI verdict engine live?

The seven thresholds (and the extended set) can be applied in Kotlin, or
in a new core module (`src/core/kpi.c`) that takes an `NsStatsSnapshot`
and returns per-KPI verdicts + one overall verdict.

- **Kotlin**: fastest to a phone screen; thresholds drift from any future
  desktop/CLI equivalent.
- **Core C (recommended)**: one verdict engine for Android, a future CLI
  `--check` (station acceptance test from a script — the same seven KPIs
  are exactly what an installer's sign-off needs headless), and the
  daemon. Matches how every shared number in this project ended up.

**Impact**: plan order (kpi.c becomes Phase 0), and whether the CLI gains
a `--check` mode almost for free.

### D2 — Where do the VRS assertions live?

Same choice for the VRS-specific KPI set. Shared code closes backlog 2.4
for the desktop at the same time; Kotlin-only leaves 2.4 open and forks
the logic.

**Impact**: whether Phase 2 is "port a screen" or "design a test engine
twice".

### D3 — JNI surface: JSON bridge or typed bindings?

- **JSON bridge (recommended for Phase 1)**: the phone polls
  `ns_stats_to_json()` once per second across JNI as a single string —
  the serialiser the daemon already publishes with. One JNI function,
  parsing with kotlinx.serialization, versioned by `schema_version`.
- **Typed bindings**: per-field JNI accessors or a flatbuffer — faster,
  far more JNI code to maintain, and per-SV views (sky plot, per-band
  CNR) need it eventually for Advanced mode.

**Impact**: Phase 1 effort (days vs weeks) and where the Advanced-mode
cut lands.

### D4 — KPI 6: "median L1 CNR" or "median CNR"?

The snapshot's `cnr_median` spans all bands. L1-only needs a small
session/core addition (the per-band data exists). All-band reads ~1 dB
different in practice on multi-band receivers.

**Impact**: one small core change, or one line of doc stating the KPI as
measured.

### D5 — Is the ionospheric monitor in the Android scope?

Not in the May design (it did not exist). The verdict is in the snapshot,
so a Normal-mode eighth row or Advanced-mode panel is nearly free —
but it is an *environment* fact, not a *station* fact, so it arguably
does not belong in a station pass/fail.

### D6 — The VRS test's map view

Desktop item 4.2 (map widget) was **dropped** as having no use case for
fixed-base assessment. The Android VRS screen as designed centres on a
map (rover + ARP + distance line). The rationale differs — the phone
moves and the VRS test is about spatial relationship — but the decision
should be made consciously against 4.2's reasoning, not inherited.

**Settled for the position picker, 2026-08-13** — and only for that.
No map SDK is embedded in either edition: picking a position hands off
to a map app or the browser and takes the coordinates back through the
clipboard, so no tile request is ever made by this process. The
reasoning, including why osmdroid's Apache-2.0 licence was not the
deciding factor, is in `android/design/editions.md`. Whether the VRS
screen should *display* a map is still open and still has to answer to
4.2.

### D7 — Phase 3 scope (persistence, alerting, favourites)

Unexamined here; defer until Phase 1/2 are real.

---

## 8. Decisions — taken 2026-08-11

| # | Decision |
|---|---|
| D1+D2 | **Shared core C.** A `src/core/kpi.c` verdict engine over the snapshot, and the VRS assertions in shared code. This adds a Phase 0 before the app, closes desktop backlog 2.4, and makes a CLI `--check` acceptance-test mode nearly free. |
| D3 | **JSON bridge first.** One JNI call polling `ns_stats_to_json()` at 1 Hz, parsed app-side, versioned by `schema_version`. Typed bindings only when Advanced mode needs per-SV data. |
| D4 | **All-band median.** KPI 6 is worded "median C/N0 ≥ 40 dB-Hz", measured as the snapshot already measures it. |
| D5 | **Ionosphere out of Android scope** for the first cut — an environment fact, not a station fact. |
| D6 | **No map view** in the VRS test screen; the 4.2 reasoning holds. Rover-to-ARP distance is shown as a number, which is what the assertion uses anyway. |
| D7 | Deferred. |

**Resulting plan:**

- **Phase 0** — `src/core/kpi.c`: the seven-KPI verdict engine over
  `NsStatsSnapshot`, plus the VRS assertion set (closing backlog 2.4).
  Testable on the desktop against live streams and captures before any
  Android code exists; optionally surfaced as CLI `--check`.
- **Phase 1** — Android app, Normal mode: NDK/CMake build of
  `ntrip_core` + `ntrip_session` + `kpi`, a foreground service running
  `ns_pump()`, the JSON bridge, one Compose screen with the verdict
  badge and seven rows.
- **Phase 2** — Advanced mode + VRS test screen (no map), sky bitmap via
  `sky_render`, per-message stats table.
- **Phase 3** — deferred.

---

## 7. Cost picture after this review

- **Phase 1 (Normal mode)**: session layer + snapshot + (D1) verdict
  engine + one Compose screen + a foreground service running `ns_pump()`.
  The C side is essentially done; the new work is the app scaffold, JNI
  bridge (D3), and thresholds.
- **Phase 2 (Advanced + VRS)**: sky bitmap and stats table are ports;
  the VRS assertion engine (D2) is the one genuinely new component.
- The riskiest unknowns are Android-side, not C-side: foreground-service
  lifecycle, battery policy, and the toolchain (NDK + CMake — the build
  system is ready for it; `ntrip_core` compiles with no platform
  headers by design).
