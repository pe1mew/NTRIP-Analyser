An observation stream says *which* satellites a station is tracking and
how strongly it hears each one. It does not say **where** any of them
are. Two of the three analysis views need that:

- **The sky view** places every satellite by azimuth and elevation.
  Without orbits there is nothing to place.
- **C/N0 against elevation** is a picture of signal strength *versus
  elevation angle*, and the elevation angle is computed from the orbit.
  Without orbits the plot has no horizontal axis to speak of.

So the orbits have to come from somewhere. There are three sources, and
the badge in the top-right corner of the **Analysis** screen always says
which one is in use.

## The badge

| Badge | Meaning |
|---|---|
| ⚪ **No orbits** | Nothing imported, and the station broadcasts none. |
| 🟢 **Station orbits** | The station sends its own — the best case. |
| 🟢 **Ephemeris stream** | A second connection filled the cache (Pro). |
| 🟢 **RINEX 2 h** | Your imported file, still current. |
| 🔴 **RINEX 30 h** | Your imported file, too old to place anything. |
| 🟠 **Phone GNSS** | Falling back to this handset's own receiver. |

Tapping it brings you here.

**Amber is not an error, but it is not the station either.** The phone's
receiver knows where the satellites are *from where you are standing*.
Beside the base that is close enough to be useful; a hundred kilometres
away it is a different sky, and satellites the station tracks may be
missing from it entirely.

## 1. The station's own stream — the best case

Many stations broadcast ephemerides beside their observations (1019,
1020, 1042, 1044, 1045, 1046 and so on). Where they do, nothing else is
needed and nothing else is fetched: the orbits are current and cover
exactly the satellites the station serves.

Nothing to configure. The badge reads **Station orbits**.

## 2. A navigation file you import

**☰ → Import navigation file** takes a RINEX 3 navigation file (`.rnx`,
or a `.gz` of one). It works for any station, in both editions.

### It has to be today's

Broadcast ephemerides describe an orbit for a few hours around the time
they were transmitted. The app will not place a satellite from one more
than **four hours** old — six for BeiDou — because by then the position
it predicts is no longer the satellite's.

This is the trap worth knowing about: **a day-old file still loads, and
still reports thousands of records.** Every one of them is outside the
window, so nothing can be placed and the view quietly falls back to the
phone. The app now says so twice — when you import it, and on the badge
— but the fix is always the same: fetch a fresh file.

A daily file goes stale as the day goes on. One downloaded this morning
is no good this evening.

### Where to get one

The **BKG GNSS Data Centre** publishes hourly and daily broadcast
navigation files, openly and without an account:

- <https://igs.bkg.bund.de/root_ftp/IGS/BRDC/>

The daily mixed-constellation file is named like
`BRDC00WRD_R_2026`**`225`**`0000_01D_MN.rnx.gz` — the bold part is the
year-day, so pick **today's**. The hourly files are fresher again, and
smaller.

BKG's data is IGS data, under IGS's own terms; the app never downloads
anything on your behalf, so what you fetch and how you use it stays
between you and the provider. Other IGS data centres publish the same
files if you prefer one closer to you.

## 3. An ephemeris stream, borrowed and returned — Pro only

Most casters publish a broadcast-ephemeris mountpoint (Kadaster's
`BCEP00KAD0`, for example). Set it in the connection settings under
*Ephemeris stream* and the app uses it **when it is needed**.

"When needed" is a real policy, not a figure of speech:

- **It is not opened at all** while the station's own stream is
  delivering orbits. Holding a second connection open to receive
  messages you are already getting is rude to the caster and pointless
  to you.
- It is opened when nothing has filled the cache for **twenty seconds**
  — coverage has stopped climbing and no orbits are arriving on the
  observation stream.
- It is opened even when a navigation file is loaded, if the station
  broadcasts none itself. A file is a fallback, not a substitute for a
  live source you configured.
- It is **closed** as soon as every tracked satellite can be placed, or
  after two minutes, whichever comes first — and never sooner than
  twenty seconds after opening, which is about two broadcast cycles.
- After closing it will not redial for fifteen minutes. An incomplete
  cache is not a reason to keep dialling a caster that is not
  delivering.

The badge reads **Ephemeris stream** while it is the source.

**The free edition does not do this.** Its settings screen says so where
the fields would be. Free covers the same ground with a navigation file
you import, and needs nothing at all from a station that broadcasts its
own orbits.

## Reading the orbit card

Below the analysis views, the card says how many satellites the stream
carries, how many have an orbit, how many are cached and how old the
newest is.

**Inside the window, age hardly matters.** A kilometre of orbit error at
20 000 km is about 0.01° — invisible on a sky plot. Outside the window
the app does not extrapolate at all: it declines to draw rather than
draw a satellite where it is not.

## What a gap actually means

*"41 of 47 satellites shown"* is not a fault. A satellite that has just
risen waits its turn in the broadcast cycle, and one whose orbit nothing
covers is **counted but deliberately not drawn** — a marker at 0,0 would
sit on the horizon due north, and the plot cannot tell that from a fact.

If the number stays far below the satellite count for minutes, look at
the badge. It names what is being used, and this page says what to do
about each answer.
