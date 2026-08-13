# JSON configuration files

The project uses **two** JSON configuration formats. They are not
variants of one another and they are not interchangeable:

| Format | Describes | Read by | Written by |
|---|---|---|---|
| **Single connection** (`config.json`) | One caster, one mountpoint, one optional ephemeris stream | CLI, Windows GUI, Android (pro) | GUI, Android (pro), `ntrip-analyser -g` |
| **Multiple connections** (`monitord.json`) | A list of connections, monitored together, plus where to write results | `ntrip-monitord` | Hand-edited; Android (pro) export |

The rule of thumb: **one thing being analysed interactively → the single
format. Several things being watched unattended → the multi format.**

---

## ⚠ Passwords are stored in the clear

**Both formats hold caster passwords as plain text, and both are written
as plain text.** There is no encryption, no obfuscation and no key
derivation anywhere in either file. A `config.json` in a Downloads
folder, attached to a support e-mail, committed to a repository or synced
to a cloud drive hands over every credential it contains.

This is a deliberate property of an *exchange* format — the file has to
be readable by a text editor, by four programs on three platforms, and by
a person diagnosing a problem — but it means the file must be handled
like a password file, because it is one:

- **Do not commit one to version control.** This repository ignores
  `*.json` outright, allowing back only the two placeholder examples
  (`bin/exampleConfig.json` and `service/monitord.example.json`) — so a
  real config cannot be committed here by accident. Any other repository
  you copy one into has no such protection.
- **Do not attach one to a bug report.** Replace the password with
  `xxx` first. Nobody diagnosing a stream needs it.
- **On a server**, `monitord.json` belongs in `/etc/ntrip-monitord/`
  owned by root with mode `0600`, not in a home directory.
- **On Android**, what the app *stores* is separate from what it
  *exports*: settings live in the app's private storage, and the pro
  edition encrypts credentials at rest there. Exporting writes a plain
  file, at which point the warnings above apply again.

Casters issue per-user registrations, so a leaked password is one
account's problem rather than a network's — but it is still an account
someone signed up for, sometimes a paid one.

---

## 1. Single connection — `config.json`

One caster and one mountpoint, optionally with a second connection for
ephemerides so the sky plot can place satellites.

### Where it is used

- **CLI** — read from the working directory at startup; `-c/--config`
  points elsewhere, `-g/--generate` writes a template. Every field can be
  overridden by a command-line flag or an environment variable, which is
  the supported way to keep the password out of the file:

  ```sh
  NTRIP_PASSWORD=$SECRET ntrip-analyser -m
  ```

- **Windows GUI** — *File → Load Configuration* and *Save
  Configuration*; *Generate Template Config* writes one with placeholder
  values.
- **Android, pro edition only** — the menu's load and save actions read
  and write this exact format, field for field, so a configuration made
  on a desktop opens on the phone and back again. The free edition has
  neither action: it holds the one connection it is typed into.

### Fields

```json
{
  "NTRIP_CASTER":   "ntrip.kadaster.nl",
  "NTRIP_PORT":     2101,
  "MOUNTPOINT":     "APEL00NLD0",
  "USERNAME":       "ntrip-analyser",
  "PASSWORD":       "password",
  "LATITUDE":       52.230481,
  "LONGITUDE":      5.942016,

  "EPH_CASTER":     "ntrip.kadaster.nl",
  "EPH_PORT":       2101,
  "EPH_MOUNTPOINT": "BCEP00KAD0",
  "EPH_USERNAME":   "ntrip-analyser",
  "EPH_PASSWORD":   "password"
}
```

| Key | Type | Meaning |
|---|---|---|
| `NTRIP_CASTER` | string | Caster hostname |
| `NTRIP_PORT` | number | Caster port, usually 2101 |
| `MOUNTPOINT` | string | Mountpoint to open |
| `USERNAME`, `PASSWORD` | string | Caster credentials, **in the clear** |
| `LATITUDE`, `LONGITUDE` | number | Position sent in the GGA uplink, degrees. Required by network (VRS) mountpoints, ignored by a single base |
| `EPH_*` | string / number | A second connection carrying broadcast ephemerides (RTCM 1019/1020/1042/1044/1045/1046). Leave the block out, or leave `EPH_CASTER` empty, to disable it |

**Missing keys are not fatal.** A key that is absent or misspelt leaves
its field empty rather than terminating the program — one typo used to
take the whole run down. What that means in practice is that a silently
empty caster is possible, so validate before trusting a file:

```sh
ntrip-analyser --check-config
```

which applies the overrides, resolves the caster by DNS, prints what it
would use, and exits.

A ready-to-edit example ships as
[`bin/exampleConfig.json`](../bin/exampleConfig.json), targeting the
Dutch Kadaster open caster with its ephemeris stream. Substitute your own
free registration for the placeholder credentials.

---

## 2. Multiple connections — `monitord.json`

A list of connections watched continuously and unattended, with one
statistics snapshot written per mountpoint per interval.

### Where it is used

- **`ntrip-monitord`** (Linux service) — reads
  `/etc/ntrip-monitord/monitord.json`, opens every listed mountpoint at
  once, and writes `<output_dir>/<mountpoint>.json` every `interval_s`
  seconds for the Munin plugin to graph. See
  [service.md](service.md).
- **Android, pro edition** — exports its saved connection profiles in
  this shape, so a set configured in the field drops straight into a
  server's monitoring configuration. The phone does *not* run several
  connections at once; the format is the handover, not the behaviour.

### Fields

```json
{
  "output_dir": "/var/lib/ntrip-monitor",
  "interval_s": 10,
  "mountpoints": [
    {
      "caster":     "rfsee.net",
      "port":       2101,
      "mountpoint": "RFSEE01",
      "username":   "user",
      "password":   "password",
      "send_gga":   false,
      "latitude":   52.0,
      "longitude":  6.0
    }
  ]
}
```

| Key | Type | Meaning |
|---|---|---|
| `output_dir` | string | Where snapshots are written. Must match the Munin plugin's `env.statedir` |
| `interval_s` | number | Seconds between snapshot writes |
| `mountpoints[]` | array | One entry per connection; each is opened and monitored |
| `mountpoints[].caster`, `.port`, `.mountpoint` | string / number | What to connect to |
| `mountpoints[].username`, `.password` | string | Credentials, **in the clear** |
| `mountpoints[].send_gga` | boolean | Send a periodic GGA uplink. Required by network (VRS) mountpoints |
| `mountpoints[].latitude`, `.longitude` | number | Position for that uplink, degrees |

Note the **different key casing and naming** from the single format:
`caster` here, `NTRIP_CASTER` there. They are separate schemas read by
separate parsers; neither program will read the other's file.

The mountpoint name becomes a filename, so it is sanitised to
`[A-Za-z0-9._-]` before use — a caster-supplied string never names a path
verbatim.

---

## Which one do I want?

| I want to… | Format |
|---|---|
| Analyse one stream now, interactively | Single — `config.json` |
| Run `--check` on a station for sign-off | Single |
| Move a setup between desktop and phone | Single |
| Watch several stations continuously and graph them | Multi — `monitord.json` |
| Take a set of connections from the field to a server | Multi, exported from the Android pro edition |

---

## See also

- [cli.md](cli.md) — every flag, environment variable and exit code
- [gui.md](gui.md) — the Windows application
- [service.md](service.md) — `ntrip-monitord`, Munin, and deployment
