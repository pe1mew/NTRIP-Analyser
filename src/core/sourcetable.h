/**
 * @file sourcetable.h
 * @brief Parse an NTRIP sourcetable into structured entries.
 *
 * `receive_mount_table()` returns the caster's sourcetable as raw text.
 * Turning its `STR;` lines into something a program can use existed only
 * inside the Win32 GUI, written straight into a ListView, so no other
 * frontend could reuse it.  This is that parsing with the presentation
 * removed.
 *
 * The record layout is NTRIP 1.0/2.0 §STR:
 *
 * ```
 * STR;mountpoint;identifier;format;format-details;carrier;nav-system;
 *     network;country;latitude;longitude;nmea;solution;generator;
 *     compression;authentication;fee;bitrate;misc
 * ```
 *
 * Fields beyond those this project uses are skipped rather than stored.
 *
 * Core module: no I/O, no platform headers, no allocation.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * @copyright Apache License 2.0 with Commons Clause (see LICENSE for details)
 */

#ifndef SOURCETABLE_H
#define SOURCETABLE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One `STR` record, with the fields this project reads. */
typedef struct {
    char   mountpoint[64];
    char   identifier[64];   /**< human-readable site name       */
    char   format[32];       /**< "RTCM 3.3", "RAW", ...          */
    char   nav_systems[64];  /**< "GPS+GLO+GAL+BDS"               */
    char   country[8];
    double latitude;         /**< degrees; 0 when absent          */
    double longitude;
    int    carrier;          /**< 0 none, 1 L1, 2 L1+L2           */
    bool   nmea;             /**< caster expects a GGA uplink     */
} SourcetableEntry;

/**
 * @brief Parse `STR` records out of a raw sourcetable.
 *
 * Lines that are not `STR;` records -- `CAS;`, `NET;`, the terminating
 * `ENDSOURCETABLE` -- are skipped.  Parsing is tolerant: a truncated
 * record fills what it can and keeps the rest empty, because a caster
 * with one malformed line should still yield a usable list.
 *
 * @param raw   Sourcetable text, NUL-terminated.
 * @param out   [out] Destination array; may be NULL to count only.
 * @param max   Capacity of @p out.
 * @return Number of records parsed, capped at @p max when @p out is
 *         given; the true total otherwise.
 */
int sourcetable_parse(const char *raw, SourcetableEntry *out, int max);

#ifdef __cplusplus
}
#endif

#endif /* SOURCETABLE_H */
