# The Android views — specification

## Two modes

**Station mode** is the sixty-second check: connect, watch the seven
KPIs, produce a verdict. It answers *does this station meet the basic
KPIs for RTK service?* and then stops.

**Analysis mode** is everything else — the sky view, the signal-quality
bars, C/N0 against elevation. It answers *what is this station actually
doing?*

| | Free | Pro |
|---|---|---|
| Station mode | yes, unlimited | yes |
| Analysis mode | **static**: shows what station mode captured | **live**: started and stopped on its own, runs as long as wanted |

The distinction is what the free edition sells on and what the paid one
improves. A free user gets the full picture of one capture and can study
it for as long as they like; a paid user can keep the instrument running
and watch the picture change — which is the only way to see a fault that
comes and goes.

This also settles what the views draw from. In free they render the
measurements accumulated during the station check, frozen at its end,
with C/N0 as the session mean. In pro they render the live session, with
C/N0 as the current epoch.


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
| Position source | phone GNSS, imported RINEX | ephemeris stream, imported RINEX (phone GNSS as fallback) |
| Satellites without a position | counted and named in the header | same |

Both editions draw a complete sky: free from the phone's GNSS and from a
RINEX file the user supplies, pro from a live ephemeris mountpoint and
from the same RINEX file. The **live stream is the paid capability** --
it fills the cache in seconds from the caster the user is already
connected to, where the free edition asks for a file obtained once a day.

Phone GNSS remains available in pro as a fallback: removing a working
source from the paid product would only make it worse when a user has
neither a file nor a mountpoint.

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

**A RINEX navigation file the user supplies** (both editions) and an
**ephemeris stream** (both editions, on demand) compute the position from
the orbit and the base's own ARP. They are exact, independent of the
handset, and cover every constellation the base tracks.

### Why phone GNSS is a fallback, not the foundation

Measured on a Huawei SNE-LX1 against a Kadaster base: **23 of 46
satellites placed, and no Galileo at all** — the handset simply does not
report that constellation. Coverage is a property of the phone, not of
anything the app or the user controls, so it cannot be what the sky view
rests on. It stays as a fallback because it needs no file and no
connection, and on a better handset it is genuinely useful.

### Why the observation stream is not enough either

Some casters interleave ephemerides with observations. Measured on
APEL00NLD0: **7 satellites in five minutes**, against 46 tracked. RFSEE01
carries none at all. Useful as a free supplement, never a source.

## Where the orbits actually come from

### A RINEX navigation file, supplied by the user

The user downloads a broadcast navigation file themselves and imports it
into the app. Verified end to end: BKG's daily merged file
(`BRDC00WRD_R_YYYYDDD0000_01D_MN.rnx.gz`) is **0.2 MB compressed, 1.7 MB
open, 156 satellites across all seven constellations**, and
`src/core/rinex_nav.c` parses it as it stands — 1730 records, 113
satellites into the cache.

**The app does not download it.** That is deliberate, and the reason is
recorded below.

### An ephemeris stream, opened on demand and closed again

Where a caster publishes an ephemeris mountpoint, it fills the cache in
about thirty seconds. The app treats it as a resource to borrow, not to
hold:

- **Open only when the cache cannot place the satellites being tracked.**
  A run with a fresh RINEX file, or with a cache from an earlier run,
  never opens it at all.
- **Close as soon as it has what it needs** — every tracked satellite
  placed — or after a bounded attempt if the caster is not delivering.
- **Do not reopen until the cache has aged**, at which point the orbits
  are no longer trustworthy and a refill is genuinely warranted.

A connection held open for hours to receive a few messages an hour is
rude to the caster and pointless to the user; this keeps it to what the
work requires.

**On ageing:** a broadcast ephemeris is nominally good for two to four
hours for positioning. A sky plot is far more forgiving — a kilometre of
orbit error at 20 000 km is about 0.01° of azimuth — so an ephemeris
hours past its fit interval still places a marker perfectly well. The
cache is therefore aged on what the *view* needs, not on what a
positioning engine would demand, and the difference is stated wherever
the age is shown.

## Why the app does not download the navigation file

An earlier plan had the app fetch the daily file from BKG or ESA. Both
serve it over open HTTPS with no account, and the data is IGS broadcast
ephemeris under a long-standing free and open policy. That plan is
**paused**, unbuilt, for reasons that were checked rather than assumed:

- BKG's site exposes no data-licence statement beyond an *Impressum*, so
  the terms covering **commercial use** could not be established — and one
  edition of this app is paid.
- Nor could the terms covering **automated bulk fetching by a distributed
  app**. A researcher pulling a file is not thousands of installs pulling
  daily, and BKG requires free registration for its NTRIP services
  specifically to manage load, which suggests the distinction matters to
  them.

Rather than proceed on an assumption, the app asks the user to obtain the
file themselves. The user then holds the relationship with the data
provider and is responsible for complying with its licence and usage
rules — which is both the honest arrangement and the one that needs no
permission from anyone.

If those terms are later confirmed in writing, the download can be added
behind the mitigations that would apply anyway: one file per device per
day, never per run; the app identified in the `User-Agent`, as it already
identifies itself to NTRIP casters; `If-Modified-Since` so most fetches
cost a 304; and the source attributed in the app.

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

## The orbits place the satellites; the phone fills the gaps

`sky_azel_for_sat()` answers, per satellite, where it is as seen from the
station — from the same ephemeris cache and the same clock the coverage
heatmap uses, so the two views cannot disagree about one satellite. The
sky view prefers it and falls back to the phone only for satellites no
orbit covers.

Measured on the device, same base, minutes apart: **23 of 47 from the
phone, 44 of 45 from the ephemeris stream** — and Galileo, which that
handset does not report at all, appears throughout. Satellites below the
horizon are dropped rather than drawn, since a marker outside the plot's
geometry would be a claim the geometry cannot make.

A satellite with no orbit is published with `az` and `el` as null rather
than zero: zero would place it on the horizon due north, and the plot
cannot tell that from a fact.

## What this means for the work already done

- `sky_render`'s RGB entry point and the pixel-identical split stay
  useful: the coverage heatmap remains a legitimate pro extra, and the
  entry point is what any core-side renderer would use.
- `rtcm_extract_arp_ecef()` stays needed by every position source.
- The ephemeris side-stream stays, as one of pro's three sources.
- The free edition no longer needs an ephemeris caster configured at
  all, which removes the largest piece of setup from the free flow.
