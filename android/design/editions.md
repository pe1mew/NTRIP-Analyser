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
| Sky plot | yes | yes |
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
