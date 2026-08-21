# GUI v3 — the template, as a specification

The app has had three shapes. **v1** was two screens and a pager of three
plots. **v2** (3.6.0, `guiV2rollout.md`) made the station screen a hub of
panels with drill-down, and made the panel registry the layout. **v3** is
this document: the same panels, held to one frame that every screen obeys.

The frame is not mine. It is drawn in
[`work-items/guiReview/guiTemplate/`](work-items/guiReview/guiTemplate/)
— eight slides, of which Dia2 and Dia5/6 carry the rules in words and
Dia3, Dia4, Dia7 and Dia8 show them applied. This file translates those
slides into something implementable, adds the second directive (say
*what* failed when a connection fails), and states where the phase-2
capabilities land inside the frame so that adding one is a filling-in
rather than a redesign.

**What does not change.** The measurement, the eight KPIs, their
thresholds and their wording; the `Panel` contract; the per-edition
`Registry.kt` and the order it fixes; share. v3 is a frame specification.
Where it touches behaviour, it says so and says why.

---

## 1. The shell

Every screen in the app is three bands and the system's own navigation:

```
┌─────────────────────────────────────────┐
│  status bar (system)                    │
├─────────────────────────────────────────┤
│  ← │ NTRIP Analyser        │  ⤴  │  ⋮   │   top bar — always on
├─────────────────────────────────────────┤
│                                         │
│  content                                │   scrolls, or is banded
│                                         │
├─────────────────────────────────────────┤
│  Analysis                          ▶   │   main screen only
├─────────────────────────────────────────┤
│  ⦀    ◯    ‹                           │   system navigation
└─────────────────────────────────────────┘
```

### 1.1 The top bar

Four slots, in this order, on every screen:

| Slot | Main screen | Any other screen |
|---|---|---|
| **Leading** | empty | `←` back |
| **Title** | `NTRIP Analyser` / `NTRIP Analyser Pro` | the same |
| **Share** | `⤴` | `⤴` |
| **Overflow** | `⋮` | `⋮` |

The title is the **app name**, never the screen name. Dia5 and Dia6 are
explicit about this, and it is what makes the frame read as one app: the
screen you are on is said by the back arrow and the tabs, not by a
changing title.

Two consequences for what exists today:

- The analysis screen's title changes from `Analysis` to the app name,
  and its `Back` text button becomes a `←` in the leading slot.
- The hub's `☰` moves out of the leading slot and becomes the `⋮`
  overflow on the right. The leading slot is reserved for back, and
  is empty exactly when there is nowhere to go back to.

### 1.2 The overflow menu

One menu, both editions, same order (Dia1):

| Row | free | pro |
|---|---|---|
| Settings | ● | ● |
| Import RINEX | ● | ● |
| Load / save configuration | — | ● |
| About / notices | ● | ● |

A pro-only row is **absent** in free, not greyed — the rule already
settled for the hub in v2 (a disabled control is indistinguishable from a
broken one to somebody who has not paid), applied to the menu.

### 1.3 The analysis bar

Pinned to the bottom of the **main screen only**; gone on every other
screen, including the analysis screens themselves (Dia2, rule 3).

| State | Appearance | Behaviour |
|---|---|---|
| Nothing measured yet | grey, muted label | inert |
| A run is live, or results exist | emphasised (dark), `▶` at the right | opens the analysis screens |

It is part of the shell, not a panel. That is the point of it: the way
into the plots is in the same place whatever the hub is showing, and it
cannot be scrolled away — which is what happens today, where **Analysis**
sits beside **Run the check** inside a card and disappears under eight
KPI rows the moment a run starts (`Screenshot_20260821_204312`).

---

## 2. The affordance grammar

This is the substance of the template, and the part with no counterpart in
the app as built. Dia2 states it and Dia4 shows a screen full of it:

> A clickable item has a triangle to indicate an action: right — there is
> more to configure; down — there is more information and will fold open
> to display; up — information can be folded away.

Formalised:

| Marker | Meaning | Example |
|---|---|---|
| `▶` | **Forward** — this row leads somewhere: a dialog, a screen, or the start of a run | Connection card, Browse mountpoints, Run the check, the analysis bar |
| `▼` | **Expand** — there is more to read, and it will fold open in place | The eight KPI rows, Satellite orbits |
| `▲` | **Collapse** — it is folded open; this folds it away | Any expanded row |
| *(none)* | Not clickable | The verdict card, the chips row |

**One deviation from the literal wording, deliberately.** The slides give
`▶` to *Run the check* and to the analysis bar, and neither is
configuration. So `▶` means *forward* — a dialog, a screen, or an action
that moves the app on — and the "more to configure" case is the most
common instance of it, not the definition. Without this, two rows in the
author's own mockup would be unmarkable.

**Where the marker comes from.** Not from each card. A row's marker is
part of the `Panel` contract and is drawn by the hub, exactly as the hub
already draws the card and the click target:

```kotlin
enum class Affordance { NONE, FORWARD, EXPAND, COLLAPSE }

interface Panel {
    // ...
    fun affordance(state: HubState): Affordance = 
        if (destination() != null) Affordance.FORWARD else Affordance.NONE
}
```

A new panel is then marked correctly by existing, a panel cannot forget
its marker, and the marker cannot drift between the editions — the same
argument that made the registry the layout in v2.

**Geometry** (from Dia4, where eight rows make the alignment visible):
the marker is right-aligned, vertically centred in the row, `12.dp` from
the card's right edge, and does not move when the row folds open. Its
touch target is the whole card, never the triangle alone.

---

## 3. The main screen

Order is the registry's, unchanged from v2. What v3 adds is the marker
column and the missing dialogs.

| Row | Marker | On tap | Present in |
|---|---|---|---|
| Verdict (`READY` / `RUNNING` / `STATION OK` / `CAUTION` / `FAILED`) | none | — | both |
| Connection — caster, mountpoint, credentials | `▶` | connection dialog | both |
| Browse mountpoints | `▶` | sourcetable dialog | both (read-only in free) |
| Chips — B/s, SV, mountpoint | none | — | both |
| **Failure** — one sentence saying what went wrong | `▶` | connection dialog, on the field at fault | both (§5) |
| Watch | `▼` | folds open | pro |
| KPI 1…8 | `▼` | folds open | both |
| Run the check / Stop | `▶` | starts, or stops | both |
| Satellite orbits | `▼` | folds open | both |
| Setup hint | none | — | both |
| More in Pro | `▶` | the store listing | free |

The verdict card carries no marker because it is a statement, not a
control. Dia2 and Dia4 both draw it without one.

---

## 4. The analysis screens

Dia5 gives the anatomy; Dia6, Dia7 and Dia8 show it filled in three ways.
Six bands, fixed, in this order:

```
┌─────────────────────────────────────────┐
│  ← │ NTRIP Analyser        │  ⤴  │  ⋮   │  1  shell top bar
├─────────────────────────────────────────┤
│  Sky view   Signal quality   C/N0 vs el │  2  selector tabs
│  ───────────                            │     with a selection indicator
├─────────────────────────────────────────┤
│  Showing what the station check         │  3  explainer — what this
│  captured. …                            │     view is, in prose
├─────────────────────────────────────────┤
│  34 of 38 satellites shown · phone GNSS │  4  summary — the numbers
│  4 without a position — measured, but   │     behind the picture
├─────────────────────────────────────────┤
│                                         │
│              the plot                   │  5  fills what is left
│                                         │
├─────────────────────────────────────────┤
│  RFSEE01  ARP: 52,211516, 5,983710      │  6a footer (sky view only)
│  ● GPS ● GLONASS ● Galileo ● BeiDou     │  6b legend
└─────────────────────────────────────────┘
```

Rules, from Dia6's numbered list:

1. **The bands do not scroll vertically.** The screen is what fits; the
   plot takes the slack. (P1.4a's `PlotLayout` already reserves a minimum
   plot height and lets a short screen scroll — that stays as the
   exception for small devices, and is the one place this rule bends.)
2. **The band below the tabs pages horizontally**, left and right, and
   the tabs follow. This is the existing `HorizontalPager` — v2's P1.7
   removed the *leave* gesture, not the pager, so there is nothing to
   restore and nothing to remove.
3. Back returns to the hub. There is no other way out, and the analysis
   bar is not drawn here.

**Three bands per view, filled:**

| View | Explainer | Summary |
|---|---|---|
| Sky view | "Showing what the station check captured…" | `34 of 38 satellites shown · phone GNSS` + what could not be placed |
| Signal quality | `Live value, this epoch` | `38 satellites · mean 47,7 dB-Hz · range 34,6 to 52,0` |
| C/N0 vs elevation | "A healthy antenna climbs smoothly from the horizon to zenith…" | `782 samples this session` |

Two things move to get here. Today the explainer sits **above** the tabs
and the summary below them; the template puts the tabs first, so the tab
row is the first thing under the app bar on every analysis screen and the
prose belongs to the view rather than to the screen. And today's
`Phone GNSS` badge — a filled pill in the top bar — has no slot in the
template. Its content is already in the summary line (`· phone GNSS`), so
the badge goes and the summary line carries the source. See the open
decision in the rollout plan for what happens to the link it carried.

---

## 5. Saying what failed

### 5.1 What the app says today, and why

Everything, in one sentence. `MonitorService.kt:125`:

```kotlin
_state.value = RunState(running = false, error = getString(R.string.err_open), …)
```

```xml
<string name="err_open">Could not open the session.</string>
```

That single string covers a mistyped host, a wrong port, a wrong
password, a mountpoint that does not exist and a caster that is down. If
the session opens and then fails, the user gets KPI 1's
`No connection to the caster` instead — equally undifferentiated.

The information exists and is thrown away at three separate points:

- **`src/net/ntrip_handler.c`** prints the real cause to `stderr` —
  `DNS lookup failed: %s` at line 117, the socket and connect failures at
  125 and 145 — and returns a bare failure. On Android nothing reads
  `stderr`.
- **`src/session/ntrip_session.c`** collapses all of it into
  `NS_END_NET_ERROR` (four call sites) or `NS_END_REJECTED` (one), and
  the Android bridge does not handle `NS_EV_DISCONNECTED` at all, so even
  that distinction never crosses the JNI boundary.
- **`NsStatsSnapshot::http_status`** already carries `401` / `404` and is
  serialised into the JSON the app parses. Nothing renders it.

So this is not a new measurement. It is a value the core already knows,
kept instead of discarded.

### 5.2 The taxonomy

One enum in the session layer, because the session is what every frontend
drives:

```c
/** @brief Why a connection could not be established, or did not last. */
typedef enum {
    NS_FAIL_NONE = 0,
    NS_FAIL_DNS,            /**< the host name does not resolve        */
    NS_FAIL_REFUSED,        /**< TCP reset: nothing is listening       */
    NS_FAIL_UNREACHABLE,    /**< no route, or the network is down      */
    NS_FAIL_TIMEOUT,        /**< the SYN went out; nothing came back   */
    NS_FAIL_NOT_NTRIP,      /**< answered, but not as a caster         */
    NS_FAIL_AUTH,           /**< 401 -- user name or password          */
    NS_FAIL_FORBIDDEN,      /**< 403 -- known user, not this mountpoint*/
    NS_FAIL_NO_MOUNTPOINT,  /**< 404, or a sourcetable was returned    */
    NS_FAIL_BUSY,           /**< 409 / 503 -- caster full or restarting*/
    NS_FAIL_DROPPED,        /**< it worked, then the peer closed       */
    NS_FAIL_STALLED,        /**< open and silent -- 3.5.0's stall check*/
} NsFailure;
```

What each one says, and what it asks the user to do. The wording is the
core's, in English, and is what the CLI, the GUI and the daemon print:

| Code | Sentence | What to check |
|---|---|---|
| `DNS` | `Cannot find "%s".` | the caster address — a typo, or no DNS on this network |
| `REFUSED` | `Nothing is listening on %s:%d.` | the **port** — the address resolved and the machine answered the door |
| `UNREACHABLE` | `No route to %s.` | the network, the VPN, flight mode |
| `TIMEOUT` | `%s:%d did not answer within %d s.` | the service may be down, or a firewall is dropping it |
| `NOT_NTRIP` | `%s:%d answered, but not as an NTRIP caster.` | the port is probably a web server or another service |
| `AUTH` | `The caster rejected the user name or password.` | credentials |
| `FORBIDDEN` | `The credentials are accepted, but not for mountpoint "%s".` | the account's permissions on this mountpoint |
| `NO_MOUNTPOINT` | `This caster has no mountpoint "%s".` | the name — offer **Browse mountpoints** |
| `BUSY` | `The caster is refusing new connections just now.` | try again; a full or restarting caster |
| `DROPPED` | `The caster closed the connection after %ld s.` | — |
| `STALLED` | `Connected, but nothing arrived for %d s.` | unchanged from 3.5.0 |

Two distinctions are the ones the directive actually asks for, and they
are worth stating plainly because they are what a user cannot work out
alone:

- **`DNS` versus `REFUSED`** is *wrong address* versus *wrong port*. The
  first never reached a machine; the second reached one and was turned
  away at that port.
- **`AUTH` versus `FORBIDDEN`** is *wrong credentials* versus *right
  credentials, wrong mountpoint*. Casters differ on which they send, so
  both must be mapped rather than assumed.

### 5.3 Where each is detected

| Code | Detected at | From |
|---|---|---|
| `DNS` | `ntrip_handler.c:111` | `getaddrinfo` non-zero |
| `REFUSED` / `UNREACHABLE` / `TIMEOUT` | `ntrip_handler.c:145` | `connect()` — `ECONNREFUSED`, `EHOSTUNREACH`/`ENETUNREACH`, `ETIMEDOUT`, and their `WSAE*` twins |
| `NOT_NTRIP` | handshake parse | no `ICY 200` and no `HTTP/1.x` status line |
| `AUTH` / `FORBIDDEN` / `NO_MOUNTPOINT` / `BUSY` | handshake | `NsHandshake::status` — already parsed, already stored |
| `NO_MOUNTPOINT` (second form) | first payload | `ENDSOURCETABLE` where a stream was expected (`ntrip_handler.c:239` already looks for it) |
| `DROPPED` / `STALLED` | pump loop | existing `NS_END_EOF` / `NS_END_STALLED` paths |

The `errno` → `NsFailure` mapping is written **once**, in `src/net`,
with the Windows and POSIX spellings side by side. No frontend maps a
platform error code; that is the whole reason the core exists.

### 5.4 How it travels

- `NsStatsSnapshot` gains `int failure` and `char failure_detail[128]`,
  serialised into the JSON object and **appended** to the CSV column list
  (appended, so existing readers of the daemon's CSV keep working).
- The session's `NS_EV_DISCONNECTED` event carries the same code beside
  the existing `NsEndReason`. The two are not merged: *how* a session
  ended and *why it could not run* are different questions, and the
  existing end reasons are a stable contract for the daemon.
- KPI 1's detail becomes failure-aware: where it says
  `No connection to the caster` it says the sentence from the table.
  The **verdict word does not change** — `FAILED` stays `FAILED`, so the
  CLI's exit codes and every consumer of the verdict vocabulary are
  untouched. What changes is the line beneath it.

### 5.5 How it looks

In the template, a failure is a row like any other:

```
┌─────────────────────────────────────────┐
│              FAILED                     │   verdict, unchanged
│   The caster rejected the user name     │   the sentence, in the
│   or password.                          │   verdict's own sub-line
├─────────────────────────────────────────┤
│  rfsee.net:2101                         │
│  RFSEE01                          ▶     │   the row that is wrong,
│  as rfsee (password set)                │   opening the dialog on it
└─────────────────────────────────────────┘
```

The failing row is the one you tap: a credentials failure opens the
connection dialog with the password field focused, a `NO_MOUNTPOINT`
failure opens **Browse mountpoints**. A message that names the fault and
does not offer the fix is half a message.

**Localisation.** The core carries the code and an English sentence; the
Android app maps `NsFailure` to its own `strings.xml` entry, which is
what makes a Dutch build possible later. CLI, GUI and daemon print the
core's sentence. This keeps the rule intact — no verdict or threshold in
a UI layer — while leaving translation where translation belongs.

---

## 6. Fitting what has not been built yet

The template has to hold the phase-2 roadmap without being re-drawn for
it. Every item lands as one of exactly three things:

| Kind | Where it goes | Marker | Roadmap items that are this |
|---|---|---|---|
| **A hub row that folds** | a `Panel` whose `Detail` is prose | `▼` | tier-2 metrics summary, statistics counters |
| **A hub row that leads to a screen** | a `Panel` with a `destination()` | `▶` | VRS (five assertions + gate), hand-over (ARP dots in local metres), tier-2 detail (six metrics) |
| **A fourth analysis tab** | a page in the pager | — | *nothing on the roadmap qualifies* |

The third row is the useful one. A view earns a **tab** only if it is a
continuously updating plot of the live stream — which is what the three
existing tabs are. Satellite **tracks** are not a fourth tab: they are
drawn inside the sky canvas, which Dia1 already shows. VRS and hand-over
are not tabs either, despite being geographic: they are answers about a
session, reached from their hub row, and they belong to the drill-down.

So the pager stays three wide, and the hub grows. That is the property
that makes the template survivable: **the list is the layout**, and the
frame around the list never changes.

Two roadmap items touch the shell rather than the hub:

- **Statistics export** — a row in the `⋮` menu beside *Load / save
  configuration*, using the `FileProvider` that 3.6.0 added for the plot.
- **TLS** — no screen at all. It shows up as a lock glyph in the
  connection row's first line and nowhere else.

---

## 7. What this specification does not settle

Listed here so the rollout plan can carry them as decisions rather than
assumptions:

1. **The orbit-source badge's link.** The badge goes; its content moves
   into the summary line. The wiki link it carried needs a home — the
   `⋮` menu, or a tappable summary line.
2. **`READY` / `RUNNING` in the verdict card.** The template shows both,
   as today. If the failure sentence lives in the verdict's sub-line,
   that sub-line is doing three jobs (progress, sustain count, failure).
3. **Fold state persistence.** Whether an expanded KPI row stays
   expanded across a rotation and across a run.
4. **The CSV column.** Appending `failure` to the daemon's CSV is
   additive but it is still a format change with an outside consumer.

---

*Sources: `work-items/guiReview/guiTemplate/Dia1…8.PNG` (the template),
`work-items/guiReview/Screenshot_20260821_*.jpg` (3.6.0 as built),
`guiV2rollout.md` (what v2 decided and why), and the code at 3.6.0 for
every claim in §5.1.*
