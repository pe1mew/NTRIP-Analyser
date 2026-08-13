# Memory

<!-- NOT loaded automatically. A task-triggered pointer in CLAUDE.md is what
     brings it into a session; without that row nothing here is ever read,
     and the failure is silent.

     Keep it lean — it is read in full whenever it is reached, and it is an
     INDEX: deep knowledge belongs in topic files, and most of this project's
     deep knowledge already lives in design/ and docs/. Point at those rather
     than restating them, or the copy drifts from the original.

     END-OF-SESSION CURATION (/curate):
     1. Review gotcha-log for recurring patterns — promote them here or to a topic file
     2. Retire entries that are fixed, refactored away, or now encoded in code
     3. Update "Current State" to reflect what shipped or changed
     Monthly (/audit-context): prune as much as you add. -->

## Topic Files

| File | When to load | Key insight |
|------|-------------|-------------|
| `memory/gotcha-log.md` | Stuck, or something behaves impossibly | Problem→root cause→fix archive; six entries are resolved history |
| `memory/doxygen-in-headers-only.md` | Writing or moving documentation comments | Doxygen merges header and `.c` blocks — document the declaration only |
| `design/architecture.md` | Touching `src/core`, `src/session`, or adding a frontend | Why the session layer exists and what the snapshot guarantees |
| `design/todo.md` | Asking "does X already exist?" | Shipped vs planned, stable item numbers, and rejected ideas with reasons |
| `design/gui-design.md` | Any `gui/` work | Window patterns; §13 is the station check as built |
| `android/design/editions.md` | Any Android product decision | Free/pro split, payment model, profiles, GGA position sources |
| `android/design/views.md` | Android UI or sky-plot work | What each view answers, and where orbits actually come from |
| `android/design/design-review.md` | Changing anything cited as `design-review Dn` | Decisions D1–D7, dated, referenced from seven code sites |
| `docs/jsonConfigs.md` | Config reading, writing or interop | One format everywhere; passwords in the clear |
| `docs/security-review.md` | Touching a parser, the socket layer, or anything a caster feeds | What a hostile caster can do; six findings closed, TLS scheduled |
| `design/tls.md` | Implementing TLS, or asked why it is not there yet | The decision, the measured surface, and what actually costs |

## Current State

<!-- 2026-08-13 -->

- **v3.3.0 released** on the desktop; substantial unreleased work in `changelog.md`.
- **Shipped recently**: KPI 8 (advertised versus actual) across all frontends;
  the station check in the Windows GUI; RINEX GLONASS fixes plus the project's
  first regression test; Android saved connection profiles with encrypted
  credentials; one shared JSON config format everywhere.
- **Decided, not built**: GGA position sources — sending follows the
  sourcetable's `nmea` flag; free sends a fixed position prefilled from the
  sourcetable entry; pro sends the phone's live position after a one-time
  consent. See `android/design/editions.md`.
- **In progress**: getting both editions onto Google Play →
  `docs/work-items/release-to-play.md` [in progress] — eight phases, of which
  telemetry, licences and the security assessment are decisions that gate the
  privacy policy and the data-safety declaration.
- **Known gaps**: no CI; two tests; the GUI station check has no saved report;
  seven pro rows in the editions table are marked *planned*, not built.

## Recently Promoted

<!-- Format: "if [situation], then [what to do] — promoted from gotcha-log YYYY-MM-DD"
     Retire an entry as soon as it appears in its destination. -->

*(nothing yet — the loop starts at the next session)*

## Key File Paths

Supplementing CLAUDE.md's list with paths found during work:

- `src/core/ns_stats.{c,h}` — the snapshot every frontend renders; `null` in
  its JSON means "not measured", so Kotlin models must be nullable.
- `src/net/ntrip_handler.c` — derives Basic auth when `AUTH_BASIC` is empty;
  a sourcetable fetch fails without it.
- `gui/gui_parsers.c` — sourcetable rows, advertised types, handshake parsing.
- `android/app/src/main/java/nl/pe1mew/ntripanalyser/Settings.kt` — encrypted
  profile store and the migration from the pre-profiles preferences file.
- `service/monitord.example.json`, `bin/exampleConfig.json` — the shared
  config format, daemon side and desktop side.

## Active Decisions

- **A learning project, held to professional standards.** No revenue goal
  and no growth to optimise, so commercially-argued proposals are
  off-target — but the engineering bar is a professional one, and "only a
  learning project" is never a reason to skip a test, a review or a
  verification (`CLAUDE.md`).
- **One measurement core, four frontends** — no threshold or verdict in any
  UI layer (`design/architecture.md`).
- **Android ships as two Play listings, not one app with an in-app unlock** —
  entitlement is the installed APK, which works in the field with no signal
  (`android/design/editions.md`).
- **One JSON exchange format everywhere**, a `mountpoints[]` list; analysers
  read the first entry and say how many they ignored (`docs/jsonConfigs.md`).
- **The app never downloads a navigation file** — the user supplies it, so the
  licence relationship stays theirs (`android/design/views.md`).
- **KPI 8 judges constellations by the sourcetable's NavSys field**, never by
  the 1005/1006 indicator bits, which cannot express BeiDou.
- **The app collects no telemetry** — no SDK, no endpoint, no consent flow.
  Installs and daily-use figures come from Play Console; the shared report
  is for the user to hand to their caster operator, not a line to the
  author. Support is deliberately minimal: the product and the wiki answer
  the questions, issues go to GitHub (`design/telemetry.md`).
- **TLS is coming, after the free launch**, as a bundled library behind a
  transport abstraction — chosen over per-platform native APIs to keep one
  code path for four frontends. It ships in **both editions** on the same
  day: the paid edition withholds convenience, never protection
  (`design/tls.md`).

---
