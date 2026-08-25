# Vendored Mbed TLS — what this copy is, and what was removed

Upstream **Mbed TLS 3.6.7** (Apache-2.0), the long-term-support line,
vendored for TLS to the caster (design/tls.md; the rollout is
design/work-items/tls-rollout.md). Taken from the official release
asset `mbedtls-3.6.7.tar.bz2`, whose SHA-256 matched the published
`mbedtls-3.6.7-sha256sum.txt`:

    a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6

Only `include/` and `library/` are present, with the LICENSE and
README. Tests, programs, docs, scripts and `3rdparty/` (the Everest
and p256-m accelerators, off in the default configuration) were not
brought over: nothing here compiles them, and this note exists so
nobody goes looking.

## Built by our build, not by theirs

Upstream's CMake is not used. Both builds — desktop CMake and the
Android NDK — compile `library/*.c` into a static library of their
own declaring (see each CMakeLists), against the default
configuration in `include/mbedtls/mbedtls_config.h`, unmodified.
That is the same arrangement cJSON has, at larger scale, and it keeps
the vendor tree free of build machinery that would otherwise need
Python and a framework checkout.

## Why 3.6 and not 4.x

4.x splits the crypto into the separate TF-PSA-Crypto repository and
retires parts of the classic API. 3.6 is the LTS branch (supported
into 2027), single-tree, and carries `mbedtls_ssl_set_hostname` —
the call design/tls.md names as non-negotiable.

## Refreshing this copy

Take the next 3.6.x **release asset** tarball (not a git snapshot),
verify its published SHA-256, replace `include/` and `library/`
whole, and update this note's version and hash. The release check
warns when the CA bundle ages; this library ages on CVE announcements
— https://mbed-tls.readthedocs.io/en/latest/security-advisories/
