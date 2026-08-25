# Statistics export on the phone — plan

Phase 2, item 4 (`design/guiV2rollout.md`: Tracks → VRS → hand-over →
**export** → tier 2 → TLS). Matrix row 76: `◐ CLI | ● GUI | ⋯ Free |
⋯ Pro | ● Daemon` — the app is the only frontend without it.

**What it is.** The machine-readable counterpart of the shared report:
the numbers behind a run as a file another program eats — a
spreadsheet, a script, an archive. The share socket produces the prose
a person reads; this produces the data, and the socket's own design
named it as the committed second consumer back on 2026-08-18.

## The desktop's rules are the rules

`gui_events.c` (IDM_FILE_EXPORT_STATS) has done this since before the
app existed, and its comment states the law: exports go **through the
same serialisers the monitoring daemon uses, so an exported file and a
Munin sample describe a stream identically instead of in two dialects
that drift apart.**

| Rule | Value |
|---|---|
| JSON | the core snapshot, `ns_stats_to_json()` |
| CSV | `ns_stats_csv_header()` + one row of the current snapshot — a sample, not a log |
| Filename | `yyyymmddhhmmss_MOUNTPOINT_stats.json` — sorts by capture time, never proposes the same name twice |
| Truncation | refused: a serialiser return past the buffer is an error, never a file |
| No statistics yet | said plainly, nothing written |

## What the app exports

Two formats, two answers, one dialect:

- **JSON: the bridge's own document, verbatim.** Not a Kotlin
  re-encoding — the exact text the app itself decodes, whose `"stats"`
  object *is* `ns_stats_to_json()` output unchanged, embedded by
  `bridge_snapshot_json()`. A superset of the desktop's export (KPI
  verdicts, satellites, watch, VRS where present) that remains
  bit-compatible with the daemon's dialect where they overlap. The
  design's own answer (`guiV2rollout.md`: "a machine-readable export
  needs no per-panel work at all"), sharpened: exporting the received
  bytes beats re-encoding the decoded model, because a re-encoding can
  drift and bytes cannot.
- **CSV: the core's header and row, exactly** — the same columns as
  the GUI's export and the daemon's log, for the spreadsheet user the
  format exists for. CSV cannot carry the document's nesting and does
  not try.

**A time-series CSV log over a whole run is out of scope** — that is
session history (backlog 2.1), a different item with different storage
questions. This item is the desktop's export: the current snapshot, on
demand.

## Where the bytes come from, and when they are available

The bridge already produces both texts; the app currently keeps
neither:

- **E1** adds `bridge_stats_csv()` beside `bridge_snapshot_json()` —
  header, newline, row, truncation-refusing — plus its JNI.
- **E2** has the service keep the latest of both (`@Volatile` strings
  in the companion, beside the accumulators), captured at the same
  1 Hz publish so the two always describe the same instant. Kept after
  the run ends — an export usually happens *after* the measurement,
  when the bridge is already closed — and cleared when the next run
  starts, like every run-scoped record.

## Delivery: the Storage Access Framework, like the config

`CreateDocument` launchers already exist for the configuration
(`MainActivity.kt`); export uses the same mechanism, proposing the
desktop's timestamped filename. No new permission, no `FileProvider`
detour, and the user picks where it lands — Drive, Downloads, wherever
SAF reaches.

## Edition, and where the control sits

**Pro**, per `android/design/editions.md`'s own table ("Export
CSV/JSON, shareable report — planned [pro]") and the matrix's rule:
the measurement is visible in both editions; the *file* is
convenience. Free's *More in Pro* card already names "import and
export of the shared configuration file"; the wording grows to cover
statistics export.

The control is an **overflow menu row** — "Export statistics…" — gated
exactly as Load/Save configuration already are (`AppMenu` gates rows on
`Features.IS_PRO`; this one gates on `HAS_EXPORT`). Tapping it asks
JSON or CSV in a two-button dialog, then launches the SAF picker. Not
a hub panel: exporting is an action on the document, not a reading of
the station, and the menu is where the document's other file actions
live.

Greyed with the desktop's own sentence when there is nothing to
export yet — a menu row that vanishes is harder to find than one that
waits (the share control's rule).

## Steps

### E1 — the bridge writes CSV too  *(done 2026-08-25)*

`bridge_stats_csv()` as planned -- core header, newline, core row,
refusing truncation -- with its JNI beside the JSON one. Proven in the
desktop bridge harness: the first line equals `ns_stats_csv_header()`
byte for byte, the row fills every column the header names, and a
64-byte buffer is refused rather than half-written. Falsified by
corrupting the newline joint: red by name, restored, 15 of 15.

*(As planned:)*

`bridge_stats_csv(NtripBridge*, char*, size_t)`: core header + `\n` +
core row for the current snapshot, refusing truncation. JNI beside
`nativeSnapshotJson`. Extended into `test_bridge_vrs.c`'s harness
(the bridge already runs on the desktop there): the CSV's first line
equals `ns_stats_csv_header()`'s output byte for byte, and the row
parses to the same column count.

### E2 — the service keeps the latest texts  *(done 2026-08-25)*

As planned, with both texts captured inside the same `snapshotJson`
success block so they can never describe two different instants, and
the JNI symbol confirmed present in the packaged library rather than
assumed. Verification of the survival-past-run-end property is E3's
on-device export, which is the only honest reader of it.

*(As planned:)*

`lastStatsJson` / `lastStatsCsv` in the companion, written where the
document is published, cleared at run start, surviving run end.
Nothing decodes them; they are held for the file they will become.

### E3 — the menu row and the pickers

`HAS_EXPORT` (pro true, free false); the gated row; the format dialog;
two `CreateDocument` launchers (`application/json`, `text/csv`) with
the timestamped proposed name; a snackbar-style notice on success or
failure, as config save gives. Strings in `main/` so neither edition
redefines them.

**Verify.** On the S23: run a check on RFSEE01, export both formats to
Downloads, read the files back over adb — the JSON parses and its
`stats.mountpoint` is RFSEE01; the CSV's header matches the desktop's;
a fresh install with no run yet shows the row greyed. Free: the row is
absent and the *More in Pro* wording covers it.

### E4 — the guards

`HAS_EXPORT` documented in the matrix gate table (the check demands
it); row 76 Free ⋯ → ○, Pro ⋯ → ●; a parity line confirming neither
edition redefines an `export_` string, in the failure-codes fashion.

### E5 — say so

Changelog under `[Unreleased]`. Wiki: the **Watch mode** page's *"What
survives is in the app, not on the phone"* section currently ends at
"share a capture worth keeping" — it gains the export as the second
way out, which quietly strengthens that page's advice. Pro-edition and
comparison tables gain the row.

## Open, and worth an answer before E3

1. **Pro-only, as recommended?** The matrix's ⋯ in both columns
   genuinely left it open; `editions.md` planned it as pro, and this
   plan follows that. The counter-argument — the CLI exports for
   anyone — cuts less than it seems: the desktop tools are not
   editions, and free's report already shares every number as prose.
2. **Should the JSON carry a capture timestamp?** The bridge document
   has none (the desktop puts it in the filename). Recommendation:
   keep the desktop's answer — the filename carries the moment — and
   add nothing to the document for it.
3. **Export from the analysis screen too?** The share control there
   sends the plot as a picture; a second exporter per screen is scope
   creep. Recommendation: the menu row only, one place, like the
   desktop's single File menu entry.
