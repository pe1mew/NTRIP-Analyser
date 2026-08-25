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

1. **The price.** "Decided at listing time" — this is listing time.
   Recommendation: a one-time price in the **€8–€15** band. The
   comparison set (NTRIP clients and GNSS field tools with far less
   measurement in them) sits at €5–€20; the pro edition carries the
   stability report, VRS check, hand-over watch, tracks, export and
   watch mode, and underpricing a measurement tool reads as a toy.
   The exact figure, the countries list and any launch discount are
   the author's alone. *(Nothing here is financial advice; it is a
   product-positioning recommendation.)*
2. **Screenshots: reshoot both listings on the author's own stations**
   (licence study action 6, open since 2026-08-13). Recommendation:
   yes, in one session — rfsee.net and the free AGRS mountpoints,
   3.8.0's UI with the TLS checkbox visible, driven on the S23 the
   way L5 drove the Huawei; `tools/make_store_shots.py` frames them.
   One reshoot ends the third-party-mountpoint question for good.
3. **The tester pool.** Recommendation: recruit from the free
   edition's existing testers (the GitHub issue reporters are exactly
   the people who kept it installed) via a license-tester list and a
   closed track, invitation at the top of the readme beside free's.
4. **Confirm the console mechanics before planning around them**
   (study-first, the store-study rule): that license testers satisfy
   the 12×14 requirement for a paid app, and whether free's own track
   status changes anything. One session in the console settles both;
   the plan assumes nothing until then.

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

### S2 — the assets

The screenshot reshoot (open question 2), both listings in one
session; pro's feature graphic from `tools/make_feature_graphic.py`;
icons exist. Store both listings' raw captures where the free ones
live, framed by the tooling.

**Verify.** Every image shows 3.8.0's UI; no third-party mountpoint
name appears unless its operator's terms allow it; free's refreshed
set is byte-different from the live listing before upload (no stale
re-upload).

### S3 — the closed track

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
price and countries (open question 1), publish. The data-safety form
goes live with the answers from S1 — and free's form gets the same
encryption-in-transit improvement at the same sitting if it has not
already.

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
