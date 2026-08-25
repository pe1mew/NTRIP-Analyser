# Watch mode

A station check grades a station. **A watch observes one.**

Ninety seconds answers *is this station healthy now*. It cannot answer
*does it stay healthy*, and that is the question behind most real
complaints: the base that drops for forty seconds every twenty minutes
passes every spot check anyone ever runs on it.

## Running one

Tap **Watch station** on the station screen — every run starts and
stops there, and each run button says beneath its name which tier
answers and roughly what it costs: the check gives a verdict in about
two minutes; the watch adds stability and needs ten minutes of
evidence before it says anything at all.

The run keeps going until you stop it — through the screen going dark,
and with a notification for its whole life so the system leaves the
connection alone. While it runs, the verdict banner says so: its
subtitle reads *watch 4 min · evidence 98/600 s* and its progress bar
climbs toward the ten-minute stability floor; once the floor is
passed, the bar retires and the subtitle carries the stability verdict
instead.

Everything else behaves as during a check: the eight rows keep
updating, and the analysis views keep filling — the **Analysis**
screen is the viewing room, not the cockpit; it starts and stops
nothing.

## The stability report

Below the eight checks, a watch fills the **Stability** card: six
metrics over the whole run — availability, frame integrity, signal
level, satellites held, ionosphere, delivery rate — each with the
figure that decided it.

**Its words are deliberately not the check's.** A station can be fit
this minute and have been unstable all week, or the reverse; so the
card says STABLE, DEGRADED or UNSTABLE, and *STATION OK* stays the
check's word alone.

**INSUFFICIENT EVIDENCE is an answer, not an error.** The card needs
ten minutes of stream before it judges anything, and until then it
says exactly how much is still owed. During a plain check — which ends
long before ten minutes — the card says so and points you at the
watch. Windows are measured in the *stream's* time, which is what
makes a replayed capture report exactly what the live run did; and a
report whose stream has stopped moving declines to keep judging,
rather than publishing STABLE from evidence that stopped being about
now.

The longer the watch, the more the verdict means: *STABLE over 0.2 h*
is a start; *STABLE over 4.0 h* is a sign-off.

## Reading the Watch card

It appears on the main screen once a watch is running.

| Line | What it means |
|---|---|
| **Watching for 3 h 07 m** | How long this watch has been going |
| **Healthy 99.4% of judged time** | The share of time the verdict was OK. "Judged" excludes the opening seconds before there was enough evidence to judge at all |
| **Current healthy streak 42 min (best 2 h 15 m)** | How long it has been clean, and the best it managed |
| **3 degradation(s); worst FAILED** | How many times it left OK, and how bad it got |
| **No degradation yet; worst OK** | Nothing has gone wrong so far |
| **2 reconnects — the stream dropped and came back** | The connection was lost and re-made. Shown only when it happened |

**A degradation is the event, not the duration.** Three short drops and
one long one both read as degradations; the streak lines are what
separate them.

**Read the reconnect line together with the degradation line.** Three
degradations *with* reconnects is a link that dropped — a tunnel, a cell
hand-over, a caster restart. Three *without* is a station that faltered
while the connection held. Those are different faults, and only the pair
tells them apart.

## The app reconnects by itself

A dropped stream is not the end of a watch. The app waits a second and
tries again, doubling the wait to a minute at most, and goes on doing
that for as long as the watch runs — so a phone that walks into a
building, hands over between masts, or meets a caster being restarted
picks the stream back up without being asked. A stream that expires is
treated as a drop, and reconnected the same way.

**This cannot turn a bad station into a good one.** The verdict's
sustain clock resets on the gap, so OK has to be re-earned from the
reconnection onwards, exactly as it does after any degradation. What
reconnecting buys is that one bad minute does not cost you the other
eight hours.

Both editions do this; it is not a paid capability. What it leaves
behind is the count on the Watch card, the same number under **check 1**,
and a break in the satellite trails where nothing was measured.

## What to run, and for how long

- **An hour** catches a station whose satellite geometry or interference
  varies through the day.
- **A working day** is what an acceptance report wants: availability
  over a period the customer cares about, with the worst state named.
- **Overnight** is the honest way to catch a scheduled restart, a
  maintenance window, or a caster that sheds clients under load.

⚠ **Android 15 sets a ceiling on this.** A phone running Android 15 or
later allows an app about **six hours** of background streaming in a
day, and then stops the service itself. The watch ends there and says
so — *"stopped by Android's six-hour limit"* — and everything measured
up to that point is kept and reported, so an interrupted overnight watch
still yields the hours it did manage. To cover a longer period, start a
fresh watch afterwards. Android 14 and earlier have no such ceiling.

## How a capture ends, and what survives it

A watch ends in one of three ways: **you stop it**, **the link fails**,
or **Android stops it** at the six-hour ceiling above.

**Capture stops; the picture stays.** Whichever ending it is, nothing
measured is thrown away. The eight rows, the sky view with its trails,
the signal bars and the C/N0 scatter all hold what they had at that
moment, and can still be read, compared and shared. What ends is the
measuring: no new data arrives, and the numbers stop moving. When it is
Android's ceiling, a dismissible notification says so, and the main
screen reads *"stopped by Android's six-hour limit"* rather than
borrowing the word for what the Stop button does.

**What survives is in the app, not on the phone.** No capture is written
to storage — the only thing this app keeps on disk is your connection
settings. A capture survives the watch ending, rotating the phone, the
screen going dark, and leaving the app for something else. It does not
survive swiping the app away, restarting the phone, or Android
reclaiming the app's memory while you are elsewhere.

So **get a capture worth keeping out of the app before the phone goes
back in a pocket**, and there are two ways out: the share control in
the top bar sends the report from the main screen and the picture from
a plot, and Pro's **Export statistics…** (in the ⋮ menu) writes the
numbers themselves as a file — the full snapshot as JSON, or the same
CSV columns the desktop tools produce, for a spreadsheet. The export
works during a run and after it; only a capture that was never taken
cannot be saved.

**A fresh watch is a fresh capture.** The plots belong to one run and
are cleared when the next one starts, which is why a period longer than
the ceiling is covered by consecutive watches rather than by one
continuous picture. Note when each one started; the app cannot join them
for you.

## How much a capture holds

Neither plot fills up in any period you are likely to watch:

| | |
|---|---|
| **Satellite trails** (Pro) | A **day** per satellite, at a point a minute. A longer watch keeps the most recent day and drops the oldest points as it goes |
| **C/N0 against elevation** | **Everything**, however long the watch. Samples are counted into the plot's own cells rather than kept one by one, so nine hours and nine minutes cost the same memory |
| **The eight rows and the Watch card** | The whole watch: availability, degradations and streaks are running totals |

The numbers to quote are **availability**, **degradations** and **worst
state**. A station at 99.9 % with one FAILED degradation is telling you
something different from one at 99.9 % that never left CAUTION.

## Practicalities

- It holds a network connection open for the whole watch. On mobile
  data, that is measurable — a typical station runs at 1–3 kB/s, so
  roughly 4–10 MB per hour. It is measurable at the other end too: a
  multi-hour watch is a multi-hour client on somebody's caster, so watch
  a station you are entitled to watch ([Getting
  started](Getting-started#whose-caster-is-it)).
- Keep the phone on power for anything longer than an hour or two.
- If a watch stops unexpectedly, check battery optimisation for this
  app. The notification should prevent it, but some manufacturers are
  aggressive regardless.
- The verdict during a watch is the same eight checks. A station that
  degrades and recovers will show the recovery: the sustain clock
  restarts, so OK is re-earned rather than remembered.
