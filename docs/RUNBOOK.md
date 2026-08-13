# Runbook

How to build, test, deploy and extend NTRIP-Analyser. Paths and commands
here are the ones actually used on the author's machine; where they are
machine-specific it says so.

## Operational principles

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

## Toolchains (this machine)

| Need | Path |
|---|---|
| Host C compiler | `C:\Program Files\CodeBlocks\MinGW\bin` (gcc, windres, mingw32-make) |
| JDK 17 for Gradle | `C:\Program Files\Eclipse Adoptium\jdk-17.0.20.8-hotspot` |
| Android SDK / adb | `%LOCALAPPDATA%\Android\Sdk\platform-tools` |
| NDK | 27.0.12077973, pinned in `android/app/build.gradle.kts` |

The JDK is **Adoptium**, not the Microsoft build the name pattern
suggests. Gradle rejects a POSIX-style `JAVA_HOME`; give it the Windows
path.

## Desktop: build and test

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

## Desktop: verify against a live caster

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

## Android: build, install, observe

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

Inspecting a debug build's stored settings:

```bash
adb shell "run-as nl.pe1mew.ntripanalyser.pro ls shared_prefs/"
```

`caster_secure.xml` is `EncryptedSharedPreferences`; nothing readable
should appear in it. `caster.xml` is the pre-profiles file and must be
empty after a migration.

A station check takes ~90 s and the sustain window is 60 s, so allow
about 105 s before expecting a settled verdict.

## Extension points

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

## Debugging common problems

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

## Releasing

`cmake --build build --target release` stages the artefacts and writes
per-platform `SHA256SUMS` (Windows and Linux assets are built on
different machines, so one shared file would be overwritten).

Version comes from `src/core/version.h` and nothing else — the build
system parses it. Android's `versionName` is still maintained by hand
against that header.

**The agent never publishes.** Prepare the assets, then hand off.

## Documentation practices

| Change | Update |
|---|---|
| Anything user-visible | `changelog.md`, with the measurement behind the claim |
| A design decision | The relevant file in `design/` or `android/design/` |
| Shipped or planned work | `design/todo.md` — stable item numbers, never renumbered |
| A non-obvious failure | `memory/gotcha-log.md` |
| A command in this file that went stale | Fix it here; a stale runbook is worse than none |
