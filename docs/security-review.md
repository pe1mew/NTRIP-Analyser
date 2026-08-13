# Security review

Conducted 2026-08-13, before the Android release. Reviewed against one
question: **what can a caster do to this software?**

The threat model that matters here is not a remote attacker on the
internet — the app opens outbound connections and listens on nothing.
It is the **caster the user chose**, which may be hostile, compromised,
or simply broken, and whose bytes reach 3,634 lines of C parsing code.

**CRC-24Q is not a security control.** It detects corruption. Anyone
constructing a frame deliberately computes the correct CRC and passes
straight through it. Every guarantee below has to hold for a *well-formed
frame that lies about its contents*.

## Summary

| # | Finding | Severity | State |
|---|---|---|---|
| F1 | RTCM 1033 read past the payload and printed what it found | Medium | **fixed** + regression test |
| F2 | Sourcetable accumulated without limit from an untrusted caster | Low–Medium | **fixed** |
| F3 | Credentials cross the network in the clear; no TLS support | Medium | **accepted, documented** — feature gap |
| F4 | `get_bits()` has no bounds checking; safety rests on callers | Low (latent) | open, recommendation |
| F5 | `base64_encode()` takes no output capacity | Low (latent) | open, recommendation |
| F6 | cJSON 1.7.18 is affected by CVE-2025-57052 (CVSS 9.8) | None as built | **not compiled** — see below |
| F7 | Android release build has no minification; alpha crypto dependency | Low | decision |

---

## F1 — RTCM 1033 read past its payload *(fixed)*

Message 1033 carries four counted strings — antenna descriptor, antenna
serial, receiver type, receiver serial — each preceded by an eight-bit
length. The decoder capped each *copy* at 64 characters but bounded no
*read*: a frame could declare 255 characters four times over while
carrying an eight-byte payload, and the decoder read every one of them.

`get_bits()` performs no bounds checking, so the reads simply walked
past the frame — about 250 bytes — and the results were printed as
station metadata.

Demonstrated against the pre-fix code with an eight-byte payload:

```
RTCM 1033 (Receiver & Antenna Descriptor):
  Antenna Descriptor: ������
  Receiver Type: ��^�
```

Those characters are memory from beyond the frame, rendered into the log
and the GUI's detail window.

**Impact.** In the shipped call paths the over-read stays inside a large
fixed buffer — the session's `frame[1029]` and the GUI's 4 KB
`RtcmRawMsg` — so this is an out-of-bounds read *of the frame*, not of
the allocation, and it discloses previously received bytes rather than
arbitrary process memory. It would become a true heap over-read the
moment a caller passes a tightly-sized buffer. It also misparsed every
field after a capped string, because the cursor was left mid-field.

**Fix.** `rtcm_read_counted_string()` bounds every read by the payload,
advances the cursor across the whole declared string even when the copy
is capped, and reports truncation so the frame is rejected rather than
half-read. 1007 and 1008 already did this correctly and were the model.

**Regression test.** `test/test_rtcm_hostile.c` builds *CRC-valid*
frames that lie: four 255-byte counters in an eight-byte payload, a
frame ending at a counter, the same for 1007, and a corrupted CRC that
must be rejected. It fails against the pre-fix parser.

## F2 — Unbounded sourcetable accumulation *(fixed)*

`receive_mount_table()` grew its buffer by whatever arrived until the
caster closed the connection. A caster that streams indefinitely
exhausts memory — on a phone, until the app is killed.

Capped at 4 MB, well above any real sourcetable (the largest public ones
are a few hundred kilobytes), with a warning when it triggers.

## F3 — Credentials cross the network in the clear *(accepted)*

NTRIP authenticates with HTTP Basic: `username:password`, **base64, not
encrypted**, over a plain TCP connection. Anyone on the path — a hotel
network, a shared site link, a mobile operator — can read them, and the
app has no TLS support at all.

This is inherent to the current implementation rather than a defect, but
it deserves a decision before a paid app stores several casters'
credentials:

- Kadaster/NSGI already offer a **TLS caster on port 443**; the free
  streams are reachable both ways.
- Supporting NTRIP over TLS would need a TLS library on all four
  frontends — significant work, and the largest security improvement
  available to this project.
- Until then, say so plainly in the wiki: credentials are sent as the
  NTRIP protocol specifies, and a caster reachable only over plain TCP
  cannot protect them.

Storage is not the weak point: the Android app holds credentials in
`EncryptedSharedPreferences` keyed from the Keystore, and configuration
files are documented as plain text (`docs/jsonConfigs.md`).

## F4 — `get_bits()` is unchecked *(recommendation)*

```c
uint64_t get_bits(const unsigned char *buf, int start_bit, int bit_len);
```

It indexes `buf[byte]` for whatever the caller asks. Every safety
property in the RTCM decoders is therefore a property of the *callers* —
and F1 is what one missing check costs.

The decoders are otherwise disciplined: 1019 guards 61 bytes and
consumes exactly 488 bits; the MSM paths check `total_bits` before each
field. But the invariant is unenforced and unenforceable by review alone.

**Recommended**: a `get_bits_checked(buf, len, start, n, *out)` returning
false past the end, used by new code, with the existing function kept
for the hot MSM loops where the caller has already proven the bound.

## F5 — `base64_encode()` has no output bound *(recommendation)*

```c
void base64_encode(const char *input, char *output);
```

Safe today only because `USERNAME` and `PASSWORD` are 128 bytes each, so
the worst case is 257 input bytes → ~344 encoded into a 512-byte buffer.
Enlarging either field silently makes this a stack overflow. Add a
capacity parameter, or document the requirement at the declaration.

## F6 — cJSON: vulnerable version, unreachable code

The vendored copy is **cJSON 1.7.18**, which falls in the range affected
by [CVE-2025-57052](https://github.com/advisories/GHSA-98j5-4649-rfv2)
(CVSS 9.8) — an out-of-bounds access in
`decode_array_index_from_pointer()` in **`cJSON_Utils.c`**.

**Not exploitable as built**: every build compiles `lib/cJSON/cJSON.c`
only — CMake, `build-gui.bat` and the Android NDK build alike — and
nothing in this project references `cJSONUtils_*`. The vulnerable file
ships in the repository but is never compiled into any artefact.

**Recommended**: update the vendored cJSON to a release carrying the
fix, and delete the unused `cJSON_Utils.*` from the vendored tree so a
future build cannot pick it up by accident.

## F7 — Android build and dependency posture

Good as found: `allowBackup="false"` (which also avoids restoring an
`EncryptedSharedPreferences` file whose Keystore key did not travel with
it), `MonitorService` not exported, `MainActivity` exported as a
launcher activity must be, credentials encrypted at rest, and a
permission set that is all justified — `INTERNET`,
`ACCESS_NETWORK_STATE`, `FOREGROUND_SERVICE` + `DATA_SYNC`,
`POST_NOTIFICATIONS`, `ACCESS_FINE_LOCATION`.

Two decisions rather than defects:

- `isMinifyEnabled = false` in the release build type. Not a security
  control on its own, and the reflection kotlinx-serialization relies on
  needs ProGuard rules if it is turned on.
- `androidx.security:security-crypto` is pinned at **1.1.0-alpha06**
  while guarding user credentials. `1.0.0` is stable.

`ACCESS_FINE_LOCATION` needs a plain-language justification in the Play
data-safety form: the free edition uses it **on-device only**, to turn
satellites into azimuth and elevation; the pro edition additionally
transmits position to the caster as GGA, with consent.

---

## What this review did not do

Stated so the coverage is not overestimated:

- **No sanitiser build.** MinGW on Windows does not ship AddressSanitizer;
  the findings above come from reading the code and from targeted hostile
  frames. A Linux build with `-fsanitize=address,undefined` replaying the
  existing `.rtcm3` captures would be worth doing and is cheap.
- **No fuzzing campaign.** `test_rtcm_hostile.c` is a handful of crafted
  frames, not coverage-guided fuzzing. The GUI already writes `.rtcm3`
  captures, so a libFuzzer corpus is available whenever this is picked up.
- **No review of the GUI's Win32 surface** beyond the frame path, and no
  review of the monitoring daemon's file writing.
- **No dependency audit beyond cJSON** — the AndroidX and Kotlin
  artefacts were not checked against advisories.
