# Writes SHA256SUMS-<tag>.txt for every asset in DIST_DIR.
#
# Run by the `release` target, not directly.  It exists as a separate
# script because `cmake -E sha256sum` writes to stdout and custom-command
# output cannot be redirected portably.
#
# The output format matches sha256sum(1), so a downloader can verify with
#   sha256sum -c SHA256SUMS-<tag>.txt
#
# Project: NTRIP-Analyser
# Author: Remko Welling, PE1MEW
# License: Apache License 2.0 with Commons Clause

if(NOT DEFINED DIST_DIR OR NOT DEFINED TAG)
    message(FATAL_ERROR "WriteChecksums.cmake needs -DDIST_DIR= and -DTAG=")
endif()

set(_sums_name "SHA256SUMS-${TAG}.txt")

file(GLOB _assets "${DIST_DIR}/*")
list(SORT _assets)

set(_out "")
foreach(_asset IN LISTS _assets)
    get_filename_component(_name "${_asset}" NAME)
    # Never checksum the checksum file, including a stale one from an
    # earlier run in the same directory.
    if(_name STREQUAL "${_sums_name}" OR _name MATCHES "^SHA256SUMS-")
        continue()
    endif()
    if(IS_DIRECTORY "${_asset}")
        continue()
    endif()
    file(SHA256 "${_asset}" _hash)
    string(APPEND _out "${_hash}  ${_name}\n")
endforeach()

if(_out STREQUAL "")
    message(WARNING "No release assets found in ${DIST_DIR}")
endif()

# LF endings, on every platform.
#
# file(WRITE) opens in text mode on Windows and turns each LF into CRLF,
# which breaks `sha256sum -c`: the trailing CR becomes part of the
# filename, so it reports "No such file or directory" for assets sitting
# right beside it.  A checksum file that cannot be checked is worse than
# none, and the failure only shows up on the platform that did not build
# it -- so it survives any amount of local testing.
#
# configure_file with NEWLINE_STYLE UNIX is the only writer CMake offers
# with newline control.  Substitution is left on because the content is
# hex digests and our own asset names, which contain no @VAR@ or ${VAR}.
file(WRITE "${DIST_DIR}/${_sums_name}.in" "${_out}")
configure_file("${DIST_DIR}/${_sums_name}.in"
               "${DIST_DIR}/${_sums_name}"
               NEWLINE_STYLE UNIX)
file(REMOVE "${DIST_DIR}/${_sums_name}.in")
message(STATUS "Wrote ${_sums_name}")
