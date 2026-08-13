# Telemetry — design note

**Decided 2026-08-13.** The app collects nothing. It contains no
analytics SDK, calls no endpoint of ours, and has no consent flow to
build, because there is nothing to consent to. What we learn comes from
Google Play Console, which reports on the store rather than on the
user's stations, and from reports a user chooses to send.

## What was wanted

Two numbers: **how many instances are downloaded**, and **how often the
app is used per day**.

Not as inputs to a growth plan. **This is a learning project, held to
professional standards**: nobody is optimising retention, and no
decision downstream depends on a funnel. That is what makes the cheap
answer the right one — the numbers satisfy curiosity about whether the
thing is used, and curiosity does not justify collecting anything from
anyone.

The absence of a business is not an excuse for a lower bar; it removes
the usual excuse for a *higher* intrusion. A commercial product might
argue that analytics pay for the work. Here there is nothing to weigh
against the user's privacy, so the answer is simply no.

## Why that needs no code

Play Console already answers both for any app distributed through Play:

- **Installs, uninstalls and active device installs** — standard
  statistics, no SDK.
- **Engagement, from foreground opens** — Play's own statistics, drawn
  from users who agreed to share app activity and aggregated with
  differential privacy.
- **Crashes and ANRs** — Android vitals, likewise with no SDK.

Two honest caveats. The engagement figures are a **sample**, not a
count: they come from users who opted in at the OS level and are
deliberately fuzzed. And they are **aggregate only** — no per-user
trail, nothing about which station was tested or what the verdict was.

For "how many people have it and roughly how much do they use it", that
is sufficient, and it costs nothing: no SDK, no server, no lawful basis
to establish, and **"no data collected" on the Play data-safety form**.

## What was rejected, and why

**Opt-in counters to an endpoint we host.** They would answer what Play
cannot — which KPI fails in the wild, whether checks settle or are
abandoned, whether the RINEX import is used at all. The cost is not the
code: it makes the author a data controller under the GDPR, with a
lawful basis to establish, a retention policy, deletion requests to
honour, and a server to keep patched. For two numbers Play already
provides, that is a poor trade.

**A third-party SDK (Firebase, Crashlytics).** Least work, richest data,
and the wrong instrument. Its collection becomes ours to declare, it
adds a processor, and it puts an analytics SDK inside a measurement tool
that professionals run on their customers' infrastructure. Some buyers
would refuse it, and they would be right to.

## The rules that hold whatever we build

These are not conditional on the model above; they are what "innocent"
means here.

**Never transmitted, by any mechanism, without an explicit act by the
user**: caster host, mountpoint name, credentials, position, or station
identity. A mountpoint plus a timestamp is close to a location fix, and
the caster is a third party's infrastructure — not ours to report on.

**Nothing in the background.** No beacon on launch, no periodic
check-in, no "anonymous usage statistics" default. If a user has not
acted, nothing leaves the device.

## The one thing to build: a report the user shares — with anyone

Not a channel into the author's inbox. **The report is for the user**:
something they hand to the operator who owns the station, to a
colleague, or to a client, saying what the stream actually did. The tool
already decides whether a station is fit to serve RTK; the report is
that decision, in a form someone else can act on.

Framing it that way is deliberate. A "send diagnostics to the developer"
button manufactures a support queue; a "share these results" button
sends the finding to the person who can fix the station — usually the
caster operator, never us.

**Scheduled before the free launch**, because launch is when unfamiliar
users meet unfamiliar stations, and a user who can show the evidence to
their own supplier does not need to ask anyone else.

Shape:

- A **Share** action on a finished run: verdict, the eight KPI values and
  their details, stream statistics, satellite counts, app and device
  version.
- **Caster host and mountpoint redacted by default**, with a tick box to
  include them — which a user sending it *to their own operator* will
  want, and one forwarding it into a public ticket will not. *(Default
  chosen here, not by the user; reversible.)*
- **Credentials and position never included**, tick box or not.
- The user reads it before it leaves. It is a file they send, not a
  packet we take.

## Support posture: minimal follow-up, by design

The author is not obliged to chase every issue or user error, and the
product should be built so that few reach him.

**The lever is the product, not the process**, and most of it is already
built: every KPI states its own verdict, its measured value and a
sentence of what it means; a failing check says which one and why; the
station-check window names which of three ways a run ended. A user who
can see that GPS is arriving and Galileo is not does not need to ask
what is wrong — they need to tell their operator, which the shared
report now lets them do.

What follows for the launch:

- **No support commitment in the listing.** Issues go to the GitHub
  tracker, which is public, searchable and answered when there is time.
  Play requires a contact address for the listing; that address is not a
  help desk and the wiki should say as much, plainly and without
  apology.
- **The wiki carries the answers**, so the same question is answered
  once. Troubleshooting is a page, not a correspondence.
- **User error is a documentation outcome**, not a support ticket: if
  several people make the same mistake, the wording in the app or the
  wiki is what changes.
- **No telemetry means no obligation to react.** Nothing arrives
  unbidden; what arrives is what someone chose to send, and it can be
  read when convenient.

**Check before shipping it**: `ns_stats_to_json()` writes `caster` and
`mountpoint` into every snapshot (`src/core/ns_stats.c`), and the Windows
GUI's *File → Export Statistics* writes that snapshot as-is. The
redaction rule should apply to both, or the desktop export quietly
becomes the leak the phone's report was careful to avoid.

## Consequences for the store submission

- **Data safety**: no data collected, no data shared — provided nothing
  above changes. Fill the form from Play's own guidance at submission
  time; the answers depend on SDKs, and this project has none.
- **Privacy policy**: short, and mostly a list of what does *not*
  happen. The two things it must cover are the pro edition transmitting
  the phone's position to the caster as GGA, with consent
  (`android/design/editions.md`), and the fact that credentials travel
  as NTRIP specifies until TLS lands (`design/tls.md`).
