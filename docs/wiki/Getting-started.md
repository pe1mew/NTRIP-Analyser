# Getting started

## 1. Tell it where the station is

Tap the connection tile on the main screen and fill in:

| Field | What goes in it |
|---|---|
| **Caster host** | The caster's address, for example `ntrip.kadaster.nl`. No `http://` |
| **Port** | Usually 2101 |
| **Mountpoint** | The stream's name, exactly as the caster publishes it. Case matters |
| **Username**, **Password** | Leave both empty for an open caster |

Not sure of the mountpoint name? Fill in the caster and port, save, then
tap **Browse mountpoints…**. The list is the caster's own sourcetable:
every stream it publishes, its format, its constellations, and whether
it expects a position from you. Free shows the list so you can read it;
type the name you want into the mountpoint field.

### Whose caster is it?

**The app is a client. It connects where you tell it to, with your
credentials, and that keeps the relationship — and the terms — between
you and the caster.**

So before you point it somewhere: have permission to be there. Public
reference networks publish open streams and are meant to be used; a
commercial network's mountpoints are for its subscribers; a station
belonging to someone else's project is theirs. Some casters limit how
many connections one account may hold, and a measurement tool that
reconnects is still a connection.

The data you receive belongs to whoever operates the station, under
their terms and not ours, and what you do with it is between you and
them.

None of this is unusual — it is the ordinary position of any NTRIP
client — but a tool that makes connecting easy should say it once,
plainly.

## 2. Run the check

Tap **Run the check**. It takes about ninety seconds, and the eight rows
fill in as the evidence arrives. A verdict is only claimed once it has
held for a full minute, so a station that flickers cannot pass by being
healthy at the right moment.

While it runs you can swipe left into the analysis views and watch, then
swipe right to come back.

## 3. Read the answer

The badge at the top is the verdict. Below it, each of the
**[eight checks](The-eight-checks)** carries its own verdict, the number
behind it, and a sentence saying what it means. Tap a row to expand it.

A **CAUTION** is worth reading rather than dismissing: it is the app
saying *this is not a clean pass, and here is precisely which part*.

## Mountpoints that want your position

Some streams — network-RTK or "nearest base" services — send nothing at
all until the receiver reports where it is. Their sourcetable entry says
so, and the app follows it: tick **Send GGA**, and give it a position by
one of three routes.

- **From station** fills in the mountpoint's own published coordinates,
  which is the right choice for testing whether a station serves the
  area it claims.
- **Pick on map** opens your map app; copy the coordinates there and
  press **Paste**.
- Or type latitude and longitude yourself.

The free edition always sends the position you set, never the phone's.
A network service will answer for the point you give it, so it must be
inside that network's coverage — a position outside it can make a
healthy service look like a broken station.

## A navigation file, if you want a full sky

The sky view needs to know where satellites are, and an observation
stream does not carry that. Many stations broadcast their own orbits and
then nothing is needed. For a station that does not, use **☰ → Import
navigation file** and pick a RINEX navigation file (`.rnx`, or a
`.gz` of one) that you have obtained yourself.

The app never downloads one for you: the file comes from a data
provider whose terms are between you and them.

**It has to be today's.** Orbits more than four hours old are not used —
by then they no longer say where the satellite is — and a stale file
still loads and still reports thousands of records, so the only sign is
the badge in the corner of the Analysis screen turning red.
[Satellite orbits](Orbits-and-the-ephemeris-stream) explains the badge
and names a place to get a current file.
