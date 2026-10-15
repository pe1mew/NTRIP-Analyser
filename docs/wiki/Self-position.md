# Self-position — Pro

**Where the station's own observations put it**, against the position
it broadcasts. Every reference station tells its users where its
antenna is (RTCM 1005/1006); this view takes the station's own GPS
observations and broadcast orbits, solves for where they place the
antenna, every epoch, and plots the result around that broadcast
position.

Open it from **Analysis**: it is the tab after *C/N0 vs elevation*.

## What it needs

- **Two GPS frequencies.** The ionosphere is removed by combining two
  carriers; a single-frequency station is **NOT COMPUTABLE**, and
  waiting will not change that.
- **Orbits.** From the station's own stream, or — for a station that
  broadcasts none — from the [ephemeris stream](Orbits-and-the-ephemeris-stream).
  Without them the view says it is waiting for orbits.
- **GPS only, deliberately.** Each constellation keeps its own time, and
  mixing them under one receiver clock put a real station 1.8 km from
  itself.

## The figures

| Figure | What it says |
|---|---|
| **Epochs solved** | How many epochs produced a position, of how many in the window |
| **Mean offset E/N/U** | Where the solutions centre, against the broadcast position |
| **Scatter about the mean** | How widely they spread — **an upper bound**, see below |
| **Worst code residual** | How well the worst epoch's measurements fitted its solution |
| **Median apparent motion** | The solved velocity. A base standing still reads tens of mm/s |

**No verdict.** No threshold for a station's own scatter has been
established from evidence, so these are figures, not a judgement — and
the view says so.

The plot is east and north around the broadcast position, which stays
at the centre: a plot that centred itself on the solutions would hide
the offset. The latest epoch is a line under it, as a diagnostic.

## How to read it — and what it is not

**This is not a survey, and the offset is not the station's error.**
A position from broadcast orbits and clocks is good to a few metres.
Both the offset and the scatter include this solution's own errors:

- The app forms its ionosphere-free combination from the signals a
  station sends — usually **L1 C/A with L5** — against satellite clocks
  defined for **L1/L2 P(Y)**, and it does not correct the
  satellite-by-satellite **code biases** between them. Each satellite
  then carries an offset of its own, up to a few metres, and the
  solution moves whenever a satellite rises or sets.
- Measured on one station over three hours, correcting those biases
  from a published product put the mean within half a metre of the
  broadcast position and cut the jumps roughly in half. The phone has
  no way to fetch that product, so the app does not apply it.

So read the **scatter as an upper bound** on how much the station's own
position varies — not as a measurement of it. A small scatter is good
news; a large one is not, by itself, a fault in the station.

The **apparent motion** is unaffected: it comes from the rate of change
of the carrier phase, which code biases do not touch. That is the
figure that would show an antenna that moved.

**REVERSED** appears when the station's receiver encodes its range
rates with the wrong sign — every satellite the exact negative of what
its orbit predicts. The app works out which convention the station uses
and the motion is right either way, but a rover that trusts those rates
is being misled by them.

## In the free edition

Free does the same work during a run — nothing is measured differently
between the editions — but has no view that draws it.
