# NTRIP-Analyser for Android — Phase 1 (Normal mode)

One screen, one verdict, seven rows: *does this station meet the basic
KPIs for RTK service?* The app connects, watches for about ninety
seconds, and shows **STATION OK**, **CAUTION** or **FAILED**.

No threshold lives in Kotlin. The verdict comes from `src/core/kpi.c` —
the same engine behind the CLI's `--check` — so a station cannot pass on
the phone and fail in a script. See [`design/design-review.md`](design/design-review.md)
for the decisions (D1–D7) this structure follows.

## Layout

```
android/
  app/src/main/cpp/     ntrip_bridge.{h,c}   plain C: session + KPI + JSON
                        jni_glue.c           JNI marshalling, no logic
                        CMakeLists.txt       NDK build of the shared core
  app/src/main/java/…/  NtripBridge.kt       the JNI boundary, Kotlin side
                        Model.kt             JSON model + settings type
                        MonitorService.kt    the run, as a foreground service
                        MainActivity.kt      Compose UI: badge + seven rows
                        Settings.kt          SharedPreferences persistence
```

**The split between `ntrip_bridge.c` and `jni_glue.c` is deliberate.**
JNI code cannot be compiled without an NDK, so anything living there
escapes desktop testing. Keeping the session lifecycle, the KPI engine
and the JSON assembly in plain C means the part that can carry bugs is
testable on any machine — which is how this bridge was validated before
an Android toolchain existed (below).

## What is verified, and what is not

| | Status |
|---|---|
| C bridge: open, pump, JSON, verdicts | **Verified** — replayed a capture and ran live against a caster for 77 polls, reaching STATION OK with a real ARP; the document parses as JSON |
| The shared core on the NDK | **Builds** — arm64-v8a and x86_64, two pre-existing warnings, no errors |
| JNI glue | **Builds and exports correctly** — all six symbols carry the `$Companion` mangling Kotlin's externals expect (`llvm-nm` on the built `.so`) |
| Kotlin, Compose UI, Gradle | **Builds** — `assembleDebug` produces a 9.4 MB APK carrying `libntrip_android.so` for both ABIs |
| The app running on a device | **Verified** — installed on a Huawei SNE-LX1 (Android 10, arm64-v8a) and reached STATION OK against a live caster, all seven PASS |

The first device run found three defects that no amount of compiling
would have caught, all now fixed:

- **`@JvmStatic` changes the JNI symbol name.** The externals are
  declared in a companion object, but `@JvmStatic` promotes them to the
  enclosing class, so the runtime looks up `..._NtripBridge_nativeOpen`,
  not `..._NtripBridge_00024Companion_nativeOpen`. Confirming the symbols
  existed proved nothing — they existed under the name nothing looks up.
- **Every double in the snapshot may be JSON `null`.** `ns_stats_to_json()`
  serialises an unmeasured double as null rather than 0, deliberately, so
  "no ARP yet" is not "a station at 0°N 0°E". The model declared them
  non-nullable, so the first document failed to decode — and because a
  decode failure publishes nothing, the screen sat at READY with no error
  while the run was working perfectly.
- **A stream that never opened reported nothing.** The loop broke the
  moment the pump failed, so a bad hostname or refused login dropped the
  UI silently back to READY. It now keeps evaluating until the KPI engine
  reaches its FAILED verdict, which names the reason.

## Building — VS Code, or Android Studio

Either works; the project is plain Gradle with no IDE-specific files.

### Prerequisites (both routes)

1. **JDK 17** — the Android Gradle Plugin 8.x requires it. A Java 8
   runtime is *not* enough.
   [Temurin 17](https://adoptium.net/temurin/releases/?version=17) is fine.
2. **Android SDK + NDK.** Without Android Studio, install the
   [command-line tools](https://developer.android.com/studio#command-line-tools-only),
   unpack to `%LOCALAPPDATA%\Android\Sdk\cmdline-tools\latest`, then:

   ```bash
   sdkmanager "platform-tools" "platforms;android-35" "build-tools;35.0.0" "ndk;27.0.12077973" "cmake;3.22.1"
   ```

3. Point Gradle at the SDK — create `android/local.properties`:

   ```
   sdk.dir=C\:\\Users\\<you>\\AppData\\Local\\Android\\Sdk
   ```

   That file is machine-specific and git-ignored; never commit it.

4. **Generate the Gradle wrapper once.** `gradle/wrapper/gradle-wrapper.properties`
   is committed, but `gradlew` and its `.jar` are not — a binary in the
   repository that nobody here can build or verify is worse than a
   documented one-liner. Opening the project in Android Studio creates
   them automatically. From the command line, with any Gradle 8.x
   installed, run once in `android/`:

   ```bash
   gradle wrapper
   ```

   After that `./gradlew` works and Gradle itself is self-updating.

Two traps worth knowing, both hit during this setup:

- **`local.properties` is a Java properties file, so backslashes are
  escapes.** A single-backslash Windows path silently collapses to
  nonsense and AGP fails with "the syntax of the filename is incorrect"
  pointing at `NdkLocator`, which looks like a missing NDK. Double them,
  or use forward slashes.
- **`ndkVersion` is pinned in `app/build.gradle.kts`.** Left to AGP's
  default it names whatever version that plugin release expects, and a
  mismatch surfaces as the same unhelpful path error rather than
  "NDK not found".

### VS Code

Works well for this project: Gradle is driven from the terminal and the
Kotlin is ordinary source. Useful extensions —
**Kotlin** (fwcd) for completion, **Gradle for Java**, and **C/C++** for
the bridge (point it at `src/` for includes).

What you give up versus Android Studio: **Compose previews**, the layout
inspector, the visual profiler, AVD management, and single-click native
debugging. For a one-screen app driven by C, that is a modest loss —
though the first native crash is much easier to diagnose in Studio.

Build and install from the `android/` directory:

```bash
./gradlew assembleDebug
```

The APK lands in `app/build/outputs/apk/debug/app-debug.apk`.

### Deploying to a phone

1. On the phone: **Settings → About → tap Build number seven times**,
   then **Developer options → USB debugging** (or **Wireless debugging**).
2. Connect by USB and accept the RSA prompt on the phone.
3. From `android/`:

   ```bash
   ./gradlew installDebug
   ```

   or, for an APK you already built:

   ```bash
   adb install -r app/build/outputs/apk/debug/app-debug.apk
   ```

Over Wi-Fi instead of a cable, pair once with the code the phone shows
under Wireless debugging:

```bash
adb pair 192.168.1.50:37021
```

Watch the app's own logging with:

```bash
adb logcat -s ntrip_android:V AndroidRuntime:E
```

### Emulator

An emulator reaches the internet, so a real caster works — but it cannot
show you what matters most on a phone: behaviour on a flaky mobile link.
Test on hardware before trusting a field verdict.

## Known limitations of Phase 1

- ~~The password is stored in plain SharedPreferences.~~ **Fixed.**
  Saved connections live in `EncryptedSharedPreferences`, keyed from the
  Android Keystore, and the pre-profiles preferences file is cleared once
  its contents have been migrated. A device whose Keystore refuses to
  produce a key falls back to plain preferences rather than losing the
  app. Note that *exported* configuration files are plain text by
  necessity — see [docs/jsonConfigs.md](../docs/jsonConfigs.md).
- **`versionName` is maintained by hand** against `src/core/version.h`.
  The desktop builds parse that header; Gradle does not yet.
- **No launcher icon adaptive variant** — a plain bitmap icon at five
  densities. Fine on every supported release, unfashionable on 26+.
- **Normal mode only.** No sky plot, no per-SV table, no VRS screen;
  those are Phase 2, and the C side for the VRS assertions already
  exists (`src/core/vrs_check.c`).

## Phase 2, when it comes

The C work is largely done: `sky_render` produces an RGB buffer that
drops into `Bitmap.createBitmap`, `vrs_check.c` holds the network-RTK
assertions, and the snapshot already carries per-type message statistics.
The open question is the JNI surface — the JSON bridge was chosen for
Phase 1 precisely because per-SV data would make it unwieldy, so
Phase 2 adds typed bindings where the volume demands them (D3).
