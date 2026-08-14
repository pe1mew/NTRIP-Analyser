# Declaring a base station: producing the RINEX and the coordinates

A community network will not take your word for where your antenna is.
[Centipede-RTK](https://docs.centipede-rtk.org/build-base/declaration.html)
asks you to submit a coordinate solution computed by an official
positioning service, together with **the RINEX observation file you gave
that service** — so the reviewer can recompute what you claim.

This page is the route from a live NTRIP stream to those two artefacts,
using this project's tools for everything they can do and
[RTKLIB](https://www.rtklib.com/)'s `convbin` for the one thing they
cannot.

## What is actually asked for

From the Centipede declaration page, by zone:

| Zone | Coordinate evidence | Observation file |
|---|---|---|
| France | IGN calculation report (`.tar.gz`) | the RINEX sent to IGN |
| Geographic Europe | `.txt` position from the ETRF2000 conversion | RINEX observation file (`.obs`) |
| Elsewhere | NRCan report (`full_output.zip`) + the values from its *summary* link | the RINEX sent to NRCan |

Plus, in every zone: your name, profession and email; a four-character
uppercase mountpoint name; the **model of antenna and receiver**; and at
least two photographs of the installation, one close-up and one showing
the surroundings.

For the Netherlands — geographic Europe — the practical chain is
NRCan **CSRS-PPP** for the ITRF solution, then EUREF's ITRF→ETRF2000
transformation (EPN Central Bureau, `epncb.oma.be`) for the `.txt`.
CSRS-PPP needs a free NRCan account before it will accept an upload.

## What this project does and does not do

Read this before planning the session, because one row of it decides the
shape of the whole procedure:

| Capability | Where | Notes |
|---|---|---|
| Judge whether the stream is fit | CLI `--check` | eight KPIs, ~90 s |
| Census of message types | CLI `-t 120` | decides two later flags — see below |
| Decode station and antenna records | CLI `-d 1005,1006,1008,1033` | ARP, antenna height, descriptors |
| **Write the stream to a file** | **GUI only** | *File → Start RTCM Capture* |
| Survive drops during a long run | GUI *Tools → Auto-reconnect*; CLI `--reconnect` | see the caveats |
| Analyse a capture offline | CLI `--sky --rtcm-stdin`, GUI replay | same code path as live |
| Write a RINEX **observation** file | **nothing here** | `convbin` does it |

The CLI reads RINEX navigation files and writes none; there is no RINEX
writer anywhere in this repository, and the decoded observables are not
retained — the session layer keeps C/N0 and PRNs, because that is all the
eight checks ever needed. So the observation file comes from `convbin`,
and the bytes it converts come from the GUI's capture (or from RTKLIB's
own `str2str`, if you would rather stay on a command line or are not on
Windows).

## 1. Pre-flight, before you commit to a long session

Three questions decide whether the capture is worth making at all, and
two of them set flags you will need in step 5.

```bash
ntrip-analyser -m
```

The sourcetable's format-details column tells you which message types the
operator *advertises*. Then measure what actually arrives:

```bash
ntrip-analyser -t 300
```

In that census, look for:

- **MSM4/5, or better MSM6/7** (1074/1077, 1084/1087, 1094/1097,
  1124/1127 …). PPP needs dual-frequency observations; a single-band
  stream cannot be processed and there is no point capturing it.
- **1005 or 1006** — the antenna reference point. 1006 also carries the
  antenna height.
- **1008 and/or 1033** — the antenna and receiver descriptors. These
  become RINEX header fields, and PPP applies a phase-centre model by
  *matching the antenna name against the IGS table*. A missing or
  invented name silently costs you centimetres in the vertical.
- **Ephemeris messages** — 1019, 1020, 1042, 1044, 1045, 1046. Their
  presence or absence decides whether `convbin` can work out the GPS week
  on its own. Write down what you saw.

Then the verdict:

```bash
ntrip-analyser --check
```

The KPIs that matter most here are the ARP arriving inside 30 s, MSM
epochs no more than 2 s apart, at least 25 satellites, a C/N0 median at
or above 40 dB-Hz, and a CRC error rate under 0.1 %. A station that
cannot hold those for a minute will not hold them for a day.

Print the station records once, and keep the output — you will need the
values in step 5 and in the declaration email:

```bash
ntrip-analyser -d 1005,1006,1008,1033
```

## 2. Measure the antenna — the step no software can undo

The RTCM stream tells you what the receiver has been *configured* to
broadcast, which on a young base is often a rough survey-in. PPP will
replace that position, but nothing in the data recovers:

- the **vertical distance from your marker to the antenna reference
  point** — measure it, write it down, photograph the tape if you like;
- the **exact antenna model**, in its IGS form (for example
  `TRM115000.00 NONE`, including the radome field).

Both go into the RINEX header in step 5, and an error in either shifts
the final coordinate by exactly the amount you are trying to measure.

Do not touch the antenna, the mount or the receiver configuration until
the capture is finished.

## 3. Capture, unattended, for hours

**Duration.** Centipede sets no minimum, but CSRS-PPP wants a static
dual-frequency session, and quality rises with length: treat **6 hours**
as a floor, **24 hours** as the target. A day of multi-GNSS MSM7 is on
the order of 100 MB — not a constraint on any modern disk.

In the GUI:

1. Open the stream as usual.
2. **Tools → Auto-reconnect** — switch it *on before* starting the
   capture.
3. **File → Start RTCM Capture**, and accept the default
   `<mountpoint>_<timestamp>.rtcm3` name.
4. Leave it. Stop it with **File → Stop RTCM Capture** when the session
   is long enough; closing the stream stops the capture and closes the
   file cleanly too.

Two properties of that capture are worth knowing, because they decide
what `convbin` sees:

- It writes **CRC-validated frames only**. Anything that fails its
  checksum, and anything that is not RTCM 3, never reaches the file — so
  the capture is clean input by construction, and its byte count is
  smaller than the stream's.
- With auto-reconnect on, a dropped link does not end the session, so
  **the capture continues into the same file** across the drop. There
  will be a gap in the epochs, which PPP tolerates; what would hurt is
  a truncated file, and that is what auto-reconnect prevents. The Health
  tab's *Reconnects* row tells you afterwards how many gaps to expect.

If you would rather capture from a command line, or you are not on
Windows, RTKLIB's `str2str` writes the same kind of file — it ships in
the same package as `convbin`, and it reconnects on its own:

```bash
str2str -in ntrip://user:pass@caster.example.org:2101/MOUNT -out file://capture.rtcm3
```

Note that this makes a *second* client connection. Some casters allow one
session per account, and the second connection will evict the first —
so capture with one tool, not two at once.

## 4. Check the capture before you convert it

A capture is worth exactly what its bytes decode to, and it is far
cheaper to find that out now than after a PPP submission comes back
wrong. Replay it through the identical code path that read it live:

```bash
ntrip-analyser --sky --rtcm-stdin < capture.rtcm3
```

Add `-R brdc.rnx` if step 1 found **no** ephemeris messages in the
stream; if it found them, the capture carries its own orbits. The run
stops at end of file and saves a sky plot — which also makes a decent
illustration for the declaration email, alongside the required photos.

Two caveats, both real:

- `--reconnect` has **no effect** in `--sky` mode; that mode deliberately
  collects one session. It is not the tool for a multi-hour live run.
- `--rtcm-stdin` works with `--sky` only. `-t`, `-s` and `--check` are
  live-stream modes.

## 5. Convert to RINEX with `convbin`

Now the one step this project does not do:

```bash
convbin -r rtcm3 -f 3 -v 3.04 -ti 30 \
        -tr 2026/08/14 06:00:00 \
        -hm BASE \
        -ha "0/TRM115000.00 NONE" \
        -hr "0/Septentrio mosaic-X5/" \
        -hd "1.245/0/0" \
        -o BASE.obs -n BASE.nav capture.rtcm3
```

What each part is doing, and where its value came from:

| Flag | Meaning | Source |
|---|---|---|
| `-r rtcm3` | input format | the capture |
| `-f 3` | number of frequencies to keep | keeps L5/E5a; the default can drop it |
| `-v 3.04` | RINEX version | CSRS-PPP accepts 2.11 and 3.x |
| `-ti 30` | decimate to 30 s | plenty for static PPP; shrinks the upload |
| `-tr` | **approximate time of the data** | the capture's start time |
| `-hm` | marker name | your four-character mountpoint |
| `-ha` | antenna number / type | step 1's 1008/1033, verified in step 2 |
| `-hr` | receiver number / type / version | step 1's 1033 |
| `-hd` | antenna delta **H/E/N** | step 2's measured height, height first |
| `-o`, `-n` | observation and navigation output | `.obs` is what you submit |

**`-tr` is the one that bites.** RTCM's MSM epoch field carries time of
week or time of day — never the week number. RTKLIB recovers the week
from ephemeris messages when the stream carries them; when it does not,
it guesses from the clock, and a capture converted days later lands in
the wrong week with no error message. That is why step 1 asked you to
note whether 1019 and friends were present: if they were not, `-tr` is
mandatory, and if they were, it is cheap insurance anyway.

The flag names above are RTKLIB 2.4.3's. Run `convbin` with no arguments
to see what your build actually supports.

Open the resulting `.obs` in a text editor before uploading it. The
header is human-readable, and thirty seconds spent on it catches every
mistake this procedure can make: wrong antenna name, zero antenna delta,
a marker name of `-`, an approximate position in the wrong hemisphere, a
first epoch in 2006.

## 6. Compute the coordinates

Upload the `.obs` to
[CSRS-PPP](https://webapp.csrs-scrs.nrcan-rncan.gc.ca/geod/tools-outils/ppp.php)
as a **static** session. Keep everything it returns: the `full_output.zip`
and the values behind the *summary* link are themselves part of the
declaration outside Europe.

In geographic Europe, run the resulting ITRF coordinates through EUREF's
ITRF→ETRF2000 transformation and keep the `.txt` it produces — that file,
not the NRCan report, is what the declaration asks for there.

## 7. Send the declaration

To `contact@centipede.fr`, with:

- name, profession, email;
- the four-character mountpoint name;
- antenna and receiver model — the same strings that went into the RINEX
  header, so the two agree;
- the coordinate evidence for your zone, and the `.obs` that produced it;
- at least two photographs, one close-up and one of the surroundings.

Processing takes a few days, but the base is usable from the moment you
submit.

## Summary of the chain

```
ntrip-analyser -m / -t 300 / --check     is this stream worth a day?
        ↓
measure marker → ARP, note the antenna model
        ↓
GUI: auto-reconnect on → Start RTCM Capture   6–24 h → capture.rtcm3
        ↓
ntrip-analyser --sky --rtcm-stdin < capture.rtcm3   do the bytes decode?
        ↓
convbin -r rtcm3 … -o BASE.obs               capture.rtcm3 → RINEX
        ↓
CSRS-PPP → ITRF → (Europe) ETRF2000 .txt
        ↓
email contact@centipede.fr
```
