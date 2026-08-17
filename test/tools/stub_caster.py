"""A minimal NTRIP caster stub that reports what a client uplinks, and
can misbehave on request.

Serves two connections, which is what one bridge run makes: the
sourcetable request the bridge sends at open (so KPI 8 has something to
compare against), and then the stream itself. On the stream it answers
ICY 200 OK, trickles a few bytes so the client believes the stream is
alive, and timestamps every line it receives -- which for a GGA-enabled
session is the uplink.

    python stub_caster.py [port] [seconds] [capture.rtcm3] [mode] [feed_s]

The **mode** decides how the session ends, which is the other thing this
stub is for: KPI 1 has to tell four states apart and only one of them is
a healthy stream, so each has to be producible on demand. Reproducing
them against a public caster would mean deliberately evicting a real
station's session, which is why they are produced here instead.

    flow    (default) keep streaming until the run is over
    silent  answer ICY 200 OK and send nothing at all, ever
    stop    stream for `feed_s`, then go quiet with the socket still open
    drop    stream for `feed_s`, then close the session

`silent` sends nothing rather than the usual filler: a single byte would
make it the *stopped* case instead of the *never produced* one, which is
exactly the distinction being tested.
"""
import socket
import sys
import threading
import time

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 2101
RUN_S = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0

# Optional: a captured .rtcm3 to replay in a loop, at roughly the rate a
# base station delivers it. Without one the stub sends filler, which
# keeps a session alive but fails every format KPI -- fine for watching
# an uplink, useless for anything that has to reach a verdict.
REPLAY = sys.argv[3] if len(sys.argv) > 3 else None
REPLAY_BPS = 2000.0

MODE = sys.argv[4] if len(sys.argv) > 4 else 'flow'
FEED_S = float(sys.argv[5]) if len(sys.argv) > 5 else 15.0

if MODE not in ('flow', 'silent', 'stop', 'drop'):
    sys.exit('stub: unknown mode %r -- flow, silent, stop or drop' % MODE)

TABLE = (
    "STR;TEST;Stub;RTCM 3.3;1005(10),1077(1);2;GPS;STUB;NLD;"
    "52.00;6.00;1;0;stub;none;N;N;0;\r\n"
    "ENDSOURCETABLE\r\n"
)

srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(('127.0.0.1', PORT))
srv.listen(4)
srv.settimeout(RUN_S)
print('stub: listening on 127.0.0.1:%d' % PORT, flush=True)

deadline = time.time() + RUN_S

while time.time() < deadline:
    try:
        conn, addr = srv.accept()
    except socket.timeout:
        break

    req = conn.recv(4096).decode('latin-1')
    first = req.split('\r\n', 1)[0]
    print('stub: %s' % first, flush=True)

    path = first.split(' ')[1] if ' ' in first else '/'
    if path == '/':
        body = ('SOURCETABLE 200 OK\r\nContent-Type: text/plain\r\n'
                'Content-Length: %d\r\n\r\n%s' % (len(TABLE), TABLE))
        conn.sendall(body.encode('latin-1'))
        conn.close()
        continue

    conn.sendall(b'ICY 200 OK\r\n\r\n')
    t0 = time.time()
    stop = threading.Event()

    payload = None
    if REPLAY:
        with open(REPLAY, 'rb') as f:
            payload = f.read()
        print('stub: replaying %s (%d bytes) in a loop'
              % (REPLAY, len(payload)), flush=True)

    def trickle(c=conn):
        chunk = int(REPLAY_BPS / 4)
        pos = 0
        while not stop.is_set():
            if MODE != 'flow' and time.time() - t0 >= FEED_S:
                # The session has delivered what it was going to. What
                # happens next is the whole point of the mode.
                if MODE == 'drop':
                    print('stub: closing the session after %.0f s'
                          % (time.time() - t0), flush=True)
                    c.close()
                else:
                    print('stub: going quiet after %.0f s, socket open'
                          % (time.time() - t0), flush=True)
                return
            try:
                if payload:
                    end = pos + chunk
                    if end <= len(payload):
                        c.sendall(payload[pos:end])
                    else:
                        c.sendall(payload[pos:] + payload[:end - len(payload)])
                    pos = end % len(payload)
                    time.sleep(0.25)
                else:
                    # Not RTCM, and not meant to be: the client only
                    # needs the socket to stay alive for the uplink to
                    # keep going.
                    c.sendall(b'\x00' * 8)
                    time.sleep(1.0)
            except OSError:
                return

    if MODE == 'silent':
        print('stub: accepted, sending nothing', flush=True)
    else:
        threading.Thread(target=trickle, daemon=True).start()

    conn.settimeout(1.0)
    buf = b''
    while time.time() < deadline:
        try:
            data = conn.recv(1024)
        except socket.timeout:
            continue
        except OSError:
            break            # `drop` closed it from under us, on purpose
        if not data:
            break
        buf += data
        while b'\n' in buf:
            line, buf = buf.split(b'\n', 1)
            line = line.strip()
            if line:
                print('stub: %6.1f s  <= %s'
                      % (time.time() - t0, line.decode('latin-1')), flush=True)

    stop.set()
    conn.close()

srv.close()
print('stub: closed', flush=True)
