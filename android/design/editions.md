# Two editions, one codebase

**Decided 2026-08-12.** The Android app ships as two Play listings built
from the same source: a free edition that answers *"is this station on
and working?"*, and a paid edition for diagnosing *why* it is not.

## The rule that is not negotiable

**Feature gating happens in the UI layer, never in the C core.**

A free user's STATION OK must mean exactly what a paid user's STATION OK
means. The eight KPIs, their thresholds and the sustain window come from
`src/core/kpi.c` and are identical in both editions — as they are in the
CLI's `--check` and the monitoring daemon. This is a measurement
instrument; a verdict that varied by licence tier would be worthless, and
a user who discovers the free app grades more leniently has been given a
reason to distrust every number the paid one shows.

The paid edition therefore shows **more**, never **different** — and
never **safer**. A security property is not a feature to sell: when TLS
lands it lands in both editions on the same day, however much the paid
edition is the one that needed it (`design/tls.md`). What the paid
edition withholds is convenience, never protection.

## The split

| | Free | Paid |
|---|---|---|
| Eight-KPI spot check | yes, unlimited | yes |
| Verdict, reasons, throughput/SV chips | yes | yes |
| Mountpoint entry | **typed manually, one saved** | several saved connections, switched from the tile |
| Sourcetable | **viewable, not selectable** | browse and tap to use |
| Watch mode | **not available** | yes, unlimited |
| Sky plot (coverage heatmap) | yes | yes |
| Ephemeris source | the station's own stream, an imported RINEX file, phone GNSS | the same three, plus an on-demand ephemeris stream when the station carries none |
| GGA position sent to the caster | **fixed, from the mountpoint's sourcetable entry** | the phone's live position |
| TLS to the caster *(planned)* | yes, the same day pro has it | yes — scheduled here, withheld from no one |
| Per-satellite C/N0 bars, and C/N0 against elevation | yes | yes |
| Reference position (ARP) | **position only** | the whole record: station ID, ITRF year, reference-versus-receiver, oscillator, ECEF |
| Message types received | — | the list of types, from the tile |
| Configuration files (load / save) | — | yes |
| Session history | — | *planned* |
| Ionosphere (ROTI) | — | *planned* |
| Network-RTK / VRS test | — | *planned* |
| Message-type *statistics* — counts, rates, intervals | — | *planned* |
| Export CSV/JSON, shareable report | — | *planned* |
| RTCM capture and offline replay | — | *planned* |

*Planned* means the desktop has it and the phone does not. The
distinction is not cosmetic: a store listing written from this table
would otherwise promise seven features the app does not have, and the
refunds would be deserved. `MAX_MOUNTPOINTS` was the same trap, caught
before it reached a listing: declared, read by nothing, and tabled as
though it worked. It is implemented now.

### Why the sourcetable is viewable but not selectable

A free user can fetch a caster's sourcetable and read it — that makes the
free app genuinely useful, and it is how someone discovers what a caster
offers. What they cannot do is *tap an entry to use it*: the mountpoint
must be typed by hand. Convenience across many mountpoints is precisely
the paid proposition, so the free edition gives the information and
withholds the workflow. Nothing is hidden; the work is manual.

### Why the position sources are not a paid feature

They were going to be: the RINEX download was to be pro-only. Two
measurements changed that. Phone GNSS placed **23 of 46 satellites and no
Galileo** on the test handset, and the observation stream yielded **7
ephemerides in five minutes** on a caster that carries them at all. A
free edition restricted to those would ship a sky view that is mostly
empty through no fault of the user — which sells nothing and teaches the
user to distrust the plot.

The second of those measurements has since been shown to be one station
rather than a rule. On `caster.centipede.fr/NEAR` the observation stream
delivers **603 ephemerides in five minutes and places 40 of 40 tracked
satellites**, and the free edition now draws that plot with no file, no
second connection and nothing configured. Where a station carries none,
the earlier finding still holds and the other sources still matter.

So the free edition keeps every source that costs nobody anything: the
station's own stream, an imported navigation file, and the phone's own
GNSS as the fallback. What it does not get is the **on-demand ephemeris
stream** (`HAS_EPH_STREAM = false`) — that is a second connection to a
caster on the user's behalf, and borrowing someone's infrastructure is a
reasonable thing to charge for. It is also the source pro now reaches
for least, because a station that broadcasts its own orbits is never
dialled.

The free edition is limited by *time*, as it always was: one capture,
then it stops. That is a limit the user understands and can work with,
rather than a measurement that quietly under-reports.

The app does not download the navigation file in either edition — see
`views.md` for why the terms could not be established, and why the user
supplying the file is the honest arrangement.

### Why the GGA uplink differs, and why *sending at all* does not

Two questions get conflated here, and separating them is most of the
design.

**Whether to send a GGA is a property of the mountpoint, not of the
edition.** The sourcetable's STR record carries an `nmea` flag — *this
mountpoint expects a GGA uplink* — and `src/core/sourcetable.c` has
parsed it all along without anyone reading it. Both editions follow it:
send when the entry asks for it, stay quiet when it does not, with a
manual override for casters whose metadata is wrong. A network service
that receives no GGA sends nothing back, and a user watching that
happen has no way to tell it from a broken station. Sending
unconditionally would be the other error: a single base has no use for
the position, so transmitting one is a privacy cost buying nothing.

**What position it carries is the edition difference**, and it maps onto
two genuinely different questions:

- *Does this station serve the area it claims?* — a base-station
  acceptance test. A **fixed** position is the better instrument here:
  repeatable, comparable between runs, independent of where the tester
  is standing. Free prefills it from the mountpoint's own sourcetable
  coordinates — test as if standing at the station — which needs no
  typing, no permission, and is always inside the network's coverage. It
  stays editable for a user testing a particular site.
- *Am I served properly **here**?* — field work, and what a professional
  is paid to answer. Pro sends the phone's live position, falling back
  to the fixed one when there is no fix, so a run started indoors still
  has something to send rather than nothing or a zero.

That fallback matters more than it looks: a GGA of 0,0 is a valid
sentence that puts the rover in the Atlantic, and a VRS will answer it.

**A fixed position can fail a healthy network**, and the app has to say
so rather than report a station fault. Type a position outside the
network and the VRS returns a distant ARP or nothing; assertion A3
(broadcast ARP near the rover) then fails on a service that is working
correctly. The sourcetable prefill makes this the uncommon case instead
of the default one.

### How it is built

**The uplink is driven by the bridge, not by the session's own timer.**
`NsOptions::send_gga` sends from the position the session was opened
with, and a rover moves; `ntrip_bridge.c` therefore keeps the cadence
itself and calls `ns_send_gga()`, which is the mechanism the Windows GUI
already uses for its VRS work. `ntrip_session.h` is explicit that one
drives the uplink or the other, never both.

Three details are deliberate:

- **The first sentence goes out on the first pump after the handshake**,
  not one interval later. A VRS answers nothing until it knows where the
  rover claims to be, so a ten-second wait costs the run its first ten
  seconds of stream — on a sixty-second acceptance window that is a
  sixth of the evidence.
- **The clock only advances on a sentence the socket accepted.** A run
  that spends its first minute reconnecting uplinks the moment it is
  back, rather than on the next tick of a free-running timer.
- **The position is pushed in, never pulled.** `MonitorService` reads
  the phone's fix from the UI's own receiver and calls
  `bridge_set_position()`; the C side does not know a phone exists.
  Null means *do not report a phone position*, so consent withdrawn, a
  lost fix and a closed screen all fall back the same way, within one
  interval.

**While the app is off screen, Android stops delivering location to a
`dataSync` foreground service**, so the last position stands until the
user returns. That is the fallback working as designed rather than a
stale reading dressed up as a live one. The alternative — a
`location`-typed service, plus background-location permission — would
track the user continuously to answer a question they asked while
looking at the screen, and this app will not do that.

### Where the fixed position comes from

Typing coordinates on a phone is the worst part of this feature, so
there are three ways not to:

| Route | Free | Pro |
|---|---|---|
| **From station** — reads the mountpoint's own sourcetable entry | yes | yes |
| **Picking the mountpoint from the sourcetable** — takes its position and its `nmea` flag with it | — (the list is inert; see above) | yes |
| **Pick on map** — hands off to a map app, takes the answer back through the clipboard | yes | yes |

The map is **somebody else's**, and that is the decision rather than a
shortcut. The Windows GUI writes a Leaflet page to a temporary file and
lets the browser fetch the tiles (`gui/gui_events.c`, `OnMapPick`); the
Android half fires a `geo:` intent, falling back to OpenStreetMap in the
browser, and reads coordinates back from the clipboard (`MapPick.kt`).
Mechanically different, because a local HTML file cannot be handed to a
modern Android browser, but the same shape and the same reasons:

- **Embedding a map SDK would make this app a tile client.** Osmdroid —
  what `ttnmapper-android-v3` uses — is Apache 2.0 and licence-clean,
  so that is not the objection. Tile requests carry where the user is
  looking; making them from a tool that otherwise collects nothing
  changes the Play data-safety answer away from *no data collected or
  shared*, to buy a convenience the user's own map app already provides.
- **The OSM tile policy forbids the bulk and offline prefetch that field
  use wants.** A browser or a maps app visiting its own provider is that
  provider's user, not ours.
- **Attribution is theirs to get right**, and it is on screen either way.

This does not revive desktop item 4.2 (map *widget*), which stays
dropped: a picker answers "which point?" once, where a widget would draw
a stationary dot for the length of a run.

### Why the privacy story is not the same in both editions

In free, location is read on the device and never leaves it: it turns
satellites into azimuth and elevation for the plot, nothing more. In
pro, the position is **transmitted to a third-party caster**. Those are
different acts and they are declared differently on the Play data-safety
form, so pro asks once, explicitly, before the first run that would
transmit a position — naming the caster and what is sent — and keeps a
switch to revoke it. A line in a settings screen is not consent for
sending someone's location to a server.

### (paused) Why an app-side RINEX download would have been paid

The sky plot needs orbits, and orbits reach the app one of two ways: a
live ephemeris mountpoint, or a broadcast RINEX navigation file
downloaded once and reused. The free edition gets the first, which costs
nothing to offer and works wherever a caster publishes ephemerides.

The download is the paid alternative because it is what makes the sky
plot work in the field when the ephemeris mountpoint is unreachable —
a rack with a firewalled network, a caster that publishes observations
only, or a site with metered connectivity where a one-off file beats a
second continuous stream. That is a professional's problem, and the
solution is worth paying for.

Implementation notes for when it is built: `src/core/rinex_nav.c`
already parses RINEX 3 NAV and the CLI already preloads from it
(`-R/--RINEX`), so the new work is fetching, not parsing. The fetch
needs a source decision — BKG and IGS mirrors are open, NASA CDDIS
requires an Earthdata login — and the files are usually compressed, so
the app needs gzip. Neither is hard; both are choices to make
deliberately rather than to discover halfway through.

### Why watch is a paid capability, not a capped one

The free edition is limited by *capability*, not by a timer: spot checks
are unlimited, and watch is simply absent. The two modes answer different
questions — "does this station pass?" and "does it keep passing?" — and
the second is what takes hours of measurement to answer. Selling that as
a crippled version of itself, stopping mid-measurement, would produce a
worse impression than not offering it: a five-minute watch cannot see
what a watch is for.

The free app therefore stays a complete, permanently useful go/no-go
checker, with nothing about it half-finished.

## A shared feature must behave identically

Gating decides whether a feature *exists* in an edition. It must never
decide how a feature *behaves* where both have it.

That line was crossed once and is worth recording. The live-versus-mean
C/N0 choice was keyed on the edition, so pro showed the last epoch and
free the mean over the capture — two people looking at the same finished
capture of the same station read different numbers, for no reason either
could see. It now follows the *run*: live while measuring, the capture
mean once stopped, in both editions. What pro buys is being able to keep
measuring, not different arithmetic.

Verified by running the same station check on both editions minutes
apart: identical KPI verdicts, with satellite count and mean C/N0
differing only as two passes over a live sky would.

The rule, for anything added later: if both editions have the feature,
the only acceptable difference is how long it may run.

## One codebase, measured

Not asserted -- counted, on 2026-08-12:

| | Lines |
|---|---|
| Shared (`src/main`) | **3725** |
| Free-specific | 47 |
| Pro-specific | 33 |

The entire difference between the editions is **five constants and the
app name**. No logic, no screen and no C file exists in one edition and
not the other, and the shared code contains seven edition-conditionals
in total. The editions are a configuration of one program, and this
table is worth re-running whenever that claim is made again.

## Mechanism

Gradle product flavors, `free` and `pro`, with separate `applicationId`s
so they are two Play listings, as decided. Each flavor supplies its own
`Features` object from `src/free/` or `src/pro/`; the shared code in
`src/main/` reads it.

Compile-time, not a runtime flag: the free APK does not contain the paid
screens, so gating cannot be defeated by flipping a boolean, and the free
download stays smaller.

```
android/app/src/
  main/   shared: the bridge, the service, the KPI screen
  free/   Features.kt (watch capped, single mountpoint), app label
  pro/    Features.kt (unlimited), app label
```

### The framework is shared; only the list differs

**Decided 2026-08-18, with GUI v2**
([guiV2rollout.md](../../design/guiV2rollout.md)), and given a template
in **GUI v3** ([guiV3spec.md](../../design/guiV3spec.md), 3.7.0). The
shell, the navigation stack, the hub, the panel contract and the share
socket all live in `main/` and are identical in both editions.

What v3 added to that shell, and why it belongs there rather than in a
flavour: one app bar with four slots and **no title parameter**, so no
screen can disagree about the app's name; one overflow menu, built from
what a screen says its rows *do*; the analysis bar, pinned, and absent
by the simple fact that no other screen passes one; and the affordance
marks, drawn by the hub from `Panel.affordance(state)` rather than by
each card. Every one of those is a rule the editions cannot break
separately, which is the whole reason the framework is shared. What a flavor
supplies is a **registry** — the list of panels it contains — and, for
the paid ones, the panel files themselves:

```
  main/   panel contract · shell · hub · share socket   (identical)
  free/   Registry.kt  ->  6 panels + MoreInPro
  pro/    Registry.kt  -> 11 panels
          VrsPanel.kt, HandoverPanel.kt, Tier2Panel.kt, ...
```

Two properties pull in opposite directions, and this arrangement gets
both:

* **Nothing paid is compiled into free.** A pro panel's file lives in
  `src/pro/`, so the free APK contains neither its screen, nor its
  strings, nor its measurement — the position stated above, unchanged.
* **There is one framework, not two.** A layout fix, a share-format
  change or a navigation bug is fixed once in `main/` and both editions
  have it. The editions cannot drift apart, because the only per-flavor
  UI file is a list.

**The build enforces this**, because a rule only a person checks is a
rule that drifts. `checkEditionParity` in `android/app/build.gradle.kts`
fails the build unless every file in a flavor is one of: `Features.kt`,
`Registry.kt`, a `*Panel.kt` in **pro**, or `MoreInProPanel.kt` in
**free** — and it rejects outright any flavor file that **shadows a name
in `src/main`**, which is what "copied into src/free to change one
thing" looks like on disk. The task was widened from "only Features.kt"
when the registries arrived (GUI v2, P1.2), deliberately and in step
with this section.

One deliberate exception: free registers a **More in Pro** card naming
what the paid edition adds. It is a single entry at the bottom of the
hub, and it is the *only* place free mentions a capability it does not
have — no greyed rows, no disabled controls, nothing that looks broken
to someone who has not paid.

Build and install a specific edition:

```bash
./gradlew installFreeDebug
./gradlew installProDebug
```

### On enforcing the boundary

These are product boundaries, not security controls. A determined
user can change the device clock or rebuild from source — this project is
open source, so the paid edition's value is the packaging, the
convenience and the support, not secrecy. The cap is enforced simply and
honestly through the flavor's `Features` object, and no effort is spent
on obfuscation that would only degrade the code.

## Payment model — decided

**Two listings: a free app and a paid app**, built from the flavors that
already exist. No billing code, and Google handles price, tax and
refunds.

The deciding argument is not the code, it is where the app is used. With
two listings the entitlement *is* the installed APK: it works in a rack
room, on a hilltop, on a survey site with no signal, for ever. An in-app
unlock has to be verified, and Play Billing's cached entitlement is
exactly the thing that is missing on a fresh install with no
connectivity. "I paid for this and it will not unlock" is the worst
review a professional tool can collect, and it would arrive from the
field where it cannot be fixed.

The licence already protects the arrangement. Commons Clause bars anyone
else from selling this code; as copyright holder the author is not bound
by his own licence, so a paid edition is consistent with the repository
being public.

Known costs, accepted: two uploads and two sets of store metadata for
every release, split reviews and install counts, and no upgrade
discount — a free user pays full price for pro.

### Upgrade path — manual, and small

Upgrading is installing a second app and typing the caster and
mountpoint again. That is the whole of it, because the free edition
holds exactly one mountpoint and **cannot read or write configuration
files** — those are a pro capability by design, so there is no file to
carry across in the first place.

An earlier draft of this section assumed the config file would bridge the
two editions. It cannot, and the friction is a minute of typing rather
than a lost setup.

### Launch scope for the first paid release

What is built today: watch mode, the on-demand ephemeris stream,
tap-to-use sourcetable entries, and the expanded KPI detail (message-type
list, full ARP block).

Two more before charging:

- ~~Multiple saved mountpoints.~~ **Built.** `MAX_MOUNTPOINTS` is now
  the bound it always claimed to be, credentials moved to encrypted
  storage in the same change, and the pre-profiles settings migrate into
  profile 0 rather than being lost.
- **The live-position GGA**, per the section above — the field-use half
  of the paid proposition.

Together those make one coherent story rather than a list of small
unlocks: *keep watching, from where you actually are, across the
mountpoints you care about.*

### Price

Decided at listing time, once comparable tools have been checked in the
Play console. Nothing in the build depends on the number.

**The paid edition is not a revenue plan.** This is a learning project
held to professional standards; the listing exists so the work has a
price rather than to earn a living from it — which raises the bar rather
than lowering it, since someone paying for an instrument is owed one
that measures correctly. That is why a subscription was never seriously considered, why
no price is set in advance, and why nothing here is shaped by what would
sell better — the split is drawn on what is honest to withhold
(convenience), not on what would convert.

## Saved connection profiles (the "multiple mountpoints" feature)

### It is multiple *casters*, not multiple mountpoint names

The phrase hides the decision. Saving mountpoint names within one caster
would be close to worthless: the expensive part of setup is the caster,
the port and the credentials, never the six characters of the mountpoint.
People work across several casters — their own base, the national
network, a client's — with different credentials, different positions and
different ephemeris mountpoints for each.

So a saved entry is a **whole connection profile**: caster, port,
mountpoint, credentials, position, GGA setting and ephemeris stream,
under a name. Free keeps one, which is what it already does.

### Storage, and the migration nobody sees until it bites

`Settings` is eleven flat keys in one `SharedPreferences` file today. A
list of profiles plus an active index replaces them, serialised with
`kotlinx.serialization` — already a dependency, since `ConfigFile` uses
it.

**Existing installs must be migrated**, not overwritten: read the old
flat keys into profile 0 on first launch. Anyone already testing the app
loses their setup otherwise, and they will report it as data loss
because that is what it is.

Credentials move to `EncryptedSharedPreferences` in the same change.
Storing one caster's password in the app sandbox was a recorded, accepted
limitation; storing half a dozen is the same limitation with a larger
blast radius. Doing it now means one storage migration rather than two,
and a better answer on the Play data-safety form.

`MAX_MOUNTPOINTS` stops being decorative and becomes the bound it always
claimed to be.

### Interop: one format, everywhere

**Superseded, by the better answer.** This section first proposed
keeping two formats: the desktop's single-connection `config.json`, plus
an export in the daemon's `mountpoints[]` shape for a set. Two formats
for one job taxes everyone who touches either, and it was replaced by
**one exchange format across the whole project** — the list.

Every program reads and writes it now. The analysers take the first
entry and say how many they ignored; the daemon takes them all; the
phone merges them into its saved connections, updating a connection it
already has rather than duplicating it. Files in the older
single-connection layout are still read everywhere, so nothing anyone
already has stops working — see
[`docs/jsonConfigs.md`](../../docs/jsonConfigs.md).

The argument against extending the format — that it turns a phone
feature into a change across every frontend — was true, and was worth
paying: `src/core/config.c` serves the CLI and the GUI at once, so
three of the four programs moved in one change.

### Switching: a picker on the tile that already exists

The NTRIP configuration tile shows the active connection and already
opens settings when tapped. It becomes the picker: the saved profiles
with the active one marked, plus add, edit and delete. Nothing new
appears on a screen whose whole purpose is one verdict.

### Deliberately not in this feature

**Running several mountpoints at once.** Each session is a socket, a
pump thread and its own KPI state; the daemon does that because it is a
server with mains power. One active profile at a time on the phone.

**Checking every saved profile in turn**, ninety seconds each, is the
obvious next feature — surveying a whole network from a van — and the
storage above is shaped so that it stays possible. It is not this
change.
