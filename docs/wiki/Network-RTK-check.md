# The network-RTK check

*Pro.* The eight checks grade a **station**. Point them at a network
service — a VRS, MAC or nearest-base mountpoint — and they can mislead:
a network computes a virtual station near *you*, so its reference
position moves, and a moving position is exactly what check 3 exists to
distrust. On a network, that movement is correct operation.

The network-RTK check asks the questions that fit a network. Five
assertions and a gate test, judged by the same engine the desktop's
`--check-vrs` uses, so a service reads the same from a phone in the
field and a terminal at a desk.

Tap **Network-RTK check** on the station screen. It runs the eight
checks and the assertions together, and **ends by itself** — about
three minutes on a healthy service.

## The five assertions

| | | What it would mean if it failed |
|---|---|---|
| **A1** | GGA accepted by caster | The service dropped the stream on being told where you are — wrong mountpoint type, or a caster that rejects your account's position |
| **A2** | RTCM after GGA | Corrections should start within seconds of the first position report. A network that stays silent is not serving you |
| **A3** | ARP near rover position | The reference position should be computed near the position you sent. One 300 km away is answering somebody else's question |
| **A4** | Keep-alive holds | With a position reported every ten seconds, the stream must run continuously for the whole window |
| **A5** | GGA gating | Not a pass/fail — see below |

A3 has a middle reading: a reference position beyond the ceiling but
under twice it says **nearest-station service** — the network is
handing you its closest physical base rather than computing a virtual
one, which is a kind of service, not a fault.

## The gate test, and what "gated" means

A real network service streams *because* you report a position, and
stops when you stop. That is the one behaviour that separates it from a
single base that merely ignores what you send — and no amount of
watching the stream flow can tell them apart. Only stopping can.

So once the eight checks have held their window and A4 has passed, the
check **stops sending the position** and watches:

- **The stream drops** → *GGA-gated (network service)*. A live network,
  behaving exactly as advertised.
- **The stream keeps flowing** past the deadline → *not gated (fixed
  base?)*. The service ignored your position throughout.

**"Not gated" is not a failure.** A fixed base ignoring GGA is behaving
correctly for what it is; the check's job was to find out what it is,
and it did. The row shows amber as a *classification*, never red. What
it tells you is that this mountpoint gives everyone the same
corrections — your distance to the station governs your accuracy, and
reporting your position buys you nothing.

## Reading it with the eight checks

Check 3 and A3 look at the same message and ask different questions:
check 3 asks *is the reference position stable and sane*, A3 asks *is
it near me*. On a fixed base both pass. On a live VRS, A3 passes while
the position tracks you — which is why the eight checks alone are the
wrong instrument here.

## Practicalities

- The check needs a position to report: the one you set in the
  connection settings, or, with [live reporting](Live-position) agreed,
  the phone's own.
- It is a verdict on **one connection**: unlike a watch, it does not
  reconnect after a drop — a drop is evidence, and A1, A4 or the gate
  will say which kind.
- The stream stopping at the end of a gated run is the test concluding,
  not a fault. The service dropped you because you stopped reporting,
  which is what it promised to do.
- Like every check, it is a client on somebody's caster — run it
  against services you are entitled to use.

## Watching the reference position (Pro)

The check above answers *what kind of service is this* in three
minutes. The **Reference position** card answers *what is it doing to
you over time*: how far the service's reference position is from where
you stand, in the same green/amber/red bands assertion A3 judges by,
and how often it has **moved**.

Tap the card for the full picture, laid out like the analysis views:
a plot with the rover at the centre and the reference position at its
true bearing and distance, the history as dots — **a hand-over reads
as a jump in the dots** — and the last five minutes of distance as a
chart, where a drift and a jump look nothing alike.

What movement *means* depends on what the service is, and the app
words it accordingly:

- On a **network service**, the reference position follows you and
  hands you between stations: *"a network switching stations under
  you, which is its job."* Expected, and now visible — a switch
  mid-survey is worth knowing about even when it is correct.
- On a **fixed base**, the reference position must not move at all:
  *"a fixed base should not move; corrections are unreliable."* A
  base whose broadcast position wanders is describing itself wrongly,
  and everything computed from it inherits the wander.

A "move" is a new position more than **10 metres** from the last one
recorded — far above re-encoding noise, far below any real hand-over —
and the same rule, the same 10 metres, is applied by the desktop and
counted into every report the core writes (`arp_moves` in the JSON and
CSV). Up to **32** positions are kept, as on the desktop.

The rover end of the distance is **this phone, whenever it has a
fix** — the reading says so — and the set position only when it does
not. That position is used for the display alone and never leaves the
device; what the app *sends* to the caster is a separate matter, still
behind [its own agreement](Live-position). The distinction exists
because of a real reading: with a station's own coordinates set as the
position (which tap-to-use fills in), the card once measured the
sourcetable against the broadcast ARP — 325 m — while its user stood
23 km away. With the phone's fix in use the distance line breathes a
little; that is your own receiver's wander, not the station's.

### Taking it on the road

This view works during any run, however started — including a
[watch](Watch-mode) begun from the analysis screen and left going for
a drive. Two sides of it behave differently on the move, and knowing
which is which is knowing what you measured:

- **The station side keeps itself, screen on or off.** The reference
  position, its history dots, the hand-over count and the moved/stable
  sentence all come from the *stream*, decoded by the same service
  that keeps a watch alive in a pocket. A network that switches
  stations under you at two in the morning is recorded either way.
- **The rover side follows you only while the app is on screen.**
  Android stops delivering location to an app it cannot see, and this
  app deliberately runs no background location service
  ([why](Live-position)). Screen off, the last fix stands: the
  distance stops tracking you — and so does the live position being
  sent, which is the same rule.

So for a drive that is *about* hand-overs: **screen on, mounted, and
[live reporting](Live-position) agreed.** With those three, the
reference position chases you and each hand-over lands as a jump in
the dots and a step in the distance chart.

Without live reporting, a drive shows something different — and the
view stays honest about it: the caster only ever sees your fixed
configured position, so a network has no reason to hand you over. The
reference position sits still while the distance *"of this phone"*
grows as you drive away from it. That is a correct picture of what the
service was told, not a fault in the view.

The history keeps the most recent **32** positions; the count keeps
counting past that. On Android 15 and later the six-hour ceiling on a
watch applies as ever ([Watch mode](Watch-mode)).

---

The verdicts and their reasoning live in the shared engine
(`src/core/vrs_check.c`); the CLI's `--check-vrs` prints the same five
rows with the same words.
