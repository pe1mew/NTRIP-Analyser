# Pro to Google Play — rollout plan

The item [release-to-play.md](release-to-play.md) has been holding a
place for since it was written: the paid edition's own listing. It
became startable on 2026-08-25, the day 3.8.0 shipped and the
author's hold — *pro does not go to Play until the last feature lands*
— expired with TLS. Everything technical is already true: the pro
release bundle exists signed and artefact-checked, the S23 runs the
release build, the wiki's Pro pages are published and current through
tier 2 and TLS, and the eight checks are identical in both editions,
which is the listing's honesty guarantee.

What remains is **store work, not engineering**: a second listing, a
price, a closed test, and forms only the author can sign. The plan is
short on code and long on decisions, so the decisions come first.

## The shape of the thing

Two listings, not an in-app unlock — decided in `editions.md` and
standing: entitlement is the installed APK, which works in the field
with no signal. That makes pro on Play a **paid app**, and a paid app
touches three console-side mechanisms the free edition never did:

1. **A payments profile.** Selling requires a Google merchant/payments
   profile linked to the developer account, plus the tax and banking
   forms that come with it. Only the author can create this.
2. **The paid flag is set at creation and cannot be toggled later.**
   A free app cannot become paid; the pro listing must be born paid,
   price attached before publish.
3. **Closed testing, again.** Google's rule for personal developer
   accounts — 12 testers opted in for 14 continuous days before
   production access — applies **per app**, not per account. Free
   serving its fortnight does not exempt pro. The saving grace:
   **license testers** install a paid app without paying, so the
   fortnight costs the testers nothing.

## Open, and worth the author's word before S1

**All four answered 2026-08-25** (the author delegated to the
recommendations; the price carries a working figure the author
confirms at S5, where it is actually attached):

1. **Price: €5.00**, one-time (author, 2026-08-26; **saved in the
   console 2026-09-09**, after the region blocks in S3 came out — it
   had been failing to save since the day it was chosen). It replaces
   a recommendation of €12.99 that rested on
   nothing: the agent described a "comparison set at €5-20" it had
   never looked up, the author asked for the real table, and the
   search found there is **no paid NTRIP app market on Play at all**.
   The clients are free because they sell hardware (Eos, Messick's,
   GeoSpot, Bluecover) or are goodwill (Lefebure); the one app that
   does what pro does -- **NtripChecker**, which analyses RTCM
   streams -- is free with 10,000+ installs; above that the jump is
   to full survey suites at $100-300, sold with receivers through
   another channel. So this price does not join a band, it creates
   one, and the honest comparator a buyer reaches for is zero. €5
   is the establish-the-category end of that: installs and reviews
   are what a cold listing lacks most, and raising a price later is
   the move that cannot be made gracefully.
2. **Screenshots: reshoot both listings** on the author's own
   stations, on the S23, 3.8.0's UI. Licence action 6 closes with it.
3. **Testers: free's own** -- the GitHub reporters, via a
   license-tester list, invitation beside free's in the readme.
4. **Mechanics: answered from Play's documentation** -- license
   testing exists precisely so testers use a paid app without paying,
   and the 12x14 closed-test rule is per-app -- **confirmed in the
   console as part of S1's author half** before anything depends on
   it (study-first). The original recommendations' fuller reasoning
   is in this file's history; the answers above are what stand.

## Steps

### S1 — the console groundwork *(author-led; agent prepares text)*

Author: payments profile, tax forms, the pro app entry created
**paid**, package `nl.pe1mew.ntripanalyser.pro`. Agent, in parallel:
the listing copy (short + full description, en and nl, from
`What-the-paid-edition-adds` and the feature matrix — every claim
traceable to a shipped row), and a filled-in worksheet for the
data-safety form: same collect-nothing baseline as free, the
encryption-in-transit answer TLS earned, **plus pro's location
disclosure** — live GGA shares the phone's position with the caster
the user names, after the one-time consent, which the form must say
plainly.

**Verify.** The copy quotes nothing the matrix does not show as ●-pro;
the check count and edition-parity sentences match `Features.kt`.

### S2 — the assets  *(done 2026-08-25 — the captures are a real run's)*

Shot on the S23, release 3.8.0, in one evening session — with the
best possible provenance: the author was already running a **50-minute
watch on APEL00NLD0 over TLS**, so pro's captures are an actual
field run, not a staged one. The author drove the screens; the agent
captured. Pro's set grew beyond the plan: main (the run-aware banner
reading "watch 50 min · stability: STABLE over 0.8 h"), the
**stability card** (STABLE over 0.7 h, six green verdicts with their
evidence), the **hand-over detail** (author's suggestion, mid-session),
the 102k-sample elevation heatmap, the sky with 37 minutes of tracks,
and live signal bars — two new captions joined the tool for the
tier-2 screens. Free's set came from an APEL TLS check plus the
existing conventions.

The redaction gate earned its keep twice: it refused pro's main
because a *running watch* banner sits taller than a finished check's
(the box was re-measured -- the first re-measure was made against the
wrong capture, which the gate also refused), and the author caught
that the **GGA uplink line carries the phone's own position** -- it
now gets the same shape-preserving treatment as the ARP
("52,xxxxx, 5,xxxxx"). AGRS mountpoint names stay visible: NSGI's
free streams are public infrastructure with anonymous access, which
is the terms-allow test the verify line asks.

Open at framing time, author's call at upload: free's `1-main.png`
shows CAUTION (the station's evening wobble was consistent) -- ship
it honest, reshoot on RFSEE01 for a STATION OK, or keep the live
listing's current main. The pro feature graphic is unchanged (not
version-bearing).

*(As planned:)*

The screenshot reshoot (open question 2), both listings in one
session; pro's feature graphic from `tools/make_feature_graphic.py`;
icons exist. Store both listings' raw captures where the free ones
live, framed by the tooling.

**Verify.** Every image shows 3.8.0's UI; no third-party mountpoint
name appears unless its operator's terms allow it; free's refreshed
set is byte-different from the live listing before upload (no stale
re-upload).

### S3 — the closed track  *(in progress 2026-08-26)*

**What the console demanded that the plan did not foresee**, recorded
as it happened:

* **The package name binds at app creation**, not at first upload —
  the agent said the opposite one step earlier and was corrected by
  the form itself. `nl.pe1mew.ntripanalyser.pro`, permanent.
* **The price came forward from S5.** A paid app cannot roll out to
  *any* track, closed included, without a price — and the price is
  padlocked behind a **seller/payments account**, which a free app
  never needed and which Google verifies over days. That account is
  the real critical path of this item, not the forms.
* **The foreground-service declaration demands a video.**
  `FOREGROUND_SERVICE_DATA_SYNC` with the honest answer ("Overig" —
  none of backup/restore, transcoding or import/export describes
  holding an NTRIP stream open) requires a link to a video showing
  the user-visible flow. Recorded on the S23 with adb `screenrecord`:
  run starts, ongoing notification, home screen, still measuring,
  return. Two lessons: the Huawei has no `screenrecord` binary
  (EMUI omits it) and Git Bash mangles `/sdcard/...` into a Windows
  path unless `MSYS_NO_PATHCONV=1` is set; and the recording pulls
  the notification shade down, so **clear personal notifications
  first** — the first take had the author's mail in frame.
* **The internal test track earned its place, for a reason the plan
  never listed.** It buys nothing toward the fortnight — Play says
  plainly that a closed test is still required — but it is the only
  way to exercise the artefact users actually receive: Play re-signs
  the bundle under Play App Signing, so every local `install -r` up
  to now had tested the author's key, not Google's. Verified on the
  S23 on 2026-08-26: `installerPackageName=com.android.vending`,
  versionName 3.8.0, versionCode 30800, no DEBUGGABLE flag, and the
  app runs. Two mechanics worth remembering: **tracks do not share
  releases** (the bundle uploaded to the closed draft leaves the
  internal release empty, which reads as three separate errors), and
  a version code exists once per app, so the second track attaches
  it with **"Toevoegen vanuit bibliotheek"** rather than a re-upload.
* **The pro feature graphic overflowed its canvas** and the listing
  showed "NTRIP Analyser P". `make_feature_graphic.py` sized text
  against free's shorter title and never measured; it now fits to
  the available width and *asserts* both lines fit before saving.
  Free's graphic and the social preview regenerated byte-identical,
  which is the fitter proving it only shrinks what overflows.
* **A paid app cannot sell to the whole world.** The price refused to
  save for two weeks behind the console's generic *"Je wijzigingen
  kunnen niet worden opgeslagen"*, with nothing marked on the page.
  The API's own 400 named the reason and the console threw it away:
  **"Rest van de wereld"** has to go, and then **China, Cuba, Iran
  and Soedan**. All five sit in the *tracks'* country lists as well,
  so clearing the price table alone does not satisfy it. €5 saved
  2026-09-09 with the five removed — which unblocks the rollout.
* **Pro was paid all along.** Google support replied that an app saved
  as free can never become paid and that pro needed a new package
  name; the agent believed it and sized the rename. The console's
  pricing page said *"Je app is: Betaald"*, with *"Je app gratis
  maken"* beside it and *"Je kunt je app wijzigen van betaald in
  kosteloos totdat je publiceert"* above — which also proves pro has
  never been published. The rename, in-app billing (which would have
  overturned `editions.md`) and selling outside Play were three
  answers to a problem that did not exist.

*(As planned:)*

Upload the existing, artefact-checked `app-pro-release.aab` (3.8.0 —
no new build; the release that exists is the release that tests),
create the closed track and license-tester list, publish the tester
invitation in the readme and the wiki's Pro page.

**Verify.** A license tester (the author's second account, or the
S23's) installs from the track without payment; the installed
`versionCode` is 30800.

### S4 — the fortnight

Recruitment and patience, as free's was. Watch Android vitals for
crashes and ANRs on hardware we do not own; every station a tester
points it at that the checks misread is the reason to test in
public. Fixes ship as 3.8.x patches to the track — each through the
ordinary release sequence, tag and all.

### S5 — production

When the fortnight completes: apply for production access, attach the
price and countries (answer 1), publish. The data-safety form goes
live with the S1 worksheet's answers — including the corrected
understanding that **encryption in transit stays "No"** while TLS is
per-connection opt-in (`play-data-safety.md` says why); the optional
free-text context is where TLS gets its honest credit, on both
listings at one sitting.

### S6 — say so

The readme's edition table gains pro's Play link; the wiki Pro page's
"where to get it"; `release-to-play.md`'s status table closes its pro
row and Phase 13 (pro beyond Play) inherits whatever the fortnight
taught; MEMORY's current state. No changelog entry — store
availability is not a change to the software.

## Out of scope, stated

- Pro on the Samsung Galaxy Store or the self-hosted F-Droid
  repository — Phase 13 of the master plan, deferred until this
  completes and deliberately fed by its evidence.
- Any code change beyond 3.8.x fixes the fortnight surfaces. The
  listing sells what is built.
- KPI 9 / latency (measurement-tiers) — it renames a listing claim,
  and it waits for this item, not the other way round.

## Process

On `main`, step commits as ever — no branch: this item is console
work, documents and assets, with no half-landed state that could
strand a release. The author owns every console action, every form,
and every publish, as the runbook's *the agent never publishes* rule
already says; the agent prepares text, drives devices for
screenshots, and verifies artefacts.
