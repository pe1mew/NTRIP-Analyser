# Licences and usage conditions

What this project is licensed under, what it depends on, and what it
connects to.

**This is a statement of position, not a work list.** It says what is
true now; where something remains undecided it says so as a condition on
the feature that would need it, not as a task. The history — which
obligation was discharged on which date — belongs in `changelog.md` and
in git.

**Not legal advice.** It records what the sources say and what follows
from them in practice.

Last reviewed: 2026-08-16.

---

## 1. What we license out

| Artefact | Licence |
|---|---|
| Code | [Apache License 2.0 **with the Commons Clause**](../LICENSE) |
| Documentation and other non-code content | [CC BY-NC 4.0](../license.md) |

**The Commons Clause forbids *selling*, not all commercial use.** It
removes the right to provide the software to third parties for a fee
where the value comes substantially from it — including paid hosting or
support. It does not stop a company using the analyser internally on its
own base stations, which is ordinary commercial use.
[`license.md`](../license.md) states the same scope; the two documents
agree.

**The author is not bound by his own licence**, so a paid Play listing is
consistent with the source being public. Others may not sell it.

**What the licence does not prevent**: someone building the app from
source and publishing it on Play **for free**. Giving it away is not
selling. The deterrents against that are the trademark on the name and
the store listing itself, not the licence — a deliberate position, not an
oversight.

**The documentation is CC BY-NC**, so third parties may not reuse it
commercially. The wiki inherits that and says so on its front page.

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
| androidx.security-crypto | 1.0.0 | Apache 2.0 | Attribution / NOTICE |
| Kotlin stdlib, Gradle, NDK toolchain | — | Apache 2.0 / build-time | No shipping obligation for build tools |

Every obligation above is discharged, and by generation rather than by
memory:

- **On Android**, *About → Open-source notices* carries the cJSON MIT
  notice and the Apache 2.0 attribution in both editions, from
  `res/raw/notices.txt`. The Google OSS-licenses Gradle plugin would have
  generated one and added a Google dependency to an app that has none;
  `tools/make_notices.py` writes the text instead.
- **On the desktop and the server**, `THIRD-PARTY-NOTICES.txt` is
  packaged beside the binaries by the `release` target and attached to
  each GitHub release.

**The versions in those notices are read from
`android/gradle/libs.versions.toml`**, the file the build resolves — so a
bumped dependency cannot leave a notice quoting a version nobody ships.
The table above is documentation; the notice is generated, and
`tools/check_release.py` fails the build if the two disagree. That
arrangement exists because this table once named
`security-crypto 1.1.0-alpha06` for weeks after the build had moved to
1.0.0.

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
  message numbers, and it stays that way. Field tables from the standard
  do not get pasted into `docs/` or the wiki.
- The store listing may say the app decodes RTCM 3.x. It does not imply
  RTCM endorsement or certification.

---

## 4. Data and services the products connect to

Every program in the suite is a **client**: it connects where the user
tells it to, with the user's own credentials. That keeps the relationship
— and the terms — between the user and the caster. It is the same
reasoning that makes the user supply the RINEX navigation file rather
than the app downloading it (`android/design/views.md`).

### Kadaster / NSGI (`ntrip.kadaster.nl`)

Used in the shipped example configuration and throughout testing. Two
services, and the distinction matters:

- **Free**: real-time streams from AGRS.NL stations, North Sea and BES
  islands, **available anonymously** — no registration, port 2101 plain
  or 443 with TLS. An e-mail address as username is optional.
- **Paid**: NETPOS network-RTK, registration via eHerkenning, roughly
  €475 per station per year at the low end.

The shipped example therefore carries **no credentials at all** and runs
unedited. That was verified rather than assumed: `bin/exampleConfig.json`
with empty username and password fetches the Kadaster sourcetable — 61
mountpoints — and returns STATION OK on all eight KPIs against
`APEL00NLD0`.
[NSGI real-time streams](https://www.nsgi.nl/referentiepunten-en-gnss-data/gnss-data/real-time-streams)

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

**Condition on a feature that does not exist.** No product fetches
anything from IGS or its mirrors; the user supplies navigation files. If
automatic download is ever reconsidered, the full IGS terms — a PDF whose
text could not be extracted mechanically here — must be read first, and
whether attribution or citation is required of a *client application*, as
distinct from a publication, recorded before any code is written.

### NASA CDDIS

Free and open under NASA's data policy, but **an Earthdata Login is
required** for HTTPS retrieval, and CDDIS publishes per-product **DOI
citations**. That combination — credentials plus a citation obligation —
is why CDDIS is a poor fit for an in-app download and why BKG was used
instead.
[Data use guidance](https://www.earthdata.nasa.gov/engage/open-data-services-software-policies/data-use-guidance)

### Casters in general

The products connect to third-party infrastructure on the user's behalf,
and **say so where a user meets it**: the wiki's *Getting started* carries
a *Whose caster is it?* section beside the fields where a caster is typed
in, *Privacy and support* repeats it, [`readme.md`](../readme.md),
[`docs/cli.md`](cli.md) and [`docs/jsonConfigs.md`](jsonConfigs.md) state
it for the desktop tools, and the listing's privacy paragraph states it
for anyone reading before they install. Holding valid access is the
user's business; no product supplies or brokers credentials.

---

## 5. Store listing

**Screenshots** follow two rules, enforced by the tool that builds them
rather than by anyone remembering:

1. **Capture against the author's own station** where a station is
   prominent, which removes the question of showing somebody else's
   identifiers. The 3.3.0 pro set is `RFSEE01`.
2. **Redact the caster address anyway**, to a domain reserved for
   documentation (RFC 2606 `example.com`), so a listing seen by thousands
   does not advertise a host that belongs to a person and invite traffic
   to it. The mountpoint name and the measurements stay — they are what
   the screenshot is *for*, and a public anonymous stream's name discloses
   nothing private.

The redaction is a table of measured boxes in
`tools/make_store_shots.py`, so a re-capture cannot quietly lose it.
Where a third-party stream is unavoidable, a public anonymous one is
preferred and the same redaction applies.

The listing does not imply endorsement by RTCM, IGS, BKG, Kadaster or
NSGI. Naming a caster as an example is factual description, not a claim
of partnership, and the wording stays that way.
