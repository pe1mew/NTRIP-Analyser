# Vendored CA bundle — the Mozilla roots, as curl distributes them

`cacert.pem` is the Mozilla root store from https://curl.se/ca/
(MPL-2.0), downloaded 2026-08-25 with its published SHA-256 verified:

    f66dff1bdf8f96060b8177976f8b7d9254bc89bc4db933d769f7384d28480bc9

It is not read at run time anywhere. `tools/make_ca_bundle.py` turns
it into the committed C array `src/session/ns_ca_bundle.c`, which is
what every build compiles — one trust store for the CLI, GUI, daemon
and both phone editions alike, because the NDK has no system PEM to
point at and every product must give the same answer about the same
caster. `check_release.py` regenerates and diffs the array against
this PEM, so the two cannot drift.

Refresh on the half-year-a-day cadence the design budgeted (or when
the release check starts warning about its age): fetch the new
`cacert.pem` + `.sha256` pair, verify, re-run the generator, commit
all three files together.
