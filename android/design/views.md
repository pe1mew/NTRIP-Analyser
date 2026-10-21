# The Android views — specification

> **The frame around these views is specified elsewhere.**
> [guiV3spec.md §4](../../design/guiV3spec.md) fixes the order of the
> bands an analysis screen is made of — tabs, what this view is, the
> numbers behind it, the plot, what the plot is of, the key to its
> colours — and `AnalysisBands` in `Views.kt` enforces it, so a view
> supplies the parts and cannot choose where they go. This document is
> about *what is in* the views: where a satellite's position comes from,
> and what the plots mean.


## Two modes

**Station mode** is the sixty-second check: connect, watch the seven
KPIs, produce a verdict. It answers *does this station meet the basic
KPIs for RTK service?* and then stops.

**Analysis mode** is everything else — the sky view, the signal-quality
bars, C/N0 against elevation, and in pro the station's self-position and
its reference position. It answers *what is this station actually
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
Pro adds two more after them — §4 Self-position and §5 Reference
position — as tabs of the same screen, so pro's tab row scrolls where
free's three share the width.

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
**ephemeris stream** (pro only, on demand — `Features.HAS_EPH_STREAM`;
this line said "both editions" until 2026-10-21, against the table
above and the code) compute the position from
the orbit and the base's own ARP. They are exact, independent of the
handset, and cover every constellation the base tracks.

### Why phone GNSS is a fallback, not the foundation

Measured on a Huawei SNE-LX1 against a Kadaster base: **23 of 46
satellites placed, and no Galileo at all** — the handset simply does not
report that constellation. Coverage is a property of the phone, not of
anything the app or the user controls, so it cannot be what the sky view
rests on. It stays as a fallback because it needs no file and no
connection, and on a better handset it is genuinely useful.

### Why the observation stream is not enough *on its own*

Some casters interleave ephemerides with observations. Measured on
APEL00NLD0: **7 satellites in five minutes**, against 46 tracked. RFSEE01
carries none at all.

That measurement was read as "useful as a free supplement, never a
source", and the app acted on it by not decoding those messages at all.
The reading was too strong. Measured on `caster.centipede.fr/NEAR`:
**1020 ×16, 1042 ×8 and 1046 ×23 in fifteen seconds**, and over a
five-minute run on the phone, **603 ephemerides, 40 of 40 tracked
satellites placed, no second connection opened**. Kadaster's own
APEL00NLD0 advertises `1019,1020,1042,1044,1045,1046` in its sourcetable.

So it is a source where the station makes it one, and no source where
the station does not — which cannot be known before connecting. The app
therefore decodes it always and keeps the other sources for the
stations that carry nothing. A station like RFSEE01 is exactly why the
ephemeris stream and the imported file remain.

## Where the orbits actually come from

### The station's own observation stream

Free, immediate, and needing no setup at all: the ephemeris messages
many stations broadcast beside their observations are decoded straight
into the same cache. Nothing distinguishes them once cached — they are
the same broadcast the ephemeris mountpoint carries.

Measured on `caster.centipede.fr/NEAR`, pro edition, five-minute run:
**603 ephemerides received, 41 orbits cached, 40 of 40 tracked
satellites placed, and 0 frames off the ephemeris stream** — it was
never opened. The free edition, which cannot open one, draws the same
plot from the same source.

The count is reported in the app (`from_obs` in the bridge document)
rather than left implicit, because "this station broadcasts its own
orbits" is a fact about the station that an installer signing one off
wants, and because without it an empty sky view cannot be told from a
station that sends no orbits.

### A RINEX navigation file, supplied by the user

The user downloads a broadcast navigation file themselves and imports it
into the app. Verified end to end: BKG's daily merged file
(`BRDC00WRD_R_YYYYDDD0000_01D_MN.rnx.gz`) is **0.2 MB compressed, 1.7 MB
open, 156 satellites across all seven constellations**, and
`src/core/rinex_nav.c` parses it as it stands — 2131 records, 139
satellites into the cache, 135 of them usable at the moment of import.
The remainder are SBAS, which the loader skips by design.

Measured on the phone with the file imported by hand: **46 of 46
tracked satellites placed, all four constellations the base streams**,
with the free edition never opening a network connection for orbits.

**The app does not download it.** That is deliberate, and the reason is
recorded below.

### An ephemeris stream, opened on demand and closed again

Where a caster publishes an ephemeris mountpoint, it fills the cache in
about thirty seconds. The app treats it as a resource to borrow, not to
hold:

- **Open only when the cache cannot place the satellites being tracked
  and has stopped filling.** A run with a fresh RINEX file, with a cache
  from an earlier run, or on a station that broadcasts its own orbits
  never opens it at all. "Complete yet?" alone was not enough on either
  end: asked on the first pump it opened a connection before a single
  frame had arrived, and asked at 40 of 41 placeable it opened one for a
  satellite that had just risen and whose orbit was on its way. The
  condition is therefore that nothing has reached the cache for twenty
  seconds — measured against NEAR's twelve-second broadcast cycle.
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

A separate view: every sample of the session, C/N0 against elevation,
gathered into cells of one degree by one dB-Hz, coloured by
constellation and drawn more solid the more samples a cell holds
(logarithmic opacity, `ElevationAccumulator`). There is no trend line;
this paragraph said "a point, with a per-constellation trend" until
2026-10-21, which the code never drew. This is the antenna
diagnostic — a healthy installation rises smoothly from roughly 35 dB-Hz
at the horizon to about 50 dB-Hz at zenith, and a flat or dented curve
points at the antenna, its siting, or an obstruction rather than at the
receiver.

Requires elevation per sample, so it depends on the same position source
as the sky view.

## 4. Self-position — pro only

Where the station's **own** observations put it, against the position it
broadcasts: GPS-only, dual-frequency, single-point, every epoch, from
the core's solver (`src/core/spp.c`), the same one the GUI and the
daemon run. The plan and the measurements are in
[`station-self-position.md`](../../design/work-items/station-self-position.md), P7.

- **Figures, no verdict**, from the core's report over the tier-2
  window: epochs solved, mean offset E/N/U on one line, scatter about
  the mean, worst code residual, median apparent motion. No threshold
  for a station's own scatter has been established from evidence.
- **The state is the core's** — solved, gathering, not computable
  (single-frequency) or not solved — so the phone cannot disagree with
  the GUI about which applies.
- **The plot** is east/north about the ARP, the ARP fixed at the
  centre: self-centring would hide the offset. Ring radius by the
  desktop's 1-2-5 rule. Drawn in the shared polar frame since
  2026-10-21 (*The polar frame*, below) — the sky view's size and place,
  where it was square at the screen's full width before — with its rings
  labelled on the north line; the mean and the latest epoch on top.
- **The legend draws each mark with the plot's own function**, so it
  does not rest on colour.
- **The latest epoch** is one line under the plot, labelled as a
  diagnostic; *REVERSED* appears only when the station's receiver
  encodes its range rates backwards.

The solve runs in free as well — a measurement is never gated by
edition — but nothing draws it there.

## 5. Reference position — pro only, inside the network-RTK check

Where the service's reference position stands relative to the rover,
and where it has been this run: the desktop's VRS monitor, redrawn.
**Since 2026-10-21 it is part of the network-RTK check**, not an
analysis tab of every run (`design/appGuiReview.md`, R11): the check
gives its answer at the gate, then watches the reference position until
Stop, and during and after it Analysis opens this screen alone. The
other views describe a base that is not moving; this one is about a
rover.

- **The plot**, in the shared polar frame: the rover at the centre;
  each distinct reference position a *station*, at its true bearing and
  distance, with a colour and a letter (A, B, …) fixed at first sight
  for the whole run (`ArpTrail.offerDot`); the station in use larger,
  joined to the rover by a red line, the baseline (R7); earlier stations
  joined to nothing. The rover's **track** for the whole run, from the
  phone's fix only, a point per 10 m and thinned rather than cut at
  50 000, each stretch in the colour of the station that served it (R8).
  The last five minutes of distance as a strip chart beneath.
- **The distance's colour is A3's verdict** from the report, the core's
  judgement of that distance — and only where A3 and the line measure
  the same distance: the phone's fix sent by live GGA, or the set
  position with no fix. Otherwise neutral (R11.6). Until 2026-10-21 the
  app coloured it by bands of its own, 5 and 50 km, which contradicted
  A3 (26 km amber beside an A3 PASS).
- The hand-over count is worded by what the service *is* — a network's
  moves are its job, a fixed base's are a fault — and the largest jump
  appears once there has been one.

It was a hub card with a detail screen until 2026-10-12, then an
analysis tab of every pro run
([`handover-on-the-phone.md`](../../design/work-items/handover-on-the-phone.md), H8),
and since 2026-10-21 the network-RTK check's.

## The polar frame — every polar plot alike

Sky view, Self-position and Reference position are drawn in one frame,
`PolarCanvas` in `Views.kt` (`design/appGuiReview.md`, R6, R12): the sky
view's geometry at a fixed size — a square at the screen's width, the
circle 18 dp inside it for N E S W outside — with the dashed axes, and
each ring labelled where it crosses the north line, just right of it,
on a patch of the page, drawn last. The legend is the sky view's row
under the plot. The size is a rule, not the height a page leaves over:
three plots under three headers were three sizes (about 720, 850 and
490 px across on a 923-px screenshot, 2026-10-21) for exactly that
reason, and a page whose words and plot do not fit now scrolls. The
mountpoint map labels its rings on the north line too.

## Every screen's summary and ⓘ

Each analysis screen and the hub open with a one-line summary and an ⓘ
that opens the screen's explanation in a dialog (`SummaryRow`,
`HelpDialog` in `Dialogs.kt`; R1). The texts are in
`res/values/strings.xml`, reviewed by the author line by line
(`design/appGuiReview.md`, *R1 texts*); the template requires a summary
and help from every analysis screen, so none can come without them.

## What the stream itself decides about these plots

**The station's message format sets the resolution of every C/N0 view**,
and it varies by a factor of sixteen:

| Messages | C/N0 field | Resolution |
|---|---|---|
| MSM4, MSM5 | DF403, 6 bits | **whole dB-Hz** |
| MSM6, MSM7 | DF408, 10 bits | 1/16 dB-Hz |
| 1002/1004, 1010/1012 | DF015/DF045, 8 bits | 1/4 dB-Hz |
| MSM1-3 | none | not carried at all |

This is not a detail of the decoder. It reaches the screen:

- **The C/N0-versus-elevation plot bins in whole decibels**, because
  that is the coarsest a stream delivers. Finer bins looked better on an
  MSM7 station and drew **horizontal white lines** through an MSM4 one:
  with half-decibel cells, a stream that only ever reports whole
  decibels can fill only every second row, and the plot shows a blank
  row between every filled one. Observed on
  `caster.centipede.fr/NEAR4` (MSM4) while `ntrip.kadaster.nl/APEL00NLD0`
  (MSM7) looked perfect — which made a property of the *station* look
  like a fault in one edition of the app.
- **An MSM7 plot is denser than an MSM4 plot of the same station**, and
  neither is more correct. A sixteenth-of-a-decibel stream spreads its
  samples across many rows; a whole-decibel stream stacks them into few.
  Read the shape of the curve, not the number of marks.
- **MSM1-3 produce no C/N0 view at all** — Signal quality is empty and
  the elevation plot stays at zero samples. KPI 6 says so in those
  words rather than blaming the station.

The general rule, and the reason this section exists: **when two runs
disagree, compare the message types before suspecting the app.** Two of
the three defects found in one afternoon's device testing were a station
difference wearing an app difference's clothes.

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
- The ephemeris side-stream stays, as one of pro's sources — now the
  fallback rather than the first answer.
- The free edition no longer needs an ephemeris caster configured at
  all, which removes the largest piece of setup from the free flow. On a
  station that broadcasts its own orbits it needs no file either.

## The mountpoint list — sorted by the core's distance

*Browse mountpoints…* measures from the map's centre — the phone where
it has a fix, the set position otherwise, and it says which — so each
row carries the core's distance and bearing and the list sorts on them
(GH#14): **Nearest** first by default, or **A–Z** / **Z–A** by name,
applied to what the filter leaves, remembered while the app runs. An
entry with no usable position reads "no usable position" and goes
**after** every one that has one, never as a distance of nought; on
onocoy those are the NRBY services and the ephemeris products. Both
editions: the order is presentation, not measurement.

## The mountpoint map — a picker, not an analysis view

Every mountpoint of the caster on one screen, opened from *Map
mountpoints…* beside *Browse mountpoints…* on the hub, before a run
(`MountMap.kt`; plan and measurements in
[`mountpoint-map.md`](../../design/work-items/mountpoint-map.md), M6).
It answers *which base is near me, and which way?* once, the way the
list answers *which one?*, so it lives beside the list and not among
the analysis tabs, which watch a run.

- **Projection: azimuthal equidistant about the centre** — the phone
  where it has a fix, the set position otherwise, and the header says
  which. In it a place's distance and bearing from the centre are true
  everywhere on the screen, so the marks are placed by the **core's**
  distance and bearing (`bridge_sourcetable_to_json`) and the rings,
  1-2-5 apart, mean what they say. Outlines further out stretch; the
  header says that too. With no usable centre the map centres on the
  mountpoints and gives no distances.
- **Marks:** the kind by colour and shape — fixed base (circle), network
  service (diamond), product with no station (triangle), unknown
  (square) — the core's classification, with its reason in the balloon.
  Entries with no usable position are counted, never drawn.
- **Under them:** Natural Earth 1:50m land and borders from the app's
  asset; nothing fetched. Rendered to an image off the main thread when
  the view rests, so a pan with 5900 marks stays in reach of a phone.
- **Tap:** a balloon with the mountpoint, identifier, format, systems,
  network, kind and why, distance and bearing, and whether it expects a
  GGA. Pro selects it there, by the same path as a list row; free is
  told what pro adds.
- **A list that did not come** is said in the app's failure sentences —
  silence, a refusal, a closed connection — and a list cut short is
  asked for once more and never drawn as the whole caster.
