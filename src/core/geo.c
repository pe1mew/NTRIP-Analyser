/**
 * @file geo.c
 * @brief Distance and direction on a spherical Earth -- implementation.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * @copyright Apache License 2.0 with Commons Clause (see LICENSE for details)
 */

#include "core/geo.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DEG2RAD (M_PI / 180.0)

double geo_distance_m(double lat1, double lon1, double lat2, double lon2)
{
    double p1 = lat1 * DEG2RAD;
    double p2 = lat2 * DEG2RAD;
    double dp = (lat2 - lat1) * DEG2RAD;
    double dl = (lon2 - lon1) * DEG2RAD;
    double a  = sin(dp / 2) * sin(dp / 2) +
                cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
    /* Rounding can carry a just past 1 for a near-antipodal pair, and
     * sqrt(1 - a) is then NaN. */
    if (a > 1.0) a = 1.0;
    return 2.0 * GEO_EARTH_RADIUS_M * atan2(sqrt(a), sqrt(1.0 - a));
}

double geo_bearing_deg(double lat1, double lon1, double lat2, double lon2)
{
    double p1 = lat1 * DEG2RAD;
    double p2 = lat2 * DEG2RAD;
    double dl = (lon2 - lon1) * DEG2RAD;

    double y = sin(dl) * cos(p2);
    double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
    if (x == 0.0 && y == 0.0) return 0.0;

    double b = atan2(y, x) / DEG2RAD;
    if (b < 0.0) b += 360.0;
    if (b >= 360.0) b -= 360.0;   /* -0.0 + 360 rounds to 360 */
    return b;
}
