# Troubleshooting

The app is built to tell you what went wrong on the screen where it went
wrong. This page covers the cases where the message alone may not be
enough.

---

## "Connected, but the caster has sent nothing"

The caster accepted the connection and then sent nothing at all.

1. **Does the mountpoint expect a position?** Network-RTK and
   "nearest base" services send nothing until the receiver reports where
   it is. Open the connection settings, tick **Send GGA**, and set a
   position — **From station** is the quickest. Its sourcetable entry
   shows whether it asks for one.
2. **Is the mountpoint actually fed?** A caster publishes a mountpoint
   whether or not its receiver is currently connected. Try another
   mountpoint on the same caster: if that one flows, the station is the
   problem, not you.

## "Data arrived for N s, then the stream stopped"

Not the same finding, and worth separating from the one above: this
station **did** deliver — check 2 will still show the frames — and then
the flow ended.

1. **Are you already connected to it?** Many casters allow one session
   per account, and starting a second check while the first is running
   evicts one of them. This is the commonest cause, and it is not the
   station's fault. Leave a gap between runs and try again.
2. **How long did it run?** The number is how long data flowed. Seconds
   points at the session being taken away; many minutes points at the
   receiver feeding the caster, or at the link to it.
3. **Try it again before reporting it.** One drop is an event; a station
   that does it every run is a finding. `--report` and the Stability
   window exist for exactly that question — they watch for hours and
   count the drops.

## The verdict says FAILED but the numbers look fine

Read which check failed. Check 8 fails when the station is **not sending
something its sourcetable advertises**, and that is a real finding even
when everything else is healthy: a rover configured from that
sourcetable will wait for a message that never comes.

## The mountpoint is rejected, or "no such mountpoint"

Mountpoint names are case-sensitive and exact. Use **Browse
mountpoints…** and compare character by character.

## The connection is refused, or asks for credentials

Some casters require a login even for streams they describe as free;
some require your e-mail address as the username. That is the caster's
convention, not the app's — check the operator's instructions.

## The sky view is empty, or shows far fewer satellites than the check counted

The sky view needs orbits, and an observation stream does not always
carry them. The header names the source it used. If it says
**navigation file** and you have not imported one, there is nothing to
place the satellites with: either the station broadcasts its own orbits
(nothing to do), or import a RINEX navigation file — see
[The analysis views](The-analysis-views).

Satellites without an orbit are counted but not drawn, deliberately.

## Signal quality and the elevation plot are empty

The stream carries no C/N0 the app reads. Check 6 will say
*"Not measured: C/N0 is read from MSM4, 5, 6 and 7"*. Streams using
MSM1, 2 or 3 carry no signal strength at all. Nothing is wrong with the
station.

## The check never finishes

A run needs sixty continuous seconds of everything passing before it
claims OK, so a station that drops briefly restarts that clock. If the
stream keeps stopping, check 1 will fail on its own within a few
seconds — a run that keeps going is a run that keeps nearly passing.

## It stops measuring when the screen goes off

It should not: a run holds a notification for its whole life so the
system leaves it alone. If your phone kills it anyway, check whether
battery optimisation is enabled for this app — some manufacturers are
aggressive about background work regardless of the notification.

## Typing a caster or mountpoint produces stray full stops

Fixed. If you are on an older build and see `APEL0. ` where you typed
`APEL0`, update — some keyboards insert punctuation when a field loses
focus, and the app now asks for a keyboard that does not.

---

## Reporting something

Issues go to the **[GitHub issue tracker](https://github.com/pe1mew/NTRIP-Analyser/issues)**,
which is public and searchable — check whether your question is already
answered there before opening a new one.

Please include the caster and mountpoint if you can share them, what the
eight rows said, and what you expected instead. A screenshot of the
verdict screen usually carries all of it.

There is no help desk behind this. See
[Privacy, and how support works](Privacy-and-support).
