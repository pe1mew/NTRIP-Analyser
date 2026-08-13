# JSON configuration

The project has **one** configuration format. Every program in it —
the CLI, the Windows GUI, the monitoring daemon and the Android app —
reads and writes the same file, so a configuration written anywhere is
usable everywhere.

```json
{
  "output_dir": "/var/lib/ntrip-monitor",
  "interval_s": 10,
  "mountpoints": [
    {
      "name": "my station",
      "caster": "ntrip.kadaster.nl",
      "port": 2101,
      "mountpoint": "APEL00NLD0",
      "username": "ntrip-analyser",
      "password": "password",
      "send_gga": false,
      "latitude": 52.230481,
      "longitude": 5.942016,

      "eph_caster": "ntrip.kadaster.nl",
      "eph_port": 2101,
      "eph_mountpoint": "BCEP00KAD0",
      "eph_username": "ntrip-analyser",
      "eph_password": "password"
    }
  ]
}
```

The file is a **list**, even when it holds one connection. That single
decision is what lets a set configured on a phone in the field be
dropped onto a server, and a server's configuration be opened on a
phone.

---

## ⚠ Passwords are stored in the clear

**Caster passwords in this file are plain text.** There is no
encryption, no obfuscation and no key derivation. A copy in a Downloads
folder, attached to a support e-mail, committed to a repository or
synced to a cloud drive hands over every credential it contains.

That is unavoidable in an *exchange* format — the file has to be
readable by a text editor, by four programs on three platforms, and by
whoever is diagnosing a problem — but it means the file must be handled
like a password file, because it is one:

- **Do not commit one to version control.** This repository ignores
  `*.json` outright, allowing back only the two placeholder examples
  (`bin/exampleConfig.json` and `service/monitord.example.json`), so a
  real configuration cannot be committed here by accident. Any other
  repository you copy one into has no such protection.
- **Do not attach one to a bug report.** Replace the password with
  `xxx` first. Nobody diagnosing a stream needs it.
- **On a server**, the daemon's copy belongs in `/etc/ntrip-monitord/`
  owned by root with mode `0600`, not in a home directory.
- **On Android**, what the app *stores* is separate from what it
  *exports*: saved connections live encrypted in the app's private
  storage, and writing a file makes a plain-text copy, at which point
  the warnings above apply again.

Casters issue per-user registrations, so a leaked password is one
account's problem rather than a network's — but it is still an account
someone signed up for, sometimes a paid one.

---

## Fields

### Top level

| Key | Type | Meaning |
|---|---|---|
| `mountpoints` | array | The connections. Required; everything else is optional |
| `output_dir` | string | Where `ntrip-monitord` writes its snapshots. Must match the Munin plugin's `env.statedir`. Ignored by the other programs |
| `interval_s` | number | Seconds between the daemon's snapshot writes. Ignored by the other programs |

### Each entry of `mountpoints`

| Key | Type | Meaning |
|---|---|---|
| `caster` | string | Caster hostname |
| `port` | number | Caster port, usually 2101 |
| `mountpoint` | string | Mountpoint to open |
| `username`, `password` | string | Caster credentials, **in the clear** |
| `send_gga` | boolean | Send the periodic GGA uplink. Network (VRS) mountpoints require it; a single base ignores it |
| `latitude`, `longitude` | number | Position for that uplink, degrees |
| `name` | string | A label for people. Optional, and ignored by every parser that does not need it |
| `eph_caster`, `eph_port`, `eph_mountpoint`, `eph_username`, `eph_password` | string / number | **Optional.** A second connection carrying broadcast ephemerides (RTCM 1019/1020/1042/1044/1045/1046), which the sky plot needs to place satellites. Leave the block out to disable it |

A missing key leaves its field empty rather than terminating the
program — one typo used to take a whole run down. The cost is that a
silently empty caster is possible, so validate a file before trusting
it:

```sh
ntrip-analyser --check-config
```

which applies the command-line and environment overrides, resolves the
caster by DNS, prints what it would use, and exits.

---

## How many connections each program uses

| Program | Reads | Writes |
|---|---|---|
| **CLI** (`ntrip-analyser`) | the **first** entry; says so when there are more | `-g` writes a template of one |
| **Windows GUI** | the **first** entry; the log names how many were ignored | *Save Configuration* writes the one it is driving |
| **`ntrip-monitord`** | **every** entry, monitored simultaneously | — |
| **Android, pro edition** | **every** entry, merged into its saved connections | *Save configuration* writes them all |
| **Android, free edition** | the **first** entry — it saves one connection | — |

The analysers examine one stream at a time, so they take the first
entry and **say so**:

```
[CONFIG] fromphone.json lists 2 connections; using the first (HANESE) and ignoring the other 1.
```

A user who exported five connections and sees one is otherwise entitled
to think the file was truncated.

The Android pro edition merges rather than replacing: an entry naming a
caster, port and mountpoint it already has updates that connection, and
anything else is added while there is room. Loading a colleague's file
gains you a connection instead of costing you five.

---

## The older single-connection format

Every release before this format wrote a flat object with upper-case
keys:

```json
{
  "NTRIP_CASTER": "ntrip.kadaster.nl",
  "NTRIP_PORT": 2101,
  "MOUNTPOINT": "APEL00NLD0",
  "USERNAME": "ntrip-analyser",
  "PASSWORD": "password",
  "LATITUDE": 52.230481,
  "LONGITUDE": 5.942016,
  "EPH_CASTER": "ntrip.kadaster.nl",
  "EPH_PORT": 2101,
  "EPH_MOUNTPOINT": "BCEP00KAD0",
  "EPH_USERNAME": "ntrip-analyser",
  "EPH_PASSWORD": "password"
}
```

**Those files still work.** The CLI, the GUI and the Android app read
them as a single connection — they exist on disks, in support e-mails
and in released assets, and they still say exactly what they meant.
Nothing writes this layout any more; saving a configuration from any
program produces the list format above, which is how a file migrates.

---

## Which file do I want?

| I want to… | Do |
|---|---|
| Analyse one stream now, interactively | Any file; the first entry is used |
| Run `--check` on a station for sign-off | Any file |
| Move a setup between desktop and phone | Save from either; both read it |
| Watch several stations continuously and graph them | Put several entries in `mountpoints`, point `ntrip-monitord` at it |
| Take a set of connections from the field to a server | Save from the Android pro edition; copy to `/etc/ntrip-monitord/` |

A ready-to-edit example ships as
[`bin/exampleConfig.json`](../bin/exampleConfig.json), targeting the
Dutch Kadaster open caster with its ephemeris stream. Substitute your
own free registration for the placeholder credentials.

---

## See also

- [cli.md](cli.md) — every flag, environment variable and exit code
- [gui.md](gui.md) — the Windows application
- [service.md](service.md) — `ntrip-monitord`, Munin, and deployment
