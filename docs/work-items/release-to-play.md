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
| 1 | Telemetry decision | privacy policy, data-safety form | **done** → `design/telemetry.md`; collect nothing |
| 2 | Licence study | store listings, wiki claims, RINEX auto-download | **done** → `docs/licences.md`, 7 actions |
| 3 | Security assessment | any public release | **done** → `docs/security-review.md`; 6 of 7 closed, TLS open |
| 4 | Live GGA implementation | pro's launch scope | designed, not built |
| 5 | Release plumbing | submission | not started |
| 6 | Samsung S23 verification | submission | not started |
| 7 | Wiki (free) → launch free | pro launch | not started |
| 8 | Wiki (pro) → launch pro | — | not started |

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
  `docs/work-items/play-listing.md`, with the reasoning behind each
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

Checked statically, since the timeout cannot be provoked on Android 10.
What the handset does confirm is the path it delegates to. **The
device-side items are what Phase 6 still owes**: a release-signed
install, a watch that survives Android 13/14 notification behaviour, and
a run under Doze.

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

`docs/work-items/play-data-safety.md`: every question in the console's
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

## Decisions

- **Two listings, not an in-app unlock** — entitlement is the installed
  APK, which works in the field with no signal (`editions.md`).
- **Pro's launch scope** — what is built, plus multiple mountpoints
  (done) and live GGA (Phase 4).
- **Price** — decided at listing time, deliberately deferred.
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
