# Refuses to package a release whose git tag disagrees with version.h.
#
# Run by the `release` target before anything is copied into dist/.
#
# This exists because v2.0.1 was tagged on a tree whose version.h still
# said 2.0.0: every binary from that tag reports the wrong release, and
# the assets would have carried the wrong name.  Packaging is the one
# moment where the tag and the version are both in hand, so it is the
# only place the two can be compared.
#
# An untagged HEAD is a development build and is allowed through with a
# warning -- being able to exercise packaging before tagging is the point.
# Set -DNTRIP_ALLOW_TAG_MISMATCH=ON to override deliberately.
#
# Project: NTRIP-Analyser
# Author: Remko Welling, PE1MEW
# License: Apache License 2.0 with Commons Clause

if(NOT DEFINED VERSION OR NOT DEFINED SRC_DIR)
    message(FATAL_ERROR "CheckReleaseTag.cmake needs -DVERSION= and -DSRC_DIR=")
endif()

find_package(Git QUIET)
if(NOT GIT_FOUND)
    message(WARNING "git not found; cannot check the release tag against version.h")
    return()
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" describe --exact-match --tags HEAD
    WORKING_DIRECTORY "${SRC_DIR}"
    OUTPUT_VARIABLE _tag
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE _rc)

if(NOT _rc EQUAL 0 OR _tag STREQUAL "")
    message(WARNING
        "HEAD carries no tag -- packaging ${VERSION} as a development build. "
        "Tag as v${VERSION} before publishing these assets.")
    return()
endif()

if(_tag STREQUAL "v${VERSION}")
    message(STATUS "Release tag ${_tag} matches version.h")
    return()
endif()

if(NTRIP_ALLOW_TAG_MISMATCH)
    message(WARNING
        "Tag ${_tag} disagrees with version.h (${VERSION}); "
        "continuing because NTRIP_ALLOW_TAG_MISMATCH is set.")
    return()
endif()

message(FATAL_ERROR
    "Release tag and version.h disagree.\n"
    "  git tag on HEAD : ${_tag}\n"
    "  version.h       : ${VERSION}  (expected tag v${VERSION})\n"
    "\n"
    "Binaries report version.h, so publishing this would ship assets whose "
    "name and self-reported version contradict the tag they came from.\n"
    "Fix src/core/version.h, or move the tag, then package again. To "
    "override deliberately, configure with -DNTRIP_ALLOW_TAG_MISMATCH=ON.")
