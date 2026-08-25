# TLS to the caster — rollout plan

Phase 2, item 6 — the last (`design/guiV2rollout.md`: … → export →
tier 2 → **TLS**). The decisions are not made here: `design/tls.md`
made them on 2026-08-13 and they stand — a **bundled TLS library
behind a transport abstraction in the session layer**, shipping in
**both editions on the same day**, because the paid edition withholds
convenience, never protection. This item also completes what pro was
waiting for before Play, and unfreezes free.

The estimate stands too: **5–8 focused days** — the largest phase-2
item by a factor of three, and the only one that touches every build
system and every frontend at once. It is steps L1–L6 below, each a
separate commit gate like every item before it.

## The decisions this plan adds (open below until the author confirms)

1. **The library: mbedTLS**, vendored source like cJSON is.
   Apache-2.0 — the project's own licence — actively maintained,
   plain C99, builds as a CMake subdirectory on desktop and NDK alike,
   and does explicit hostname verification
   (`mbedtls_ssl_set_hostname`), which `tls.md` names as
   non-negotiable. wolfSSL falls to its GPL licence; BearSSL to its
   maintenance pace.
2. **`build-gui.bat` retires.** `tls.md` said decide before starting.
   CMake already builds the GUI (`ntrip-analyser-gui` target); a
   hand-listed `gcc` line cannot absorb a TLS library; and the
   promoted gotcha *two build systems over one source set* argues for
   fewer, not cleverer. One deletion, runbook and docs updated, and
   the "GUI builds under CMake but build-gui.bat fails" symptom row
   retires with it.
3. **One CA bundle, embedded, everywhere.** Not per-OS trust stores:
   the NDK has none, and one bundled list means every product trusts
   identically — one more face of one-core-four-frontends. The
   Mozilla/curl PEM bundle, converted to a C array at build time so no
   platform has a file-path problem, refreshed on the half-day-a-year
   cadence the design already budgeted. A release check warns when the
   bundle is older than a year.
4. **Two new failure codes**, not zero and not five:
   `NS_FAIL_TLS_HANDSHAKE` (the caster does not speak TLS here, or
   negotiation failed — points at the port/flag) and
   `NS_FAIL_TLS_CERT` (expired, wrong host, untrusted — points at the
   caster, and the detail sentence says which of the three). The
   failure-code parity check will demand `Failure.kt` and the
   sentences in the same commit, which is the guard working.
5. **The flag rides the connection, and every connection of a caster
   inherits it**: the observation stream, the sourcetable fetch, and
   pro's ephemeris side-stream each carry their own TLS setting from
   their own config — the eph stream may be a different caster. An
   explicit boolean in the shared config (`TLS`), never port-sniffing;
   the UI may *suggest* it when the port is 443.
6. **The release that ships it is 3.8.0** — a minor, both editions,
   and the release that opens pro's road to Play
   (`release-to-play.md` takes over from there).

## On a branch, until accepted

**The whole item is developed and tested on a branch — `tls` — and
reaches `main` only by the author's acceptance and merge** (author's
direction, 2026-08-25). Every item before this one landed on `main`
step by step, and could: each step left `main` releasable. TLS is
different in exactly the way that rule cares about — it rewires the
socket layer under every product and every build system, and a
half-landed transport seam on `main` would sit inside whatever else
ships next. The branch keeps `main` releasable throughout; free's
Play cadence continues from `main` untouched.

Working rules on the branch, so it stays mergeable rather than
becoming a world of its own:

- The same step-by-commit discipline: L1–L6 are commits on the
  branch, each gated by the author as ever, with the same falsified
  verifies.
- Rebase or merge `main` into the branch after any `main` release, so
  the eventual merge is small and boring.
- CI runs on the branch via a **draft pull request opened at L1** —
  confirmed while writing this plan: the workflows fire on pushes to
  `main` and on `pull_request`, so the draft PR is what buys CI per
  branch push, and it doubles as the running record the acceptance
  review reads. The author's merge at the end is the PR's merge.
- The acceptance gate at the end: the full suite, the 98+ release
  checks, both editions on the Huawei over Kadaster's TLS, and the
  author's word — then one merge to `main`, and 3.8.0 cuts from
  `main` only.

## Steps

### L1 — the transport seam, with nothing behind it changed  *(done 2026-08-25)*

Built as planned — `ns_transport.{h,c}` in the session layer, the
platform socket code moved whole, both callers rewired — with two
deviations worth their lines. First, the sourcetable fetch came out
*more* changed than "pure refactor" promised, because unifying on the
seam's connect path is the seam: it resolves IPv6 now (AF_UNSPEC like
the stream, where it was AF_INET only), it fails in the stream's
failure taxonomy instead of platform-numbered stderr lines, and its
per-call WSAStartup/WSACleanup pair is gone — the transport starts
Winsock once and never cleans up, because a WSACleanup from one caller
pulls the stack from under every other connection in the process.
Second, the falsification taught one thing the plan's wording missed:
with recv eating every byte, `stall`, `failure` and `bridge_vrs` went
red as demanded, but `capture` stayed green — it replays a file, and a
file never crosses the transport. "Half the suite red" was really
"every test with a socket under it red", which is the sharper claim.

*(As planned:)*

`ns_transport` in the session layer: connect / send / recv / close as
an indirection, the existing plaintext code becoming its first
implementation. `sock_connect`, `sock_recv`, the four raw `send`s and
five `closesocket`s in `ntrip_session.c`, and the sourcetable fetch's
six calls in `ntrip_handler.c`, all through the seam. **A pure
refactor**: no config change, no new capability.

**Verify.** The whole suite green unchanged — fifteen tests including
the loopback harnesses, which exercise the seam by existing. Falsify
by breaking the plaintext transport's recv: half the suite must go
red, proving the seam actually carries the traffic.

### L2 — the library joins every build  *(done 2026-08-25)*

Mbed TLS **3.6.7** (the LTS line; 4.x splits into a second repository
and was declined in the vendor note), taken from the official release
asset with its published SHA-256 verified, vendored as `include/` and
`library/` only — `lib/mbedtls/NTRIP-ANALYSER-NOTE.md` records what
the copy is and how to refresh it. Upstream's CMake is not used:
every build compiles `library/*.c` itself, cJSON's arrangement at
larger scale. Three deviations from the step's wording, all of them
the step done more honestly:

* **The daemon has its own build** the plan forgot by name —
  `service/Makefile` — and it needed nothing but its wildcard
  extending to the vendor directory; built under MinGW to prove it.
* **The retirement was bigger than one file**: `build-gui.ps1` and
  the four hand-listed gcc tasks in `.vscode/tasks.json` were the
  same disease and went too (the .bat had in fact been broken since
  L1 — it never learned `ns_transport.c` — the two-lists gotcha
  firing one last time on its way out). Nine documents updated,
  including CI's own comments and the readme's stale one-liners.
* **`check_source_lists` was taught, not excused**: the vendor tree
  is outside its `src/` regexes by design, so a new check demands
  that all three builds name `lib/mbedtls/library` — falsified red
  by pointing the NDK list at a directory that does not exist, then
  restored. 98 checks became 101.

Notices: Mbed TLS joins both generated texts, its version read from
`build_info.h` — the header the builds compile — never typed.
Verified by artefact: desktop suite 15/15 green with `libmbedtls.a`
linked beneath the session; the daemon binary rebuilt; the NDK `.so`
rebuilt and `llvm-nm` shows `mbedtls_ssl_set_hostname` and the X.509
family in arm64. No behaviour change: no source file outside the
build lists moved.

*(As planned:)*

mbedTLS vendored; desktop CMake, the NDK CMakeLists, and the daemon's
build all link it; `build-gui.bat` deleted with its runbook section.
`check_source_lists` is scoped to `src/`, so the vendor directory
needs either its exclusion stated or the check taught — decided when
the tree shows which.

**Verify.** All four artefact families build; the NDK `.so` still
loads (symbol check, by artefact); no behaviour change yet.

### L3 — the TLS transport  *(done 2026-08-25)*

Built as planned, with the fixtures exactly as promised — a toy CA
and four leaves committed under `test/data/tls/` (README says why the
keys are worthless), a real TLS caster on its own thread in
`test_tls.c`, fourteen assertions across the five cases, each landing
on its code with its sentence. Three deviations and one discovery:

* **The CA bundle is converted at refresh time, not build time.**
  Decision 3 said "at build time"; four build systems would each have
  needed Python. Instead `tools/make_ca_bundle.py` writes the
  committed `src/session/ns_ca_bundle.c` from the verified
  `lib/ca-bundle/cacert.pem` (121 Mozilla roots, SHA-256 checked
  against curl's published sum), and `check_release.py` regenerates
  and diffs it exactly as it does the notices — the array cannot
  drift from the PEM. Same guarantee, one Python dependency.
* **The flag rides `NsOptions.use_tls` for now** — the session-level
  carrier the tests drive directly. The config field, every
  frontend's checkbox, and the sourcetable fetch's own flag are L4,
  as the step order always said.
* **The specific certificate sentence travels as an out-parameter**
  (`why`) from the transport, and the session prefers it over the
  vocabulary's generic one — expired, not-yet-valid (naming the
  device clock), wrong host (naming the host asked for), untrusted.
  `Failure.kt` and `strings.xml` gained codes 13 and 14 in this same
  commit, which is the parity check's demand being met, not watched.
* **Discovery, from the falsification**: removing
  `mbedtls_ssl_set_hostname` was supposed to redden the wrong-host
  case alone; it reddened *every* TLS case, because Mbed TLS 3.6
  refuses to verify a certificate at all when no hostname was set.
  The call the design named non-negotiable is one the library itself
  enforces — the sharper proof. Restored, 16/16.

The stall detector survives encryption by construction: mbedTLS reads
through a BIO that never blocks in the data phase, so the session's
own select stays in charge of time. 104 release checks (the CA-array
diff joined `check_generated`); the app builds with the new codes;
the daemon's wildcard picked up the bundle without being asked.

*(As planned:)*

Handshake, chain verification against the embedded bundle, explicit
hostname verification, clean close; the two failure codes classified
where the plaintext failures already are, with sentences; reconnection
repeats the handshake for free (the design note's observation — the
session already re-runs connect).

**Verify.** The loopback harness grows a TLS caster (same thread
pattern as `test_bridge_vrs`) using **committed test-only
certificates**: a good chain, an expired leaf, a wrong-hostname leaf,
a self-signed leaf — fixtures in `test/data/`, generated once, their
private keys worthless by construction. Four negative cases plus the
downgrade case (a plaintext server answered where TLS was demanded),
each classifying to the right code with the right sentence. These are
the tests `tls.md` calls the valuable ones.

### L4 — the flag reaches every frontend

Config field (array reader and legacy reader), the CLI flag, the GUI
checkbox, the app's settings checkbox (read-only during a run like
every connection field), the daemon's config, the eph-stream fields.
Port-443 suggestion in the UIs that have a port field.

**Verify.** A config file written by one frontend round-trips through
the others (the shared-config tests extend); the app checkbox reaches
`bridge_open` and the session.

### L5 — live proof

Kadaster's TLS caster on 443, the positive case the design names:
CLI `--check` over TLS, the app on the Huawei over TLS end to end
(check, watch, sourcetable, eph if offered). And one honest negative
live: the same caster demanded on a plaintext port classifies as
handshake failure, not as a hang.

### L6 — say so, everywhere

Matrix row 90 ⋯→● five columns; **no Features gate** — both editions,
so there is nothing to gate, and the gate-table check has nothing to
demand for once. The security review's F3 disclosure *narrows* but
does not retire (`tls.md`: many casters offer no TLS); the
password-in-clear sentence in the settings dialog becomes conditional
on the flag. Wiki: Getting-started and a security note; the
port conventions. Changelog under a new `[3.8.0]` when it cuts.
CA-bundle age check joins `check_release.py`. Play data-safety: the
listing's encryption-in-transit answer can improve — flagged for the
author, whose form it is.

## Out of scope, stated

- Client certificates, TLS for the *served* side (nothing is served),
  and proxy traversal.
- Retiring the F3 disclosure — TLS narrows it; cleartext casters keep
  it alive.
- Pro's Play submission itself: `release-to-play.md` owns that, and it
  begins the day 3.8.0 exists.

## Open, and worth the author's word before L1

**All four accepted by the author, 2026-08-25, before L1 began.**

1. mbedTLS, vendored (recommended above).
2. `build-gui.bat` retires (recommended above).
3. One embedded CA bundle everywhere (recommended above).
4. 3.8.0 as the shipping version, both editions same day (the design
   note's own rule; restated for the record).
