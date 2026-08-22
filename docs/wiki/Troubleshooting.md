# Troubleshooting

The app is built to tell you what went wrong on the screen where it went
wrong. This page covers the cases where the message alone may not be
enough.

---

## The check will not start: what the message means

Since 3.7.0 a stream that will not open says which of the things you
typed is the one at fault, and the row it names is the one to tap. The
sentence appears under the verdict; tapping the connection card opens the
settings with the cursor already in the field the fault points at.

| What the app says | What it means | What to change |
|---|---|---|
| Cannot find “*host*”. Check the caster address. | The name does not resolve. Nothing was reached. | The **caster** field — or the phone's network, if nothing resolves |
| Nothing is listening on *host*:*port*. Check the port. | The machine answered the door and refused at that port. The address is right | The **port**. 2101 is the usual one |
| No route to *host*. Check the network. | The phone could not get onto the network at all | Wi-Fi, mobile data, a VPN |
| *host*:*port* did not answer. It may be down, or a firewall may be dropping it. | The request went out and nothing came back | Whether the caster is up; a firewall between you and it |
| *host*:*port* answered, but not as an NTRIP caster. | Something is on that port, and it is not a caster — usually a web server | The **port**, and whether that host serves NTRIP at all |
| The caster rejected the user name or password. | The caster read your credentials and said no | **User name** and **password** |
| Your account is accepted, but not for mountpoint “*name*”. | The credentials are right; the account is not permitted this stream | Ask the operator, or pick another mountpoint |
| This caster has no mountpoint “*name*”. | The caster does not carry that name | The **mountpoint** — use **Browse mountpoints…** and compare exactly |
| The caster is refusing new connections just now. | Full, or restarting | Nothing. Try again shortly |
| The caster refused the request. | It said no in a way the app has no specific words for | The caster's own message is in the log |
| The caster closed the connection. | It worked, then the caster hung up | Often the caster's end; see the section below on streams that stop |
| Connected, but nothing is arriving. | The socket is open and silent | See *"Data arrived for N s, then the stream stopped"* below |

Two distinctions are worth knowing, because no app can guess them for
you:

* **“Cannot find” versus “nothing is listening”** is *wrong address*
  versus *wrong port*. The first never reached a machine; the second
  reached one and was turned away at that door.
* **“Rejected the user name or password” versus “not for mountpoint”**
  is *wrong credentials* versus *right credentials, wrong stream*.
  Casters differ in which they send, so the app reports what it was
  told rather than guessing.

The CLI and the daemon say the same sentences. In the CLI they follow
the `exit=` line; in the daemon's journal the failure is named beside
the reason: `session ended (reason 3, auth)`.

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
mountpoints…** and compare character by character. The app names the
mountpoint it asked for in the message, so compare that against the
list rather than against what you meant to type.

## The connection is refused, or asks for credentials

Some casters require a login even for streams they describe as free;
some require your e-mail address as the username. That is the caster's
convention, not the app's — check the operator's instructions.

If the app says the caster **rejected the user name or password**, the
caster read your credentials and refused them; if it says your account
is **not permitted** for that mountpoint, they were accepted and the
stream was not. The first is worth retyping, the second is worth an
e-mail to the operator.

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
