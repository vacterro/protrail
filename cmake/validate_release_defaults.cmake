# T-33: build-time validation of the canonical Release Defaults source.
#
# The shipped executable must never fall back to unknown values, so a missing
# or malformed resources/release_defaults.json has to fail the build visibly
# instead of being discovered at runtime. This script is invoked from
# CMakeLists.txt at configure time; it also runs as a CTest case with a
# deliberately broken file to prove the gate itself is real.
#
# Usage: cmake -D DEFAULTS_FILE=<path> -P validate_release_defaults.cmake

if(NOT DEFINED DEFAULTS_FILE)
    message(FATAL_ERROR "RELEASE_DEFAULTS_INVALID: DEFAULTS_FILE was not provided")
endif()

if(NOT EXISTS "${DEFAULTS_FILE}")
    message(FATAL_ERROR "RELEASE_DEFAULTS_MISSING: ${DEFAULTS_FILE} does not exist")
endif()

file(READ "${DEFAULTS_FILE}" _json)
if(_json STREQUAL "")
    message(FATAL_ERROR "RELEASE_DEFAULTS_INVALID: ${DEFAULTS_FILE} is empty")
endif()

# Real JSON parsing (CMake 3.19+): a syntax error is reported, never assumed
# away. Every top-level key serialize_json() writes must be present, so a
# Release Defaults source that silently lost a whole config section fails here.
foreach(_key IN ITEMS schema_version master_enabled start_with_windows trail click render)
    string(JSON _value ERROR_VARIABLE _err GET "${_json}" "${_key}")
    if(_err)
        message(FATAL_ERROR
            "RELEASE_DEFAULTS_INVALID: ${DEFAULTS_FILE} is missing or malformed at key '${_key}': ${_err}")
    endif()
endforeach()

string(JSON _schema GET "${_json}" "schema_version")
if(_schema LESS 1)
    message(FATAL_ERROR "RELEASE_DEFAULTS_INVALID: schema_version ${_schema} is not a valid schema")
endif()
