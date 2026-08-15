/**
 * @file cli_stream.h
 * @brief CLI stream modes, implemented on the session layer.
 *
 * These replace the per-mode connect/frame/decode loops that lived in
 * ntrip_handler.c -- five entry points that each carried a private copy
 * of the transport (design/architecture.md §1.1, §9 step 5).  Each mode
 * is now an event handler over the same NtripSession the GUI and the
 * monitoring service use.
 *
 * Output stays line-compatible with the old implementations except where
 * the old code was wrong: interval statistics are now epoch-based (the
 * legacy table could print an average below its own minimum), and frames
 * failing CRC no longer appear as message type "0" in the decode stream.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#ifndef CLI_STREAM_H
#define CLI_STREAM_H

#include <stdbool.h>
#include <stdint.h>
#include "net/ntrip_handler.h"       /* NTRIP_Config */
#include "session/ntrip_session.h"   /* NsOptions, NtripSession */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief `-d`: decode the stream, optionally filtered by message type.
 *
 * No filter: every message is fully decoded to stdout.  With a filter:
 * matching messages are decoded, all others print as their bare type
 * number.  Runs until the connection drops or the process is interrupted.
 *
 * @param config       Connection settings for the observation stream.
 * @param filter_list  Message types to decode fully; NULL for all.
 * @param filter_count Number of entries in @p filter_list.
 * @param debug        Print the caster's response header after login.
 */
int cli_stream_decode(const NTRIP_Config *config,
                      const int *filter_list, int filter_count,
                      bool debug);

/**
 * @brief `-t`: collect per-type statistics for @p seconds, print a table.
 */
int cli_analyze_types(const NTRIP_Config *config, int seconds);

/**
 * @brief `-s`: count unique satellites per GNSS for @p seconds.
 */
int cli_analyze_sats(const NTRIP_Config *config, int seconds);

/**
 * @brief Ephemeris side-stream for `--sky` (moved from ntrip_handler.c).
 *
 * Connects to the EPH_* mountpoint in @p config and feeds the ephemeris
 * cache.  Decoder text is swallowed into a string buffer, so a progress
 * line on the terminal stays intact.
 *
 * @param config    Connection settings; the EPH_* fields select the caster.
 * @param stop_flag Polled each iteration; non-zero stops the stream.
 * @param verbose   Log each cached ephemeris to stderr.
 * @return 0 on a clean stop, non-zero on a connection failure.
 */
int run_eph_stream(const NTRIP_Config *config,
                   const volatile int *stop_flag, bool verbose);

/**
 * @brief When true, stream modes reconnect with backoff after a drop.
 *
 * Set by the CLI's `--reconnect` flag before a mode runs.  Off by
 * default: a finite analysis (`-t 60`, `-s 120`) should fail loudly on a
 * drop rather than quietly extend its window, so the choice belongs to
 * the user.
 */
extern bool cli_auto_reconnect;

/**
 * @brief Exit status when the capture the run existed for did not happen.
 *
 * Defined here rather than beside main.c's other codes because the modes
 * in this file are what return it.  A cron job must be able to tell "the
 * disk filled" from "the caster refused", and when the artefact does not
 * exist, a station verdict is not the news -- so this overrides even
 * `--check`'s caution code.
 */
#define EXIT_CAPTURE_FAILED 7

/**
 * @brief `--capture <path>`: write CRC-valid frames there; NULL = off.
 */
extern const char *cli_capture_path;

/**
 * @brief `--capture-max <MB>`, in bytes; 0 = no limit.
 */
extern uint64_t cli_capture_max_bytes;

/**
 * @brief Copy the capture settings into @p opt.
 *
 * One place, so every mode that opens a stream captures on the same
 * terms, and a mode added later cannot quietly forget to.
 */
void cli_capture_apply(NsOptions *opt);

/**
 * @brief Report what the capture wrote; true when it failed.
 *
 * Prints to stderr even under `-q`: the file is the result of the run,
 * not chatter about it.  Call before @ref ns_close.
 */
bool cli_capture_finish(const NtripSession *s);

/**
 * @brief `--check` / `--check-vrs`: the station acceptance test.
 *
 * Runs the seven-KPI engine (core/kpi.h) against the stream until the
 * verdict resolves or ~3 minutes pass.  In VRS mode it also drives the
 * GGA cadence from the config's LATITUDE/LONGITUDE, applies the VRS
 * assertion set (core/vrs_check.h), and finishes with the gate test:
 * GGA stops and the stream's reaction classifies the service.
 *
 * @param config   Connection settings.
 * @param vrs_mode Add the VRS assertions and gate test.
 * @return Exit code: 0 STATION OK, 1 FAILED, 6 CAUTION.
 */
int cli_check(const NTRIP_Config *config, bool vrs_mode);

#ifdef __cplusplus
}
#endif

#endif /* CLI_STREAM_H */
