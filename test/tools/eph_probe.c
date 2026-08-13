/*
 * Exercises the Android bridge's orbit sources on the desktop.
 *
 * The bridge is plain C99 with no JNI, so what the phone runs can be
 * driven here against a real caster. Opened without an ephemeris
 * side-stream, this is exactly the free edition: whatever it manages to
 * place comes from the observation stream itself.
 *
 * It prints the `eph` block of the bridge's own snapshot document each
 * second -- `from_obs` counts ephemerides decoded off the observation
 * stream, `placeable` of `tracked` is what the sky view can draw:
 *
 *   eph_probe: t= 12.0 s  tracked=32 placeable=32 cached=54 from_obs=41
 *
 * Not part of `test_all`: it needs a caster, and the test suite is
 * deliberately free of both network and caster. Build and run it as
 * `docs/RUNBOOK.md` describes.
 *
 *   eph_probe <host> <port> <mountpoint> [user] [password] [seconds]
 */
#include "ntrip_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr,
                "usage: %s <host> <port> <mountpoint> "
                "[user] [password] [seconds]\n", argv[0]);
        return 2;
    }
    const char *host = argv[1];
    int         port = atoi(argv[2]);
    const char *mp   = argv[3];
    const char *user = argc > 4 ? argv[4] : "";
    const char *pass = argc > 5 ? argv[5] : "";
    double      secs = argc > 6 ? atof(argv[6]) : 60.0;

    /* Bordeaux, near enough for a network mountpoint to answer. The GGA
     * uplink follows the sourcetable's nmea flag in the app; here it is
     * simply on, since a VRS answers nothing without one. */
    NtripBridge *b = bridge_open(host, port, mp, user, pass,
                                 44.837789, -0.579180, true, false);
    if (!b) { fprintf(stderr, "bridge_open failed\n"); return 1; }

    double t = 0.0, next_report = 1.0;
    while (t < secs) {
        bridge_pump(b, 100, t);
        t += 0.1;

        if (t < next_report) continue;
        next_report += 1.0;

        /* Read the same document the app parses, so the probe cannot
         * report something the phone would not. */
        char json[16384];
        if (bridge_snapshot_json(b, json, sizeof(json)) < 0) continue;
        const char *e = strstr(json, "\"eph\":");
        printf("eph_probe: t=%5.1f s  %.*s\n", t,
               e ? (int)strcspn(e, "}") + 1 : 0, e ? e : "");
        fflush(stdout);
    }

    int tracked = 0;
    int placeable = bridge_placeable(b, &tracked);
    /* Messages, not satellites, on the right: each ephemeris is
     * rebroadcast every few seconds, so the count runs well past the
     * number of orbits cached. */
    printf("eph_probe: %d of %d tracked satellites placeable; "
           "%d orbits cached, %d ephemerides off the observation stream\n",
           placeable, tracked, bridge_eph_count(b), bridge_obs_eph(b));

    /* The image the app would show. Written only when there is something
     * to draw, which is the whole question being asked. */
    unsigned char *rgb = (unsigned char *)malloc(400 * 400 * 3);
    if (rgb) {
        bool drawn = bridge_sky_rgb(b, rgb, 400, 400);
        printf("eph_probe: sky view %s\n",
               drawn ? "drawn" : "EMPTY -- nothing could be placed");
        free(rgb);
    }

    bridge_close(b);
    return 0;
}
