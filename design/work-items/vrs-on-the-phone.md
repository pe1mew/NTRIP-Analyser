# Network-RTK assertions on the phone — plan

Phase 2, item 2 (`design/guiV2rollout.md`: Tracks → **VRS** → hand-over →
export → tier 2 → TLS). **Pro only**, decided by the author 2026-08-24.

**What it is.** The eight checks grade a station; on a network-RTK
service they can *actively mislead* — a moving 1005 is correct VRS
operation, not a fault (`design/feature-matrix.md`). The core has held
the answer since backlog 2.4: five assertions and a gate test
(`src/core/vrs_check.h`), judged identically wherever they run. The CLI
runs them as `--check-vrs`. The phone — the thing a surveyor actually
carries to a VRS — does not.

## The engine is finished, and the CLI is the workflow to copy

`vrs_check.h` divides the labour: the **caller** owns the workflow
(open, send each GGA, decide when to enter the gate test), the
**engine** owns the verdicts. What the caller must do is exactly what
`cli_check()` in `src/cli/cli_stream.c:840` already does:

| Step | Rule (the CLI's, verbatim in behaviour) |
|---|---|
| GGA cadence | every 10 s, `vrs_note_gga()` after each accepted send |
| Judge | `vrs_update()` once per snapshot, beside `kpi_update()` |
| Enter the gate | when the KPIs have sustained **and** A4 has passed: stop GGA, `vrs_begin_gate_test()` |
| End | the gate resolves (GATED / NOT_GATED), or an assertion fails outright, or the 300 s ceiling |
| Report | the five rows beside the eight, and the service classification on the verdict line |

A5 is a **classification, not a pass/fail**: a fixed base that ignores
GGA never drops, and that is correct behaviour for what it is. The
report says *gated* or *not gated (fixed base?)* — never FAILED.

## Where the workflow lives: in the bridge

The bridge is already the caller in the engine's sense: it sends the
GGA (`bridge_pump`, `ntrip_bridge.c:400`), so **only the bridge knows
the moment a sentence was accepted by the socket** — the fact A1 and A2
are timed from. It already runs `kpi_update` per pump and publishes the
snapshot JSON the app decodes. The VRS run sits beside the KPI run:
`VrsRun`/`VrsReport` fields in `NtripBridge`, `vrs_note_gga` at the
send site, `vrs_update` in the pump, the CLI's gate-entry condition
verbatim.

Kotlin drives nothing and times nothing. It asks for a VRS-mode run,
and reads the report out of the document — the same division as the
eight checks, and the reason two frontends cannot disagree.

## Edition

`Features.HAS_VRS_CHECK` — true in pro, false in free. The control and
the results are a **panel in pro's registry only**: free's hub composes
nothing VRS at all, per "the list is the layout"
(`android/design/editions.md`). *More in Pro* names it; no greyed rows.

The gate — a paid frontend over a shared measurement — follows the
matrix's own rule: the paid edition withholds convenience, never
protection. The assertions exist in CLI for anyone; pro sells the
convenience of running them from a pocket.

## Steps

### V1 — a proof of the engine  *(done 2026-08-24)*

**Built without the loopback this step was named for.** Reading the
engine showed `vrs_update` takes a snapshot struct and the caller's
clock and touches nothing else -- so synthetic snapshots and a driven
clock test exactly the contract every frontend uses, the 60 s hold and
90 s gate cost nothing, and the session layer stays tested where it
already is (`test_stall`, `test_failure`). `test/test_vrs.c`: both
endings, every failure branch, the policy override as a living
assertion. 24 checks; 14 of 14 suite tests pass.

Writing it caught two contract facts worth knowing for V2: `complete`
is true once A1..A4 resolve *even if the gate was never entered* (the
header says so; a caller must not wait on `complete` to mean "gate
answered"), and the engine sees connection edges only through updates,
so the bridge must keep calling `vrs_update` per pump even when nothing
else changed.

*(As planned, superseded:)*

`vrs_check.c` has no test. `test/test_failure.c` already runs a
loopback caster that answers as told; a sibling `test_vrs.c` drives the
engine through both endings — a caster that drops after GGA stops
(GATED) and one that keeps streaming (NOT_GATED) — plus one outright
failure (no RTCM after GGA: A2). Registered in `CMakeLists.txt` desktop
**and** NDK lists, or documented as an omission — `check_release.py`
compares the two.

**Verify.** The tests go red when `VRS_RTCM_S` is halved in the test's
policy, green as committed. `verify:` tier, no network.

### V2 — the bridge drives a VRS run

`bridge_open(..., bool vrs_mode)` (or a `bridge_vrs_start()` beside it —
whichever reads better at the JNI glue). In VRS mode: note each
accepted GGA, update per pump, enter the gate by the CLI's condition,
and **stop the run** when the CLI would — the gate resolving is the end
of a VRS check, not a fault in it. The document gains a `"vrs"` object:
five results (verdict, value, label, detail — engine words, exactly as
the KPI rows travel), the gate state, `failed`, `complete`.

**Verify.** The loopback caster from V1, driven through the bridge's
own JSON: the object appears in VRS mode, is absent otherwise, and the
run ends with the gate's answer. Free's document is byte-identical to
today's.

### V3 — the panel

`VrsPanel` in **pro's `Registry.kt` only**: a "Network-RTK check ▶" row
under Run-the-check, enabled when a GGA position is configured (the
same condition the uplink itself needs). Running, it shows the five
rows styled as the eight are — engine verdicts, engine words — and the
gate line; finished, the classification stands where the verdict does.
The share socket gets its section from the panel, text first, like
every other panel.

The run is a **check**, not a watch: it ends when the gate answers.
Watch mode is untouched — a watch deliberately never stops its GGA.

**Verify.** On rfsee.net/HANESE (author authorised): the run reaches
the gate, and classifies. HANESE is a single base that wants GGA — the
expected answer is **not gated (fixed base?)**, which is the honest
result and proves A5's other branch on real hardware. Free: registry
diff shows no VRS entry; `checkEditionParity` passes.

### V4 — the guards

`HAS_VRS_CHECK` documented in `design/feature-matrix.md`'s gate table
(check_release.py caught `HAS_TRACKS` arriving undocumented; do not
feed it twice). Matrix row 45: Pro ⋯ → ●. A check that the Kotlin
panel's assertion count matches `VRS_ASSERT_COUNT`, in the fashion of
the failure-code parity check.

### V5 — say so

`changelog.md` under `[Unreleased]`; the wiki — a *Network-RTK check*
page (what the five assertions mean, what *gated* means, why a fixed
base saying NOT_GATED is not a failure), linked from The-eight-checks
and Pro-edition; `docs/cli.md` cross-reference if it names the app.

## Open, and worth an answer before V3

1. **Where the control sits.** A panel row under Run-the-check is the
   v3-conformant default (recommended). The alternative — a mode toggle
   inside the connection settings — hides a capability the listing
   sells.
2. **Live GGA during a VRS check.** Pro can uplink the phone's own
   fix. The CLI checks with the configured position; A3 judges against
   whatever was last sent, so either works. Recommendation: use exactly
   what the run's settings say, as the uplink already does — no special
   case.
3. **The two remaining desktop assertions** (station-ID sanity via
   1007/1033, the two-position shift test) need caller workflow no
   frontend has. Out of scope here; they stay in the header's "not yet
   asserted" note.
