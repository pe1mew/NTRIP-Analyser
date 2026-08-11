# ntrip-monitord — the NTRIP monitoring service

`ntrip-monitord` watches one or more NTRIP mountpoints continuously and
publishes per-mountpoint statistics snapshots that a Munin plugin turns
into graphs: throughput, frame integrity, message rates, satellites,
C/N0 and availability. It is the tool for the question the interactive
applications cannot answer: *was this stream healthy last night?*

Everything here was exercised on a production deployment (Ubuntu 24.04,
munin-node 2.0); the pitfalls documented below were hit for real.

## How it works

```
ntrip-monitord ── holds one persistent session per mountpoint
      │
      │  every interval_s, writes atomically (tmp + rename)
      ▼
/var/lib/ntrip-monitor/<mountpoint>.json     ← one snapshot per mountpoint
      │
      │  read at munin-node's poll (every 5 min)
      ▼
munin plugin `ntrip_monitor` ── prints multigraph config/values
```

A daemon, not a self-contained plugin, for a reason: munin-node invokes
plugins for a few seconds every five minutes. Rates need a persistent
connection, and dropouts — the thing a stream monitor most needs to
catch — are invisible to a probe that connects briefly per poll.

The snapshot is single-line JSON in the project-wide statistics schema
(`src/core/ns_stats.h`, `schema_version` field). It is written to a
temporary file and `rename()`d into place, so a reader never sees a
half-written document. The same file is directly usable by anything
else: `cat`, `jq`, a cron job.

## Building

On the target machine (needs `build-essential`):

```sh
make -C service
```

The Makefile compiles the daemon against the same `src/core/`,
`src/net/` and `src/session/` sources as the CLI and GUI, so all
artefacts report the same version (`ntrip-monitord --version`).

## Installing

```sh
cd service
sudo make install
```

This installs, in order:

| What | Where |
|---|---|
| `ntrip-monitord` | `/usr/local/sbin/` |
| systemd unit | `/etc/systemd/system/ntrip-monitord.service` |
| Munin plugin | `/usr/local/share/munin/plugins/ntrip_monitor` |
| Example config | `/etc/ntrip-monitord/monitord.example.json` |
| sysusers fragment | `/usr/lib/sysusers.d/ntrip-monitord.conf` |
| State directory | `/var/lib/ntrip-monitor/` |

and runs `systemd-sysusers`, which creates the `ntrip-monitor` system
account the unit runs as.

**Why a static account and not `DynamicUser=`:** deployment proved
DynamicUser wrong for this service. systemd relocates a dynamic user's
state directory under `/var/lib/private/`, which is root-only — and the
entire point of the state directory is that the *munin* user reads it.
The unit therefore uses `User=ntrip-monitor` with
`StateDirectoryMode=0755` and `UMask=0022`, so snapshots come out
world-readable whatever the ambient umask.

## Configuring

```sh
sudo cp /etc/ntrip-monitord/monitord.example.json /etc/ntrip-monitord/monitord.json
sudo chown root:ntrip-monitor /etc/ntrip-monitord/monitord.json
sudo chmod 640 /etc/ntrip-monitord/monitord.json
sudo nano /etc/ntrip-monitord/monitord.json
```

The `chown`/`chmod` matter: the config holds caster passwords, so it
should be readable by the service group and nobody else.

```json
{
  "output_dir": "/var/lib/ntrip-monitor",
  "interval_s": 10,
  "mountpoints": [
    {
      "caster": "caster.example.net",
      "port": 2101,
      "mountpoint": "EXAMPLE1",
      "username": "user",
      "password": "password",
      "send_gga": false,
      "latitude": 52.0,
      "longitude": 6.0
    }
  ]
}
```

- `output_dir` must match the Munin plugin's `env.statedir` (both
  default to `/var/lib/ntrip-monitor`).
- `send_gga: true` enables a periodic GGA uplink at the configured
  position — required by VRS / network mountpoints, harmless elsewhere.
- Up to 16 mountpoints; the daemon round-robins them on one thread.

This is deliberately **not** the interactive tools' `config.json`: that
schema describes one connection, a monitor needs a list.

## Running

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now ntrip-monitord
journalctl -u ntrip-monitord -f      # logs go to stderr → journald
ls -la /var/lib/ntrip-monitor/       # one <mountpoint>.json per stream
```

The daemon reconnects with exponential backoff (5 s doubling to 60 s)
when a caster drops, and counts every reconnect in the snapshot.

For testing without systemd, `--oneshot` connects, waits a settling
window, publishes one snapshot per mountpoint and exits:

```sh
ntrip-monitord --config test.json --oneshot
```

## Wiring up Munin

```sh
sudo ln -s /usr/local/share/munin/plugins/ntrip_monitor /etc/munin/plugins/
sudo systemctl restart munin-node
```

Graphs appear under the **ntrip** category after two master poll cycles
(about ten minutes). Six graph families per mountpoint:

| Graph | Shows | Alert defaults |
|---|---|---|
| throughput | bytes/s | — |
| integrity | CRC errors, malformed frames, re-syncs (rates) | warning on any CRC error |
| satellites | satellites tracked | — |
| cnr | mean C/N0 | warning below 35 dB-Hz |
| availability | connected (0/1), reconnects | critical when not connected |
| msgrate | frames/s per RTCM type | — |

Plugin configuration, if the defaults need changing
(`/etc/munin/plugin-conf.d/ntrip`):

```
[ntrip_monitor]
env.statedir /var/lib/ntrip-monitor
env.stale    120
```

### Staleness is a feature

A snapshot older than `env.stale` seconds (default 120) is reported as
`U` — undefined — for every field. Reporting the last known values
instead would draw a flat, healthy-looking line while the daemon is
down, which is precisely the wrong failure mode for a monitor. If all
your ntrip graphs go blank at once, check the daemon first:
`systemctl status ntrip-monitord`.

Two further behaviours worth knowing:

- The plugin ignores JSON files without a `schema_version` field, so a
  stray file in the state directory cannot invent a graph family.
- Munin field names are a frozen contract. Munin has no version
  negotiation; renaming a field silently starts a new RRD and orphans
  the history.

### Satellites and C/N0

`sats_total` and `cnr_mean` report the satellites observed within the
last five seconds and their mean C/N0, tracked in the session layer so
the daemon, the GUI and Android all count the same way.

Two things worth knowing when reading those graphs:

- **The count is "currently in view", not "seen this session".** A
  satellite that sets drops out of the count. That is what makes the
  graph useful as a health signal: a base losing sky view shows a
  falling line rather than a permanently rising one.
- **C/N0 needs MSM7.** MSM4/5/6 carry no extended C/N0 field, so a base
  transmitting only those reports satellite counts with `cnr_mean` at
  zero. That is the stream's doing, not the daemon's. If you need the
  signal graph, configure the base to send MSM7 (1077/1087/1097/1127).

## Verifying the chain

Each stage can be checked independently, from producer to consumer:

```sh
systemctl is-active ntrip-monitord                    # daemon up?
ls -l --time-style=full-iso /var/lib/ntrip-monitor/   # snapshot fresh?
cat /var/lib/ntrip-monitor/<MP>.json | jq .connected  # content sane?
sudo munin-run ntrip_monitor                          # plugin output?
rrdtool lastupdate /var/lib/munin/*/[host]-ntrip_throughput_*-g.rrd
```

A graph that "does not update" usually turns out to be a zoom window
far wider than the data: RRDs created twenty minutes ago render as a
sliver at the right edge of a 30-hour view.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| Snapshots exist but Munin shows nothing | Plugin not symlinked, or `env.statedir` mismatch |
| All graphs blank (`U`) | Daemon stopped — the staleness guard is doing its job |
| `connected` is 0, `reconnects` climbing | Caster unreachable or credentials rejected; see `journalctl -u ntrip-monitord` |
| Snapshots under `/var/lib/private/` | The unit was started with an old `DynamicUser=` version; reinstall the unit, `systemctl daemon-reload`, remove the stale directories, restart |
| Plugin works via `munin-run` but not in graphs | munin *master* not polling this node; check `/var/log/munin/munin-update.log` |

## See also

- [Documentation index](readme.md)
- [`design/architecture.md`](../design/architecture.md) §5 — why the
  daemon+plugin shape was chosen
- `src/core/ns_stats.h` — the snapshot schema the JSON follows
