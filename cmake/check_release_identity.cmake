# Release identity gate: every public release surface names the SAME version.
# Run as: cmake -D SOURCE_DIR=<repo> -P check_release_identity.cmake
#
# The root VERSION file is the authority (project(), VERSIONINFO and the
# packaging pipeline read it directly). This script fails when a document
# surface drifts from it, so a split identity such as VERSION=0.1.1 with a
# README or CHANGELOG still announcing 0.1.0 cannot pass CTest.

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "RELEASE_IDENTITY: SOURCE_DIR not set")
endif()

file(STRINGS "${SOURCE_DIR}/VERSION" _version LIMIT_COUNT 1)
string(STRIP "${_version}" _version)
if(NOT _version MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "RELEASE_IDENTITY: VERSION holds '${_version}'")
endif()

set(_failures "")

# README badge: the first bold **vX.Y.Z** line.
file(STRINGS "${SOURCE_DIR}/README.md" _badge REGEX "^\\*\\*v[0-9]+\\.[0-9]+\\.[0-9]+\\*\\*$" LIMIT_COUNT 1)
if(NOT _badge STREQUAL "**v${_version}**")
    list(APPEND _failures "README.md badge '${_badge}' != '**v${_version}**'")
endif()

# CHANGELOG: the newest (first) "## X.Y.Z" entry.
file(STRINGS "${SOURCE_DIR}/CHANGELOG.md" _entry REGEX "^## [0-9]+\\.[0-9]+\\.[0-9]+" LIMIT_COUNT 1)
if(NOT _entry MATCHES "^## ${_version}( |$)")
    list(APPEND _failures "CHANGELOG.md newest entry '${_entry}' is not ${_version}")
endif()

# Release notes for the current version.
if(NOT EXISTS "${SOURCE_DIR}/RELEASE_NOTES_v${_version}.md")
    list(APPEND _failures "RELEASE_NOTES_v${_version}.md missing")
else()
    file(STRINGS "${SOURCE_DIR}/RELEASE_NOTES_v${_version}.md" _title LIMIT_COUNT 1)
    if(NOT _title STREQUAL "# ProTrail v${_version}")
        list(APPEND _failures "RELEASE_NOTES_v${_version}.md title '${_title}'")
    endif()
endif()

if(_failures)
    list(JOIN _failures "\n  " _joined)
    message(FATAL_ERROR "RELEASE_IDENTITY_SPLIT for ${_version}:\n  ${_joined}")
endif()
message(STATUS "RELEASE_IDENTITY_OK ${_version}")
