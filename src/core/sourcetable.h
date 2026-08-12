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
    /**
     * Format details: the advertised message types and rates, e.g.
     * `1005(10),1077(1),1087(1)`.  Long lists from a generous caster
     * are truncated rather than rejected.
     */
    char   format_details[256];
    char   nav_systems[64];  /**< "GPS+GLO+GAL+BDS"               */
    char   network[64];
    char   country[8];
    double latitude;         /**< degrees; 0 when absent          */
    double longitude;
    int    carrier;          /**< 0 none, 1 L1, 2 L1+L2           */
    bool   nmea;             /**< caster expects a GGA uplink     */
} SourcetableEntry;

/** @brief One advertised message type and the rate it promises. */
typedef struct {
    int    type;         /**< RTCM message number                      */
    double interval_s;   /**< advertised interval; 0 when unstated      */
} SourcetableType;

/**
 * @brief Parse a format-details field into advertised types.
 *
 * The field lists what a mountpoint promises to send, and how often:
 * `1005(10),1077(1),1087(1)` -- type 1005 every ten seconds, 1077 and
 * 1087 every second. A type with no parenthesised interval is promised
 * without a rate, which is a weaker promise but still a promise.
 *
 * @param details Format-details text, as @ref SourcetableEntry carries.
 * @param out     [out] Destination; NULL to count only.
 * @param max     Capacity of @p out.
 * @return Number parsed, or the total when @p out is NULL.
 */
int sourcetable_parse_types(const char *details,
                            SourcetableType *out, int max);

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
