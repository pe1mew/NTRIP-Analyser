/**
 * @file sourcetable_map.h
 * @brief A sourcetable as map data, and the token that brings a choice
 *        back from the map.
 *
 * design/work-items/mountpoint-map.md, M2. The GUI writes a page that a
 * browser opens; the page cannot call the GUI, so a mountpoint chosen
 * on the map comes back as text on the clipboard (decision D2). Both
 * ends of that route live here, in the core, because both handle text
 * from outside the program -- a caster's sourcetable going out, and
 * whatever is on the clipboard coming back -- and the core is where
 * that can be tested.
 *
 * **Out: GeoJSON.** One FeatureCollection, one Point per mountpoint with
 * a usable position. Every string is escaped so that the output is safe
 * inside a `<script>` block as well as valid JSON: no `<`, `>` or `&`
 * appears literally, nor U+2028/U+2029, nor a control character. Text
 * that is not valid UTF-8 is replaced byte for byte by U+FFFD rather
 * than guessed at.
 *
 * **Back: a token.** `ntrip-analyser:mount=<caster>:<port>/<mountpoint>`
 * with the caster and mountpoint percent-encoded, so a `:` or `/` inside
 * either cannot be read as a separator. It carries an identifier, never
 * a value: the receiving frontend looks the name up in the sourcetable
 * it already holds and takes every figure from there.
 *
 * Core module: no I/O, no platform headers, no allocation.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * @copyright Apache License 2.0 with Commons Clause (see LICENSE for details)
 */

#ifndef SOURCETABLE_MAP_H
#define SOURCETABLE_MAP_H

#include <stdbool.h>
#include <stddef.h>
#include "core/sourcetable.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What every selection token starts with. */
#define ST_TOKEN_PREFIX "ntrip-analyser:mount="

/**
 * @brief Longest token accepted, in bytes. A real one is under 300;
 *        anything longer is refused unread.
 */
#define ST_TOKEN_MAX 1024

/** @brief Why @ref sourcetable_match_token selected nothing. */
typedef enum {
    ST_TOKEN_NONE         = -1,  /**< not a token at all: ordinary text, say nothing */
    ST_TOKEN_MALFORMED    = -2,  /**< starts like one, but cannot be read            */
    ST_TOKEN_OTHER_CASTER = -3,  /**< names a caster or port other than the loaded one */
    ST_TOKEN_NOT_LISTED   = -4,  /**< the loaded sourcetable has no such mountpoint  */
    ST_TOKEN_AMBIGUOUS    = -5,  /**< the sourcetable lists that name more than once */
} SourcetableTokenRefusal;

/**
 * @brief The word for a kind, as the map data carries it.
 *
 * @return "fixed", "network", "product" or "unknown".
 */
const char *sourcetable_kind_name(SourcetableKind kind);

/**
 * @brief Write the token that selects one mountpoint.
 *
 * Caster and mountpoint are percent-encoded: every byte outside
 * `A-Z a-z 0-9 - . _ ~` becomes `%XX`.
 *
 * @param caster     Caster host as configured.
 * @param port       Caster port.
 * @param mountpoint Mountpoint name as the sourcetable gives it.
 * @param buf        [out] Destination.
 * @param cap        Capacity of @p buf.
 * @return Length written, excluding the terminator; -1 when an argument
 *         is NULL or empty, the port is out of range, or the token does
 *         not fit -- in which case @p buf holds an empty string.
 */
int sourcetable_make_token(const char *caster, int port, const char *mountpoint,
                           char *buf, size_t cap);

/**
 * @brief Find the one entry a token selects.
 *
 * Accepted only when all three hold (mountpoint-map.md, D2): the caster
 * matches @p caster ignoring case and the port matches @p port; the
 * mountpoint is in @p entries, compared exactly; and exactly one entry
 * has that name. A duplicate is refused, never resolved to the first.
 *
 * Leading and trailing white space is ignored, since a copy may carry a
 * newline. Text that does not start with @ref ST_TOKEN_PREFIX is
 * ST_TOKEN_NONE -- the clipboard holds ordinary text most of the time,
 * and that is not an event.
 *
 * @param text    Clipboard text; need not be terminated.
 * @param len     Bytes in @p text. A NUL inside them makes the token
 *                malformed, as does a length over @ref ST_TOKEN_MAX.
 * @param caster  The caster whose sourcetable is loaded.
 * @param port    Its port.
 * @param entries The loaded sourcetable.
 * @param n       Entries in it.
 * @param matches [out] Entries with that name, for the log when
 *                ambiguous; may be NULL.
 * @return Index into @p entries, or a negative @ref SourcetableTokenRefusal.
 */
int sourcetable_match_token(const char *text, size_t len,
                            const char *caster, int port,
                            const SourcetableEntry *entries, int n,
                            int *matches);

/**
 * @brief Write a sourcetable as a GeoJSON FeatureCollection.
 *
 * One Point feature per entry whose position @ref sourcetable_pos_usable
 * accepts. Each feature's properties: `mountpoint`, `identifier`,
 * `network`, `country`, `format`, `nav_systems`, `nmea`, `kind`,
 * `kind_why`, `token`, and `distance_m` and `bearing_deg` from the
 * origin -- null when no origin is set.
 *
 * Top-level members beside the standard ones (RFC 7946 §6.1 allows
 * them): `caster`, `port`, `origin` (`[lon, lat]` or null), `total`
 * (entries given) and `omitted` (entries left out for want of a usable
 * position). An entry with no usable position is **counted, never
 * drawn**: placing it at 0,0 would be a station in the Gulf of Guinea.
 *
 * Sized like snprintf: call with @p buf NULL to learn the length. When
 * the output does not fit, @p buf holds an empty string -- never a
 * truncated document, which might still parse.
 *
 * @param entries    Sourcetable entries; NULL only when @p n is 0.
 * @param n          Number of entries.
 * @param caster     Caster host, for the header and the tokens.
 * @param port       Caster port.
 * @param origin_lat Origin for distance and bearing, degrees; a position
 *                   @ref sourcetable_pos_usable refuses (such as 0,0)
 *                   means "no origin".
 * @param origin_lon Origin longitude, degrees.
 * @param buf        [out] Destination, or NULL to measure.
 * @param cap        Capacity of @p buf.
 * @return Length of the complete document, excluding the terminator; it
 *         fits when this is less than @p cap. -1 on a NULL argument that
 *         is not allowed to be NULL, or if any piece could not be
 *         formatted whole -- the output is refused, never cut.
 */
long sourcetable_to_geojson(const SourcetableEntry *entries, int n,
                            const char *caster, int port,
                            double origin_lat, double origin_lon,
                            char *buf, size_t cap);

/* ── A tile source the user supplies (mountpoint-map.md, M3b) ────────── */

/** @brief Longest tile URL template accepted, in bytes. */
#define MAP_TILE_URL_MAX  1024
/** @brief Longest attribution text accepted, in bytes. */
#define MAP_TILE_ATTR_MAX 256

/**
 * @brief Whether a tile URL template can be handed to the map pages.
 *
 * The user types it, usually pasted from a provider's page with a key
 * in it (`...?api_key=...`). It must:
 *
 * - start with `https://` -- the key would otherwise travel in clear;
 * - contain `{z}`, `{x}` and `{y}`;
 * - use no other placeholder than those and `{s}` and `{r}`, the only
 *   ones Leaflet fills -- any other makes Leaflet throw on every tile;
 * - hold no white space, control character, quote, backslash, `<` or
 *   `>` -- none belongs in a URL, and refusing them here means the page
 *   never has to cope with them;
 * - not end with `=` -- the dialog offers a template ending in `?key=`
 *   for the key to be pasted after, and saved without one every tile
 *   would fail;
 * - be at most @ref MAP_TILE_URL_MAX bytes.
 *
 * @param url Template; NULL or empty is refused.
 * @param why [out] The first reason it was refused, for the dialog;
 *            may be NULL.
 * @param cap Capacity of @p why.
 * @return true when usable.
 */
bool map_tile_url_usable(const char *url, char *why, size_t cap);

/**
 * @brief A string as a JSON literal that is safe inside `<script>`.
 *
 * The same escaping as every string in @ref sourcetable_to_geojson, for
 * text the GUI puts into a page by itself -- the user's tile URL and
 * attribution. All or nothing, like that function.
 *
 * @param s   Text; NULL is written as `""`.
 * @param buf [out] Destination, or NULL to measure.
 * @param cap Capacity of @p buf.
 * @return Length of the literal including its quotes, excluding the
 *         terminator; it fits when this is less than @p cap.
 */
long map_json_quote(const char *s, char *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* SOURCETABLE_MAP_H */
