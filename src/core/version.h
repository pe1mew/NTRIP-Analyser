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
 * MAJOR — a user-visible contract breaks: an executable or a CLI option
 *         renamed or removed, an incompatible config schema, a field
 *         removed or repurposed in the statistics snapshot, a renamed
 *         Munin field.
 * MINOR — new capability, backward compatible.
 * PATCH — fixes only, no new capability.
 *
 * **Bump this in the same commit as the change that earns it, not at
 * release time.**  v2.0.1 was tagged on a tree that still said 2.0.0
 * here, so binaries built from that tag report the wrong release.  The
 * `release` target now refuses to package when the git tag on HEAD and
 * this file disagree (cmake/CheckReleaseTag.cmake).
 */
#define NTRIP_VERSION_MAJOR   3
#define NTRIP_VERSION_MINOR   3
#define NTRIP_VERSION_PATCH   0

/** Human-readable version.  Keep in step with the three numbers above. */
#define NTRIP_VERSION_STRING  "3.3.0"

/** Comma-separated form required by the Win32 VERSIONINFO resource. */
#define NTRIP_VERSION_RC      3,3,0,0

/** Dotted four-part form for the Win32 resource string fields. */
#define NTRIP_VERSION_RC_STR  "3.3.0.0"

/**
 * Android requires a monotonically increasing integer independent of the
 * display name.  Derived as MAJOR*10000 + MINOR*100 + PATCH, which keeps
 * ordering correct while remaining readable: 3.0.0 is 30000, 3.1.3 is
 * 30103.  Minor and patch are therefore capped at 99.
 */
#define NTRIP_ANDROID_VERSION_CODE 30300

/* ── Product identity ─────────────────────────────────────────────── */
#define NTRIP_PRODUCT_NAME    "NTRIP-Analyser"
#define NTRIP_COMPANY_NAME    "Remko Welling, PE1MEW"

/* ── Artefact names ───────────────────────────────────────────────────
 * Used in banners and in the statistics snapshot, so a consumer can tell
 * which program produced a given record. */
#define NTRIP_ARTEFACT_CLI     "ntrip-analyser"
#define NTRIP_ARTEFACT_GUI     "ntrip-analyser-gui"
#define NTRIP_ARTEFACT_SERVICE "ntrip-monitord"
#define NTRIP_ARTEFACT_ANDROID "ntrip-analyser-android"

/**
 * Defensive default for a caller that reaches the session layer without
 * naming itself.  Deliberately not one of the names above: if this string
 * ever appears in a caster's log it means a front end forgot to set
 * `user_agent`, and it should be identifiable as exactly that rather than
 * quietly impersonating the CLI.
 *
 * Shared code that *is* reachable from several front ends -- the
 * sourcetable fetch, for instance -- takes the agent from its caller
 * instead, so each front end names itself truthfully.
 */
#define NTRIP_ARTEFACT_LIB     "ntrip-analyser-lib"

/**
 * @brief NTRIP User-Agent product token: `NTRIP <artefact>/<version>`.
 *
 * Composing it here rather than at each call site keeps every request the
 * project makes truthful about its version.  A caster operator reading a
 * connection log sees which artefact connected and which release built
 * it, which is the whole reason the header exists.
 *
 * @param artefact One of the NTRIP_ARTEFACT_* string literals above.
 *
 * @note Literal concatenation, so @p artefact must be a string literal
 *       and the result is a compile-time constant.
 */
#define NTRIP_USER_AGENT(artefact)  "NTRIP " artefact "/" NTRIP_VERSION_STRING

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
