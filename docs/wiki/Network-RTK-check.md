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

The verdicts and their reasoning live in the shared engine
(`src/core/vrs_check.c`); the CLI's `--check-vrs` prints the same five
rows with the same words.
