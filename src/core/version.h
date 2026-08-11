/**
 * @file version.h
 * @brief Single source of truth for the NTRIP-Analyser version.
 *
 * Every artefact built from this repository reports the same version:
 * the CLI, the Windows GUI, the monitoring service and the Android app.
 * See design/architecture.md §7a for the scheme and the rules for
 * bumping each component.
 *
 * **Deliberately dependency-free.** This header contains preprocessor
 * definitions only -- no includes, no types -- so that it can be consumed
 * by C, by `windres` when compiling `gui/resource.rc`, and by build
 * tooling that parses it textually (CMake, Gradle).  Do not add an
 * `#include` here.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#ifndef NTRIP_VERSION_H
#define NTRIP_VERSION_H

/* ── Product version ──────────────────────────────────────────────────
 * One version for all artefacts.  They are built from one commit, so a
 * single number makes a bug report unambiguous: "2.0.0" identifies the
 * exact source of every binary in the release.
 *
 * MAJOR — a user-visible contract breaks: a CLI option removed or given
 *         a new meaning, an incompatible config schema, a field removed
 *         or repurposed in the statistics snapshot, a renamed Munin
 *         field.
 * MINOR — new capability, backward compatible.
 * PATCH — fixes only, no new capability.
 */
#define NTRIP_VERSION_MAJOR   2
#define NTRIP_VERSION_MINOR   0
#define NTRIP_VERSION_PATCH   0

/** Human-readable version.  Keep in step with the three numbers above. */
#define NTRIP_VERSION_STRING  "2.0.0"

/** Comma-separated form required by the Win32 VERSIONINFO resource. */
#define NTRIP_VERSION_RC      2,0,0,0

/** Dotted four-part form for the Win32 resource string fields. */
#define NTRIP_VERSION_RC_STR  "2.0.0.0"

/**
 * Android requires a monotonically increasing integer independent of the
 * display name.  Derived as MAJOR*10000 + MINOR*100 + PATCH, which keeps
 * ordering correct while remaining readable: 2.0.0 is 20000, 2.1.3 is
 * 20103.  Minor and patch are therefore capped at 99.
 */
#define NTRIP_ANDROID_VERSION_CODE 20000

/* ── Product identity ─────────────────────────────────────────────── */
#define NTRIP_PRODUCT_NAME    "NTRIP-Analyser"
#define NTRIP_COMPANY_NAME    "Remko Welling, PE1MEW"

/* ── Artefact names ───────────────────────────────────────────────────
 * Used in banners and in the statistics snapshot, so a consumer can tell
 * which program produced a given record. */
#define NTRIP_ARTEFACT_CLI     "ntripanalyse"
#define NTRIP_ARTEFACT_GUI     "ntrip-analyser-gui"
#define NTRIP_ARTEFACT_SERVICE "ntrip-monitord"
#define NTRIP_ARTEFACT_ANDROID "ntrip-analyser-android"

/* ── Contract versions ────────────────────────────────────────────────
 * These are versioned independently of the product, as plain integers,
 * because they outlive any single release: a Munin graph, an installed
 * phone build or an archived CSV file may be years older than the binary
 * reading it.  A consumer checks the integer, not the product version.
 *
 * NS_STATS_SCHEMA_VERSION lives in ns_stats.h, next to the struct it
 * describes.
 */

/** Layout of `config.json`.  Bump when a field is removed or repurposed. */
#define NTRIP_CONFIG_SCHEMA_VERSION 1

#endif /* NTRIP_VERSION_H */
