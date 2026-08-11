/**
 * @file nmea_parser.h
 * @brief NMEA 0183 sentence parsing API for NTRIP-Analyser.
 *
 * This header declares functions and types for parsing NMEA 0183 sentences,
 * such as GGA, RMC, and others, for use in GNSS and NTRIP applications.
 *
 * Project: NTRIP-Analyser
 * @author Remko Welling
 * @license Apache License 2.0 with Commons Clause (see LICENSE for details)
 *
 * ## Usage:
 *   - Use create_gngga_sentence() to generate a GNGGA NMEA sentence for a given latitude and longitude.
 *   - The output buffer must be at least 100 bytes.
 */

#ifndef NMEA_PARSER_H
#define NMEA_PARSER_H

/**
 * @brief Create a GNGGA NMEA sentence buffer for given latitude and longitude.
 *
 * Generates a valid NMEA 0183 GNGGA sentence using the provided latitude and longitude,
 * a fixed height of 1.5 meters above sea level, and fix quality 2.
 * The Age of Differential Data field will be left blank.
 *
 * @param latitude  Latitude in decimal degrees (positive = N, negative = S)
 * @param longitude Longitude in decimal degrees (positive = E, negative = W)
 * @param buffer [out] Output buffer for the NMEA sentence (must be at least 100 bytes)
 * @pre The buffer must be at least 100 bytes to hold the complete NMEA sentence.
 * @post The buffer will contain a valid GNGGA NMEA sentence formatted as per NMEA 0183.
 */
void create_gngga_sentence(double latitude, double longitude, char *buffer);

#endif // NMEA_PARSER_H