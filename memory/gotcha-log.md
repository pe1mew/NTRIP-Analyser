# Gotcha log

Problem → root cause → fix. Append as things go wrong; promote a pattern
into a topic file or `MEMORY.md` when it recurs. Entries stay after the
cause is fixed, marked resolved, so the same investigation is not
repeated.

Seeded 2026-08-13 from the sessions that produced the current
unreleased work.

---

### JNI symbols existed under a name nothing looks up (2026-08-11)
**Problem**: Every native call threw `UnsatisfiedLinkError`, although
`nm` showed the symbols present in `libntrip_android.so`.
**Root cause**: The externals are declared in a Kotlin `companion
object`, so the obvious symbol name carries `$Companion` — but
`@JvmStatic` promotes them to static methods on the *enclosing* class,
and the JVM resolves the name from where the method ends up. The symbols
existed under the `$Companion` form; nothing looked there.
**Fix**: `JNI_FN(name)` expands to `Java_..._NtripBridge_##name`.
Confirming that symbols exist proves nothing here — the check that
matters is a native call that does not throw.
**Tags**: android, jni, kotlin

### A perfect run that never reached the screen (2026-08-11)
**Problem**: The phone sat at READY while a run completed correctly.
**Root cause**: `ns_stats_to_json()` emits `null` for anything not yet
measured, so that "no ARP yet" is not reported as "a station at 0N 0E".
The Kotlin model declared those doubles non-nullable, decoding threw,
and a decode failure publishes nothing — so the screen kept its previous
state with no error anywhere.
**Fix**: Nullable in the model. When a snapshot field can be absent, the
model must say so.
**Tags**: android, json, silent-failure

### GLONASS: one satellite out of 279 (2026-08-12)
**Problem**: A daily RINEX navigation file yielded 1 GLONASS satellite.
**Root cause**: RINEX 3.05 gives GLONASS a fourth orbit line that 3.04
did not. The loader consumed a fixed three; the leftover line was taken
for an unknown system, and the four-line "resync" skip then ate the next
record. The reader never came back into phase.
**Fix**: Read by structure, not by counting lines per system — an epoch
line names its system in column 1, a continuation line starts with
spaces. `test/test_rinex_nav.c` pins it, with two 3.05 records back to
back, which is what turns one misread into a cascade.
**Tags**: rinex, parser, resolved

### Fresh orbits reported as "no orbits at all" (2026-08-12)
**Problem**: The app said the sky view could place nothing, over a cache
of 139 orbits.
**Root cause**: Ephemeris age took the entry with the highest week and
`toe`, across systems whose week numbers share no origin. It picked a
NavIC record whose reference epoch was 7.4 h in the *future* and
returned a negative age, which the UI rendered as "none".
**Fix**: Smallest per-satellite age, computed in each system's own time
frame; an epoch ahead of now counts as fresh. Beware GLONASS: its `toe`
is Moscow seconds-of-day, not seconds-of-week.
**Tags**: ephemeris, time-frames, resolved

### The sky view fell back to phone GNSS for 30 s of every run (2026-08-12)
**Problem**: A new run showed a handful of satellites, then jumped to
the full constellation. It looked like the orbit cache was being
cleared.
**Root cause**: Nothing clears the cache. Placement needs the *station's*
position, which arrives only with the first 1005/1006 — up to 30 s on a
station that sends one every 30 s.
**Fix**: The bridge remembers the last broadcast position per mountpoint
for the process lifetime. Deliberately placement-only: `have_arp_info`
stays false, so KPI 3 still waits for a real message.
**Tags**: android, sky-plot, arp, resolved

### KPI 8 failed healthy stations three times (2026-08-12)
**Problem**: Advertised-versus-actual reported failures on stations that
were fine — judged too early, then on rate-less types, then on a
constellation advertised but not visible.
**Root cause**: Three separate wrong assumptions: that 20 s is enough
evidence; that every advertised type has a rate; and that advertising
QZSS in Europe is a fault rather than ordinary.
**Fix**: A 30 s floor, a 600 s grace for rate-less types, and judging
only the one direction that misleads — streaming something never
advertised. Constellations are compared against the sourcetable's NavSys
field, never the 1005/1006 indicator bits, which cannot express BeiDou.
**Tags**: kpi, sourcetable, resolved

### A bounded test that could never end (2026-08-12)
**Problem**: The GUI's station check would run for ever on a mountpoint
the caster does not list.
**Root cause**: With no sourcetable entry, KPI 8 stays PENDING — rightly,
since "could not check" is not a pass — and a pending KPI holds the
roll-up at RUNNING, which never settles.
**Fix**: The same 300 s ceiling the CLI applies, and the window now names
which of three ways a run ended.
**Tags**: gui, kpi, resolved

### Config load named the wrong mountpoint (2026-08-13)
**Problem**: "Loaded configuration for " with an empty name.
**Root cause**: After settings became derived from a profile store, the
message still read the value captured before the store updated.
**Fix**: Name what was just loaded, from the new value.
**Tags**: android, compose, state

---

## Environment traps

### Heredocs corrupt C string literals (tooling)
**Problem**: `\n` inside a heredoc-fed script arrives as a real newline,
producing C source that no longer compiles.
**Fix**: Use the file-editing tools for anything containing escapes, or
write the script to a file first.
**Tags**: tooling

### Windows filesystem is case-insensitive (tooling)
**Problem**: Resizing `SHOT.png` into `shot.png` destroyed the original;
later reads returned the resized copy.
**Fix**: Distinct names, not distinct case.
**Tags**: tooling

### `JAVA_HOME` is Adoptium, not Microsoft (2026-08-12)
**Problem**: Gradle: "JAVA_HOME is set to an invalid directory".
**Fix**: `C:\Program Files\Eclipse Adoptium\jdk-17.0.20.8-hotspot`, as a
Windows path — Gradle rejects the POSIX form.
**Tags**: android, toolchain
