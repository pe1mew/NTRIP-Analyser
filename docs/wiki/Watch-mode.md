# Watch mode

A station check grades a station. **A watch observes one.**

Ninety seconds answers *is this station healthy now*. It cannot answer
*does it stay healthy*, and that is the question behind most real
complaints: the base that drops for forty seconds every twenty minutes
passes every spot check anyone ever runs on it.

## Running one

Open **Analysis** and tap **Start analysis**. The run keeps going until
you stop it — through the screen going dark, and with a notification for
its whole life so the system leaves the connection alone.

Everything else behaves as during a check: the eight rows keep updating,
and the analysis views keep filling.

## Reading the Watch card

It appears on the main screen once a watch is running.

| Line | What it means |
|---|---|
| **Watching for 3 h 07 m** | How long this watch has been going |
| **Healthy 99.4% of judged time** | The share of time the verdict was OK. "Judged" excludes the opening seconds before there was enough evidence to judge at all |
| **Current healthy streak 42 min (best 2 h 15 m)** | How long it has been clean, and the best it managed |
| **3 degradation(s); worst FAILED** | How many times it left OK, and how bad it got |
| **No degradation yet; worst OK** | Nothing has gone wrong so far |

**A degradation is the event, not the duration.** Three short drops and
one long one both read as degradations; the streak lines are what
separate them.

## What to run, and for how long

- **An hour** catches a station whose satellite geometry or interference
  varies through the day.
- **A working day** is what an acceptance report wants: availability
  over a period the customer cares about, with the worst state named.
- **Overnight** is the honest way to catch a scheduled restart, a
  maintenance window, or a caster that sheds clients under load.

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
