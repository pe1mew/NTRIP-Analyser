# Android NTRIP-Analyser — staged approach

Single user, single app, **two modes**: **Normal** for the quick "is it on and working" check that completes in under a minute; **Advanced** for a deeper read including end-to-end testing of network-RTK services. The user toggles between them in settings (or a top-bar switch).

## Mode 1 — Normal (rudimentary KPI check)

The minimum-viable set that answers *"does this station meet basic KPIs for RTK service?"* Runs as soon as the user opens the app on an already-configured mountpoint, completes within ~60 s, and emits a single green / yellow / red verdict.

**The seven rudimentary KPIs:**

| # | KPI | Pass when |
|---|---|---|
| 1 | **Stream connected and producing** | Auth ok, TCP open, bytes/s > 100 sustained for 10 s |
| 2 | **Format is RTCM 3.x** | First 0xD3 + valid length within 10 s |
| 3 | **ARP broadcast** | 1005 or 1006 received within 30 s, coords non-zero |
| 4 | **Multi-GNSS observations flowing** | MSM frames for at least GPS *and* Galileo at ≥ 0.5 Hz |
| 5 | **SV count in expected range** | Total ≥ 25 SVs (typical mid-latitudes) |
| 6 | **Median L1 CNR ≥ 40 dB-Hz** | Antenna + LNA chain healthy |
| 7 | **No CRC error spike** | < 1 error in 1 000 frames over the 60 s window |

All seven green for 60 continuous seconds → **STATION OK**. One yellow → caution. One red on #1, #2, #3, #4, or #7 → **FAILED**.

**Normal-mode UI:** big colored verdict badge at the top, the seven-row checklist below with value + check/cross per row, throughput chip, and a single *Run again* button. No sky plot, no per-SV tables — those live in Advanced mode.

## Mode 2 — Advanced (deeper diagnostic)

A superset of Normal: the seven Normal KPIs stay pinned at the top so the headline verdict is always visible, and the dashboard adds extended KPIs, sky-plot visualisations, **and an explicit end-to-end test for network-RTK / nearby-service mountpoints** where the rover has to send NMEA GGA.

**Extended KPI set (layered on top of the seven):**

| KPI | What it adds | Why a normal user doesn't need it |
|---|---|---|
| **ARP plausibility** | Compares broadcast ARP against the phone's own GNSS fix (< 10 m = pass for single-station; < distance threshold for VRS) | Requires GPS permission; only relevant to first-time install or VRS test |
| **Per-GNSS SV thresholds** | GPS ≥ 6, GLO ≥ 4, GAL ≥ 6, BDS ≥ 4 (latitude-aware) | Total SV count usually covers this implicitly |
| **Per-band CNR breakdown** | Median CNR per signal (L1C, L2W, L5Q, E1C, E5Q, ...) | Diagnostic for antenna / front-end issues |
| **Sky-sector coverage** | Heatmap quadrants checked: no quadrant with < 10 % expected SVs after 60 s | Catches obstruction / mis-pointed antenna |
| **Latency** | Newest MSM `epoch_time` vs system clock | Matters for RTK clients, not for "is it on" |
| **Per-PRN tracking continuity** | Flag SVs whose observed_flag has gaps > 5 min while above horizon | Catches receiver-side loss of lock |
| **Ephemeris age per GNSS** | Time since most recent 1019 / 1020 / 1041 / 1042 / 1044 / 1045 / 1046 | Indicates whether eph stream is healthy too |
| **Sourcetable conformance** | What the mountpoint advertises vs what it actually delivers | Catches stale metadata at the caster |

### Network-RTK / nearby-service test (the new feature)

Many casters (Kadaster NETPOS, BKG GREF, Trimble VRS Now, etc.) provide **virtual reference station** or **nearest physical station** mountpoints that require the rover to **upload its position as an NMEA GGA sentence**, in exchange for RTCM corrections computed (or selected) for that location. This isn't tested by the Normal seven KPIs because Normal assumes a static single-station mountpoint — but in field use it's exactly the service most rovers actually consume.

**The test workflow:**

1. **Pick the mountpoint** — either from the sourcetable (filter by `Network` carrier flag, e.g. `MAX`, `iMAX`, `VRS`, `RTCM3-VRS`, `RTCM3-NEAR`) or by saved profile.
2. **Choose the GGA position source:**
   - **Phone GPS** (default — uses Android Fused Location to get a reasonable lat/lon/alt)
   - **Manual coordinates** (enter lat/lon/alt directly, for testing a survey monument or a known reference)
   - **Saved test points** (e.g. four corners of the network coverage area, to verify the VRS computes correctly across the whole region)
3. **App starts the GGA keep-alive loop** — sends the chosen GGA immediately, then every 10 s thereafter (caster-configurable, default 10 s) to match the rebroadcast cadence most casters expect.
4. **Run the seven Normal KPIs** on the resulting stream.
5. **Run the VRS-specific KPIs** below.
6. **Optional: position shift test** — re-run from a second GGA position ≥ 30 km away and verify the ARP / station ID actually changes. Proves the network service is dynamic, not just returning a fixed nearest station.

**VRS-specific KPIs:**

| KPI | Pass when |
|---|---|
| **GGA accepted by caster** | No disconnect within 5 s of sending the first GGA |
| **RTCM starts after GGA** | First valid RTCM frame received within 10 s of GGA send |
| **Virtual ARP near rover** | Broadcast 1005 / 1006 ARP is within a sane distance of the GGA position (typically < 50 km for VRS, < 100 km for nearest-station service) |
| **Station ID / 1007 / 1033 reasonable** | Returned reference-station ID matches the network's documented range (and changes when GGA shifts, for true VRS) |
| **Keep-alive holds** | Stream stays continuous as long as the 10 s GGA cadence is maintained — no disconnect after 60 s |
| **Drop on GGA stop** *(optional)* | Stop sending GGA — most VRS casters drop the stream within ~30-60 s, confirming the service is GGA-gated as advertised |
| **Position-shift response** *(optional)* | When the GGA position is moved ≥ 30 km, the broadcast ARP shifts to follow within 30 s (true VRS) or stays close to the new GGA's nearest physical station (nearest-station service) |

**UI for the network-RTK test:**

- **Dedicated screen** reached from a "Test network-RTK service" button in Advanced mode.
- **Map view** at the top showing the rover position (phone GPS or entered manually) and the broadcast ARP as two markers, with a line + distance between them.
- **GGA source picker** as a dropdown (phone GPS / manual / saved test points).
- **Pinned KPI strip** with the VRS-specific KPIs (green/yellow/red, same visual language as Normal mode).
- **Cadence indicator** — small dot pulsing every 10 s when a GGA is sent, with the exact NMEA string available on long-press for debugging.
- **"Test from another point"** button to load a second GGA position and verify the ARP shifts.
- **Sourcetable filter chip** that pre-screens for `Network` carrier flag and lists VRS-style mountpoints first.

This test layer reuses the existing seven Normal KPIs underneath — the network-RTK mode just adds the GGA send loop, the second-position re-test, and the VRS-specific assertion set.

### Other Advanced-mode UI additions (carried over)

- **Full sky plot** (live markers + heatmap toggle) — the existing `sky_render.c` heatmap renders directly into an Android `Bitmap`.
- **Per-message stats table** — RTCM type, count, min/avg/max dt (the Msg Stats list from the desktop GUI).
- **Per-SV detail popup** — PRN, az/el, per-band CNR table, exactly as in the desktop GUI.
- **CRC error counter** with running rate and history sparkline.
- **Sky-coverage heatmap export** — PDF for install report, PNG for sharing.
- **RTCM capture toggle** — record raw frames to phone storage for later offline analysis.
- **Sourcetable browser** — fetch and display the `-m` equivalent, with the network-RTK filter chip above.

## Mode switch

A single Settings entry — *Mode: ○ Normal ● Advanced* — with a short helper string:

> Normal: a one-screen pass/fail check that completes in ~60 s.
> Advanced: full diagnostic dashboard, including sky plot, per-SV detail, message decoding, and end-to-end testing of network-RTK / VRS mountpoints.

Persist the choice; first-run defaults to Normal. The mode can also be toggled from an overflow-menu item on the main screen so a user who opened in Normal and wants to test a VRS service can switch in one tap.

## Shared underpinnings — what's already portable

The two modes share **all the same data-extraction code**; only the rendering layer differs.

- **`src/rtcm3x_parser.c` + `src/ntrip_handler.c`** — plain C99, no Win32, no GUI. Compile directly with the Android NDK for the parsing core that drives both modes.
- **`src/nmea_parser.c`** — already builds the GGA sentence the GUI sends; usable straight through JNI for the network-RTK keep-alive loop, no changes needed.
- **`src/sv_ephemeris.c` + `src/sv_orbit.c`** — provide propagation for sky-position calculations in Advanced mode.
- **`src/sky_collect.c` + `src/sky_render.c`** — already render to a portable RGB buffer with a built-in PNG encoder. Wraps directly into `Bitmap.createBitmap(rgbBytes, …)` for the Advanced sky plot. No GDI+, no libpng, no dependencies.
- **`src/rinex_nav.c`** — useful for an offline test mode where the installer can pre-load a recent BRDC file at the rack without needing the eph caster reachable from the rack's network.

So Normal mode is essentially the NTRIP client + the seven aggregation counters on top — a few hundred lines of Kotlin/Compose around a thin JNI wrapper of the C parsing core. Advanced mode adds the sky-plot bitmap, the per-message stats UI, **and the GGA-driven network-RTK test screen** — all built on the same underlying decoder.

## Suggested first-cut delivery

1. **Phase 1 — Normal mode only.** One Compose screen, seven KPIs, JNI to a slim NDK build of `rtcm3x_parser` + `ntrip_handler` + a 60-second aggregator. Ship this first; it's enough for installer sign-off and casual owner checks.
2. **Phase 2 — Advanced mode toggle + sky plot bitmap + per-message stats table + network-RTK test screen.** Reuses `sky_render`, `sv_ephemeris`, `nmea_parser`, and the GGA keep-alive logic that already lives in the desktop tool's worker thread. The network-RTK test is the differentiating feature here — it turns the app into something useful for *consumers* of network-RTK services too, not just for *operators* of fixed mountpoints.
3. **Phase 3 — Persistence and alerting.** Rolling KPI log, push notifications on status changes, multi-station favourites. Worth doing only after Phase 1/2 have confirmed the workflow.
