# Orbits and the ephemeris stream

The sky view needs to know where each satellite is. An observation
stream says *which* satellites are being tracked, not where they are, so
the orbits have to come from somewhere.

There are three sources, and the header of the sky view always names the
one that did the work.

## 1. The station's own stream — the best case

Many stations broadcast ephemerides beside their observations
(1019, 1020, 1042, 1044, 1045, 1046 and so on). Where they do, nothing
else is needed and nothing else is fetched: the orbits are current and
cover exactly the satellites the station serves. The header reads **the
station's own stream**.

## 2. A navigation file you imported

**☰ → Import navigation file** takes a RINEX 3 file. It covers any
station, and it is what the free edition uses. The header reads
**navigation file**.

## 3. An ephemeris stream, borrowed and returned — pro only

Most casters publish a broadcast-ephemeris mountpoint (Kadaster's
`BCEP00KAD0`, for example). Set it in the connection settings under
*Ephemeris stream* and the app will use it **when it is needed**.

"When needed" is a real policy, not a figure of speech:

- **It is not opened at all** while the station's own stream is
  delivering orbits. Holding a second connection open to receive
  messages you are already getting is rude to the caster and pointless
  to you.
- It is opened when nothing has filled the cache for **twenty seconds**
  — coverage has stopped climbing and no orbits are arriving on the
  observation stream.
- It is opened even when a navigation file is loaded, if the station
  broadcasts none itself. A file is a fallback, not a substitute for a
  live source you configured.
- It is **closed** as soon as every tracked satellite can be placed, or
  after two minutes, whichever comes first — and never sooner than
  twenty seconds after opening, which is about two broadcast cycles.
- After closing it will not redial for fifteen minutes. An incomplete
  cache is not a reason to keep dialling a caster that is not
  delivering.

The header reads **ephemeris stream** while it is the source.

## Reading the orbit card

Below the analysis views, the card says how many satellites the stream
carries, how many can be placed, how many orbits are cached and how old
the newest is.

**Age matters far less than it looks for a sky plot**: a kilometre of
orbit error at 20 000 km is about 0.01°, so orbits hours old still draw
the sky correctly. They would not do for positioning, which this app
does not do.

## What a gap actually means

*"41 of 47 satellites shown"* is not a fault. A satellite that has just
risen waits its turn in the broadcast cycle, and one whose orbit nothing
covers is **counted but deliberately not drawn** — a marker at 0,0 would
sit on the horizon due north, and the plot cannot tell that from a fact.

If the number stays far below the satellite count for minutes, either
the station broadcasts no orbits and you have no other source
configured, or the ephemeris mountpoint you set is not delivering. The
header tells you which, by naming what it is using.
