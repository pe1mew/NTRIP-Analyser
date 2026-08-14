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

### Heredocs corrupt C string literals (2026-08-12)
**Problem**: `\n` inside a heredoc-fed script arrives as a real newline, producing C source that no longer compiles.
**Fix**: Use file-editing tools for anything containing escapes, or write the script to a file first.

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

## Promoted

<!-- Track what has been promoted, so it is not promoted twice and so the loop
     is visibly working. Keep incrementing occurrences AFTER promotion: a
     promoted pattern that recurs means the promotion did not take. -->

| Date | Gotcha | Occurrences | Promoted to |
|------|--------|-------------|-------------|
| 2026-08-13 | A remembered value must not satisfy the KPI that asks for it | 1 | project file, hard constraint |
| 2026-08-13 | Judge constellations by NavSys, never the 1005/1006 bits | **3** — 2026-08-12 three times in one session | project file, domain facts; `memory/MEMORY.md` active decisions |
| 2026-08-14 | Scripted file edits corrupt what they rewrite — escapes, then line endings | **2** — heredoc 2026-08-12, doubled CRs 2026-08-14 | project file, hard constraint |
| 2026-08-14 | A data property appears in every renderer, so fix it in all of them | **2** — Android 2026-08-13, GUI 2026-08-14 | `memory/MEMORY.md` active decisions |
