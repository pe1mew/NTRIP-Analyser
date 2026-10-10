# Gotcha Log

<!-- Structured problem/solution journal. Append-only.
     Part of the self-learning loop: Capture → Surface → Promote → Retire.

     PROMOTION LIFECYCLE:
     - New entries start here (Capture)
     - At end-of-session, review for patterns (Surface, via /curate)
     - When an entry recurs 2-3 times, promote it to a topic file or the
       memory index as an "if X, then Y" pattern (Promote)
     - When the root cause is fixed, mark [RESOLVED] IN THE HEADING (Retire) —
       curation reads headings, not bodies. Same for a recurrence count: [x3].
       "Fixed" means code or procedure changed. An entry retired on advice
       alone is not retired: the v3.4.0 tag gotcha was marked [RESOLVED] with
       "check the tag first" as its fix, and recurred identically in three days.
     - Record promotions in the "Promoted" table below.

     Template for new entries:

### [Short description] (YYYY-MM-DD)
**Problem**: What went wrong or was confusing.
**Root cause**: Why it happened.
**Fix**: What solved it.

     2-3 lines. Write the lesson, not the narrative of the session that
     found it. If it needs a page, it belongs in a topic file or a design
     doc, not here. -->

<!-- Seeded 2026-08-13 from the sessions that produced the current unreleased
     work. These are real defects with measured root causes, not examples. -->

### JNI symbols existed under a name nothing looks up (2026-08-11) [RESOLVED]
**Problem**: Every native call threw `UnsatisfiedLinkError` although the symbols were present in `libntrip_android.so`.
**Root cause**: The externals are declared in a Kotlin `companion object`, but `@JvmStatic` promotes them to static methods on the *enclosing* class, and the JVM resolves the symbol from where the method ends up. They existed under the `$Companion` name; nothing looked there.
**Fix**: `JNI_FN(name)` expands to `Java_..._NtripBridge_##name`. Confirming symbols exist proves nothing here — only a native call that does not throw does.

### A correct run that never reached the screen (2026-08-11) [RESOLVED]
**Problem**: The phone sat at READY while a run completed normally.
**Root cause**: `ns_stats_to_json()` emits `null` for anything unmeasured, so "no ARP yet" is not reported as "a station at 0N 0E". The Kotlin model declared those doubles non-nullable, decoding threw, and a decode failure publishes nothing — the screen kept its previous state with no error anywhere.
**Fix**: Nullable in the model. A snapshot field that can be absent must be modelled as absent.

### GLONASS: one satellite out of 279 (2026-08-12) [RESOLVED]
**Problem**: A daily RINEX navigation file yielded one GLONASS satellite.
**Root cause**: RINEX 3.05 gives GLONASS a fourth orbit line that 3.04 did not. The loader consumed a fixed three; the leftover line was taken for an unknown system, and the four-line "resync" skip then ate the next record, so the reader never came back into phase.
**Fix**: Read by structure, not by counting lines per system — epoch lines name their system in column 1, continuations start with spaces. `test/test_rinex_nav.c` pins it with two 3.05 records back to back, which is what turns one misread into a cascade.

### Fresh orbits reported as "no orbits at all" (2026-08-12) [RESOLVED]
**Problem**: The app said the sky view could place nothing, over a cache of 139 orbits.
**Root cause**: Ephemeris age took the entry with the highest week and `toe` across systems whose week numbers share no origin. It picked a NavIC record with a reference epoch 7.4 h in the *future* and returned a negative age, which the UI rendered as "none".
**Fix**: Smallest per-satellite age, computed in each system's own frame; an epoch ahead of now counts as fresh. GLONASS `toe` is Moscow seconds-of-day, not seconds-of-week.

### The sky view fell back to phone GNSS for 30 s of every run (2026-08-12) [RESOLVED]
**Problem**: A new run showed a handful of satellites, then jumped to the full constellation — it looked like the orbit cache was being cleared.
**Root cause**: Nothing clears the cache. Placement needs the *station's* position, which arrives only with the first 1005/1006 — up to 30 s on a station that sends one every 30 s.
**Fix**: The bridge remembers the last broadcast position per mountpoint for the process lifetime, for placement only: `have_arp_info` stays false, so KPI 3 still waits for a real message.

### KPI 8 failed healthy stations, three times (2026-08-12) [RESOLVED] [x3]
**Problem**: Advertised-versus-actual reported failures on stations that were fine — judged at 20 s, then on rate-less types, then on a constellation advertised but not visible.
**Root cause**: Three wrong assumptions: that 20 s is enough evidence; that every advertised type carries a rate; and that advertising QZSS in Europe is a fault rather than ordinary.
**Fix**: A 30 s floor, a 600 s grace for rate-less types, and judging only the direction that misleads — streaming something never advertised. Constellations are compared against the sourcetable's NavSys field, never the 1005/1006 bits.

### A bounded test that could never end (2026-08-12) [RESOLVED]
**Problem**: The GUI station check ran indefinitely on a mountpoint the caster does not list.
**Root cause**: With no sourcetable entry KPI 8 stays PENDING — rightly, since "could not check" is not a pass — and a pending KPI holds the roll-up at RUNNING, which never settles.
**Fix**: The 300 s ceiling the CLI already applied, plus a header naming which of three ways a run ended.

### Config load named the wrong mountpoint (2026-08-13) [RESOLVED]
**Problem**: "Loaded configuration for " with an empty name.
**Root cause**: Once settings became derived from a profile store, the message still read the value captured before the store updated.
**Fix**: Name what was just loaded, from the new value.

### A network mountpoint could not be checked at all (2026-08-13) [RESOLVED]
**Problem**: `--check` on `caster.centipede.fr/NEAR` reported "connected but no data arriving" and FAILED, while `curl` with an `Ntrip-GGA` header streamed 15 kB in ten seconds.
**Root cause**: `--check` set `opt.send_gga = false` and only `--check-vrs` drove the uplink, so a service that answers nothing until it knows where the rover is was never told.
**Fix**: The mountpoint decides, from the sourcetable NMEA flag the check already parses for KPI 8. The GUI was never affected — it uplinks by default — and Android follows the same flag.

### A mountpoint 816 entries into a sourcetable did not exist (2026-08-13) [RESOLVED]
**Problem**: On Android, KPI 8 said "no sourcetable entry to compare against" for a mountpoint that plainly existed, and the run never settled.
**Root cause**: The bridge parsed a fixed 512 entries; Centipede publishes 1217 and lists `NEAR` at 816. The CLI had always counted first and allocated to fit.
**Fix**: Count, allocate, parse — and log what the fetch did, because the C side's stderr goes nowhere on Android and "cannot judge" with no way to ask why is not a diagnosis.
**Lesson**: A fixed cap on data from a third party is a silent truncation waiting for a bigger third party. Count first.

### A property of the station looked like a defect in the app (2026-08-13) [x2]
**Problem**: The C/N0-versus-elevation plot showed horizontal white lines in the free edition and none in pro, which read as the two editions diverging.
**Root cause**: The stations differed, not the editions. MSM4 carries C/N0 in whole dB-Hz, so half-decibel plot cells could fill only every second row; the pro station was MSM7 at a sixteenth of a decibel and filled them all.
**Fix**: Bin at the coarsest resolution any stream delivers (1 dB), and document the per-format resolution in `android/design/views.md`.
**Lesson**: Before suspecting the app, point both editions at the *same* mountpoint. `checkEditionParity` now fails the build if an edition acquires code of its own, so a real divergence cannot hide behind this.
**Recurrence 2026-08-14**: the same striping in the Windows GUI, same cause, same 1 dB cure. A data property shows up in every renderer, so fixing one frontend leaves the others wrong.

### An IME invented characters in a mountpoint (2026-08-13) [RESOLVED]
**Problem**: Typing `APEL0` on the handset saved `APEL0. `; the caster then reported no such mountpoint.
**Root cause**: EMUI's keyboard commits a full stop and a space on focus loss. The caster field had been given a URI keyboard for exactly this; the mountpoint, username and password fields had not.
**Fix**: The same `KeyboardType.Uri` on all of them. `trim()` cannot help -- it removes a space, not a character the user never typed.

### A commit landed in the wrong worktree's index (2026-08-13) [RESOLVED]
**Problem**: A commit carrying a 28-file message contained one whitespace change to `changelog.md`.
**Root cause**: Each worktree has its own index. The work was staged in `.claude/worktrees/<name>`; `git commit` was run in the main repository, where the only dirty file was a stray edit.
**Fix**: Commit from inside the worktree that holds the work, then fast-forward `main` onto its branch.

### Heredocs corrupt C string literals (2026-08-12) [x5]
**Problem**: `\n` inside a heredoc-fed script arrives as a real newline, producing C source that no longer compiles.
**Fix**: Use file-editing tools for anything containing escapes, or write the script to a file first.
*Four more on 2026-08-22*, in one session and all the same shape: a `printf` in `cli_stream.c` that would not compile, and three error messages in `make_store_shots.py` broken identically. Each cost a build. The reliable dodge when a heredoc must be used is `chr(92) + 'n'`, which no shell can eat -- but the answer above was available every time, and this very entry could not be edited until it was applied.

### Windows filesystem is case-insensitive (2026-08-12)
**Problem**: Resizing `SHOT.png` into `shot.png` destroyed the original; later reads returned the resized copy.
**Fix**: Distinct names, not distinct case.

### JAVA_HOME is Adoptium, not Microsoft (2026-08-12)
**Problem**: Gradle: "JAVA_HOME is set to an invalid directory".
**Fix**: `C:\Program Files\Eclipse Adoptium\jdk-17.0.20.8-hotspot`, as a Windows path — Gradle rejects the POSIX form. Also on this machine: the only host C compiler is CodeBlocks' MinGW.

### A backup taken after the damage restored the damage (2026-08-14)
**Problem**: Deliberately breaking a source file to prove a test catches it left the break in place; the "restore" wrote it back.
**Root cause**: The first attempt crashed *after* writing the broken file and before restoring. The next script read that file as its baseline and saved it as `.bak` — the backup captured the break.
**Fix**: Take the baseline from `git`, not from the working file, and assert the restored text differs from the broken one. A restore that cannot fail is not a restore.

### A file rewritten with newline='
' doubled every carriage return (2026-08-14)
**Problem**: 2014 lines of `MainActivity.kt` ended `
`; subsequent edits stopped matching anything.
**Root cause**: The script converted LF to CRLF *and* opened the file with `newline='
'`, which converts again on write.
**Fix**: Open with `newline=''` on both sides and do the conversion once, explicitly. Better: use the file-editing tools, which get this right.

### Keystrokes went to whichever window had focus (2026-08-14)
**Problem**: Automating the Windows GUI with SendKeys typed a file path into something else; the target instance never received it.
**Root cause**: SendKeys is delivered to the focused window, and `AppActivate` does not reliably win focus on a busy desktop.
**Fix**: Drive a Win32 app by messages to its own handles — `WM_SETTEXT` to the control id, `WM_COMMAND` with the menu id. It reaches that window and nothing else, and needs no focus.

### A link can be correct while its destination does not exist (2026-08-14)
**Problem**: The app's new orbit badge opened the repository front page instead of the wiki page it names.
**Root cause**: GitHub creates the wiki *repository* when the first page is saved in the browser; enabling the wiki in Settings does not. Every link into an unborn wiki redirects.
**Fix**: `tools/publish_wiki.sh`, and a check that every in-app wiki link names a page that exists in `docs/wiki/`. The check cannot prove it was pushed — only the fetch can.

### GLONASS has no week, so a day-old orbit passed as an hour old (2026-08-14)
**Problem**: A ten-hour-old navigation file reported "newest orbit 58 min old", and a satellite could have been placed from a day-stale orbit.
**Root cause**: GLONASS `toe` is Moscow **seconds of day**. Every age and every validity test wraps at 86400 s, so yesterday's record lands a few hours behind now.
**Fix**: `SvEphemeris.toe_utc`, an absolute date, filled wherever one is known — any file carries a calendar datetime. Live streams keep the wrap; their records are seconds old. `test/test_eph_validity.c` pins both halves.

### A fetch cache made a fixed page look unfixed (2026-08-14)
**Problem**: The published privacy policy still showed the old app names minutes after the fix was deployed and the build reported success.
**Root cause**: Fetched responses are cached for fifteen minutes per URL. The build was fine; the reader was stale.
**Fix**: Add a distinct query string when re-checking a page you have just fetched. Also: `pages/builds/latest` lags behind the deployment — the site is the evidence, not the API.

### The constraint against scripted rewrites, broken an hour after writing it (2026-08-14) [x3]
**Problem**: A heredoc-fed script wrote `"
"` into `tools/verify_memory.py` as a literal newline, splitting a statement across two lines. Two other edits in the same script matched nothing and said nothing.
**Root cause**: The same escape-mangling promoted to a hard constraint that morning — and `.replace()` without an assertion, which cannot fail out loud.
**Fix**: Edit tools for anything with escapes; every scripted substitution asserts its target was found. A constraint is not learned until the next mistake is a different one.

### A navigation gesture eaten by an overscroll animation (2026-08-14)
**Problem**: On the S23, dragging right from the sky view no longer left the Analysis screen. The same build did it on the Android 10 handset.
**Root cause**: The screen reads what the pager *cannot* consume as "leave". From Android 12 the stretch overscroll consumes that leftover to animate with it, so `onPostScroll` saw nothing. The older glow effect only draws, which is why the gesture had always worked where it was tested.
**Fix**: `LocalOverscrollConfiguration provides null` around the pager — the stretch is worth less than the gesture. A nested-scroll parent only sees what every child declines to take.
**Lesson**: A gesture built on leftover deltas is a gesture built on what nobody else wanted, and that changes by platform version. Test navigation gestures on the newest Android available, not the oldest.

### An edition looked broken because its install was an hour old (2026-08-14) [x3]
*Recurred 2026-08-16 on the VPS: the tree was rebuilt to 3.4.0 and the binary in `/usr/local/bin` was still 3.3.0, because the build and the install were two commands and only the first was run. Not an Android property — any deployed artefact.*

*Recurred 2026-08-18 without any install being involved: after bumping to 3.5.0, `cmake --build build --target test_all` passed 12/12 while `bin/ntrip-analyser --version` still printed **3.4.0** — that target does not build the CLI, so the binary beside the tests was the previous one. Green tests are evidence about the targets that were built, and say nothing about an artefact that was not. Build the default target before believing a version.*

**Problem**: Two defects fixed in free were reported as still present in pro — the same two, on the same handset.
**Root cause**: Both fixes live in shared `src/main`; pro had them in source the moment they were written. The APK on the phone was built at 16:26 and the fix at 17:45.
**Fix**: Compare install timestamps before reading code — `adb shell dumpsys package <id> | grep lastUpdateTime`.
**Lesson**: One codebase and two editions means the *installs* diverge even when the code cannot. `checkEditionParity` guarantees shared code; nothing guarantees the phone has the newer build.

### The Gradle wrapper could not be executed anywhere but Windows (2026-08-14) [RESOLVED]
**Problem**: CI failed with `./gradlew: Permission denied`.
**Root cause**: The wrapper was committed from Windows, where git does not track the executable bit, so its mode was 100644.
**Fix**: `git update-index --chmod=+x android/gradlew`, not a `chmod` step in the workflow, which would have hidden it from every other clone.
**Lesson**: CI's first value is being a machine that is not yours. Every Linux and macOS clone had this defect for months and nothing said so.

### NDK 27 did not align the library to 16 KB pages (2026-08-14) [RESOLVED]
**Problem**: Play has required 16 KB page support since November 2025 for anything targeting Android 15+. `llvm-readelf` showed the shipping `.so` with LOAD segments at `0x1000`.
**Root cause**: The alignment was assumed to come free with a recent NDK. It did not, in this configuration.
**Fix**: `-Wl,-z,max-page-size=16384` in the native CMakeLists, verified in the bundle rather than in an intermediate.
**Lesson**: A toolchain's reputation is not evidence. Read the artefact.

### Play's crawler inferred a login from a password field (2026-08-14) [x2]
**Problem**: The submission was blocked by "missing credentials": Play had screenshotted the caster settings dialog and taken *Username* and *Password* for an account.
**Root cause**: Those fields configure access to a third party's NTRIP caster. The app has no accounts at all.
**Fix**: App access declared as "no restricted parts", with the reasoning and a public anonymous caster in the release notes, where a human reviewer reads.
**Lesson**: Expect automation to read the screen literally, and answer the human behind it.
**Recurred 2026-09-16, and the fix did not hold.** Free's first *production* release (30800) was rejected the day production access was granted -- "Violation of Play Console Requirements", decided by automated systems, citing `LoginWall.png` of the same dialog. Pro's closed-track pre-launch report had flagged it too. The release-note hint had also been removed that week (next entry). **Sturdier fix**: a real credential set in *App-toegang → Inloggegevens toevoegen* -- a dedicated reviewer account on the author's own caster (always up, reachable from anywhere, unguessable ASCII username, rate limiting kept on but loose, never rotated during a review), a backup mountpoint, and English instructions quoting the app's own labels (*Caster settings…*, *Save*, *Run the check*). Answer the automation where it looks, and the human in the same place. Tested from mobile data before sending -- a caster reachable from the home LAN proves nothing about a reviewer's reach.

### Device tooling is not standard equipment (2026-08-14) [x2]
**Problem**: `screenrecord` is absent on the EMUI handset, and every `adb shell` path was rewritten — `/sdcard/f.mp4` reached the phone as `C:/Program Files/Git/sdcard/f.mp4`.
**Fix**: Check `ls /system/bin/<tool>` before building a plan around it, and prefix `adb shell` commands carrying Unix paths with `MSYS_NO_PATHCONV=1`.
*Recurred 2026-08-20*: `adb shell uiautomator dump /sdcard/ui.xml` answered "dumped to /Files/Git/sdcard/ui.xml" and the file was unreadable. The fix was known and written down; it was not applied because the command looked like a read, not a path.

### A measurement that could not see what it was measuring (2026-08-15) [RESOLVED]
**Problem**: Before turning `-Wall -Wextra` on in CI, the tree was measured at **one** warning. The first CI run found **six**.
**Root cause**: `gcc -fsyntax-only` was used for speed. The truncation diagnostics (`-Wstringop-truncation`, `-Wformat-truncation`) come from the optimiser's value-range propagation and appear only from `-O2`, so that mode can never report them. A first attempt also passed `-std=c99` where the build uses `gnu99`, which hid `M_PI` and aborted a file early.
**Fix**: Compile the way the build compiles — same standard, same optimisation — or publish no number. Promoted to the project file.

### snprintf silenced nothing; it renamed the warning (2026-08-15) [RESOLVED]
**Problem**: `strncpy` truncation warnings were "fixed" with `snprintf(dst, sizeof dst, "%s", src)`. The next run reported the same lines under `-Wformat-truncation`.
**Root cause**: Both forms leave the bound for the optimiser to infer, so gcc still sees a possible truncation — only the diagnostic's name changed.
**Fix**: State the bound in the call: `snprintf(dst, sizeof dst, "%.*s", (int)sizeof(dst) - 1, src)`. Silent, and always NUL-terminated unlike `strncpy`.

### A stale CMake cache packaged the previous version (2026-08-15) [RESOLVED]
**Problem**: After bumping `version.h` to 3.4.0, `cmake --build build --target release` printed *"Packaging 3.3.0 for windows-x64"*.
**Root cause**: The version is read with `file(READ)` at configure time, and CMake was never told the build depends on that file — so an existing build directory kept the old cache. Only the release tag check noticed; **untagged, it would have produced 3.3.0-named assets from a 3.4.0 tree in silence.** CI never sees this because CI always configures from scratch.
**Fix**: `CMAKE_CONFIGURE_DEPENDS` on `src/core/version.h`. Where two build systems can disagree, the one that is *incremental* is the one that lies.

### A hand-written source list drifted until a second build system was run (2026-08-15) [RESOLVED]
**Problem**: `make -C service` failed to link — undefined reference to `get_gnss_id_from_rtcm` — on its first CI run. It had been broken for some time.
**Root cause**: `service/Makefile` lists its sources by hand and never gained `src/net/ntrip_handler.c`. CMake keeps its own list and built the daemon happily, so the failure was invisible to everyone except a packager or a VPS deployment.
**Fix**: Wildcard the shared directories (`src/core`, `src/net`, `src/session` are shared *by definition*; anything platform-specific lives elsewhere). **If two build systems describe the same sources, CI must run both** — the gap the GUI's `build-gui.bat` had until it was retired outright on 2026-08-25 (TLS L2), leaving `CMakeLists.txt` as the only desktop list.

### A tag on a tree whose bump was never committed (2026-08-15) [x2] [RESOLVED]
*Recurred 2026-08-18, identically, with `v3.5.0`: same staged-not-committed bump, same failed run, same message with the numbers changed. It had been marked `[RESOLVED]` three days earlier — wrongly, because the recorded fix was **advice** ("check `git show <tag>:version.h` first"), and advice is not a mechanism. What actually works is already in place and worked twice: `CheckReleaseTag.cmake` failed the run before anything was published. Retiring an entry needs a change to the code or the procedure, not a resolution to be careful.*

**Problem**: `v3.4.0` was tagged and pushed; the release workflow failed at packaging with "tag v3.4.0, version.h 3.3.0".
**Root cause**: The version bump was *staged*, not committed, so the tag landed on the previous commit. Staged changes look identical to committed ones in an editor and in `git status --short`'s left column.
**Fix**: `git show <tag>:src/core/version.h` before trusting a tag. The guard worked exactly as designed and cost one failed run instead of a mislabelled release — this is what `cmake/CheckReleaseTag.cmake` is for.

### `gh run watch | tail` reports success for a failed run (2026-08-15)
**Problem**: A failed CI run was nearly reported as passing: `gh run watch --exit-status ... | tail` printed `watch-exit=0`.
**Root cause**: `$?` after a pipeline is the **last** command's status — `tail`'s — not `gh`'s. `--exit-status` was set and correct; the pipe discarded it.
**Fix**: Redirect instead of piping (`gh run watch ... > /dev/null; echo $?`), or read the verdict from `gh run view` rather than an exit code that has passed through a pipe.

### The constraint against scripted edits, broken a fourth time (2026-08-16) [RESOLVED] [x4]
**Problem**: A `sed -i` meant to *find* a table row in `memory/gotcha-log.md` replaced it with the letter `X`, deleting a promoted pattern.
**Root cause**: The command was written as a substitution to test a match, with a throwaway replacement, and `-i` wrote it. Reaching for `sed` at all was the error: `CLAUDE.md` forbids it, this file's own Promoted table records three previous instances, and both were read earlier the same day.
**Fix**: Repaired with the editing tools. The lesson is not "be careful with sed" — it is that a constraint carried only in a document is obeyed until the moment convenience argues otherwise. **Use Grep to search and Edit to change; `sed -i` has no legitimate use in this repository.**

### systemd discarded stderr because stdout was silenced (2026-08-16) [RESOLVED]
**Problem**: A six-hour unattended capture ran perfectly and reported nothing — no frame count, no byte count, no reconnect count in the journal.
**Root cause**: The `systemd-run` recipe set `StandardOutput=null` to keep the message-type stream out of the journal. `StandardError=` defaults to **`inherit`**, which means *whatever StandardOutput is* — so stderr, where the summary and every error go, was discarded with it.
**Fix**: `--property=StandardError=journal` alongside it; the two belong together. Silencing one stream in systemd silences the other unless you say otherwise.

### A doc comment invented an edition difference (2026-08-16) [RESOLVED]
**Problem**: `android/design/editions.md` listed per-satellite C/N0 as absent in free and *planned* for pro. Both editions have drawn those bars all along.
**Root cause**: `SignalBars`' own documentation said `liveValues` was "true when the bars show this epoch (pro), false ... (free)". The call site passes `runState.running` and has never consulted `Features`. The comment described a split that never existed, and the design table was written from the comment rather than the code.
**Fix**: Corrected at the source, with the correction stated so it is not re-derived. **A claim in a comment is a claim**: check it against the call site before promoting it into a design document — three of four "drifts" found in one review traced back to documents copying each other.

### A schema field nobody fills (2026-08-16) [x3]
**Problem**: `NsStatsSnapshot.latency_s` is declared, documented, serialised into the daemon's JSON and the CSV export, and displayed on the Android station tile. Nothing computes it. Every consumer has published "not measured" since the schema was written.
**Root cause**: The field was added with the schema, in anticipation of code that never arrived. The ARP fields did exactly the same thing earlier — the daemon published `arp_valid:false` for every station until the KPI engine's first live run tripped over it.
**Fix**: Not yet; tracked as phases 0 and 1 of `design/work-items/measurement-tiers.md`. The lesson is the pattern, not the field: **a declared-but-unfilled field is worse than a missing one, because it looks like an answer.**

*Third occurrence found the same day: `sourcetable_offset_m`, declared and serialised to JSON and CSV, never computed — while the GUI performs that very comparison in its own code, leaving the shared field empty. Two coordinate faults (3.3 km, then 25 km) passed all eight KPIs because of it. A promoted pattern that keeps recurring means the promotion has not taken: the next occurrence should be prevented mechanically, not remembered — a check that every `NsStatsSnapshot` field is written somewhere outside `ns_stats_init` would have found all three.*

### systemd-run reports a launch, not a life (2026-08-16) [RESOLVED]
**Problem**: A six-hour capture was started on the VPS, printed `Running as unit: ntrip-capture.service`, and had already exited — after 7 ms.
**Root cause**: `systemd-run` reports that it handed the job to systemd, which says nothing about whether the process survived. The installed binary was an older build that rejected `--capture` outright.
**Fix**: `systemctl status <unit>` immediately after starting; `Active: active (running)` is the only confirmation that counts. The artefact, not the launcher's promise. (Noted and unexplained: the unit recorded `status=7`, which no exit code in that older build accounts for.)

### `install /dev/stdin` writes nothing (2026-08-16) [RESOLVED]
**Problem**: `sudo install -m 600 /dev/stdin /etc/ntrip/rfsee.json` with a pasted heredoc produced no file, quietly enough that the next command's failure was the first sign.
**Root cause**: `install` stats its source and will not copy a terminal or a pipe.
**Fix**: `sudo tee <path> > /dev/null <<'EOF'` to write a root-owned file from a heredoc, then `chmod`. Create the directory in the same command, or the move that follows fails on a path that was never made.

### A green verdict is not a correct registration (2026-08-16) [x2]
**Problem**: `RFSEE01` advertised a position 3.3 km from its antenna — a longitude with two digits rotated. Correcting it put HANESE's coordinates into RFSEE01's entry, moving it 25 km. **Both states returned STATION OK on all eight KPIs.**
**Root cause**: No KPI compares the sourcetable's declared position against the broadcast 1005/1006 ARP. `sourcetable_offset_m` exists in the snapshot for exactly this and nothing computes it; the GUI does the comparison in its own code, so the CLI and Android never see it.
**Fix**: Read the sourcetable directly — `-m | cut -d';' -f2,10,11` — after any registration change; a passing check says nothing about it. Specified as phase 0 of `design/work-items/measurement-tiers.md`, and it is the strongest tier-1 candidate precisely because it earned its place twice in one afternoon.

### A link that works in the repository and 404s on the website (2026-08-16) [RESOLVED]
**Problem**: `docs/licences.md` linked `../LICENSE`. Fine when browsing GitHub, a 404 for every visitor to the published site — verified: `https://pe1mew.github.io/LICENSE` does not exist.
**Root cause**: GitHub Pages serves `docs/` **as the site root**, so a relative link out of that folder points above the root. Nineteen such links had accumulated across eight files, most of them long before this session.
**Fix**: Absolute `https://github.com/pe1mew/NTRIP-Analyser/blob/main/…` for anything outside `docs/`; relative for anything inside it, where `jekyll-relative-links` rewrites `x.md` to `x.html` and both views work. `tools/check_release.py` now fails on `](../` in any `docs/*.md`.

### One session per account made a healthy station read as broken (2026-08-16) [RESOLVED]
**Problem**: Two consecutive `--check` runs on HANESE reported FAILED after 15 s and then 10 frames. A minute later the same station gave 45 of 45 epochs and a clean 90 s pass.
**Root cause**: The caster allows one session per account, and `--check` opens *two* connections — a sourcetable fetch, then the stream. Runs in quick succession evict one another.
**Fix**: Leave a gap between checks on a single-session caster. The report also misattributed this — KPI 1 said "connected but no data arriving" while KPI 2 counted 102 decoded frames — fixed 2026-08-17 (phase 4 of `design/work-items/cli-track.md`): KPI 1 now says `Data arrived for N s, then the stream stopped`, and the Troubleshooting page names the eviction as the first cause to rule out.

### A theme built for a landing page clipped the documentation (2026-08-16) [RESOLVED]
**Problem**: Architecture diagrams on the published site were cut off mid-line.
**Root cause**: `jekyll-theme-minimal` gives content a **500 px** column — right for a project page with three paragraphs, far too narrow for ninety-column diagrams and wide tables.
**Fix**: `docs/assets/css/style.scss` widens the column to 1080 px above 960 px viewport and leaves the theme's mobile behaviour alone. Verified by measuring `scrollWidth` against `clientWidth` per element rather than by eye: seven `<pre>` blocks, none clipped.

### A GUI log window that had never worked when the program is started normally (2026-08-18) [RESOLVED]
**Problem**: Checking a change in the GUI's Log tab, nothing from the worker appeared at all — not the capture lines under test, not the untouched handshake lines. Only the UI thread's own lines and the ephemeris worker's showed.
**Root cause**: Started from Explorer the process has no console, so `stdout` has no descriptor: `_fileno` returns negative, every `_dup2` in the redirect fails silently, and the pipe is created and pumped for the whole session with nothing ever written into it. Every previous check of that window had been launched from a shell — including mine — where the streams already have a descriptor.
**Fix**: Attach the streams to `NUL` before redirecting. **Verify a GUI from the launcher its users use**; a terminal-launched Win32 GUI is a different program in every respect that touches stdio.

### Making a channel work exposes what it was hiding (2026-08-18) [RESOLVED]
**Problem**: With the log pipe finally carrying text, the window filled with one run-on line of message-type numbers, burying every event.
**Root cause**: Two faults that had been latent behind the dead pipe. The pipe is text-mode at *both* ends, so the write side's `\n`→`\r\n` is folded straight back by the read side and an EDIT control — which breaks only on CR LF — renders a whole session as one line. And a per-frame type trace plus a `Sent GGA` every few seconds had never been seen by anyone.
**Fix**: Expand at the single point pipe text enters the control; delete the traces. **A dead channel hides every bug downstream of it** — expect a queue of them when it starts working, and budget for that rather than treating them as new regressions.

### A binary pulled through `adb shell` came back corrupted (2026-08-20)
**Problem**: The shared plot PNG, pulled with `adb shell run-as … cat > plot.png`, would not open. Its signature read `89 50 4E 47 0D 0D 0A 1A` — the PNG magic with an extra `0D` — and the local file was 949 bytes larger than the one on the phone.
**Root cause**: `adb shell` allocates a PTY, which translates `LF` to `CRLF`. Every `0x0A` in the image gained a `0x0D`. Nothing reports this: `cat` succeeds, the file arrives, only its content is wrong.
**Fix**: `adb exec-out` for anything that is not text. Same family as the path-rewriting entry above — **the shell between you and the device edits what passes through it**, in both directions.

### A recording contains only what it draws (2026-08-20)
**Problem**: The plot shared from the analysis screen came out on a transparent background — fine in a gallery, invisible ink on a white page.
**Root cause**: `rememberGraphicsLayer().record {}` captures the subtree it wraps. The surface behind that subtree is painted by the `Scaffold`, outside the layer, so every pixel the plot did not draw stayed clear. The screen looked right the whole time; only the export was wrong.
**Fix**: `drawRect(surface)` inside `record {}`, before `drawContent()`. **A subtree capture is not a screenshot** — it has no background unless the recorded subtree paints one, and the on-screen appearance cannot tell you whether it does.

### A tap aimed from an old screenshot hits whatever moved into the spot (2026-08-20) [x2]
**Problem**: Driving the app over `adb`, a tap meant for the share control opened the project wiki in Chrome. Earlier in the project the same habit silently changed the active mountpoint.
**Root cause**: Coordinates read off a screenshot taken before the layout changed. On the analysis bar, share sits at x≈603 and the orbit badge at x≈996; a verdict card growing by two lines moves everything below it.
**Fix**: `MSYS_NO_PATHCONV=1 adb shell uiautomator dump /sdcard/ui.xml`, read the `bounds` of the node with the right `text` or `content-desc`, tap its centre — and screenshot after every tap that was supposed to change the screen.

### A claim's own check broke the build that checks claims (2026-08-20)
**Problem**: Correcting a stale sentence in `MEMORY.md` -- the v3.5.0 release is published, not a draft -- came with a verify command that used `gh`. CI went red on a docs-only commit: *"To use GitHub CLI in a GitHub Actions workflow, set the GH_TOKEN environment variable."*
**Root cause**: `tools/verify_memory.py` has two tiers, and its own header says so: `verify:` runs on every push under `--offline`, `verify-net:` is deferred to the Monday job, which sets `GH_TOKEN`. The check was written as `verify:` without reading how checks are classified, so a network call ran in the tier that has no network credentials.
**Fix**: `verify-net:`. Offline is now 14 pass / 0 fail with 2 network claims deferred; the full run is 16 / 0. **When adding evidence to a claim, run the harness the way CI runs it** -- `python tools/verify_memory.py --offline` -- not just the way that suits the desk you are sitting at.

### A third build system, and only one of them was checked (2026-08-22) [RESOLVED]
**Problem**: P4.3 added `src/core/ns_failure.c` to `CMakeLists.txt`, and every desktop target built. CI went red on **Android** — `ld.lld: error: undefined symbol: ns_failure_text` — and stayed red for two commits, because the next step's verification was also desktop-only.
**Root cause**: `android/app/src/main/cpp/CMakeLists.txt` keeps its own list of the shared C sources. This project's promoted pattern says *where two build systems describe one source set, CI must run both*; the Android NDK build is a **third**, over the same `src/`, and "every target builds" from the desktop tree says nothing about it.
**Fix**: the file was added to the NDK list in P5.1, where the same linker error appeared locally -- and in P6.1 the class was closed rather than remembered. `tools/check_release.py` now compares the two source lists and fails naming the file and the list it is missing from; deliberate omissions are declared there with their reason. Falsified by deleting the entry again, which reproduces the CI failure in a second instead of in a run.

### A layout that reports less than it was asked for is centred (2026-08-22)
**Problem**: The station screen drew its cards floating in the middle: a band of empty space under the title, and as much again above the analysis bar. Only on a hub with few cards, and only on a screen taller than its content -- so the phone with the smaller window never showed it.
**Root cause**: `StationHub` is a custom `Layout` inside `Modifier.fillMaxSize().verticalScroll(...)`. In that order the scroll hands the layout a **minimum height of the whole viewport**, and the layout reported the height of its content instead. Compose centres a child that comes back smaller than the minimum it was given, which is why the slack appeared *above* the content as well as below.
**Fix**: three attempts, and only the third was the cause. `.coerceAtLeast(constraints.minHeight)` stopped the *centring* but left the hub draggable inside its own slack -- the author reported the fault again, correctly. `fillMaxWidth()` in place of `fillMaxSize()` removed the viewport-sized minimum, so the hub is as tall as its content. And the margins moved *inside* the scroll, because outside it they are a frame the content can never fill: a strip of nothing under the app bar and another above the pinned bar, on every screen, for ever.

Found by probe rather than by reading: a background colour showed the empty band was outside the hub, `onGloballyPositioned` put the hub's top at 711 px where its padding said 338, and logging the children's heights gave 860 px of content in a 1700 px viewport -- the difference, halved, was the band. **When a layout is placed somewhere unexpected, ask it where it is** rather than deducing it from the modifier chain. And when a fix is reported as not working, believe the report: twice here the symptom was still there because only part of the cause had gone.

### A redaction that moved off the thing it hid (2026-08-22) [RESOLVED]
**Problem**: `make_store_shots.py` frames captures for the Play listing and paints over two things: the caster's real address and the station's ARP to six decimals. Re-run against GUI v3 captures, it painted `ntrip.example.com:2101` into the gap *above* the connection tile -- leaving the real host readable below it -- and `52,xxxxxx, 5,xxxxxx` across the constellation legend, leaving the real coordinates untouched one line up. Both files were written to `docs/images/store/pro/`, bound for a public listing.
**Root cause**: the boxes are fixed pixel positions, measured against the v2 layout. The file's own comment warned about exactly this -- *"Re-measure if the layout changes; a box that has drifted paints over the wrong line"* -- and a warning in a comment is obeyed until the day nobody reads it.
**Fix**: boxes re-measured for v3, and the class closed rather than re-warned. The tool now refuses to write when a box does not cover **grey ink**: there must be text under it, and it must not be coloured. A first attempt only checked for *ink* and passed happily with the box sitting on the legend -- the legend is ink. Both conditions were needed, and the second is the one that catches a drift onto a neighbouring line. Every box is checked before the first file is written, because a half-updated directory is the state most likely to be uploaded unnoticed. Falsified by restoring the v2 box: the run stops, names the box and the capture, and leaves the existing screenshots alone.

### A script truncated a file before deciding what to write (2026-08-22)
**Problem**: `MainActivity.kt` -- 627 lines -- became a zero-byte file in the middle of a step.
**Root cause**: an edit helper written as `io.open(p, 'w').write(f(s))`. Python opens the file, which truncates it, *before* evaluating `f(s)`; `f` raised on a failed assertion and the write never happened. The assertion was right -- the anchor matched three places -- so the guard fired and destroyed the file it was guarding.
**Fix**: compute the new text first, then open for writing. Recovered with `git show HEAD:path` rather than `git checkout`, line endings converted back to CRLF by hand and confirmed byte-identical with `git diff`. Fifth instance of the promoted *scripted edits corrupt what they rewrite* pattern, and the first where the corruption was total.

### The phone is somebody's phone, not a test fixture (2026-08-22)
**Problem**: `adb shell pm clear` on the pro edition, run to get a clean "nothing measured yet" screen for a screenshot, wiped the author's caster, mountpoint, username and password. No backup exists: the store is encrypted per install.
**Root cause**: treating the handset as scratch space. The command was the shortest route to the state I wanted, and its other effect was somebody else's configuration.
**Fix**: nothing could restore it; the author retyped it. **Reach for the state, not the reset** -- force-stop clears a run without clearing settings, a spare profile gives pro a blank hub, and a release-signed build upgrades in place where a debug build demands an uninstall. The rule held the second time the same day: free on the S23 is Play's copy, so installing over it would have taken its configuration with it, and that one was handed to the author instead.

### Play got a binary the tag never contained (2026-08-23) [RESOLVED]
**Problem**: free 3.7.0, installed from Play on an S23, drew the sky view's coordinate line and constellation legend behind the system's navigation buttons -- the exact fault fixed and verified before the release was tagged.
**Root cause**: the Android release is built by two commands, `assembleFreeRelease` for the APK and `bundleFreeRelease` for the bundle Play takes. After the fix landed, "rebuild the assets" rebuilt the APKs (17:27) and not the bundle (12:43, four commits earlier). The tag contained the fix, the GitHub APK contained the fix, and the artefact a thousand people install did not. Nothing compared the file with the source it claimed to come from -- the ninth instance of *what is installed is not what was built*, and the first where the mismatch reached users.
**Fix**: `tools/check_release.py` gained two comparisons per artefact under `app/build/outputs`: it must be newer than every source it is built from, and its packaged manifest must declare this tree's version. Falsified by dating the bundle back to 12:43, which fails the check by name. The runbook now builds APKs and bundles in one command and says why they may not be built apart. Released as 3.7.1.

### Nine hours of capture, ten minutes of record (2026-08-23) [RESOLVED]
**Problem**: an overnight watch drew trails a few minutes long and a C/N0 scatter of 25 412 samples, where nine hours at forty satellites a second is over a million. Rotating the phone reset both plots to zero.
**Root cause**: both accumulators were `remember { }` inside the composition that draws them, while the run lives in the service's companion. A rotation destroyed them and the run-start effect then cleared them; worse, the document is collected with `collectAsStateWithLifecycle`, so with the activity stopped **no document reached the UI at all** and nothing accumulated. The plots were honest about far less than the run.
**Fix**: the record belongs to the run. Both accumulators moved to `MonitorService`'s companion, cleared at run start rather than at composition re-entry, fed where the document is published; satellites only the handset can place are still fed by the UI, which is the only side that has its fixes. Measured on hardware: 45 samples/s off screen against nothing before, and unchanged across a rotation. The C/N0 scatter had carried this since it was written and tracks inherited it by copying the precedent -- **a wrong precedent spreads by being followed**.

### A failed build, then a successful install (2026-08-23)
**Problem**: `assembleProRelease` failed on a locked `classes.dex`, and the `adb install` chained after it reported *Success*. The phone then ran the **previous** APK while I was about to verify a change that was not in it.
**Root cause**: `A && B` guards on the exit status of the command, not on the freshness of the file it leaves behind; a build that fails leaves the last good artefact exactly where the installer looks. Caught only because the APK's timestamp was checked before trusting it -- the same day, and the same shape, as Play receiving a bundle built four and a half hours before the fix it claimed to carry.
**Fix**: `./gradlew --stop`, rebuild, and **read the artefact's timestamp before installing it**. Fifth instance of *what is installed is not what was built*, and the second in one day.

### A line that fits in the file wraps on the phone (2026-08-23)
**Problem**: the new Watch-card line read *"2 reconnect(s) — the stream dropped and came back"*. In the strings file it looked like every other line; on a 1080-wide phone it wrapped, leaving "back" alone on a line while every other line in the card fitted.
**Root cause**: judging a rendered string by reading its source. Nothing in the file says how wide it is.
**Fix**: shortened to the shape of the line above it and measured from the accessibility tree -- 48 px, one line, the same height as a KPI row's evidence. Text is a rendered artefact like any other; measure it where it lands.

### A pause that lives in prose is not a checklist (2026-08-25) [x2]
**Problem**: two releases running (3.7.2, 3.7.3), the author pasted my release command list top to bottom and hit `no matches found` at the upload -- the desktop packaging step lived in a sentence ("tell me *tagged* and I'll package") between the commands, and sentences do not execute.
**Root cause**: handing over commands whose prerequisites do not yet exist, with the gap marked only in prose. The runbook's own order was correct both times; the handoff format was the bug.
**Fix**: release commands are handed over only when everything they name already exists on disk. Promoted below.

### The provenance flag outlived nothing; the cache outlived the run (2026-08-25) [RESOLVED]
**Problem**: the sky header credited a day-old navigation file for placements the ephemeris stream had made minutes earlier.
**Root cause**: source attribution by a per-run flag (`usedEphStream`, reset in `startRun`) over a cache that lives with the process. The flag's own doc comment stated the right rule and the reset defeated it. Family: the matrix's "a stale file used to read as a full cache", in the other direction.
**Fix**: the reset is deleted; a flag's lifetime must match the thing it describes. Reproduced against the fix in the author's exact scenario.

### A hidden button is a promise; a guarded door is a wall (2026-08-25) [RESOLVED]
**Problem**: starting a network-RTK check over a running test spawned a second worker and broke the measurement (author-reproduced).
**Root cause**: the UI hides run verbs while running, but `startRun`'s guard was a silent `worker != null` -- and the field survives thread death, so it could both let a race through and wedge shut after a crash.
**Fix**: `worker?.isAlive` with a logged refusal at the single door every run passes through. The service is not exported, so the door is only reachable from inside the app; the author's route retried green.

### touch is not enough when write and build share a second (2026-08-25) [x3]
**Problem**: three times in two days a test stayed red (or green) after a source change because make skipped recompiling -- the restore landed in the same clock second as the previous build and mtime comparison saw nothing.
**Root cause**: one-second mtime resolution on this MinGW/make setup; `touch` alone raced the same wall.
**Fix**: delete the object file (`rm build/CMakeFiles/<target>.dir/.../file.obj`) before rebuilding when a just-edited file's test result looks impossible. Candidate for promotion at next recurrence.

### The run is a copy of the settings, and the copy forgot a field (2026-08-25) [RESOLVED]
**Problem**: the app's first TLS run went out in plain text to port 443; Azure's gateway answered the plaintext with its own 400 page and the run failed as REJECTED while the checkbox sat ticked.
**Root cause**: settings travel to MonitorService by Intent extras, and `tls` had no extra -- the rebuilt CasterSettings defaulted it false. Third face of the flag-lifetime family: a field the copy forgets is a field the run silently does without.
**Fix**: EXTRA_TLS / EXTRA_EPH_TLS ride the Intent, with the lesson in a comment at the unpack site. Found by BLOG-instrumenting the bridge (`tls=0`).

### mbedTLS's entropy accumulator never returned on EMUI (2026-08-25) [RESOLVED]
**Problem**: `mbedtls_ctr_drbg_seed` blocked forever on the Huawei -- every TLS connect wedged before its first byte of I/O.
**Root cause**: mbedTLS's own entropy gathering (`mbedtls_entropy_func`) hangs on this EMUI 10 handset; mechanism unidentified, behaviour reproducible.
**Fix**: the DRBG seeds from the OS RNG directly (`/dev/urandom`, `BCryptGenRandom` on Windows) -- the OS pool is the root of trust either way, and the accumulator added nothing but the hang.

### -O0 crypto reads as a hang (2026-08-25) [RESOLVED]
**Problem**: after the entropy fix, handshakes still "wedged" -- in truth they took over a minute: unoptimized mbedTLS bignum on the 2019 Kirin.
**Root cause**: NDK debug builds compile C at -O0, and TLS is the first heavy math the debug APK ever ran.
**Fix**: `CMAKE_C_FLAGS_DEBUG += -O2` in the NDK CMakeLists -- the C core is debugged on the desktop through the suite, never through a debug APK. Cost an afternoon of misdiagnosis first.

### Gradle cannot see this repo's C sources (2026-08-25) [x3 in one day]
**Problem**: three rounds of "the fix didn't work" on the phone -- each time the installed .so predated the edit.
**Root cause**: the NDK sources live outside the Gradle project (`${REPO_ROOT}/src`), and both the externalNativeBuild task's up-to-date check and the build cache are blind to them: deleting `.cxx` alone re-served the stale build from cache.
**Fix**: `gradlew clean` + `--no-build-cache` whenever a `src/` or `lib/` C file changed. Promoted to the runbook's Common Problems.

### The Bash tool's heredocs halve backslashes (2026-08-25) [x4 in one day]
**Problem**: python edit scripts fed through `python - <<'EOF'` heredocs failed asserts on anchors that plainly existed -- and once *matched the wrong thing*: `'\0'` reached Python as a literal NUL byte.
**Root cause**: the tool layer processes backslash escapes in the command text before the shell sees it, even inside quoted heredocs; every `\` arrives halved. Recurrence of the promoted scripted-edit gotcha by a brand-new mechanism.
**Fix**: any edit script with a backslash in it is written to the scratchpad with the Write tool (verbatim by contract) and run as a file. Promoted table incremented; the CLAUDE.md constraint now names the mechanism.

### git's EOL conversion rewrote a checksummed vendored file (2026-08-25) [RESOLVED]
**Problem**: the regenerate-and-diff check went red on the CA bundle after the branch merge; the regenerated array had grown by 2,950 bytes and `cacert.pem` no longer matched curl's published SHA-256.
**Root cause**: text autocrlf converted the vendored PEM at checkout -- so the embedded trust store would differ between a Windows and a Linux checkout, and the provenance checksum was unverifiable on one of them.
**Fix**: `.gitattributes`: `cacert.pem -text` (upstream's exact bytes are the point), the generated array `eol=lf`; bytes restored from the index, sha256 matches curl again. Rule: a vendored file whose checksum is part of its provenance is binary to git, whatever it contains.

### A check that rewrites what another check reads (2026-08-25) [RESOLVED]
**Problem**: the first full release-check run on the 3.8.0 artefacts cried stale at bundles built one minute earlier.
**Root cause**: `check_generated` rewrites `ns_ca_bundle.c` to prove it matches the PEM -- same content, fresh mtime -- and the artefact-staleness check downstream reads mtimes. The notices.txt disease, second member.
**Fix**: `ns_ca_bundle.c` joined the GENERATED_SOURCES carve-out. Any future generated-and-committed source starts life in that set.

### SIGPIPE: the platform-specific death only CI could see (2026-08-25) [RESOLVED]
**Problem**: the TLS suite passed 16/16 on Windows and died of SIGPIPE on Linux -- twice, on the L5 and L6 pushes.
**Root cause**: TLS made writes to peer-closed sockets routine (close_notify at teardown, records to refusing casters); POSIX's default for those is process death, and Windows has no such signal to raise, so no local run could show it.
**Fix**: every transport send carries MSG_NOSIGNAL (which also retired the same latent exposure under every plain-text send the daemon ever made); the test ignores the signal for its server thread. The branch plan's draft-PR-for-CI rule caught it before the merge -- the discipline paying for itself.

### A parsed field nobody consumed (2026-08-25) [RESOLVED]
**Problem**: the first live TLS check scored frame integrity 79.6% -- Kadaster's 443 wraps NTRIP 2 in `Transfer-Encoding: chunked`, and the chunk-size lines were costing one frame in five its CRC.
**Root cause**: `ns_proto` had parsed `handshake.chunked` since the field existed and the GUI displayed it; nothing ever decoded the framing. A parsed-but-unconsumed field is a decision postponed until a live wire forces it.
**Fix**: the session de-chunks (gated on the handshake), the sourcetable fetch de-chunks in place, and the loopback caster serves chunked with boundaries cut mid-frame -- falsified by gating the decoder off, which reproduced the live corruption exactly.

### A recommendation dressed as research (2026-08-26)
**Problem**: the agent recommended a €12.99 price "mid-upper of the comparison set -- NTRIP clients and GNSS field tools sit at €5-20", and the author was one click from attaching that number to a real listing.
**Root cause**: the band was an impression, not a search. Nothing had been looked up; the phrase "comparison set" implied evidence that did not exist. The project has a verified-claims discipline for *state* ("shipped", "16 tests") and none for *recommendations*, which decay the same way and cost more.
**Fix**: the author asked for the real table, the search found there is **no paid NTRIP app market on Play at all** (clients free because they sell hardware; the nearest analogue, NtripChecker, free with 10,000+ installs; the next rung $100-300 survey suites), and the price became €5 on evidence. Promoted below: cite the basis or say plainly there isn't one.

### A release build cannot replace a debug build in place (2026-08-26)
**Problem**: `install -r` of pro's release APK over the Huawei's leftover debug build failed with `INSTALL_FAILED_UPDATE_INCOMPATIBLE`; the only route is uninstall, which wipes saved connections and credentials on a phone that holds the author's real data.
**Root cause**: different signing keys. A debug build left on a test phone is not a neutral state -- it is a one-way door that costs data to leave.
**Fix**: asked before uninstalling, then rebuilt the RFSEE01 profile from `bin/config.json` and **proved the retyped password by running a check** rather than asserting it. Rule: after a debugging session, put the release build back the same day, while its settings are still cheap to restore.

### The device is not the same device twice (2026-08-26) [x2]
**Problem**: `adb shell screenrecord` -- "inaccessible or not found" on the Huawei; and the same command wrote nothing because Git Bash rewrote `/sdcard/fsvc.mp4` into `C:/Program Files/Git/sdcard/fsvc.mp4`.
**Root cause**: EMUI ships no `screenrecord` binary (Samsung does), and MSYS path conversion mangles absolute device paths that look like POSIX paths.
**Fix**: record on the S23; `export MSYS_NO_PATHCONV=1` for every adb command naming a device path.

### A screen recording carries whatever the shade holds (2026-08-26)
**Problem**: a video destined for Google -- and an unlisted YouTube upload -- pulled down the notification shade twice, capturing the author's Google Payments mail about verifying a bank account, and on the retake the phone's own charging and USB notifications.
**Root cause**: demonstrating an ongoing notification means showing the shade, and the shade is not ours.
**Fix**: inspect the shade with a screenshot **before** recording, never after; and record **unplugged over Wi-Fi adb** (`adb tcpip 5555`), which removes the charging notifications the cable itself creates. Screen timeout and battery saver are raised for the take and restored afterwards, both values read first.

### A layout that depends on a string must measure the string (2026-08-26) [RESOLVED]
**Problem**: pro's Play feature graphic read "NTRIP Analyser P" -- title and tagline both ran off the 1024x500 canvas. Caught by the author's eye at upload, not by any check.
**Root cause**: `make_feature_graphic.py` chose font sizes as fractions of the canvas height, sized against free's shorter title, and never measured the text it drew. Its own docstring promised "everything sits inside a wide safe area".
**Fix**: fit the text to the available width and **assert** both lines fit before saving. Free's graphic and the social preview regenerated byte-identical, which is the fitter proving it shrinks only what overflows.

### Play Console mechanics that cost a step each (2026-08-26)
**Problem**: four wrong assumptions in one sitting -- that the package name binds at first upload, that the price could wait until production, that a bundle uploaded once is available to every track, and that a version code can be uploaded twice.
**Root cause**: none of it is guessable; all of it is stated by the forms.
**Fix**: package binds **at app creation** and is permanent; the price is padlocked behind a **seller/payments account** and blocks rollout to *any* track including closed; **tracks do not share releases** (an empty internal release reads as three separate errors); a version code exists once per app, so a second track attaches it with **"Toevoegen vanuit bibliotheek"**. Recorded in `design/work-items/pro-to-play.md` S3.
**Added 2026-09-16**: the console's menu moved -- *Beleid en programma's* now sits under *Controleren en verbeteren*, and a completed declaration shows only on App-content's *Actie ondernomen* tab while the default tab reads "Je bent helemaal bij". A remembered route was out of date; the route that works is in `pro-to-play.md` S5. Ask for a screenshot of the menu rather than recite one.

### A verify command that only works on a desk (2026-08-26) [RESOLVED]
**Problem**: CI was red for two days, on two pushes, and neither of us looked. The failing check was `git rev-parse v3.8.0`, the evidence behind "3.8.0 released" in the memory index.
**Root cause**: the claim was true and the checker's environment was not -- `actions/checkout` fetches no tags by default, so the tag existed on the remote, resolved on every desk, and was invisible to the runner. A guard that fails on a good tree is the exact failure mode this project already has a comment about.
**Fix**: both checkouts that run `verify_memory.py` now use `fetch-depth: 0`. Rule for a new verify command: it has to pass **in CI**, not only where it was written -- and after adding one, watch the run it first appears in.

### The console's error banner threw away the reason (2026-09-09)
**Problem**: pro's price refused to save for two weeks behind `Je wijzigingen kunnen niet worden opgeslagen`, with nothing marked on the page. Three sittings went into guessing -- the payments profile, per-country minima, the free/paid flag -- and the closed test stayed blocked throughout.
**Root cause**: the console discards what its own API returns. `UpdateAppPrice` answers HTTP 400 with a localised, actionable sentence: `Verwijder Rest van de wereld als je je app betaald wilt maken`, and then the same for China, Cuba, Iran and Soedan.
**Fix**: read the failed request's response before theorising -- one HAR ended two weeks of it. The substance is worth keeping too: **a paid app cannot target "Rest of the world" or the embargoed countries**, and they must come out of the **tracks'** country lists as well as the price table; clearing the price page alone does not satisfy it. €5 saved 2026-09-09 once all five were gone.

### Support answered a question about someone else's app (2026-09-09)
**Problem**: Google support explained that an app saved as free can never become paid and that pro needed a new package name. The agent accepted it, told the author the package was burned, and sized a rename across six files.
**Root cause**: a canned reply matched to the phrase "cannot set a price", not to this app. The console's own pricing page said `Je app is: Betaald`, with `Je app gratis maken` beside it and `wijzigen van betaald in kosteloos totdat je publiceert` above -- which also proved pro had never been published. The author doubted the diagnosis; the agent had not looked.
**Fix**: the console page is the state, a support reply is only a claim about it. Read the page that holds the setting before acting on any answer about it.

### A purchase flow is not a charge (2026-09-09)
**Problem**: the first real tester of pro's paid closed test was reported as charged €5. The agent recorded the plan's licence-testing assumption as falsified, rewrote three sections of the work item and told the author not to invite the 14. The payment method had been **"Testkaart, wordt altijd goedgekeurd"** -- Google's test instrument. No money moved; licence testing had worked all along.
**Root cause**: the agent escalated to falsification on one word of a report without asking what the checkout actually showed. Both of the day's earlier reversals had the same shape: acting on a report *about* an artefact instead of on the artefact.
**Fix**: read the instrument, not the flow. Two things are worth keeping from the false alarm -- the author's own install can never fail this check (a developer is not charged for their own app), and **licence testing runs a real-looking checkout at the real price**, so the tester invitation must say so or an unwarned tester stops at it. The reverse arrived a day later: a Google AI Overview said flatly that closed-test testers must buy a paid app, against an order at `Totaal EUR 0,00` already read in the console. **A generated summary does not outrank an artefact.** Settled by the tester's own screen: "Dit is een testbestelling. Er worden geen kosten in rekening gebracht."

### A deliberate fix removed because it looked out of place (2026-09-16)
**Problem**: rewriting free's first production release note, the agent struck the line *"Try: use caster.centipede.fr, port 2101, mountpoint NEAR…"* as a quick-start that did not belong in a release note and as load on a volunteer network. The same day, the production release was rejected for missing login credentials.
**Root cause**: the line was the 2026-08-14 fix for exactly that rejection -- a public anonymous caster placed where a human reviewer reads -- and this log said so. The agent judged the line by how it read, not by why it was there, and never searched the log. Whether its removal decided the rejection is unproven (the email cites an automated system and a screenshot); that it was a fix removed unread is not.
**Fix**: before recommending the removal of anything that looks odd -- a line, a flag, a workaround -- search `memory/gotcha-log.md` and the history for why it exists. Something strange that survives in a carefully kept project usually has a reason written down.

### Read the wiring, missed the gate (2026-09-13)
**Problem**: assessing GH#5, the agent told the author free could already export its settings -- *Save config* was wired in `MainActivity.kt` and passed through `Shell.kt` with no `Features` check -- and built its interim recommendation ("document the manual route") on that. The author knew better: free has no such row.
**Root cause**: the gate is one file further on, `AppMenu` in `Dialogs.kt`, `if (Features.IS_PRO)` around both config rows. `Shell.kt`'s own comment names it (*"AppMenu in Dialogs.kt, which gates the configuration rows on the edition"*); the agent read the comment and not the gate.
**Fix**: a capability claim about an edition is settled where the row is *drawn*, not where its action is *wired*. Grep `Features.` at the composable that renders the control before saying an edition has it.

### MSYS_NO_PATHCONV fixes the device path and breaks the local one (2026-09-13)
**Problem**: pulling both editions' APKs for the signing-certificate check, `adb pull /data/app/...` first failed because Git Bash rewrote the device path; with `MSYS_NO_PATHCONV=1` it failed again because the *local* destination `/c/Users/...` now reached `adb.exe` unconverted. Then `apksigner` given the same `/c/` path printed nothing at all -- its error swallowed by the `grep` it was piped into, which read as "no certificate lines".
**Root cause**: the variable switches conversion off for every argument, and Windows tools cannot resolve MSYS paths. A filter after a failing tool turns an error into an empty match.
**Fix**: with `MSYS_NO_PATHCONV=1`, pass local paths in Windows form (`C:/Users/...`). Run a tool unfiltered once before trusting a filtered empty result.

### A character count that fit was rejected (2026-09-16)
**Problem**: free's Dutch production release note measured 500 characters with `wc -m` against Play's 500 cap; Play refused it as too long.
**Root cause**: Play counts the field differently from `wc -m` -- line breaks are the likely difference.
**Fix**: keep real margin (the note shipped at 474), and write capped Play fields as one paragraph where the form allows it; the reviewer-instructions field went in that way at 443 and 464.

### The session's worktree was reassigned mid-session (2026-09-09)
**Problem**: the session's worktree was recycled by the pool onto a detached commit from August (`e7a8757`), while the session kept working for days.
**Root cause**: worktrees are pooled; nothing about the session's own state changes when one is reassigned.
**Fix**: before any edit, check `git rev-parse --abbrev-ref HEAD` and the log of the tree being edited. Here every edit went to the main checkout, confirmed on `main` each time.

### Checking the claims dirtied the tree (2026-09-17)
**Problem**: `/curate` ran `tools/verify_memory.py`; afterwards `notices.txt` and `THIRD-PARTY-NOTICES.txt` showed as modified, though nobody had edited them and `git diff` was empty.
**Root cause**: one claim's verify command runs `check_release.py`, which regenerates the notices to compare them. The generator writes LF; this checkout has `core.autocrlf=true`; and unlike `ns_ca_bundle.c`, neither notices file has a `.gitattributes` rule. Same content, different bytes, a dirty status. The 2026-08-25 entry's fix (`GENERATED_SOURCES`) cured the staleness face of this, not the line-ending face.
**Fix**: both generated notices now carry `text eol=lf` in `.gitattributes` (2026-09-17), as the CA bundle already did, so the generator's bytes and the checkout's agree.

### A public document advertised an account that never locks out (2026-09-17)
**Problem**: the reviewer-credential procedure, committed and pushed, told anyone reading the repository that the author's caster had an account with "no geo-IP restriction or rate banning". The author asked whether the procedure exposed anything; it did not expose the credentials, but it did expose the account's weakest property.
**Root cause**: the advice optimised for the reviewer's convenience and was written for a private reader, into a public file.
**Fix**: rate limiting stays on, set loose; the account gets an unguessable name, a long random password and access to the review mountpoints only; all three documents reworded. Before writing operational detail into this repository, ask what it tells an attacker -- the repo is public, and "how our accounts are configured" is part of the attack surface even with no secret in it.

### CI died in a third-party action's default, not in our code (2026-09-17)
**Problem**: the curate commit's CI failed in *Both editions* before any build step: `android-actions/setup-android@v3` ran `sdkmanager tools` and got "Failed to find package 'tools'". The scheduled run three days earlier had passed.
**Root cause**: the runner image moved to cmdline-tools 16.0, which no longer carries the legacy `tools` package, and the action's `packages` input defaults to `'tools platform-tools'` (read from its own `action.yml`). A default nobody wrote down broke on a change nobody made here.
**Fix**: `packages: platform-tools` in `ci.yml`. When CI fails on a commit that touched no code, read which step failed before reading the commit.

### A handed-over command written for the wrong shell (2026-10-09)
**Problem**: the agent offered a commit message as a Bash heredoc (`git commit -F- <<'MSG' ... MSG`) to an author working at a PowerShell prompt, with the PowerShell alternative mentioned underneath. PowerShell has no heredoc of that form, so the wrapper line became the commit's subject: `git commit -F- <<'MSG' Plan: desktop first, ...`, with `MSG` as the last body line. The body survived; the subject is shell syntax, and fixing it needed an amend and a force-push of an already-pushed commit.
**Root cause**: the agent writes its own commands for the Bash tool and handed one over in the same dialect. The environment says PowerShell is the author's primary shell; the caveat was written but placed after the broken version, which is the same as not writing it.
**Fix**: a command handed to the author is written for **their** shell, not the agent's. For commit messages that means `git commit` with the editor, or `-m` subject and `-m` body — and then the text must survive PowerShell quoting: no apostrophes or double quotes inside a `-m` string, or they bite next. Heredocs only when the author is demonstrably in Git Bash. Same family as the shell-in-the-middle entries below, one layer further out: there the shell edited what reached the device, here it edited what reached git.

### Numbers only a person reads are numbers nothing checks (2026-10-09)
**Problem**: the message-detail decoder has printed MSM7 pseudoranges **5.6x** too large and phases **1.4x** too large since it was written, by applying the phase rate's `0.0001` scale to all three fields where RTCM gives 2^-29 ms and 2^-31 ms. The MSM4 path was ~12% out on both for the same reason, and QZSS 1124 -- an MSM4 -- was read with MSM6's 20- and 24-bit widths, so those cells came from the wrong bits entirely. Found while reading the layout to build `msm_extract_obs`, not by any check.
**Root cause**: the columns feed the GUI's detail view and the CLI's verbose output and nothing else. Every tested path reads C/N0, whose scale is right. A field no measurement consumes has no guard, and the three wrong constants sat beside a correct one for months.
**Fix**: the scales are now named constants used by both decoders, and `test_msm_cnr.c` renders a built frame through `rtcm_set_output_buffer` -- the GUI's own capture path -- and asserts the printed strings against values it computes from RTCM's scales, including a check that the old scale does *not* appear. Falsified by restoring `0.0001`: two checks red. **If a number is only ever read by a human, write the test that reads it like one.**

### The reference implementation accused the wrong code (2026-10-09)
**Problem**: the first run of `test/manual/sv_state_vs_rtklib.c` -- P2's cross-check against RTKLIB -- reported our propagator 1 800 km out across the whole fit interval, while the clock agreed to 0.04 ns. A real 1 800 km error would mean the sky plot has pointed at the wrong satellites since it was written.
**Root cause**: the harness, not the propagator. RTKLIB's `eph_t` stores toe twice -- an absolute `gtime_t` and `toes` in seconds of week -- and the longitude-of-node term reads `toes`. The harness set only the first, so RTKLIB dropped `-omge * toe` and rotated its *own* orbit by 25 radians. With `toes` set: 0.0000 m.
**Fix**: the shape of the disagreement was the clue, and is the lesson. The clock agreed, and a rotation about Z preserves both `|r|` and `r.v` -- so the pattern said "rotated frame", not "wrong orbit". **Read what the shape of a disagreement rules out before deciding which side is wrong**: a comparison tool is code too, and it is the code that was written five minutes ago.

### A politely written test defended nothing (2026-10-09)
**Problem**: P5's falsification removed the `has_vel` gate from the report's velocity accumulation -- the gate that stops a stream carrying no phase-range rates from publishing a speed -- and the case named "but a stream without rates reports no speed" stayed green. The check had passed every run since it was written, and was protecting nothing.
**Root cause**: the test built its snapshot the way a well-behaved session does. `has_vel = false` was set and `selfpos_speed_mms` was left at its `NS_UNSET` initialiser, so the comparison rejected the sample on the sentinel and never consulted the flag. The test proved that *the caller blanks the field*, which is the caller's manners, not the report's defence.
**Fix**: make the input hostile. The snapshot now carries 7 mm/s *beside* `has_vel = false` -- a number it has no right to -- and the gate is the only thing that can stop it; the falsification then reddens the case by name. **A test must feed the input the defence exists for, not the input a cooperating caller would send** -- the same shape as P4's too-uniform ephemerides, one layer up: there the inputs were identical where they should have differed, here the input was correct where it should have been wrong.

### The fields were filled and never published (2026-10-09)
**Problem**: P5 claimed nine `selfpos_*` snapshot fields "serialised under `selfpos_*`". The report's serialiser carried them; `ns_stats_to_json()` and the CSV row carried none of them, so the daemon's snapshot, `File > Export Statistics` and every CSV reader described a stream without ever mentioning where it put itself. Found a step later, while wiring the GUI window that needed them.
**Root cause**: two serialisers, one claim. The work went into `station_report.c` and the sentence that was written covered both, because both emit keys of that name. The field block's own Doxygen said "consumers read by key, so the schema version stays" -- a promise about a serialiser nobody had touched.
**Fix**: both serialisers, same null-not-zero rule, four new cases covering nulls *and* values. The lesson is narrower than "a field nothing fills": **a field that one surface publishes is not a field that is published**, and a commit message that names the measurement rather than the surface cannot be checked against either. Name the surface in the claim, and the reader of the next step will notice when one is missing.

### The test harness was looking at another desktop (2026-10-09)
**Problem**: verifying the new GUI window from a launcher with no console, `FindWindow("NtripSelfPosClass")` returned 0 after the menu command -- and so did `FindWindow` for the *main* window's class, while `GetClassNameA` read that very class off the live handle. It read exactly like "the window was never created".
**Root cause**: two independent traps, each enough on its own. `FindWindow` and `EnumWindows` search the **calling thread's desktop**, and this host runs on a different one from the process it launched; a `SendMessage` to a handle already in hand crosses regardless, which is why the app still obeyed `WM_CLOSE`. And in the enumeration that replaced them, PowerShell's `Write-Output` inside a scriptblock used as a delegate becomes the delegate's **return value**: the lines never printed, and the callback's non-false return stopped the walk at the first window.
**Fix**: `EnumThreadWindows` (thread-scoped, not desktop-scoped) and `[Console]::WriteLine` inside the callback. Then the window appeared: `class='NtripSelfPosClass' title='Self-position' visible=True`. **When a check says the thing does not exist, prove the check can see a thing that does** -- here, the main window, which was provably there and equally invisible to the same call.

### A verdict on a station, from thirty-two seconds (2026-10-09)
**Problem**: the new Self-position window, opened on a stream 32 s old, filled its banner with **NOT COMPUTABLE**. Nothing had solved because nothing had had time to. The author's one-word reply to the screenshot was "not good", and it was right: that is a finding about the station, stated from a window far too short to carry one.
**Root cause**: the window had two states where it needed four. Anything that was not "solved" was presented as "cannot be solved", so `SPP_NO_EPOCH` -- the ordinary condition for the first seconds of every run -- wore the same words as `SPP_SINGLE_FREQ`, which is the one status that really is a property of the stream. **This is `gui-design.md` §14.5's second entry again**: the Stability window said "no dual-frequency pair to measure with" at twenty-five seconds, and the fix there was to wait until the window was judgeable. Same author, same mistake, a new window, a month later -- and the lesson had been written down in the very document the new section was added to.
**Fix**: four states -- solved, gathering, not computable, not solved -- with "gathering" naming what is being waited for. The general rule, which is the one that keeps being relearned: **a screen may only say what its evidence supports, and "not yet" is evidence of nothing.** Writing that down in a design document did not stop it happening; the thing that caught it was a person looking at the window.

### Nothing a build can check is wrong on screen (2026-10-09)
**Problem**: the same window shipped a heading cut to "What it mea...", notes under a horizontal scrollbar, a caption severed mid-word, and ring labels reading "0.125 m" on a plot with no points in it. The build was warning-free and 19/19 tests passed throughout.
**Root cause**: three column widths guessed at `190 / 90 / 210` px -- against `gui-design.md` §14.6, *Column widths are measured, not guessed*, which the same session had just read -- a plot allowed half the window regardless of what the figures needed, a one-line `TextOut` for a sentence longer than the plot, and a radius chooser returning its smallest step for an empty set.
**Fix**: widths measured from the longest string each column can hold, the plot capped at what is left after the figures, `DrawText` with `DT_WORDBREAK | DT_END_ELLIPSIS`, and no ring labels until points set the scale. Then **look at it**: `PrintWindow` into a bitmap, saved as a PNG and read back, which is how the remaining three were found after the first was fixed. Two traps in that harness: call `SetProcessDPIAware()` first, or a 1000x620 window is reported and captured as 800x496 at 125 % scaling and the clipping looks like the bug; and the empty state is not the solved state, which still needs a live station.

### The feature was never called (2026-10-09)
**Problem**: connected to a real station, the Self-position window sat at `GATHERING` -- 170 s of stream, 157 snapshots, 0 epochs solved, reason `no observations in this epoch`. That status is the field's *initial value*: the solve had never run. `selfpos_solve()` was defined in `ntrip_session.c` and called from nowhere, because the call site discarded the return value of `obs_feed()` -- the one thing that says an epoch has closed. The whole feature was dead through P5 **and** P6, two commits and two days of work on top of it.
**Root cause**: three failures that lined up.
  1. **Every test starts below the session.** `test_spp` builds its own epoch, `test_observables` its own payload, `test_station_report` and `test_ns_stats` their own snapshots. Nothing drove `ntrip_session` and asked whether a closed epoch reaches the solver. The suite was 19 green tests around a hole in the middle.
  2. **Every surface handled "nothing solved" correctly** -- null in the JSON, "nothing solved" in the CLI, a reason in the window. The feature's own care about absence is what made its absence look like a station's property.
  3. **The build asks the compiler almost nothing.** Flags were `-O3 -DNDEBUG -std=gnu99`: no `-Wall`, so `-Wunused-function` never fired on a static function nobody called. Proved after the fact: the committed file compiled with `-Wall` names it in one line.
**Fix**: the call site, plus the two defences that were missing. `-Wall` on our own targets (the codebase turned out clean under it, bar four real `strncpy` truncations in `gui_thread.c` and a tray tooltip, all fixed), and `test/test_selfpos_session.c`, which replays RTCM through `ns_open_file()` and asserts a closed epoch reaches the solve -- red on the shipped defect, with a one-bit control (DF393 set, epoch never closes) that must stay `SPP_NO_EPOCH`. **A test that starts below the wiring cannot see the wiring**, and "the build is warning-free" means nothing until you know what the build asks.

### Measured the way the build measures -- and then did it again (2026-10-09)
**Problem**: before adding `-Wall`, the codebase was swept for warnings with `gcc -Wall -fsyntax-only` and reported clean. The real `-O3` build then produced ten: six `-Wformat-truncation` and four `-Wstringop-truncation`.
**Root cause**: those warnings come from the optimiser's value range propagation, which `-fsyntax-only` never runs. **This is verbatim the 2026-08-15 entry** -- "`-fsyntax-only` is blind to the truncation warnings" -- promoted that day to `CLAUDE.md` as a hard constraint, and repeated here by the agent that had read it. (Earlier in the same sweep, `gcc` was not even on PATH in that shell: the failing command piped through `grep -c` printed `0` and read as "no warnings". The compiler the build uses is in `build/CMakeCache.txt`.)
**Fix**: measure with the flags, optimisation level and compiler the build actually uses -- `grep C_FLAGS build/CMakeFiles/<target>.dir/flags.make` says what those are -- and check the tool ran at all before believing a zero.

### The rule was written down and applied to one case (2026-10-09)
**Problem**: with the solve finally running against a real six-system station, it placed it **1.8 km** from its broadcast position: worst code residual 2.6 km, apparent motion 829 m/s, clock drift 393 m/s, 25 satellites at PDOP 0.9.
**Root cause**: the solver accepted every constellation but GLONASS and SBAS under **one** receiver-clock unknown. Its own header says why GLONASS is out -- *"FDMA inter-frequency biases will not fit under one receiver-clock unknown"* -- which is not a fact about FDMA: every system keeps its own time and its own hardware path, and multi-GNSS SPP therefore carries a clock unknown per system. BeiDou adds a second fault, BDT being 14 s behind GPS while a 1042 ephemeris dates its `toe` in BDT: ~55 km of satellite position. The rule that excluded one case was the rule that excluded all of them, written out in full, in the file, and generalised to nothing.
**Fix**: GPS only until the solve has a clock unknown per system, with the measured numbers at the filter so the next reader sees the evidence rather than the preference. **When a refusal is justified by a general principle, check which other cases the principle covers** -- the sentence naming the exclusion is the place to do it. And the test that missed it is the shape to remember: `test_spp.c` verified by closure against *one* synthetic constellation, building its measurements from the same single-clock model the solver inverts. A closure test cannot discover that the world has more clocks than the model.

### The linker could not write, and the check could not tell (2026-10-09)
**Problem**: the author was about to re-test the GPS-only fix against a live station on a GUI binary that did not contain it. `bin/ntrip-analyser-gui.exe` was fifteen minutes older than `src/core/spp.c`, while `bin/ntrip-analyser.exe` was current.
**Root cause**: two failures, one on each side.
  1. **Windows refuses to overwrite a running `.exe`**: `ld.exe: cannot open output file ...: Permission denied`. The author had the GUI open -- as anyone testing a GUI does -- so every build since relinked the CLI and left the GUI alone. Nothing in the normal workflow says so.
  2. **The check was blind to it.** Verification had been `cmake --build build 2>&1 | grep -cE "warning:"`, reported as "0 warnings". A link error prints `error`, and a pipe discards make's exit status in favour of grep's. The test suite does not build the GUI, so `20/20` stayed green over a GUI that had failed to link. Both halves of "0 warnings, 20/20 tests" were true and the conclusion was wrong.
**Fix**: take the build's **exit status** first and read warnings second -- `cmake --build build > log 2>&1; echo $?` -- and, when a GUI change is to be tested, compare the binary's timestamp against its sources before handing it over (`find src gui -newer bin/ntrip-analyser-gui.exe`). **Close the GUI before building it.** This is `What is installed is not what was built` in its sixth shape, and the third time in one day that a failing tool has read as a clean result -- `gcc` absent from PATH printing `0` through `grep -c`, `-fsyntax-only` missing ten warnings, and now a linker that could not write at all.

### The guard fired on the thing the protocol exists to express (2026-10-10)
**Problem**: a station streaming ten GPS satellites reported *fewer than four usable satellites*, 0 epochs solved in 106 s. `SPP_TOO_FEW_SATS` with both refusal counters at zero does not mean "few satellites": it means **not one cell was examined**.
**Root cause**: the base sends BeiDou in two frames -- 48 cells, then 5 -- which is what MSM's multiple-message bit is for. The epoch-boundary rule read the second frame as "this system has contributed before, so a new bundle has begun" and reset the set, throwing away GPS, GLONASS, Galileo and BeiDou's own first frame. Six cells of 128 reached the solver. The guard existed for a base whose multiple-message bit never clears, and it could not tell that case from a legitimate continuation.
**Fix**: compare where comparison is sound. The code already said epoch fields are not comparable *across* constellations and stopped one step short of the consequence: they are comparable **within** one. Same system and same epoch is a continuation; same system and a different epoch is the stuck-bit case. **A guard against a malformed stream must be written so it cannot fire on a well-formed one** -- and the thing it nearly always mistakes is the protocol feature it was never told about. Found only by walking a real capture frame by frame; every test in the suite sent one frame per system per epoch, which is the one shape that cannot show it.

### The epoch was dated by whichever frame happened to close it (2026-10-10)
**Problem**: a second live station solved every epoch and placed itself **7.8 km** away -- 3888 m worst code residual, 779 mm/s of apparent motion, 8 satellites at PDOP 1.1. Good geometry, impossible answer.
**Root cause**: the solve took its time from the frame that cleared DF393, and that frame is whatever the base chooses to send last. This one ends every bundle with BeiDou, whose epoch field is **BDT -- 14 s behind GPS**. Fourteen seconds moves a GPS satellite 55 km along its orbit, and what survives the receiver-clock unknown is kilometres of position and residual. The *other* station tested that day ends its bundles with NavIC, which is GPS-aligned, so identical code was correct there **by luck** -- which is exactly why one station looked plausible and the other did not.
**Fix**: convert in core where it can be tested -- `msm_epoch_to_gps_tow_ms()`: GPS, Galileo, QZSS, NavIC and SBAS as they are, BeiDou +14 s, GLONASS refused because day-of-week plus milliseconds of *day* cannot be placed in a GPS week without the date and the leap seconds. The epoch carries `tow_gps_ms`, taken from the first frame that can say rather than the last frame to arrive. **A value that arrives on several scales must be converted at the boundary, not consumed wherever it lands** -- and the clue was already in the code: the stream clock had been locking to one constellation's scale for months, with a comment explaining why the scales are not comparable. The solve two files away did not read it.

### The station stopped sending the thing the code was waiting for (2026-10-10)
**Problem**: an hour after the epoch-boundary rule was fixed and verified against RFSEE01, the same station stopped solving entirely: 41 snapshots, 0 epochs, *"waiting for the first observation epoch"*. Nothing had changed in the code since the run that worked.
**Root cause**: NavIC had dropped out of view. It had been the last frame of every bundle, and the only one clearing DF393 -- so with it went the signal the code waited on to close a set. Every remaining frame said "more follows" and no set ever closed. A 60 s capture confirmed it: **zero** DF393 clears in the whole file. The irony is that this is the exact case the original guard was written for ("a base whose multiple-message bit never clears") -- it detected the condition correctly and still never closed anything, because detection reset the set instead of completing it.
**Fix**: close the set when the next one demonstrably begins, not only when the protocol says so -- a bundle beginning is proof the previous one ended, and for some stations it is the only proof there will be. **A stream's shape is not a property of the station; it is a property of the sky at that moment.** Two runs an hour apart, same caster, same mountpoint, same code, different message set. Anything that waits for a particular frame must also have a way to proceed without it, and the third test case written against a live capture should be assumed to describe that hour rather than that station.

## Promoted

<!-- Track what has been promoted, so it is not promoted twice and so the loop
     is visibly working. Keep incrementing occurrences AFTER promotion: a
     promoted pattern that recurs means the promotion did not take. -->

| Date | Gotcha | Occurrences | Promoted to |
|------|--------|-------------|-------------|
| 2026-08-13 | A remembered value must not satisfy the KPI that asks for it | 1 | project file, hard constraint |
| 2026-08-13 | Judge constellations by NavSys, never the 1005/1006 bits | **3** — 2026-08-12 three times in one session | project file, domain facts; `memory/MEMORY.md` active decisions |
| 2026-08-14 | Scripted file edits corrupt what they rewrite — escapes, then line endings, then the whole file | **13** — heredoc 2026-08-12, doubled CRs and a literal newline 2026-08-14, `sed -i` deleting a table row 2026-08-16, four eaten backslashes and one truncated file 2026-08-22, **four halved-backslash heredocs 2026-08-25** (the tool halves `\\` before the shell sees it — one anchor became a literal NUL) | project file, hard constraint — scripts now go through Write to a file, never a heredoc |
| 2026-08-14 | A data property appears in every renderer, so fix it in all of them | **2** — Android 2026-08-13, GUI 2026-08-14 | `memory/MEMORY.md` active decisions |
| 2026-08-14 | Read the artefact; a toolchain's reputation is not evidence | **10** — 16 KB alignment, bundle ABIs, signing key (2026-08-14); wrapped strings on-device 2026-08-23 (reconnect line) and 2026-08-25 twice (export filename template, banner evidence line); **then four in a new shape, 2026-09-09 to -13: acting on a *report about* an artefact** — a support reply against the console's pricing page, "charged" against the checkout's test card, an AI Overview against a €0,00 order, and wiring against the gate in `Dialogs.kt`. The promotion names toolchains; the recurrences are second-hand accounts | project file, hard constraint — **widened 2026-09-17 to cover reports about an artefact** |
| 2026-08-15 | Measure the way the build measures, or report no number | **4** — `-fsyntax-only` blind to truncation warnings, `-std=c99` hiding `M_PI`, both 2026-08-15; **2026-10-09 the identical `-fsyntax-only` sweep, by an agent that had read the constraint, missed the same ten truncation warnings — and in the same sweep `gcc` was absent from PATH, so a failed command piped through `grep -c` reported `0` and read as clean** | project file, hard constraint |
| 2026-08-15 | Two build systems over one source set: CI must run both | **2, then closed** — `build-gui.bat` retired 2026-08-25 (TLS L2; it had been broken since L1), `service/Makefile` builds by wildcard | `memory/MEMORY.md` active decisions |
| 2026-08-25 | Gradle cannot see out-of-tree C sources — clean + `--no-build-cache` after any `src/` or `lib/` C change | **3** — three stale-`.so` rounds in one afternoon; deleting `.cxx` alone re-served the cache | `docs/RUNBOOK.md` Common Problems |
| 2026-08-16 | A snapshot field nothing fills is worse than a missing one | **3** — ARP fields (until a live run tripped over them), `latency_s` and `sourcetable_offset_m` (both found 2026-08-16) | `memory/MEMORY.md` active decisions |
| 2026-08-16 | A green verdict is not a correct registration — read the sourcetable | **2** — 3.3 km transposition and a 25 km paste, both STATION OK, 2026-08-16 | `memory/MEMORY.md` active decisions; specified as measurement-tiers phase 0 |
| 2026-08-16 | What is installed is not what was built — on any platform | **6** — pro's APK 2026-08-14, the VPS binary 2026-08-16, `test_all` green beside a stale `bin/` 2026-08-18, **Play's 3.7.0 bundle built before the fix it claimed to carry, and an install that succeeded from a failed build, both 2026-08-23**; **2026-10-09 the GUI could not relink because it was running, and "0 warnings, 20/20" was reported over the failure** | `memory/MEMORY.md` active decisions (generalised from Android); `tools/check_release.py` artefact checks |
| 2026-08-18 | A tag points at a commit, not at your working tree | **2** — v3.4.0 2026-08-15, v3.5.0 2026-08-18, both staged-not-committed, both caught by the packaging guard | `docs/RUNBOOK.md` release sequence, with the `git show <tag>:` check |
| 2026-08-18 | An entry is only `[RESOLVED]` when code or procedure changed | **1** — the v3.4.0 tag gotcha was retired on advice alone and recurred in three days | this log's header, promotion lifecycle |
| 2026-08-20 | The shell between you and the device edits what passes through it — and so does the one between you and the author | **4** — paths rewritten by Git Bash 2026-08-14 and again 2026-08-20, a PNG CRLF-mangled by the PTY 2026-08-20; 2026-09-13 the cure for the first broke the local path instead; **2026-10-09 a Bash heredoc handed to a PowerShell prompt wrote its own wrapper into a commit subject** | `memory/MEMORY.md` active decisions; `MSYS_NO_PATHCONV=1` for paths, `adb exec-out` for bytes |
| 2026-08-22 | The device under test holds the author's data | **2** — `pm clear` wiped pro's caster and credentials; free on the S23 was left alone for the same reason | `memory/MEMORY.md` active decisions |
| 2026-08-25 | A pause that lives in prose is not a checklist | **2** — 3.7.2 and 3.7.3 both hit `no matches found` at the upload because packaging lived in a sentence between the commands | `docs/RUNBOOK.md` release sequence; hand over commands only when everything they name exists |
| 2026-08-26 | Cite the basis, or say there isn't one | **1** — a €12.99 price "band" the agent had never looked up, nearly attached to a live listing; the real search found no paid NTRIP market at all | `CLAUDE.md` hard constraint |
| 2026-08-25 | A flag's lifetime must match the thing it describes | **2** — `usedEphStream` reset per run over a process-lived cache (2026-08-25); the same family as the matrix's stale-file-as-full-cache | `memory/MEMORY.md` active decisions |
| 2026-10-09 | A screen may only say what its evidence supports — "not yet" is evidence of nothing | **4** — "fewest held: 9" from a partial first epoch and "no dual-frequency pair" at 25 s (both 2026-08-18, fixed with `SR_WARMUP_S` and a judgeability gate); **NOT COMPUTABLE at 32 s in the new Self-position window, and ring labels of 0.125 m on a plot with no points, both 2026-10-09** — the second pair happened in a window added to the same design document that records the first | `design/gui-design.md` §14.5 and §16.5; `memory/MEMORY.md` active decisions |
| 2026-10-09 | A falsification that stays green accuses the test, not the code | **3** — synthetic ephemerides sharing one `af1` hid a deleted satellite clock-drift term (P4); a snapshot that blanked `selfpos_speed_mms` beside `has_vel = false` hid a deleted `has_vel` gate (P5); **and again in P6, hours after the promotion**, a snapshot left at its `NS_UNSET` initialisers hid a deleted status gate in the *snapshot* serialiser — the promotion did not stop the test being written politely; the falsification caught it | `memory/MEMORY.md` active decisions and Recently Promoted |
