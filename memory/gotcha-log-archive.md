# Gotcha Log — archive

Resolved entries, moved here from [gotcha-log.md](gotcha-log.md) under
the framework's archive rule (agent-ready-projects v1.49.2, #178):
moved, never summarised. Those dated before 2026-10 on 2026-10-14; every
resolved entry up to 2026-10-16 on 2026-10-17, by the author's decision
at that day's curation, when the read surface stood at 611k characters.
Open entries and the Promoted table stay in the log. Grep both files
for a mechanism.

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

### A property of the station looked like a defect in the app (2026-08-13) [x2] [RESOLVED]
**Retired 2026-10-14**: 1 dB cells in both renderers (`Views.kt`, `gui/gui_state.h` "One whole decibel per cell"); `checkEditionParity` fails the build if an edition grows code of its own.
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

### Heredocs corrupt C string literals (2026-08-12) [x5] [RESOLVED]
**Retired 2026-10-14**: `CLAUDE.md` Hard Constraints: no `sed -i`; a python edit script never travels through a heredoc — written to the scratchpad and run.
**Problem**: `\n` inside a heredoc-fed script arrives as a real newline, producing C source that no longer compiles.
**Fix**: Use file-editing tools for anything containing escapes, or write the script to a file first.
*Four more on 2026-08-22*, in one session and all the same shape: a `printf` in `cli_stream.c` that would not compile, and three error messages in `make_store_shots.py` broken identically. Each cost a build. The reliable dodge when a heredoc must be used is `chr(92) + 'n'`, which no shell can eat -- but the answer above was available every time, and this very entry could not be edited until it was applied.

### JAVA_HOME is Adoptium, not Microsoft (2026-08-12) [RESOLVED]
**Retired 2026-10-14**: `docs/RUNBOOK.md` names the Adoptium path in its prerequisites table and its Common Problems row.
**Problem**: Gradle: "JAVA_HOME is set to an invalid directory".
**Fix**: `C:\Program Files\Eclipse Adoptium\jdk-17.0.20.8-hotspot`, as a Windows path — Gradle rejects the POSIX form. Also on this machine: the only host C compiler is CodeBlocks' MinGW.

### A file rewritten with newline='\r\n' doubled every carriage return (2026-08-14) [RESOLVED]
**Retired 2026-10-14**: `CLAUDE.md` Hard Constraints, "read and write with `newline=''` and convert once". This entry's own `\r\n` had been turned into real line breaks, splitting the heading; repaired the same day.
**Problem**: 2014 lines of `MainActivity.kt` ended `\r\r\n`; subsequent edits stopped matching anything.
**Root cause**: The script converted LF to CRLF *and* opened the file with `newline='\r\n'`, which converts again on write.
**Fix**: Open with `newline=''` on both sides and do the conversion once, explicitly. Better: use the file-editing tools, which get this right.

### A link can be correct while its destination does not exist (2026-08-14) [RESOLVED]
**Retired 2026-10-14**: `tools/check_release.py` checks every wiki link names a page in `docs/wiki/`.
**Problem**: The app's new orbit badge opened the repository front page instead of the wiki page it names.
**Root cause**: GitHub creates the wiki *repository* when the first page is saved in the browser; enabling the wiki in Settings does not. Every link into an unborn wiki redirects.
**Fix**: `tools/publish_wiki.sh`, and a check that every in-app wiki link names a page that exists in `docs/wiki/`. The check cannot prove it was pushed — only the fetch can.

### GLONASS has no week, so a day-old orbit passed as an hour old (2026-08-14) [RESOLVED]
**Retired 2026-10-14**: `sv_ephemeris.c` judges age on `toe_utc`, pinned by `test/test_eph_validity.c`.
**Problem**: A ten-hour-old navigation file reported "newest orbit 58 min old", and a satellite could have been placed from a day-stale orbit.
**Root cause**: GLONASS `toe` is Moscow **seconds of day**. Every age and every validity test wraps at 86400 s, so yesterday's record lands a few hours behind now.
**Fix**: `SvEphemeris.toe_utc`, an absolute date, filled wherever one is known — any file carries a calendar datetime. Live streams keep the wrap; their records are seconds old. `test/test_eph_validity.c` pins both halves.

### The constraint against scripted rewrites, broken an hour after writing it (2026-08-14) [x3] [RESOLVED]
**Retired 2026-10-14**: `CLAUDE.md` Hard Constraints now carry the mechanism (heredoc ban, Write-then-run). Not encoded: "assert every substitution found its target".
**Problem**: A heredoc-fed script wrote `"
"` into `tools/verify_memory.py` as a literal newline, splitting a statement across two lines. Two other edits in the same script matched nothing and said nothing.
**Root cause**: The same escape-mangling promoted to a hard constraint that morning — and `.replace()` without an assertion, which cannot fail out loud.
**Fix**: Edit tools for anything with escapes; every scripted substitution asserts its target was found. A constraint is not learned until the next mistake is a different one.

### A navigation gesture eaten by an overscroll animation (2026-08-14) [RESOLVED]
**Retired 2026-10-14, obsolete**: the leave-gesture is gone and the pager keeps the platform's overscroll (`AnalysisScreen.kt`); no `LocalOverscrollConfiguration` remains in `android/app/src`.
**Problem**: On the S23, dragging right from the sky view no longer left the Analysis screen. The same build did it on the Android 10 handset.
**Root cause**: The screen reads what the pager *cannot* consume as "leave". From Android 12 the stretch overscroll consumes that leftover to animate with it, so `onPostScroll` saw nothing. The older glow effect only draws, which is why the gesture had always worked where it was tested.
**Fix**: `LocalOverscrollConfiguration provides null` around the pager — the stretch is worth less than the gesture. A nested-scroll parent only sees what every child declines to take.
**Lesson**: A gesture built on leftover deltas is a gesture built on what nobody else wanted, and that changes by platform version. Test navigation gestures on the newest Android available, not the oldest.

### An edition looked broken because its install was an hour old (2026-08-14) [x3] [RESOLVED]
**Retired 2026-10-14**: `docs/RUNBOOK.md` has the `dumpsys package … lastUpdateTime` step and the GUI binary-timestamp row. The VPS install is still advice only.
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

### A schema field nobody fills (2026-08-16) [x3] [RESOLVED]
**Retired 2026-10-14**: `tools/check_release.py` "no new snapshot field is left unfilled", with the known gaps listed by name.
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

### A recording contains only what it draws (2026-08-20) [RESOLVED]
**Retired 2026-10-14**: `AnalysisScreen.kt` draws the surface first inside `plotLayer.record {}`.
**Problem**: The plot shared from the analysis screen came out on a transparent background — fine in a gallery, invisible ink on a white page.
**Root cause**: `rememberGraphicsLayer().record {}` captures the subtree it wraps. The surface behind that subtree is painted by the `Scaffold`, outside the layer, so every pixel the plot did not draw stayed clear. The screen looked right the whole time; only the export was wrong.
**Fix**: `drawRect(surface)` inside `record {}`, before `drawContent()`. **A subtree capture is not a screenshot** — it has no background unless the recorded subtree paints one, and the on-screen appearance cannot tell you whether it does.

### A third build system, and only one of them was checked (2026-08-22) [RESOLVED]
**Problem**: P4.3 added `src/core/ns_failure.c` to `CMakeLists.txt`, and every desktop target built. CI went red on **Android** — `ld.lld: error: undefined symbol: ns_failure_text` — and stayed red for two commits, because the next step's verification was also desktop-only.
**Root cause**: `android/app/src/main/cpp/CMakeLists.txt` keeps its own list of the shared C sources. This project's promoted pattern says *where two build systems describe one source set, CI must run both*; the Android NDK build is a **third**, over the same `src/`, and "every target builds" from the desktop tree says nothing about it.
**Fix**: the file was added to the NDK list in P5.1, where the same linker error appeared locally -- and in P6.1 the class was closed rather than remembered. `tools/check_release.py` now compares the two source lists and fails naming the file and the list it is missing from; deliberate omissions are declared there with their reason. Falsified by deleting the entry again, which reproduces the CI failure in a second instead of in a run.

### A layout that reports less than it was asked for is centred (2026-08-22) [RESOLVED]
**Retired 2026-10-14**: `MainActivity.kt` `.fillMaxWidth()` with its reason, and `Panel.kt` `coerceAtLeast(constraints.minHeight)`.
**Problem**: The station screen drew its cards floating in the middle: a band of empty space under the title, and as much again above the analysis bar. Only on a hub with few cards, and only on a screen taller than its content -- so the phone with the smaller window never showed it.
**Root cause**: `StationHub` is a custom `Layout` inside `Modifier.fillMaxSize().verticalScroll(...)`. In that order the scroll hands the layout a **minimum height of the whole viewport**, and the layout reported the height of its content instead. Compose centres a child that comes back smaller than the minimum it was given, which is why the slack appeared *above* the content as well as below.
**Fix**: three attempts, and only the third was the cause. `.coerceAtLeast(constraints.minHeight)` stopped the *centring* but left the hub draggable inside its own slack -- the author reported the fault again, correctly. `fillMaxWidth()` in place of `fillMaxSize()` removed the viewport-sized minimum, so the hub is as tall as its content. And the margins moved *inside* the scroll, because outside it they are a frame the content can never fill: a strip of nothing under the app bar and another above the pinned bar, on every screen, for ever.

Found by probe rather than by reading: a background colour showed the empty band was outside the hub, `onGloballyPositioned` put the hub's top at 711 px where its padding said 338, and logging the children's heights gave 860 px of content in a 1700 px viewport -- the difference, halved, was the band. **When a layout is placed somewhere unexpected, ask it where it is** rather than deducing it from the modifier chain. And when a fix is reported as not working, believe the report: twice here the symptom was still there because only part of the cause had gone.

### A redaction that moved off the thing it hid (2026-08-22) [RESOLVED]
**Problem**: `make_store_shots.py` frames captures for the Play listing and paints over two things: the caster's real address and the station's ARP to six decimals. Re-run against GUI v3 captures, it painted `ntrip.example.com:2101` into the gap *above* the connection tile -- leaving the real host readable below it -- and `52,xxxxxx, 5,xxxxxx` across the constellation legend, leaving the real coordinates untouched one line up. Both files were written to `docs/images/store/pro/`, bound for a public listing.
**Root cause**: the boxes are fixed pixel positions, measured against the v2 layout. The file's own comment warned about exactly this -- *"Re-measure if the layout changes; a box that has drifted paints over the wrong line"* -- and a warning in a comment is obeyed until the day nobody reads it.
**Fix**: boxes re-measured for v3, and the class closed rather than re-warned. The tool now refuses to write when a box does not cover **grey ink**: there must be text under it, and it must not be coloured. A first attempt only checked for *ink* and passed happily with the box sitting on the legend -- the legend is ink. Both conditions were needed, and the second is the one that catches a drift onto a neighbouring line. Every box is checked before the first file is written, because a half-updated directory is the state most likely to be uploaded unnoticed. Falsified by restoring the v2 box: the run stops, names the box and the capture, and leaves the existing screenshots alone.

### Play got a binary the tag never contained (2026-08-23) [RESOLVED]
**Problem**: free 3.7.0, installed from Play on an S23, drew the sky view's coordinate line and constellation legend behind the system's navigation buttons -- the exact fault fixed and verified before the release was tagged.
**Root cause**: the Android release is built by two commands, `assembleFreeRelease` for the APK and `bundleFreeRelease` for the bundle Play takes. After the fix landed, "rebuild the assets" rebuilt the APKs (17:27) and not the bundle (12:43, four commits earlier). The tag contained the fix, the GitHub APK contained the fix, and the artefact a thousand people install did not. Nothing compared the file with the source it claimed to come from -- the ninth instance of *what is installed is not what was built*, and the first where the mismatch reached users.
**Fix**: `tools/check_release.py` gained two comparisons per artefact under `app/build/outputs`: it must be newer than every source it is built from, and its packaged manifest must declare this tree's version. Falsified by dating the bundle back to 12:43, which fails the check by name. The runbook now builds APKs and bundles in one command and says why they may not be built apart. Released as 3.7.1.

### Nine hours of capture, ten minutes of record (2026-08-23) [RESOLVED]
**Problem**: an overnight watch drew trails a few minutes long and a C/N0 scatter of 25 412 samples, where nine hours at forty satellites a second is over a million. Rotating the phone reset both plots to zero.
**Root cause**: both accumulators were `remember { }` inside the composition that draws them, while the run lives in the service's companion. A rotation destroyed them and the run-start effect then cleared them; worse, the document is collected with `collectAsStateWithLifecycle`, so with the activity stopped **no document reached the UI at all** and nothing accumulated. The plots were honest about far less than the run.
**Fix**: the record belongs to the run. Both accumulators moved to `MonitorService`'s companion, cleared at run start rather than at composition re-entry, fed where the document is published; satellites only the handset can place are still fed by the UI, which is the only side that has its fixes. Measured on hardware: 45 samples/s off screen against nothing before, and unchanged across a rotation. The C/N0 scatter had carried this since it was written and tracks inherited it by copying the precedent -- **a wrong precedent spreads by being followed**.

### A pause that lives in prose is not a checklist (2026-08-25) [x2] [RESOLVED]
**Retired 2026-10-14**: `docs/RUNBOOK.md` release sequence: "Hand the release commands over only when everything they name…".
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

### Gradle cannot see this repo's C sources (2026-08-25) [x3 in one day] [RESOLVED]
**Retired 2026-10-14**: `docs/RUNBOOK.md` Common Problems: `gradlew clean`, then build with `--no-build-cache`.
**Problem**: three rounds of "the fix didn't work" on the phone -- each time the installed .so predated the edit.
**Root cause**: the NDK sources live outside the Gradle project (`${REPO_ROOT}/src`), and both the externalNativeBuild task's up-to-date check and the build cache are blind to them: deleting `.cxx` alone re-served the stale build from cache.
**Fix**: `gradlew clean` + `--no-build-cache` whenever a `src/` or `lib/` C file changed. Promoted to the runbook's Common Problems.

### The Bash tool's heredocs halve backslashes (2026-08-25) [x4 in one day] [RESOLVED]
**Retired 2026-10-14**: `CLAUDE.md` Hard Constraints name the mechanism and the cure (Write to the scratchpad, run the file).
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

### A release build cannot replace a debug build in place (2026-08-26) [RESOLVED]
**Retired 2026-10-14**: debug builds are `….dev` and bench builds `….bench` (`app/build.gradle.kts`), so neither ever replaces the Play app.
**Problem**: `install -r` of pro's release APK over the Huawei's leftover debug build failed with `INSTALL_FAILED_UPDATE_INCOMPATIBLE`; the only route is uninstall, which wipes saved connections and credentials on a phone that holds the author's real data.
**Root cause**: different signing keys. A debug build left on a test phone is not a neutral state -- it is a one-way door that costs data to leave.
**Fix**: asked before uninstalling, then rebuilt the RFSEE01 profile from `bin/config.json` and **proved the retyped password by running a check** rather than asserting it. Rule: after a debugging session, put the release build back the same day, while its settings are still cheap to restore.

### A layout that depends on a string must measure the string (2026-08-26) [RESOLVED]
**Problem**: pro's Play feature graphic read "NTRIP Analyser P" -- title and tagline both ran off the 1024x500 canvas. Caught by the author's eye at upload, not by any check.
**Root cause**: `make_feature_graphic.py` chose font sizes as fractions of the canvas height, sized against free's shorter title, and never measured the text it drew. Its own docstring promised "everything sits inside a wide safe area".
**Fix**: fit the text to the available width and **assert** both lines fit before saving. Free's graphic and the social preview regenerated byte-identical, which is the fitter proving it shrinks only what overflows.

### A verify command that only works on a desk (2026-08-26) [RESOLVED]
**Problem**: CI was red for two days, on two pushes, and neither of us looked. The failing check was `git rev-parse v3.8.0`, the evidence behind "3.8.0 released" in the memory index.
**Root cause**: the claim was true and the checker's environment was not -- `actions/checkout` fetches no tags by default, so the tag existed on the remote, resolved on every desk, and was invisible to the runner. A guard that fails on a good tree is the exact failure mode this project already has a comment about.
**Fix**: both checkouts that run `verify_memory.py` now use `fetch-depth: 0`. Rule for a new verify command: it has to pass **in CI**, not only where it was written -- and after adding one, watch the run it first appears in.

### Checking the claims dirtied the tree (2026-09-17) [RESOLVED]
**Retired 2026-10-14**: `.gitattributes` pins both notices files to `text eol=lf`.
**Problem**: `/curate` ran `tools/verify_memory.py`; afterwards `notices.txt` and `THIRD-PARTY-NOTICES.txt` showed as modified, though nobody had edited them and `git diff` was empty.
**Root cause**: one claim's verify command runs `check_release.py`, which regenerates the notices to compare them. The generator writes LF; this checkout has `core.autocrlf=true`; and unlike `ns_ca_bundle.c`, neither notices file has a `.gitattributes` rule. Same content, different bytes, a dirty status. The 2026-08-25 entry's fix (`GENERATED_SOURCES`) cured the staleness face of this, not the line-ending face.
**Fix**: both generated notices now carry `text eol=lf` in `.gitattributes` (2026-09-17), as the CA bundle already did, so the generator's bytes and the checkout's agree.

### CI died in a third-party action's default, not in our code (2026-09-17) [RESOLVED]
**Retired 2026-10-14**: `.github/workflows/ci.yml` asks `setup-android@v4` for `platform-tools` only.
**Problem**: the curate commit's CI failed in *Both editions* before any build step: `android-actions/setup-android@v3` ran `sdkmanager tools` and got "Failed to find package 'tools'". The scheduled run three days earlier had passed.
**Root cause**: the runner image moved to cmdline-tools 16.0, which no longer carries the legacy `tools` package, and the action's `packages` input defaults to `'tools platform-tools'` (read from its own `action.yml`). A default nobody wrote down broke on a change nobody made here.
**Fix**: `packages: platform-tools` in `ci.yml`. When CI fails on a commit that touched no code, read which step failed before reading the commit.

### [RESOLVED] Numbers only a person reads are numbers nothing checks (2026-10-09)
**Problem**: the message-detail decoder has printed MSM7 pseudoranges **5.6x** too large and phases **1.4x** too large since it was written, by applying the phase rate's `0.0001` scale to all three fields where RTCM gives 2^-29 ms and 2^-31 ms. The MSM4 path was ~12% out on both for the same reason, and QZSS 1124 -- an MSM4 -- was read with MSM6's 20- and 24-bit widths, so those cells came from the wrong bits entirely. Found while reading the layout to build `msm_extract_obs`, not by any check.
**Root cause**: the columns feed the GUI's detail view and the CLI's verbose output and nothing else. Every tested path reads C/N0, whose scale is right. A field no measurement consumes has no guard, and the three wrong constants sat beside a correct one for months.
**Fix**: the scales are now named constants used by both decoders, and `test_msm_cnr.c` renders a built frame through `rtcm_set_output_buffer` -- the GUI's own capture path -- and asserts the printed strings against values it computes from RTCM's scales, including a check that the old scale does *not* appear. Falsified by restoring `0.0001`: two checks red. **If a number is only ever read by a human, write the test that reads it like one.**

### [RESOLVED] The reference implementation accused the wrong code (2026-10-09)
**Problem**: the first run of `test/manual/sv_state_vs_rtklib.c` -- P2's cross-check against RTKLIB -- reported our propagator 1 800 km out across the whole fit interval, while the clock agreed to 0.04 ns. A real 1 800 km error would mean the sky plot has pointed at the wrong satellites since it was written.
**Root cause**: the harness, not the propagator. RTKLIB's `eph_t` stores toe twice -- an absolute `gtime_t` and `toes` in seconds of week -- and the longitude-of-node term reads `toes`. The harness set only the first, so RTKLIB dropped `-omge * toe` and rotated its *own* orbit by 25 radians. With `toes` set: 0.0000 m.
**Fix**: the shape of the disagreement was the clue, and is the lesson. The clock agreed, and a rotation about Z preserves both `|r|` and `r.v` -- so the pattern said "rotated frame", not "wrong orbit". **Read what the shape of a disagreement rules out before deciding which side is wrong**: a comparison tool is code too, and it is the code that was written five minutes ago.

### A politely written test defended nothing (2026-10-09) [RESOLVED]
**Retired 2026-10-14**: `test/test_station_report.c` now sets `has_vel = false` beside a stale `speed_mms = 7.0`, so the flag is what the test proves.
**Problem**: P5's falsification removed the `has_vel` gate from the report's velocity accumulation -- the gate that stops a stream carrying no phase-range rates from publishing a speed -- and the case named "but a stream without rates reports no speed" stayed green. The check had passed every run since it was written, and was protecting nothing.
**Root cause**: the test built its snapshot the way a well-behaved session does. `has_vel = false` was set and `selfpos_speed_mms` was left at its `NS_UNSET` initialiser, so the comparison rejected the sample on the sentinel and never consulted the flag. The test proved that *the caller blanks the field*, which is the caller's manners, not the report's defence.
**Fix**: make the input hostile. The snapshot now carries 7 mm/s *beside* `has_vel = false` -- a number it has no right to -- and the gate is the only thing that can stop it; the falsification then reddens the case by name. **A test must feed the input the defence exists for, not the input a cooperating caller would send** -- the same shape as P4's too-uniform ephemerides, one layer up: there the inputs were identical where they should have differed, here the input was correct where it should have been wrong.

### [RESOLVED] The fields were filled and never published (2026-10-09)
**Problem**: P5 claimed nine `selfpos_*` snapshot fields "serialised under `selfpos_*`". The report's serialiser carried them; `ns_stats_to_json()` and the CSV row carried none of them, so the daemon's snapshot, `File > Export Statistics` and every CSV reader described a stream without ever mentioning where it put itself. Found a step later, while wiring the GUI window that needed them.
**Root cause**: two serialisers, one claim. The work went into `station_report.c` and the sentence that was written covered both, because both emit keys of that name. The field block's own Doxygen said "consumers read by key, so the schema version stays" -- a promise about a serialiser nobody had touched.
**Fix**: both serialisers, same null-not-zero rule, four new cases covering nulls *and* values. The lesson is narrower than "a field nothing fills": **a field that one surface publishes is not a field that is published**, and a commit message that names the measurement rather than the surface cannot be checked against either. Name the surface in the claim, and the reader of the next step will notice when one is missing.

### [RESOLVED] The test harness was looking at another desktop (2026-10-09)
**Problem**: verifying the new GUI window from a launcher with no console, `FindWindow("NtripSelfPosClass")` returned 0 after the menu command -- and so did `FindWindow` for the *main* window's class, while `GetClassNameA` read that very class off the live handle. It read exactly like "the window was never created".
**Root cause**: two independent traps, each enough on its own. `FindWindow` and `EnumWindows` search the **calling thread's desktop**, and this host runs on a different one from the process it launched; a `SendMessage` to a handle already in hand crosses regardless, which is why the app still obeyed `WM_CLOSE`. And in the enumeration that replaced them, PowerShell's `Write-Output` inside a scriptblock used as a delegate becomes the delegate's **return value**: the lines never printed, and the callback's non-false return stopped the walk at the first window.
**Fix**: `EnumThreadWindows` (thread-scoped, not desktop-scoped) and `[Console]::WriteLine` inside the callback. Then the window appeared: `class='NtripSelfPosClass' title='Self-position' visible=True`. **When a check says the thing does not exist, prove the check can see a thing that does** -- here, the main window, which was provably there and equally invisible to the same call.

### [RESOLVED] A verdict on a station, from thirty-two seconds (2026-10-09)
**Problem**: the new Self-position window, opened on a stream 32 s old, filled its banner with **NOT COMPUTABLE**. Nothing had solved because nothing had had time to. The author's one-word reply to the screenshot was "not good", and it was right: that is a finding about the station, stated from a window far too short to carry one.
**Root cause**: the window had two states where it needed four. Anything that was not "solved" was presented as "cannot be solved", so `SPP_NO_EPOCH` -- the ordinary condition for the first seconds of every run -- wore the same words as `SPP_SINGLE_FREQ`, which is the one status that really is a property of the stream. **This is `gui-design.md` §14.5's second entry again**: the Stability window said "no dual-frequency pair to measure with" at twenty-five seconds, and the fix there was to wait until the window was judgeable. Same author, same mistake, a new window, a month later -- and the lesson had been written down in the very document the new section was added to.
**Fix**: four states -- solved, gathering, not computable, not solved -- with "gathering" naming what is being waited for. The general rule, which is the one that keeps being relearned: **a screen may only say what its evidence supports, and "not yet" is evidence of nothing.** Writing that down in a design document did not stop it happening; the thing that caught it was a person looking at the window.

### [RESOLVED] Nothing a build can check is wrong on screen (2026-10-09)
**Problem**: the same window shipped a heading cut to "What it mea...", notes under a horizontal scrollbar, a caption severed mid-word, and ring labels reading "0.125 m" on a plot with no points in it. The build was warning-free and 19/19 tests passed throughout.
**Root cause**: three column widths guessed at `190 / 90 / 210` px -- against `gui-design.md` §14.6, *Column widths are measured, not guessed*, which the same session had just read -- a plot allowed half the window regardless of what the figures needed, a one-line `TextOut` for a sentence longer than the plot, and a radius chooser returning its smallest step for an empty set.
**Fix**: widths measured from the longest string each column can hold, the plot capped at what is left after the figures, `DrawText` with `DT_WORDBREAK | DT_END_ELLIPSIS`, and no ring labels until points set the scale. Then **look at it**: `PrintWindow` into a bitmap, saved as a PNG and read back, which is how the remaining three were found after the first was fixed. Two traps in that harness: call `SetProcessDPIAware()` first, or a 1000x620 window is reported and captured as 800x496 at 125 % scaling and the clipping looks like the bug; and the empty state is not the solved state, which still needs a live station.

### [RESOLVED] The feature was never called (2026-10-09)
**Problem**: connected to a real station, the Self-position window sat at `GATHERING` -- 170 s of stream, 157 snapshots, 0 epochs solved, reason `no observations in this epoch`. That status is the field's *initial value*: the solve had never run. `selfpos_solve()` was defined in `ntrip_session.c` and called from nowhere, because the call site discarded the return value of `obs_feed()` -- the one thing that says an epoch has closed. The whole feature was dead through P5 **and** P6, two commits and two days of work on top of it.
**Root cause**: three failures that lined up.
  1. **Every test starts below the session.** `test_spp` builds its own epoch, `test_observables` its own payload, `test_station_report` and `test_ns_stats` their own snapshots. Nothing drove `ntrip_session` and asked whether a closed epoch reaches the solver. The suite was 19 green tests around a hole in the middle.
  2. **Every surface handled "nothing solved" correctly** -- null in the JSON, "nothing solved" in the CLI, a reason in the window. The feature's own care about absence is what made its absence look like a station's property.
  3. **The build asks the compiler almost nothing.** Flags were `-O3 -DNDEBUG -std=gnu99`: no `-Wall`, so `-Wunused-function` never fired on a static function nobody called. Proved after the fact: the committed file compiled with `-Wall` names it in one line.
**Fix**: the call site, plus the two defences that were missing. `-Wall` on our own targets (the codebase turned out clean under it, bar four real `strncpy` truncations in `gui_thread.c` and a tray tooltip, all fixed), and `test/test_selfpos_session.c`, which replays RTCM through `ns_open_file()` and asserts a closed epoch reaches the solve -- red on the shipped defect, with a one-bit control (DF393 set, epoch never closes) that must stay `SPP_NO_EPOCH`. **A test that starts below the wiring cannot see the wiring**, and "the build is warning-free" means nothing until you know what the build asks.

### [RESOLVED] The rule was written down and applied to one case (2026-10-09)
**Problem**: with the solve finally running against a real six-system station, it placed it **1.8 km** from its broadcast position: worst code residual 2.6 km, apparent motion 829 m/s, clock drift 393 m/s, 25 satellites at PDOP 0.9.
**Root cause**: the solver accepted every constellation but GLONASS and SBAS under **one** receiver-clock unknown. Its own header says why GLONASS is out -- *"FDMA inter-frequency biases will not fit under one receiver-clock unknown"* -- which is not a fact about FDMA: every system keeps its own time and its own hardware path, and multi-GNSS SPP therefore carries a clock unknown per system. BeiDou adds a second fault, BDT being 14 s behind GPS while a 1042 ephemeris dates its `toe` in BDT: ~55 km of satellite position. The rule that excluded one case was the rule that excluded all of them, written out in full, in the file, and generalised to nothing.
**Fix**: GPS only until the solve has a clock unknown per system, with the measured numbers at the filter so the next reader sees the evidence rather than the preference. **When a refusal is justified by a general principle, check which other cases the principle covers** -- the sentence naming the exclusion is the place to do it. And the test that missed it is the shape to remember: `test_spp.c` verified by closure against *one* synthetic constellation, building its measurements from the same single-clock model the solver inverts. A closure test cannot discover that the world has more clocks than the model.

### [RESOLVED] The linker could not write, and the check could not tell (2026-10-09)
**Problem**: the author was about to re-test the GPS-only fix against a live station on a GUI binary that did not contain it. `bin/ntrip-analyser-gui.exe` was fifteen minutes older than `src/core/spp.c`, while `bin/ntrip-analyser.exe` was current.
**Root cause**: two failures, one on each side.
  1. **Windows refuses to overwrite a running `.exe`**: `ld.exe: cannot open output file ...: Permission denied`. The author had the GUI open -- as anyone testing a GUI does -- so every build since relinked the CLI and left the GUI alone. Nothing in the normal workflow says so.
  2. **The check was blind to it.** Verification had been `cmake --build build 2>&1 | grep -cE "warning:"`, reported as "0 warnings". A link error prints `error`, and a pipe discards make's exit status in favour of grep's. The test suite does not build the GUI, so `20/20` stayed green over a GUI that had failed to link. Both halves of "0 warnings, 20/20 tests" were true and the conclusion was wrong.
**Fix**: take the build's **exit status** first and read warnings second -- `cmake --build build > log 2>&1; echo $?` -- and, when a GUI change is to be tested, compare the binary's timestamp against its sources before handing it over (`find src gui -newer bin/ntrip-analyser-gui.exe`). **Close the GUI before building it.** This is `What is installed is not what was built` in its sixth shape, and the third time in one day that a failing tool has read as a clean result -- `gcc` absent from PATH printing `0` through `grep -c`, `-fsyntax-only` missing ten warnings, and now a linker that could not write at all.

### [RESOLVED] The guard fired on the thing the protocol exists to express (2026-10-10)
**Problem**: a station streaming ten GPS satellites reported *fewer than four usable satellites*, 0 epochs solved in 106 s. `SPP_TOO_FEW_SATS` with both refusal counters at zero does not mean "few satellites": it means **not one cell was examined**.
**Root cause**: the base sends BeiDou in two frames -- 48 cells, then 5 -- which is what MSM's multiple-message bit is for. The epoch-boundary rule read the second frame as "this system has contributed before, so a new bundle has begun" and reset the set, throwing away GPS, GLONASS, Galileo and BeiDou's own first frame. Six cells of 128 reached the solver. The guard existed for a base whose multiple-message bit never clears, and it could not tell that case from a legitimate continuation.
**Fix**: compare where comparison is sound. The code already said epoch fields are not comparable *across* constellations and stopped one step short of the consequence: they are comparable **within** one. Same system and same epoch is a continuation; same system and a different epoch is the stuck-bit case. **A guard against a malformed stream must be written so it cannot fire on a well-formed one** -- and the thing it nearly always mistakes is the protocol feature it was never told about. Found only by walking a real capture frame by frame; every test in the suite sent one frame per system per epoch, which is the one shape that cannot show it.

### [RESOLVED] The epoch was dated by whichever frame happened to close it (2026-10-10)
**Problem**: a second live station solved every epoch and placed itself **7.8 km** away -- 3888 m worst code residual, 779 mm/s of apparent motion, 8 satellites at PDOP 1.1. Good geometry, impossible answer.
**Root cause**: the solve took its time from the frame that cleared DF393, and that frame is whatever the base chooses to send last. This one ends every bundle with BeiDou, whose epoch field is **BDT -- 14 s behind GPS**. Fourteen seconds moves a GPS satellite 55 km along its orbit, and what survives the receiver-clock unknown is kilometres of position and residual. The *other* station tested that day ends its bundles with NavIC, which is GPS-aligned, so identical code was correct there **by luck** -- which is exactly why one station looked plausible and the other did not.
**Fix**: convert in core where it can be tested -- `msm_epoch_to_gps_tow_ms()`: GPS, Galileo, QZSS, NavIC and SBAS as they are, BeiDou +14 s, GLONASS refused because day-of-week plus milliseconds of *day* cannot be placed in a GPS week without the date and the leap seconds. The epoch carries `tow_gps_ms`, taken from the first frame that can say rather than the last frame to arrive. **A value that arrives on several scales must be converted at the boundary, not consumed wherever it lands** -- and the clue was already in the code: the stream clock had been locking to one constellation's scale for months, with a comment explaining why the scales are not comparable. The solve two files away did not read it.

### [RESOLVED] The station stopped sending the thing the code was waiting for (2026-10-10)
**Problem**: an hour after the epoch-boundary rule was fixed and verified against RFSEE01, the same station stopped solving entirely: 41 snapshots, 0 epochs, *"waiting for the first observation epoch"*. Nothing had changed in the code since the run that worked.
**Root cause**: NavIC had dropped out of view. It had been the last frame of every bundle, and the only one clearing DF393 -- so with it went the signal the code waited on to close a set. Every remaining frame said "more follows" and no set ever closed. A 60 s capture confirmed it: **zero** DF393 clears in the whole file. The irony is that this is the exact case the original guard was written for ("a base whose multiple-message bit never clears") -- it detected the condition correctly and still never closed anything, because detection reset the set instead of completing it.
**Fix**: close the set when the next one demonstrably begins, not only when the protocol says so -- a bundle beginning is proof the previous one ended, and for some stations it is the only proof there will be. **A stream's shape is not a property of the station; it is a property of the sky at that moment.** Two runs an hour apart, same caster, same mountpoint, same code, different message set. Anything that waits for a particular frame must also have a way to proceed without it, and the third test case written against a live capture should be assumed to describe that hour rather than that station.

### [RESOLVED] A station sent a standard field with the sign reversed (2026-10-10)
**Problem**: RFSEE01's position solved cleanly while its velocity read **1 666 m/s**, drift 932 m/s, rate residual 830 m/s. The same code and orbits gave 0.03 m/s on APEL00NLD0.
**Root cause**: the receiver at RFSEE01 writes RTCM's phase-range rate (DF399/DF404, positive while the range grows) with the opposite sign -- every satellite the exact negative of its orbit's prediction, to the millimetre per second. Our decoder was right; the station was not. Separately, the rate RMS summed **pre**-fit residuals beneath a comment claiming post-fit, so it reported the clock drift on every station and made the fault look like noise of the drift's size rather than a model mismatch.
**Fix**: let the data decide -- fit both conventions, keep the one that fits (cm/s against hundreds of m/s, never close), claim nothing with only four rates, and **report** a reversed station rather than silently correcting it. **A field defined by a standard is still a claim made by somebody's firmware**; where the data can say which way round it is, ask the data. The diagnostic that settled it was printing measured against predicted per satellite across two stations -- one line made "decoder, solver or station?" a question with one answer. And the residual bug is the comment-says-one-thing shape again: read the code beneath a comment that describes what the code should do.

### [RESOLVED] [x2] A new function's name filled a snapshot field it never touched (2026-10-12)
**Recurred the same day, 2026-10-12**: a note added inside `play-listing.md`'s *Full description* section was measured by `check_release.py` as listing text, 4181 characters against Play's 4000, and CI failed on `1fd9814`. The fix the first entry wrote down (*run `check_release.py`*) had been written for `src/core` changes, and this was a document. **`check_release.py` reads documents too** (the listing, the feature matrix, the notices), so run it before handing over *any* commit, not only code ones. The note now has a section of its own outside the measured one.

**Problem**: CI failed on `e978ec8`, but the cause was M1 (`38e32f0`), pushed with it: `check_release.py` reported "the known-gap list has no entries that are now filled -- sourcetable_pos_valid". Nothing had filled the field. M1 had added a core function of exactly that name, `sourcetable_pos_valid(lat, lon)`.
**Root cause**: the unfilled-field check is a whole-word search for each `NsStatsSnapshot` field name outside `ns_stats.c`, so any identifier spelled like a field counts as a write. The local run before committing M1 was `test_all` and `verify_memory.py`, not `check_release.py`. The guard that would have caught it locally was never run.
**Fix**: the function became `sourcetable_pos_usable`, so one name stops meaning two things, and the field stays on `SNAPSHOT_KNOWN_GAPS` where it belongs. Before naming a core function, grep `ns_stats.h` for the name. Run `python tools/check_release.py` after any `src/core` change; locally, its only expected failures are the stale-release-artefact lines.

### [RESOLVED] A string handed to a UI library ran as HTML (2026-10-12)
**Problem**: a test sourcetable entry named `<img src=x onerror="window.pwned=1">` ran its script when the mountpoint map's marker was hovered. The page source was clean: the core had escaped every `<`.
**Root cause**: `bindTooltip(p.mountpoint)` passes a string, and Leaflet sets a string tooltip as innerHTML (attribution too). The core's escaping protects the *page source*; once JavaScript decodes the JSON, a name is raw text again. Escaping once at the boundary is necessary but not sufficient, because the library is a second boundary.
**Fix**: every caster-supplied string reaches Leaflet as an element built with `textContent`, and the user's attribution goes through `textContent` → `innerHTML` before Leaflet. Caught only because the page was opened with a hostile sourcetable and a `window.pwned` canary, not by reading the code.

### [RESOLVED] A page said "Copied" when nothing was copied (2026-10-12)
**Problem**: the location picker said "Copied to clipboard!" in a browser that had refused the write (`NotAllowedError`), so **<<** pasted whatever was there before. This had been the case since the picker was written.
**Root cause**: the copy used `navigator.clipboard` whenever it existed and fell back only when it did not. The result was never consulted; the success message was printed unconditionally after the call.
**Fix**: one `copyText(t, done)` for both map pages, which tries `execCommand` and then the API, and reports `done(false)` with a selected box to copy by hand. A message announcing success must be the *callback* of the success, never the line after the call. Same family as "a screen may only say what its evidence supports".

### [RESOLVED] A library option did half of what its name says (2026-10-12)
**Problem**: Leaflet's `noWrap: true` stopped the repeated worlds, but the page still requested 16 tiles off the world's edge (x = −2…5 at zoom 2), all wasted.
**Root cause**: the name was read as the behaviour. Leaflet's own documentation says `noWrap` "can be used in combination with the `bounds` option to prevent requesting tiles outside the CRS limits".
**Fix**: `bounds` added. Found by counting the tile requests the page made, not by reading the option. A recurrence of *read the artefact, not the toolchain's promise*.

### [RESOLVED] A shipped feature broke when the service under it changed (2026-10-12)
**Problem**: the location picker (**Map**) had shown nothing but *API KEY REQUIRED* watermarks since CARTO began requiring a key from everyone (about 2026-09-23). It was found three weeks later, and only because a new feature reused its tile source.
**Root cause**: the feature depended on a third party's free terms, and nothing ever looked at the page it produces. The OSM Foundation's tile server, the obvious alternative, refuses a page opened from a file (no `Referer`, 403); a comment in the code had said so, and it was verified again.
**Fix**: no default tile source. Natural Earth outlines are built in, and the user may set their own key. Nothing shipped depends on a third party's goodwill for the map underneath any more. Lesson: **a dependency on someone else's free service is a claim about their terms, and terms change** — prefer what the program carries.

### [RESOLVED] The GUI's ANSI text went into a UTF-8 page (2026-10-12)
**Problem**: CARTO's attribution `© OpenStreetMap contributors © CARTO` would have shown as `� … �`.
**Root cause**: the GUI is an ANSI program, so `©` is the single byte 0xA9; the page is UTF-8, and the core rightly replaces invalid UTF-8 with U+FFFD. Each half was right on its own.
**Fix**: `AnsiToUtf8` (CP_ACP → wide → CP_UTF8) before quoting. Any GUI text bound for a page, a JSON file or the network crosses an encoding boundary. The sourcetable's bytes do not, because they never were ANSI.

### [RESOLVED] A Leaflet map measured its container before it had a size (2026-10-12)
**Problem**: the mountpoint map opened at maximum zoom on empty sea.
**Root cause**: the map's top edge was set by script *after* `L.map()`. Leaflet measures the container once, at creation, so `fitBounds` saw 0 px of height.
**Fix**: a flex layout, with the header filled in before the map is made. Found by `getBoundingClientRect` and the pane transform, not by the screenshot, which in the browser pane is sometimes blank while the canvas holds 845 894 painted pixels. When a screenshot shows nothing, count the canvas's pixels before believing it.

### [RESOLVED] A new field would have refused the biggest caster on the phone (2026-10-13)
**Problem**: M6a added the core's kind, distance and bearing to each sourcetable entry crossing to the app. The JNI glue allocated a fixed 1 MB and the bridge refuses rather than cuts, so onocoy -- 1.26 MB with the new fields -- would have reached the phone as "Could not reach the caster". Underneath, the bridge read at most 4096 entries, so the phone's list of onocoy had always lacked about a third.
**Root cause**: a size chosen from the casters in hand (Centipede's 150 kB), never re-measured when the document grew.
**Fix**: the bridge sizes its own output (256 KB doubling to 32 MB, written again from the same table), and the entry ceiling is 16384. Found only because the change was run against the largest caster available through the bridge's own fetch, compiled on the desktop -- not against the two-entry caster on the phone.

### [RESOLVED] Twenty seconds of silence was taken for the end of a list (2026-10-13)
**Problem**: the phone's map drew 12 of onocoy's ~5900 mountpoints as though that were the caster.
**Root cause**: the sourcetable fetch ends on `ENDSOURCETABLE` or on two silent ten-second waits, and returned whatever it had either way. onocoy goes silent part-way now and then -- curl from the desktop then got 1315 entries without the closing line, then 9, then none.
**Fix**: `receive_mount_table_ex()` reports an answer that stops before `ENDSOURCETABLE` as stalled or dropped; `receive_mount_table_whole()` asks once more and refuses a list still cut short, in every frontend. Six casters checked all end a whole list with the line. A timeout is not an end-of-data marker; where the protocol has one, require it.

### [RESOLVED] A loopback test died of SIGPIPE on Linux only (2026-10-13)
**Problem**: CI failed `test_bridge_vrs` with SIGPIPE after new cases were added at its end; Windows passed.
**Root cause**: the test's caster thread kept writing to a stream the bridge had closed. Before, the process ended at the close; the new cases kept it alive long enough for a write to hit the closed socket, and Linux kills the writer. Windows has no such signal.
**Fix**: `signal(SIGPIPE, SIG_IGN)` in the test, as `test_tls.c` already did. Closing the socket on a failed send was rejected: the socket is non-blocking, and a full buffer fails the same way. A loopback harness on POSIX ignores SIGPIPE from its first line.

### [RESOLVED] Frame times taken where they could not mean anything (2026-10-13)
**Problem**: the map's frame times were reported from debug builds, and one set was read from counters never reset before the author's pinch, so it held two map loads and a selection besides.
**Root cause**: a debug build runs Compose unoptimised -- the same pan was a median 24 ms on debug and 16 ms on a release-configured build -- and `gfxinfo` accumulates since the process started.
**Fix**: a `bench` build type (release settings, debug key, `….bench`) and `dumpsys gfxinfo <pkg> reset` immediately before the action being measured (`docs/RUNBOOK.md`). The same rule as *measure the way the build measures*, for frames.

### [RESOLVED] Compose state read in composition redrew the whole screen per gesture step (2026-10-13)
**Problem**: panning the map ran at a median 73 ms a frame even with its outlines pre-rendered.
**Root cause**: the view's scale and pan were keys of a `LaunchedEffect`, so every gesture step recomposed the screen; the marker loop then read those states twice per mark, 5900 times.
**Fix**: the view is watched through `snapshotFlow` and read once per frame in the draw; empty-map panning went to 7 ms. Read state where it is used -- the draw phase -- and once.

### A toolchain move left the old Gradle daemon holding lint's cache (2026-10-14) [RESOLVED]
**Problem**: after AGP 8.11 → 8.13 and again 8.13 → 9.4, `lintVitalAnalyzeProRelease` failed with "being used by another process" on a jar in `lint-cache`; `clean` failed the same way.
**Root cause**: a daemon of the previous Gradle version still had the file open, and the new wrapper's `--stop` reaches only daemons of its own version.
**Fix**: stop it with that version's own `gradle --stop` (from `~/.gradle/wrapper/dists`). In `docs/RUNBOOK.md`'s toolchain note.

### A merge of a subset erased what only the superset holds (2026-10-14) [RESOLVED]
**Problem**: importing free's connection into pro blanked pro's ephemeris stream; self-position on a station that sends no orbits sat at "waiting for orbits".
**Root cause**: the merge replaced a matching connection outright, and free's empty ephemeris fields meant "I cannot hold this", not "remove it".
**Fix**: `mergeConnections(fromFree = true)` keeps the fields free cannot hold. A merge from a narrower source must say which fields it owns.

### The wiki went six weeks unpublished behind a broken clone (2026-10-15) [RESOLVED]
**Problem**: `tools/publish_wiki.sh` failed with "fatal: not a git repository" and copied nothing; the live wiki was still the 25 August version, so the Pro page said "closed testing" a day after pro reached Play. The CI "pages deployment" kept passing, which is the project website, not the wiki.
**Root cause**: an interrupted clone on 2026-09-02 left `$TMPDIR/ntrip-wiki/.git` holding only `hooks/`; the script reused any folder with a `.git` in it, and git refused that one.
**Fix**: the script reuses its copy only when `git -C "$WORK" rev-parse --git-dir` prints exactly `.git`, and stops with a message when a fetch fails. A published page is checked by fetching it — the wiki's `_history` date — not by any build status.

### A release went live with no release notes in one edition (2026-10-15) [RESOLVED]
**Problem**: 3.9.0 reached Play with pro's notes in English and Dutch and free's in neither; free's page shows no *What's new* at all. Play gave no warning.
**Root cause**: notes are entered per edition and per language, and nothing checks the box is filled before review.
**Fix**: `docs/RUNBOOK.md` *Before submitting to Play* now asks for a block per listing language on each edition (2026-10-16).
