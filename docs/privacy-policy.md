# Privacy policy — NTRIP-Analyser

**Applies to every program in the suite:** the Android apps *NTRIP
Analyser* (`nl.pe1mew.ntripanalyser.free`) and *NTRIP Analyser Pro*
(`nl.pe1mew.ntripanalyser.pro`), and the desktop and server programs —
the command-line tool, the Windows GUI and the monitoring service.
**Last updated:** 16 August 2026.

NTRIP-Analyser measures GNSS correction streams. It has no account, no
advertising, and no server belonging to its author. Most of this policy
is therefore a list of things that do not happen.

**One policy, not four.** The same answer holds for every program, and
two documents saying the same thing in different words would eventually
say different things. Where a program genuinely differs — and the desktop
ones do, on credentials and on files they write — this says so.

## The short version

**None of these programs collects anything.** There is no analytics
library in any of them, nothing is sent to the developer, and none has
background activity of any kind. What they transmit, they transmit to the
NTRIP caster *you* configure, because that is what connecting to a caster
means.

---

# The Android apps

## What the app handles, and where it goes

| What | Where it is kept | What leaves the device |
|---|---|---|
| Caster address, port, mountpoint | On the device | Sent to that caster when you run a check |
| Caster username and password | On the device, encrypted with a key held in the Android Keystore | Sent to that caster, as the NTRIP protocol specifies — see *Credentials in transit* |
| The position you type or pick | On the device | Sent to the caster **only** if the mountpoint asks for a position (the sourcetable's NMEA flag) |
| This phone's position | Not stored | **Pro only, and only after you agree:** sent to the caster about every ten seconds during a run. Free never sends it |
| Satellite positions from this phone's receiver | In memory, for the duration of the screen | Never |
| A RINEX navigation file you import | Copied into the app's private storage | Never |
| Measurement results | In memory during a run | Only when *you* export or share them |

## Permissions, and why each one exists

- **Internet** — to connect to the caster you configure. There is no
  other network destination.
- **Location (precise)** — asked when you first open the Analysis view.
  In **both** editions it is used on the device to place satellites in
  the sky view. In **pro** it is *also* the source of the position sent
  to the caster, but only after the separate, explicit agreement
  described below, which you can withdraw at any time.
- **Notifications** — a measurement run needs at least sixty continuous
  seconds, so it runs as a foreground service, and Android requires it
  to show a notification.
- **Foreground service (data sync)** — the same run. It is not a
  location-type service: the app does **not** collect location in the
  background. With the app off screen, it simply stops learning where
  you are and keeps sending the position you configured.

## The position sent to a caster

Network-RTK services need to know roughly where the receiver is, or they
cannot compute corrections for it. Whether a position is sent at all
follows the mountpoint's own sourcetable entry — a single base station
gets none.

- **Free** sends the fixed position you typed, picked on a map, or took
  from the station's own sourcetable entry. It never sends the phone's
  position.
- **Pro** can send this phone's position instead, so a network service
  answers for where you actually are. It asks once, naming the caster,
  before the first run that would do this; turning the setting off stops
  it, and the fixed position is used instead.

The caster is a third party's infrastructure — usually your correction
provider — and its own privacy policy governs what it records. This app
has no relationship with it beyond the connection you configure.

## Credentials in transit

NTRIP as specified sends the username and password with HTTP Basic
authentication over a plain TCP connection. This app follows the
protocol, so **anyone able to observe the network path can read
them**. That is a property of NTRIP rather than of this app, and it is
stated in the app at the moment you type a password.

TLS support is planned, and when it arrives it arrives in **both**
editions on the same day: a paid edition may withhold convenience, never
protection.

## Picking a position on a map

The app contains no map. *Pick on map* hands the coordinates you are
already editing to whatever map application you have installed, or to
OpenStreetMap in your browser; you copy a position there and paste it
back. That handover passes those coordinates to the app or website you
chose, and it is governed by their terms, not this policy. No map tiles
are requested by this app.

---

# The desktop and server programs

The command-line tool, the Windows GUI and the monitoring service run on
your own machine, under your own account. They have no store, no
permissions model and no update mechanism, and they behave differently
from the app in three ways worth knowing.

**Credentials are stored in the clear.** The configuration is a JSON file
you own, and the caster password sits in it as plain text — unlike the
app, which encrypts credentials with a key held in the Android Keystore.
Nothing obscures it, and nothing pretends to. Keep such a file outside a
repository, readable only by you (`chmod 600`), and prefer a caster that
does not reuse a password you care about. See
[`docs/jsonConfigs.md`](jsonConfigs.md).

**They write files that contain station data.** All three can produce
output that describes a real installation, and its retention is entirely
yours:

| File | Written by | What is in it |
|---|---|---|
| `.rtcm3` capture | CLI `--capture`, GUI *Start RTCM Capture* | The stream verbatim — observations, and the station's broadcast position |
| Sky plot PNG, CSV and JSON exports | CLI, GUI | Measurements, the mountpoint name, and often the station's coordinates |
| `<mountpoint>.json` snapshots | the service | Current statistics, including the reference position, rewritten each interval |

None of these leave the machine unless you send them. A capture is the
raw stream, so treat it as you would treat the station: publishing one
publishes where that antenna is.

**Nothing is transmitted anywhere but the caster you configure.** There
is no telemetry, no version check and no crash reporting. A machine with
no route to your caster does nothing at all on the network.

The same statements about NTRIP credentials in transit apply: HTTP Basic
over plain TCP, readable by anything on the path, with TLS planned and
arriving in every program together.

---

# What the developer receives

Nothing automatic, from any program.

For the **apps**, Google Play Console reports aggregate statistics about
the listing — install counts, and crash and engagement figures that
Google gathers and anonymises under its own policies. They contain no
information about which stations you tested or what the results were.

For the **desktop and server programs**, GitHub reports how many times a
release asset has been downloaded. That is a number per file, with
nothing attached to it — no addresses, no identities, and no knowledge
that a given download was ever run.

If you send a report, an export, or an e-mail, the developer receives
exactly what you chose to send, and nothing else.

## Children

These are professional measurement tools and are not directed at
children.

## Your rights

Because no personal data is collected by any of these programs, there is
nothing held about you to access, correct or delete. Everything they know
is on your own device: clearing the app's data or uninstalling it removes
all of it on Android, including the stored credentials; on the desktop
and the server, deleting the configuration file, the captures and the
snapshot directory does the same.

## Changes

Material changes to this policy are published here before the release
that makes them. This document lives in the project's public repository,
so its full history is visible there.

## Contact

Remko Welling (PE1MEW) — see the contact address on the app's Play
listing, or open an issue at
<https://github.com/pe1mew/NTRIP-Analyser/issues>.
