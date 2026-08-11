/**
 * @file ns_stats.h
 * @brief Point-in-time statistics snapshot for one NTRIP stream session.
 *
 * This is the shared contract between everything that observes a stream
 * and everything that reports on it:
 *
 *   - the monitoring service publishes it to Munin;
 *   - the Android app evaluates it as pass/fail KPIs;
 *   - the GUI exports it as JSON / CSV;
 *   - the session history is a *sequence* of these.
 *
 * Defining it once is the point.  See design/architecture.md §4.
 *
 * **Deliberately point-in-time.** A snapshot describes the session as of
 * one instant; it carries running totals and current rates, never a
 * series.  History is a ring of snapshots (or of a reduced form) held by
 * whoever wants it, which keeps this struct fixed-size and serialisable.
 *
 * **Layer rules** (design/architecture.md §2.2): this header and its
 * implementation are `src/core/` code.  They must not perform I/O, must
 * not call `printf`, and must not include a platform header, so that they
 * compile unchanged for Windows, Linux and the Android NDK.  Serialisers
 * therefore write into a caller-supplied buffer.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#ifndef NS_STATS_H
#define NS_STATS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Schema version, emitted in every serialisation.
 *
 * Munin graphs, installed phone builds and archived CSV files outlive any
 * one release, so consumers need to know what they are reading.  Bump on
 * any change that removes or repurposes a field; adding a field at the
 * end is backward compatible and does not require a bump.
 */
#define NS_STATS_SCHEMA_VERSION 1

/** Distinct RTCM message types carried in one snapshot.
 *
 * A busy multi-constellation stream runs to roughly twenty types
 * (MSM for up to seven constellations, plus 1005/1006/1007/1008/1013,
 * the ephemeris set, 1033 and 1230), and advertised-but-absent types
 * occupy slots too.  Overflow sets @ref NsStatsSnapshot::types_truncated
 * rather than being dropped silently. */
#define NS_MAX_TYPES 32

/** Constellations: GPS, GLONASS, Galileo, QZSS, BeiDou, SBAS, NavIC, spare. */
#define NS_MAX_GNSS  8

/** Field widths.  Fixed so the struct can be memcpy'd and ring-buffered. */
#define NS_MOUNTPOINT_LEN 64
#define NS_CASTER_LEN     128
#define NS_SOFTWARE_LEN   96

/** @brief Sentinel for an unavailable double-valued measurement. */
#define NS_UNSET (-1.0)

/** @brief How the connected mountpoint serves corrections. */
typedef enum {
    NS_STATION_UNKNOWN = 0,  /**< not enough evidence yet                */
    NS_STATION_FIXED   = 1,  /**< single physical base at a fixed ARP    */
    NS_STATION_VRS     = 2,  /**< VRS / MAC / nearest-base network       */
} NsStationType;

/** @brief Verdict comparing an advertised message type against observation. */
typedef enum {
    NS_VERDICT_UNKNOWN = 0,  /**< no sourcetable entry; cannot compare   */
    NS_VERDICT_OK      = 1,  /**< advertised and arriving at rate        */
    NS_VERDICT_MISSING = 2,  /**< advertised but never received          */
    NS_VERDICT_RATE    = 3,  /**< arriving, but off the advertised rate  */
    NS_VERDICT_EXTRA   = 4,  /**< received but not advertised            */
} NsVerdict;

/** @brief NTRIP protocol version the caster answered with. */
typedef enum {
    NS_NTRIP_UNKNOWN = 0,
    NS_NTRIP_V1      = 1,    /**< "ICY 200 OK" -- not HTTP at all        */
    NS_NTRIP_V2      = 2,    /**< "HTTP/1.x 200 OK"                      */
} NsNtripVersion;

/**
 * @struct NsTypeStats
 * @brief Per-RTCM-message-type accounting.
 *
 * Intervals are measured **per epoch, not per frame**.  MSM splits one
 * epoch across several frames when the observations do not fit in one, so
 * counting frames would report a correctly-behaving base as transmitting
 * at a multiple of its true rate.  @ref frames divided by @ref epochs is
 * how many frames one epoch takes.
 */
typedef struct {
    int      msg_type;            /**< RTCM message number               */
    uint64_t frames;              /**< frames received                   */
    uint64_t epochs;              /**< distinct epochs observed          */
    double   min_dt;              /**< shortest epoch interval, seconds  */
    double   max_dt;              /**< longest epoch interval, seconds   */
    double   avg_dt;              /**< mean epoch interval, seconds      */
    float    advertised_interval; /**< seconds; 0 = not advertised,
                                   *   -1 = advertised without a rate    */
    int      verdict;             /**< @ref NsVerdict                    */
} NsTypeStats;

/**
 * @struct NsGnssStats
 * @brief Per-constellation satellite and signal summary.
 */
typedef struct {
    int   gnss_id;        /**< 1=GPS 2=GLO 3=GAL 4=QZS 5=BDS 6=SBAS 7=NavIC */
    int   sats_tracked;   /**< satellites seen this epoch                   */
    float cnr_mean;       /**< dB-Hz; 0 = no C/N0 available                 */
    float cnr_median;
    float cnr_min;
    float cnr_max;
} NsGnssStats;

/**
 * @struct NsStatsSnapshot
 * @brief Everything known about one stream session at one instant.
 */
typedef struct {
    int      schema_version;      /**< @ref NS_STATS_SCHEMA_VERSION      */

    /* ── Identity ─────────────────────────────────────────────────── */
    char     mountpoint[NS_MOUNTPOINT_LEN];
    char     caster[NS_CASTER_LEN];
    double   t_start_unix;        /**< session start, seconds since epoch */
    double   uptime_s;            /**< since the session opened           */

    /* ── Connection ───────────────────────────────────────────────── */
    int      ntrip_version;       /**< @ref NsNtripVersion               */
    int      http_status;         /**< 200, 401, 404, ...; 0 if unknown  */
    char     caster_software[NS_SOFTWARE_LEN];  /**< the Server: header  */
    int      reconnects;          /**< reconnections this session        */
    bool     connected;           /**< true while the socket is up       */

    /* ── Volume and frame integrity ───────────────────────────────── */
    uint64_t bytes_total;
    double   bytes_per_s;         /**< over the most recent interval     */
    uint64_t frames_ok;           /**< CRC-24Q valid                     */
    uint64_t frames_crc_error;    /**< complete frames, CRC mismatch     */
    uint64_t frames_malformed;    /**< bad preamble or runt frame        */
    uint64_t framing_resyncs;     /**< implausible length; re-acquired   */
    double   crc_error_rate;      /**< share of frames checked, 0..1     */

    /* ── Per message type ─────────────────────────────────────────── */
    NsTypeStats types[NS_MAX_TYPES];
    int         n_types;
    bool        types_truncated;  /**< more types existed than fitted    */

    /* Roll-up of the advertised-versus-observed comparison. */
    bool     advertised_known;    /**< a sourcetable entry was available */
    int      advertised_count;    /**< types the sourcetable promises    */
    int      types_missing;       /**< advertised, never received        */
    int      types_offrate;       /**< arriving, but not at rate         */
    int      types_extra;         /**< received, not advertised          */

    /* ── Satellites and signal ────────────────────────────────────── */
    NsGnssStats gnss[NS_MAX_GNSS];
    int         n_gnss;
    int         sats_total;
    float       cnr_mean_all;     /**< dB-Hz across all tracked SVs      */

    /* ── Reference station ────────────────────────────────────────── */
    int      station_type;        /**< @ref NsStationType                */
    bool     arp_valid;           /**< a 1005/1006 has been received     */
    double   arp_lat, arp_lon, arp_alt;
    double   arp_drift_m;         /**< from the first ARP; NS_UNSET if none */
    int      arp_moves;           /**< distinct positions beyond a threshold */
    bool     sourcetable_pos_valid;
    double   sourcetable_offset_m;/**< declared vs broadcast; NS_UNSET if
                                   *   not comparable, e.g. on a VRS     */

    /* ── Timeliness ───────────────────────────────────────────────── */
    double   latency_s;           /**< newest MSM epoch vs system clock;
                                   *   NS_UNSET until computed           */

    /* ── Ionosphere ───────────────────────────────────────────────────
     * ROTI from dual-frequency MSM7 (see core/iono.h).  Additive fields:
     * existing JSON consumers read by key, so the schema version stays.
     */
    int      iono_verdict;        /**< IonoVerdict; 0 = unknown          */
    float    iono_roti_median;    /**< TECU/min; -1 until measurable     */
    float    iono_roti_max;       /**< worst satellite; -1 until known   */
    int      iono_sats_dualfreq;  /**< satellites with a usable pair     */
    int      iono_slips;          /**< arcs broken this session          */
} NsStatsSnapshot;

/**
 * @brief Zero a snapshot and stamp the schema version.
 *
 * Sets the double fields that use @ref NS_UNSET to that value rather than
 * zero, so "not measured" is never mistaken for "measured as zero" -- a
 * distinction that matters for drift and latency in particular.
 */
void ns_stats_init(NsStatsSnapshot *s);

/**
 * @brief Find or create the entry for @p msg_type.
 *
 * @return Pointer to the entry, or NULL if the table is full, in which
 *         case @ref NsStatsSnapshot::types_truncated is set.
 */
NsTypeStats *ns_stats_type(NsStatsSnapshot *s, int msg_type);

/**
 * @brief Find or create the entry for @p gnss_id.
 * @return Pointer to the entry, or NULL if the table is full.
 */
NsGnssStats *ns_stats_gnss(NsStatsSnapshot *s, int gnss_id);

/**
 * @brief Serialise a snapshot as a single-line JSON object.
 *
 * Writes at most @p cap bytes including the terminating NUL.  Strings are
 * escaped; non-finite doubles are emitted as JSON `null`, since JSON has
 * no NaN or Infinity and emitting either produces a document that strict
 * parsers reject.
 *
 * @return Bytes that *would* have been written excluding the NUL, in the
 *         manner of snprintf.  A value >= @p cap means the output was
 *         truncated and must not be used.  Negative on a NULL argument.
 */
int ns_stats_to_json(const NsStatsSnapshot *s, char *out, size_t cap);

/**
 * @brief Write the CSV header matching @ref ns_stats_to_csv_row.
 * @return As @ref ns_stats_to_json.
 */
int ns_stats_csv_header(char *out, size_t cap);

/**
 * @brief Serialise the scalar summary fields as one CSV row.
 *
 * Only the scalars: the per-type and per-GNSS tables are variable-length
 * and do not belong in a fixed row.  A snapshot appended per interval
 * gives a time series suitable for a spreadsheet.
 *
 * @return As @ref ns_stats_to_json.
 */
int ns_stats_to_csv_row(const NsStatsSnapshot *s, char *out, size_t cap);

/** @brief Human-readable name for a @ref NsVerdict, e.g. "missing". */
const char *ns_verdict_name(int verdict);

/** @brief Human-readable name for a @ref NsStationType, e.g. "vrs". */
const char *ns_station_type_name(int station_type);

#ifdef __cplusplus
}
#endif

#endif /* NS_STATS_H */
