# Reporting where you are

A network-RTK service computes corrections **for the position the rover
reports**. What that position is decides what you are actually
measuring, and there are two different questions:

| The question | The position to send |
|---|---|
| *Does this station serve the area it claims?* | A **fixed** position — repeatable, comparable between runs, independent of where you are standing |
| *Am I served properly **here**?* | **This phone's** position |

The free edition answers the first. Pro can answer either.

## Turning it on

In the connection settings, with **Send GGA** ticked, tick **Report this
phone's position**.

It asks once, before the first run that would transmit anything, and the
question names the caster it will go to. Agreeing is remembered;
unticking the switch stops it, and the position you typed is sent
instead.

## What is actually sent

- A GGA sentence, about **every ten seconds**, while a run is going.
- To **that caster and nowhere else**. This app has no server and sends
  nothing to its author.
- Only when the mountpoint asks for a position at all. A single base
  station gets none, whatever this setting says.

## The fallback, and why it matters

**Without a fix, the position you configured is sent instead** — never a
zero. A GGA of 0,0 is a valid sentence that puts the rover in the
Atlantic, and a VRS will happily answer it.

So indoors, or in the first seconds before the receiver has a fix, a run
still has something honest to send.

**While the app is off screen, Android stops delivering location to it**
and the last known position stands until you come back. That is
deliberate: the alternative is a service that tracks you continuously in
the background, and this app will not do that to answer a coverage
question. If you are walking a site, keep the screen on.

The same rule reaches the [reference-position
view](Network-RTK-check#watching-the-reference-position-pro): its
station side (hand-overs, the dots) records with the screen off,
because that comes from the stream — but its rover side, the distance
to you, freezes with the last fix, for exactly the reason above.

## Which to use when

- **Accepting a network service** — fixed, taken from the station's own
  sourcetable entry with **From station**. Every run is then comparable,
  and the position is certainly inside the network.
- **Investigating a complaint at a site** — live. The service answers
  for where the complaint is, which is the whole point.
- **Testing coverage at the edge of a network** — fixed, at the point in
  question, picked on a map. You do not have to travel there.

A fixed position **outside** the network's coverage can make a healthy
service look like a broken station: the VRS returns a distant reference
position or nothing, and check 3 or check 1 reports it. That is why
*From station* exists.

## Privacy

This is the one place the app transmits something about you, which is
why it asks rather than assuming. It is also why the two editions
declare different things on the Play data-safety form: free never sends
a position it did not get from you.

The full policy is at
<https://pe1mew.github.io/NTRIP-Analyser/privacy-policy>.
