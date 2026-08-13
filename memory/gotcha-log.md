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

### Heredocs corrupt C string literals (2026-08-12)
**Problem**: `\n` inside a heredoc-fed script arrives as a real newline, producing C source that no longer compiles.
**Fix**: Use file-editing tools for anything containing escapes, or write the script to a file first.

### Windows filesystem is case-insensitive (2026-08-12)
**Problem**: Resizing `SHOT.png` into `shot.png` destroyed the original; later reads returned the resized copy.
**Fix**: Distinct names, not distinct case.

### JAVA_HOME is Adoptium, not Microsoft (2026-08-12)
**Problem**: Gradle: "JAVA_HOME is set to an invalid directory".
**Fix**: `C:\Program Files\Eclipse Adoptium\jdk-17.0.20.8-hotspot`, as a Windows path — Gradle rejects the POSIX form. Also on this machine: the only host C compiler is CodeBlocks' MinGW.

## Promoted

<!-- Track what has been promoted, so it is not promoted twice and so the loop
     is visibly working. Keep incrementing occurrences AFTER promotion: a
     promoted pattern that recurs means the promotion did not take. -->

| Date | Gotcha | Occurrences | Promoted to |
|------|--------|-------------|-------------|
| 2026-08-13 | A remembered value must not satisfy the KPI that asks for it | 1 | project file, hard constraint |
| 2026-08-13 | Judge constellations by NavSys, never the 1005/1006 bits | **3** — 2026-08-12 three times in one session | project file, domain facts; `memory/MEMORY.md` active decisions |
