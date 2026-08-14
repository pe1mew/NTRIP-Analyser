# Privacy policy — NTRIP Analyser for Android

**Applies to:** *NTRIP Analyser* (`nl.pe1mew.ntripanalyser.free`) and
*NTRIP Analyser Pro* (`nl.pe1mew.ntripanalyser.pro`).
**Last updated:** 13 August 2026.

NTRIP Analyser measures GNSS correction streams. It has no account, no
advertising, and no server belonging to its author. Most of this policy
is therefore a list of things that do not happen.

## The short version

**The app collects nothing.** It contains no analytics library, sends
nothing to its developer, and has no background activity of any kind.
What the app transmits, it transmits to the NTRIP caster *you* configure,
because that is what connecting to a caster means.

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

## What the developer receives

Nothing automatic. Google Play Console reports aggregate statistics
about the listing — install counts, and crash and engagement figures
that Google gathers and anonymises under its own policies. They contain
no information about which stations you tested or what the results were.

If you send a report, an export, or an e-mail, the developer receives
exactly what you chose to send, and nothing else.

## Children

The app is a professional measurement tool and is not directed at
children.

## Your rights

Because no personal data is collected, there is nothing held about you
to access, correct or delete. Everything the app knows is on your own
device: clearing the app's data, or uninstalling it, removes all of it,
including the stored credentials.

## Changes

Material changes to this policy will be published here before the app
version that makes them is released. This document lives in the
project's public repository, so its full history is visible there.

## Contact

Remko Welling (PE1MEW) — see the contact address on the app's Play
listing, or open an issue at
<https://github.com/pe1mew/NTRIP-Analyser/issues>.
