# Getting started

## 1. Tell it where the station is

Tap the connection tile on the main screen and fill in:

| Field | What goes in it |
|---|---|
| **Caster host** | The caster's address, for example `ntrip.kadaster.nl`. No `http://` |
| **Port** | Usually 2101 for plain connections; casters that offer TLS usually serve it on 443 |
| **Use TLS** | Encrypt the connection. An explicit choice — the app suggests it when the port is 443, but never assumes |
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

### Encrypted connections

NTRIP sends your username and password base64-encoded, which is an
encoding, not encryption: on a plain connection anything on the
network path can read them. If the caster offers TLS — by convention
on port 443, where plain NTRIP uses 2101 — tick **Use TLS** and the
whole connection is encrypted, credentials and stream alike.

The certificate is always verified: against the same root authorities
a browser trusts, and the certificate must name the caster you asked
for. There is no "connect anyway" button, deliberately — a
measurement tool that shrugs at a wrong certificate would happily
measure an attacker's station. A refused connection tells you which
side to distrust: *"did not complete a TLS handshake — check the port
and the TLS setting"* points at the settings; a certificate sentence
(expired, wrong name, untrusted) points at the caster.

The ephemeris stream in the paid edition has a TLS switch of its own,
because it may be a different caster.

## 2. Run the check

Tap **Run the check**. It takes about ninety seconds, and the eight rows
fill in as the evidence arrives. A verdict is only claimed once it has
held for a full minute, so a station that flickers cannot pass by being
healthy at the right moment.

While it runs you can tap **Analysis** to watch the views, and **Back**
to return to the verdict.

## 3. Read the answer

The badge at the top is the verdict. When a run cannot start at all it
says why there instead of counting seconds — *the caster rejected the
user name or password* — and the row that carries the field at fault is
the one to tap; [Troubleshooting](Troubleshooting) lists every message
and what to change.

Below it, each of the **[eight checks](The-eight-checks)** carries its
own verdict, the number behind it, and a sentence saying what it means.
A row with a **▾** has more to show: tap it to fold it open.

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
then nothing is needed. For a station that does not, use **⋮ → Import
navigation file** and pick a RINEX navigation file (`.rnx`, or a
`.gz` of one) that you have obtained yourself.

The app never downloads one for you: the file comes from a data
provider whose terms are between you and them.

**It has to be today's.** Orbits more than four hours old are not used —
by then they no longer say where the satellite is — and a stale file
still loads and still reports thousands of records, so the only sign is
the sky view's summary line turning red.
[Satellite orbits](Orbits-and-the-ephemeris-stream) explains what that
line says and names a place to get a current file.
