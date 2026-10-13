# Every mountpoint on a map — plan

**Asked for** by the author (2026-10-11): show all of a caster's
mountpoints on a map in the Windows GUI, and optionally in Android pro.

**Status:** M1–M5 (desktop) built and on `main`, 2026-10-12. D3 and D4
decided by the author 2026-10-13, changing M6 into a zoomable map on
the phone (see "D3 and D4 — decided"). M6 not started.

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

### D3 and D4 — decided  *(author, 2026-10-13)*

**D3: the plot, with Natural Earth under it.** Of three options put to
the author — the bare polar plot, the polar plot over Natural Earth,
and share-as-GeoJSON only — the second. The outlines are the GUI's
own: Natural Earth 1:50m, public domain, carried in the app, so the
app still fetches nothing and the data-safety answer is unchanged. The
objection to osmdroid and a WebView was to *fetching* tiles; this
fetches none.

**What it costs, measured:** `gui/basemap_ne50m.js` is 1 184 613 bytes
and **437 424 compressed** (zlib level 6, which is what an APK's
asset compression is), against 13.4 MB for each debug APK today:
about 3 %. 1:110m was not measured; at this cost it is not needed.

**D4: free sees the map and cannot select from it**, as with its
sourcetable list — the information free, the workflow paid
(`editions.md`).

**The author's shape for it:**

- On the hub, *Browse mountpoints…* takes half the width, and a new
  *Map mountpoints…* sits beside it on the same row.
- *Map mountpoints…* opens a **full-screen map** that can be zoomed and
  panned, and closed.
- Tapping a mountpoint shows its information in a **balloon**, in both
  editions.
- In pro the balloon has a **Select mountpoint** button.
- In free the balloon carries a brief line saying what pro adds.

So the gate is the one the list already has,
`SOURCETABLE_SELECTABLE`; no `HAS_MOUNT_MAP` flag is needed, since both
editions show the map.

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

**Also noticed:** `ntrip-analyser --help` said `-r` is to be used "with
-m", but combining the two is refused ("Cannot combine action flags").
`-r` alone prints the raw table. *Fixed 2026-10-12:* the help now
describes `-r` as its own action. The comment in `main.c` that claimed
`-m -r` and `-m -d` were allowed was corrected too: `claim_action()`
has never allowed either.

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

### M2 — as built  *(2026-10-12)*

`src/core/sourcetable_map.{c,h}` holds both ends of the map route.
`test/test_sourcetable_map.c` is the 23rd test.

- **Out:** `sourcetable_to_geojson()`.
  - **Output:** a FeatureCollection whose top-level members are
    `caster`, `port`, `origin`, `total` and `omitted`. Each feature
    carries mountpoint, identifier, network, country, format, NavSys,
    `nmea`, `kind`, `kind_why`, `token`, `distance_m` and
    `bearing_deg`.
  - **Sizing:** like snprintf, and all or nothing. Every capacity from
    1 to the full length is tested to give an empty string, never a
    prefix.
  - **Escaping:** `<` `>` `&`, control characters, DEL and U+2028/9
    are all written as `\uXXXX`.
  - **Invalid UTF-8** becomes U+FFFD byte for byte, including
    overlong forms and surrogates. It is never guessed to be Latin-1.
- **Back:** `sourcetable_make_token()` and `sourcetable_match_token()`.
  - **Encoding:** caster and mountpoint are percent-encoded outside
    `A-Z a-z 0-9 - . _ ~`. This settles the open question from M4: a
    name containing `/` or `:` (or an IPv6 caster) cannot be misread,
    and a bare reserved character in a token is malformed rather than
    guessed at.
  - **Refusals:** NONE (ordinary text, not an event), MALFORMED,
    OTHER_CASTER, NOT_LISTED, AMBIGUOUS, each tested by name.
  - **Matching:** the mountpoint is compared exactly, the caster
    ignoring case. A token is made from the exact entry, so a case
    difference means another entry. The M1 sample had no names that
    differed only in case.

**What the casters send.** The 8471 records of the M1 sample hold
exactly one non-ASCII name (`Tombebœuf`, valid UTF-8). None holds a
control character, invalid UTF-8, or any of `< > & " \`. The escaping
is for the caster that is not polite. The test pins that `Tombebœuf`
passes unchanged.

**Two faults in the first draft, found by its own test and by mutation:**

- **The writer formatted pieces into a 64-byte scratch buffer and cut
  them silently.** The feature header came out as
  `[5.9"mountpoint"`, and the test crashed reading the unparseable
  result. Long fixed text now goes through `put_s`, and a formatted
  piece that does not fit marks the output broken. The whole document
  is then refused (-1), never emitted cut.
- **A mutation run** built the test against 13 deliberately broken
  copies of the module. Three survived at first:
  - **an explicit NUL check:** redundant, because the decoder already
    refuses NUL. It was removed rather than kept as untested code.
  - **the length limit:** the 64 KB case was malformed for another
    reason. The test now pads a *valid* token past `ST_TOKEN_MAX`.
  - **the kind:** every test entry was a fixed base. The test now has
    a network, a product and an unknown.

  All 13 mutants are now caught.

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

### M3 — as built  *(2026-10-12)*

**D1 changed: no tiles. The map is drawn over Natural Earth (author,
2026-10-12).** Opening the first draft showed every CARTO tile
watermarked *API KEY REQUIRED*. curl gave the same tile for any
referer. CARTO's own page says a key has been required from everyone
since about 2026-09-23: free for non-commercial use up to 5M requests
a month, requested through a form. The OSM Foundation's tile server is
no way round it: a request without a `Referer`, which is what a page
opened from a file sends, gets *403 Access blocked* (fetched). **The
shipped location picker (`OnMapPick`, **Map**) has the same fault
today.**

Two routes were weighed for the basemap:

- **The author's PocketAPRS maps** (`maps/`, 2001–02): Palm databases
  in an undocumented delta-coded polyline format. They can be decoded,
  but no licence or provenance was found, and they cover Western
  Europe and a coarse world only.
- **Natural Earth**, chosen. Its terms say *"in the public domain"*
  and *"Crediting the authors is unnecessary"*. It is documented and
  global.

What was built:

- **The data:** `tools/make_basemap.py` takes release v5.1.2 (pinned)
  1:50m land plus admin-0 boundary lines. It rounds coordinates to 3
  decimals and writes `gui/basemap_ne50m.js`: 1421 land polygons, 393
  border lines, 1 184 518 bytes, byte-identical on a second run.
  Disputed lines, lines of control and indefinite boundaries are drawn
  dashed.
- **In the GUI:** the file is compiled in as an RCDATA resource, so
  the executable grew to 3.2 MB. It is written beside the page as
  `ntrip_basemap.js`. Nothing under the map is fetched; only Leaflet
  is, with SRI.
- **Credit**, although none is required: Natural Earth's own wording
  *Made with Natural Earth*, linked, in the map's attribution. Also a
  section in `THIRD-PARTY-NOTICES.txt` (generated by
  `make_notices.py`, which reads the version from the data file) and
  rows in `docs/licences.md`.
- **The page writer** is `gui/gui_mountmap.c`, standard C I/O only. A
  scratch harness produced the GUI's exact page from the M1 sourcetables
  for measurement in a browser.
- **The menu command** is `OnMountMap` in `gui_events.c`: View →
  Mountpoint map. With no sourcetable loaded it says so rather than
  opening an empty map. The caster recorded at fetch time
  (`AppState.stCaster/stPort`) is the one named, not whatever the field
  says now.

**Measured** in the built-in browser, from the M1 sourcetables:

| Caster | Entries drawn | Script | Page load |
|---|---|---|---|
| onocoy | 6114 of 6123 (9 counted, not drawn) | 130 ms | 0.65 s |
| Kadaster | 61, spanning to the Caribbean Netherlands | 62 ms | 0.64 s |

No clustering is needed. Leaflet loaded, so the SRI hashes are
accepted, which matters on `OnMapPick` too. No console errors.

**Three faults found by opening the page:**

1. **The map was created while its container was 0 px high.** It was
   positioned below the bar by script, afterwards, so `fitBounds`
   opened at maximum zoom on empty sea. Now a flex layout, with the bar
   filled before the map is made.
2. **At zoom 11, two stations 25 km apart sat on plain land** with no
   coast in sight: 1:50m has nothing to show at that scale. The opening
   zoom is now capped at 8, with a metric scale bar. The page says the
   outlines are "good for a country, coarse for a town".
3. **A hostile mountpoint name ran script on hover.** A made-up
   sourcetable entry named `<img src=x onerror="window.pwned=1">` set
   `window.pwned`. The popups were safe (built with `textContent`), but
   `bindTooltip(p.mountpoint)` passes a string, and Leaflet sets a
   string tooltip as innerHTML. The core's escaping kept the page
   source clean (no raw `<` anywhere); the hole was in the JavaScript,
   after decoding. The tooltip now gets an element. Re-tested: hover
   and click, `window.pwned` undefined, no `<img>` in the page.

**Not built here, M3b:** an optional tile source the user configures
with their own key, used by both pages, and the fix for `OnMapPick`.

### M3b — the user's own tile source  *(2026-10-12)*

**Asked for** by the author: *"optional add a feature where the user
adds its own key for OSM or CARTO."* One correction was made before
building. The OSM Foundation's tile server has no key to add; it
refuses our pages for want of a `Referer`. The feature is therefore
*a tile URL template with the user's key in it*, which covers CARTO
and any provider of the same shape.

What was built:

- **Core** (`sourcetable_map.{c,h}`):
  - `map_tile_url_usable()` refuses an empty URL, a non-`https://` URL
    (the key would travel in clear), one missing any of `{z}` `{x}`
    `{y}`, any placeholder Leaflet does not fill (it throws on every
    tile), and any white space, quote, backslash, `<` or `>`. The
    first reason found goes to the dialog.
  - `map_json_quote()` puts the URL and the attribution into the page
    with the same script-safe escaping as the GeoJSON.
  - Both are in `test_sourcetable_map`, now 82 checks.
- **GUI, Tools → Map tiles…**: the project's first dialog (`DIALOGEX
  IDD_MAP_TILES`). It holds the URL template and the attribution, with
  CARTO's template as the example, exactly as CARTO's key page gives
  it: `…/rastertiles/voyager/{z}/{x}/{y}.png?key=YOUR_KEY`, read
  2026-10-12. OK refuses an unusable URL with the reason; Clear removes
  both. Both values are stored under `HKCU\Software\NTRIP-Analyser` (`MapTileUrl`,
  `MapTileAttribution`), beside the thresholds path:
  - not in a config file, because a key is personal while config files
    describe a caster and get shared;
  - never in the log, because the URL carries the key.

  A saved URL that has stopped being usable falls back to the outlines,
  and the log says why without naming the URL.
- **Both pages share `drawBase()`**: tiles when set, otherwise the
  Natural Earth outlines. The attribution goes through `textContent`
  before Leaflet, which sets attribution as HTML.
- **The location picker** moved from a `snprintf` into an 8 KB buffer
  in `OnMapPick` to `MapPickWritePage()` in `gui_mountmap.c`.
  - **Without tiles** it opens at zoom 8 over the outlines, with the
    note *"No tile source is set, so this shows outlines only: too
    coarse to find a mast"*.
  - **With tiles** it opens at zoom 13, as before.
  - **A NaN or out-of-range start** opens at 0,0 rather than writing
    `nan` into the script.
  - **Click to copy** is unchanged.

**Checked in a browser** with the harness pages, using a deliberately
hostile attribution (`<img src=x onerror=…>`):

| Page | Result |
|---|---|
| Mountpoint map with tiles | requests go to `basemaps.cartocdn.com/…?key=…`; the attribution is shown as literal text; `window.pwned` stays unset |
| Picker without tiles | outlines and the note |
| Picker with tiles | zoom 13 |
| Picker with a NaN start | 0,0, no errors |

A click on the picker updated the coordinates and showed "Copied to
clipboard!". Whether text reaches the Windows clipboard, and the
dialog itself, are for the author's check on the GUI.

**After the author's first look at the dialog (2026-10-12):** *"add
default/example url for carto so that api key can easily be added;
make links clickable and selectable so they can be copied."*

- **With nothing saved, the dialog opens pre-filled** with CARTO's
  template ending in `?key=`, the caret placed after it, and CARTO's
  attribution. A **CARTO** button refills both.
- **The core now refuses a URL ending in `=`**, so the template cannot
  be saved without the key. That is pinned in the test.
- **The key page is a SysLink** that opens in the browser.
  `ICC_LINK_CLASS` was added to the GUI's `InitCommonControlsEx`.
- **The example URL and attribution are read-only edits**, selectable
  and copyable, where they were static text.

**Found while doing it:** the GUI is an ANSI program, so a typed `©`
is the single byte 0xA9. In a UTF-8 page the core rightly turns that
byte into U+FFFD, which would have shown CARTO's own attribution as
`� OpenStreetMap contributors � CARTO`. The attribution is now
converted from the ANSI code page to UTF-8 (`AnsiToUtf8`) before it
is quoted. The URL needs no conversion, since `map_tile_url_usable`
admits ASCII only.

**Verified by the author with a real CARTO key (2026-10-12).** Streets
and place names are shown, so the key is accepted from a page opened
from a file. The attribution shows correct © signs, and Centipede's
1295 entries include the two `NEAR` services.

**One world, not several.** On a wide window the tiles repeated
sideways while the stations, drawn once, did not, which left a second
North America with no stations on it (author's screenshot). The tile
layer now has `noWrap` and the view is held to one world with
`maxBounds`. Two things were measured on the way:

- **`noWrap` alone still requested tiles off the world's edge**, x =
  −2…5 at zoom 2, 16 of 32 requests. Leaflet's documentation pairs
  `noWrap` with `bounds` for exactly that. With `bounds` set, exactly
  the 16 tiles of the zoom-2 world are requested.
- **Raising the least zoom until the world fills the window's width**
  was tried and dropped. At 1900×950 it forced zoom 3, too tall to
  show Centipede's stations from Iceland to Réunion at once. Beside
  the single world there is now plain sea colour, and the opening view
  shows every station drawn.

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

### M4 — as built  *(2026-10-12)*

- **The page:** each popup has *Use this mountpoint*, which copies the
  feature's `token` as the core wrote it. The page never assembles
  one.
- **The GUI:**
  - `AddClipboardFormatListener` from opening a map page until the
    next `KeepSourcetable`, which calls `MapStopListening`; also
    removed on `WM_DESTROY`.
  - `WM_CLIPBOARDUPDATE` → `MapTokenFromClipboard` reads `CF_TEXT`,
    at most `ST_TOKEN_MAX + 1` bytes. The GUI's own copies are
    skipped.
  - `sourcetable_match_token` decides. NONE (ordinary text) is
    silent. Every refusal is logged with its reason: another caster
    ("a map page left open from an earlier one?"), not listed,
    ambiguous with the row count, or unreadable.
- **A match** finds the row by the index its `lParam` carries, so a
  sorted list still selects the right entry. It selects and scrolls
  to the row, then calls `UseMountpointRow`, which is now also what a
  double-click calls: one path, so the map can set neither more nor
  less than a click. The log says "(chosen on the map)". The taskbar
  button flashes, because Windows will not let the GUI take the
  foreground from the browser.

**Found by testing the page: copying could claim success it did not
have.** The location picker's copy routine, carried over unchanged
since before this plan, used `navigator.clipboard` whenever it existed
and fell back only when it did not. In the browser pane the write was
refused (`NotAllowedError: Write permission denied`), and the page
still said "Copied to clipboard!". There is now one `copyText(t, done)`
for both pages:

1. `execCommand('copy')` first, which answers at once;
2. the clipboard API second;
3. `done(false)` when neither wrote.

The map then says *"The browser would not copy. Copy this with
Ctrl+C:"* and offers the token in a selected box. The picker says to
select the numbers.

**Checked in a browser** with the hostile sourcetable:

- **A successful copy** captured exactly the core's percent-encoded
  token for the `<img onerror>` mountpoint on caster `evil"host<`.
- **With both copy methods refused**, the fallback text and the
  selected box appeared.
- **Nothing ran**: `window.pwned` stayed unset throughout.

**Still to be seen on the GUI** (the plan's M4 test list): a token
selects the row and sets the Mountpoint field; a token for an
unlisted name is refused with its reason; a page from caster A after
loading caster B's table is refused; ordinary text does nothing.

A duplicated name cannot be staged live, because no configured caster
lists one (M1 measured 0 duplicates in 8471 records) and the GUI
cannot load a sourcetable from a file. `test_sourcetable_map` pins
that case.

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

**M5 as built (2026-10-12).** Everything above went straight to
`main`, a commit per step:

- **`docs/gui.md`**: feature list, the *Mountpoint Map*, *Map and <<*
  and *Map tiles* sections (including what each marker means and does
  not), both menus, and the source files.
- **`design/gui-design.md` §17** (Reference moved to §18).
- **`design/todo.md` 2.6**, keeping 4.2 dropped and saying why this
  is not it.
- **`design/feature-matrix.md`**: a *Mountpoint map* row (GUI ●, pro
  ⋯), and the *Position from map* row corrected for the desktop's
  tiles.
- **`changelog.md`**: four entries, each with its measurement.

`docs/licences.md` and `THIRD-PARTY-NOTICES.txt` were done in M3 and
M3b.

### M6 — Android, both editions: the map  *(replanned 2026-10-13)*

Replaces the plan below it, which predates D3 and D4. Built on a
branch; it ships after pro is in production, the same gate as P7.

**M6a — the bridge says what each entry is.** Kotlin computes none of
it. `bridge_sourcetable_json` gains, per entry, from the core:
`kind` and `kind_why` (`sourcetable_kind`), `usable`
(`sourcetable_pos_usable`), and `distance_m` / `bearing_deg`
(`geo.c`) from an origin passed in — the phone's position in pro, the
set position otherwise. `sourcetable_map.c` and `geo.c` are already in
the Android build. The JSON writing is split from the network fetch
(`bridge_sourcetable_to_json(raw, origin, …)`), so it is tested
offline in `test/` with the M1 hostile names, which the fetch never
allowed.

**M6a — as built  *(2026-10-13)*.** `bridge_sourcetable_to_json(raw,
origin, out, cap)` writes the document; `bridge_sourcetable_json` now
fetches, then grows its own buffer from 256 KB to at most 32 MB,
writing again from the same table, and returns it allocated. Each
entry gains `network`, `kind`, `kind_why`, `usable`, `distance_m`,
`bearing_deg`; `SourceEntry` decodes them. The app's two places that
took a position from an entry (*From station*, and picking from the
list) now ask `usable` instead of testing for 0,0 themselves.
`test_bridge_sourcetable`, the 24th test: every field, a hand-worked
distance, three ways to have no origin, three positions that are no
place, hostile text parsed back with cJSON, and every buffer size from
32 bytes to one short refused, never cut.

**Found while building it, all fixed here:**

- **The bridge escaped only `"`, `\` and newline**, and wrote the
  position with `%f`. A control character in a name, or a latitude of
  `nan`, gave the app a document it could not decode — shown as an
  unreachable caster. Strings now go through the core's
  `map_json_quote`; an unusable position is written 0,0 and flagged.
- **`NewStringUTF` takes modified UTF-8** (JNI specification), which a
  caster's four-byte character is not. The document now crosses as
  bytes and Kotlin decodes it as UTF-8.
- **A fixed 1 MB in the JNI glue would have refused onocoy** once each
  entry carried the core's figures: measured at 1.26 MB for 4096
  entries. Hence the bridge sizing its own output.
- **The bridge stopped at 4096 entries**, so the phone's list of onocoy
  (over 6100) silently lacked about a third. The ceiling is now 16384.

**Measured through the bridge's own fetch**, compiled on the desktop
against live casters, origin RFSEE01:

| Caster | Entries | Document | Fixed / network / product | Unusable |
|---|---|---|---|---|
| onocoy | 5930 (4096 before) | 1 827 843 bytes | 5929 / 0 / 1 (`EPHEMERIS`) | 4 |
| Centipede | 1298 | 392 914 bytes | 1296 / 2 (`NEAR`, `NEAR4`) / 0 | 0 |
| Kadaster | 61 | 19 156 bytes | 58 / 0 / 3 (`BCEP00KAD0`, `RTCM3EPH`, `RTCM3EPH2`) | 0 |

The kinds agree with M1's. Nearest to RFSEE01: Kadaster `APEL0` 1.6 km
at 264°, Centipede `NLDAL` 21.7 km at 101°.

**Not yet seen on the phone.** This branch is cut from `main`, which
has no `.dev` application id, so a debug build here would install over
the Play edition; the Kotlin decode waits for that.

**M6b — the outlines as an asset.** `tools/make_basemap.py` also
writes `android/app/src/main/assets/basemap_ne50m.bin`: the same
polygons and border classes from the same pinned v5.1.2 input,
coordinates as 32-bit integers in thousandths of a degree — the
rounding the JS already has. Byte-identical on a second run. Credit in
the app's Notices (`make_notices.py`, which already reads the version)
and on the map itself.

**M6c — the screen.** A destination of its own (`Dest.MountMap`),
full screen, Back closes it.

- **Projection: azimuthal equidistant about the origin.** Distance
  and bearing from the centre are then true everywhere on the screen,
  so the plot's rings mean what they say, which a Mercator map's would
  not. Outlines further out are distorted, and get worse with distance.
  That is the projection's cost, and it is stated on the screen.
- **Rings** by the self-position plot's 1-2-5 rule, in km. **North
  up.** **Pinch to zoom, drag to pan**, and a control that returns to
  the origin.
- **Marks:** the kind by colour **and** shape (fixed, network,
  product, unknown), as the GUI's legend. Entries with no usable
  position are counted on screen, never drawn.
- **No origin.** When no usable position is set, the map centres on
  the entries and draws no rings, and says why.
- **Tap:** the nearest mark within a finger's reach opens a balloon
  with the mountpoint, identifier, format, NavSys, kind with its
  reason, and distance and bearing. In pro, **Select mountpoint** does
  exactly what tapping the list row does: one path. In free, a line
  saying pro selects from here.
- **Loading:** the same fetch the list makes, shared rather than
  copied.

**M6d — the hub row.** `BrowsePanel` draws the two buttons, half width
each, on one row.

**Verify on a device**, against real casters:

- a station known to lie north-east of the phone is drawn north-east,
  at the distance the balloon gives;
- Kadaster's Caribbean entries appear across the Atlantic, at their
  distance;
- onocoy's 6123 entries pan and zoom smoothly on the Huawei, measured
  in frame time, not judged by eye;
- in pro, Select fills the mountpoint as the list does; in free the
  balloon shows the line and nothing is selected.

### M6 as first planned  *(superseded by the above)*

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

Not chosen for D3 (2026-10-13); kept as an option. Only if M6 has
shipped and the author wants it. `Share.kt` already
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
