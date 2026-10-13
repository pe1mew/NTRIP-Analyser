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
    /**
     * STR field 12: 0 a single base, 1 a network solution, -1 when the
     * record leaves it out or puts anything else there. Kept as stated;
     * @ref sourcetable_kind decides how far to believe it.
     */
    int    solution;
} SourcetableEntry;

/** @brief What a mountpoint's sourcetable entry says it is. */
typedef enum {
    ST_KIND_UNKNOWN = 0,  /**< the entry does not say, or says too little */
    ST_KIND_FIXED   = 1,  /**< one physical base at a fixed position      */
    ST_KIND_NETWORK = 2,  /**< VRS, MAC, nearest-base: a service          */
    ST_KIND_PRODUCT = 3,  /**< no observations: ephemeris, SSR, ...       */
} SourcetableKind;

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
 * @brief Parse a NavSys field into a constellation bitmask.
 *
 * `GPS+GLO+GAL+BDS+QZS` becomes bits by core constellation id, so
 * `1 << 1` is GPS, `1 << 2` GLONASS and so on. This field, not the
 * 1005/1006 indicator bits, is what a mountpoint advertises: the
 * message carries flags for GPS, GLONASS and Galileo only and cannot
 * state BeiDou at all, so judging a station by it would fault every
 * BeiDou-capable base in existence.
 *
 * @param navsys Field text; NULL or empty yields 0.
 * @return Bitmask of advertised constellations.
 */
unsigned sourcetable_navsys_mask(const char *navsys);

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

/**
 * @brief Find a network-service keyword in sourcetable text.
 *
 * `VRS`, `MAC`, `NEAR`, `NRBY`, `FKP`, `IMAX` or `SSR`, case-insensitive,
 * as a whole word. `NRBY` is onocoy's name for its nearest-base services
 * (`NRBY_ADV`, added 2026-10-13). A plain substring test is not safe: "LINEAR" contains
 * "NEAR" and "MACHINE" contains "MAC", and reading a fixed base as a
 * virtual station *suppresses* the position checks -- it hides the very
 * fault they exist to find, so the error must not be made in that
 * direction. A match needs a non-alphanumeric character, or the end of
 * the string, on both sides, which still matches the real forms:
 * "VRS", "NET-VRS", "1077(1) MAC".
 *
 * Moved from the GUI's station classification, so that the GUI and the
 * mountpoint map read a sourcetable the same way.
 *
 * @param text Any field; NULL yields NULL.
 * @return The keyword matched, as a static string, or NULL.
 */
const char *sourcetable_network_keyword(const char *text);

/**
 * @brief Classify a mountpoint from its sourcetable entry alone.
 *
 * In order, the first that applies:
 *
 * 1. A network keyword in the format-details or network field --
 *    NETWORK. Operators name their services; this is the most specific
 *    statement an entry makes.
 * 2. A network keyword in the mountpoint name, where a trailing number
 *    is allowed: `NEAR`, `NEAR4`, `CORS_VRS` -- NETWORK.
 * 3. Message types are advertised and none of them comes from a
 *    station -- no observations (legacy 1001-1004, 1009-1012, or an
 *    MSM) and nothing describing one (1005-1008, 1033, 1230) --
 *    PRODUCT: an ephemeris or correction stream, not a base at all.
 *    The descriptive messages count because operators list them alone:
 *    `1005,1033` is a base that did not list its observations.
 * 4. The solution field says 0 -- FIXED.
 * 5. Otherwise UNKNOWN, including a solution field that says 1.
 *
 * Why the solution field counts for so little, and the `nmea` flag for
 * nothing, is measured: 8471 entries from seven casters, 2026-10-11
 * (design/work-items/mountpoint-map.md, M1). Every entry filled the
 * solution field; the 15 that said "network" were 10 ephemeris and SSR
 * products and 5 physical IGS stations, while two nearest-base services
 * said "single base". One caster sets `nmea` on all 769 of its bases.
 * 26 real bases listed only descriptive messages, which is why those
 * count as a station in rule 3.
 * No entry carried a keyword in the details or network field; two
 * carried one in the mountpoint name.
 *
 * This is what the *sourcetable* says. A live stream says more -- a
 * reference point that follows the rover is a network service whatever
 * its entry claims -- and the frontends that hold a stream weigh that
 * as well.
 *
 * @param e   Entry; NULL yields UNKNOWN.
 * @param why [out] Which evidence decided it, for display; may be NULL.
 * @param cap Capacity of @p why.
 * @return The kind.
 */
SourcetableKind sourcetable_kind(const SourcetableEntry *e,
                                 char *why, size_t cap);

/**
 * @brief Whether a sourcetable position can be drawn or measured from.
 *
 * Rejected, never clamped:
 * - not finite (the field said "nan" or "inf");
 * - latitude outside ±90 or longitude outside ±180;
 * - exactly 0,0 -- the caster convention for "not stated", not a
 *   station in the Gulf of Guinea.
 *
 * @param lat,lon Position as parsed, degrees.
 * @return true when the position can be used.
 */
bool sourcetable_pos_usable(double lat, double lon);

#ifdef __cplusplus
}
#endif

#endif /* SOURCETABLE_H */
