# NTRIP-Analyser documentation

One documentation tree for all applications, because they ship from one
repository at one version and much of what needs explaining — the build,
the RTCM/NTRIP domain, the stream-health semantics — is shared between
them. Application manuals are flat files here; a subdirectory per
application appears only when one genuinely needs multiple pages.

## Which page do I need?

| I am using… | Read |
|---|---|
| The Windows desktop application | **[gui.md](gui.md)** |
| The command-line tool (`ntripanalyse`) | **[cli.md](cli.md)** |
| The monitoring service + Munin graphs | **[service.md](service.md)** |
| The Android app | Not released yet; its design notes live in `android/design/` |

## Shared pages

| Page | Covers |
|---|---|
| **[compile.md](compile.md)** | Building the CLI and GUI on Windows and Linux; shell completion |
| `concepts.md` | *(planned)* Stream-health semantics — what CRC error rate, advertised-vs-observed and ARP stability mean, independent of which application renders them |

## For developers, not users

Architecture and the feature backlog live in [`design/`](../design/):
[architecture.md](../design/architecture.md) explains how one core serves
four frontends; [todo.md](../design/todo.md) records what is shipped,
what is planned, and why.

`images/` holds the screenshots the manuals and the top-level readme
embed.
