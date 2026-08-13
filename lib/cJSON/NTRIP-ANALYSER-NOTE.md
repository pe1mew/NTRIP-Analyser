# Vendored cJSON — what this copy is, and what was removed

Upstream cJSON 1.7.18 (MIT), vendored for the JSON configuration files
and the statistics snapshot. Only `cJSON.c` is compiled: by CMake, by
`build-gui.bat`, and by the Android NDK build alike.

## cJSON_Utils was deleted, deliberately

`cJSON_Utils.c` and `cJSON_Utils.h` are **not present** in this copy.
They implement JSON Pointer and JSON Patch, which this project does not
use — and 1.7.18 is within the range affected by
[CVE-2025-57052](https://github.com/advisories/GHSA-98j5-4649-rfv2)
(CVSS 9.8), an out-of-bounds access in
`decode_array_index_from_pointer()` in exactly that file.

Nothing here was ever exposed to it, because no build compiled the file.
It is gone so that no future build can compile it by accident, and so a
dependency scanner reading this tree does not report a vulnerability the
artefacts never carried.

If JSON Pointer support is ever needed, take it from an upstream release
that carries the fix rather than restoring these files.

Reviewed 2026-08-13; see `docs/security-review.md` F6.
