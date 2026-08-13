# Privacy, and how support works

## Privacy, in short

**The app collects nothing.** No analytics library, no account, no
advertising, and nothing sent to its developer. It has no server.

What it transmits, it transmits to the caster *you* configure, because
that is what connecting to a caster means:

- your **credentials**, as the NTRIP protocol specifies;
- a **position**, but only when the mountpoint asks for one — and in the
  free edition only the position you set yourself, never the phone's.

Your credentials are stored encrypted on the device. **They travel as
NTRIP specifies, which is base64 over a plain connection**, so anyone
able to observe the network path can read them. That is a property of
the protocol rather than of this app, and the app says so where you type
a password. Encrypted transport is planned, and when it arrives it
arrives in both editions on the same day.

Location permission is asked when you first open the analysis views, and
in the free edition it is used **on the device only**, to place
satellites in the sky view.

The full policy is at
**<https://pe1mew.github.io/NTRIP-Analyser/privacy-policy>**.

## Support, honestly

This is a personal project, held to professional standards but built in
someone's own time. There is no help desk, and the contact address the
Play listing requires is not one.

What there is:

- **This wiki.** If something is unclear, it is a page here that should
  change. Questions asked more than once become documentation rather
  than a queue of replies.
- **The [GitHub issue tracker](https://github.com/pe1mew/NTRIP-Analyser/issues)** —
  public, searchable, and answered when there is time. Search before
  opening; include what the eight rows said.
- **The app itself.** Every check states its own verdict and the number
  behind it, so a failing station tells you which part failed and why.
  That is deliberate: you should rarely need to ask anyone, and when you
  do, the person to ask is usually the operator of the station rather
  than the author of this app.

## If the station is at fault

That is the normal outcome of a failing check, and the app is built to
give you something to hand over: which check failed, the measured value,
and what it means. Send that to whoever runs the station — they can fix
it, and nobody else can.
