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
| Seven-KPI spot check | yes, unlimited | yes |
| Verdict, reasons, throughput/SV chips | yes | yes |
| Mountpoint entry | **typed manually, one saved** | multiple saved, switchable |
| Sourcetable | **viewable, not selectable** | browse and tap to use |
| Watch mode | **not available** | yes, unlimited |
| Sky plot (coverage heatmap) | yes | yes |
| Ephemeris source | imported RINEX, on-demand stream, phone GNSS | same |
| Session history | — | yes |
| Per-SV / per-band C/N0 | — | yes |
| Ionosphere (ROTI) | — | yes |
| Network-RTK / VRS test | — | yes |
| Message-type statistics | — | yes |
| Export CSV/JSON, shareable report | — | yes |
| RTCM capture and offline replay | — | yes |

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

So both editions get the same sources. The free edition is limited by
*time*, as it always was: one capture, then it stops. That is a limit the
user understands and can work with, rather than a measurement that
quietly under-reports.

The app does not download the navigation file in either edition — see
`views.md` for why the terms could not be established, and why the user
supplying the file is the honest arrangement.

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

## Upgrade path — a known cost of two listings

Two separate listings mean upgrading is *installing a second app*: saved
settings do not carry across, and the user manages two icons. A single
listing with an in-app purchase would avoid both. That trade was
considered and two listings chosen deliberately; if the friction shows up
in reviews, the flavors already isolate everything an IAP migration would
need to touch.
