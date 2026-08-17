# service/

`ntrip-monitord`, a daemon that monitors NTRIP streams continuously and
publishes, per mountpoint, a statistics snapshot for Munin
(`<mountpoint>.json`) and a tier-2 stability report over a rolling window
(`<mountpoint>.report.json`) — plus its systemd unit, sysusers fragment
and the Munin plugin itself.

**The manual — building, installing, configuring, troubleshooting — is
[docs/service.md](../docs/service.md).** This file only tells you what
lives here.
