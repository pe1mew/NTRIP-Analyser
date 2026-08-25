# The NDK build of the shared C core, for the Android app.
#
# The core was written to be portable -- no platform headers, no I/O
# (design/architecture.md §2.2) -- so this file compiles the repository's
# own sources directly rather than vendoring a copy.  There is exactly
# one implementation of the RTCM decoder, the statistics snapshot and the
# KPI engine, and the phone uses it unchanged.
#
# The desktop CMakeLists.txt is not reused here because it also declares
# the CLI, the Win32 GUI and the UNIX daemon, none of which build for
# Android.  Keeping this list explicit is the price of that.

cmake_minimum_required(VERSION 3.22.1)
project(ntrip_android C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Optimized C even in debug APKs.  The C core is debugged on the
# desktop, through the test suite -- never through a debug APK -- and
# unoptimized mbedTLS made a TLS handshake take half a minute on the
# 2019 test handset, which read as a hang and cost an afternoon (L5).
string(APPEND CMAKE_C_FLAGS_DEBUG " -O2")

# ../../../../..  ->  android/app/src/main/cpp  to the repository root
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.."
                       ABSOLUTE)

# ── Mbed TLS (vendored; see lib/mbedtls/NTRIP-ANALYSER-NOTE.md) ──────
# The same arrangement as the desktop build: library/*.c compiled by
# this target against the default configuration, upstream's own CMake
# unused.  The GLOB is read at configure time -- a Gradle sync or clean
# build picks up a refreshed vendor copy.
file(GLOB MBEDTLS_SOURCES ${REPO_ROOT}/lib/mbedtls/library/*.c)

add_library(ntrip_android SHARED
    ntrip_bridge.c
    jni_glue.c

    # ntrip_core
    ${REPO_ROOT}/src/core/ns_stats.c
    ${REPO_ROOT}/src/core/ns_failure.c
    ${REPO_ROOT}/src/core/rtcm3x_parser.c
    ${REPO_ROOT}/src/core/sv_ephemeris.c
    ${REPO_ROOT}/src/core/sv_orbit.c
    ${REPO_ROOT}/src/core/sv_track.c
    ${REPO_ROOT}/src/core/kpi.c
    ${REPO_ROOT}/src/core/sourcetable.c
    ${REPO_ROOT}/src/core/vrs_check.c
    ${REPO_ROOT}/src/core/station_report.c
    ${REPO_ROOT}/src/core/iono.c
    ${REPO_ROOT}/src/core/rinex_nav.c
    ${REPO_ROOT}/src/core/sky_collect.c
    ${REPO_ROOT}/src/core/sky_render.c
    ${REPO_ROOT}/src/core/config.c
    ${REPO_ROOT}/src/core/nmea_parser.c

    # ntrip_session
    ${REPO_ROOT}/src/session/ntrip_session.c
    ${REPO_ROOT}/src/session/ns_transport.c
    ${REPO_ROOT}/src/session/ns_ca_bundle.c
    ${REPO_ROOT}/src/net/ntrip_proto.c
    ${REPO_ROOT}/src/net/ntrip_handler.c

    ${REPO_ROOT}/lib/cJSON/cJSON.c
    ${MBEDTLS_SOURCES}
)

target_include_directories(ntrip_android PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${REPO_ROOT}/src
    ${REPO_ROOT}/lib/cJSON
    ${REPO_ROOT}/lib/mbedtls/include
    ${REPO_ROOT}/lib/mbedtls/library
)

# Bionic declares clock_gettime, getaddrinfo and strncasecmp behind the
# same POSIX guards glibc uses, and -std=c99 hides them without this.
target_compile_definitions(ntrip_android PRIVATE
    _POSIX_C_SOURCE=200809L
    _DEFAULT_SOURCE
    _USE_MATH_DEFINES
)

# ── 16 KB memory pages ───────────────────────────────────────────────
# Android devices launching with Android 15 may use 16 KB pages, and a
# shared library laid out for 4 KB ones cannot be loaded on them: the
# app does not misbehave, it fails to start. Play has required 16 KB
# support of every app targeting Android 15+ since November 2025.
#
# Measured rather than assumed: the library this build produced had
# LOAD segments at 0x1000. With these flags they are at 0x4000, which
# `llvm-readelf -l` shows and `docs/RUNBOOK.md` says how to check.
target_link_options(ntrip_android PRIVATE
    "-Wl,-z,max-page-size=16384"
    "-Wl,-z,common-page-size=16384"
)

target_link_libraries(ntrip_android log m)
