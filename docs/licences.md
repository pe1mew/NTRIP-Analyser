# Licences and usage conditions

What this project is licensed under, what it depends on, and what it
connects to. Written for the Play release: every obligation below has to
be discharged before a listing goes live.

**Not legal advice.** This records what the sources say and what follows
from it in practice; anything marked ⚠ needs the author's decision, and
two items need a lawyer's eye if the paid listing grows.

Last reviewed: 2026-08-13.

---

## 1. What we license out

| Artefact | Licence |
|---|---|
| Code | Apache License 2.0 **with the Commons Clause** |
| Documentation and other non-code content | CC BY-NC 4.0 (per `license.md`) |

**The Commons Clause forbids *selling*, not all commercial use.** It
removes the right to provide the software to third parties for a fee
where the value comes substantially from it — including paid hosting or
support. It does not stop a company using the analyser internally on its
own base stations, which is ordinary commercial use.

⚠ **`license.md` overstates this.** It says "Non-Commercial Use Only …
including any use that generates revenue", which is broader than the
clause in `LICENSE` actually is. Two documents in one repository giving
different answers is the kind of thing that gets quoted back at you.
Recommend aligning `license.md` to the clause's real scope.

**The author is not bound by his own licence**, so a paid Play listing
is consistent with the source being public. Others may not sell it.

⚠ **What the licence does *not* prevent**: someone building the app from
source and publishing it on Play **for free**. Giving it away is not
"selling". If that matters, the deterrents are trademark on the name and
the store listing itself, not the licence.

**Consequence for the wiki**: documentation is CC BY-NC, so third
parties may not reuse it commercially. That is the intent; just note the
wiki inherits it, and say so on the wiki's front page.

---

## 2. What we ship inside the product

| Component | Version | Licence | Obligation |
|---|---|---|---|
| cJSON | vendored in `lib/cJSON` | MIT | Reproduce the copyright and permission notice in the distribution |
| AndroidX core-ktx | 1.15.0 | Apache 2.0 | Attribution / NOTICE |
| AndroidX lifecycle | 2.8.7 | Apache 2.0 | Attribution / NOTICE |
| AndroidX activity-compose | 1.9.3 | Apache 2.0 | Attribution / NOTICE |
| Compose BOM (ui, material3) | 2024.12.01 | Apache 2.0 | Attribution / NOTICE |
| kotlinx-serialization-json | 1.7.3 | Apache 2.0 | Attribution / NOTICE |
| androidx.security-crypto | **1.1.0-alpha06** | Apache 2.0 | Attribution / NOTICE — and see the alpha question in the release work item |
| Kotlin stdlib, Gradle, NDK toolchain | — | Apache 2.0 / build-time | No shipping obligation for build tools |

**Action before launch**: an **open-source notices screen** in the app,
reachable from the About dialog, carrying the cJSON MIT notice and the
Apache 2.0 attribution for the rest. This is the standard discharge and
is currently missing. The Gradle plugin `com.google.android.gms.oss-licenses`
generates one, but adds a Google dependency; a static text resource
generated at build time avoids that.

The desktop builds ship cJSON too — the same MIT notice belongs in the
release archive, not only in the Android app.

---

## 3. The RTCM and NTRIP standards

The message formats this project decodes are defined in **RTCM 10403.x**
(differential GNSS services) and **RTCM 10410.1** (NTRIP). These are
**paid, copyrighted documents** sold by RTCM; membership grants access.

- **Implementing a format from a purchased specification is not
  restricted** by the document's copyright — the copyright covers the
  text, not the interoperability it describes. Open-source
  implementations such as RTKLIB and pyrtcm have done exactly this for
  years.
- **Reproducing the specification's text or tables would be.** Our
  documentation describes message semantics in its own words and cites
  message numbers; keep it that way. Do not paste field tables from the
  standard into `docs/` or the wiki.
- The store listing may say the app decodes RTCM 3.x. It should not
  imply RTCM endorsement or certification.

---

## 4. Data and services the app connects to

The app is a **client**: it connects where the user tells it to, with
the user's own credentials. That keeps the relationship — and the terms
— between the user and the caster. This is the same reasoning that made
the user supply the RINEX file rather than the app downloading it
(`android/design/views.md`).

### BKG GNSS Data Centre (`igs.bkg.bund.de`)

Source of the daily broadcast navigation file used in testing. Open
access; an account is needed only for personalised services, and BKG
states it does not share account data with third parties. BKG disclaims
responsibility for errors in what it publishes.
[Privacy](https://igs.bkg.bund.de/privacy) ·
[Data centre](https://igs.bkg.bund.de/)

### IGS

The data BKG redistributes is IGS data, governed by the IGS **Data and
Product Disclaimer and Terms of Use** (5 August 2020), linked from
[IGS Data Access](https://igs.org/data-access/). IGS operates an open
access policy and publishes citation guidance.

⚠ **Open item**: the full terms are a PDF whose text could not be
extracted mechanically here. Before the app fetches anything from IGS
mirrors automatically — the paused auto-download — read that PDF and
record whether attribution or citation is required of a *client
application*, as distinct from a publication.

### NASA CDDIS

Free and open under NASA's data policy, but **an Earthdata Login is
required** for HTTPS retrieval, and CDDIS publishes per-product **DOI
citations**. That combination — credentials plus a citation obligation —
is why CDDIS is a poor fit for an in-app download and why BKG was used
instead.
[Data use guidance](https://www.earthdata.nasa.gov/engage/open-data-services-software-policies/data-use-guidance)

### Kadaster / NSGI (`ntrip.kadaster.nl`)

Used in the shipped example configuration and throughout testing. Two
services, and the distinction matters:

- **Free**: real-time streams from AGRS.NL stations, North Sea and BES
  islands, **available anonymously** — no registration, port 2101 plain
  or 443 with TLS. An e-mail address as username is optional.
- **Paid**: NETPOS network-RTK, registration via eHerkenning, roughly
  €475 per station per year at the low end.

⚠ **Correction needed**: `readme.md` and `docs/` tell the user to
"substitute your own free Kadaster registration" for the placeholder
credentials in `bin/exampleConfig.json`. Per NSGI, the free streams need
no registration at all. Simplify the instruction and drop the
placeholder credentials from the example.
[NSGI real-time streams](https://www.nsgi.nl/referentiepunten-en-gnss-data/gnss-data/real-time-streams)

### Casters in general

The app connects to third-party infrastructure on the user's behalf.
The wiki and the listing should state plainly that the user is
responsible for holding valid access to any caster they configure, and
that the app neither supplies nor brokers credentials.

---

## 5. Store listing

- **Screenshots** taken against Kadaster and rfsee mountpoints show a
  third party's station identifiers. The free AGRS streams are public
  and anonymous, so showing a mountpoint name is disclosure of nothing
  private — but ⚠ prefer screenshots of the author's own `RFSEE01` /
  `HANESE` where a station is prominent, which removes the question.
- The listing must not imply endorsement by RTCM, IGS, BKG, Kadaster or
  NSGI.
- Naming a caster as an example is factual description, not a claim of
  partnership; keep the wording that way.

---

## 6. Actions this study produced

| # | Action | Blocks | State |
|---|---|---|---|
| 1 | Align `license.md` with the Commons Clause's real scope | — | **done** 2026-08-13 |
| 2 | Add an OSS notices screen (cJSON MIT + Apache 2.0 attribution) | free launch | open |
| 3 | Ship the cJSON notice in the desktop release archive too | next release | open |
| 4 | Fix the "free Kadaster registration" instruction; drop placeholder credentials from the example config | free launch | **done** 2026-08-13 |
| 5 | Read the IGS terms PDF before any automatic download is reconsidered | RINEX auto-download only | open |
| 6 | Decide on screenshots showing third-party mountpoints | listing | open |
| 7 | State caster responsibility to the user | free launch | **partly** — in `readme.md`, `docs/cli.md` and `docs/jsonConfigs.md`; the wiki and listing copy follow in phase 7 |

**Action 4 was verified, not assumed.** `bin/exampleConfig.json` with
empty credentials fetches the Kadaster sourcetable (61 mountpoints) and
returns **STATION OK on all eight KPIs** against `APEL00NLD0` — so the
free AGRS streams are genuinely anonymous and the shipped example runs
unedited.
