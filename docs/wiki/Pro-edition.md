# NTRIP Analyser Pro

**Start with the free pages.** Everything there applies here unchanged:
[Getting started](Getting-started), [the eight checks](The-eight-checks),
[the analysis views](The-analysis-views) and
[troubleshooting](Troubleshooting) describe this edition too.

The two editions **measure identically**. The checks, their thresholds
and their verdicts come from one engine, so a station that passes in one
passes in the other. Nothing is measured differently and nothing is
withheld to make the free edition look worse.

These pages cover only what is different.

## Where to get it

Pro is in **closed testing** on Google Play. A personal developer
account must run a closed test with **twelve testers who stay opted in
for fourteen continuous days** before it may request production
access, and that rule counts per app — the free edition serving it
does not exempt this one.

**Testers are not charged.** Pro is a paid app, but testers are added
as licence testers and install it without paying.

[Join the test](https://play.google.com/apps/testing/nl.pe1mew.ntripanalyser.pro),
or [open an issue](https://github.com/pe1mew/NTRIP-Analyser/issues)
asking to be added.

What helps most during the test is the same thing that helps most
after it: a station where the verdict looks wrong. The thresholds
behind every verdict are judgements rather than facts, and a station
you know well is the best evidence against one.

| | |
|---|---|
| **[Watch mode](Watch-mode)** | Keep measuring for hours: availability, streaks, degradations |
| **[Stability report](Watch-mode#the-stability-report)** | Six metrics over stream time with their own verdicts — has this station *been* fit |
| **[Network-RTK check](Network-RTK-check)** | Five assertions and a gate test for VRS and network services, where the eight checks alone mislead |
| **[Reference position](Network-RTK-check#watching-the-reference-position-pro)** | Live rover-to-ARP distance, and the hand-over history that shows a network switching stations under you |
| **[Saved connections](Saved-connections)** | Up to sixteen casters, and the shared configuration file |
| **[Reporting where you are](Live-position)** | Sending this phone's position to a network mountpoint |
| **[Orbits and the ephemeris stream](Orbits-and-the-ephemeris-stream)** | A complete sky view on a station that broadcasts no orbits |

## The smaller differences

- **Export statistics.** The ⋮ menu writes the current snapshot as a
  file: JSON carrying everything the app knows, or CSV in exactly the
  columns the desktop GUI exports and the monitoring daemon logs — so
  a phone's export and a server's sample can sit in one spreadsheet.
  Available during a run (a timestamped snapshot) and after it (the
  settled result).

- **Pick a mountpoint from the sourcetable.** Tap an entry in *Browse
  mountpoints…* and it becomes the connection — with its published
  position and its NMEA setting, so a network mountpoint is configured
  correctly without typing anything.
- **Per-message-type statistics.** Expanding check 4 lists every type
  the station sends, how many epochs arrived and the interval between
  them.
- **The full reference-position detail.** Expanding check 3 shows the
  station ID, the ITRF realisation year, which constellations the
  station claims to serve, whether it calls itself a reference station,
  and the antenna height where the message carries one.
