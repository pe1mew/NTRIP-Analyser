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

### V2 — the bridge drives a VRS run  *(done 2026-08-24)*

Built as planned, with two additions the building surfaced:

* **`bridge_vrs_gate()`** -- the explicit way into the gate test,
  beside the CLI's automatic condition. `vrs_check.h`'s contract
  anticipates a frontend that enters the gate from a control, and the
  desktop test needs it too: a loopback serving junk frames cannot
  sustain eight KPIs, so the automatic entry -- four lines lifted from
  `cli_stream.c` -- is the one piece verified only in V3, on a live
  caster.
* **VRS mode turns `auto_reconnect` off and the GGA uplink on.** A
  check is a verdict on one connection; a silent reconnect would hand
  A5 a stream the gate test is waiting to see drop.

`test/test_bridge_vrs.c` proves the plumbing over a real socket: the
bridge compiles unmodified on the desktop (its logging is
`__ANDROID__`-guarded), the clock is the caller's, and the caster runs
on a thread because `bridge_open` blocks fetching the sourcetable while
the observation connection waits in the backlog. Falsified by removing
the per-pump `vrs_update`: red. 15 of 15 suite tests pass; both
editions and the NDK library rebuild; a normal run's document carries
no `vrs` object at all.

*(As planned:)*

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

### V3 — the panel  *(done 2026-08-24)*

Built as planned, with the registry's own decision honoured over the
plan's wording: the panel sits **between the eight checks and
Run-the-check**, the slot pro's `Registry.kt` had reserved for it in
writing. The enable condition became `settings.isComplete`, the same as
Run-the-check's -- the C side forces the uplink on in VRS mode, so a
separate GGA precondition would have gated the button on a thing the
run supplies itself.

**Verified live on the S23 against rfsee.net/RFSEE01** (not HANESE as
drafted -- the same class of service, and the caster the handset was
already configured for, under the author's standing authorisation).
The whole sequence, read off the screen: A1-A4 resolve to PASS, the
**automatic gate entry fires** at KPI-sustained + A4 -- the one V2
could not reach -- A5 watches, and 90 s later classifies *"Still
streaming past the deadline after GGA stopped: fixed base"*, the card
says **Service: not gated (fixed base?)**, and the run ends by itself
into Run again. A5 carries WARN amber, not FAIL red, which is the
distinction this feature exists to draw.

Not exercised on a device: the share section's text (code-reviewed
only), and the GATED branch live -- that needs a real VRS service, and
the loopback covers it (`test_bridge_vrs.c`).

*(As planned:)*

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

### V4 — the guards  *(done 2026-08-24)*

The gate row landed in V3, demanded by the existing check before the
commit could go green. What V4 added is `check_vrs_parity()` -- and the
planned assertion-count comparison changed shape when the code showed
there was no Kotlin count to compare: the panel draws however many
items arrive. What *does* cross the language boundary as a bare number
is the gate code, and Kotlin's `gateResolved` reads it as `gate >= 2`
-- true only while GATED and NOT_GATED sit third and fourth in the C
enum. So the check pins the enum's order, the Kotlin expression, the
matrix's "five further checks" against `VRS_ASSERT_COUNT`, and that
neither edition redefines a `vrs_` string. Both falsified red and
restored; 85 checks. Row 45 reads ○ free, ● pro.

*(As planned:)*

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
