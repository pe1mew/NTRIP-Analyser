# The Android views — specification

**Specified 2026-08-12**, correcting an earlier misreading: what was built
first was the sector *coverage heatmap* (`sky_render`), which answers
"which parts of the sky are being received well". What is wanted is the
**satellite sky view** — the desktop's Sky Plot: one labelled marker per
satellite, positioned by azimuth and elevation.

Three views, shared by both editions. The layout and the drawing are the
same in free and pro; only *when* and *how often* they are drawn differs.

## 1. Sky view — satellites, not sectors

One marker per tracked satellite: coloured by constellation, brightness
scaled by C/N0, labelled with the PRN (`G29`, `R22`, `E15`). Compass
rose, elevation rings at 15°/30°/45°/60°/75°, mountpoint and ARP in the
footer — as the desktop Sky Plot draws it.

| | Free | Pro |
|---|---|---|
| When | **once, full screen, after the 90 s capture** | continuously updating |
| Position source | phone GNSS | phone GNSS, ephemeris stream, or downloaded RINEX |
| Satellites without a position | counted and named in the header | same |

### Where the positions come from

An RTCM observation stream says *which* satellites the base tracks and
how strongly; it never says where they are. Three sources can supply
azimuth and elevation:

**Phone GNSS** (both editions). Android's `GnssStatus` reports
constellation, svid, azimuth, elevation and C/N0 for every satellite the
*phone* is tracking. Matching those by (constellation, PRN) against the
base's tracked list gives a position for each.

This is an approximation, and a sound one: a GNSS satellite is roughly
20 000 km away, so an observer tens of kilometres from the base sees it
at essentially the same azimuth and elevation — under a tenth of a
degree of difference for a 30 km separation, far below the marker size.
It holds while the phone is anywhere near the station being checked,
which is the field case the free edition exists for. It does **not** hold
for checking a base on another continent from an office, and the view
should say which source it used so that distinction is never silent.

**What the phone can actually report.** More than "satellites it has a
lock on". The GNSS engine holds an almanac and ephemerides — which is
what assisted GNSS delivers over the network, and why a phone produces a
full sky within seconds of a cold start — so it can compute azimuth and
elevation for satellites above the horizon that it is not currently
tracking. Those appear in `GnssStatus` with a C/N0 near zero and
`usedInFix` false; the per-satellite `hasAlmanacData()` and
`hasEphemerisData()` flags exist because the list includes them. How
completely a device does this varies by chipset and HAL, so it is a
useful tendency rather than a guarantee.

Assisted GNSS is therefore not a second source the app can draw on:
Android exposes **no public API to read the assistance data**, which
lands in the HAL. It improves what `GnssStatus` can tell us, and that is
all. (`GnssNavigationMessage`, API 24+, delivers decoded navigation
subframes, but only for satellites the phone is actively tracking —
strictly less than `GnssStatus` offers here, for considerably more work.)

**Satellites that cannot be placed must be counted, not omitted
silently.** Whatever the source, some of the base's satellites may have
no position: the phone is indoors, or the ephemeris stream has not yet
delivered that constellation. The view states it —

> 39 of 44 satellites shown · 5 without a position (phone GNSS)

— naming the source, so a sparse plot is never mistaken for a station
tracking poorly. That distinction is the whole point of the view: an
absent marker must never be readable as a missing satellite.

**Ephemeris stream** (pro) and **downloaded RINEX** (pro) compute the
position from the orbit and the base's own ARP, so they are exact and
independent of where the phone is. They are also what makes the pro
edition usable away from the site.

## 2. Signal quality — C/N0 per satellite

A bar per satellite, coloured by constellation, with the count, mean and
range in the header.

| | Free | Pro |
|---|---|---|
| Value | **mean over the whole capture** | live, this epoch |

### Averaging in power, not in decibels

C/N0 is logarithmic, so the mean of the dB values is not the mean of the
signal. Averaging must convert to linear power first:

```
mean_dB = 10 * log10( mean( 10^(cn0_i / 10) ) )
```

Averaging the dB numbers directly biases the result low — it is a
geometric mean of the powers — and the error grows with the spread, which
is exactly when the number matters. A satellite that fades and recovers
would be reported as consistently poor rather than as intermittently
strong.

## 3. C/N0 versus elevation

A separate view: every sample of the session as a point, C/N0 against
elevation, with a per-constellation trend. This is the antenna
diagnostic — a healthy installation rises smoothly from roughly 35 dB-Hz
at the horizon to about 50 dB-Hz at zenith, and a flat or dented curve
points at the antenna, its siting, or an obstruction rather than at the
receiver.

Requires elevation per sample, so it depends on the same position source
as the sky view.

## What this means for the work already done

- `sky_render`'s RGB entry point and the pixel-identical split stay
  useful: the coverage heatmap remains a legitimate pro extra, and the
  entry point is what any core-side renderer would use.
- `rtcm_extract_arp_ecef()` stays needed by every position source.
- The ephemeris side-stream stays, as one of pro's three sources.
- The free edition no longer needs an ephemeris caster configured at
  all, which removes the largest piece of setup from the free flow.
