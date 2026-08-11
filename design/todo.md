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

### 1.1 Diff advertised vs. observed RTCM message types — **Shipped**

The Msg Stats tab gained **Advertised** and **Status** columns rather than a separate view — that
table already is "message types", so the comparison belongs where the observations live and column
sorting keeps working. Four verdicts, colour-coded by `NM_CUSTOMDRAW`: `ok`, `missing` (red),
`slow`/`fast Nx` (amber), `extra` (blue). Advertised types are seeded as rows at connect with count
0, since a type that never arrives has no row to appear in otherwise and would be invisible by its
absence — which is the whole point of the check.

`ParseAdvertisedTypes()` and `SourcetableFindMountpoint()` live in `gui/gui_parsers.c`. The
sourcetable is fetched automatically on the worker thread when the mountpoint was typed in rather
than picked from the list, and the fetched table also fills the mountpoint list via
`WM_APP_SOURCETABLE` instead of being discarded.

**Rate comparison must be per epoch, not per frame.** MSM splits one epoch across several frames of
the same type when the observations do not fit in one frame, so a base sending 1127 once per second
in two parts appears to send it twice per second. Timing is therefore sampled only when the MSM
epoch field changes (`msm_get_epoch()`), and the Status column appends `N frames/ep` when a type is
split, so a doubled frame count is self-explanatory.

Note DF393, the multiple-message bit, does **not** identify these splits: measured on a real
capture it was set on five of six MSM types and clear on 1127, with no splitting present at all. It
marks the end of the whole multi-constellation bundle for a station and epoch, not the end of one
message type's frames. `msm_get_multiple_message_bit()` exposes it for bundle-boundary work, but
the epoch field is what the rate check uses.

### 1.2 Cross-check sourcetable position against broadcast ARP — **Shipped**

Four rows on the Stream Health tab: station type, broadcast ARP, sourcetable match, ARP stability.
They live there because they are stream-level facts about the connection, alongside the CRC and
advertised-type rows, and the detail column has room for the explanation — which matters when the
answer is "14.3 km, check the caster registration". The declared position is captured from the
mountpoint list at connect (`sourceLat`/`sourceLon`), the broadcast one comes from
`rtcm_get_station_arp()`, and movement re-uses the `vrsArpHist*` accumulator that already records
positions more than ~10 m apart.

**This required the VRS classification that 1.2 depended on.** A virtual station's reference point
legitimately follows the rover, so without classification every network stream reported a large
sourcetable mismatch and a moving ARP — both false alarms. `ClassifyStation()` uses two signals and
always reports which one fired: sourcetable keywords, and the behavioural test that the ARP sits
within 150 m of the GGA being sent. On a VRS the sourcetable comparison reads `n/a` and ARP
movement is reported as hand-overs rather than a fault. This closes remaining items 1 and 2 of
§2.4; hand-over logging, GGA uplink compliance and baseline sanity remain open.

Keyword matching requires **token boundaries**, not substrings. "LINEAR" contains "NEAR" and
"MACHINE" contains "MAC", and a false VRS verdict is the dangerous direction of error because it
*suppresses* the position checks, hiding the fault the feature exists to find.

Known limit: the 150 m band is a heuristic. A physical base genuinely that close to the rover would
be classified VRS and have its checks suppressed. The conclusive test — shift the GGA with the VRS
Monitor's direction buttons and see whether the ARP follows — is not wired into the classifier.

### 2.3 Report NTRIP protocol version and handshake detail — **Shipped**

Three rows at the top of the Stream Health tab — NTRIP version, Response, Caster software — placed
first because the handshake happens first; the tab now reads in connection order. The full response
headers go to the Log on connect under an `[INFO] Caster handshake` line, since that is the
multi-line part and the log already holds this kind of diagnostic text. `ParseNtripResponse()` in
`gui/gui_parsers.c` handles both replies: NTRIP 1.0's `ICY 200 OK`, which is not HTTP at all, and
NTRIP 2.0's ordinary HTTP status line. A caster answering ICY despite our `Ntrip-Version:
Ntrip/2.0` request is reported as informational, not a fault — that is simply an NTRIP 1.0 caster,
and surfacing it is the point of the item.

**This fixed a pre-existing bug that could mis-accept a rejected connection.** The worker had
already been reading the whole response header, but tested it with
`strstr(header, "200") || strstr(header, "ICY")` — a substring search over the entire header. A
`404 Not Found` carrying `Content-Length: 200`, or a `503` from `Server: caster/2.0.0 build 200`,
both satisfied it, after which the analyser would try to decode an HTML error page as RTCM. The
status line is now parsed properly and a rejection reports the actual status.

Stream Health rows also gained severity colouring (`NM_CUSTOMDRAW`, severity in the item lParam)
matching the Msg Stats treatment: red for real faults, amber for advisories, blue for
informational. The tab carries 14 rows now and problems need to stand out.

### 2.1 Session history with time-series charts — **Shipped**  (subsumes 4.1)

A floating Session History window (View → Session History, `gui/gui_hist_window.c`) with six
stacked strip charts on one shared time axis: throughput, message rate, CRC errors, satellites
tracked, mean C/N0 and reference drift. Sharing the axis is the point — a reconnect reads as a
simultaneous trough in throughput and message rate, while a bad link shows CRC spikes with
throughput unchanged. This is also item **4.1**'s message-rate graph, built once rather than twice.

Three decisions that matter more than they look:

- **1 s sampling, not the ESP32's 30 s.** A 30 s bucket averages a short dropout away, which is
  precisely the failure the item exists to expose. `HIST_CAP` 14400 × 24 B = 337 KB for four hours.
- **Pixel columns show the peak, not the mean.** Compressing 7200 samples into 700 columns by
  averaging flattens a one-second CRC spike of 9 to 0.87 — invisible. Taking the column maximum
  keeps it at full height. Both behaviours are covered by tests.
- **The drift reference is latched on the first ARP and never moved**, since re-centring on the
  current position would hide drift entirely.

C/N0 uses a fixed 20–60 dB-Hz band rather than a zero-based axis: a tracked signal is never
0 dB-Hz, so zero-basing squeezed the useful 35–55 range into the top tenth of the panel. The other
five panels stay zero-based, where a trough to zero *is* the fault being looked for.

The time axis draws vertical gridlines at the same instants in every panel so a feature can be
traced across metrics, with labels once at the bottom. Tick spacing adapts to session length
(seconds → m:ss → h:mm:ss), and ticks are positioned by sample index rather than interpolated time,
so gridlines stay aligned with the traces even if sampling stutters.

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

### 0.1 Displayed version numbers follow `src/core/version.h` — **Shipped**

The About dialog composes its text from `NTRIP_PRODUCT_NAME`, `NTRIP_VERSION_STRING` and
`NTRIP_COMPANY_NAME` rather than the hardcoded "v0.1.0" it had shown for the whole 2.0.0 release.

The sweep found more than the dialog. Two request paths still sent `User-Agent: NTRIP CClient/1.0`
— the sourcetable fetch (`src/net/ntrip_handler.c`) and the `--sky` observation stream
(`src/cli/main.c`), both of them paths that have not yet moved onto the session layer, which had
been building a correct header all along. The monitoring daemon, meanwhile, never set `user_agent`
at all and so inherited the generic default instead of naming itself.

The `NTRIP <artefact>/<version>` convention is now one macro, `NTRIP_USER_AGENT()`, beside the
artefact names in `version.h`; it had been spelled out by hand at five call sites. The session
layer's fallback names the project rather than the CLI, since a caller that leaves `user_agent`
unset is any front end.

Verified by building all three artefacts and reading the strings back out of each binary — GUI
`ntrip-analyser-gui/2.0.0`, CLI `ntrip-analyser/2.0.0`, daemon `ntrip-monitord/2.0.0` — with no
`CClient` string left anywhere. (The CLI token read `ntripanalyse/2.0.0` when this item shipped;
0.2 renamed it in the same unreleased batch.)

**Left open deliberately — see 0.2.** The CLI answers to four different names.

### 0.2 The CLI has four names — **Shipped**

`NTRIP_ARTEFACT_CLI` was `"ntripanalyse"`, the built binary was `ntripanalyser` (CMake
`OUTPUT_NAME`), `--version` printed a hardcoded `"ntrip-analyser"`, and the documentation was itself
split — roughly 30 references against 11.

All four are now **`ntrip-analyser`**, chosen because it matches the product, is consistent with
`ntrip-analyser-gui`, and was already what `--version` printed. 84 references across 16 tracked
files, plus the two shell-completion files, which are *named* for the command and so needed
`git mv` rather than an edit. Historical changelog entries were left alone: they record what the
command was called at the time, and rewriting them would falsify the record.

The rename made `NTRIP_ARTEFACT_CLI` and `NTRIP_ARTEFACT_COMMON` the same string, which exposed a
latent ambiguity — `receive_mount_table()` is reachable from both the CLI and the GUI and was
identifying as neither. It now takes the agent from its caller, so each front end names itself.
What remains is `NTRIP_ARTEFACT_LIB` (`"ntrip-analyser-lib"`), used only as the session layer's
fallback: deliberately not a real artefact name, so that if it ever appears in a caster's log it
reads as a front end that forgot to identify itself rather than as the CLI.

### 0.3 Release asset naming and packaging — **Shipped**

Installed names stay plain (`ntrip-analyser`, `ntrip-analyser-gui.exe`, `ntrip-monitord`) because
the systemd unit, the Munin plugin, the docs and any user script refer to them by name. Release
assets carry `<artefact>-<version>-<os>-<arch>`, generated from `version.h` by a `release` target
so a filename cannot disagree with the binary inside it. Full rationale in
[architecture.md §7.5](architecture.md).

Windows binaries are now linked `-static`. They previously depended on `libwinpthread-1.dll`, which
is not part of Windows — a published `.exe` would have failed to start on any machine without MinGW
installed, i.e. every machine we ship to. This was found by inspecting the import table of a
release-configuration build, not by running it, since a development machine has the DLL and cannot
reproduce the failure.

Verified end to end on both platforms: the target builds, packages, writes a per-platform
`SHA256SUMS` that `sha256sum -c` accepts, and the daemon tarball extracts to a runnable binary with
the Munin plugin's executable bit intact.

### 0.4 Satellite and C/N0 aggregation in the session layer — **Shipped**

The daemon's `sats_total` and `cnr_mean` Munin graphs read zero from the day the service was
deployed. The numbers existed only in the GUI, computed as a side effect of drawing the sky plot,
where no other frontend could reach them.

`src/core/sv_track.c` now keeps a per-(constellation, PRN) table of what the stream carries and at
what C/N0. The session layer feeds it every MSM frame (`sv_track_feed`, ignores non-MSM so the
caller needs no classification) and summarises it in `stats_refresh`, so every consumer counts the
same way.

**It does not reuse the GUI's satellite view, deliberately.** That view computes azimuth and
elevation, needing a decoded ephemeris per SV *and* a station ARP, and goes blank when either is
missing. Satellite count and C/N0 need neither: PRNs come from the MSM satellite mask, C/N0 from
the MSM7 signal block. Reusing it would have made the daemon's graphs depend on ephemeris
availability for no reason.

Two semantics worth remembering:

- The count is **currently in view** (a 5 s staleness window), not **seen this session**. A setting
  satellite lowers it — which is what makes the graph a health signal rather than a ratchet.
- C/N0 is **MSM7-only**. MSM4/5/6 carry no extended C/N0, so those streams get counts with
  `cnr_mean` at zero, matching what `NsGnssStats` already documents.

Verified by replaying the 206-frame capture and comparing satellite by satellite against the
pre-existing `extract_satellites()` — exact agreement on all 39 SVs across GPS (10), GLONASS (9),
Galileo (10) and BeiDou (10), on both Windows and Linux. Mean C/N0 45.5 dB-Hz, consistent with the
MSM7 layout fixed under 1.4.

A caution recorded for the next person: `-s / --sat` does **not** honour `--rtcm-stdin` — that flag
is `--sky`-only — so `-s` silently connects to the live caster instead of replaying a file. An
early comparison against it looked like agreement purely by coincidence.

### 0.5 The last two framing loops, in `--sky` — **Shipped**

`run_sky_stdin_stream()` (`src/cli/main.c:328`) and `run_sky_obs_stream()` (`:522`) still frame RTCM
by hand — the only duplicates of the session layer's framing left in the tree, and the reason the
migration in [architecture.md §9](architecture.md) is not finished.

**Done: the prerequisite.** The stdin path could not move onto the session layer at all, because
`ns_open_file()` takes a path and `stdin` has none. `ns_open_stream(FILE *f, bool own, ...)` is the
general form — `ns_open_file()` is now a wrapper — and `ns_close()` closes only what the session
opened, so a caller's handle survives. Verified equivalent to `ns_open_file()` on the capture (206
frames, 39 SVs, 45.46 dB-Hz through both) and working on `stdin`.

Recorded while testing it: a Windows caller **must** put the handle in binary mode first.
In text mode the same capture yields **1** frame instead of 206, because CRLF byte pairs inside
RTCM payloads get translated. `run_sky_stdin_stream()` already does this (`_setmode`, `:338`); any
new caller must too.

**Done: `run_sky_stdin_stream()`.** It now opens `ns_open_stream(stdin, false, ...)` and does its
per-frame work in `sky_on_event()`, a handler shared with the obs source. Both hand-written copies
of the per-frame body are gone with it.

One behavioural change came free: the old loops accepted any frame with a plausible preamble and
length **without checking its CRC**, so a corrupted frame was decoded as if it were sound. The
session validates CRC first.

Verified against a binary built from the previous commit, both fed the same fresh capture
(`fresh.rtcm3`, 447 frames):

| Measure | Old | New |
|---|---|---|
| frames / MSM / bytes | 447 / 444 / 119 KB | identical |
| PNG, back-to-back runs | `0d024b7d…` | `0d024b7d…` (byte-identical) |
| sector updates, ephemeris cache saturated | 3330 | 3330 |

Two measurement traps worth recording, because both first looked like regressions:

- **The PNG embeds something time-dependent.** Two runs seconds apart hash the same; runs a minute
  apart do not. Comparisons must be back-to-back.
- **Sector updates depend on how much ephemeris has arrived**, not only on the observations. Paced
  runs gave old 2994–3117 against new 3330 — non-overlapping, and it looked systematic. Delaying the
  observations until the eph cache had filled brought both to exactly 3330. Any before/after
  comparison of this number must pre-warm the cache or it measures the caster, not the code.

**Done: `run_sky_obs_stream()`.** Its DNS lookup, socket, GET request, HTTP-header skip, GGA
keep-alive and receive timeout are all `ns_open()` now. `grep 0xD3 src/cli/main.c` returns nothing:
**the session layer is the only RTCM framer in the tree.**

It carried the same header-skip bug the session layer had already fixed — `buffer[received] = '\0'`
with `received` able to reach `sizeof(buffer)`, a one-byte overflow, and `strstr()` over a buffer
not guaranteed to be terminated.

Two deliberate behaviour changes:

- **"Connected" now means the caster accepted us.** It was printed the moment `connect()` returned,
  which is optimistic — a 401 or 404 connects perfectly well. It is now emitted on `NS_EV_HANDSHAKE`,
  and the function returns failure when no handshake ever completed.
- **GGA is sent only when a position is configured** (`opt.send_gga`), where the old code always
  sent it, transmitting `0,0` when none was set. The interval stays 5 s.

Verified: the deterministic stdin path stays byte-identical (447 / 444 / 119 kB, PNG
`560ae252…` for both binaries back to back), and live 40 s runs overlap — frames old
{235, 235, 235} against new {235, 240, 246}, sector updates old {1662, 1672, 1672} against new
{1650, 1671, 1671}. Overlapping ranges, unlike the disjoint ones that exposed the ephemeris-timing
effect above.

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

### 1.1 Diff advertised vs. observed RTCM message types — **Shipped**, see §0

### 1.2 Cross-check sourcetable position against broadcast ARP — **Shipped**, see §0

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
CLI output was never affected: both extractors are called only from `gui/`, and the CLI's MSM7
output comes from the full decoder, which reads the layout correctly.

The scatter is fed by its own accumulator (`SigCnrState`) on every MSM epoch rather than from the
trail buffer — the 60 s trail sampling yields one point per satellite per minute, far too sparse to
read as a cloud.

### 0.1 Displayed version numbers must follow `src/core/version.h` — **Shipped**, see §0

### 1.4b Per-constellation C/N0 columns in the Satellites tab — **Shipped**

The tab shows `GNSS` / `Seen` / `In View` / `C/N0 Min` / `C/N0 Mean` / `C/N0 Max` / `Satellites`,
fed from `ns_stats()->gnss[]` via `AppState.gnssStats` rather than recomputed, so it agrees with the
daemon's Munin graphs by construction. Works for replay as well as live, since both drive
`ObsProcessFrame`.

`Seen` (cumulative, from `satStats`) and `In View` (current 5 s window, from `sv_track`) are both
shown on purpose — either alone invites a misreading. C/N0 renders as `-` rather than `0.00` when
the stream carries none, because MSM4/5/6 have no C/N0 field and a zero reads as a dead signal.

Original text below, kept because its cost estimate was wrong in an instructive way.

The Satellites ListView still shows only `GNSS` / `Sats Seen` / `Satellites`
(`gui/gui_layout.c:338-340`). Min/mean/max C/N0 columns there would give a sortable tabular view
alongside the charts.

**Re-priced by 0.4.** This item assumed `SatStatsSummary` would need a new C/N0 field. It no longer
does: `sv_track_summarise()` already produces per-constellation `sats_tracked` and
`cnr_mean`/`cnr_median`/`cnr_min`/`cnr_max` in `NsGnssStats`, and the session layer keeps it
current. The remaining work is reading `ns_stats()->gnss[]` in the GUI and adding the columns — no
new parsing, no new plumbing.

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

### 2.1 Session history ring buffer with time-series charts — **Shipped**, see §0

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

### 2.3 Report NTRIP protocol version and handshake detail — **Shipped**, see §0

Still open from the original note: the client builds the same request in **five near-duplicate
places** (`src/ntrip_handler.c:301` and four further call sites) — worth consolidating. Note the
GUI worker does not use any of them; it opens its own socket and writes its own request in
`gui/gui_thread.c`, so that is a sixth copy.

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

1. ~~**Classify the mountpoint.**~~ **Done** — `ClassifyStation()` in `gui/gui_events.c`, using
   sourcetable keywords (token-boundary matched) plus the behavioural ARP-tracks-GGA test.
2. ~~**Gate item 1.2 on the classification.**~~ **Done** — the sourcetable comparison reads `n/a`
   and ARP movement is reported as hand-overs when the stream is classified VRS.
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
| 4.1 | Real-time message rate graph | **Shipped** | Delivered as the Message rate panel of the item 2.1 Session History window — one charting path, not two |
| 4.2 | Map widget showing base and rover positions | Partial | The browser-based map picker covers coordinate entry, and the VRS Monitor polar plot covers the base/rover relationship live. An in-window map remains unbuilt — reassess whether it is still wanted |
| 4.3 | Multi-connection support | Partial | A second NTRIP connection exists but is purpose-built for ephemeris-only casters (`WorkerOpenEphStream`, `gui/gui_state.h:443`, commit `320f412`). Generalising it to N arbitrary streams for side-by-side base comparison is the remaining work |
| 4.4 | Export analysis results to CSV / JSON | **Shipped** | File > Export Statistics writes the session snapshot as JSON or CSV through  /  — the same serialisers the daemon publishes through, so an export and a Munin sample agree by construction. Format follows the typed extension, not just the filter. Truncation is a failure, not a partial write |
| 4.5 | Dark mode / theme support | Open | Verified: no theme handling in `gui/` |
| 4.6 | Tray icon for background monitoring | Open | Verified: no `Shell_NotifyIcon` call |
| 4.7 | Auto-reconnect on connection drop | **Shipped** | Tools > Auto-reconnect on drop, off by default so an unattended run means what it always did. Uses the session layer backoff the daemon relies on; applies to the next stream opened. Stream Health gains a Reconnects row, set before the function early-returns on a missing ARP so it shows on streams without 1005/1006 |

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

- [`design/architecture.md`](architecture.md) — how one core is to be shared across the CLI, the
  Windows GUI, the planned monitoring service and the Android app. Several items here (4.4 export,
  and anything the service or phone needs) depend on the statistics-snapshot schema defined there.
- RTCM Standard: RTCM 10403.3 — Differential GNSS Services Version 3
- NTRIP Protocol: [BKG NTRIP Documentation](https://igs.bkg.bund.de/ntrip/about)
- GUI design document: [`gui/design.md`](gui-design.md)
