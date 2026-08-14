# Runbook

How to build, test, deploy and extend NTRIP-Analyser. Paths and commands
here are the ones actually used on the author's machine; where they are
machine-specific it says so.

## Principles

- **One core, four programs.** A change under `src/core` reaches the CLI,
  the GUI, the daemon and the Android app. Rebuild and re-run every one
  of them before calling it done — core changes have sat unbuilt in the
  desktop programs for days because only the phone was being tested.
- **A clean build proves nothing.** This project measures live streams.
  Verify against a real caster and quote the numbers.
- **Two build systems describe the GUI.** CMake and `build-gui.bat` each
  list `gui/*.c` by hand. Adding a file to one and not the other breaks
  whichever the next person happens to use.
- **The device is part of the toolchain.** Android behaviour that matters
  — foreground service, GNSS, permissions — cannot be seen in a build.

## Local Development

### Prerequisites

| Need | Path |
|---|---|
| Host C compiler | `C:\Program Files\CodeBlocks\MinGW\bin` (gcc, windres, mingw32-make) |
| JDK 17 for Gradle | `C:\Program Files\Eclipse Adoptium\jdk-17.0.20.8-hotspot` |
| Android SDK / adb | `%LOCALAPPDATA%\Android\Sdk\platform-tools` |
| NDK | 27.0.12077973, pinned in `android/app/build.gradle.kts` |

The JDK is **Adoptium**, not the Microsoft build the name pattern
suggests. Gradle rejects a POSIX-style `JAVA_HOME`; give it the Windows
path.

### Desktop: build and test

```bash
export PATH="/c/Program Files/CodeBlocks/MinGW/bin:$PATH"
cmake -B build            # only after changing CMakeLists.txt
cmake --build build       # CLI + GUI; binaries land in bin/
cmake --build build --target test_all
```

`test_all` runs CTest with `--output-on-failure`. Tests link
`ntrip_core` only, so they need no network and no caster.

The GUI has a second, independent build:

```bash
./build-gui.bat           # windres + one gcc line, explicit source list
```

Binaries always land in `bin/`, never in the build tree — the programs
look for `config.json` in the working directory, and a clean rebuild
must not delete someone's configuration.

### Desktop: verify against a live caster

```bash
cd bin
./ntrip-analyser.exe --check-config      # DNS + resolved fields, no connection
./ntrip-analyser.exe --check             # ~90 s, exit 0 OK / 6 caution / 1 failed
./ntrip-analyser.exe -m                  # sourcetable
```

The GUI cannot be driven from a terminal. To exercise it: launch it,
fill the connection fields, *Connection → Open Stream*, then the window
under test. Its log window captures `stdout`/`stderr`, so `printf` from
core code is visible there.

### Android: build, install, observe

```powershell
$env:JAVA_HOME = 'C:\Program Files\Eclipse Adoptium\jdk-17.0.20.8-hotspot'
cd android
.\gradlew.bat assembleFreeDebug assembleProDebug
```

```bash
export PATH="$LOCALAPPDATA/Android/Sdk/platform-tools:$PATH"
adb install -r app/build/outputs/apk/pro/debug/app-pro-debug.apk
adb shell am start -n nl.pe1mew.ntripanalyser.pro/nl.pe1mew.ntripanalyser.MainActivity
adb logcat -c && adb logcat -d -s ntrip_android:V     # the app's own tag
adb exec-out screencap -p > shot.png                  # look at the result
```

Editions install side by side: `…ntripanalyser.free` and `….pro`, each
with its own sandbox and its own saved connections.

**Comparing the editions: same build, same mountpoint.** Install both
from one invocation, and point them at the *same* caster before
concluding anything. An apparent edition difference is usually neither:
a screen that looks wrong in free and right in pro has, in practice,
meant the two were pointed at stations with different message types
(`android/design/views.md`), or that one of them was still showing what
it accumulated before the change.

The build enforces the code half of that: `checkEditionParity` fails if
anything but `Features.kt` appears in `src/free` or `src/pro`, so the
two editions cannot quietly acquire separate implementations of the same
screen. It runs as part of every build; run it alone with
`.\gradlew.bat checkEditionParity`.

Inspecting a debug build's stored settings:

```bash
adb shell "run-as nl.pe1mew.ntripanalyser.pro ls shared_prefs/"
```

`caster_secure.xml` is `EncryptedSharedPreferences`; nothing readable
should appear in it. `caster.xml` is the pre-profiles file and must be
empty after a migration.

A station check takes ~90 s and the sustain window is 60 s, so allow
about 105 s before expecting a settled verdict.

### Verifying what a run *sends*, without a VRS

Public casters advertise no `nmea` mountpoint, so the GGA uplink cannot
be observed against one. `test/tools/` holds a stub caster that answers
the sourcetable request, accepts the stream request and timestamps every
line the client sends back, plus a probe that drives the **Android
bridge** — plain C99, no JNI, so the phone's own code runs here.

```bash
export PATH="/c/Program Files/CodeBlocks/MinGW/bin:$PATH"
gcc -std=c99 -I src -I android/app/src/main/cpp -I lib/cJSON \
    test/tools/gga_probe.c android/app/src/main/cpp/ntrip_bridge.c \
    -o bin/gga_probe.exe -L build -lntrip_session -lntrip_core -lcjson -lws2_32 -lm
```

```bash
python test/tools/stub_caster.py 2103 32 &      # port, seconds
./bin/gga_probe.exe 127.0.0.1 2103 TEST 1       # host, port, mountpoint, send_gga
```

The stub prints the uplink as it arrives, so the cadence, the first
sentence's timing and a mid-run position change are all visible:

    stub:    0.0 s  <= $GNGGA,110943.00,5200.0000,N,00600.0000,E,1,...
    stub:   20.0 s  <= $GNGGA,111003.00,5130.0000,N,00530.0000,E,1,...

With `send_gga` 0 the stub must print nothing at all — a mountpoint that
does not ask for a position must not be sent one.

### Verifying where a run's *orbits* come from

The same trick answers the sky view's question. `eph_probe` opens the
bridge with no ephemeris side-stream — which is exactly the free
edition — and prints the `eph` block of the bridge's own snapshot each
second, so what a phone would show is visible on a desktop:

```bash
export PATH="/c/Program Files/CodeBlocks/MinGW/bin:$PATH"
gcc -std=c99 -I src -I android/app/src/main/cpp -I lib/cJSON \
    test/tools/eph_probe.c android/app/src/main/cpp/ntrip_bridge.c \
    -o bin/eph_probe.exe -L build -lntrip_session -lntrip_core -lcjson -lws2_32 -lm
```

```bash
./bin/eph_probe.exe caster.centipede.fr 2101 NEAR centipede centipede 45
```

`from_obs` counts ephemerides decoded off the observation stream;
`placeable` of `tracked` is what the sky view can draw. On a station
that broadcasts its own orbits both climb and the last line reports the
sky view drawn:

    eph_probe: t= 24.0 s  "eph":{"tracked":38,"placeable":38,"cached":38,"from_obs":47,"age_s":0}
    eph_probe: 38 of 38 tracked satellites placeable; 38 orbits cached, 79 ephemerides off the observation stream
    eph_probe: sky view drawn

On a station that carries none, `from_obs` stays 0 and the sky view is
reported EMPTY — which is the case the ephemeris side-stream and the
imported navigation file exist for.

The orbit cache is process-wide, so an earlier `-R` load or another run
in the same process would flatter the result; the probe starts clean
each time for that reason.

### Android: release builds

```powershell
$env:JAVA_HOME = 'C:\Program Files\Eclipse Adoptium\jdk-17.0.20.8-hotspot'
cd android
.\gradlew.bat assembleFreeRelease assembleProRelease
```

Release builds are **minified** (R8, plus resource shrinking). What that
breaks is invisible in a debug build, so a release build is worth
installing and running before it matters:

```bash
adb install -r app/build/outputs/apk/pro/release/app-pro-release.apk
adb logcat -c && adb shell am start -n nl.pe1mew.ntripanalyser.pro/nl.pe1mew.ntripanalyser.MainActivity
adb logcat -d -s ntrip_android:V AndroidRuntime:E
```

Two failures to watch for, both release-only: an `UnsatisfiedLinkError`
on Start (a renamed JNI entry point) and a screen that stays at READY
while a run works (a stripped serializer, so no document decodes).
`app/proguard-rules.pro` says which rule guards what.

**Play takes a bundle, not an APK.** `assembleFreeRelease` produces an
APK, which is right for the handset, for the Samsung Galaxy Store and
for a self-hosted F-Droid repository — and which Play refuses for a new
app. The artefact to upload is:

```powershell
cd android
.\gradlew.bat bundleFreeRelease     # app/build/outputs/bundle/freeRelease/*.aab
```

Play then generates per-device APKs from it, which is why the bundle is
worth *looking inside* before uploading: this app carries a JNI library,
and a bundle missing an ABI installs on nothing without ever failing a
build.

```bash
python - <<'EOF'
import zipfile
z = zipfile.ZipFile('android/app/build/outputs/bundle/freeRelease/app-free-release.aab')
print([n for n in z.namelist() if n.endswith('.so')])
EOF
```

Expect `libntrip_android.so` under **both** `base/lib/arm64-v8a/` and
`base/lib/x86_64/`. The build is 64-bit only by choice
(`abiFilters` in `app/build.gradle.kts`), so a 32-bit-only ARM phone is
offered the app by nobody — it is not a crash, it is an absence.

**Toolchain, as of 2026-08-14.** AGP 8.11.2 on Gradle 8.13, compiling
and targeting **API 36**, NDK 27.0.12077973. The SDK needs
`platforms;android-36` and `build-tools;36.0.0` installed; AGP 8.7.3
built against 36 but warned it was untested, which is not a thing to
ship on. Play refuses new apps below API 36 from **31 August 2026**.

**16 KB memory pages.** Devices launching with Android 15 may use 16 KB
pages, and a shared library laid out for 4 KB ones does not load there —
the app fails to start rather than misbehaving. Play requires support of
every app targeting Android 15+. `src/main/cpp/CMakeLists.txt` passes
`-Wl,-z,max-page-size=16384`; verify it survived a toolchain change:

```bash
NDK=/c/Users/drasv/AppData/Local/Android/Sdk/ndk/27.0.12077973
RE=$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe
"$RE" -l android/app/build/intermediates/cxx/RelWithDebInfo/*/obj/arm64-v8a/libntrip_android.so | grep LOAD
```

Every LOAD segment must end in **`0x4000`**. `0x1000` is the 4 KB layout
that will not load on a 16 KB device — it was the default here until
2026-08-14.

**Signing.** `android/keystore.properties` is git-ignored and absent
from a fresh clone, so Gradle falls back to the debug key and says so at
configuration time. That artefact runs but cannot be uploaded — Play
rejects it. `android/keystore.properties.example` has the `keytool`
line; run it yourself, and keep the `.jks` outside the repository.

**The version comes from `src/core/version.h`**, parsed by
`app/build.gradle.kts`: `versionName` is `MAJOR.MINOR.PATCH` and
`versionCode` is `MMmmpp` (3.3.0 → 30300). Play refuses a versionCode it
has already seen, so re-uploading means bumping the patch in the header
— where every other artefact reads it too.

### Before submitting to Play

```bash
python tools/check_release.py
```

This compares the claims the project makes about itself against the
things they claim about: the version, the addresses the app can open,
how many checks the About blurb says there are, Play's title and
description limits, and whether the generated notices still match the
dependencies they name. It exists because each of those has been wrong
here at least once, and none of them fails a build or a test — the About
blurb said *seven* KPIs for months after `--check` began reporting
eight.

Add a check whenever drift is found by hand, so that the next time it is
found by machine.

The rest of the submission checklist — the ones no script can settle —
is in [`design/work-items/play-listing.md`](../design/work-items/play-listing.md).

### Publishing the wiki

```bash
bash tools/publish_wiki.sh --push
```

`docs/wiki/` is the source; GitHub serves the pages from a second
repository, so they have to be copied. The app links into them (About →
Documentation, and the orbit badge on the Analysis screen), which makes
this part of shipping rather than optional. `check_release.py` verifies
that every wiki link in the app names a page that exists in `docs/wiki/`
— it cannot tell whether that page has been pushed.

### Icons

One script draws every form the icon ships in — the Windows `.ico`, the
Android bitmaps, the adaptive and themed vectors, and the Play listing
asset:

```bash
python tools/make_icons.py
```

Edit the geometry there, never the generated files; the whole point is
that the four cannot drift apart. Free and pro differ by accent colour
only, and the desktop GUI uses the blue mark. Rebuild the GUI afterwards
so `windres` picks up the new `.ico`.

## What CI checks, and what it does not

`.github/workflows/ci.yml`, on every push to `main` and every pull
request:

- **Core and tests** — configures, builds and runs `ctest` on Linux,
  then `tools/check_release.py`. The five tests link `ntrip_core` alone
  and touch no platform headers, which is why they run anywhere.
- **Both editions** — `assembleFreeDebug assembleProDebug`, which also
  runs `checkEditionParity` as a preBuild dependency, so an edition
  acquiring code of its own fails the build here too.

Weekly, and on demand: **verified claims**, running
`tools/verify_memory.py`. It is not on every push because several of its
commands ask whether the site and the wiki are still live, and a check
that fails on someone else's outage teaches people to ignore it.

**The Win32 GUI is not built by CI.** It is Windows-only and has a
second, hand-written build path (`build-gui.bat`) that lists every
source by hand. Building it on a runner would need a MinGW toolchain
whose failures would say more about the runner than about the code, so
it stays a manual step — build it, and run the station check against a
live caster, before any desktop release.

## Adding a New Frontend, Window, KPI or Config Field

**Adding a GUI source file** — add it to *both* `CMakeLists.txt`
(`ntrip-analyser-gui` target) and `build-gui.bat`.

**Adding a floating GUI window** — copy the shape of
`gui/gui_vrs_window.c`: `Register…WindowClass` + `Create…Window`, an
`AppState` field for the HWND and a saved `RECT`, a `WM_APP_*` id in
`resource.h`, a menu item in `resource.rc`, and a command case in
`gui_events.c`. State that must outlive the window belongs in
`AppState`, not in the window.

**Adding a KPI** — `KPI_COUNT` in `kpi.h`, the evaluation in `kpi.c`,
and its entry in the `is_hard[]` table. Every frontend renders whatever
the array holds, so no UI needs changing — but `--check` help text,
`docs/cli.md` and the Android strings quote the count.

**Adding a config field** — `src/core/config.c` (both the array reader
and the legacy reader if it applies), the GUI's writer in
`gui_events.c`, `MonitordMountpoint` in `ConfigFile.kt`, and
`docs/jsonConfigs.md`.

**Adding a test** — one `add_executable` + `add_test` pair in
`CMakeLists.txt`, linking `ntrip_core`. Fixtures live in `test/data/`
and the path is passed as `argv[1]` so the working directory does not
matter.

## Common Problems

| Symptom | Look at |
|---|---|
| Gradle: "JAVA_HOME is set to an invalid directory" | Use the Windows path to Adoptium JDK 17 |
| `gcc: command not found` | Put CodeBlocks' MinGW on `PATH` |
| GUI builds under CMake but `build-gui.bat` fails | A source file was added to only one |
| Android: `UnsatisfiedLinkError` | `@JvmStatic` moves the symbol to the enclosing class — see `memory/gotcha-log.md` |
| Android: screen stuck on a stale value | A snapshot field is `null` and the Kotlin model is non-nullable, so nothing decodes |
| Sky view places few satellites | It needs the station's own position (1005/1006), not only orbits |
| `adb` cannot see the device | `adb kill-server`, then `adb reconnect offline` |
| A run never settles | A pending KPI holds the roll-up at RUNNING; there is a 300 s ceiling |

## Deployment

`cmake --build build --target release` stages the artefacts and writes
per-platform `SHA256SUMS` (Windows and Linux assets are built on
different machines, so one shared file would be overwritten).

Version comes from `src/core/version.h` and nothing else — the build
system parses it. Android's `versionName` is still maintained by hand
against that header.

**The agent never publishes.** Prepare the assets, then hand off.

### Post-deploy verification

There is no server and no CI here, so "deployed" means an artefact
someone downloads. Verify the artefact itself, not the build tree:

- [ ] Run the built binary from `bin/`, not from the build directory —
      the working directory decides which `config.json` it finds.
- [ ] `ntrip-analyser --version` matches `src/core/version.h`.
- [ ] `--check` completes against a live caster and exits 0, 6 or 1.
- [ ] For Android, install the APK on a device and run one station
      check: the foreground service, GNSS and permissions cannot be
      exercised by a build.

## Document Size Heuristic

If a document passes ~150 lines during a session, split the new content
into its own file and link back. This runbook is at that boundary; the
next operational area (store listings, signing) becomes a separate file
rather than a section here.

## Documentation Practices

| Change | Update |
|---|---|
| Anything user-visible | `changelog.md`, with the measurement behind the claim |
| A design decision | The relevant file in `design/` or `android/design/` |
| Shipped or planned work | `design/todo.md` — stable item numbers, never renumbered |
| A non-obvious failure | `memory/gotcha-log.md` |
| A command in this file that went stale | Fix it here; a stale runbook is worse than none |
