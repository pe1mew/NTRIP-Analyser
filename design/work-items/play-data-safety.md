# Play data safety — the declaration, question by question

**Two listings, two declarations.** They differ in exactly one answer,
and that answer is the reason the two editions exist as separate
listings at all.

Transcribe this into the Play Console. Google's wording changes; where
it has, **follow the console and correct this file**, but do not change
an answer without changing the thing it describes.

Sources: `design/telemetry.md` (nothing is collected, and why),
`android/design/editions.md` (what pro transmits and when),
`docs/privacy-policy.md` (what the user is told), `design/tls.md` and
`design/work-items/tls-rollout.md` (TLS shipped 3.8.0; what that does
-- and deliberately does not do -- to the answers below).

---

## What the app actually does with data

Everything below follows from these four facts. If one of them stops
being true, the declaration is wrong.

1. **There is no server.** No analytics SDK, no endpoint belonging to
   the author, no background activity of any kind. What Play Console
   reports about installs and crashes is Google's own collection, not
   ours, and is not declared here.
2. **Credentials and configuration stay on the device**, encrypted at
   rest, and are sent only to the caster the user typed in — to log in
   to it.
3. **Free never transmits a position it did not get from the user.**
   Location is read on the device for the sky view. The GGA a network
   mountpoint receives is the fixed position the user typed, picked on a
   map, or took from the station's own sourcetable entry.
4. **Pro can transmit this phone's position** to that caster, about
   every ten seconds during a run, after an explicit one-time agreement
   naming the caster, and only for mountpoints that ask for a position.

---

## Section 1 — Data collection and security

| Question | Free | Pro |
|---|---|---|
| Does your app collect or share any of the required user data types? | **No** | **Yes** |
| Is all of the user data collected or shared by your app encrypted in transit? | *(not asked)* | **No** |
| Do you provide a way for users to request that their data is deleted? | *(not asked)* | **No** |

**Why "No" for free.** The only data leaving the device goes to the
caster the user configured, at the moment they tap Run, which is Play's
*user-initiated transfer* exemption: the user actively initiates it and
is aware the data goes to that third party. Nothing else is transmitted
anywhere.

**Why "Yes" for pro**, on the same reasoning that would have allowed
"No": the live position is arguably exempt too — the user ticks a
switch, agrees to a dialog naming the caster, and starts the run. We
declare it anyway. A tool that measures other people's infrastructure
should be the last thing on the phone to take an exemption for
transmitting a location, and a user comparing the two listings should
be able to see the difference that the paid edition actually makes.

**Why "No" to encryption in transit — still, after TLS (2026-08-25).**
TLS shipped in 3.8.0, both editions, and the answer does not flip:
the form asks whether **all** shared data is encrypted in transit, and
TLS is a per-connection choice the user makes caster by caster. A
plain-text run remains a permitted path, so the honest binary answer
remains **No** -- the form describes the worst permitted path, not the
best available one. (An earlier version of this file, and a session
note of 2026-08-25, said the answer would become "Yes" when TLS
landed; that was optimism about a binary form.) What TLS does change:
the in-app disclosure now follows the checkbox, and the free-text
context in the console may say that TLS is available and verified on
every connection where the caster offers it. The answer flips only if
TLS ever becomes required or default-on -- a product decision nobody
has taken.

**Why "No" to deletion requests.** The developer holds nothing to
delete. Everything is on the user's device and goes with the app when it
is uninstalled — which the privacy policy says in those words.

---

## Section 2 — Data types

### Free

**No data types are declared.** Work through the categories and mark
every one *not collected, not shared*:

Location · Personal info · Financial info · Health and fitness ·
Messages · Photos and videos · Audio files · Files and docs · Calendar ·
Contacts · App activity · Web browsing · App info and performance ·
Device or other IDs.

### Pro

One type is declared. Everything else is marked *not collected, not
shared*, exactly as for free.

| Field | Answer |
|---|---|
| Data type | **Location → Precise location** |
| Collected | **No** |
| Shared | **Yes** |
| Processed ephemerally | No |
| Required or optional | **Optional** — the user can decline and the app works |
| Purpose | **App functionality** only |

**Collected "No", shared "Yes"** is deliberate and is the honest shape
of it: the position is not gathered *by us* — there is nowhere for it to
go — it is passed to a third party the user nominated. Ticking
"collected" would imply a server that does not exist.

**Purposes not to tick**, and they are all absent for a reason:
Analytics, Advertising or marketing, Personalization, Account
management, Developer communications, Fraud prevention.

---

## Section 3 — Security practices

| Practice | Answer |
|---|---|
| Data is encrypted in transit | **No** (pro; not asked for free) — see Section 1: TLS is opt-in per connection, and "all" means all |
| You can request that data be deleted | **No** |
| Committed to follow the Play Families Policy | **No** — a professional measurement tool, not directed at children |
| Independent security review | **No** — there has been a review (`design/security-review.md`), but it is the author's own, and claiming otherwise would be a lie about its independence |

---

## Questions the form does not ask, and the answers if it is ever asked

- **Caster username and password.** Sent to that caster to log in, as a
  browser sends a password to the site the user typed. Stored encrypted
  on the device. Not declared, on the user-initiated exemption; if a
  reviewer ever disputes it, the honest declaration would be *Personal
  info → User IDs, shared, optional, app functionality* — and the answer
  would be the same in both editions.
- **An imported RINEX navigation file.** Copied into the app's private
  storage and never transmitted. Not declared.
- **Satellite positions from the phone's GNSS.** Held in memory while a
  view is open, never stored, never transmitted in either edition.

---

## Before every submission

Read this list before touching the form; each item is a way the answers
above could quietly stop being true.

- [ ] **Any new dependency?** An SDK that phones home makes its
      collection ours to declare. There is none today, and
      `androidx.security-crypto`, kotlinx-serialization and Compose do
      not transmit anything.
- [ ] **TLS landed 2026-08-25 and the answer stayed "No"** -- it is
      opt-in per connection, and the form's "all" means all. Re-answer
      only if TLS becomes required or default-on.
- [ ] **Has anything new left the device?** A shared report, a crash
      uploader, a "send us your log" button — none exist, and each would
      need declaring.
- [ ] **Does the privacy policy still match this page?** They are two
      statements of one fact and must never disagree.
- [ ] **Free's answer is still "No".** If free ever transmits a position
      the user did not set, that is a different app and a different
      declaration.
