# Thresholds — every number that decides a verdict

Both tiers of measurement end in a word: `STATION OK`, `CAUTION`,
`FAILED` for the ninety-second check, and `STABLE`, `DEGRADED`,
`UNSTABLE` for the stability report. Behind every one of those words is
a number someone chose.

This page lists all of them, with the rule each is used in and the
reasoning behind the value. It exists for three reasons:

1. **So a verdict can be argued with.** "Failed" is not useful unless
   you can see what it was measured against.
2. **So the numbers can be checked.** Several here are starting points
   rather than settled facts, and this page says which.
3. **So they can be changed on purpose.** A control network and a
   hobby base are not held to the same standard, and neither should
   inherit the other's numbers by accident.

Every threshold lives in `src/core/` — never in the CLI, the GUI, the
service or the Android app. That is what makes a free verdict worth the
same as a paid one, and a desktop verdict the same as a phone's.

> **On overriding them.** A mechanism for user-supplied thresholds that
> survives a restart is **proposed but not built** — see
> [the design track](https://github.com/pe1mew/NTRIP-Analyser/blob/main/design/work-items/thresholds-track.md).
> Until it lands, the values below are the values the programs use, and
> changing one means rebuilding.

---

## How to read a rule

Most thresholds have **two** levels, and the middle band is the useful
one:

| Band | Tier 1 | Tier 2 | Means |
|---|---|---|---|
| Good | `PASS` | `STABLE` | at or better than the threshold |
| Middle | `WARN` | `DEGRADED` | worth looking at; not yet unusable |
| Bad | `FAIL` | `UNSTABLE` | will cost you fixes in the field |
| No answer | `PENDING` | `INSUFFICIENT EVIDENCE` | not enough evidence *yet* — a state, not a failure |

The last row matters as much as the others. A measurement that has not
had time to be made is reported as exactly that, never as a pass.

---

## Tier 1 — the acceptance check (`src/core/kpi.h`)

Eight checks, about ninety seconds, answering *is this station fit to
serve RTK right now?*

### 1. Connected and producing

| | |
|---|---|
| Threshold | `KPI_MIN_BYTES_PER_S` = **100 bytes/s** |
| Rule | sustained throughput at or above it passes |

**Rationale.** A four-constellation MSM7 station at 1 Hz runs about
1.7 kB/s, and even a single-constellation MSM4 base clears 300 B/s. 100
is therefore not a quality bar but a *liveness* bar: below it the stream
is not delivering corrections at all, whatever the socket says. Set low
deliberately, because this check exists to separate "nothing is
arriving" from every other kind of problem, and a liveness test that
also judges quality tells you neither.

### 2. RTCM 3.x format

| | |
|---|---|
| Threshold | none — structural |
| Rule | at least one CRC-valid RTCM 3 frame must decode |

**Rationale.** Not a number: either the bytes frame and pass CRC-24Q or
they do not. A caster serving RTCM 2, NMEA or an HTML error page fails
here, which is why it is separate from check 1.

### 3. Reference position (ARP)

| | |
|---|---|
| Threshold | `KPI_ARP_DEADLINE_S` = **30 s** |
| Rule | a 1005/1006 with non-zero coordinates must arrive within it; before that the check is `PENDING`, not failed |

**Rationale.** 1005 is customarily sent every 10 s, so 30 s allows three
attempts and one to be lost. Under ten seconds the check would fail
stations that are merely between broadcasts — punishing a base for the
gap between its own messages.

### 4. Observations flowing

| | |
|---|---|
| Threshold | `KPI_MSM_MAX_DT_S` = **2.0 s** (0.5 Hz) |
| Rule | every constellation the station streams must average an epoch at least this often |

**Rationale.** RTK needs corrections at 1 Hz; 2 s is one missed epoch of
tolerance, so a station is not failed for a single hiccup while a base
that has genuinely halved its rate is caught. Measured **per epoch, not
per frame** — MSM splits one epoch across several frames, so counting
frames would report a healthy base as transmitting at a multiple of its
true rate.

### 5. Satellites in view

| | |
|---|---|
| Thresholds | `KPI_EXPECT_SATS` per constellation; `KPI_MIN_SATS` = **25** as the fallback |
| Rule | at or above the expectation passes; at or above **half** of it warns; below that fails |

Per-constellation expectations, above a 10° mask at mid latitude:

| GNSS | Expected |
|---|---|
| GPS | 8 |
| GLONASS | 6 |
| Galileo | 6 |
| QZSS | 1 |
| BeiDou | 8 |
| SBAS | 2 |
| NavIC | 2 |

**Rationale.** A table rather than one number, because a GPS+GLONASS
station cannot reach a flat 25 however healthy it is, and failing it for
that is failing it for its age. The values are deliberately modest — a
number a station should comfortably exceed rather than its best case —
because they decide a verdict. Checked against real streams: Kadaster's
`APEL00NLD0` delivers 47 across five systems where this table expects
29, and Centipede's `NEAR` delivers 40 across five where it expects 30.

The half-of-expected warn band is the interesting part: a station
holding 60 % of what it should is usually a siting or antenna problem
rather than a fault, and it deserves a different word from one holding
20 %.

### 6. Median C/N0

| | |
|---|---|
| Threshold | `KPI_MIN_CNR_MEDIAN` = **40 dB-Hz** |
| Rule | at or above passes; at or above 90 % of it (**36 dB-Hz**) warns; below fails |

**Rationale.** 40 dB-Hz is the conventional floor for a healthy
geodetic antenna and LNA chain; below it, ambiguity resolution starts
costing time. The median, not the mean, so a handful of low-elevation
satellites cannot drag the figure down. The 10 % warn band exists
because antenna chains age gradually — the useful signal is a station
that has *moved* into the band, which is why tier 2 measures the fall
rather than the level.

### 7. Frame integrity (CRC)

| | |
|---|---|
| Thresholds | `KPI_MIN_INTEGRITY_PCT` = **99.9 %** of frames passing; `KPI_BAD_INTEGRITY_PCT` = **99.0 %** |
| Window | `KPI_INTEGRITY_WINDOW_S` = **60 s**, which is `KPI_SUSTAIN_S` |
| Rule | at or above 99.9 % passes; at or above 99.0 % warns; below fails |

**Rationale.** Reported as the share of frames that **passed** — 100 %
is a clean stream and the figure falls as frames fail — because that is
how the question is asked. 99.9 % is the same standard as one error per
thousand frames (`KPI_MAX_CRC_RATE`), and tier 2 uses the identical
pair, so the two tiers cannot disagree about what a clean stream is.

> **On the name.** This is *frame integrity*, the share of frames that
> passed CRC-24Q. It is deliberately **not** called FER. Consistent with
> BER, FER would be Frame *Error* Rate — the complement of this figure —
> and in codec usage "erasure" means a frame that never arrived at all,
> which here is a dropout rather than a corruption and is reported by
> availability and delivery rate instead. Neither RTCM 10403.x nor NTRIP
> defines an error-rate metric; the term the field uses for the
> complement is **CRC error rate**, which is what the detail lines say
> when they describe the failing side.

A clean TCP path delivers essentially zero CRC failures, so anything
measurable points at the serial link, the radio, or a caster mangling
bytes.

**Measured over the last 60 s, not over the session.** A figure
accumulated since the stream opened dilutes in both directions: a burst
in the first seconds is washed out by every clean second after it, and
one late in a long run is washed out by everything before. Either way
the verdict stops being able to move — and a verdict that cannot move
makes the sustain clock meaningless, since it would be timing a number
that can no longer change. The window needs at least 100 frames before
it will report; below that it stays `PENDING`.

### 8. Advertised versus actual

| | |
|---|---|
| Threshold | none — comparison |
| Rule | a type the sourcetable advertises but never sends is a **failure**; a type sent but never advertised is a **warning** |

**Rationale.** The asymmetry is the whole point. A rover configured from
the sourcetable will not receive what it was promised — that is a
fault. Extra data nobody advertised is data the rover can use; only the
metadata is wrong. Constellations are read from the STR `nav-system`
field rather than the 1005/1006 indicator bits, because those bits cover
GPS, GLONASS and Galileo only and a BeiDou-capable base cannot declare
itself there.

---

## Tier 1 — VRS assertions (`src/core/vrs_check.h`)

Added automatically when the mountpoint is classified as a network
service. They test behaviour a fixed base never claims to have.

| # | Assertion | Threshold | Rationale |
|---|---|---|---|
| A1 | The caster accepts the GGA | `VRS_ACCEPT_S` = **5 s** | A network service that objects to a position does so immediately; a disconnect later than this is unrelated. |
| A2 | Corrections start after the GGA | `VRS_RTCM_S` = **10 s** | A VRS generates a stream for your position on demand. Ten seconds is generous for that; longer suggests it is not generating anything. |
| A3 | The ARP is near the rover | `VRS_ARP_MAX_KM` = **50 km** | A virtual or nearest-base ARP should be within the network's cell. Beyond 50 km it is not serving your position, whatever it is called. |
| A4 | The stream holds at the GGA cadence | `VRS_HOLD_S` = **60 s** | One minute covers several keep-alive intervals, so a stream that only survives while you hammer it is caught. |
| A5 | The gate test | `VRS_GATE_S` = **90 s** | Stop the GGA and a real network service drops you. Ninety seconds is longer than any caster's keep-alive tolerance seen in practice. **Opt-in**: it ends the session it is testing. |

A5 not dropping is a *classification*, not a fault: it means a fixed
base behind a network-looking name.

---

## Tier 2 — the stability report (`src/core/station_report.h`)

Six measurements over hours, answering *has this station been fit, and
is it staying that way?* Same data, different question — and
deliberately a different vocabulary, so that a station which is fit now
and was unstable all week can say both without contradicting itself.

| # | Measurement | Degraded at | Unstable at | Measures |
|---|---|---|---|---|
| 1 | Availability | `SR_RECONNECTS_WARN_PER_H` = **1.0 /h** | `SR_RECONNECTS_BAD_PER_H` = **4.0 /h** | reconnections per hour |
| 2 | Frame integrity | `SR_INTEGRITY_WARN_PCT` = **99.9 %** | `SR_INTEGRITY_BAD_PCT` = **99.0 %** | the *lowest* share of frames passing CRC in any 10 minutes |
| 3 | Signal level | `SR_CNR_DROP_WARN` = **3 dB-Hz** | `SR_CNR_DROP_BAD` = **6 dB-Hz** | the *fall* from the window's best mean C/N0 |
| 4 | Satellites held | `SR_SATS_WARN` = **25** | `SR_SATS_BAD` = **15** | the fewest held at any moment |
| 5 | Ionosphere | `IONO_ROTI_UNSETTLED` = **0.5 TECU/min** | `IONO_ROTI_DISTURBED` = **1.0 TECU/min** | the worst median ROTI |
| 6 | Delivery rate | `SR_OFFRATE_WARN` = **10 %** | `SR_OFFRATE_BAD` = **33 %** | share of samples with an advertised type off its rate |

### Why these values

**1. Availability.** One reconnection an hour is a link worth
investigating; four is a link nobody can survey on. Both are about the
*path*, not the base — a station on domestic broadband will show this
where the same receiver on fibre will not.

**2. Frame integrity** uses exactly tier 1's pair — 99.9 % passing to
warn, 99.0 % to fail — so the two tiers do not disagree about what a
clean stream is.

**The lowest reading in any ten minutes, not the figure for the
session.** This distinction is the whole metric. The rate carried on the
statistics snapshot is cumulative since the session opened, and the
extreme of a running mean fails in both directions: two errors against a
small early denominator read as 0.93 % and are banked as the "worst" for
the rest of the run, while a burst of corrupted frames six hours in
moves that cumulative figure by hundredths of a per cent and is never
noticed. Both were observed on a live stream, in the same session.

So integrity is measured over `SR_INTEGRITY_WINDOW_S` = **600 s** of
stream — ten minutes, matching `SR_MIN_WINDOW_S`, so the first reading
arrives exactly when the report first has the evidence to judge anything
at all — and the worst of those readings is what the report carries.

The window is a choice with a cost worth stating: a bad *minute* inside
a good ten shows up diluted by ten, so a minute at 99 % reads as 99.9 %
across the window. It is caught, but as `DEGRADED` rather than
`UNSTABLE`. A shorter window is more sensitive to bursts and noisier on
slow stations; ten minutes is the length at which a reading describes
something a person would call an outage.

**3. Signal level is measured as a fall, not a level.** An absolute
C/N0 says more about the site than about the station: a rooftop in a
city is not a field. A *drop* says something changed — water in a
connector, a loose cable, a new obstruction. 3 dB is half the power and
is the smallest change worth a message; 6 dB is a quarter and is a
fault. A station that falls 7 dB to 41 dB-Hz is flagged although 41
would pass tier 1 outright, and that is the intended behaviour.

**4. Satellites held** uses tier 1's fallback as its warn level so the
tiers agree; 15 is roughly the point below which RTK stops resolving in
reasonable time. This is the *minimum over the window*, so a single bad
moment is visible — which is why the report ignores the first 30 s (see
below).

**5. Ionosphere** reuses `core/iono.h`'s own scale rather than inventing
one: space weather does not mean something different here than it does
there. Under 0.5 TECU/min is quiet, over 1.0 is disturbed. This is the
one measurement on the page that is **nobody's fault** — it exists so
that poor RTK during a storm can be attributed rather than blamed on the
base.

**6. Delivery rate** counts samples in which any advertised type was
arriving off its advertised rate. A tenth of the window is a station
that stutters; a third is a station whose sourcetable entry is
fiction.

---

## Evidence rules — not thresholds about the station

These decide how much proof is needed before any verdict is offered.
They are not judgements about a station's quality, and mixing them up
with the ones above is how a report starts grading an hour's question on
a minute's data.

| Rule | Value | Why |
|---|---|---|
| `KPI_SUSTAIN_S` | **60 s** | Tier 1's verdict must hold unchanged for a full minute before it is reported. A station that flickers cannot pass by being healthy at the right moment — and a number that keeps moving cannot be quoted in a handover. |
| Tier 1 ceiling | **300 s** | A run that cannot conclude must still end. A mountpoint absent from the sourcetable leaves check 8 unable to judge — rightly, since "could not check" is not a pass — and without a ceiling the run would never finish. |
| `SR_WARMUP_S` | **30 s** | Tier 2 ignores the first 30 s of a session. `sats_total` describes the last five seconds, so a sample taken while the first epoch is still arriving sees a partial constellation. Found the hard way: a station that never dropped below 39 satellites was reported as holding 9. |
| `SR_MIN_WINDOW_S` | **600 s** | No tier-2 verdict at all below ten minutes. |
| `SR_MIN_SAMPLES` | **10** | And not without ten samples, however long the window claims to be. |
| `KPI_INTEGRITY_WINDOW_S` | **60 s** | Stream each tier-1 integrity reading covers, and deliberately `KPI_SUSTAIN_S`: a check has to be able to change within the run, or the sustain clock is timing a number that can no longer move. At least 100 frames must arrive in it. |
| `SR_INTEGRITY_WINDOW_S` | **600 s** | Stream each tier-2 integrity reading covers, matching `SR_MIN_WINDOW_S` so the first arrives when the report can first judge. |
| Sampling cadence | **1 s of stream** | Tier 2 samples once per second *of stream time*, in every program. A replay at disk speed therefore produces the same number of samples as the live run did. |

**Tier-2 windows are measured in stream time**, taken from the
observation epochs rather than the host clock. A dropout counts, because
the epochs on either side say so; an NTP correction cannot distort a
running window; and a captured file is judged over the window it holds
rather than the seconds it took to read.

---

## What is not a threshold

Two numbers that look like thresholds and are not:

- **`report_window_s`** in the service configuration (default 3600 s) is
  the *length of the rolling window*, not a pass mark. The daemon
  publishes a report covering between one and two of them. It is floored
  at `SR_MIN_WINDOW_S`, because a shorter setting could only ever
  publish `INSUFFICIENT EVIDENCE`.
- **`SV_TRACK_STALE_S`** (5 s) is how long a satellite stays counted
  after its last observation. It describes the measurement, not the
  station.

---

## Which of these are settled, and which are guesses

Stated plainly, because a page like this reads as more authoritative
than it is:

| Confidence | Thresholds |
|---|---|
| **Well founded** — conventional, or checked against real streams | KPI 6 (40 dB-Hz), KPI 5's per-constellation table, KPI 7 (1 in 1000), the ROTI scale |
| **Reasoned, not measured** — defensible, but no study behind the exact value | KPI 1 (100 B/s), KPI 3 (30 s), KPI 4 (2 s), the whole of tier 2's six pairs, all five VRS assertions |
| **Learned from a defect** — the value exists because something was wrong without it | `SR_WARMUP_S` (30 s), `KPI_SUSTAIN_S` (60 s), the 300 s ceiling |

The middle row is the honest answer to "are these correct?": they are
plausible, they behave sensibly against the stations this project has
measured, and none of them has been derived from a population of
stations. That is the strongest argument for making them overridable —
and for showing them on screen beside the verdict they produced.

---

## See also

- [The CLI's `--check` and `--report`](cli.md)
- [The GUI's Station Check and Stability windows](gui.md)
- [The monitoring service](service.md)
