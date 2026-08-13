# The analysis views

Swipe left from the main screen, or tap **Analysis**. Three views, and
you can swipe between them; swipe right from the first to come back.

In the free edition these show **what the station check captured**, held
after the run ends.

---

## Sky view

Where the satellites are, by constellation. The header names the source
of the positions, which matters more than it looks:

| It says | Where the positions came from |
|---|---|
| **the station's own stream** | The station broadcasts its orbits alongside its observations. The best case: current, and exactly the satellites it serves |
| **navigation file** | The RINEX file you imported |
| **phone GNSS** | Your handset's own receiver, for satellites nothing else could place |

**A satellite with no known orbit is not drawn.** It is counted, and the
line under the plot says how many were left out. Drawing it at 0,0 would
put it on the horizon due north, and the plot cannot tell that from a
fact.

The count reads, for example, *"41 of 41 satellites shown"* — the second
number is what the stream carries, the first what could be placed.

## Signal quality

One bar per satellite, coloured by constellation, with the mean and
range in the header.

The mean is **averaged in power, not in decibels**. Averaging dB
overstates by roughly 2 dB, which is the difference between a station
that passes check 6 and one that does not.

## C/N0 versus elevation

The antenna's own signature. A healthy installation climbs smoothly from
roughly 35 dB-Hz at the horizon to about 50 at zenith.

- A **flat** curve suggests the antenna, its siting, or the cable.
- A **dent** at one bearing is usually an obstruction.
- Marks are shaded by how often that combination occurred, so a cell hit
  a thousand times reads darker than one hit once.

The plot covers **one run**: starting a new check clears it, so two
stations are never drawn on top of each other.

### Why two stations can look different for no fault of theirs

**The station's message format sets the resolution of these plots.**
C/N0 is carried differently by different message types:

| Messages | Resolution |
|---|---|
| MSM4, MSM5 | whole dB-Hz |
| MSM6, MSM7 | a sixteenth of a dB-Hz |
| 1002/1004, 1010/1012 (legacy) | a quarter of a dB-Hz |
| MSM1–3 | none at all |

So an MSM7 station's plot looks denser than an MSM4 station's, and
neither is more correct — read the shape of the curve, not the number of
marks. On a station sending MSM1, 2 or 3, both C/N0 views are empty
because the stream carries no signal strength, and check 6 says so.

## Importing a navigation file

**☰ → Import navigation file** takes a RINEX 3 navigation file, plain or
`.gz`. The app tells you how many records it accepted, or why it could
not use the file — a file that turns out to be the wrong one never
replaces the one that was working.

The app never downloads one. The file comes from a data provider whose
terms are between you and them.
