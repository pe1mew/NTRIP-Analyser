# Every mountpoint on a map — plan

**Asked for** by the author (2026-10-11): show all of a caster's
mountpoints on a map in the Windows GUI, and optionally in Android pro.

**Status:** plan only. Nothing is built. The decisions in
[§ Decisions](#decisions-open-until-the-author-confirms) are open until
the author confirms them.

## What it answers

A sourcetable is a list of names. Most of the questions someone asks of
it are about places:

- *Which base is nearest to my site?* The list already has a distance
  column (`gui/gui_parsers.c`). It gives a number, but not a direction,
  and it says nothing about the station on the far side of a ridge
  that sits 2 km further away.
- *Where does this caster have coverage, and where is the gap?* A list
  cannot answer this at all.
- *Is this entry's position believable?* Positions at 0,0, a Dutch
  station placed off the coast of Africa, and two mountpoints stacked
  on one point are obvious on a map and invisible in a column of
  numbers to two decimals.

## This is not item 4.2 coming back

[`todo.md` §4.2](../todo.md) dropped a map *widget* because it had
nothing to show: one fixed base is a stationary dot for the whole run,
and "a map earns its place by showing movement or **spatial
relationships**". A map of every mountpoint shows spatial relationships
and nothing else. It is also used the way
[`editions.md`](../../android/design/editions.md) says a map should be
used, as a picker: it answers "which one?" once, before a run, and is
closed during the run.

4.2 stays dropped. This plan needs its own todo item (M5).

## Constraints already decided elsewhere

| Constraint | Source | What it means here |
|---|---|---|
| The map is somebody else's: no embedded map SDK, no tiles fetched by this app | `editions.md` "Where the fixed position comes from"; `MapPick.kt` header | On Android, the app fetches no tiles. On the desktop, the browser fetches them, as `OnMapPick` already does |
| No threshold, verdict or rule in a frontend | `CLAUDE.md` | How a mountpoint is classified (fixed base or network) and whether its position is plausible are decided in `src/core`. Today the keyword half of the classification is in `gui/gui_events.c` `ClassifyStation`, and it moves to the core |
| Never gate a measurement by edition | `CLAUDE.md` | A map is a view of the sourcetable, not a measurement, so gating it in the UI is allowed. The figures behind it (distance, bearing, plausibility) come from the same core function in every edition |
| The sourcetable is hostile input | the hostile-frame tests; `feature-matrix.md` | Every field that reaches an HTML page is escaped by one tested function in the core. A mountpoint named `</script><script>…` must stay text |
| Information free, workflow paid | `editions.md` "Why the sourcetable is viewable but not selectable" | Tapping a mountpoint on the map to use it is the pro workflow (`SOURCETABLE_SELECTABLE`). See decision D4 for free |

## Decisions (open until the author confirms)

**D1 — GUI: a browser page, not a drawn window.** *Recommended.* The GUI
writes a self-contained Leaflet page to `%TEMP%` and opens it, exactly as
`OnMapPick` does: same Leaflet 1.9.4, same CARTO Voyager tiles. The
alternative is a Win32 window drawing positions on bare axes, which has
no coastline, no roads and no terrain. That would be a scatter plot of
the sourcetable, not a map. The browser page is the cheaper of the two
and the only one that answers the "on the far side of a ridge" question.

**D2 — Choosing a mountpoint on the page reaches the GUI through the
clipboard.** A `file://` page cannot call the GUI, and a local HTTP
listener so that it could is attack surface for a convenience, so that
is refused. The existing picker already uses the clipboard (copy, then
press `<<`). Proposed: the popup's *Use* button copies a token,
`ntrip-analyser:mount=<caster>:<port>/<mountpoint>`. While the map page
is open the GUI listens with `AddClipboardFormatListener`.

A token selects a row only when **all three** of these hold:

1. The caster and port match the caster whose sourcetable is loaded.
   The comparison ignores case in the host name, as the daemon's
   side-stream matching already does.
2. The name matches a mountpoint in that sourcetable.
3. It matches **exactly one** row. One caster should never list a
   mountpoint twice, but the sourcetable is hostile input. If the name
   matches two rows, the GUI selects neither, logs the name and the
   number of matches, and never quietly picks the first.

When all three hold, the row is selected exactly as a click on it would
select it. Any other text, and any token that fails a check, is ignored,
and the reason is logged for a token that fails a check. The clipboard
therefore cannot select anything the user could not already click.

**The token carries an identifier, never a value.** The position,
format details and `nmea` flag all come from the GUI's own parse of the
loaded sourcetable, not from the clipboard. Anything can write to the
clipboard, so the GUI only looks the name up. A map click and a list
click then give the same figures, because they read the same copy.
This is the reverse of the location picker, which returns `lat,lon`,
because there the position *is* the answer.

Check 1 makes switching casters safe even if the "listen only since the
last sourcetable load" bookkeeping in M4 fails. A page left open from
caster A cannot select a same-named mountpoint on caster B. The fallback,
if D2 is declined, is a manual step: the page copies the bare name and
the user finds the row.

**D3 — Android pro: a drawn plot, not a tile map.** *Recommended, and
the only option inside the existing decision.* Pro draws a Compose
Canvas centred on the phone's position (or on the configured fixed
position), with distance rings and north up, and each mountpoint
placed by bearing and distance. This is the same polar idea as the
self-position plot and the sky plot. Tap to use, as the sourcetable
list does in pro. It fetches nothing, so the Play data-safety answer
does not change. It is not a map in the tile sense, and the plan says
so plainly rather than calling it one. Two options were rejected:
osmdroid, for the reasons already given in `editions.md`, and a
WebView, which moves the tile requests into this app's process and
is the same objection by another route.

  *Optional second route, both editions:* **Share as GeoJSON** hands
  the mountpoints to whatever map app the user has. I have not checked
  which Android map apps open GeoJSON, KML or GPX from a share intent.
  M7 has to establish that on a device before any of it is written
  down as working.

**D4 — Free on Android.** `editions.md` argues that the information is
free and the workflow is paid. On that reasoning free would see the plot
but not be able to tap it, just as its sourcetable list cannot be
tapped. The author asked for pro only. The plan builds it behind a
`Features.HAS_MOUNT_MAP` flag, so either answer costs one line. **The
author's word decides it.**

**D5 — Distance on a sphere, no baseline verdict.** Distance and bearing
are great-circle figures (haversine, mean Earth radius), as the GUI's
distance column already computes them. On a sphere the error against
the ellipsoid stays well under 1 %, which is enough to rank candidates
by distance and not enough to survey with. Markers are **not**
coloured by "too far for RTK". A baseline limit would be a threshold.
I have not researched a defensible figure, and none is proposed. If one
is wanted later, it goes in `thresholds.c` with its basis cited, as
every other threshold does.

## What already exists, so the estimate is honest

- `sourcetable_parse()` in the core already gives latitude, longitude,
  network, NavSys and the `nmea` flag. The GUI, the CLI and the Android
  bridge all call it.
- **The STR `solution` field is not parsed.** It is field 12 in the
  STR layout the parser's own header quotes, and it marks a single base
  (0) or a network (1). It is the sourcetable's own statement of what
  the GUI's keyword list guesses at. M1 adds it and checks real
  sourcetables to see whether casters fill it in honestly.
- `haversine_km` is a `static` function in `gui/gui_parsers.c`, so the
  core has no distance function. `geo_distance_m` is used by
  `ClassifyStation`. M1 gives the core one function, and the GUI's copy
  is deleted.
- `OnMapPick` (`gui/gui_events.c`) already writes the Leaflet page,
  handles the temp-file write, and puts the attribution on screen.
- The Android `SourceEntry` (`Model.kt`) already carries `lat`, `lon` and
  `nmea`.

## Steps

### M1 — the core: kind, plausibility, distance

`src/core/sourcetable.{c,h}`, plus a small `geo` helper if the distance
function wants a home of its own.

- `SourcetableEntry` gains `int solution`: −1 if absent, otherwise 0 or 1.
- *(Proposed; the measurement changed it. See "M1 — as built" below.)*
  `sourcetable_kind(const SourcetableEntry*)` returns FIXED, NETWORK or
  UNKNOWN, with the reason as a string, using the evidence the
  sourcetable carries:
  - the `solution` field;
  - the `nmea` flag;
  - the keyword list that today lives in `ClassifyStation`.

  The keyword list moves here. The GUI's step 1 then calls this
  function, so the map and Stream Health cannot disagree about a
  station. Step 2, which compares the ARP with the GGA, stays where it
  is: it depends on the live stream, not on the sourcetable.
- `sourcetable_pos_usable()` (proposed as `sourcetable_pos_valid`;
  renamed, see "M1 — as built") rejects a position when:
  - it is 0,0;
  - it is out of range;
  - it is not finite.

  Nothing is clamped. A network entry's position is shown, but marked
  as the network's nominal point, not a station.
- `geo_distance_bearing(lat1, lon1, lat2, lon2, &km, &deg)`.

**Tests:** `test/test_sourcetable.c`, new. No test calls `sourcetable_parse` today, so the parser that three frontends share gets its first test here. It covers:
- truncated records and a missing `solution` field;
- a `solution` field holding text instead of a digit;
- positions at 0,0, at ±90, NaN, and `1e308`;
- keywords in mixed case;
- distance and bearing against hand-worked cases: due north, crossing
  the antimeridian, and a near-antipodal pair.

**Check against a live caster:** pull at least three real sourcetables
and record how many entries fill in `solution`, and whether the value
agrees with the keyword rule. If casters leave it blank, the keyword
rule stays the main signal. Either way the answer is written into this
plan.

`src/core` changes, so rebuild and test all four programs (`RUNBOOK.md`).

### M1 — as built, and what the casters said  *(2026-10-11)*

**The measurement.** Raw sourcetables from seven configured casters
(`ntrip-analyser -c <config> -r`), 8471 STR records, run through the
core's own parser by a scratch program:

| Caster | Entries | solution 0 / 1 | keyword in details or network | `nmea` set | position unusable |
|---|---|---|---|---|---|
| onocoy | 6123 | 6123 / 0 | 0 | 3 | 9 |
| Centipede | 1287 | 1287 / 0 | 0 | 2 | 0 |
| RTK2go | 769 | 769 / 0 | 0 | **769** | 16 |
| EUREF-IP | 223 | 211 / 12 | 0 | 0 | 0 |
| Kadaster | 61 | 58 / 3 | 0 | 0 | 0 |
| GEODNET | 6 | 6 / 0 | 0 | 5 | **6** |
| RFSEE | 2 | 2 / 0 | 0 | 0 | 0 |

No duplicate mountpoint names on any caster.

What the plan assumed, and what turned out to be true:

- **The `solution` field is always filled, and wrong in both
  directions.** All 15 entries saying "network" were checked:
  - 10 are ephemeris or SSR products: `BCEP00BKG0`, `BCEP00KAD0`,
    `RTCM3EPH`, `RTCM3EPH2`, `EUREF01/02`, `SSRA0x`;
  - 5 are physical IGS stations: Grasse, Toulouse, Ny-Ålesund,
    Oberpfaffenhofen, Santa Cruz.

  Meanwhile Centipede's `NEAR` and `NEAR4` (nearest-base services) and
  GEODNET's `AUTO*` (a network service) all say "single base".
- **The keyword rule never fired** on these casters: no keyword in the
  details or network field of any of the 8471 entries. Services are
  named in the *mountpoint name* instead: `NEAR`, `NEAR4`.
- **`nmea` is no evidence of a network.** RTK2go sets it on all 769 of
  its single bases.

**So the rule changed** from the one proposed above
(`sourcetable_kind`, documented in `sourcetable.h`). The first that
applies wins:

1. A keyword in the details or network field gives NETWORK. This is the
   GUI's rule, unchanged.
2. A keyword in the mountpoint name gives NETWORK. A trailing number is
   allowed (`NEAR4`), a letter is not (`MACO` and `TMAC` are towns).
3. A fourth kind, **PRODUCT**: types are advertised, and none of them
   comes from a station. That means no observations (legacy or MSM) and
   nothing describing a station (1005–1008, 1033, 1230). The
   descriptive messages count because 26 real bases listed only those,
   for example `1005,1033` or `1230(10)`. A first version that looked at
   observations alone called those 26 bases products.
4. `solution` 0 gives FIXED. It was wrong for 3 of 8456: `NEAR`,
   `NEAR4` (caught by rule 2) and GEODNET's `AUTO*` (not caught;
   position 0,0, so not drawn either).
5. Anything else is UNKNOWN, **including `solution` 1**, with the reason
   shown.

The resulting tally:

| Kind | Entries |
|---|---|
| Network | 2 (`NEAR`, `NEAR4`) |
| Product | 13, every one an ephemeris or SSR stream by name |
| Unknown | 5, the IGS stations above |
| Fixed | the remaining 8451 |

**Stream Health is unchanged.** The GUI's step 1 now calls
`sourcetable_network_keyword()`, the same list and the same whole-word
rule, moved. It does not use rules 2–5. The decision whether a
mountpoint *name* should switch Stream Health into network mode, and
therefore gate the VRS check, belongs to the station check. It is not
a side effect of M1.

**Found while moving it, and fixed in M1:**

- The GUI took the declared position for the ARP cross-check from the
  list's *display text*, which is rounded to 0.01° (about 1 km). The GUI
  now keeps the parsed entries (`AppState.stEntries`, each row's
  `lParam` is its index) and reads the position from them at full
  precision.
- When the sourcetable was fetched while opening a stream, the network
  field and declared position were never taken at all. Classification
  and the cross-check had nothing to work from on that path. They are
  now taken when the table arrives.
- The distance column measured from positions such as latitude 400.
  It now shows a dash for any position `sourcetable_pos_usable` refuses.

**Renamed after CI, 2026-10-12.** The function was first called
`sourcetable_pos_valid`, which is also the name of an `NsStatsSnapshot`
field that nothing fills yet (measurement-tiers phase 0).
`check_release.py` finds unfilled fields by whole-word search outside
`ns_stats.c`, so the function's name read as the field being filled,
and CI failed with "remove from SNAPSHOT_KNOWN_GAPS". The field is
still a gap. The function became `sourcetable_pos_usable` so that one
name stops meaning two things. Phase 0 will very likely fill the field
*with* this function's answer.

**Distance.** `src/core/geo.{c,h}`: `geo_distance_m` and
`geo_bearing_deg`. These replace the GUI's two private copies
(`haversine_km`, `geo_distance_m`). Metres, not the kilometres the plan
said: the ARP checks need metres. Near the antipode the haversine is
0.13 m short of half the circumference (measured, gcc `-O2`). The test
allows 1 m there and pins that the result is never NaN.

**Tests:** `test_sourcetable`, 21st in the suite. It includes the real
names and advertised-type lists from the sample that broke a first
draft.

**Also noticed, not fixed:** `ntrip-analyser --help` says `-r` is to be
used "with -m", but combining the two is refused ("Cannot combine
action flags"). `-r` alone prints the raw table.

### M2 — the core: the page's data, escaped once

`sourcetable_to_geojson(entries, n, caster, port, origin_lat, origin_lon, buf, cap)`
writes a GeoJSON FeatureCollection. Each feature carries:

- the mountpoint, identifier and network;
- the selection token for D2, built here so the page never assembles
  one itself;
- NavSys and format;
- the kind and its reason;
- distance and bearing from the origin, when an origin is set.

Entries with an invalid position are **left out of the features** and
counted in a `"omitted"` member, so the page can say "37 mountpoints
have no usable position". They are never drawn at 0,0. This is the same
rule as "a satellite with no orbit is absent".

The escaping covers the cases that break a page, not only the cases
that break JSON: `<`, `>`, `&`, U+2028, U+2029 and control characters
are all written as `\uXXXX`. The output then cannot close a `<script>`
block, whichever way a frontend embeds it.

**Tests (hostile):**
- a name containing `</script>`;
- quotes and backslashes in every field;
- a raw U+2028;
- an embedded NUL;
- a buffer too small: the output must be truncated *with the return
  value saying so*, never a half-written string that still parses.

The test parses the output back and compares it field by field.

### M3 — the GUI: View → Mountpoint map

- A menu item, enabled when a sourcetable is loaded. It writes
  `ntrip_mountpoint_map.html` to `%TEMP%` and opens it.
- The page is built on the `OnMapPick` template:
  - circle markers, coloured by kind (fixed / network / product /
    unknown; see "M1 — as built");
  - the origin marker taken from the Lat/Lon fields, when they are set;
  - `fitBounds` on the valid entries;
  - a popup with the fields above, and the distance and bearing when
    there is an origin;
  - a header line naming the caster, the count drawn and the count
    omitted.
- **Large casters:** some public casters list thousands of mountpoints.
  The page uses Leaflet's canvas renderer (`preferCanvas`) and no
  clustering plugin to begin with. **Measure** load time and page size
  on the largest sourcetable available before deciding whether
  clustering is needed. I have not measured it.
- The `OnMapPick` page and this one load Leaflet from unpkg without
  Subresource Integrity. Add `integrity` hashes to both pages, because
  this page now carries third-party text.
- Legend and attribution are on the page. Both are checked by opening
  the file, not by reading the code that writes it.

### M4 — the GUI: choosing from the map (D2)

The clipboard listener, active only while a map page has been opened
since the last sourcetable load. A token is accepted only under the
three checks in D2:

1. the caster and port are the loaded caster's;
2. the name is in the loaded table;
3. the name matches exactly one row.

Selecting it uses the same path as a click on the ListView, including
what that click already fills in: the format details and the position.
The log line says where the selection came from. A refusal is logged
with its reason: another caster, not in the table, or ambiguous with
the number of matches.

**The token is built and read in the core, not in the GUI.** M2 writes
it into each GeoJSON feature, so the page copies a string it did not
put together itself. Next to `sourcetable_to_geojson` goes
`sourcetable_match_token(token, caster, port, entries, n)`, which
returns the one matching row index or a refusal code. Clipboard text
is hostile input, and GUI code is not covered by `test_all`, so the
parsing goes where it can be tested.

`test/test_sourcetable.c` tests it with:
- a well-formed token;
- a caster that differs only in case;
- the wrong port;
- the wrong caster;
- a name that is not in the table;
- a duplicate name, which must be refused, never resolved to the
  first match;
- a token with no `/`;
- an empty name;
- a 64 KB token;
- a token with an embedded NUL;
- a mountpoint name that itself contains `/` or `:`. M2's writer and
  this reader must agree on how that is written. Decide the escaping
  and test it from both ends.

**Test on the GUI itself** (promoted rule, 2026-10-10):
- open the page and choose a mountpoint, then confirm the row is
  selected and the stream opens;
- copy a token for a mountpoint that is not in the table, then
  confirm nothing changes and the log gives the reason;
- open the page for caster A, load caster B's sourcetable, then use a
  token from A's page for a mountpoint name B also lists. Confirm it
  is refused as "another caster". This checks the bookkeeping and
  check 1 separately: break one and the other still refuses;
- feed a sourcetable that lists one name twice, then confirm the token
  is refused as ambiguous;
- copy ordinary text, then confirm nothing changes and nothing is
  logged.

### M5 — say so (desktop)

- `docs/gui.md`: the menu item, what the colours mean, the clipboard
  route.
- `design/gui-design.md`: a new section, as built.
- `design/todo.md`: a new item that cites this plan and keeps the
  difference from 4.2 explicit.
- `design/feature-matrix.md`: a "Mountpoint map" row:
  - ● GUI;
  - ○ CLI;
  - ○ daemon;
  - ⋯ or ○ for Android, following D3 and D4.
- `changelog.md`, with the measurement from M1 (how many casters fill
  in `solution`) and from M3 (load time on the largest table).

M1–M5 can be merged as one branch to `main`.

### M6 — Android pro: the bearing-and-distance plot (D3, optional)

*Optional, as asked. It starts only after the author confirms D3 and D4,
and after pro is in production (the same gate as self-position P7).*

- The bridge exposes `sourcetable_kind`, `sourcetable_pos_usable` and
  distance and bearing per entry. Kotlin computes none of them.
- A Compose Canvas screen opened from the sourcetable dialog:
  - origin at the centre;
  - distance rings at 1/2/5×10ⁿ km, the same ring rule as the
    self-position plot;
  - north up;
  - the kind shown by both colour and shape (colour alone fails
    colour-blind readers);
  - entries with no position counted, not drawn.
- Tapping an entry in pro does what tapping the list row does: it
  takes the mountpoint, its position and its `nmea` flag. In free, if
  D4 says free sees the plot, tapping does nothing, as in the list.
- Gate: `Features.HAS_MOUNT_MAP` in both `Features.kt` files. If it is
  a panel, it goes in the `Registry.kt` lists.
- Verify on a device against a real caster: a station known to be
  north-east of the phone is drawn north-east.

### M7 — Android: share as GeoJSON (optional, D3 second route)

Only if M6 has shipped and the author wants it. `Share.kt` already
shares files. First find out on a device which installed map apps open
the file, and record that here. Only then decide whether to build it.

### M8 — say so (Android)

- `android/design/views.md`;
- `android/design/editions.md`: the table, and the "map is somebody
  else's" section, which this plot keeps to;
- the feature matrix;
- the Play listing, **only if** the listing describes the feature. The
  free listing's sentence about picking positions on a map stays true.

## What this will not claim

- That a mountpoint is *at* its sourcetable position. The position is
  what the operator registered. A live stream's 1005/1006 is the
  evidence, and KPI 3 and the Stream Health cross-check already judge
  it.
- That the nearest base is the right base. Distance is one input. The
  station check is the answer.
- That a network entry's point is a station.

## Out of scope, stated

- A map in the CLI or the daemon. The CLI could write the same HTML
  file with one flag, since M2 makes that cheap. It is not proposed
  until someone needs it.
- Showing live stream health per marker. That would mean connecting to
  every mountpoint, which is a different product.
- Offline or cached tiles, anywhere.

## Process

Same as self-position:
- one branch;
- each step leaves the build green and the tests passing;
- `src/core` steps re-run all four programs;
- the author commits.
