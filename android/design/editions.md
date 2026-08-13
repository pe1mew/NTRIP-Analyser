# Two editions, one codebase

**Decided 2026-08-12.** The Android app ships as two Play listings built
from the same source: a free edition that answers *"is this station on
and working?"*, and a paid edition for diagnosing *why* it is not.

## The rule that is not negotiable

**Feature gating happens in the UI layer, never in the C core.**

A free user's STATION OK must mean exactly what a paid user's STATION OK
means. The seven KPIs, their thresholds and the sustain window come from
`src/core/kpi.c` and are identical in both editions — as they are in the
CLI's `--check` and the monitoring daemon. This is a measurement
instrument; a verdict that varied by licence tier would be worthless, and
a user who discovers the free app grades more leniently has been given a
reason to distrust every number the paid one shows.

The paid edition therefore shows **more**, never **different**.

## The split

| | Free | Paid |
|---|---|---|
| Eight-KPI spot check | yes, unlimited | yes |
| Verdict, reasons, throughput/SV chips | yes | yes |
| Mountpoint entry | **typed manually, one saved** | multiple saved, switchable *(not built yet — see Launch scope)* |
| Sourcetable | **viewable, not selectable** | browse and tap to use |
| Watch mode | **not available** | yes, unlimited |
| Sky plot (coverage heatmap) | yes | yes |
| Ephemeris source | **imported RINEX file, phone GNSS** | on-demand ephemeris stream, and an imported file |
| GGA position sent to the caster | **fixed, from the mountpoint's sourcetable entry** | the phone's live position |
| Configuration files (load / save) | — | yes |
| Session history | — | *planned* |
| Per-SV / per-band C/N0 | — | *planned* |
| Ionosphere (ROTI) | — | *planned* |
| Network-RTK / VRS test | — | *planned* |
| Message-type statistics | — | *planned* |
| Export CSV/JSON, shareable report | — | *planned* |
| RTCM capture and offline replay | — | *planned* |

*Planned* means the desktop has it and the phone does not. The
distinction is not cosmetic: a store listing written from this table
would otherwise promise seven features the app does not have, and the
refunds would be deserved. `MAX_MOUNTPOINTS` is the same trap caught
early — declared, unread, and listed above as though it worked.

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

So the free edition keeps the sources that make its sky view honest: an
imported navigation file, which places everything, and the phone's own
GNSS as the fallback when no file has been imported. What it does not
get is the **on-demand ephemeris stream** (`HAS_EPH_STREAM = false`) —
that is a second connection to a caster on the user's behalf, and
borrowing someone's infrastructure is a reasonable thing to charge for.

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

- **Multiple saved mountpoints.** `MAX_MOUNTPOINTS = 16` is declared in
  the pro flavor and read by nothing; the table below has been promising
  a feature that does not exist.
- **The live-position GGA**, per the section above — the field-use half
  of the paid proposition.

Together those make one coherent story rather than a list of small
unlocks: *keep watching, from where you actually are, across the
mountpoints you care about.*

### Price

Decided at listing time, once comparable tools have been checked in the
Play console. Nothing in the build depends on the number.
