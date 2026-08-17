# The eight checks

Every check states its own verdict, the number behind it, and a sentence
of what that means. The overall verdict is the worst of the eight, and
it must hold for **sixty continuous seconds** before it is reported.

Where a check cannot be judged at all, it says so rather than passing:
*"we could not check"* and *"we checked and it was fine"* are different
statements, and only one of them is a pass.

---

## 1. Connected and producing

The caster accepted the connection and bytes are arriving. The number is
throughput in bytes per second.

- **"Connected, but the caster has sent nothing"** — the caster accepted
  you and then sent nothing at all. On a network mountpoint this usually
  means it is waiting for a position: see **Send GGA** in
  [Getting started](Getting-started). Otherwise the mountpoint may be
  published but not currently fed by its receiver.
- **"Data arrived for N s, then the stream stopped"** — a different
  finding, and one worth reading carefully. The station *did* deliver,
  and check 2 will still show the frames it delivered; something then
  ended the flow. That may be the receiver, the caster, or the network
  in between, and this check cannot tell you which — but it is not a
  station that never produced.
  One cause is close to home: many casters allow **one session per
  account**, so a second check started while the first is still running
  evicts it. Leave a gap between runs before suspecting the station.
- **"Connection lost after N s of data"** — the socket itself went away
  after the station had delivered. `--check` does not reconnect, on
  purpose: a drop inside an acceptance run is a finding rather than a
  nuisance to paper over.
- **"Connected, but throughput is below the minimum"** — something is
  arriving, but not enough to be a working observation stream.

## 2. RTCM 3.x format

Frames arrive, and their CRC checks out. The number is how many were
decoded.

- **"No valid RTCM 3.x frame yet -- wrong format?"** — the stream is not
  RTCM 3. Some mountpoints publish a receiver's raw binary format, which
  this tool does not decode. The sourcetable's format column says which.

## 3. Reference position (ARP)

The station broadcasts its own coordinates, in message 1005 or 1006.
Without it a rover has nothing to compute against.

- **"No RTCM 1005/1006 within the 30 s allowance"** — the station is not
  publishing its position. This is a genuine fault for a base station;
  report it to whoever runs it.
- **"ARP seen earlier this run but not confirmed now"** — it arrived and
  then stopped, which usually means the message is sent very
  infrequently.

## 4. Observations flowing

Every constellation the station is streaming must arrive at **0.5 Hz or
faster**. The number is how many constellations are at rate.

Note what this does *not* demand: a particular set of constellations. A
station that sends GPS and GLONASS, and says so in its sourcetable,
passes. An older station is an old station, not a broken one.

- **"Some constellations slower than 0.5 Hz"** — part of what it sends
  is arriving too slowly to position with.
- **"No observations arriving at rate"** — nothing usable is coming
  through.

## 5. Satellites in view

Counted against **what this station advertises**, not against a fixed
number: the sourcetable says which constellations it serves, and the
expectation is the sum of what those normally deliver.

- **"Below expectation -- obstruction or partial tracking?"** — fewer
  satellites than the station should see. Sky obstruction, a mask angle
  set high, or a receiver tracking only part of what it could.
- **"Fewer than half the expected satellites"** — a fault worth
  reporting.

## 6. Median C/N0

Signal strength across the satellites, as the median. Healthy is **40
dB-Hz or better**; the check warns within 10 % of that and fails well
below it.

This is the antenna and its cabling talking. A low median points at the
antenna, its siting, a long or lossy cable run, or a failing LNA — not
at the receiver.

- **"Not measured: C/N0 is read from MSM4, 5, 6 and 7"** — this stream's
  message types carry no signal strength the app reads. That is a limit
  of the reader on those formats, not a fault of the station, so it is a
  caution and never a failure.

## 7. Frame integrity (CRC)

The proportion of frames that fail their checksum. Healthy is **fewer
than one error in a thousand**.

- **"Elevated CRC error rate"** — the link is corrupting frames. This is
  usually the network path rather than the station: a marginal mobile
  connection, or a caster under load.

## 8. Advertised versus actual

Compares the stream against the promise in the caster's sourcetable, in
three directions:

- **"Advertised message types are not being sent"** — a promise not
  kept. A rover configured from this sourcetable will not receive what
  it was told to expect, so this **fails**.
- **"Some types arrive off their advertised rate"**
- **"Sending types the sourcetable does not advertise"**
- **"Streaming a constellation the sourcetable omits"**

The last three are cautions. Sending more than you advertise is not a
fault in the stream, but it misleads whoever chose the mountpoint from
the sourcetable, so it is worth telling the operator.

Advertising a constellation that is not currently streamed is
**ordinary** and is not flagged — QZSS is advertised across Europe and
visible from none of it.

- **"No sourcetable entry to compare against"** — the caster published
  nothing for this mountpoint, so the comparison could not be made at
  all. A caution, because nothing was checked.
