# NTRIP over TLS — design note

**Decided 2026-08-13.** TLS will be added, using a **bundled TLS library
behind a transport abstraction in the session layer**, scheduled
**after the free edition ships**.

## Why, given that the industry does not

Cleartext credentials are the prevailing practice in this field. NTRIP
authenticates with HTTP Basic — base64, an encoding, not encryption —
and most public casters offer nothing else. Following the field would
mean doing nothing.

The decision is on principle rather than on prevalence: a paid tool
that holds several casters' credentials and is carried onto site
networks should not send them where anything on the path can read them.
Prevailing practice is a description of what exists, not a standard to
meet.

The exposure concentrates in the paid edition. Free sends credentials
only if the user configures a caster that needs them, and its shipped
example is anonymous; pro stores a set of connections and is carried
onto site networks. That is what makes pro the edition the work is
scheduled *for* — not the edition it is limited *to*: see the decision
on editions below.

## Why a bundled library rather than each platform's own

The alternative was Schannel on Windows, OpenSSL on Linux and a JNI
bridge to Java TLS on Android: no CA maintenance, trust roots supplied
by each OS, and three separate implementations to keep correct.

It was rejected because it breaks the rule the whole project is built
on. All logic lives in `src/core` and `src/session`, in C, and the
Android app compiles those sources unchanged; a JNI TLS bridge would put
connection logic in Kotlin for one frontend only. One core, four
frontends, one code path — that principle decided this.

## What it touches

Measured, not estimated. The socket work is the small part:

| File | Sites |
|---|---|
| `src/session/ntrip_session.c` | `sock_connect()` (~25 lines), `sock_recv()` (~20 lines), four raw `send()`, five `closesocket()` |
| `src/net/ntrip_handler.c` | the sourcetable fetch: six socket calls |

An `ns_transport` indirection — connect, recv, send, close, with a
plaintext and a TLS implementation behind it — is half a day to a day.
Reconnection already re-runs connect, so the handshake repeats without
further work.

## What actually costs

**Trust roots.** TLS without certificate validation is theatre; a
man-in-the-middle still succeeds. Windows and Linux hand a trust store
to their native APIs, but **the NDK provides neither a TLS API nor a
trust store**, so on Android the library must either read the system CA
path or carry a bundled root list — and a bundled list is a maintenance
item as roots expire and appear. Hostname verification must be explicit;
libraries do not do it by default.

**`build-gui.bat` has to change.** It lists every source file by hand in
a single `gcc` command line. A TLS library is dozens of files and does
not fit that model, so this work forces a choice: link a prebuilt static
library, or retire `build-gui.bat` and let CMake be the only GUI build.
Decide that before starting, not halfway through.

**Four build systems**: desktop CMake, `build-gui.bat`, the Android NDK
build, and the UNIX daemon.

**Config, UI and documentation across four frontends**: a TLS flag in
the shared configuration format, controls in the GUI and the app, port
conventions, and the wiki.

**Negative tests are the valuable ones**: expired certificate, hostname
mismatch, self-signed, and a downgrade attempt. Kadaster's TLS caster on
port 443 provides the positive case, serving the same free streams as
port 2101.

**Estimate: 5–8 focused days**, plus roughly half a day a year to
refresh the CA bundle.

## What it does not fix

Many casters offer no TLS at all. Support helps where it exists and
narrows the warning added in the security review (F3) — it does not
retire it. The disclosure in the app and in the session log stays.

## Both editions — **security is not a compromise**

**Decided: as soon as TLS is available in pro, it is available in free.**

The work is scheduled for pro because that is where the exposure
concentrates, but the capability is not withheld from anyone. Two
reasons, and the second is the one that settles it:

- Once the transport lives in the shared core, free gets it for nothing.
  Gating it would be *effort spent* to make the free edition less safe.
- A security property is not a feature to sell. Every other line in
  `editions.md` withholds convenience — more saved connections, a
  continuous watch, tap-to-select. Withholding protection from users who
  did not pay is a different act, and not one this project makes.

It also keeps the edition rule intact: the paid edition shows **more**,
never **different**, and never *safer*.

## Sequence

After the free launch, as the first substantial pro feature. It does not
block that launch: free's exposure is limited to casters the user
configures with credentials, and that is disclosed where the user can
act on it.

See `docs/security-review.md` F3 for the finding, and
`docs/work-items/release-to-play.md` for where this sits in the plan.
