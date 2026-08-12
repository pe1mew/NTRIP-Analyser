# NTRIP-Analyser — one project, four artefacts over two shared libraries.
#
#   ntrip_core     — parsing, ephemerides, orbits, geodesy, statistics.
#                    No I/O, no platform headers: this is what the Android
#                    NDK compiles unchanged (design/architecture.md §2.2).
#   ntrip_session  — the NTRIP stream loop, shared by every frontend.
#
#   ntrip-analyser      CLI          (Windows + Linux)
#   ntrip-analyser-gui  Windows GUI  (WIN32 only)
#   ntrip-monitord      service      (UNIX only)
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

# ── Where executables land ────────────────────────────────────────────
# bin/, alongside build-gui.bat and the VS Code tasks, which have always
# written there.  One stable location for every build system.
#
# The reason this matters is not tidiness.  The programs look for
# config.json in the working directory, so configs end up sitting beside
# the executable -- and if that is a build directory, a clean rebuild
# deletes them.  A build tree must stay disposable, so the binaries come
# out of it instead.
#
# bin/ is a tracked directory (bin/readme.md is a placeholder, since git
# does not track empty directories) and .gitignore keeps everything else
# in it out of the repository.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
# Multi-config generators (Visual Studio, Ninja Multi-Config) otherwise
# append Debug/ or Release/ to the path above; pin each so the binary is
# in the same place whichever generator built it.
foreach(_cfg ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER "${_cfg}" _CFG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_CFG} "${CMAKE_SOURCE_DIR}/bin")
endforeach()

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
    src/core/sv_track.c
    src/core/kpi.c
    src/core/vrs_check.c
    src/core/iono.c
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
add_executable(ntrip-analyser
    src/cli/main.c
    src/cli/cli_help.c
    src/cli/cli_stream.c
)
target_link_libraries(ntrip-analyser PRIVATE ntrip_session)
if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(ntrip-analyser PRIVATE Threads::Threads)
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
        gui/gui_iono_window.c
        gui/gui_iono_sky_window.c
        gui/resource.rc
    )
    target_include_directories(ntrip-analyser-gui PRIVATE gui)
    target_link_libraries(ntrip-analyser-gui PRIVATE
        ntrip_session comctl32 comdlg32 gdiplus)
    # windres needs two search paths: src/ for core/version.h, which
    # resource.rc includes, and the repository root, because the manifest
    # is referenced as "gui/ntrip-gui.exe.manifest" -- a path relative to
    # the root, where build-gui.bat runs windres from.  CMake runs it in
    # the build directory instead, so without this the manifest is not
    # found.  Keeping resource.rc's path root-relative means both build
    # systems work from one file.
    set_source_files_properties(gui/resource.rc PROPERTIES
        COMPILE_FLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}/src -I${CMAKE_CURRENT_SOURCE_DIR}"
        # CMake scans .rc files for #include, but ICON and the manifest
        # reference their files by a bare path, which the scanner does not
        # see.  Without these, editing the icon leaves windres thinking it
        # is up to date and the executable keeps the previous one -- a
        # stale-build trap that looks like the icon change silently
        # failing.
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/gui/ntrip-analyser.ico;${CMAKE_CURRENT_SOURCE_DIR}/gui/ntrip-gui.exe.manifest")
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

install(TARGETS ntrip-analyser RUNTIME DESTINATION bin)

# ── API documentation ─────────────────────────────────────────────────
# Optional: the target only exists when Doxygen is installed, so a build
# never fails for want of a documentation tool.
#
#   cmake --build build --target docs
#
# The Doxyfile takes the version from the NTRIP_VERSION environment
# variable rather than hardcoding one, so it cannot contradict
# version.h.  This target supplies it; a bare `doxygen` run works too and
# simply omits the version.
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    add_custom_target(docs
        COMMAND ${CMAKE_COMMAND} -E env
                "NTRIP_VERSION=${PROJECT_VERSION}"
                ${DOXYGEN_EXECUTABLE}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Generating API documentation with Doxygen ${DOXYGEN_VERSION}"
        VERBATIM)
else()
    message(STATUS "Doxygen not found; the 'docs' target is unavailable")
endif()

# ── Self-contained Windows binaries ───────────────────────────────────
# MinGW links libwinpthread-1.dll dynamically by default, and that DLL is
# not part of Windows.  A release .exe built without this starts with a
# missing-DLL dialog on any machine that has no MinGW installed -- which
# is every machine we ship to.  Linking statically makes each executable
# a single self-contained file, which is what lets the release ship bare
# .exe assets instead of a zip with runtime DLLs beside them.
if(WIN32)
    target_link_options(ntrip-analyser PRIVATE -static)
    if(TARGET ntrip-analyser-gui)
        target_link_options(ntrip-analyser-gui PRIVATE -static)
    endif()
endif()

# ── Release packaging ─────────────────────────────────────────────────
#
# Two naming rules, deliberately different:
#
#   installed / typed   ntrip-analyser
#   release asset       ntrip-analyser-2.0.1-linux-x64
#
# The installed name must never carry a version or a platform.  The
# systemd unit, the Munin plugin, the documentation and any user's script
# all refer to the command by name, and every one of them breaks if that
# name changes each release.
#
# A downloaded file has the opposite problem: it lands in a Downloads
# folder stripped of every trace of where it came from, so it has to say
# what it is on its face.  That is also the first thing anyone needs when
# a bug report says only "the analyser crashes".
#
# Build the assets with:  cmake --build build --target release

if(WIN32)
    set(NTRIP_REL_OS "windows")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(NTRIP_REL_OS "linux")
else()
    string(TOLOWER "${CMAKE_SYSTEM_NAME}" NTRIP_REL_OS)
endif()

# Human-facing architecture names.  These are read by a person choosing a
# download, not by a toolchain, so "x64" rather than "x86_64".
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    set(NTRIP_REL_ARCH "x64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    set(NTRIP_REL_ARCH "arm64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(armv7l|armv7|arm)$")
    set(NTRIP_REL_ARCH "armhf")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i386|i686|x86)$")
    set(NTRIP_REL_ARCH "x86")
else()
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" NTRIP_REL_ARCH)
endif()

set(NTRIP_REL_TAG  "${PROJECT_VERSION}-${NTRIP_REL_OS}-${NTRIP_REL_ARCH}")
set(NTRIP_DIST_DIR "${CMAKE_BINARY_DIR}/dist")

add_custom_target(release
    # Checked first, so a mismatch fails before anything is packaged.
    COMMAND ${CMAKE_COMMAND}
            -DVERSION=${PROJECT_VERSION}
            -DSRC_DIR=${CMAKE_SOURCE_DIR}
            -DNTRIP_ALLOW_TAG_MISMATCH=$<BOOL:${NTRIP_ALLOW_TAG_MISMATCH}>
            -P "${CMAKE_SOURCE_DIR}/cmake/CheckReleaseTag.cmake"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${NTRIP_DIST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory   "${NTRIP_DIST_DIR}"
    COMMENT "Packaging ${PROJECT_VERSION} for ${NTRIP_REL_OS}-${NTRIP_REL_ARCH}"
    VERBATIM)
add_dependencies(release ntrip-analyser)

add_custom_command(TARGET release POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:ntrip-analyser>"
        "${NTRIP_DIST_DIR}/ntrip-analyser-${NTRIP_REL_TAG}${CMAKE_EXECUTABLE_SUFFIX}"
    # A ready-to-edit configuration ships beside the binaries: the
    # programs read config.json from the working directory, and a new
    # user's first hurdle is knowing what that file should contain.
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_SOURCE_DIR}/bin/exampleConfig.json"
        "${NTRIP_DIST_DIR}/exampleConfig.json"
    VERBATIM)

if(TARGET ntrip-analyser-gui)
    add_dependencies(release ntrip-analyser-gui)
    add_custom_command(TARGET release POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:ntrip-analyser-gui>"
            "${NTRIP_DIST_DIR}/ntrip-analyser-gui-${NTRIP_REL_TAG}${CMAKE_EXECUTABLE_SUFFIX}"
        VERBATIM)
endif()

# The daemon is the one artefact that is not just a binary: without the
# unit file, the sysusers fragment, the Munin plugin and an example
# config, a bare executable is about a quarter of the product.  So it
# ships as a tarball that mirrors what `make install` would place.
if(TARGET ntrip-monitord)
    add_dependencies(release ntrip-monitord)
    set(_svc_stage "${CMAKE_BINARY_DIR}/_pkg/ntrip-monitord-${NTRIP_REL_TAG}")
    add_custom_command(TARGET release POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${_svc_stage}"
        COMMAND ${CMAKE_COMMAND} -E make_directory   "${_svc_stage}"
        COMMAND ${CMAKE_COMMAND} -E copy
                "$<TARGET_FILE:ntrip-monitord>"
                "${CMAKE_SOURCE_DIR}/service/ntrip-monitord.service"
                "${CMAKE_SOURCE_DIR}/service/ntrip-monitord.sysusers"
                "${CMAKE_SOURCE_DIR}/service/monitord.example.json"
                "${CMAKE_SOURCE_DIR}/service/munin/ntrip_monitor"
                "${CMAKE_SOURCE_DIR}/docs/service.md"
                "${_svc_stage}/"
        COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_BINARY_DIR}/_pkg"
                ${CMAKE_COMMAND} -E tar cfz
                "${NTRIP_DIST_DIR}/ntrip-monitord-${NTRIP_REL_TAG}.tar.gz"
                "ntrip-monitord-${NTRIP_REL_TAG}"
        VERBATIM)
endif()

# Per-platform checksum file: Windows and Linux assets are built on
# different machines, so a single shared SHA256SUMS would be overwritten
# by whichever upload happened last.
add_custom_command(TARGET release POST_BUILD
    COMMAND ${CMAKE_COMMAND}
            -DDIST_DIR=${NTRIP_DIST_DIR}
            -DTAG=${NTRIP_REL_TAG}
            -P "${CMAKE_SOURCE_DIR}/cmake/WriteChecksums.cmake"
    COMMAND ${CMAKE_COMMAND} -E echo "Release assets in ${NTRIP_DIST_DIR}"
    VERBATIM)
