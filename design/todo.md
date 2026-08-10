# NTRIP-Analyser — Feature Backlog

This is the single source of truth for planned and considered features. Every item carries an
**Inspiration** tag recording where the idea came from, so the provenance survives after the
discussion that produced it is forgotten.

**Reconciled against `main` on 2026-08-10.** The first draft of this list was written against
commit `e7a8757` and had drifted badly: two items were already shipped, and every source line
reference pointed at the wrong code because `src/rtcm3x_parser.c` had grown from roughly 1000 to
2100+ lines. All references below were re-verified against `main`. When editing this file, re-check
line numbers — prefer citing a function name alongside the line so a stale number is self-evident.

## Status markers

| Marker | Meaning |
|---|---|
| **Open** | Not started. |
| **Partial** | Some of the machinery exists; the item names what is left. |
| **Shipped** | Implemented on `main`. Kept for the record in §0, with its original number. |

Item numbers are stable and are never reused or renumbered, so shipped items keep their IDs.

## Inspiration tags

| Tag | Source |
|---|---|
| `[ESP32]` | [aryesil/RTK-BASE-ESP32](https://github.com/aryesil/RTK-BASE-ESP32) — an ESP32-based RTK base station. It is a correction *producer* while this project is a *consumer*, so its configuration features do not transfer, but its monitoring and analysis layer maps directly onto our problem domain. |
| `[design.md]` | Migrated from the former "Future Enhancements" list in `gui/design.md` §12, or originated in this project. |

---

## 0. Shipped

Retained so the list does not re-propose work that is already done.

### 2.2 Raw stream capture and offline replay — **Shipped**

Capture writes raw frames to disk from the File menu (`gui/gui_thread.c:547`); the replay worker
reads a `.rtcm3` capture and feeds it through the normal decode path
(`gui/gui_thread.c:1031`, menu item `IDM_FILE_RTCM_REPLAY` in `gui/resource.h:24`).

### 1.3 CRC-24Q error rate as a first-class metric — **Shipped**

Counters live in `AppState` (`healthFramesOk`, `healthCrcErrors`, `healthMalformed`,
`healthResyncs`), incremented from both the live worker and the replay worker in
`gui/gui_thread.c`, and reset per session alongside `streamBytes`.

Presented in two places. The status bar's byte counter (part 2) gains a `· N CRC (x.xx%)`
suffix **only when the count is non-zero**, so a clean link looks exactly as it did before. The
full breakdown — frames checked, frames OK, CRC errors, error rate, malformed frames, framing
re-syncs — is on a new **Stream Health** tab (`RefreshStreamHealth()` in `gui/gui_events.c`).

No parser change was needed: `analyze_rtcm_message()` returns `0` *only* for a complete frame
whose CRC failed, and `-1` for a bad preamble or runt frame, so the call site can distinguish the
two faults. Note this contract is load-bearing — a future change that returns `0` for another
reason would silently corrupt these counts.

Deliberately **not** per-message-type: the type field is read before the CRC is validated, so on a
corrupt frame the type is untrustworthy.

### 3.2 Ephemeris decoding — 1019 / 1020 / 1042 / 1044 / 1045 / 1046 — **Shipped**

Decoders live in `src/rtcm3x_parser.c` (`decode_rtcm_1020:1364`, `decode_rtcm_1044:1472`,
`decode_rtcm_1042:1750`, `decode_rtcm_1046:1871`), backed by `src/sv_ephemeris.c`,
`src/sv_orbit.c` and a RINEX navigation loader in `src/rinex_nav.c`.

This was filed as a gateway item, and it opened the gate as predicted: the polar sky plot
(`gui/gui_sky_window.c`, `src/sky_render.c`, `src/sky_collect.c`) and per-SV detail
(`gui/gui_sv_detail.c`) both followed from having orbits. A second, optional NTRIP connection for
ephemeris-only casters was added alongside it (`WorkerOpenEphStream`, `gui/gui_state.h:443`) —
see item 4.3.

---

## 1. Tier 1 — High value, low effort

### 1.1 Diff advertised vs. observed RTCM message types — **Open**

Compare the message types a mountpoint advertises in its sourcetable STR line against the types
actually observed on the stream, and report the difference.

- **Why** — Public casters routinely advertise `1004,1005,1012` while streaming MSM7, or advertise
  messages that never arrive. This is probably the most common real-world base misconfiguration,
  and it is invisible unless the two lists are placed side by side.
- **Inspiration** — `[ESP32]` `src/network/DataOutput.cpp`, which generates a sourcetable STR line
  declaring its supported RTCM message types.
- **Notes** — Both halves are already in memory. The sourcetable Format and Details strings are now
  captured into `AppState` (`sourceFormat`, `sourceDetails` at `gui/gui_state.h:304-305`), and the
  Msg Stats tab counts observed types. Only the comparison is missing.

### 1.2 Cross-check sourcetable position against broadcast ARP — **Open**

Compare the mountpoint's sourcetable latitude/longitude against the Antenna Reference Point
broadcast in RTCM 1005/1006, and monitor 1005 over time for mid-session position jumps.

- **Why** — A mismatch catches copy-paste errors made when the base was registered with the caster.
  Watching 1005 across a session catches a base that silently changes position, a fault that is
  near-impossible to spot by eye and that degrades every rover downstream.
- **Inspiration** — `[ESP32]` `src/gnss/GNSS_Processor.cpp`, which treats 1005 as authoritative and
  flags ARP changes above a 0.2 mm threshold.
- **Notes** — `decode_rtcm_1005()` (`src/rtcm3x_parser.c:576`) already decodes the ECEF position and
  computes distance and heading to the configured rover position. `gui/gui_parsers.c` has a
  Haversine implementation for the mountpoint distance column.
- **Depends on item 2.4** — this check assumes a single fixed base. On a VRS or nearest-base service
  the ARP legitimately follows the rover, so both halves of this item produce false alarms until the
  stream can be classified. Do 2.4 first.

### 1.3 Surface CRC-24Q error rate as a first-class metric — **Shipped**, see §0

### 1.4 Per-satellite C/N0 visualisation — **Shipped**

Two views in a floating **Signal Quality** window (View → Signal Quality,
`gui/gui_signal_window.c`), following the same window pattern as the Sky Plot and VRS Monitor:

- **Signal bars** — one bar per tracked SV, current-epoch C/N0, coloured by constellation using
  the same hues as the sky plot so a satellite reads identically in both windows. Uses data
  already in `SkySat`; no new storage.
- **C/N0 vs elevation scatter** — every accumulated track sample, with a per-constellation mean
  overlaid in 5° elevation bins. This is the view the item was filed for: a clean install rises
  monotonically from horizon to zenith, while obstructions and multipath show as a dip at
  particular elevations. Bins with fewer than 5 samples are skipped rather than drawn, so the mean
  line never implies more confidence than the data supports.

The scatter needed per-SV C/N0 history, which `SkyTrackBuffer` did not carry. Rather than grow it,
`SkyTrackPoint.ts` was repacked from an absolute `double` to a `float ts_rel` offset from
`SkyPlotState.sessionT0`, freeing room for `float cnr_dbhz` **at no memory cost** — the struct
stays 16 bytes and the buffer stays 11.3 MB. A float resolves a 24-hour offset to better than
0.01 s, far finer than the 60 s sampling, and every consumer compares differences rather than
absolute times. A compile-time assertion in `gui_state.h` now pins the 16-byte size, since a future
widening would silently cost 5.6 MB.

Note the earlier memory figures in that header were wrong (24 B / 17.7 MB); the struct was 16 B /
11.3 MB before this change too. Corrected.

**Building this surfaced a parser bug that had made every C/N0 reading in the application wrong.**
`msm7_extract_cnr()` and `msm7_update_per_band_cnr()` both assumed MSM signal data is laid out as
contiguous 80-bit per-cell blocks, and read C/N0 at a fixed offset within each. MSM actually stores
each field as its own array across all cells — every fine pseudorange, then every fine phase range,
then every lock time, then every half-cycle flag, then every C/N0 — as the full MSM7 decoder in the
same file correctly does. The extractors were therefore reading pseudorange and phase bits as C/N0.
Measured on a 204-frame MSM7 capture, values spanned 0.75–63.94 dB-Hz and peaked in the 60–65
bucket; after the fix they span 35–55 and peak at 45–50, which is the expected distribution. This
also silently corrupted the sky plot's C/N0 shading and the per-SV detail window, both now correct.

The scatter is fed by its own accumulator (`SigCnrState`) on every MSM epoch rather than from the
trail buffer — the 60 s trail sampling yields one point per satellite per minute, far too sparse to
read as a cloud.

### 1.4b Per-constellation C/N0 columns in the Satellites tab — **Open**

The Satellites ListView still shows only `GNSS` / `Sats Seen` / `Satellites`
(`gui/gui_layout.c:338-340`). Min/mean/max C/N0 columns there would give a sortable tabular view
alongside the charts, and `SatStatsSummary` (`src/ntrip_handler.h:103`) would need a C/N0 field —
it currently carries only `sat_seen[]` flags.

- **Why** — C/N0 is the primary indicator of antenna and siting quality at the base. A base with
  good message rates but poor C/N0 is quietly delivering bad corrections.
- **Inspiration** — `[ESP32]` GNSS tab signal-distribution bar charts.
- **Already built** — this item originally argued that CNR was parsed and thrown away. That is no
  longer true: per-SV CNR is now retained (`cnr_dbhz`, "best CNR this epoch",
  `gui/gui_state.h:144` and `:214`) and drives CNR shading in the sky plot. Extraction happens at
  `src/rtcm3x_parser.c:967` (MSM7) and `:2304`, `:2389`, `:2462` (the 6-bit MSM variants).
- **Remaining** — the Satellites tab still shows only three columns, `GNSS` / `Sats Seen` /
  `Satellites` (`gui/gui_layout.c:338-340`), with no signal quality at all. The work is now a
  display change reusing the existing per-SV CNR rather than new parsing: add min/mean/max C/N0 per
  constellation, and ideally a per-SV breakdown consistent with the sky plot's shading.

---

## 2. Tier 2 — High value, moderate effort

### 2.1 Session history ring buffer with time-series charts — **Open**

Sample key metrics at a fixed interval into a ring buffer, and chart them over the session:
message rate, throughput, CRC error rate, satellite count, mean C/N0, position delta.

- **Why** — The current min/max/avg delta-time statistics actively *hide* the faults that matter.
  A 45-second dropout of type 1077 and a steady 1 Hz stream can produce similar averages. A plot
  over time makes gaps, bursts and reconnects self-evident.
- **Inspiration** — `[ESP32]` `src/system/History.cpp` — a 1440-entry ring buffer at 30-second
  sampling (12 hours) with 16-byte entries.
- **Notes** — Still open for stream metrics, but the pattern is now established twice in this
  codebase and should be reused rather than reinvented: `SkyTrackBuffer` (`gui/gui_state.h:96-101`,
  60 s × 1440 = 24 h of az/el trail per SV) and the VRS 5-minute distance ring buffer
  (`VRS_DIST_BUFFER_N`, `gui/gui_state.h:365-371`). Subsumes item 4.1 — build one charting path,
  not two.

### 2.3 Report NTRIP protocol version and handshake detail — **Open**

Report whether the caster answered with NTRIP v1 (`ICY 200 OK`) or v2 (HTTP), and make the raw
response headers visible.

- **Why** — A cheap caster-compliance diagnostic. Version and header differences explain a whole
  class of "it works with one client but not another" reports.
- **Inspiration** — `[ESP32]` `src/network/DataOutput.cpp`, which auto-detects ICY versus HTTP
  per connection on a single port.
- **Notes** — The client already *sends* `Ntrip-Version: Ntrip/2.0` on every request
  (`src/ntrip_handler.c:301`, and four further call sites). What is missing is the other half:
  parsing what came back and surfacing it. Note the five near-duplicate request builders — worth
  consolidating while touching this.

### 2.4 VRS / nearby-service analysis — **Partial**

Detect that a mountpoint is a network service rather than a single physical base, and analyse it
on its own terms: baseline to the virtual station, hand-over behaviour, and GGA uplink compliance.

- **Why** — A VRS, MAC or nearest-base service behaves in ways that make single-base checks
  actively misleading. The virtual station *follows the rover*, so a moving 1005 is correct
  operation rather than the fault that item 1.2 flags. Without an explicit notion of VRS, the
  Analyser reports healthy network streams as broken.
- **Inspiration** — `[design.md]` project-local. This is the one entry with no `[ESP32]` ancestry:
  RTK-BASE-ESP32 is a single fixed base and has no VRS concept at all. It arose from the in-progress
  VRS monitor work on `main` (commit `b79007e`, plus the still-untracked `gui/gui_vrs_window.c`).
- **Already built** — `gui/gui_vrs_window.h` describes a floating VRS Monitor (View → VRS Monitor)
  that plots rover GGA against the broadcast ARP: live rover-to-virtual-station distance, a polar
  direction/distance plot, a rolling 5-minute distance strip chart, and accumulated ARP dots that
  reveal hand-overs. Supporting state is at `gui/gui_state.h:357-420` (`vrsDistanceKm`, the
  `VRS_DIST_BUFFER_N` ring buffer, `VRS_ARP_HIST_N` ARP history).

Remaining work:

1. **Classify the mountpoint.** Two independent signals, both cheap: the sourcetable Details and
   Network fields (`VRS`, `MAC`, `NEAR`, `FKP`, `iMAX`, `SSR`), now available in `AppState`
   (`gui/gui_state.h:304-305`); and behavioural detection — an ARP that tracks the rover GGA within
   a few metres is a virtual station regardless of what the sourcetable claims. The behavioural test
   is the authoritative one, since sourcetable metadata is frequently wrong.
2. **Gate item 1.2 on the classification.** The static-position and position-jump checks must be
   suppressed, or reinterpreted, once a stream is classified VRS. This is the dependency that makes
   this item worth doing before 1.2 rather than after.
3. **Report hand-overs explicitly.** The ARP dots make swaps visible on the plot, but the event
   itself — timestamp, old and new ARP, distance jumped — should reach the log and the item 2.1
   timeline, since a hand-over mid-survey is a plausible cause of a position discontinuity the user
   is trying to explain.
4. **GGA uplink compliance.** Network services require periodic GGA and will drop or stop
   generating corrections without it. Report the interval actually being sent, whether the caster
   demanded GGA, and warn when the stream stalls in a way consistent with a missed uplink.
5. **Baseline sanity.** Warn when the virtual station sits implausibly far from the rover, which
   indicates the network has fallen back to a distant physical base — corrections are still
   flowing, but accuracy has quietly degraded.

---

## 3. Tier 3 — Flagship, high effort

### 3.1 Ionospheric monitor — **Open** (re-priced downward)

Build a geometry-free code and carrier-phase combination from dual-frequency MSM7 observations,
and map slant delay to vertical delay at a thin-shell pierce point.

- **Why** — Genuinely differentiating: no other free NTRIP analysis tool does this. It turns the
  Analyser from a stream checker into an observation-quality instrument.
- **Inspiration** — `[ESP32]` `src/gnss/Iono.cpp` — 350 km thin shell, phase-arc management with
  cycle-slip detection via lock-time reduction and phase jumps, spherical trigonometry for pierce
  point geography.
- **Notes** — Verified still open; there is no iono code in the tree. **This item is materially
  cheaper than when it was filed.** It was priced as flagship effort partly because satellite
  elevation was unobtainable, and elevation is exactly what the thin-shell slant-to-vertical mapping
  needs. Since item 3.2 shipped, `src/sv_orbit.c` and `src/sv_ephemeris.c` provide orbits and the
  sky plot already computes elevation per SV. What remains genuinely hard is the phase-arc and
  cycle-slip management, not the geometry. Recommended as the next flagship.

---

## 4. Migrated from `gui/design.md` §12

All items in this section are tagged `[design.md]`.

| # | Item | Status | Notes |
|---|---|---|---|
| 4.1 | Real-time message rate graph (GDI+ / Direct2D) | Open | Subsumed by item 2.1 — one charting path, not two. GDI+ is already linked (`-lgdiplus`, used by `gui/gui_snapshot.c`) |
| 4.2 | Map widget showing base and rover positions | Partial | The browser-based map picker covers coordinate entry, and the VRS Monitor polar plot covers the base/rover relationship live. An in-window map remains unbuilt — reassess whether it is still wanted |
| 4.3 | Multi-connection support | Partial | A second NTRIP connection exists but is purpose-built for ephemeris-only casters (`WorkerOpenEphStream`, `gui/gui_state.h:443`, commit `320f412`). Generalising it to N arbitrary streams for side-by-side base comparison is the remaining work |
| 4.4 | Export analysis results to CSV / JSON | Open | `gui/gui_snapshot.c` exports the window as **PNG** only (`save_window_as_png`). Structured data export is untouched |
| 4.5 | Dark mode / theme support | Open | Verified: no theme handling in `gui/` |
| 4.6 | Tray icon for background monitoring | Open | Verified: no `Shell_NotifyIcon` call |
| 4.7 | Auto-reconnect on connection drop | Open | Verified: reconnect is manual only (`gui/gui_events.c:894`). Reconnect events should appear on the item 2.1 timeline |

---

## 5. Considered and rejected

Recorded so the comparison is not repeated later.

**Producer-side features from `[ESP32]`, not applicable to a consumer:** WiFi AP and station
management, survey-in mode, antenna reference point offset configuration, UDP and USB serial
output, GNSS module NVM commands, OTA firmware update.

**L1/L5 jamming and interference indicators `[ESP32]`** — attractive, but the ESP32 obtains these
from proprietary receiver output. They are **not carried in RTCM**, so they are unobtainable by a
stream consumer. The nearest derivable proxy is a simultaneous collapse of C/N0 across all
constellations, which item 1.4 would make detectable.

---

## Reference

- RTCM Standard: RTCM 10403.3 — Differential GNSS Services Version 3
- NTRIP Protocol: [BKG NTRIP Documentation](https://igs.bkg.bund.de/ntrip/about)
- GUI design document: [`gui/design.md`](../gui/design.md)
