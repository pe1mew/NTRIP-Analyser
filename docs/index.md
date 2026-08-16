# NTRIP-Analyser

Connects to an NTRIP caster and answers one question about a GNSS base
station: **is it fit to serve RTK, and if not, why?** One measurement
core in C99 serves four programs — a command-line tool, a Windows GUI, a
Linux monitoring daemon, and an Android app in two editions.

By Remko Welling (PE1MEW). Source, releases and issues:
[github.com/pe1mew/NTRIP-Analyser](https://github.com/pe1mew/NTRIP-Analyser).

One documentation tree for all four, because they ship from one
repository at one version and much of what needs explaining — the build,
the RTCM and NTRIP domain, what the stream-health numbers mean — is the
same whichever program renders it.

## Which page do I need?

| I am using… | Read |
|---|---|
| The Windows desktop application | **[gui.md](gui.md)** |
| The command-line tool (`ntrip-analyser`) | **[cli.md](cli.md)** |
| The monitoring service and its Munin graphs | **[service.md](service.md)** |
| The Android app | **[privacy-policy.md](privacy-policy.md)** — what it handles, what leaves the phone, and what the developer receives. The user guide is the [wiki](https://github.com/pe1mew/NTRIP-Analyser/wiki) |

## Shared pages

| Page | Covers |
|---|---|
| **[compile.md](compile.md)** | Building on Windows and Linux; shell completion |
| **[jsonConfigs.md](jsonConfigs.md)** | The one JSON configuration format, which program reads how many entries, and why the passwords in it are in the clear |
| **[base-declaration.md](base-declaration.md)** | From a live stream to the RINEX observation file and coordinates a network such as Centipede-RTK asks for |
| **[licences.md](licences.md)** | What the project licenses out, what it ships, and the terms of the data services it connects to |
| **[privacy-policy.md](privacy-policy.md)** | One policy for every program in the suite |

A `concepts.md` is planned, to explain the stream-health semantics — CRC
error rate, advertised-versus-observed, ARP stability — once, independent
of which application renders them.

## For developers, not users

Architecture and the feature backlog live in
[`design/`](https://github.com/pe1mew/NTRIP-Analyser/tree/main/design):
[architecture.md](https://github.com/pe1mew/NTRIP-Analyser/blob/main/design/architecture.md)
explains how one core serves four frontends;
[feature-matrix.md](https://github.com/pe1mew/NTRIP-Analyser/blob/main/design/feature-matrix.md)
says which program does what, and why each split falls where it does;
[todo.md](https://github.com/pe1mew/NTRIP-Analyser/blob/main/design/todo.md)
records what is shipped, what is planned, and why;
[security-review.md](https://github.com/pe1mew/NTRIP-Analyser/blob/main/design/security-review.md)
says what a hostile caster can do to this software, what was fixed, and
what is still open.

Those links are absolute deliberately: `design/` is outside the folder
GitHub Pages publishes, so a relative `../design/…` link resolves to
nothing on the website even though it works when browsing the repository.
`tools/check_release.py` fails the build if one creeps back in.

**`docs/` is served as a website**, so it holds what is written to be
read by someone who is not us. Working documents — the release plan, the
store listings, the security assessment — live in `design/`, which Pages
never sees. `images/` holds the screenshots the manuals and the
repository's front page embed.
