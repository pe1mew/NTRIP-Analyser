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
| **Write the stream to a file** | CLI `--capture`; GUI *File → Start RTCM Capture* | frames only, identical from either |
| Survive drops during a long run | CLI `--reconnect`; GUI *Tools → Auto-reconnect* | one file spans the outage |
| Analyse a capture offline | CLI `--sky --rtcm-stdin`, GUI replay | same code path as live |
| Write a RINEX **observation** file | **nothing here** | `convbin` does it |

The CLI reads RINEX navigation files and writes none; there is no RINEX
writer anywhere in this repository, and the decoded observables are not
retained — the session layer keeps C/N0 and PRNs, because that is all the
eight checks ever needed. So the observation file comes from `convbin`.
The bytes it converts come from this project, from either program: the
capture lives in the shared session layer, so the CLI and the GUI write
the same file from the same stream.

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

## 2. The antenna — the step no software can undo

The RTCM stream tells you what the receiver has been *configured* to
broadcast, which on a young base is often a rough survey-in. PPP will
replace that position, but nothing in the data recovers the antenna's
identity or its height. Both go into the RINEX header in step 5, and an
error in either shifts the final coordinate by exactly the amount you are
trying to measure.

Do not touch the antenna, the mount or the receiver configuration until
the capture is finished.

### Find the calibration, and know which table it is in

PPP corrects for the antenna by matching the name in your RINEX header
against a calibration table. The name must be the **exact** one from that
table, radome field included — a near miss is a miss. Do not trust a
vendor's word for it; grep the tables themselves. They are the two that
matter, and the answer takes seconds:

```bash
curl -sL https://files.igs.org/pub/station/general/igs20.atx | grep -B2 -A2 -i "<model>"
```

```bash
curl -sL "https://geodesy.noaa.gov/ANTCAL/LoadFile?file=ngs20.atx" | grep -B2 -A2 -i "<model>"
```

A hit prints a `TYPE / SERIAL NO` line; the first 20 characters are the
string you need — 15 for the model, a space, then 4 for the radome
(usually `NONE`). Three outcomes:

| Found in | What it means |
|---|---|
| `igs20.atx` | Best case. CSRS-PPP uses IGS products and will apply it, so the solution refers to your ARP directly. |
| only `ngs20.atx` | NGS calibrated it, IGS has not adopted it — commonly because it is a relative calibration converted to absolute. CSRS-PPP may report the antenna as unknown and apply nothing. |
| neither | No correction at all. The solution refers to the phase centre, which on a survey antenna sits several centimetres above the ARP. |

Read the entry, not just the name. `METH / BY / # / DATE` says how it was
calibrated, and a `COMMENT` reading `CONVERTED FROM RELATIVE NGS ANTENNA
CALIBRATIONS` means it is not a native absolute calibration whatever
`METH` says. `# OF FREQUENCIES` matters too: an entry with only `G01` and
`G02` covers GPS L1/L2 and nothing else — which is usually fine, because
that is what PPP solves with.

### Where the ARP is, and what to measure

The **antenna reference point** is the flat underside of the housing, the
face that seats on the mount. Not the dome, not the tip of the thread.
Anything below it — adapter, levelling mount, spacer — is part of the
distance you measure, not part of the antenna.

The **marker** is the point the published coordinate will refer to. On a
survey pillar that is a scribed cross or a bolt. On a rooftop or a mast
there is often no such thing, and inventing one helps nobody:

- **No marker**: declare the ARP itself, and use `0.000` for the antenna
  delta. The PPP solution *is* the ARP coordinate, and "the coordinate is
  the antenna reference point of a &lt;model&gt;" is a complete, checkable
  statement. Photograph the antenna and its mount so a reviewer can see
  what the point refers to.
- **A real marker**: measure the **vertical** distance from it up to the
  ARP — tape held plumb, not laid along a slanted mast — and read it to
  the millimetre. A centimetre of error is a centimetre of error in the
  published height, permanently, and nothing downstream can detect it.

## 3. Capture, unattended, for hours

**Duration.** Centipede sets no minimum, but CSRS-PPP wants a static
dual-frequency session, and quality rises with length: treat **6 hours**
as a floor, **24 hours** as the target. Measured on a four-constellation
MSM7 station (`1077/1087/1097/1127` at 1 Hz): about **2 kB/s, so ~180 MB
a day**. Not a constraint on a normal disk, but worth checking against a
small VPS volume before starting — and `--capture-max` is there for
exactly that.

On any machine that stays up — a Pi, a VPS, a laptop that does not
sleep — the CLI is the tool for this:

```bash
ntrip-analyser -t 86400 --reconnect --capture /var/spool/gnss/ -q
```

A day of stream into a directory, drops ridden out, with the message
census printed at the end as a record of what the file contains. The
capture is named `YYYYMMDDHHmmss_<mountpoint>.rtcm3`. Exit 7 means the
capture failed — the one status a cron job must not ignore.

Started over SSH, that run has to outlive the connection. See
[Running it unattended](cli.md#running-it-unattended-over-ssh) in the CLI
manual: `systemd-run` is the shape to use, because stopping the unit
closes the capture cleanly instead of abandoning it.

In the GUI, on Windows:

1. Open the stream as usual.
2. **Tools → Auto-reconnect** — switch it *on before* starting the
   capture.
3. **File → Start RTCM Capture**, and accept the default
   `YYYYMMDDHHmmss_<mountpoint>.rtcm3` name.
4. Leave it. Stop it with **File → Stop RTCM Capture** when the session
   is long enough; closing the stream stops the capture and closes the
   file cleanly too.

Two properties of the capture are worth knowing — they hold for both
programs, which write it through the same code — because they decide
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

RTKLIB's `str2str` will also write such a file, and ships in the same
package as `convbin`. There is no longer a reason to reach for it here,
and one reason not to: running it *beside* the analyser makes a second
client connection, and some casters allow one session per account, where
the second evicts the first. Capture and analyse in one process.

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

Open the resulting `.obs` in a text editor before uploading it — `head
-20` is enough. The header is human-readable, and thirty seconds spent on
it catches every mistake this procedure can make:

| Header line | What it must say |
|---|---|
| `MARKER NAME` | your four-character name, not `-` |
| `ANT # / TYPE` | the calibration string **exactly**, radome at columns 17–20 |
| `REC # / TYPE / VERS` | the receiver; nothing keys off it, but a reviewer reads it |
| `ANTENNA: DELTA H/E/N` | your measured height — or a deliberate `0.0000` if the ARP is the declared point |
| `APPROX POSITION XYZ` | picked up from the broadcast 1005; sanity-check the hemisphere |
| `TIME OF FIRST OBS` | the day you captured, proving `-tr` worked |
| `SYS / # / OBS TYPES` | for `G`, both an L1 and an L2 code — this is the dual-frequency requirement, visible |

A worked example, from a real conversion of a six-hour capture (Harxon
CSX627A on a Unicore UM980, no ground marker so the ARP is the declared
point, and a stream carrying no ephemerides so `-tr` is mandatory):

```bash
convbin -r rtcm3 -f 3 -v 3.04 -ti 30 \
        -tr 2026/08/15 18:13:28 \
        -hm RFSE \
        -ha "0/HXCSX627A       NONE" \
        -hr "0/UNICORE UM980/" \
        -hd "0.000/0/0" \
        -o RFSE227S.26o 20260815201328_RFSEE01.rtcm3
```

Note the seven spaces inside the antenna string, and that the filename's
`227` is the day of year — the conversion does not care, but a reviewer
reading it in six months does.

Converting the capture **while it is still being written** is a useful
rehearsal: it proves the flags and the header before the session ends,
while there is still time to fix them.

## 6. Compute the coordinates

Upload the `.obs` to
[CSRS-PPP](https://webapp.csrs-scrs.nrcan-rncan.gc.ca/geod/tools-outils/ppp.php)
as a **static** session. Keep everything it returns: the `full_output.zip`
and the values behind the *summary* link are themselves part of the
declaration outside Europe.

**Read the report for what it says about your antenna.** It names the
model it used and whether corrections were applied. If it did not
recognise the name — the likely outcome when the calibration is in
`ngs20.atx` but not `igs20.atx` — the height it returns refers to the
**phase centre**, not the ARP, and you must correct it yourself.

The offset to subtract is the one PPP actually solves for: the
ionosphere-free combination of the two vertical offsets in the ANTEX
entry.

> UP(IF) = 2.5457 × UP(L1) − 1.5457 × UP(L2)

For the worked example above — `UP` of 49.84 mm at L1 and 44.65 mm at L2
— that is **57.9 mm**, so the phase centre sits about 58 mm above the
ARP and the reported height is that much too high. Horizontal offsets on
a survey antenna are a few millimetres and can usually be ignored;
the vertical one cannot.

Whatever you conclude, say it in the declaration: the antenna name, which
table its calibration came from, and whether PPP applied it. That tells a
reviewer exactly how far to trust the vertical, which is the entire point
of declaring the equipment at all.

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
ntrip-analyser -t 86400 --reconnect --capture DIR/    6–24 h → .rtcm3
        ↓
ntrip-analyser --sky --rtcm-stdin < capture.rtcm3   do the bytes decode?
        ↓
convbin -r rtcm3 … -o BASE.obs               capture.rtcm3 → RINEX
        ↓
CSRS-PPP → ITRF → (Europe) ETRF2000 .txt
        ↓
email contact@centipede.fr
```
