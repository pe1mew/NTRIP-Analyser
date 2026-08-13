/*
 * Exercises the Android bridge's GGA uplink on the desktop.
 *
 * The bridge is plain C99 with no JNI, so the code the phone runs can be
 * driven here against `stub_caster.py`: open with send_gga, pump for a
 * while, move the reported position halfway through, and let the stub
 * report what actually arrived on the socket. This is how the uplink is
 * verified at all -- public casters advertise no `nmea` mountpoint, so
 * there is nothing live to point it at.
 *
 * Not part of `test_all`: it needs a listener, and the test suite is
 * deliberately free of both network and caster. Build and run it as
 * `docs/RUNBOOK.md` describes.
 *
 *   gga_probe [host [port [mountpoint [send_gga]]]]
 */
#include "ntrip_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

int main(int argc, char **argv)
{
    const char *host = argc > 1 ? argv[1] : "127.0.0.1";
    int port = argc > 2 ? atoi(argv[2]) : 2101;
    const char *mp = argc > 3 ? argv[3] : "TEST";
    bool gga = argc > 4 ? atoi(argv[4]) != 0 : true;

    NtripBridge *b = bridge_open(host, port, mp, "", "",
                                 52.000000, 6.000000, gga, false);
    if (!b) { fprintf(stderr, "bridge_open failed\n"); return 1; }

    /* Twenty-six seconds covers three uplinks at the ten-second cadence,
     * with the position moving between the second and the third -- which
     * is what proves the sentence follows the rover rather than the
     * position the session was opened with. */
    double t = 0.0;
    int moved = 0;
    while (t < 26.0) {
        bridge_pump(b, 100, t);
        t += 0.1;
        if (!moved && t > 12.0) {
            moved = 1;
            printf("probe: moving the reported position to 51.5, 5.5\n");
            fflush(stdout);
            bridge_set_position(b, 51.500000, 5.500000);
        }
    }
    bridge_close(b);
    printf("probe: done\n");
    return 0;
}
