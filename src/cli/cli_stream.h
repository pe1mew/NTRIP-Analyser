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
#include "net/ntrip_handler.h"   /* NTRIP_Config */

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
void cli_stream_decode(const NTRIP_Config *config,
                       const int *filter_list, int filter_count,
                       bool debug);

/**
 * @brief `-t`: collect per-type statistics for @p seconds, print a table.
 */
void cli_analyze_types(const NTRIP_Config *config, int seconds);

/**
 * @brief `-s`: count unique satellites per GNSS for @p seconds.
 */
void cli_analyze_sats(const NTRIP_Config *config, int seconds);

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

#ifdef __cplusplus
}
#endif

#endif /* CLI_STREAM_H */
