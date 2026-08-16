# Release the Android app to the app stores

*(The file is still called `release-to-play.md`; the plan outgrew the
name on 2026-08-14 and the path is kept so the references to it keep
working.)*

*This is the **Android** track. Three others run in parallel and are not
blocked by it: [cli-track.md](cli-track.md),
[gui-track.md](gui-track.md), and
[measurement-tiers.md](measurement-tiers.md) — the last of which does
wait on this one, since KPI 9 renames a claim in the live listing.*

## What & Why

Two Play listings — free and pro — built from the flavors that exist.
Everything between here and there: telemetry, a security assessment, a
licence study, user documentation on the GitHub wiki, and the store
plumbing itself.

The ordering below is not preference. Each phase produces something a
later phase needs, and three of them are *decisions* that block the
privacy policy and the data-safety declaration, which block submission.

## Store strategy — free first, three stores

**The free edition goes to Google Play, the Samsung Galaxy Store and
F-Droid. Pro follows, where it can.** One edition in three places is a
smaller job than two editions in one, and it puts the tool in front of
the people who will find its faults.

### Google Play — the long pole, and it cannot be hurried

The developer account is **in verification**. Beyond that, a personal
developer account registered recently must run a **closed test with at
least 12 testers opted in for 14 continuous days** before it may apply
for production access. That is Google's rule and there is no way to
shortcut it: twelve real people have to install the app and leave it
installed for a fortnight.

So it takes the time it takes. **The recruitment happens in the open**,
from the top of the repository readme, alongside the download links for
the other two stores. Testers who arrive early shorten nothing — the
fourteen days start when twelve are in — but they are also the first
people to run this against stations we have never seen, which is worth
more than the fortnight.

The opt-in link for a closed track takes the form
`https://play.google.com/apps/testing/<package>`; it only resolves once
the track exists, so confirm the real one in the console before leaning
on it.

### Samsung Galaxy Store and F-Droid — study before work

Neither is a matter of uploading the same bundle, but only one of them
still needs studying before work can start:

1. **Samsung Galaxy Store rules** — what the listing requires, what the
   review checks, what a seller account needs, and how an APK differs
   from the Play bundle. **Nothing known blocks the free edition**:
   Samsung has no FLOSS requirement, so the Commons Clause is beside the
   point there — it is in fact the arrangement the Clause was written
   for, where the author sells and nobody else does.
2. **F-Droid — settled**, see below. The free edition goes to a
   repository we host; the official one is closed to us by licence.
3. **The paid edition on Samsung, a feasibility study of its own,
   deferred until free is out** — whether pro can be sold there at all
   from this country as an individual, what the commission and payout
   arrangements are, and what that means for the entitlement model,
   which today is *the installed APK is the licence*
   (`android/design/editions.md`).

### F-Droid: settled 2026-08-14 — our own repository, not theirs

**The official repository will not take this app, and no packaging work
would change that.** Its inclusion policy requires every app to be
"Free, Libre and Open Source Software (FLOSS)", judged against the
Debian Free Software Guidelines, the FSF, GNU and the OSI. All four
forbid restricting sale — DFSG §1 and OSD §1 in as many words — and
`license.md` says plainly "**You may not sell it.**" The Commons Clause
and the F-Droid main repository are mutually exclusive by definition,
not by interpretation.

Nothing else about the app is in the way: no Google Play Services, no
proprietary SDK, no tracking library, every dependency Apache 2.0 or
MIT.

**Decision: publish the free edition from a repository we host
ourselves.** The inclusion policy governs *F-Droid's* repository; anyone
may publish their own, and users add its URL to the same client. That
buys the client, the update mechanism and the audience with **no licence
change at all** — and self-distribution has never been in tension with
the Clause, which restricts others from selling, not us from giving
away.

The two rejected alternatives, so they are not revisited:

- *Drop F-Droid* — loses a channel that costs little once the repository
  exists.
- *Relicense the free edition as plain Apache 2.0* — the editions share
  one core, so the shared C and Kotlin would have to lose the Clause
  too, reinstating exactly the "anyone may sell it" risk it exists to
  prevent, on the parts that took the most work.

**Pro is not part of this decision.** Whether a paid edition belongs in
a self-hosted repository at all — where there is no store to take
payment — is a separate question, deferred until free is out.

## Current Status

Phases 2 and 3 are done. Phase 1 (telemetry) is a decision and blocks
the privacy policy; 4 onwards are sequential.

| # | Phase | Blocks | State |
|---|---|---|---|
| 1 | Telemetry decision | privacy policy, data-safety form | **done** → `design/telemetry.md`; collect nothing |
| 2 | Licence study | store listings, wiki claims, RINEX auto-download | **done** → `docs/licences.md`, 7 actions |
| 3 | Security assessment | any public release | **done** → `design/security-review.md`; 6 of 7 closed, TLS open |
| 4 | Live GGA implementation | pro's launch scope | **done** → built and verified against a live network mountpoint |
| 5 | Release plumbing | submission | **done** → signing, version, R8, icons, listings, notices |
| 6 | Samsung S23 verification | submission | **run** 2026-08-14 on Android 16 — everything but the signature |
| 7 | Wiki (free) | free launch | **done** 2026-08-14 → twelve pages published |
| 8 | Wiki (pro) | pro launch | **done** 2026-08-14 → same wiki, Pro section |
| 9 | Play closed testing: 12 testers × 14 days | free on Play | open — **account verified 2026-08-14**; needs the keystore, then a track |
| 10 | Samsung Galaxy Store rules study | phase 11 | not started — nothing known blocks free |
| 11 | Free on the Samsung Galaxy Store | — | blocked by 10 |
| 12 | Free from a self-hosted F-Droid repository | — | **decided** 2026-08-14; not built |
| 13 | Pro beyond Play: Samsung sale feasibility, and whether pro belongs in a self-hosted repo at all | pro elsewhere | deferred until free is out |

---

### Phase 1 — Telemetry: informative but innocent — **DONE 2026-08-13**

**Decision: the app collects nothing.** No SDK, no endpoint, no consent
flow. The two numbers wanted — instances downloaded, and uses per day —
come from Play Console's own statistics, which report installs and
foreground-open engagement (a differentially-private sample of users who
opted in at the OS level) with no code at all. Crashes and ANRs arrive
through Android vitals the same way.

Opt-in counters on our own endpoint were rejected: they would make the
author a data controller under the GDPR, with a lawful basis, retention,
deletion requests and a server to keep patched, for numbers Play already
supplies. A third-party SDK was rejected as the wrong instrument inside
a tool professionals run on customers' infrastructure.

The counterpart is a **report the user shares with whoever owns the
station** — not a channel into the author's inbox. Scheduled before the
free launch: verdict, KPI values, stream statistics, caster and
mountpoint, complete by default with a control to strip the station's
identity for a public posting. Credentials and the phone's position are
never included, because neither is a measurement.

An export is a deliberate act by the user and completeness is its
purpose — a station acceptance record that does not name the station
documents nothing. The desktop's existing *Export Statistics* is right
as it stands. See `design/telemetry.md`, "Export is not leakage".

**Support posture, decided with it**: minimal follow-up after
deployment. The product answers the questions — every KPI states its
value and meaning already — the wiki answers the rest once, and issues
go to the public GitHub tracker rather than to a help desk. The listing
must carry a contact address because Play requires one; the wiki says
plainly what it is not.

Original analysis:

**The baseline is free and already innocent.** Play Console *Android
vitals* reports crashes, ANRs, battery and wakelock behaviour with **no
SDK, no consent flow, no privacy-policy expansion, and no data you
hold**. The only question worth asking is what vitals cannot tell us
that would change what we build.

Candidates it cannot answer: which KPIs fail in the wild, whether checks
run to a settled verdict or get abandoned, whether the RINEX import is
used at all, and the edition/app/Android version mix.

**Never transmitted, whatever is chosen**: caster host, mountpoint name,
credentials, position, or station identity. A mountpoint plus a
timestamp is close to a location fix, and the caster is a third party's
infrastructure, not ours to report on. Crash traces must be scrubbed —
a stack frame carrying a config object would leak all of it.

Options, in ascending cost: vitals only · vitals + opt-in counters to an
endpoint we run · a third-party SDK (Firebase). The middle option keeps
us the data controller under GDPR; the third adds a processor and a
larger data-safety declaration.

**Decision needed before the privacy policy can be written.**

### Phase 2 — Licences and usage conditions of everything we did not write — **DONE 2026-08-13**

Full study in `docs/licences.md`; it produced seven actions, four of
which block the free launch. The findings that changed something:
`license.md` claims a broader restriction than the Commons Clause
actually imposes; the app has no open-source notices screen; and the
free Kadaster streams need no registration, which the readme currently
says they do.

Original scope:

- **Our own**: Apache 2.0 + Commons Clause. The author may sell; nobody
  else may. Confirm nothing in the pro listing contradicts the repo.
- **Vendored**: cJSON (MIT, in `lib/cJSON`) — attribution required, and
  the pinned version should be checked against known CVEs.
- **Android runtime**: AndroidX, Compose, kotlinx-serialization (all
  Apache 2.0) and `androidx.security:security-crypto`. An OSS-notices
  screen in the app is the usual discharge.
- **Data sources**, the unresolved one: BKG / IGS / CDDIS RINEX terms —
  this is the study that was paused when the auto-download was dropped
  (`android/design/views.md`). The app currently has the user fetch the
  file, which keeps the relationship theirs; confirm that reading it
  in-app carries no further condition.
- **Casters**: Kadaster registration terms, and what the app should say
  about connecting to third-party casters on the user's behalf.
- **Store listing**: screenshots showing a real caster's mountpoint
  names — permitted, or replace with our own.

### Phase 3 — Security assessment — **DONE 2026-08-13**

Report in `design/security-review.md`. Two defects found and fixed: RTCM
1033 read past its payload and printed what it found there, and the
sourcetable accumulated without limit from an untrusted caster. Both are
covered by `test/test_rtcm_hostile.c`, which fails against the pre-fix
parser.

Six of the seven findings are closed: two decoder defects fixed, the
unbounded bit reader given a checked variant, base64 encoding given a
capacity, the vulnerable-but-unused cJSON file deleted, and the alpha
crypto dependency replaced with stable 1.0.0 (existing encrypted stores
verified readable on the handset).

**F3 remains a decision, not a defect.** NTRIP sends credentials
base64-encoded over plain TCP and this client speaks no TLS, while
Kadaster already offers a TLS caster on port 443. It is now disclosed
where a user can act on it -- once per session in the log, and under the
password field in the app -- but disclosure is not protection. Adding
TLS is the largest security improvement available to the project and
would touch all four frontends.

Original scope:

The real surface is **3,634 lines of C parsing bytes an attacker
controls**: `rtcm3x_parser.c` (2,527), `ntrip_handler.c`, `ntrip_proto.c`,
`sourcetable.c`, `rinex_nav.c`. A hostile or broken caster feeds all of
them, and there are ~16 fixed-buffer, `memcpy` and `strncpy` sites in the
network-facing files.

- Build `ntrip_core` with `-fsanitize=address,undefined` and replay
  existing `.rtcm3` captures through it — the GUI already writes those
  files, so **the fuzz corpus is a by-product we already have**.
- A libFuzzer harness on the frame path and the sourcetable parser,
  linking `ntrip_core` only, fits the test scaffolding added this month.
- Review the length-field arithmetic in MSM decoding, where an
  attacker-supplied count drives a loop.
- Android: `MainActivity` is exported (correct), `MonitorService` is
  not (correct); justify `ACCESS_FINE_LOCATION`; confirm raw-socket
  NTRIP is unaffected by cleartext-traffic policy.
- Supply chain: **`androidx.security:security-crypto` is pinned at
  `1.1.0-alpha06` and now guards user credentials.** Shipping an alpha
  in a paid app is a decision, not an oversight — take it deliberately
  (1.0.0 is stable).

### Phase 4 — Live GGA implementation — **built**

As designed in `android/design/editions.md`: sending follows the
sourcetable's `nmea` flag in both editions; the fixed position is filled
from the mountpoint's own sourcetable entry, or picked in the user's own
map app; pro reports the phone's live position with a fixed-position
fallback, behind a one-time consent. The uplink itself was verified
against a stub caster (`test/tools/`, `docs/RUNBOOK.md`) — no public
caster advertises an `nmea` mountpoint to test against.

What Phase 5 now has to work from:

- **The consent wording is written** (`gga_consent_body` in
  `strings.xml`): it names the caster, states the ten-second cadence,
  and says the app has no server and sends nothing to its author. The
  privacy policy must say the same in the same terms.
- **`ACCESS_FINE_LOCATION` now has two justifications**, and they are
  not equal: satellite positions for the sky view, read on the device
  in both editions, and — in pro only, after consent — a position
  transmitted to a third-party caster. The data-safety form distinguishes
  those.
- **No background location, and no `location` foreground-service type.**
  Off screen, the last position stands and the fallback carries the run.
  Anything that changes here changes the declaration.

Remaining before launch: a **hands-on pass on the device** — the consent
dialog, the map hand-off and paste, *From station*, and a pro run whose
uplink follows the handset.

### Phase 5 — Release plumbing — **built**

- **Signing** — `signingConfigs` reads `android/keystore.properties`
  (git-ignored, as are `*.jks` and `*.keystore`); absent, the build
  falls back to the debug key and logs that it did.
  `keystore.properties.example` carries the `keytool` invocation and
  what losing the key costs. **The keystore itself is the author's to
  create**: passwords are not something this project's tooling should
  ever handle.
- **Version** — parsed from `src/core/version.h` by
  `app/build.gradle.kts`; `versionCode` is `MMmmpp`.
- **R8** — enabled with `proguard-rules.pro`, and verified rather than
  assumed: a minified pro build returned STATION OK against a live
  caster, with the encrypted store, the JNI and the sky render all
  intact. This closes **open question 7**.
- **Icons** — `tools/make_icons.py` generates the Windows `.ico`, the
  Android bitmaps, adaptive and themed vectors, and the 512 px store
  assets from one geometry. Free and pro differ by accent colour.
- **Names** — `NTRIP Analyser` and `NTRIP Analyser Pro`; "free" in a
  title is promotional text under Play's metadata policy.
- **Privacy policy** — `docs/privacy-policy.md`, published from `/docs`
  via GitHub Pages (`docs/_config.yml`, `docs/index.md`). This closes
  **open questions 3 and 4**: one document, both listings, every
  edition difference marked inside it.
- **Listings and data safety** — drafted in
  `design/work-items/play-listing.md`, with the reasoning behind each
  answer rather than the answers alone.

Left to the author, and listed at the end of that document: enabling
Pages, the contact address, the screenshots (**open question 6**), and
generating the keystore.

### Phase 6 — Samsung S23 verification

On a **release-signed** build, not a debug one. This is also the first
run on Android 13/14 — the test handset is Android 10, so the tightened
foreground-service and notification rules have never been exercised.

**One of those rules was found unhandled, and is now handled.** The app
targets API 35, where a `dataSync` foreground service may run about six
hours in a day before the system calls `Service.onTimeout()` and expects
it to stop within seconds; a service that does not is killed with
`ForegroundServiceDidNotStopException`. Watch mode is exactly such a
service and is sold on running for hours, so on a modern handset an
overnight watch would have ended as a crash. `MonitorService.onTimeout`
now winds the run up through the same path the Stop button uses, reports
the outcome as `LIMIT_REACHED` — *"stopped by Android's six-hour
limit"* — and posts a notification saying so, since a phone that has
been watching all night is in a pocket. Documented in the wiki's *Watch
mode*.

The Android 13 rules it neighbours were already handled: the
`POST_NOTIFICATIONS` runtime request is guarded on `TIRAMISU`, and
location is asked for only when a view needs it.

### Run on the S23, 2026-08-14 — **SM-S911B, Android 16, SDK 36**

Three major versions above the Android 10 handset this app grew up on,
and one above what the plan assumed. Everything below was run from the
**app bundle**, installed through `bundletool` as the exact split set
Play would deliver to that device: `base`, `split_config.arm64_v8a`,
`split_config.nl`.

| What | Result |
|---|---|
| Split install from the bundle | three APKs, no `UnsatisfiedLinkError`, no stuck-at-READY |
| `POST_NOTIFICATIONS` (Android 13+) | prompted on first launch, granted, `USER_SET` |
| Foreground service type | `types=0x00000001` — `dataSync`, accepted by Android 16 |
| Free: station check | settled in 90 s, 47 SV, 2589 B/s |
| Pro: station check | settled in 90 s, 49 SV, sourcetable of **1212** mountpoints parsed |
| Pro: watch mode | 6 minutes, 11 380 C/N0 samples, 1418 ephemerides off the observation stream |
| **Doze** | forced deep idle for 3 minutes mid-watch: service stayed foreground, satellites went 49 → 52, the sky kept updating |
| Stop | service released; `isForeground` count back to 0 |

The orbit badge also showed its third green state for the first time on
hardware — **Station orbits** — because Centipede NEAR broadcasts its own
ephemerides, so nothing was fetched and nothing was guessed.

**What is still owed**: the signature. Everything above ran on a build
signed with the debug key, since the release keystore is the author's to
create. R8, the bundle, the splits and every runtime rule are verified;
only the key is not.

### Checked 2026-08-14: the app must move to API 36, and cannot ship as it stands

Play's own page is unambiguous:

> "Starting August 31, 2026: New apps and app updates must target
> Android 16 (API level 36) or higher to be submitted to Google Play"

That is **seventeen days away**, and this app targets 35. The deadline
cannot be beaten: the developer account is still in verification, and
the closed test then needs twelve testers opted in for fourteen
continuous days. So **targeting 36 is not optional and not deferrable**
— it is the next piece of engineering.

**Done 2026-08-14.** `compileSdk` and `targetSdk` are 36, on **AGP
8.11.2 / Gradle 8.13** — 8.7.3 built against 36 but warned it was
untested, which is not a thing to ship on. `platforms;android-36` and
`build-tools;36.0.0` installed; NDK unchanged at 27.0.12077973.

**Edge-to-edge was the cost, and it did show.** Android 16 will not let
an app targeting 36 opt out of drawing behind the system bars, and the
layout itself held up — the top bar and the content sat correctly — but
**the status bar's own icons stayed light on our light background**: on
the S23 the clock, the signal bars and the battery were all but
invisible. The bars are drawn over our surface now, so their appearance
is ours to set. `AppTheme` sets `isAppearanceLightStatusBars` and
`isAppearanceLightNavigationBars` from the theme, verified in both:
dark icons on the light scheme, light icons on the dark one, toggled
with `adb shell cmd uimode night yes|no`.

Re-verified afterwards on the S23 at target 36: a full check to a
settled verdict, service released, `checkEditionParity`, five tests and
34 release checks — two of them new, one refusing a `targetSdk` below
Play's floor and one insisting the native build still asks for 16 KB
alignment.

### Fixed 2026-08-14: the native library was laid out for 4 KB pages

Separate from the deadline and already blocking: Play has required 16 KB
page support of every app targeting Android 15+ since November 2025, and
`llvm-readelf` showed this project's shipping `libntrip_android.so` with
LOAD segments at `0x1000`. On a device with 16 KB pages the app would
not start at all.

`-Wl,-z,max-page-size=16384` in the native CMakeLists puts both ABIs at
`0x4000`, verified in the bundle rather than in an intermediate, and the
realigned build ran a full check on the S23. NDK 27 did not do this by
default here, which is why it is pinned in the build and checked in
`docs/RUNBOOK.md`.

### Phase 7 — the free wiki — **published** 2026-08-14

Seven pages in `docs/wiki/`, written for a free user who will never see
pro's documentation: Home, Getting started, The eight checks, The
analysis views, Troubleshooting, What the paid edition adds, and
Privacy and support, plus a `_Sidebar`.

They are kept in this repository so they are reviewed and versioned with
the code that they describe — the wiki is a separate repository and
nothing in it would be. Publishing is a copy, and
`tools/publish_wiki.sh` is that copy:

```bash
bash tools/publish_wiki.sh          # show what would change
bash tools/publish_wiki.sh --push   # publish it
```

**The wiki repository does not exist until the first page is saved in
the browser.** Enabling the wiki in Settings is not enough — GitHub
creates it lazily, and until then every link into it redirects to the
repository front page. That is exactly how the app's orbit badge was
found to lead nowhere: the link was right, the destination did not
exist. Twelve pages and the sidebar are live as of 2026-08-14.

**Publishing is a release step, not an afterthought**: the app links
into the wiki from About → Documentation and from the orbit badge on the
Analysis screen, so a docs/wiki change that is not pushed leaves those
buttons describing an older app.

**They must be re-read before launch**, because they state behaviour:
every threshold and every quoted message was taken from `src/core/kpi.c`
and the app's strings on the day they were written, and a wiki that
describes the tool inaccurately is worse than no wiki.

### The data-safety declaration — **written**

`design/work-items/play-data-safety.md`: every question in the console's
form, answered per edition, with what each answer rests on and a
pre-submission checklist of the ways an answer could quietly stop being
true.

The one decision inside it: **pro declares its live position even though
Play's user-initiated-transfer exemption would arguably cover it.** A
tool that measures other people's infrastructure should be the last
thing on a phone to take an exemption for transmitting a location, and a
user comparing the listings should see the difference the paid edition
makes.

### Phase 8 — the pro wiki — **drafted**

Five more pages in `docs/wiki/`, documenting only the differences: what
Pro adds, watch mode, saved connections and the configuration file,
reporting the phone's position, and the ephemeris stream. Each points
back to the free pages for everything shared, and **no free page links
to a pro page**, so the free wiki still stands alone — checked
mechanically, along with every internal link.

One wiki serves both listings: there is one repository, so there is one
wiki. The sidebar carries a Pro section, and a free user who never taps
into it never needs to.

### Phases 7 and 8 — Wiki, then launch

GitHub's wiki is a separate repository (`NTRIP-Analyser.wiki.git`). It is
user documentation, not the developer docs in `docs/`: getting started,
what each KPI means and what to do when one fails, reading the sky view,
importing a navigation file, and troubleshooting.

The **privacy policy needs a stable public URL** for the Play listing —
a wiki page serves, GitHub Pages is tidier. Decide once, use for both.

Free ships first and its wiki must stand alone: a free user never sees
pro's pages. Pro's wiki then documents only the differences.

Going live also **opens the GitHub issue tracker**, which is not in
use until then: from the free release it takes what users report,
while `design/todo.md` keeps the design-level work.

The wiki also carries the **support posture** (`design/telemetry.md`):
a troubleshooting page so a question is answered once, the GitHub
tracker as the route for issues, and no implied help desk behind the
contact address Play requires.

### Phase 8b — the upload artefact — **built** 2026-08-14

`bundleFreeRelease` produces `app-free-release.aab`, 3.7 MB, and it had
never been built here: every release path in this project produced an
APK, which Play refuses for a new app.

Its contents were checked rather than assumed, because a JNI app is
where bundles go wrong quietly — Play generates per-device APKs, and a
bundle missing an ABI installs on nothing without failing a build.
`libntrip_android.so` is present under `base/lib/arm64-v8a/` and
`base/lib/x86_64/`, `res/raw/notices.txt` is in the base module, one
dex, `BundleConfig` present.

**The build is 64-bit only by choice.** A 32-bit-only ARM phone will see
the app as incompatible rather than crash on it. Coherent with
`minSdk 26`, and worth saying on the listing rather than leaving a user
to wonder why it will not install.

⚠ **Not yet verified: the split APKs Play actually delivers.** That
needs `bundletool`, which is not installed here — it would generate the
exact per-device set and install it. Until then the bundle's *contents*
are verified and its *delivery* is not.

### Phase 9 — Play closed testing

Twelve testers, opted in and staying opted in for fourteen continuous
days, before production access can be requested. Nothing here is
engineering; the work is recruitment and patience.

- The invitation lives at the top of `readme.md`, with the opt-in link
  and what a tester is agreeing to.
- Testers need the app to be worth keeping installed for a fortnight, so
  the free edition's own quality is the recruitment argument.
- **What to watch during the fortnight**: Android vitals for crashes and
  ANRs on hardware we do not own, and any station a tester points it at
  that the eight checks read wrongly. That second one is the whole point
  of testing in public.

### Phase 10 — the store studies

Three questions, answered in writing before any packaging:

| Study | Settles |
|---|---|
| Samsung Galaxy Store rules | listing requirements, review criteria, seller-account needs, APK versus AAB |
| F-Droid rules | inclusion policy, licence acceptability, build-from-source and metadata |
| Pro on Samsung, feasibility | whether the paid edition can be sold there at all, and what that does to *the APK is the licence* |

Each ends in a recommendation, not a summary. The licence question above
is the one that can stop a whole branch of this plan, so it goes first.

### Phase 11 — free on the Samsung Galaxy Store

Written from the study, and expected to cover: what the build must
change (if anything), what the listing needs, how updates are published,
and what the review will look for. Not written before the study is done
— a plan built on assumptions about a store's rules is a plan to be
surprised.

### Phase 12 — free from a self-hosted F-Droid repository

Decided; the shape is known and the work is small, but none of it is
built. What it needs:

- **An index, signed.** `fdroidserver` builds one from a directory of
  APKs (`fdroid update`), signed with a key that is *not* the APK
  signing key's business but must be just as carefully kept: whoever
  holds it can push updates to everyone who added the repository.
- **Static hosting over HTTPS.** GitHub Pages already serves `docs/`, so
  the mechanics exist. **But the APKs would then live in git**, a couple
  of megabytes per release, in the history for ever. A separate
  repository — or a branch that is never merged — keeps the binaries out
  of this one. Decide that before the first publish, because moving it
  afterwards changes the URL every user has added.
- **Metadata**: the description, icon and screenshots the client shows.
  These exist already, from the store listing work.
- **The URL, and a QR code for it**, in the readme and the wiki. Adding
  a repository is a deliberate act by the user; make it a short one.
- **A warning worth writing plainly**: a third-party repository is
  trusted by the person who adds it. Say who runs it and what it
  contains.

The tooling specifics above are from `fdroidserver`'s documented
behaviour and should be confirmed against the current version when this
is built rather than taken on faith from this document.

### Phase 13 — pro beyond Play

Deferred until free is out, deliberately: pro's second-store question is
commercial rather than technical, and answering it now would be
answering it without the evidence free's launch produces.

## Decisions

- **Two listings, not an in-app unlock** — entitlement is the installed
  APK, which works in the field with no signal (`editions.md`).
- **Pro's launch scope** — what is built, plus multiple mountpoints
  (done) and live GGA (Phase 4).
- **Price** — decided at listing time, deliberately deferred.
- **F-Droid: our own repository, not the official one** (2026-08-14).
  The official repository requires FLOSS and the Commons Clause forbids
  sale, so the two are mutually exclusive; self-hosting needs no licence
  change and keeps the Clause doing its job. Pro's place in it is a
  later question.
- **TLS after the free launch**, using a bundled library rather than
  each platform's native API, because a JNI bridge for Android would put
  connection logic in Kotlin for one frontend and break the one-core
  rule. Cleartext credentials are the field's prevailing practice; the
  decision is on principle. See `design/tls.md`.

## Open Questions

*(1 and 2 answered 2026-08-13: collect nothing — `design/telemetry.md`;
security-crypto pinned to stable 1.0.0. 3, 4 and 7 answered with phase 5.
Numbers kept stable.)*

3. ~~**Privacy-policy hosting**~~ — **GitHub Pages from `/docs`.** The
   policy is versioned and reviewed with the code it describes;
   `docs/privacy-policy.md`.
4. ~~**Does free need its own privacy policy**~~ — **No: one document,
   both listings.** Every edition difference is marked inside it, which
   is more honest than two texts that can drift; and a reader comparing
   the editions can see exactly what the paid one does differently.
5. **RINEX terms** — does the paused auto-download become possible once
   the IGS terms PDF is read (licence action 5), or stay dropped?
6. **Screenshots showing third-party mountpoints** — keep, or reshoot on
   the author's own stations? (licence action 6)
7. ~~**`isMinifyEnabled`**~~ — **Enabled**, with `proguard-rules.pro`
   and a minified build verified against a live caster. Turning it on
   before the first release means its failures are met on a bench.

## Outcome

*(filled when the work completes)*
