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

| | |
|---|---|
| **[Watch mode](Watch-mode)** | Keep measuring for hours: availability, streaks, degradations |
| **[Network-RTK check](Network-RTK-check)** | Five assertions and a gate test for VRS and network services, where the eight checks alone mislead |
| **[Saved connections](Saved-connections)** | Up to sixteen casters, and the shared configuration file |
| **[Reporting where you are](Live-position)** | Sending this phone's position to a network mountpoint |
| **[Orbits and the ephemeris stream](Orbits-and-the-ephemeris-stream)** | A complete sky view on a station that broadcasts no orbits |

## The smaller differences

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
