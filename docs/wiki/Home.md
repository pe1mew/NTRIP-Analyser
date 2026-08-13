# NTRIP Analyser

Point it at an NTRIP caster and it answers one question about a GNSS
base station: **is this station fit to serve RTK, and if not, why?**

It does not steer a rover and it does not compute a position. It
measures what the station actually delivers — message types and their
rates, satellites and their signal strength, the reference position it
broadcasts, and how steadily it holds all of that — and states a verdict
you can act on, or hand to whoever runs the station.

A run takes about ninety seconds and ends in one of three words:

| | |
|---|---|
| **STATION OK** | Every check passed, and kept passing for a full minute |
| **CAUTION** | Something is off, or could not be judged. The station may still be usable — read which check and why |
| **FAILED** | At least one check failed outright |

## Where to start

- **[Getting started](Getting-started)** — your first run, in about five minutes
- **[The eight checks](The-eight-checks)** — what each one means, and what to do when it is not green
- **[The analysis views](The-analysis-views)** — sky, signal quality, and the antenna curve
- **[Troubleshooting](Troubleshooting)** — the messages you are most likely to meet
- **[What the paid edition adds](What-the-paid-edition-adds)**
- **[Privacy, and how support works](Privacy-and-support)**

## What you need

A caster address, a mountpoint name, and credentials if that caster
requires them. Many public casters do not: several national networks
publish open reference stations, and the app connects to those with the
username and password left empty.

You do **not** need a survey receiver. The measurement is of the
station's stream, and the phone's own GNSS is used only to draw
satellites in the sky view.
