# Stage the sound catalog beside the built executable.
# Architecture: LOCK -> PREFLIGHT -> INVENTORY -> CLASSIFY -> VALIDATE -> APPLY -> COMMIT
#
# Runs as:
#   cmake -DSTAGE_SOUND_SOURCE_DIR=... -DSTAGE_SOUND_DEST_DIR=... -DSTAGE_SOUND_MANIFEST_FILE=... -P stage_sound.cmake
#
# Variable names deliberately avoid the CMAKE_ prefix, which is reserved for
# CMake itself and mangles values passed from custom command/target invocations.
#
# Authority model (T-118): the committed manifest and the source catalog are the
# ONLY authority for what this build owns. A transaction journal, an allocation
# receipt, a rollback backup and a provenance anchor are all files inside the
# build tree, writable by any process that can write there, so none of them can
# be an authority source. There is therefore no transaction workspace and no
# rollback path: a rollback that deletes a destination file is justified only by
# records an intruder can forge. An interrupted run is reconciled by the next
# run, which is idempotent from source + manifest, so recovery never destroys
# data.
#
# Invariants:
#  * Missing source directory -> clean skip (portable build without sound/).
#  * Concurrency isolation: file lock prevents racing staging invocations.
#  * Semicolon / control characters / traversal in source filenames rejected fail-closed before mutation.
#  * Manifest and destination directories must be plain directories: a reparse point
#    (symlink, junction, mount) is an ambiguous alias and is refused before any mutation.
#  * Probe uses collision-safe non-destructive temp file; preexisting operator files never touched.
#  * Manifest records "<sha256> <name>" per owned file.
#  * Existing destination content must match previously committed manifest hash to be build-owned.
#  * When the destination already holds the source bytes, no write is made.
#    Ownership of the name comes from the committed manifest, so an entry that
#    names the file keeps it and refreshes its content hash; a file no committed
#    entry names is left unclaimed, because byte equality proves content, never
#    ownership, and would otherwise grant deletion authority no one granted.
#  * Operator-modified or unowned destination files are NEVER overwritten or deleted.
#  * Hashless legacy or corrupted manifest entries never grant deletion authority.
#  * All copies verify destination bytes: hash(destination) == hash(expected_source).
#  * Deletion is bounded to files the committed manifest proves owned AND whose
#    current bytes still match that proof; every deleted path is re-checked against
#    the canonical destination directory immediately before the removal.
#  * Nothing outside the manifest directory and the destination directory is ever
#    written or removed; no cleanup path runs on a no-op staging.
#  * Accurate final destination count reported after all additions and removals.

if(NOT DEFINED STAGE_SOUND_SOURCE_DIR OR "${STAGE_SOUND_SOURCE_DIR}" STREQUAL "")
    message(FATAL_ERROR "STAGE_SOUND_SOURCE_DIR must be set")
endif()
if(NOT DEFINED STAGE_SOUND_DEST_DIR OR "${STAGE_SOUND_DEST_DIR}" STREQUAL "")
    message(FATAL_ERROR "STAGE_SOUND_DEST_DIR must be set")
endif()
if(NOT DEFINED STAGE_SOUND_MANIFEST_FILE OR "${STAGE_SOUND_MANIFEST_FILE}" STREQUAL "")
    message(FATAL_ERROR "STAGE_SOUND_MANIFEST_FILE must be set")
endif()

set(SOUND_SOURCE_DIR "${STAGE_SOUND_SOURCE_DIR}")
set(SOUND_DEST_DIR "${STAGE_SOUND_DEST_DIR}")
set(MANIFEST_FILE "${STAGE_SOUND_MANIFEST_FILE}")

# Clean skip if source directory does not exist
if(NOT EXISTS "${SOUND_SOURCE_DIR}")
    message(STATUS "Sound source directory does not exist, skipping staging")
    return()
endif()

# ---------------------------------------------------------------------------
# Helper: Filename and SHA256 validation
# ---------------------------------------------------------------------------
function(validate_wav_basename _name _out_valid)
    set(${_out_valid} FALSE PARENT_SCOPE)
    if(NOT _name OR "${_name}" STREQUAL "")
        return()
    endif()
    # Reject semicolons, control characters, path separators, drive colons, and traversal
    if(_name MATCHES "[;\r\n\t/\\\\]" OR _name MATCHES "(:|\\.\\.)")
        return()
    endif()
    get_filename_component(_cname "${_name}" NAME)
    if(NOT "${_cname}" STREQUAL "${_name}")
        return()
    endif()
    string(TOLOWER "${_name}" _lname)
    if(NOT _lname MATCHES "\\.wav$")
        return()
    endif()
    set(${_out_valid} TRUE PARENT_SCOPE)
endfunction()

function(validate_sha256_hash _hash _out_valid)
    set(${_out_valid} FALSE PARENT_SCOPE)
    string(LENGTH "${_hash}" _hlen)
    if(_hlen EQUAL 64 AND _hash MATCHES "^[a-fA-F0-9]+$")
        set(${_out_valid} TRUE PARENT_SCOPE)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Helper: a directory must be a plain directory, never a reparse point.
#
# A reparse point (symlink, junction, volume mount) makes the requested path an
# alias for some other object. Comparing REALPATH against the requested path
# catches every alias type on every platform instead of only the ones a
# type predicate can name, which is what made the old prefix check bypassable.
# ---------------------------------------------------------------------------
function(dir_is_plain _path _out_plain)
    set(${_out_plain} FALSE PARENT_SCOPE)
    get_filename_component(_abs "${_path}" ABSOLUTE)
    if(NOT EXISTS "${_abs}" OR NOT IS_DIRECTORY "${_abs}")
        return()
    endif()
    get_filename_component(_real "${_abs}" REALPATH)
    if(WIN32)
        string(TOLOWER "${_abs}" _abs_n)
        file(TO_CMAKE_PATH "${_abs_n}" _abs_n)
        string(TOLOWER "${_real}" _real_n)
        file(TO_CMAKE_PATH "${_real_n}" _real_n)
    else()
        set(_abs_n "${_abs}")
        set(_real_n "${_real}")
    endif()
    string(REGEX REPLACE "/+$" "" _abs_n "${_abs_n}")
    string(REGEX REPLACE "/+$" "" _real_n "${_real_n}")
    if("${_abs_n}" STREQUAL "${_real_n}")
        set(${_out_plain} TRUE PARENT_SCOPE)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Canonical destination resolution for boundary checking
# ---------------------------------------------------------------------------
if(NOT EXISTS "${SOUND_DEST_DIR}")
    file(MAKE_DIRECTORY "${SOUND_DEST_DIR}")
endif()
if(NOT IS_DIRECTORY "${SOUND_DEST_DIR}")
    message(FATAL_ERROR "Invalid staging destination path: '${SOUND_DEST_DIR}' is not a directory")
endif()
get_filename_component(DEST_CANONICAL "${SOUND_DEST_DIR}" REALPATH)
if(WIN32)
    string(TOLOWER "${DEST_CANONICAL}" DEST_CANONICAL_NORM)
    file(TO_CMAKE_PATH "${DEST_CANONICAL_NORM}" DEST_CANONICAL_NORM)
else()
    set(DEST_CANONICAL_NORM "${DEST_CANONICAL}")
endif()

function(is_bounded_to_dest _file_path _out_bounded)
    set(${_out_bounded} FALSE PARENT_SCOPE)
    get_filename_component(_dir "${_file_path}" DIRECTORY)
    if(NOT EXISTS "${_dir}")
        return()
    endif()
    get_filename_component(_real_dir "${_dir}" REALPATH)
    if(WIN32)
        string(TOLOWER "${_real_dir}" _norm_dir)
        file(TO_CMAKE_PATH "${_norm_dir}" _norm_dir)
    else()
        set(_norm_dir "${_real_dir}")
    endif()
    if("${_norm_dir}" STREQUAL "${DEST_CANONICAL_NORM}")
        set(${_out_bounded} TRUE PARENT_SCOPE)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# 1. LOCK: Concurrency isolation (Invariant H)
# ---------------------------------------------------------------------------
if(NOT DEFINED STAGE_SOUND_LOCK_TIMEOUT)
    set(STAGE_SOUND_LOCK_TIMEOUT 30)
endif()

get_filename_component(MANIFEST_DIR "${MANIFEST_FILE}" DIRECTORY)
if(NOT MANIFEST_DIR)
    set(MANIFEST_DIR ".")
endif()

if(NOT EXISTS "${MANIFEST_DIR}")
    file(MAKE_DIRECTORY "${MANIFEST_DIR}")
endif()

dir_is_plain("${MANIFEST_DIR}" _man_dir_plain)
if(NOT _man_dir_plain)
    message(FATAL_ERROR "Invalid staging manifest path: '${MANIFEST_DIR}' is not a plain directory (reparse point or alias detected). Refusing to stage against a redirected manifest location.")
endif()

dir_is_plain("${SOUND_DEST_DIR}" _dest_dir_plain)
if(NOT _dest_dir_plain)
    message(FATAL_ERROR "Invalid staging destination path: '${SOUND_DEST_DIR}' is not a plain directory (reparse point or alias detected). Refusing to write into a redirected destination.")
endif()

get_filename_component(MANIFEST_DIR_CANONICAL "${MANIFEST_DIR}" REALPATH)
get_filename_component(MANIFEST_NAME "${MANIFEST_FILE}" NAME)
set(MANIFEST_CANONICAL "${MANIFEST_DIR_CANONICAL}/${MANIFEST_NAME}")
if(WIN32)
    string(TOLOWER "${MANIFEST_CANONICAL}" MANIFEST_CANONICAL_NORM)
    file(TO_CMAKE_PATH "${MANIFEST_CANONICAL_NORM}" MANIFEST_CANONICAL_NORM)
else()
    set(MANIFEST_CANONICAL_NORM "${MANIFEST_CANONICAL}")
endif()

set(STAGE_LOCK_FILE "${MANIFEST_FILE}.lock")
file(LOCK "${STAGE_LOCK_FILE}" GUARD PROCESS TIMEOUT "${STAGE_SOUND_LOCK_TIMEOUT}" RESULT_VARIABLE _lock_res)
if(NOT _lock_res EQUAL 0)
    message(FATAL_ERROR "Failed to acquire staging lock for '${MANIFEST_FILE}': another staging process is active")
endif()

# ---------------------------------------------------------------------------
# 2. PREFLIGHT & VALIDATE: manifest target type & non-destructive probe
# ---------------------------------------------------------------------------
if(EXISTS "${MANIFEST_FILE}" AND IS_DIRECTORY "${MANIFEST_FILE}")
    message(FATAL_ERROR "Invalid staging manifest path: '${MANIFEST_FILE}' is a directory")
endif()

if(EXISTS "${MANIFEST_DIR}" AND NOT IS_DIRECTORY "${MANIFEST_DIR}")
    message(FATAL_ERROR "Invalid staging manifest path: '${MANIFEST_DIR}' is not a directory")
endif()

if(NOT EXISTS "${MANIFEST_DIR}")
    file(MAKE_DIRECTORY "${MANIFEST_DIR}")
    if(NOT IS_DIRECTORY "${MANIFEST_DIR}")
        message(FATAL_ERROR "Invalid staging manifest path: cannot create directory '${MANIFEST_DIR}'")
    endif()
endif()

# Non-destructive writability probe: exclusive unique temp name, never touch existing files
set(_probe_ok FALSE)
foreach(_try_idx RANGE 1 100)
    string(TIMESTAMP _ts "%Y%m%d_%H%M%S")
    string(RANDOM LENGTH 16 ALPHABET "0123456789abcdef" _probe_rand)
    set(_probe_file "${MANIFEST_DIR}/.stage_probe_${_ts}_${_try_idx}_${_probe_rand}.tmp")
    if(NOT EXISTS "${_probe_file}")
        set(_probe_token "PROTRAIL_STAGE_PROBE_${_ts}_${_try_idx}_${_probe_rand}")
        file(WRITE "${_probe_file}" "${_probe_token}\n")
        if(EXISTS "${_probe_file}")
            file(READ "${_probe_file}" _read_token)
            string(STRIP "${_read_token}" _read_token)
            if("${_read_token}" STREQUAL "${_probe_token}")
                set(_probe_ok TRUE)
            endif()
            file(REMOVE "${_probe_file}")
        endif()
        break()
    endif()
endforeach()
if(NOT _probe_ok)
    message(FATAL_ERROR "Invalid staging manifest path: '${MANIFEST_FILE}' is not writable")
endif()

# ---------------------------------------------------------------------------
# 3. INVENTORY: Source catalog & filename contracts
# ---------------------------------------------------------------------------
if(CMAKE_HOST_WIN32)
    execute_process(
        COMMAND cmd.exe /c dir /b /a-d
        WORKING_DIRECTORY "${SOUND_SOURCE_DIR}"
        OUTPUT_VARIABLE _raw_dir_listing
        RESULT_VARIABLE _dir_res
        ERROR_QUIET
    )
    if(_dir_res EQUAL 0 AND _raw_dir_listing)
        if(_raw_dir_listing MATCHES ";")
            message(FATAL_ERROR "Invalid sound filename: source directory contains a filename with a semicolon or control character")
        endif()
    endif()
endif()

file(GLOB RAW_SOURCE_PATHS LIST_DIRECTORIES false "${SOUND_SOURCE_DIR}/*.wav")
set(SOURCE_KEYS "")

foreach(src_path IN LISTS RAW_SOURCE_PATHS)
    get_filename_component(filename "${src_path}" NAME)
    validate_wav_basename("${filename}" _src_valid)
    if(NOT _src_valid)
        message(FATAL_ERROR "Invalid sound filename: '${filename}' contains invalid characters or traversal components")
    endif()
    file(SHA256 "${src_path}" _src_hash)
    string(TOLOWER "${_src_hash}" _src_hash)
    string(SHA256 _k "${filename}")
    list(APPEND SOURCE_KEYS "${_k}")
    set(SRC_NAME_${_k} "${filename}")
    set(SRC_HASH_${_k} "${_src_hash}")
endforeach()

# ---------------------------------------------------------------------------
# 4. INVENTORY: Prior manifest
# ---------------------------------------------------------------------------
set(OLD_MANIFEST_KEYS "")
set(OLD_RAW_MANIFEST_CONTENT "")

if(EXISTS "${MANIFEST_FILE}")
    file(READ "${MANIFEST_FILE}" OLD_RAW_MANIFEST_CONTENT)
    file(STRINGS "${MANIFEST_FILE}" RAW_LINES)
    foreach(line IN LISTS RAW_LINES)
        string(STRIP "${line}" trimmed)
        if(trimmed AND NOT trimmed MATCHES "^#")
            if(trimmed MATCHES "^([a-fA-F0-9]+)[ \t]+(.+)$")
                string(LENGTH "${CMAKE_MATCH_1}" _m_hlen)
                if(_m_hlen EQUAL 64)
                    string(TOLOWER "${CMAKE_MATCH_1}" _m_hash)
                    set(_m_file "${CMAKE_MATCH_2}")
                    string(SHA256 _k "${_m_file}")
                    list(APPEND OLD_MANIFEST_KEYS "${_k}")
                    set(OLD_NAME_${_k} "${_m_file}")
                    set(OLD_HASH_${_k} "${_m_hash}")
                    set(OLD_VALID_${_k} TRUE)
                else()
                    set(_legacy_fn "${CMAKE_MATCH_2}")
                    string(SHA256 _k "${_legacy_fn}")
                    list(APPEND OLD_MANIFEST_KEYS "${_k}")
                    set(OLD_NAME_${_k} "${_legacy_fn}")
                    set(OLD_HASH_${_k} "")
                    set(OLD_VALID_${_k} FALSE)
                endif()
            else()
                # Legacy bare filename or invalid/corrupted hash entry
                string(REGEX REPLACE "^[a-fA-F0-9]+[ \t]+" "" _legacy_fn "${trimmed}")
                if(NOT _legacy_fn)
                    set(_legacy_fn "${trimmed}")
                endif()
                string(SHA256 _k "${_legacy_fn}")
                list(APPEND OLD_MANIFEST_KEYS "${_k}")
                set(OLD_NAME_${_k} "${_legacy_fn}")
                set(OLD_HASH_${_k} "")
                set(OLD_VALID_${_k} FALSE)
            endif()
        endif()
    endforeach()
endif()

# ---------------------------------------------------------------------------
# 5. INVENTORY: Destination assets
# ---------------------------------------------------------------------------
file(GLOB RAW_DEST_PATHS LIST_DIRECTORIES false "${SOUND_DEST_DIR}/*.wav")
set(DEST_KEYS "")

foreach(dst_path IN LISTS RAW_DEST_PATHS)
    get_filename_component(filename "${dst_path}" NAME)
    file(SHA256 "${dst_path}" _dst_hash)
    string(TOLOWER "${_dst_hash}" _dst_hash)
    string(SHA256 _k "${filename}")
    list(APPEND DEST_KEYS "${_k}")
    set(DEST_NAME_${_k} "${filename}")
    set(DEST_HASH_${_k} "${_dst_hash}")
    set(DEST_EXISTS_${_k} TRUE)
endforeach()

# ---------------------------------------------------------------------------
# 6. CLASSIFY & PLAN: Identify actions and collisions before execution
# ---------------------------------------------------------------------------
set(COLLISIONS "")
set(MUTATE_KEYS "")
set(OWNED_SOURCE_KEYS "")

foreach(k IN LISTS SOURCE_KEYS)
    set(_fn "${SRC_NAME_${k}}")
    set(_sh "${SRC_HASH_${k}}")
    if(DEST_EXISTS_${k})
        set(_dh "${DEST_HASH_${k}}")
        if("${_dh}" STREQUAL "${_sh}")
            # The destination already holds exactly the bytes the build would
            # write, so no write is needed. Ownership of the name comes from the
            # committed manifest, not from the bytes: an entry that names this
            # file means the build owns it, and a stale hash on that entry is a
            # stale content record, not a dispute about ownership. Refreshing the
            # hash is what lets an interrupted run heal without inventing any
            # authority the manifest did not already grant.
            if(DEFINED OLD_VALID_${k} AND OLD_VALID_${k})
                set(ACTION_${k} "UNCHANGED_OWNED")
                set(COMMITTED_HASH_${k} "${_dh}")
                list(APPEND OWNED_SOURCE_KEYS "${k}")
            else()
                # No committed manifest entry names this file. Bytes that merely
                # match the source prove content, never ownership, so such a file
                # is never claimed and never gains deletion authority. A hashless
                # or corrupted entry is the same case: it names the file but
                # proves nothing about it.
                set(ACTION_${k} "ADOPT_IDENTICAL")
                message(STATUS "Preserving preexisting identical WAV without claiming it: ${_fn}")
            endif()
        else()
            # Content differs: destination can only be updated if it is proven build-owned
            if(DEFINED OLD_VALID_${k} AND OLD_VALID_${k} AND "${_dh}" STREQUAL "${OLD_HASH_${k}}")
                # Previously owned and unedited by operator
                set(ACTION_${k} "UPDATE_OWNED")
                list(APPEND OWNED_SOURCE_KEYS "${k}")
                list(APPEND MUTATE_KEYS "${k}")
            else()
                # Operator modification or unowned collision
                list(APPEND COLLISIONS "${_fn}")
            endif()
        endif()
    else()
        # New asset not present in destination: will be created and owned
        set(ACTION_${k} "COPY_NEW")
        list(APPEND OWNED_SOURCE_KEYS "${k}")
        list(APPEND MUTATE_KEYS "${k}")
    endif()
endforeach()

# Stale destination removal classification.
# Deletion authority comes from the committed manifest alone: the entry must be
# present and hash-verified, and the file's current bytes must still match that
# proof. A file that no longer matches was touched from outside and is preserved.
set(TO_REMOVE_KEYS "")
foreach(k IN LISTS DEST_KEYS)
    set(_fn "${DEST_NAME_${k}}")
    set(_dh "${DEST_HASH_${k}}")
    list(FIND SOURCE_KEYS "${k}" _s_idx)
    if(_s_idx EQUAL -1)
        # File is not in current source catalog
        list(FIND OLD_MANIFEST_KEYS "${k}" _o_idx)
        if(_o_idx EQUAL -1)
            # Unowned file with no manifest provenance
            message(STATUS "Preserving unowned WAV: ${_fn}")
        else()
            # Present in old manifest: check if hash proves ownership
            if(DEFINED OLD_VALID_${k} AND OLD_VALID_${k})
                if("${_dh}" STREQUAL "${OLD_HASH_${k}}")
                    # Unmodified staged output from previous run
                    list(APPEND TO_REMOVE_KEYS "${k}")
                else()
                    # Content modified by operator
                    message(STATUS "Preserving operator-modified WAV: ${_fn}")
                endif()
            else()
                # Legacy unverified or corrupted entry: cannot prove ownership
                message(STATUS "Preserving legacy unverified WAV: ${_fn}")
            endif()
        endif()
    endif()
endforeach()

# ---------------------------------------------------------------------------
# 7. VALIDATE: Refuse collisions fail-closed before any mutations
# ---------------------------------------------------------------------------
if(COLLISIONS)
    message(FATAL_ERROR "Unowned destination file collision detected: ${COLLISIONS}")
endif()

# Every path this run may delete must be provably inside the destination now,
# not merely when it was classified. Replacement races between classification
# and removal would otherwise let a redirected path through.
foreach(k IN LISTS TO_REMOVE_KEYS)
    set(_fn "${DEST_NAME_${k}}")
    set(_target_path "${SOUND_DEST_DIR}/${_fn}")
    is_bounded_to_dest("${_target_path}" _bounded)
    if(NOT _bounded)
        message(FATAL_ERROR "Refusing removal: '${_target_path}' is not bounded to the staging destination '${DEST_CANONICAL_NORM}'")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# 8. APPLY: Execute removals and copies with byte-level verification
# ---------------------------------------------------------------------------
# Remove stale owned WAVs
foreach(k IN LISTS TO_REMOVE_KEYS)
    set(_fn "${DEST_NAME_${k}}")
    set(_target "${SOUND_DEST_DIR}/${_fn}")
    # Re-check the byte proof immediately before the destructive act.
    file(SHA256 "${_target}" _pre_rm_hash)
    string(TOLOWER "${_pre_rm_hash}" _pre_rm_hash)
    if(NOT "${_pre_rm_hash}" STREQUAL "${OLD_HASH_${k}}")
        message(FATAL_ERROR "Refusing removal of '${_fn}': destination bytes no longer match the committed manifest proof")
    endif()
    file(REMOVE "${_target}")
    if(EXISTS "${_target}")
        message(FATAL_ERROR "Failed to remove stale staged WAV: ${_fn}")
    endif()
    message(STATUS "Removed stale staged WAV: ${_fn}")
endforeach()

# Copy or update changed assets
foreach(k IN LISTS SOURCE_KEYS)
    set(_fn "${SRC_NAME_${k}}")
    set(_sh "${SRC_HASH_${k}}")
    if(ACTION_${k} STREQUAL "COPY_NEW" OR ACTION_${k} STREQUAL "UPDATE_OWNED")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E copy "${SOUND_SOURCE_DIR}/${_fn}" "${SOUND_DEST_DIR}/${_fn}"
            RESULT_VARIABLE _cp_res
        )
        if(NOT _cp_res EQUAL 0)
            message(FATAL_ERROR "Failed to copy staged WAV: ${_fn}")
        endif()
        # Byte-level verification: destination must match expected source hash
        file(SHA256 "${SOUND_DEST_DIR}/${_fn}" _actual_dst_hash)
        string(TOLOWER "${_actual_dst_hash}" _actual_dst_hash)
        if(NOT "${_actual_dst_hash}" STREQUAL "${_sh}")
            message(FATAL_ERROR "Staging verification failed for '${_fn}': actual destination hash '${_actual_dst_hash}' != expected source hash '${_sh}'")
        endif()
        set(COMMITTED_HASH_${k} "${_actual_dst_hash}")
    endif()
endforeach()

# Injected test gate: verify crash/failure between destination copy and manifest commit
if(DEFINED STAGE_SOUND_TEST_FAIL_BEFORE_COMMIT AND STAGE_SOUND_TEST_FAIL_BEFORE_COMMIT)
    message(FATAL_ERROR "STAGE_SOUND_TEST_FAIL_BEFORE_COMMIT: simulated failure between copy and commit")
endif()

# ---------------------------------------------------------------------------
# 9. COMMIT: Write new ownership manifest atomically using exclusive temp file
# ---------------------------------------------------------------------------
set(NEW_MANIFEST_CONTENT "")
foreach(k IN LISTS OWNED_SOURCE_KEYS)
    string(APPEND NEW_MANIFEST_CONTENT "${COMMITTED_HASH_${k}} ${SRC_NAME_${k}}\n")
endforeach()

# Fast-path: if manifest already exists and content is identical without mutations, skip rewrite
set(_need_commit TRUE)
if(EXISTS "${MANIFEST_FILE}" AND "${NEW_MANIFEST_CONTENT}" STREQUAL "${OLD_RAW_MANIFEST_CONTENT}" AND NOT MUTATE_KEYS AND NOT TO_REMOVE_KEYS)
    set(_need_commit FALSE)
endif()

if(_need_commit)
    # Exclusively created invocation-owned temporary manifest
    set(_commit_tmp "")
    foreach(_try_idx RANGE 1 1000)
        string(TIMESTAMP _commit_ts "%Y%m%d_%H%M%S")
        string(RANDOM LENGTH 16 ALPHABET "0123456789abcdef" _rand_hex)
        set(_candidate "${MANIFEST_FILE}.commit_${_commit_ts}_${_try_idx}_${_rand_hex}.tmp")
        if(NOT EXISTS "${_candidate}")
            set(_commit_tmp "${_candidate}")
            break()
        endif()
    endforeach()

    if(NOT _commit_tmp)
        message(FATAL_ERROR "Could not allocate unique temporary manifest file path")
    endif()

    file(WRITE "${_commit_tmp}" "${NEW_MANIFEST_CONTENT}")
    if(NOT EXISTS "${_commit_tmp}")
        message(FATAL_ERROR "Failed to write staging manifest temporary file: ${_commit_tmp}")
    endif()

    file(READ "${_commit_tmp}" _written_content)
    if(NOT "${_written_content}" STREQUAL "${NEW_MANIFEST_CONTENT}")
        file(REMOVE "${_commit_tmp}")
        message(FATAL_ERROR "Temporary manifest file content verification failed: ${_commit_tmp}")
    endif()

    file(RENAME "${_commit_tmp}" "${MANIFEST_FILE}")
    if(NOT EXISTS "${MANIFEST_FILE}")
        message(FATAL_ERROR "Failed to commit staging manifest")
    endif()

    file(READ "${MANIFEST_FILE}" _final_manifest_content)
    if(NOT "${_final_manifest_content}" STREQUAL "${NEW_MANIFEST_CONTENT}")
        message(FATAL_ERROR "Staging manifest verification failed: content mismatch in '${MANIFEST_FILE}'")
    endif()
endif()

file(LOCK "${STAGE_LOCK_FILE}" RELEASE)

# ---------------------------------------------------------------------------
# 10. Accurate destination count computed after all mutations
# ---------------------------------------------------------------------------
file(GLOB FINAL_DEST_WAVS LIST_DIRECTORIES false "${SOUND_DEST_DIR}/*.wav")
list(LENGTH SOURCE_KEYS SOURCE_COUNT)
list(LENGTH FINAL_DEST_WAVS DEST_COUNT)
message(STATUS "Sound catalog staged: ${SOURCE_COUNT} source WAVs, ${DEST_COUNT} destination WAVs")
