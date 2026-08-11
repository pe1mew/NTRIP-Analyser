# NTRIP-Analyser — one project, four artefacts over two shared libraries.
#
#   ntrip_core     — parsing, ephemerides, orbits, geodesy, statistics.
#                    No I/O, no platform headers: this is what the Android
#                    NDK compiles unchanged (design/architecture.md §2.2).
#   ntrip_session  — the NTRIP stream loop, shared by every frontend.
#
#   ntripanalyse       CLI          (Windows + Linux)
#   ntrip-analyser-gui Windows GUI  (WIN32 only)
#   ntrip-monitord     service      (UNIX only)
#
# Build:
#   cmake -B build && cmake --build build
#
# The version comes from src/core/version.h, parsed below, so the build
# system cannot disagree with the binaries about what release this is.

cmake_minimum_required(VERSION 3.13)

# ── Version, read from the single source of truth ─────────────────────
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/core/version.h" _version_h)
string(REGEX MATCH "NTRIP_VERSION_MAJOR[ \t]+([0-9]+)" _ "${_version_h}")
set(NTRIP_VER_MAJOR ${CMAKE_MATCH_1})
string(REGEX MATCH "NTRIP_VERSION_MINOR[ \t]+([0-9]+)" _ "${_version_h}")
set(NTRIP_VER_MINOR ${CMAKE_MATCH_1})
string(REGEX MATCH "NTRIP_VERSION_PATCH[ \t]+([0-9]+)" _ "${_version_h}")
set(NTRIP_VER_PATCH ${CMAKE_MATCH_1})

project(NTRIP-Analyser
        VERSION ${NTRIP_VER_MAJOR}.${NTRIP_VER_MINOR}.${NTRIP_VER_PATCH}
        LANGUAGES C)
message(STATUS "NTRIP-Analyser ${PROJECT_VERSION} (from src/core/version.h)")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# Strict -std=c99 hides POSIX declarations on glibc; without these,
# clock_gettime, getaddrinfo and strncasecmp fail to resolve.
if(UNIX)
    add_compile_definitions(_POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
endif()
add_compile_definitions(_USE_MATH_DEFINES)

# ── cJSON (vendored) ──────────────────────────────────────────────────
add_library(cjson STATIC lib/cJSON/cJSON.c)
target_include_directories(cjson PUBLIC lib/cJSON)

# ── ntrip_core ────────────────────────────────────────────────────────
add_library(ntrip_core STATIC
    src/core/ns_stats.c
    src/core/rtcm3x_parser.c
    src/core/sv_ephemeris.c
    src/core/sv_orbit.c
    src/core/rinex_nav.c
    src/core/sky_collect.c
    src/core/sky_render.c
    src/core/config.c
    src/core/nmea_parser.c
)
target_include_directories(ntrip_core PUBLIC src)
target_link_libraries(ntrip_core PUBLIC cjson)
if(UNIX)
    target_link_libraries(ntrip_core PUBLIC m)
endif()

# ── ntrip_session (with the NTRIP protocol + client beneath it) ───────
add_library(ntrip_session STATIC
    src/session/ntrip_session.c
    src/net/ntrip_proto.c
    src/net/ntrip_handler.c
)
target_link_libraries(ntrip_session PUBLIC ntrip_core)
if(WIN32)
    target_link_libraries(ntrip_session PUBLIC ws2_32)
endif()

# ── CLI ───────────────────────────────────────────────────────────────
add_executable(ntripanalyse
    src/cli/main.c
    src/cli/cli_help.c
    src/cli/cli_stream.c
)
target_link_libraries(ntripanalyse PRIVATE ntrip_session)
if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(ntripanalyse PRIVATE Threads::Threads)
    # The historical Linux binary name.
    set_target_properties(ntripanalyse PROPERTIES OUTPUT_NAME ntripanalyser)
endif()

# ── Windows GUI ───────────────────────────────────────────────────────
if(WIN32)
    add_executable(ntrip-analyser-gui WIN32
        gui/gui_main.c
        gui/gui_layout.c
        gui/gui_events.c
        gui/gui_thread.c
        gui/gui_log.c
        gui/gui_parsers.c
        gui/gui_detail.c
        gui/gui_sky_window.c
        gui/gui_snapshot.c
        gui/gui_sv_detail.c
        gui/gui_vrs_window.c
        gui/gui_signal_window.c
        gui/gui_hist_window.c
        gui/resource.rc
    )
    target_include_directories(ntrip-analyser-gui PRIVATE gui)
    target_link_libraries(ntrip-analyser-gui PRIVATE
        ntrip_session comctl32 comdlg32 gdiplus)
    # windres must find core/version.h, which resource.rc includes.
    set_source_files_properties(gui/resource.rc PROPERTIES
        COMPILE_FLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}/src")
endif()

# ── Monitoring service ────────────────────────────────────────────────
if(UNIX)
    add_executable(ntrip-monitord service/ntrip-monitord.c)
    target_link_libraries(ntrip-monitord PRIVATE ntrip_session)

    install(TARGETS ntrip-monitord RUNTIME DESTINATION sbin)
    install(FILES service/ntrip-monitord.service
            DESTINATION /etc/systemd/system)
    install(FILES service/ntrip-monitord.sysusers
            DESTINATION /usr/lib/sysusers.d RENAME ntrip-monitord.conf)
    install(PROGRAMS service/munin/ntrip_monitor
            DESTINATION share/munin/plugins)
endif()

install(TARGETS ntripanalyse RUNTIME DESTINATION bin)
