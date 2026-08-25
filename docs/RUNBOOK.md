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
- **One build system describes the desktop.** `build-gui.bat`, its
  PowerShell twin and the hand-listed VS Code tasks retired with the TLS
  rollout (2026-08-25): a second source list is a second place to forget
  a file. `CMakeLists.txt` owns the only desktop list; the daemon's
  `service/Makefile` survives on wildcards, and CI builds it.
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

**Before believing a bug report about one edition, check what is
installed.** The editions share `src/main`, so a fix reaches both the
moment it is written — but only the edition you rebuilt reaches the
phone. Two defects fixed in shared code were reported as live in pro,
whose APK was an hour older than the fix:

```bash
adb -s <serial> shell dumpsys package nl.pe1mew.ntripanalyser.pro | grep lastUpdateTime
```

Installing a release-signed build over a debug-signed one needs an
uninstall, and that takes the app's saved connections and imported
navigation file with it. Know what is on the device before doing it.

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

### Producing a stream that stops, without evicting a real station

KPI 1 distinguishes four states and only one of them is a healthy
stream. The other three were found on live casters — a single-session
caster evicting the analyser's own earlier connection — and reproducing
them there means deliberately taking a real station's session away. The
stub does it instead, taking a fourth argument for how the session ends:

```bash
python test/tools/stub_caster.py 2101 90 bin/some_capture.rtcm3 stop 12 &
cd bin && ./ntrip-analyser.exe --caster 127.0.0.1 --port 2101 \
    --mountpoint TEST --user x --password y --check
```

| Mode | What the caster does | KPI 1 must say |
|---|---|---|
| `flow` | streams for the whole run *(the default)* | `Authenticated, connected, data flowing` |
| `silent` | accepts, then sends nothing at all | `Connected, but the caster has sent nothing` |
| `stop` | streams for `feed_s`, then goes quiet with the socket open | `Data arrived for N s, then the stream stopped` |
| `drop` | streams for `feed_s`, then closes the session | `Connection lost after N s of data` |

The run's total seconds must comfortably exceed `feed_s` plus the ten
the check needs to notice, or the stub's own deadline closes the socket
and turns `stop` into `drop`.

`stop` also turns itself into `drop` after `stall_timeout_s` — 60
seconds of silence by default — because that is exactly what the
dead-man's switch is for. Inside that window KPI 1 reads
`Data arrived for N s, then the stream stopped`; past it the session
has given up on the socket and it reads `Connection lost after N s of
data`. Both are true of the same stub, at different moments, so a run
made to demonstrate one of them wants `feed_s` and the run length
chosen with the timeout in mind.

Point the GUI at the same stub — caster `127.0.0.1`, that port,
mountpoint `TEST`, any credentials — to see the four states in the
station-check window.

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
.\gradlew.bat assembleFreeRelease assembleProRelease bundleFreeRelease bundleProRelease
```

**Build the APKs and the bundles together, always.** They are different
artefacts of the same release -- the APK goes to GitHub and to a
handset, the bundle goes to Play -- and building them by separate
commands is how free 3.7.0 shipped: the sky view's inset fix was
committed, the APKs were rebuilt, the bundle was not, and Play served a
binary four and a half hours older than the tag it claimed to be. The
tag was right and the users' phones were wrong.

`python tools/check_release.py` now refuses to pass when an artefact
under `app/build/outputs` is older than the sources it was built from,
or declares a version this tree does not. Run it **after** building and
before uploading -- in that order, because it is the built file it
checks, not the intention to build one.

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

-- but prefer the combined command above, so the APK and the bundle
cannot disagree about what they contain.

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
is in [`design/work-items/play-listing.md`](https://github.com/pe1mew/NTRIP-Analyser/blob/main/design/work-items/play-listing.md).

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

- **Core and tests** — configures with `-Wall -Wextra`, builds and runs
  `ctest` on Linux, then `tools/check_release.py`. Six of the seven tests
  link `ntrip_core` alone and touch no platform headers, which is why
  they run anywhere; `test_capture` links `ntrip_session` because the
  thing it pins lives there, and still needs no network — it replays a
  file it builds itself.
- **The daemon's own build path** — `make -C service`, which uses its own
  flags and its own source list. CMake keeps a separate list, so without
  this step the two drift apart silently; they had, and `make -C service`
  had stopped linking.
- **Both editions** — `assembleFreeDebug assembleProDebug`, which also
  runs `checkEditionParity` as a preBuild dependency, so an edition
  acquiring code of its own fails the build here too.

Weekly, and on demand: **verified claims**, running
`tools/verify_memory.py`. It is not on every push because several of its
commands ask whether the site and the wiki are still live, and a check
that fails on someone else's outage teaches people to ignore it.

**The Win32 GUI is not built by CI.** It is Windows-only, and building
it on a runner would need a MinGW toolchain whose failures would say
more about the runner than about the code, so it stays a manual step —
`cmake --build build` builds it with everything else; run the station
check against a live caster before any desktop release. (Its second,
hand-written build path retired with the TLS rollout; CMake's list is
the only list.)

## Adding a New Frontend, Window, KPI or Config Field

**Adding a GUI source file** — add it to the `ntrip-analyser-gui`
target in `CMakeLists.txt`. That is the only list; the hand-written
second build retired with the TLS rollout.

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
| A C change under `src/` or `lib/` does not reach the phone | Gradle's up-to-date check **and** build cache are blind to out-of-tree sources: `gradlew clean` then build with `--no-build-cache` |
| `gcc: command not found` | Put CodeBlocks' MinGW on `PATH` |
| Android: `UnsatisfiedLinkError` | `@JvmStatic` moves the symbol to the enclosing class — see `memory/gotcha-log.md` |
| Android: screen stuck on a stale value | A snapshot field is `null` and the Kotlin model is non-nullable, so nothing decodes |
| Sky view places few satellites | It needs the station's own position (1005/1006), not only orbits |
| `adb` cannot see the device | `adb kill-server`, then `adb reconnect offline` |
| A run never settles | A pending KPI holds the roll-up at RUNNING; there is a 300 s ceiling |

## Deployment

`cmake --build build --target release` stages the artefacts and writes
per-platform `SHA256SUMS` (Windows and Linux assets are built on
different machines, so one shared file would be overwritten).

**Windows assets are built here; Linux assets are built by CI.** Pushing
a `v*` tag runs `.github/workflows/release-linux.yml`, which builds,
tests, packages and attaches the Linux assets to that tag's release —
creating it as a *draft* if it does not exist yet. It runs on
`ubuntu-22.04` deliberately: a binary cannot start against a glibc older
than the one it was built on, and `ubuntu-latest` would exclude Debian 12
and Ubuntu 22.04, which is most of the VPS estate.

So the release sequence is: bump `src/core/version.h`, commit it, tag
it locally, build and stage the Windows assets here -- then **push the
tag**, and let CI open the release. Tag and header must agree —
`cmake/CheckReleaseTag.cmake` fails the packaging otherwise, which is
the whole reason it exists, and it can only compare a tag that already
exists.

```bash
git tag v3.7.1                       # after committing the bump
cmake --build build --target release # the guard reads the tag here
git push origin v3.7.1               # CI opens a draft, Linux assets attached
gh release upload v3.7.1 build/dist/*
gh release edit v3.7.1 --title "NTRIP-Analyser 3.7.1" --notes-file notes.md
gh release edit v3.7.1 --draft=false
```

**Hand the release commands over only when everything they name
already exists.** Two releases (3.7.2, 3.7.3) hit `no matches found`
at the asset upload because the desktop packaging step lived in a
sentence between the commands -- a pause that lives in prose is not a
checklist. Package first, then write the upload commands.

**Push the tag before reaching for `gh release create`.** This order
used to read the other way round -- create the release, then push the
tag -- and it does not work once the tag exists locally: *"tag v3.7.1
exists locally but has not been pushed ... please push it before
continuing or specify the `--target` flag"*. The packaging guard wants
the tag to exist and `gh release create` wants it not to, so the only
order that satisfies both is to tag, package, push, and then fill in
the release CI has already opened. Met at v3.7.1.

**Commit the bump before tagging, and read the tag rather than the tree
to check it.** A tag points at a commit; a staged bump is not in one, and
staged and committed changes are indistinguishable in an editor and in
the left column of `git status --short`. This has now failed twice, at
v3.4.0 and again at v3.5.0, in exactly the same way — both times caught
by the packaging guard at the cost of one failed run:

```bash
git show v3.5.0:src/core/version.h | grep NTRIP_VERSION_STRING
```

If that disagrees with the tag, move the tag rather than editing history:
delete it locally and on the remote, re-tag the bump commit, push again.

Version comes from `src/core/version.h` and nothing else — every build
system parses it, Gradle included: `versionName` and `versionCode` are
both derived there (`android/app/build.gradle.kts`), so the header is the
only file a bump touches. (This line used to say Android's `versionName`
was kept by hand; it has not been for some time.)

**The agent never publishes.** Prepare the assets, then hand off.

"Publishes" covers **uploading**, not just the moment a draft goes
public: `gh release upload`, `gh release create`, `gh release edit
--draft=false`, and pushing a tag are all the author's, whichever of them
happens to be reversible. Attaching a binary to a draft is putting a
binary on the project's release page under the author's name — the draft
status is a detail of timing, not a difference in kind. Crossed on
2026-08-18, when the assets for v3.5.0 were uploaded on the strength of
"let's add the Windows artefacts"; that is a request for the assets to
exist, which is `cmake --build build --target release`, and stops there.
Written down because the line was genuinely ambiguous until it was
crossed.

### Post-deploy verification

There is no server here, so "deployed" means an artefact someone
downloads. CI builds and tests what goes into the Linux assets, but a
green run is not a substitute for running the thing: verify the artefact
itself, not the build tree.

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
