"""A minimal NTRIP caster stub that reports what a client uplinks.

Serves two connections, which is what one bridge run makes: the
sourcetable request the bridge sends at open (so KPI 8 has something to
compare against), and then the stream itself. On the stream it answers
ICY 200 OK, trickles a few bytes so the client believes the stream is
alive, and timestamps every line it receives -- which for a GGA-enabled
session is the uplink.
"""
import socket
import sys
import threading
import time

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 2101
RUN_S = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0

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

    def trickle(c=conn):
        while not stop.is_set():
            try:
                # Not RTCM, and not meant to be: the client only needs
                # the socket to stay alive for the uplink to keep going.
                c.sendall(b'\x00' * 8)
            except OSError:
                return
            time.sleep(1.0)

    threading.Thread(target=trickle, daemon=True).start()

    conn.settimeout(1.0)
    buf = b''
    while time.time() < deadline:
        try:
            data = conn.recv(1024)
        except socket.timeout:
            continue
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
