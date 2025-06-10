#include <stdio.h>
#include <time.h>
#include "nmea_parser.h"

// Helper to calculate NMEA checksum
static unsigned char nmea_checksum(const char *sentence) {
    unsigned char c = 0;
    while (*sentence && *sentence != '*') {
        c ^= (unsigned char)(*sentence++);
    }
    return c;
}

void create_gngga_sentence(double latitude, double longitude, char *buffer) {
    // Get current UTC time
    time_t rawtime;
    struct tm *ptm;
    char timestr[16];
    time(&rawtime);
    ptm = gmtime(&rawtime);
    snprintf(timestr, sizeof(timestr), "%02d%02d%02d.00", ptm->tm_hour, ptm->tm_min, ptm->tm_sec);

    // Convert latitude to NMEA format
    char lat_hem = (latitude >= 0) ? 'N' : 'S';
    double lat_abs = latitude >= 0 ? latitude : -latitude;
    int lat_deg = (int)lat_abs;
    double lat_min = (lat_abs - lat_deg) * 60.0;

    // Convert longitude to NMEA format
    char lon_hem = (longitude >= 0) ? 'E' : 'W';
    double lon_abs = longitude >= 0 ? longitude : -longitude;
    int lon_deg = (int)lon_abs;
    double lon_min = (lon_abs - lon_deg) * 60.0;

    // Compose the sentence without checksum, Age of Differential Data is blank (two commas at the end)
    char sentence[100];
    snprintf(sentence, sizeof(sentence),
        "GNGGA,%s,%02d%07.4f,%c,%03d%07.4f,%c,1,08,1.0,1.5,M,0.0,M,,",
        timestr,
        lat_deg, lat_min, lat_hem,
        lon_deg, lon_min, lon_hem
    );

    // Calculate checksum
    unsigned char cksum = nmea_checksum(sentence);

    // Write final NMEA sentence to buffer
    snprintf(buffer, 128, "$%s*%02X\r\n", sentence, cksum);
}