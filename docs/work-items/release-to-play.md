# Release the Android app to Google Play

## What & Why

Two Play listings — free and pro — built from the flavors that exist.
Everything between here and there: telemetry, a security assessment, a
licence study, user documentation on the GitHub wiki, and the store
plumbing itself.

The ordering below is not preference. Each phase produces something a
later phase needs, and three of them are *decisions* that block the
privacy policy and the data-safety declaration, which block submission.

## Current Status

Phases 2 and 3 are done. Phase 1 (telemetry) is a decision and blocks
the privacy policy; 4 onwards are sequential.

| # | Phase | Blocks | State |
|---|---|---|---|
| 1 | Telemetry decision | privacy policy, data-safety form | not started |
| 2 | Licence study | store listings, wiki claims, RINEX auto-download | **done** → `docs/licences.md`, 7 actions |
| 3 | Security assessment | any public release | **done** → `docs/security-review.md`; 6 of 7 closed, TLS open |
| 4 | Live GGA implementation | pro's launch scope | designed, not built |
| 5 | Release plumbing | submission | not started |
| 6 | Samsung S23 verification | submission | not started |
| 7 | Wiki (free) → launch free | pro launch | not started |
| 8 | Wiki (pro) → launch pro | — | not started |

---

### Phase 1 — Telemetry: informative but innocent

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

Report in `docs/security-review.md`. Two defects found and fixed: RTCM
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

### Phase 4 — Live GGA implementation

Designed in `android/design/editions.md`, not built: sending follows the
sourcetable's `nmea` flag in both editions; free sends a fixed position
prefilled from the mountpoint's sourcetable entry; pro sends the phone's
live position with a fixed-position fallback, behind a one-time consent.

The consent dialog and what it says are **inputs to the privacy policy**,
so this lands before Phase 5.

### Phase 5 — Release plumbing

Signing config and a keystore that is backed up and never committed;
`versionName` wired to `src/core/version.h` instead of hand-maintained;
`versionCode` scheme; icon check (currently plain bitmaps, no adaptive
variant); `isMinifyEnabled` and a ProGuard pass over reflection used by
kotlinx-serialization; the privacy policy (Phase 1 + 4 decide its
content); the Play data-safety declaration; store listings.

### Phase 6 — Samsung S23 verification

On a **release-signed** build, not a debug one. This is also the first
run on Android 13/14 — the test handset is Android 10, so the tightened
foreground-service and notification rules have never been exercised.

### Phases 7 and 8 — Wiki, then launch

GitHub's wiki is a separate repository (`NTRIP-Analyser.wiki.git`). It is
user documentation, not the developer docs in `docs/`: getting started,
what each KPI means and what to do when one fails, reading the sky view,
importing a navigation file, and troubleshooting.

The **privacy policy needs a stable public URL** for the Play listing —
a wiki page serves, GitHub Pages is tidier. Decide once, use for both.

Free ships first and its wiki must stand alone: a free user never sees
pro's pages. Pro's wiki then documents only the differences.

## Decisions

- **Two listings, not an in-app unlock** — entitlement is the installed
  APK, which works in the field with no signal (`editions.md`).
- **Pro's launch scope** — what is built, plus multiple mountpoints
  (done) and live GGA (Phase 4).
- **Price** — decided at listing time, deliberately deferred.

## Open Questions

1. **Telemetry stance** — vitals only, or opt-in counters we host? Gates
   the privacy policy.
2. **`security-crypto` alpha** — ship `1.1.0-alpha06`, or move to
   `1.0.0` stable before a paid release?
3. **Privacy-policy hosting** — wiki page or GitHub Pages?
4. **Does the free wiki need its own privacy policy** distinct from
   pro's, given only pro transmits a position?
5. **RINEX terms** — does the paused auto-download become possible once
   Phase 2 answers the BKG/IGS conditions, or stay dropped?

## Outcome

*(filled when the work completes)*
