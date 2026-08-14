# NTRIP-Analyser

Connects to an NTRIP caster and answers one question about a GNSS base
station: **is it fit to serve RTK, and if not, why?** One measurement
core in C99 serves four programs — a command-line tool, a Windows GUI, a
Linux monitoring daemon, and an Android app in two editions.

By Remko Welling (PE1MEW). Source, releases and issues:
[github.com/pe1mew/NTRIP-Analyser](https://github.com/pe1mew/NTRIP-Analyser).

## For users of the Android app

- [Privacy policy](privacy-policy.md) — what the app handles, what
  leaves the phone, and what the developer receives (nothing automatic).

## Documentation

- [Command-line tool](cli.md)
- [Windows GUI](gui.md)
- [Monitoring daemon](service.md)
- [Configuration files](jsonConfigs.md) — one format for every program
- [Declaring a base station](base-declaration.md) — from a live stream to
  the RINEX and coordinates Centipede-RTK asks for
- [Building](compile.md)
- [Third-party licences](licences.md)
- [Security review](https://github.com/pe1mew/NTRIP-Analyser/blob/main/design/security-review.md)
