/**
 * @file thresholds.h
 * @brief User-supplied thresholds: one table, parsed, validated, printed.
 *
 * Every number that decides a verdict is a judgement rather than a fact
 * — `docs/thresholds.md` says which of them are well founded and which
 * are starting points — so a user must be able to disagree. A control
 * network and a hobby base are not held to the same standard, and
 * neither should inherit the other's numbers by accident.
 *
 * ### One table, not three lists
 *
 * Parsing, validating and printing a threshold are three jobs that must
 * agree about what thresholds exist. Written as three lists they drift:
 * a field gains a parser and no validation, or is loaded and never
 * shown. So there is **one table** (@ref ThField), and each job is a
 * loop over it. Adding a threshold is one row.
 *
 * That table is also what makes the promise testable: a release check
 * can assert that every threshold named in `docs/thresholds.md` appears
 * in `--thresholds-print`, so the page cannot drift from the code.
 *
 * ### The file
 *
 * @code
 * {
 *   "schema_version": 1,
 *   "name": "RFSEE domestic",
 *   "tier1": { "min_cnr_median": 38.0, "expect_sats": { "gps": 8 } },
 *   "tier2": { "reconnects_warn_per_h": 3.0, "sats_warn": 20 }
 * }
 * @endcode
 *
 * **Partial by design.** A file carries only what it changes; anything
 * absent keeps its built-in value. A complete file would rot the moment
 * a threshold was added, and would silently pin its author to the values
 * of whichever release they first wrote it under.
 *
 * ### Layer
 *
 * `src/core/` does no I/O: this parses **text**, and the caller owns the
 * file. Errors are written into a caller's buffer, never printed.
 *
 * See design/work-items/thresholds-track.md.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#ifndef THRESHOLDS_H
#define THRESHOLDS_H

#include <stdbool.h>
#include <stddef.h>

#include "core/kpi.h"
#include "core/station_report.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Schema version this build writes and understands. */
#define TH_SCHEMA_VERSION 1

/** Upper bound on the table; generous, and asserted against at build. */
#define TH_MAX_FIELDS 40

/** Longest policy name kept, including the NUL. */
#define TH_NAME_LEN 64

/** @brief Which tier a field belongs to, and so which struct it lives in. */
typedef enum {
    TH_TIER1 = 1,
    TH_TIER2 = 2,
} ThTier;

/** @brief A field's storage type. */
typedef enum {
    TH_DOUBLE = 0,
    TH_INT,
} ThType;

/**
 * @struct ThField
 * @brief One threshold: where it lives, what it may be, what it means.
 *
 * @c lo and @c hi are the range a *file* may set it to. They are not
 * thresholds about a station; they are the bounds outside which the
 * setting could not describe a measurement at all — a negative rate, a
 * percentage above a hundred, a window shorter than the evidence it
 * needs. A value outside them is **refused**, naming the field, rather
 * than clamped: a clamped value produces a verdict the user did not ask
 * for and cannot reproduce.
 */
typedef struct {
    const char *key;      /**< as it appears in the file                */
    ThTier      tier;
    ThType      type;
    size_t      offset;   /**< into KpiPolicy or SrPolicy               */
    double      lo, hi;   /**< accepted range, inclusive                */
    const char *unit;     /**< for printing; "" when a bare count       */
    int         decimals; /**< for printing                             */
    const char *what;     /**< one line, shown by --thresholds-print    */
} ThField;

/**
 * @struct Thresholds
 * @brief The effective policy, plus where each value came from.
 *
 * Provenance is not decoration. Once a verdict can be produced under a
 * non-default standard, "STATION OK" stops being comparable between two
 * users — so a program must be able to say, field by field, what it
 * used and whether that came from a file.
 */
typedef struct {
    KpiPolicy kpi;
    SrPolicy  sr;
    char      name[TH_NAME_LEN];   /**< from the file; "" when built-in */
    int       schema_version;
    bool      loaded;              /**< a file was applied              */
    bool      set[TH_MAX_FIELDS];  /**< per field, indexed as the table */
} Thresholds;

/** @brief The table. */
int             thresholds_field_count(void);
const ThField  *thresholds_field(int i);

/** @brief Built-in values, nothing overridden. */
void thresholds_defaults(Thresholds *t);

/**
 * @brief Apply a policy document onto @p t.
 *
 * @p t must already hold defaults (@ref thresholds_defaults): the file
 * is an overlay, so anything it does not mention is left alone.
 *
 * On failure @p t is **unchanged** — a half-applied policy is worse
 * than none, because the verdict it produces belongs to no stated
 * standard at all.
 *
 * @param err  Written with one sentence naming the field at fault.
 * @return true when the document was accepted entire.
 */
bool thresholds_parse(Thresholds *t, const char *json_text,
                      char *err, size_t err_cap);

/** @brief Read field @p i as a double, whatever its storage type. */
double thresholds_value(const Thresholds *t, int i);

/** @brief True when field @p i came from a file rather than the build. */
bool thresholds_is_set(const Thresholds *t, int i);

/**
 * @brief Short fingerprint of the effective values, e.g. "a4f19c2b".
 *
 * Two reports are comparable only if they were produced under the same
 * standard. The name says which policy a user *meant*; this says which
 * numbers were actually in force, so an edited file cannot masquerade
 * as the one it used to be.
 *
 * @param out At least 9 bytes.
 */
void thresholds_fingerprint(const Thresholds *t, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* THRESHOLDS_H */
