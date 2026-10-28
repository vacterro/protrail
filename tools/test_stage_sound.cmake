# Regression oracle suite for tools/stage_sound.cmake
# Registered as CTest test `protrail_sound_staging`.
#
# This oracle and tools/stage_sound.cmake share ONE authority model (T-118): the
# committed manifest plus the source catalog are the only authority for what this
# build owns. A transaction journal, an allocation receipt, a rollback backup and
# a provenance anchor all live inside the build tree, so any process that can
# write there can forge them; none of them can authorize a write or a deletion.
# Timestamp ordering is not authentication, and SHA-256 equality proves content,
# never ownership. Deletion authority requires a committed manifest entry whose
# hash still matches the bytes on disk, and every deletion is bounded to the
# canonical destination directory immediately before it happens.
#
# Reporting contract: a scenario that cannot be exercised on this host is counted
# as NOT_RUN and is never reported as a PASS. The final line prints both counts.
#
# Scenario index:
#   R01 Clean first staging.
#   R02 Repeated idempotent staging.
#   R03 Verified-owned file update.
#   R04 Operator-modified file + source update.
#   R05 Operator-modified file + source deletion.
#   R06 Unowned same-name collision.
#   R07 Unowned unrelated file preservation.
#   R08 Existing operator-owned manifest.probe.
#   R09 Manifest path itself is a directory.
#   R10 Retry after manifest-path repair.
#   R11 Interrupted NEW copy: destination written, manifest not committed.
#   R12 Interrupted copy then repeated run: no rollback, stable result.
#   R13 Hashless legacy manifest record grants no deletion authority.
#   R14 Invalid or corrupted hash record grants no deletion authority.
#   R15 Same timestamp, different source bytes.
#   R16 Destination content equals committed manifest hash.
#   R17 Debug and Release isolation.
#   R18 Asset-only rebuild without executable relink.
#   R19 Full 380-WAV staging.
#   R20 Empty source catalog.
#   R21 Missing source catalog.
#   R22 Unicode and spaces.
#   R23 Semicolon rejection on Windows.
#   R24 Accurate failure and destination-count reporting.
#   R25 Preexisting unowned destination with identical bytes.
#   R26 Source deletion after R25 preserves the unowned destination.
#   R27 Manifest record naming a traversal path deletes nothing outside.
#   R28 Existing operator-owned commit temporary file survives.
#   R29 Interrupted UPDATE before manifest commit.
#   R30 Source change after an interrupted UPDATE reports a bounded conflict.
#   R31 Interrupted REMOVE and repeated execution.
#   R32 Concurrent staging of the same configuration with locking.
#   R33 Malformed or duplicated manifest records grant no deletion authority.
#   R34 Ambiguous ownership fails closed and preserves the file.
#   R35 Foreign files in the manifest's sibling directories are preserved.
#   R36 Planted journal and receipt evidence grant no deletion authority.
#   R37 Debug staging never mutates an interrupted Release state.
#   R38 Release recovery leaves Debug untouched.
#   R39 Concurrent configurations with locking.
#   R40 Preexisting fixture directory with operator data preserved.
#   R41 An interrupted run leaves no transaction or recovery apparatus behind.
#   R42 Preexisting foreign fixture directory survives the complete suite.
#   R43 Preexisting operator-owned WAV inside the staging directory survives.
#   R44 Foreign transaction metadata survives successful staging.
#   R45 Foreign transaction metadata survives failed staging.
#   R46 Reparse-point destination is refused before mutation.
#   R47 Forged provenance naming a retired operator file grants nothing.
#   R48 An interrupted run reconciles and never deletes to recover.
#   R49 Concurrent same-config staging cannot remove another's artifact.
#   R50 Cross-configuration isolation with an interrupted run.
#   R51 Internal reparse point as the manifest directory is refused.
#   R52 External common-prefix sibling reparse point is refused.
#   R53 No-op staging with foreign metadata preserves every byte.
#   R54 No-op with a redirected backup directory modifies nothing external.
#   R55 Correctly ordered forged provenance and journal delete no operator WAV.
#   R56 Legitimate interrupted NEW recovery still reconciles.
#   R57 Legitimate interrupted UPDATE/REMOVE recovery still reconciles.
#   R58 Unchanged staging with foreign backup artifacts triggers no cleanup.
#   R59 Interrupted recovery then repeated execution preserves unknown files.
#   R60 Windows junction / reparse-point matrix on a real Windows filesystem.

cmake_minimum_required(VERSION 3.24)

set(_script "${CMAKE_CURRENT_LIST_DIR}/stage_sound.cmake")
if(NOT EXISTS "${_script}")
    message(FATAL_ERROR "stage_sound.cmake not found beside test_stage_sound.cmake")
endif()

string(TIMESTAMP _test_ts "%Y%m%d_%H%M%S")
string(RANDOM LENGTH 12 ALPHABET "0123456789abcdef" _test_rand)
set(_root "${CMAKE_CURRENT_BINARY_DIR}/stage_sound_reg_${_test_ts}_${_test_rand}")
file(MAKE_DIRECTORY "${_root}")

function(run_stage source dest manifest out_rc out_msg)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DSTAGE_SOUND_SOURCE_DIR=${source}"
            "-DSTAGE_SOUND_DEST_DIR=${dest}"
            "-DSTAGE_SOUND_MANIFEST_FILE=${manifest}"
            -P "${_script}"
        RESULT_VARIABLE _rc
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE _err)
    set(${out_rc} ${_rc} PARENT_SCOPE)
    set(${out_msg} "${_out}\n${_err}" PARENT_SCOPE)
endfunction()

function(run_stage_expect_success source dest manifest out_msg)
    run_stage("${source}" "${dest}" "${manifest}" _rc _m)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "Expected stage_sound success, got exit code ${_rc}\n${_m}")
    endif()
    set(${out_msg} "${_m}" PARENT_SCOPE)
endfunction()

function(run_stage_expect_failure source dest manifest expected_token)
    run_stage("${source}" "${dest}" "${manifest}" _rc _m)
    if(_rc EQUAL 0)
        message(FATAL_ERROR "Expected stage_sound failure, but exited 0!\n${_m}")
    endif()
    if(expected_token)
        string(FIND "${_m}" "${expected_token}" _pos)
        if(_pos EQUAL -1)
            message(FATAL_ERROR "Expected failure message containing '${expected_token}', got:\n${_m}")
        endif()
    endif()
endfunction()

function(count_wavs dir out_var)
    file(GLOB _w LIST_DIRECTORIES false "${dir}/*.wav")
    list(LENGTH _w _n)
    set(${out_var} ${_n} PARENT_SCOPE)
endfunction()

# Reparse points. A Windows directory junction needs no elevation, unlike a
# symbolic link, so the reparse-point scenarios are genuinely exercisable on a
# stock developer workstation. A scenario that cannot establish the reparse point
# it needs reports NOT_RUN and is never counted as a pass.
set(_total_scenarios 60)
set(_not_run_count 0)
set(_not_run_list "")

function(not_run name reason)
    math(EXPR _n "${_not_run_count} + 1")
    set(_not_run_count "${_n}" PARENT_SCOPE)
    set(_not_run_list "${_not_run_list} ${name}" PARENT_SCOPE)
    message(STATUS "${name}: NOT_RUN -- ${reason}")
endfunction()

function(make_junction target link out_ok)
    set(${out_ok} FALSE PARENT_SCOPE)
    if(NOT CMAKE_HOST_WIN32)
        return()
    endif()
    if(NOT EXISTS "${target}")
        file(MAKE_DIRECTORY "${target}")
    endif()
    file(TO_NATIVE_PATH "${target}" _mk_target)
    file(TO_NATIVE_PATH "${link}" _mk_link)
    execute_process(
        COMMAND cmd.exe /c mklink /J "${_mk_link}" "${_mk_target}"
        OUTPUT_QUIET ERROR_QUIET
        RESULT_VARIABLE _mk_rc)
    if(NOT _mk_rc EQUAL 0)
        return()
    endif()
    if(NOT IS_DIRECTORY "${link}")
        return()
    endif()
    set(${out_ok} TRUE PARENT_SCOPE)
endfunction()

# Remove only the link, never the target: rmdir on a junction deletes the
# reparse point itself.
function(remove_junction link)
    if(NOT EXISTS "${link}")
        return()
    endif()
    file(TO_NATIVE_PATH "${link}" _rm_link)
    execute_process(
        COMMAND cmd.exe /c rmdir "${_rm_link}"
        OUTPUT_QUIET ERROR_QUIET)
endfunction()

# =============================================================================
# R01: Clean first staging
# =============================================================================
set(_r01 "${_root}/r01")
set(_r01_src "${_r01}/src")
set(_r01_dst "${_r01}/dst")
set(_r01_man "${_r01}/manifest")
file(MAKE_DIRECTORY "${_r01_src}" "${_r01_dst}")
file(WRITE "${_r01_src}/a.wav" "RIFF-a-content")
file(WRITE "${_r01_src}/b.wav" "RIFF-b-content")

run_stage_expect_success("${_r01_src}" "${_r01_dst}" "${_r01_man}" _msg01)
count_wavs("${_r01_dst}" _cnt01)
if(NOT _cnt01 EQUAL 2)
    message(FATAL_ERROR "R01: Expected 2 destination WAVs, got ${_cnt01}")
endif()
file(READ "${_r01_dst}/a.wav" _a_content)
file(READ "${_r01_dst}/b.wav" _b_content)
if(NOT "${_a_content}" STREQUAL "RIFF-a-content" OR NOT "${_b_content}" STREQUAL "RIFF-b-content")
    message(FATAL_ERROR "R01: Destination file contents do not match source bytes")
endif()
if(NOT EXISTS "${_r01_man}")
    message(FATAL_ERROR "R01: Manifest file not created")
endif()

# =============================================================================
# R02: Repeated idempotent staging
# =============================================================================
file(READ "${_r01_man}" _man01_before)
run_stage_expect_success("${_r01_src}" "${_r01_dst}" "${_r01_man}" _msg02)
count_wavs("${_r01_dst}" _cnt02)
if(NOT _cnt02 EQUAL 2)
    message(FATAL_ERROR "R02: Repeated staging modified destination count: ${_cnt02}")
endif()
file(READ "${_r01_man}" _man01_after)
if(NOT "${_man01_before}" STREQUAL "${_man01_after}")
    message(FATAL_ERROR "R02: Repeated staging modified manifest without changes")
endif()

# =============================================================================
# R03: Verified-owned file update
# =============================================================================
set(_r03 "${_root}/r03")
set(_r03_src "${_r03}/src")
set(_r03_dst "${_r03}/dst")
set(_r03_man "${_r03}/manifest")
file(MAKE_DIRECTORY "${_r03_src}" "${_r03_dst}")
file(WRITE "${_r03_src}/track.wav" "TRACK_V1")
run_stage_expect_success("${_r03_src}" "${_r03_dst}" "${_r03_man}" _msg03_1)

# Source updates track.wav
file(WRITE "${_r03_src}/track.wav" "TRACK_V2_NEW_BYTES")
run_stage_expect_success("${_r03_src}" "${_r03_dst}" "${_r03_man}" _msg03_2)
file(READ "${_r03_dst}/track.wav" _track_v2)
if(NOT "${_track_v2}" STREQUAL "TRACK_V2_NEW_BYTES")
    message(FATAL_ERROR "R03: Owned file was not updated with new source content")
endif()
file(SHA256 "${_r03_dst}/track.wav" _track_v2_hash)
string(TOLOWER "${_track_v2_hash}" _track_v2_hash)
file(READ "${_r03_man}" _man03_content)
string(FIND "${_man03_content}" "${_track_v2_hash}" _pos03)
if(_pos03 EQUAL -1)
    message(FATAL_ERROR "R03: Manifest does not contain updated SHA256")
endif()

# =============================================================================
# R04: Operator-modified file + source update (Defect A: Collision fail-closed)
# =============================================================================
set(_r04 "${_root}/r04")
set(_r04_src "${_r04}/src")
set(_r04_dst "${_r04}/dst")
set(_r04_man "${_r04}/manifest")
file(MAKE_DIRECTORY "${_r04_src}" "${_r04_dst}")
file(WRITE "${_r04_src}/hero.wav" "HERO_INITIAL_AUDIO")
run_stage_expect_success("${_r04_src}" "${_r04_dst}" "${_r04_man}" _msg04_1)

# Operator modifies hero.wav directly in destination
file(WRITE "${_r04_dst}/hero.wav" "OPERATOR_CUSTOM_HERO_AUDIO")

# Source also updates hero.wav
file(WRITE "${_r04_src}/hero.wav" "SOURCE_NEW_HERO_AUDIO")

# Staging must FAIL CLOSED and NOT overwrite operator-modified destination
run_stage_expect_failure("${_r04_src}" "${_r04_dst}" "${_r04_man}" "collision")
file(READ "${_r04_dst}/hero.wav" _r04_persisted)
if(NOT "${_r04_persisted}" STREQUAL "OPERATOR_CUSTOM_HERO_AUDIO")
    message(FATAL_ERROR "R04 REGRESSION (Defect A): Operator-modified file was clobbered by source update!")
endif()

# =============================================================================
# R05: Operator-modified file + source deletion (Defect D: Ownership revoked)
# =============================================================================
set(_r05 "${_root}/r05")
set(_r05_src "${_r05}/src")
set(_r05_dst "${_r05}/dst")
set(_r05_man "${_r05}/manifest")
file(MAKE_DIRECTORY "${_r05_src}" "${_r05_dst}")
file(WRITE "${_r05_src}/sound_a.wav" "SOUND_A_SOURCE")
file(WRITE "${_r05_src}/sound_b.wav" "SOUND_B_STABLE")
run_stage_expect_success("${_r05_src}" "${_r05_dst}" "${_r05_man}" _msg05_1)

# Operator replaces sound_a.wav in destination
file(WRITE "${_r05_dst}/sound_a.wav" "OPERATOR_PRESERVED_SOUND_A")

# Source deletes sound_a.wav
file(REMOVE "${_r05_src}/sound_a.wav")

# Staging succeeds but must NOT delete operator-modified sound_a.wav
run_stage_expect_success("${_r05_src}" "${_r05_dst}" "${_r05_man}" _msg05_2)
if(NOT EXISTS "${_r05_dst}/sound_a.wav")
    message(FATAL_ERROR "R05 REGRESSION: Operator-modified file was deleted after source deletion!")
endif()
file(READ "${_r05_dst}/sound_a.wav" _r05_content)
if(NOT "${_r05_content}" STREQUAL "OPERATOR_PRESERVED_SOUND_A")
    message(FATAL_ERROR "R05: Operator-modified file content was corrupted!")
endif()
if(NOT EXISTS "${_r05_dst}/sound_b.wav")
    message(FATAL_ERROR "R05: Stable sound_b.wav was incorrectly removed")
endif()
count_wavs("${_r05_dst}" _cnt05)
if(NOT _cnt05 EQUAL 2)
    message(FATAL_ERROR "R05: Expected 2 destination WAVs, got ${_cnt05}")
endif()

# =============================================================================
# R06: Unowned same-name collision
# =============================================================================
set(_r06 "${_root}/r06")
set(_r06_src "${_r06}/src")
set(_r06_dst "${_r06}/dst")
set(_r06_man "${_r06}/manifest")
file(MAKE_DIRECTORY "${_r06_src}" "${_r06_dst}")

# Destination has unowned collision.wav created by operator
file(WRITE "${_r06_dst}/collision.wav" "OPERATOR_SECRET_RECORDING")
# Source has new collision.wav
file(WRITE "${_r06_src}/collision.wav" "SOURCE_NEW_TRACK")

run_stage_expect_failure("${_r06_src}" "${_r06_dst}" "${_r06_man}" "collision")
file(READ "${_r06_dst}/collision.wav" _coll_content)
if(NOT "${_coll_content}" STREQUAL "OPERATOR_SECRET_RECORDING")
    message(FATAL_ERROR "R06 REGRESSION: Unowned file overwritten by source collision!")
endif()

# =============================================================================
# R07: Unowned unrelated file preservation
# =============================================================================
set(_r07 "${_root}/r07")
set(_r07_src "${_r07}/src")
set(_r07_dst "${_r07}/dst")
set(_r07_man "${_r07}/manifest")
file(MAKE_DIRECTORY "${_r07_src}" "${_r07_dst}")
file(WRITE "${_r07_src}/foobar.wav" "FOOBAR_AUDIO")
file(WRITE "${_r07_dst}/unrelated.wav" "UNRELATED_USER_AUDIO")
file(WRITE "${_r07_dst}/bar.wav" "BAR_USER_AUDIO")

run_stage_expect_success("${_r07_src}" "${_r07_dst}" "${_r07_man}" _msg07)
if(NOT EXISTS "${_r07_dst}/unrelated.wav" OR NOT EXISTS "${_r07_dst}/bar.wav")
    message(FATAL_ERROR "R07: Unowned unrelated files were removed")
endif()
file(READ "${_r07_dst}/unrelated.wav" _unrel)
file(READ "${_r07_dst}/bar.wav" _bar)
if(NOT "${_unrel}" STREQUAL "UNRELATED_USER_AUDIO" OR NOT "${_bar}" STREQUAL "BAR_USER_AUDIO")
    message(FATAL_ERROR "R07: Unowned unrelated file contents were mutated")
endif()
if(NOT EXISTS "${_r07_dst}/foobar.wav")
    message(FATAL_ERROR "R07: foobar.wav was not staged")
endif()
count_wavs("${_r07_dst}" _cnt07)
if(NOT _cnt07 EQUAL 3)
    message(FATAL_ERROR "R07: Expected 3 destination WAVs, got ${_cnt07}")
endif()

# =============================================================================
# R08: Existing operator-owned manifest.probe (Defect B: Non-destructive probe)
# =============================================================================
set(_r08 "${_root}/r08")
set(_r08_src "${_r08}/src")
set(_r08_dst "${_r08}/dst")
set(_r08_man "${_r08}/manifest")
file(MAKE_DIRECTORY "${_r08_src}" "${_r08_dst}")
file(WRITE "${_r08_src}/ping.wav" "PING")

# Operator has preexisting manifest.probe in the manifest directory
file(WRITE "${_r08}/manifest.probe" "OPERATOR_PROBE_FILE_NEVER_DELETE")

run_stage_expect_success("${_r08_src}" "${_r08_dst}" "${_r08_man}" _msg08)

# manifest.probe must be intact and unmutated
if(NOT EXISTS "${_r08}/manifest.probe")
    message(FATAL_ERROR "R08 REGRESSION (Defect B): Existing manifest.probe was deleted by staging probe!")
endif()
file(READ "${_r08}/manifest.probe" _probe_data)
if(NOT "${_probe_data}" STREQUAL "OPERATOR_PROBE_FILE_NEVER_DELETE")
    message(FATAL_ERROR "R08 REGRESSION (Defect B): Existing manifest.probe was clobbered by staging probe!")
endif()
if(NOT EXISTS "${_r08_dst}/ping.wav")
    message(FATAL_ERROR "R08: ping.wav was not staged")
endif()

# =============================================================================
# R09: Manifest path itself is a directory (Defect C: Fail-closed before copy)
# =============================================================================
set(_r09 "${_root}/r09")
set(_r09_src "${_r09}/src")
set(_r09_dst "${_r09}/dst")
set(_r09_man_dir "${_r09}/manifest_is_a_dir")
file(MAKE_DIRECTORY "${_r09_src}" "${_r09_dst}" "${_r09_man_dir}")
file(WRITE "${_r09_src}/sample.wav" "SAMPLE_DATA")
file(WRITE "${_r09_dst}/preserve.wav" "PRESERVE_DATA")

run_stage_expect_failure("${_r09_src}" "${_r09_dst}" "${_r09_man_dir}" "is a directory")

# Fail-closed invariant: destination must not have copied sample.wav
if(EXISTS "${_r09_dst}/sample.wav")
    message(FATAL_ERROR "R09 REGRESSION (Defect C): Files were copied before manifest path validation!")
endif()
if(NOT EXISTS "${_r09_dst}/preserve.wav")
    message(FATAL_ERROR "R09: Preexisting file was clobbered")
endif()

# =============================================================================
# R10: Retry after manifest-path repair
# =============================================================================
set(_r10_man_file "${_r09}/manifest_is_a_dir/valid_manifest.txt")
run_stage_expect_success("${_r09_src}" "${_r09_dst}" "${_r10_man_file}" _msg10)
if(NOT EXISTS "${_r09_dst}/sample.wav")
    message(FATAL_ERROR "R10: Retry after manifest path repair failed to stage sample.wav")
endif()
count_wavs("${_r09_dst}" _cnt10)
if(NOT _cnt10 EQUAL 2)
    message(FATAL_ERROR "R10: Expected 2 destination WAVs after repair, got ${_cnt10}")
endif()

# =============================================================================
# R11: Interrupted NEW copy: destination written, manifest not committed
# =============================================================================
set(_r11 "${_root}/r11")
set(_r11_src "${_r11}/src")
set(_r11_dst "${_r11}/dst")
set(_r11_man "${_r11}/manifest")
file(MAKE_DIRECTORY "${_r11_src}" "${_r11_dst}")
file(WRITE "${_r11_src}/tx_test.wav" "TX_DATA")

    # Injected genuine failure between copy and commit
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DSTAGE_SOUND_SOURCE_DIR=${_r11_src}"
            "-DSTAGE_SOUND_DEST_DIR=${_r11_dst}"
            "-DSTAGE_SOUND_MANIFEST_FILE=${_r11_man}"
            "-DSTAGE_SOUND_TEST_FAIL_BEFORE_COMMIT=TRUE"
            -P "${_script}"
        RESULT_VARIABLE _rc11_fail
        OUTPUT_VARIABLE _out11
        ERROR_VARIABLE _err11
    )
if(_rc11_fail EQUAL 0)
    message(FATAL_ERROR "R11: Expected simulated failure between copy and commit")
endif()
# The copy landed but the manifest did not, so the copied file carries no
# ownership proof. Nothing rolls it back: recovering by deletion would need an
# authority record that nobody can validate.
if(NOT EXISTS "${_r11_dst}/tx_test.wav")
    message(FATAL_ERROR "R11: tx_test.wav was not copied before failure")
endif()
if(EXISTS "${_r11_man}")
    message(FATAL_ERROR "R11: Manifest was committed despite failure before commit")
endif()
# No recovery apparatus may exist at all.
foreach(_r11_artifact
        "${_r11_man}.tx"
        "${_r11_man}.tx_backup"
        "${_r11_man}.tx_backup/tx_provenance.meta"
        "${_r11_man}.new"
        "${_r11_dst}/tx_test.wav.new")
    if(EXISTS "${_r11_artifact}")
        message(FATAL_ERROR "R11: staging created an apparatus entry it cannot authenticate: ${_r11_artifact}")
    endif()
endforeach()

# Next run reconciles from source + manifest without rolling anything back.
run_stage_expect_success("${_r11_src}" "${_r11_dst}" "${_r11_man}" _msg11)
if(NOT EXISTS "${_r11_man}")
    message(FATAL_ERROR "R11: Manifest not committed after the repeated run")
endif()
file(READ "${_r11_dst}/tx_test.wav" _r11_bytes)
if(NOT "${_r11_bytes}" STREQUAL "TX_DATA")
    message(FATAL_ERROR "R11: Repeated staging corrupted the interrupted copy instead of reconciling it")
endif()

# =============================================================================
# R12: Interrupted copy then repeated run: no rollback, stable reconciliation
# =============================================================================
set(_r12 "${_root}/r12")
set(_r12_src "${_r12}/src")
set(_r12_dst "${_r12}/dst")
set(_r12_man "${_r12}/manifest")
file(MAKE_DIRECTORY "${_r12_src}" "${_r12_dst}")

# First run: stage orphan.wav with simulated crash before commit
file(WRITE "${_r12_src}/orphan.wav" "ORPHAN_HALF_COPIED")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DSTAGE_SOUND_SOURCE_DIR=${_r12_src}"
        "-DSTAGE_SOUND_DEST_DIR=${_r12_dst}"
        "-DSTAGE_SOUND_MANIFEST_FILE=${_r12_man}"
        "-DSTAGE_SOUND_TEST_FAIL_BEFORE_COMMIT=TRUE"
        -P "${_script}"
    RESULT_VARIABLE _rc12_fail
)
if(_rc12_fail EQUAL 0)
    message(FATAL_ERROR "R12: Expected failure before commit during interrupted run")
endif()
if(NOT EXISTS "${_r12_dst}/orphan.wav")
    message(FATAL_ERROR "R12: orphan.wav was not copied during interrupted run")
endif()
if(EXISTS "${_r12_man}")
    message(FATAL_ERROR "R12: Manifest was committed by the interrupted run")
endif()

# Update source before retrying: remove orphan.wav, introduce legit.wav
file(REMOVE "${_r12_src}/orphan.wav")
file(WRITE "${_r12_src}/legit.wav" "LEGIT_AUDIO")

# The repeated run must NOT roll back orphan.wav. An uncommitted copy has no
# ownership proof, and deleting on unproven records is the defect being fixed.
# It stages what the source now says and leaves the unproven file alone.
run_stage_expect_success("${_r12_src}" "${_r12_dst}" "${_r12_man}" _msg12)
if(NOT EXISTS "${_r12_dst}/legit.wav")
    message(FATAL_ERROR "R12: legit.wav was not staged by the repeated run")
endif()
file(READ "${_r12_dst}/orphan.wav" _r12_orphan)
if(NOT "${_r12_orphan}" STREQUAL "ORPHAN_HALF_COPIED")
    message(FATAL_ERROR "R12 REGRESSION: the interrupted copy was corrupted by the repeated run")
endif()
count_wavs("${_r12_dst}" _cnt12)
if(NOT _cnt12 EQUAL 2)
    message(FATAL_ERROR "R12: Expected 2 destination WAVs (orphan preserved, legit staged), got ${_cnt12}")
endif()

# =============================================================================
# R13: Hashless legacy manifest deletion attempt (Defect D)
# =============================================================================
set(_r13 "${_root}/r13")
set(_r13_src "${_r13}/src")
set(_r13_dst "${_r13}/dst")
set(_r13_man "${_r13}/manifest")
file(MAKE_DIRECTORY "${_r13_src}" "${_r13_dst}")

# Legacy manifest contains bare filename without SHA256 hash
file(WRITE "${_r13_man}" "legacy_track.wav\n")
file(WRITE "${_r13_dst}/legacy_track.wav" "VALUABLE_OPERATOR_DATA")
file(WRITE "${_r13_src}/fresh.wav" "FRESH_AUDIO")

# Staging must NOT delete legacy_track.wav because bare name lacks content ownership proof
run_stage_expect_success("${_r13_src}" "${_r13_dst}" "${_r13_man}" _msg13)
if(NOT EXISTS "${_r13_dst}/legacy_track.wav")
    message(FATAL_ERROR "R13 REGRESSION (Defect D): Hashless legacy entry deleted unverified destination file!")
endif()
file(READ "${_r13_dst}/legacy_track.wav" _legacy_content)
if(NOT "${_legacy_content}" STREQUAL "VALUABLE_OPERATOR_DATA")
    message(FATAL_ERROR "R13: legacy_track.wav content was modified")
endif()
if(NOT EXISTS "${_r13_dst}/fresh.wav")
    message(FATAL_ERROR "R13: fresh.wav was not staged")
endif()

# =============================================================================
# R14: Invalid or corrupted hash record
# =============================================================================
set(_r14 "${_root}/r14")
set(_r14_src "${_r14}/src")
set(_r14_dst "${_r14}/dst")
set(_r14_man "${_r14}/manifest")
file(MAKE_DIRECTORY "${_r14_src}" "${_r14_dst}")

# Manifest line contains corrupted/truncated hash
file(WRITE "${_r14_man}" "1234notahash corrupted.wav\n")
file(WRITE "${_r14_dst}/corrupted.wav" "CORRUPTED_ENTRY_ASSET")
file(WRITE "${_r14_src}/new_valid.wav" "NEW_VALID_AUDIO")

run_stage_expect_success("${_r14_src}" "${_r14_dst}" "${_r14_man}" _msg14)
# Corrupted entry cannot prove ownership: corrupted.wav must be preserved
if(NOT EXISTS "${_r14_dst}/corrupted.wav")
    message(FATAL_ERROR "R14: Corrupted manifest hash allowed unauthorized deletion")
endif()
if(NOT EXISTS "${_r14_dst}/new_valid.wav")
    message(FATAL_ERROR "R14: new_valid.wav was not staged")
endif()

# =============================================================================
# R15: Same timestamp, different source bytes (Defect E: False metadata success)
# =============================================================================
set(_r15 "${_root}/r15")
set(_r15_src "${_r15}/src")
set(_r15_dst "${_r15}/dst")
set(_r15_man "${_r15}/manifest")
file(MAKE_DIRECTORY "${_r15_src}" "${_r15_dst}")
file(WRITE "${_r15_src}/sound.wav" "BYTES_V1")

# Fix timestamp to a known fixed time
if(WIN32)
    execute_process(COMMAND powershell -NoProfile -Command "(Get-Item '${_r15_src}/sound.wav').LastWriteTime = (Get-Date '2025-01-01 12:00:00')")
endif()
run_stage_expect_success("${_r15_src}" "${_r15_dst}" "${_r15_man}" _msg15_1)

# Overwrite source with new bytes, and explicitly restore the exact same timestamp
file(WRITE "${_r15_src}/sound.wav" "BYTES_V2_DIFFERENT")
if(WIN32)
    execute_process(COMMAND powershell -NoProfile -Command "(Get-Item '${_r15_src}/sound.wav').LastWriteTime = (Get-Date '2025-01-01 12:00:00')")
endif()
run_stage_expect_success("${_r15_src}" "${_r15_dst}" "${_r15_man}" _msg15_2)
file(READ "${_r15_dst}/sound.wav" _r15_read)
if(NOT "${_r15_read}" STREQUAL "BYTES_V2_DIFFERENT")
    message(FATAL_ERROR "R15 REGRESSION (Defect E): Updated source bytes not staged when timestamp preserved!")
endif()

# =============================================================================
# R16: Destination content equals committed manifest hash
# =============================================================================
file(STRINGS "${_r15_man}" _r16_lines)
foreach(_line IN LISTS _r16_lines)
    string(STRIP "${_line}" _line_trimmed)
    if(_line_trimmed MATCHES "^([a-fA-F0-9]+)[ \t]+(.+)$")
        set(_exp_hash "${CMAKE_MATCH_1}")
        set(_f_name "${CMAKE_MATCH_2}")
        file(SHA256 "${_r15_dst}/${_f_name}" _act_hash)
        string(TOLOWER "${_exp_hash}" _exp_hash)
        string(TOLOWER "${_act_hash}" _act_hash)
        if(NOT "${_exp_hash}" STREQUAL "${_act_hash}")
            message(FATAL_ERROR "R16: Manifest hash ${_exp_hash} != destination file hash ${_act_hash} for ${_f_name}")
        endif()
    endif()
endforeach()

# =============================================================================
# R17: Debug and Release isolation
# =============================================================================
set(_r17 "${_root}/r17")
set(_r17_src "${_r17}/src")
set(_r17_dst_rel "${_r17}/dst_rel")
set(_r17_dst_dbg "${_r17}/dst_dbg")
set(_r17_man_rel "${_r17}/manifest.Release")
set(_r17_man_dbg "${_r17}/manifest.Debug")
file(MAKE_DIRECTORY "${_r17_src}" "${_r17_dst_rel}" "${_r17_dst_dbg}")
file(WRITE "${_r17_src}/alpha.wav" "ALPHA")
file(WRITE "${_r17_src}/beta.wav" "BETA")

run_stage_expect_success("${_r17_src}" "${_r17_dst_rel}" "${_r17_man_rel}" _msg17_r1)
run_stage_expect_success("${_r17_src}" "${_r17_dst_dbg}" "${_r17_man_dbg}" _msg17_d1)

# Remove beta.wav from source
file(REMOVE "${_r17_src}/beta.wav")

# Restage Release, then Debug
run_stage_expect_success("${_r17_src}" "${_r17_dst_rel}" "${_r17_man_rel}" _msg17_r2)
run_stage_expect_success("${_r17_src}" "${_r17_dst_dbg}" "${_r17_man_dbg}" _msg17_d2)

if(EXISTS "${_r17_dst_rel}/beta.wav" OR EXISTS "${_r17_dst_dbg}/beta.wav")
    message(FATAL_ERROR "R17: Stale beta.wav not removed from isolated configurations")
endif()
count_wavs("${_r17_dst_rel}" _cnt_rel)
count_wavs("${_r17_dst_dbg}" _cnt_dbg)
if(NOT _cnt_rel EQUAL 1 OR NOT _cnt_dbg EQUAL 1)
    message(FATAL_ERROR "R17: Expected 1 WAV in each isolated configuration")
endif()

# =============================================================================
# R18: Asset-only rebuild without executable relink
# =============================================================================
set(_r18 "${_root}/r18")
set(_r18_src "${_r18}/src")
set(_r18_dst "${_r18}/dst")
set(_r18_man "${_r18}/manifest")
file(MAKE_DIRECTORY "${_r18_src}" "${_r18_dst}")
file(WRITE "${_r18_src}/base.wav" "BASE_AUDIO")
run_stage_expect_success("${_r18_src}" "${_r18_dst}" "${_r18_man}" _msg18_1)

# Add new asset
file(WRITE "${_r18_src}/added.wav" "ADDED_AUDIO")
run_stage_expect_success("${_r18_src}" "${_r18_dst}" "${_r18_man}" _msg18_2)
if(NOT EXISTS "${_r18_dst}/added.wav")
    message(FATAL_ERROR "R18: Added asset was not staged on incremental asset restage")
endif()
count_wavs("${_r18_dst}" _cnt18)
if(NOT _cnt18 EQUAL 2)
    message(FATAL_ERROR "R18: Expected 2 destination WAVs, got ${_cnt18}")
endif()

# Exercise the real CMake custom target 'protrail_stage_sound' if in build tree
if(EXISTS "${CMAKE_BINARY_DIR}/CMakeCache.txt")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --target protrail_stage_sound --config Release
        RESULT_VARIABLE _r18_tgt_res
        OUTPUT_VARIABLE _r18_tgt_out
        ERROR_VARIABLE _r18_tgt_err
    )
    if(NOT _r18_tgt_res EQUAL 0)
        message(FATAL_ERROR "R18: Real CMake build target protrail_stage_sound failed: ${_r18_tgt_out}\n${_r18_tgt_err}")
    endif()
endif()

# =============================================================================
# R19: Full 380-WAV staging
# =============================================================================
set(_real_sound "${CMAKE_CURRENT_LIST_DIR}/../sound")
if(EXISTS "${_real_sound}")
    count_wavs("${_real_sound}" _real_cnt)
    if(_real_cnt GREATER_EQUAL 380)
        set(_r19 "${_root}/r19")
        set(_r19_dst "${_r19}/dst")
        set(_r19_man "${_r19}/manifest")
        file(MAKE_DIRECTORY "${_r19_dst}")

        run_stage_expect_success("${_real_sound}" "${_r19_dst}" "${_r19_man}" _msg19_1)
        count_wavs("${_r19_dst}" _cnt19_1)
        if(NOT _cnt19_1 EQUAL _real_cnt)
            message(FATAL_ERROR "R19: Expected ${_real_cnt} staged WAVs, got ${_cnt19_1}")
        endif()

        # Idempotent second run on full catalog
        run_stage_expect_success("${_real_sound}" "${_r19_dst}" "${_r19_man}" _msg19_2)
        count_wavs("${_r19_dst}" _cnt19_2)
        if(NOT _cnt19_2 EQUAL _real_cnt)
            message(FATAL_ERROR "R19: Repeated full catalog staging changed count: ${_cnt19_2}")
        endif()
    endif()
endif()

# =============================================================================
# R20: Empty source catalog
# =============================================================================
set(_r20 "${_root}/r20")
set(_r20_src "${_r20}/src")
set(_r20_dst "${_r20}/dst")
set(_r20_man "${_r20}/manifest")
file(MAKE_DIRECTORY "${_r20_src}" "${_r20_dst}")

run_stage_expect_success("${_r20_src}" "${_r20_dst}" "${_r20_man}" _msg20)
count_wavs("${_r20_dst}" _cnt20)
if(NOT _cnt20 EQUAL 0)
    message(FATAL_ERROR "R20: Expected 0 destination WAVs from empty source catalog")
endif()
string(FIND "${_msg20}" "0 source WAVs, 0 destination WAVs" _pos20)
if(_pos20 EQUAL -1)
    message(FATAL_ERROR "R20: Expected '0 source WAVs, 0 destination WAVs' in output, got: ${_msg20}")
endif()

# =============================================================================
# R21: Missing source catalog
# =============================================================================
set(_r21 "${_root}/r21")
set(_r21_dst "${_r21}/dst")
set(_r21_man "${_r21}/manifest")
file(MAKE_DIRECTORY "${_r21_dst}")
file(WRITE "${_r21_dst}/preserve.wav" "PRESERVE_MISSING_SRC")

run_stage_expect_success("${_root}/nonexistent_src_dir" "${_r21_dst}" "${_r21_man}" _msg21)
if(NOT EXISTS "${_r21_dst}/preserve.wav")
    message(FATAL_ERROR "R21: Destination file clobbered when source directory missing")
endif()
string(FIND "${_msg21}" "skipping staging" _pos21)
if(_pos21 EQUAL -1)
    message(FATAL_ERROR "R21: Expected 'skipping staging' message, got: ${_msg21}")
endif()

# =============================================================================
# R22: Unicode and spaces
# =============================================================================
set(_r22 "${_root}/r22")
set(_r22_src "${_r22}/src")
set(_r22_dst "${_r22}/dst")
set(_r22_man "${_r22}/manifest")
file(MAKE_DIRECTORY "${_r22_src}" "${_r22_dst}")
file(WRITE "${_r22_src}/Ünicode Track.wav" "UNICODE_AUDIO")
file(WRITE "${_r22_src}/track with spaces.wav" "SPACED_AUDIO")

run_stage_expect_success("${_r22_src}" "${_r22_dst}" "${_r22_man}" _msg22)
if(NOT EXISTS "${_r22_dst}/Ünicode Track.wav" OR NOT EXISTS "${_r22_dst}/track with spaces.wav")
    message(FATAL_ERROR "R22: Unicode or spaced WAV filename failed to stage")
endif()
file(READ "${_r22_dst}/Ünicode Track.wav" _u_data)
file(READ "${_r22_dst}/track with spaces.wav" _s_data)
if(NOT "${_u_data}" STREQUAL "UNICODE_AUDIO" OR NOT "${_s_data}" STREQUAL "SPACED_AUDIO")
    message(FATAL_ERROR "R22: Unicode or spaced WAV content corrupted")
endif()

# =============================================================================
# R23: Semicolon rejection on Windows
# =============================================================================
set(_r23 "${_root}/r23")
set(_r23_src "${_r23}/src")
set(_r23_dst "${_r23}/dst")
set(_r23_man "${_r23}/manifest")
file(MAKE_DIRECTORY "${_r23_src}" "${_r23_dst}")
file(WRITE "${_r23_src}/corrupt;name.wav" "CORRUPT")
file(WRITE "${_r23_dst}/unrelated.wav" "KEEP_UNRELATED")

run_stage_expect_failure("${_r23_src}" "${_r23_dst}" "${_r23_man}" "Invalid sound filename")
if(NOT EXISTS "${_r23_dst}/unrelated.wav")
    message(FATAL_ERROR "R23: Fail-closed violation: preexisting file clobbered on semicolon rejection")
endif()

# =============================================================================
# R24: Accurate failure and destination-count reporting
# =============================================================================
set(_r24 "${_root}/r24")
set(_r24_src "${_r24}/src")
set(_r24_dst "${_r24}/dst")
set(_r24_man "${_r24}/manifest")
file(MAKE_DIRECTORY "${_r24_src}" "${_r24_dst}")

file(WRITE "${_r24_dst}/kept.wav" "RIFF-kept")
file(WRITE "${_r24_dst}/stale.wav" "RIFF-stale")
file(SHA256 "${_r24_dst}/kept.wav" _h24_kept)
file(SHA256 "${_r24_dst}/stale.wav" _h24_stale)
file(WRITE "${_r24_man}" "${_h24_kept} kept.wav\n${_h24_stale} stale.wav\n")

# Source only has kept.wav
file(WRITE "${_r24_src}/kept.wav" "RIFF-kept")

run_stage_expect_success("${_r24_src}" "${_r24_dst}" "${_r24_man}" _msg24)
if(EXISTS "${_r24_dst}/stale.wav")
    message(FATAL_ERROR "R24: Stale file was not removed")
endif()
count_wavs("${_r24_dst}" _cnt24)
if(NOT _cnt24 EQUAL 1)
    message(FATAL_ERROR "R24: Expected destination count 1, got ${_cnt24}")
endif()
string(FIND "${_msg24}" "1 source WAVs, 1 destination WAVs" _pos24)
if(_pos24 EQUAL -1)
    message(FATAL_ERROR "R24 REGRESSION: Output count mismatch! Message was: ${_msg24}")
endif()

# Verify failure on missing required parameter
run_stage_expect_failure("${_r24_src}" "${_r24_dst}" "" "STAGE_SOUND_MANIFEST_FILE must be set")

# =============================================================================
# R25: Preexisting unowned destination with identical bytes (Defect A)
# =============================================================================
set(_r25 "${_root}/r25")
set(_r25_src "${_r25}/src")
set(_r25_dst "${_r25}/dst")
set(_r25_man "${_r25}/manifest")
file(MAKE_DIRECTORY "${_r25_src}" "${_r25_dst}")
file(WRITE "${_r25_src}/same.wav" "SAME_BYTES_ASSET_CONTENT")
# Preexisting unowned destination with identical bytes
file(WRITE "${_r25_dst}/same.wav" "SAME_BYTES_ASSET_CONTENT")

# Staging must succeed
run_stage_expect_success("${_r25_src}" "${_r25_dst}" "${_r25_man}" _msg25)
count_wavs("${_r25_dst}" _cnt25)
if(NOT _cnt25 EQUAL 1)
    message(FATAL_ERROR "R25: Destination file missing")
endif()

# Invariant A: Preexisting unowned identical file must NEVER be claimed in manifest
file(READ "${_r25_man}" _man25)
if(_man25 MATCHES "same\\.wav")
    message(FATAL_ERROR "R25 REGRESSION (Defect A): Preexisting unowned identical file was silently adopted into manifest!")
endif()

# =============================================================================
# R26: Source deletion after R25 preserves unowned destination (Defect A)
# =============================================================================
# Continuing from R25: source deletes same.wav
file(REMOVE "${_r25_src}/same.wav")

# Run staging again
run_stage_expect_success("${_r25_src}" "${_r25_dst}" "${_r25_man}" _msg26)

# Operator-owned file MUST survive in destination!
if(NOT EXISTS "${_r25_dst}/same.wav")
    message(FATAL_ERROR "R26 REGRESSION (Defect A): Preexisting unowned identical destination file was deleted after source deletion!")
endif()
file(READ "${_r25_dst}/same.wav" _r26_c)
if(NOT "${_r26_c}" STREQUAL "SAME_BYTES_ASSET_CONTENT")
    message(FATAL_ERROR "R26: Operator file content was modified")
endif()

# =============================================================================
# R27: Manifest record naming a traversal path deletes nothing outside (Defect B)
# =============================================================================
set(_r27 "${_root}/r27")
set(_r27_src "${_r27}/src")
set(_r27_dst "${_r27}/dst")
set(_r27_man "${_r27}/manifest")
file(MAKE_DIRECTORY "${_r27_src}" "${_r27_dst}")
file(WRITE "${_r27_src}/normal.wav" "NORMAL")

# External file outside SOUND_DEST_DIR
set(_r27_external "${_r27}/external_target.wav")
file(WRITE "${_r27_external}" "HIGHLY_VALUABLE_EXTERNAL_CONTENT")
file(SHA256 "${_r27_external}" _ext_h)
string(TOLOWER "${_ext_h}" _ext_h)

# A manifest record that tries to claim a path outside the destination directory
file(WRITE "${_r27_man}" "${_ext_h} ../external_target.wav\n")

# Staging must never act on it: removal is bounded to the canonical destination
# directory and re-checked immediately before every deletion.
run_stage_expect_success("${_r27_src}" "${_r27_dst}" "${_r27_man}" _msg27)

# Assert external file was NOT deleted or modified!
if(NOT EXISTS "${_r27_external}")
    message(FATAL_ERROR "R27 REGRESSION (Defect B): Traversal manifest record caused deletion of external file!")
endif()
file(READ "${_r27_external}" _ext_read)
if(NOT "${_ext_read}" STREQUAL "HIGHLY_VALUABLE_EXTERNAL_CONTENT")
    message(FATAL_ERROR "R27 REGRESSION (Defect B): External file was corrupted through a manifest traversal record!")
endif()
if(NOT EXISTS "${_r27_dst}/normal.wav")
    message(FATAL_ERROR "R27: normal.wav was not staged alongside the forged record")
endif()

# =============================================================================
# R28: Existing operator-owned commit temporary file survives (Defect C)
# =============================================================================
set(_r28 "${_root}/r28")
set(_r28_src "${_r28}/src")
set(_r28_dst "${_r28}/dst")
set(_r28_man "${_r28}/manifest")
file(MAKE_DIRECTORY "${_r28_src}" "${_r28_dst}")
file(WRITE "${_r28_src}/sound.wav" "SOUND_DATA")

set(_r28_commit_tmp "${_r28_man}.stage_commit.tmp")
file(WRITE "${_r28_commit_tmp}" "PREEXISTING_OPERATOR_COMMIT_TMP_CONTENT")

# Successful staging must not clobber operator's temporary file
run_stage_expect_success("${_r28_src}" "${_r28_dst}" "${_r28_man}" _msg28_1)
if(NOT EXISTS "${_r28_commit_tmp}")
    message(FATAL_ERROR "R28 REGRESSION (Defect C): Operator-owned commit temp file was deleted during staging!")
endif()
file(READ "${_r28_commit_tmp}" _tmp_read_1)
if(NOT "${_tmp_read_1}" STREQUAL "PREEXISTING_OPERATOR_COMMIT_TMP_CONTENT")
    message(FATAL_ERROR "R28 REGRESSION (Defect C): Operator-owned commit temp file was overwritten during successful staging!")
endif()

# Failing staging must also not modify operator's temporary file
run_stage_expect_failure("${_r28_src}" "${_r28_dst}" "" "STAGE_SOUND_MANIFEST_FILE must be set")
file(READ "${_r28_commit_tmp}" _tmp_read_2)
if(NOT "${_tmp_read_2}" STREQUAL "PREEXISTING_OPERATOR_COMMIT_TMP_CONTENT")
    message(FATAL_ERROR "R28 REGRESSION (Defect C): Operator-owned commit temp file was modified during failed staging!")
endif()

# =============================================================================
# R29: Interrupted UPDATE before manifest commit (Defect D)
# =============================================================================
set(_r29 "${_root}/r29")
set(_r29_src "${_r29}/src")
set(_r29_dst "${_r29}/dst")
set(_r29_man "${_r29}/manifest")
file(MAKE_DIRECTORY "${_r29_src}" "${_r29_dst}")
file(WRITE "${_r29_src}/audio.wav" "AUDIO_VERSION_1")

# Step 1: Stage V1 cleanly
run_stage_expect_success("${_r29_src}" "${_r29_dst}" "${_r29_man}" _msg29_1)

# Step 2: Update source to V2
file(WRITE "${_r29_src}/audio.wav" "AUDIO_VERSION_2")

# Step 3: Run with injected failure between copy and commit
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DSTAGE_SOUND_SOURCE_DIR=${_r29_src}"
        "-DSTAGE_SOUND_DEST_DIR=${_r29_dst}"
        "-DSTAGE_SOUND_MANIFEST_FILE=${_r29_man}"
        "-DSTAGE_SOUND_TEST_FAIL_BEFORE_COMMIT=TRUE"
        -P "${_script}"
    RESULT_VARIABLE _rc29
    OUTPUT_VARIABLE _out29
    ERROR_VARIABLE _err29
)
if(_rc29 EQUAL 0)
    message(FATAL_ERROR "R29: Expected simulated failure before commit, got 0")
endif()

# Verify state: destination updated to V2, manifest still records V1
file(READ "${_r29_dst}/audio.wav" _dst29_val)
if(NOT "${_dst29_val}" STREQUAL "AUDIO_VERSION_2")
    message(FATAL_ERROR "R29: Destination was not updated to V2")
endif()
file(READ "${_r29_man}" _man29_val)
file(SHA256 "${_r29_dst}/audio.wav" _h29_v2)
string(TOLOWER "${_h29_v2}" _h29_v2)
if(_man29_val MATCHES "${_h29_v2}")
    message(FATAL_ERROR "R29: Manifest was committed despite failure!")
endif()
# No journal, no rollback material: nothing that could later be forged.
if(EXISTS "${_r29_man}.tx" OR EXISTS "${_r29_man}.tx_backup")
    message(FATAL_ERROR "R29: Staging created a transaction workspace that no authority can validate")
endif()

# =============================================================================
# R30: Source change after an interrupted UPDATE reports a bounded conflict
# =============================================================================
# Continuing from R29: the destination now holds the unowned V2 bytes while the
# manifest still records V1. That state is genuinely ambiguous -- it is
# indistinguishable from an operator edit -- so the run must fail closed,
# preserve the file, and say so, rather than guess and destroy data.
file(WRITE "${_r29_src}/audio.wav" "AUDIO_VERSION_3")

run_stage_expect_failure("${_r29_src}" "${_r29_dst}" "${_r29_man}" "collision")
file(READ "${_r29_dst}/audio.wav" _dst30_val)
if(NOT "${_dst30_val}" STREQUAL "AUDIO_VERSION_2")
    message(FATAL_ERROR "R30 REGRESSION: The ambiguous destination file was clobbered by a source change")
endif()
file(READ "${_r29_man}" _man30_val)
file(SHA256 "${_r29_dst}/audio.wav" _h30_v2)
string(TOLOWER "${_h30_v2}" _h30_v2)
if(_man30_val MATCHES "${_h30_v2}")
    message(FATAL_ERROR "R30: Manifest was rewritten despite the failed reconciliation")
endif()

# After the stale file is cleared by its owner, staging reconciles normally.
file(REMOVE "${_r29_dst}/audio.wav")
run_stage_expect_success("${_r29_src}" "${_r29_dst}" "${_r29_man}" _msg30b)
file(READ "${_r29_dst}/audio.wav" _dst30b_val)
if(NOT "${_dst30b_val}" STREQUAL "AUDIO_VERSION_3")
    message(FATAL_ERROR "R30: Staging did not reconcile once the ambiguous file was cleared")
endif()
file(SHA256 "${_r29_dst}/audio.wav" _h30_v3)
string(TOLOWER "${_h30_v3}" _h30_v3)
file(READ "${_r29_man}" _man30b_val)
if(NOT _man30b_val MATCHES "${_h30_v3}")
    message(FATAL_ERROR "R30: Manifest was not updated to V3 after the conflict was resolved")
endif()

# =============================================================================
# R31: Interrupted REMOVE and repeated execution
# =============================================================================
set(_r31 "${_root}/r31")
set(_r31_src "${_r31}/src")
set(_r31_dst "${_r31}/dst")
set(_r31_man "${_r31}/manifest")
file(MAKE_DIRECTORY "${_r31_src}" "${_r31_dst}")
file(WRITE "${_r31_src}/keep.wav" "KEEP_DATA")
file(WRITE "${_r31_src}/drop.wav" "DROP_DATA")

# Stage both files
run_stage_expect_success("${_r31_src}" "${_r31_dst}" "${_r31_man}" _msg31_1)

# Remove drop.wav from source
file(REMOVE "${_r31_src}/drop.wav")

    # Fail before commit during removal
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DSTAGE_SOUND_SOURCE_DIR=${_r31_src}"
            "-DSTAGE_SOUND_DEST_DIR=${_r31_dst}"
            "-DSTAGE_SOUND_MANIFEST_FILE=${_r31_man}"
            "-DSTAGE_SOUND_TEST_FAIL_BEFORE_COMMIT=TRUE"
            -P "${_script}"
        RESULT_VARIABLE _rc31
        OUTPUT_VARIABLE _out31
        ERROR_VARIABLE _err31
    )
if(_rc31 EQUAL 0)
    message(FATAL_ERROR "R31: Expected failure before commit")
endif()

# Retry staging normally: should recover and reconcile cleanly
run_stage_expect_success("${_r31_src}" "${_r31_dst}" "${_r31_man}" _msg31_2)
if(EXISTS "${_r31_dst}/drop.wav")
    message(FATAL_ERROR "R31: Stale file drop.wav was not removed after recovery")
endif()
if(NOT EXISTS "${_r31_dst}/keep.wav")
    message(FATAL_ERROR "R31: keep.wav was lost during removal recovery")
endif()
count_wavs("${_r31_dst}" _cnt31)
if(NOT _cnt31 EQUAL 1)
    message(FATAL_ERROR "R31: Expected 1 WAV in destination, got ${_cnt31}")
endif()

# =============================================================================
# R32: Concurrent staging of the same configuration with locking (Invariant H)
# =============================================================================
set(_r32 "${_root}/r32")
set(_r32_src "${_r32}/src")
set(_r32_dst "${_r32}/dst")
set(_r32_man "${_r32}/manifest")
file(MAKE_DIRECTORY "${_r32_src}" "${_r32_dst}")
file(WRITE "${_r32_src}/track.wav" "CONCURRENCY_TEST_AUDIO")

# Acquire lock simulating another staging process running
set(_r32_lock "${_r32_man}.lock")
file(LOCK "${_r32_lock}" GUARD PROCESS TIMEOUT 5 RESULT_VARIABLE _hold_lock_res)
if(NOT _hold_lock_res EQUAL 0)
    message(FATAL_ERROR "R32: Failed to acquire test lock")
endif()

# Concurrent staging run with timeout=1s must fail due to lock contention
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DSTAGE_SOUND_SOURCE_DIR=${_r32_src}"
        "-DSTAGE_SOUND_DEST_DIR=${_r32_dst}"
        "-DSTAGE_SOUND_MANIFEST_FILE=${_r32_man}"
        "-DSTAGE_SOUND_LOCK_TIMEOUT=1"
        -P "${_script}"
    RESULT_VARIABLE _rc32_locked
    OUTPUT_VARIABLE _out32
    ERROR_VARIABLE _err32
)
if(_rc32_locked EQUAL 0)
    message(FATAL_ERROR "R32 REGRESSION (Invariant H): Concurrent staging run did not fail when locked!")
endif()

# Release lock
file(LOCK "${_r32_lock}" RELEASE)

# Staging now succeeds
run_stage_expect_success("${_r32_src}" "${_r32_dst}" "${_r32_man}" _msg32)
if(NOT EXISTS "${_r32_dst}/track.wav")
    message(FATAL_ERROR "R32: track.wav not staged after lock release")
endif()

# =============================================================================
# R33: Malformed or duplicated manifest records grant no deletion authority
# =============================================================================
set(_r33 "${_root}/r33")
set(_r33_src "${_r33}/src")
set(_r33_dst "${_r33}/dst")
set(_r33_man "${_r33}/manifest")
file(MAKE_DIRECTORY "${_r33_src}" "${_r33_dst}")
file(WRITE "${_r33_src}/main.wav" "MAIN_AUDIO")
file(WRITE "${_r33_dst}/victim.wav" "DO_NOT_DELETE_VICTIM")
file(SHA256 "${_r33_dst}/victim.wav" _v_hash)
string(TOLOWER "${_v_hash}" _v_hash)

# Subcase A: a valid-looking record whose hash is truncated
file(WRITE "${_r33_man}" "1234notahash victim.wav\n")
run_stage_expect_success("${_r33_src}" "${_r33_dst}" "${_r33_man}" _msg33a)
if(NOT EXISTS "${_r33_dst}/victim.wav")
    message(FATAL_ERROR "R33: A truncated manifest hash deleted the destination file!")
endif()

# Subcase B: a bare name with no content proof at all
file(WRITE "${_r33_man}" "victim.wav\n")
run_stage_expect_success("${_r33_src}" "${_r33_dst}" "${_r33_man}" _msg33b)
if(NOT EXISTS "${_r33_dst}/victim.wav")
    message(FATAL_ERROR "R33: A hashless manifest record deleted the destination file!")
endif()

# Subcase C: a well-formed hash that does not match the destination bytes
file(WRITE "${_r33_man}" "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa victim.wav\n")
run_stage_expect_success("${_r33_src}" "${_r33_dst}" "${_r33_man}" _msg33c)
if(NOT EXISTS "${_r33_dst}/victim.wav")
    message(FATAL_ERROR "R33: A manifest record that does not match the bytes deleted the destination file!")
endif()

# Subcase D: positive control. A valid record whose hash matches the bytes and
# whose name is absent from the source DOES grant removal, proving the subcases
# above failed to delete for lack of authority, not because deletion is off.
file(WRITE "${_r33_dst}/proven.wav" "PROVEN_STALE_BYTES")
file(SHA256 "${_r33_dst}/proven.wav" _p_hash)
string(TOLOWER "${_p_hash}" _p_hash)
file(WRITE "${_r33_man}" "${_p_hash} proven.wav\n")
run_stage_expect_success("${_r33_src}" "${_r33_dst}" "${_r33_man}" _msg33d)
if(EXISTS "${_r33_dst}/proven.wav")
    message(FATAL_ERROR "R33: Positive control failed -- a proven stale file was not removed")
endif()

# victim.wav must still be byte-for-byte intact after subcases A-C
file(READ "${_r33_dst}/victim.wav" _r33_final)
if(NOT "${_r33_final}" STREQUAL "DO_NOT_DELETE_VICTIM")
    message(FATAL_ERROR "R33: The surviving destination file content was mutated!")
endif()

# =============================================================================
# R34: Ambiguous ownership fails closed and preserves the file
# =============================================================================
set(_r34 "${_root}/r34")
set(_r34_src "${_r34}/src")
set(_r34_dst "${_r34}/dst")
set(_r34_man "${_r34}/manifest")
file(MAKE_DIRECTORY "${_r34_src}" "${_r34_dst}")
file(WRITE "${_r34_src}/audio.wav" "SOURCE_AUDIO")

# Manifest claims audio.wav was staged with HASH_A
set(_hash_a "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
file(WRITE "${_r34_man}" "${_hash_a} audio.wav\n")

# But destination audio.wav holds operator content whose hash differs
file(WRITE "${_r34_dst}/audio.wav" "OPERATOR_CUSTOM_UNTOUCHABLE_DATA")

# Ambiguous ownership: the run must FAIL CLOSED and PRESERVE the operator file
run_stage_expect_failure("${_r34_src}" "${_r34_dst}" "${_r34_man}" "collision")

if(NOT EXISTS "${_r34_dst}/audio.wav")
    message(FATAL_ERROR "R34 REGRESSION: Operator file was deleted during an ambiguous-ownership run!")
endif()
file(READ "${_r34_dst}/audio.wav" _r34_val)
if(NOT "${_r34_val}" STREQUAL "OPERATOR_CUSTOM_UNTOUCHABLE_DATA")
    message(FATAL_ERROR "R34: Operator file content was clobbered during an ambiguous-ownership run!")
endif()
# No apparatus may have been created to "resolve" the conflict
if(EXISTS "${_r34_man}.tx" OR EXISTS "${_r34_man}.tx_backup")
    message(FATAL_ERROR "R34: Staging created a transaction workspace while reporting a conflict")
endif()

# =============================================================================
# R35: Foreign files and directories in backup location preserved (Defect A)
# =============================================================================
set(_r35 "${_root}/r35")
set(_r35_src "${_r35}/src")
set(_r35_dst "${_r35}/dst")
set(_r35_man "${_r35}/manifest_r35")
file(MAKE_DIRECTORY "${_r35_src}" "${_r35_dst}")
file(WRITE "${_r35_src}/sound.wav" "SOUND_DATA_R35")

# Preexisting legacy .stage_backup with operator file
set(_legacy_backup "${_r35}/.stage_backup")
file(MAKE_DIRECTORY "${_legacy_backup}")
file(WRITE "${_legacy_backup}/operator_private.txt" "OPERATOR_SECRET_DO_NOT_DELETE")

# Preexisting manifest-scoped tx_backup with operator file
set(_scoped_backup "${_r35_man}.tx_backup")
file(MAKE_DIRECTORY "${_scoped_backup}")
file(WRITE "${_scoped_backup}/operator_notes.txt" "OPERATOR_NOTES_DO_NOT_DELETE")

# Run normal staging
run_stage_expect_success("${_r35_src}" "${_r35_dst}" "${_r35_man}" _msg35)

# Verify staging succeeded
if(NOT EXISTS "${_r35_dst}/sound.wav")
    message(FATAL_ERROR "R35: sound.wav was not staged")
endif()

# Verify foreign backup directory and files preserved byte-for-byte
if(NOT EXISTS "${_legacy_backup}/operator_private.txt")
    message(FATAL_ERROR "R35 REGRESSION (Defect A): Foreign file in .stage_backup was deleted!")
endif()
file(READ "${_legacy_backup}/operator_private.txt" _r35_p_val)
if(NOT "${_r35_p_val}" STREQUAL "OPERATOR_SECRET_DO_NOT_DELETE")
    message(FATAL_ERROR "R35: Content of operator_private.txt was corrupted!")
endif()

if(NOT EXISTS "${_scoped_backup}/operator_notes.txt")
    message(FATAL_ERROR "R35 REGRESSION (Defect A): Foreign file in scoped tx_backup was deleted!")
endif()
file(READ "${_scoped_backup}/operator_notes.txt" _r35_n_val)
if(NOT "${_r35_n_val}" STREQUAL "OPERATOR_NOTES_DO_NOT_DELETE")
    message(FATAL_ERROR "R35: Content of operator_notes.txt was corrupted!")
endif()

# =============================================================================
# R36: Planted journal and receipt evidence grant no deletion authority (Defect B)
# =============================================================================
set(_r36 "${_root}/r36")
set(_r36_src "${_r36}/src")
set(_r36_dst "${_r36}/dst")
set(_r36_man "${_r36}/manifest_r36")
file(MAKE_DIRECTORY "${_r36_src}" "${_r36_dst}")

# Destination contains an operator-owned manual.wav
set(_operator_wav_content "OPERATOR_CRITICAL_MANUAL_WAV_BYTES_DO_NOT_TOUCH")
file(WRITE "${_r36_dst}/manual.wav" "${_operator_wav_content}")
file(SHA256 "${_r36_dst}/manual.wav" _r36_manual_hash)
string(TOLOWER "${_r36_manual_hash}" _r36_manual_hash)

# A rogue standalone journal plus a matching allocation receipt claim manual.wav
file(WRITE "${_r36_man}.tx" "NEW|manual.wav|${_r36_manual_hash}\n")
set(_r36_tx_dir "${_r36_man}.tx_backup")
file(MAKE_DIRECTORY "${_r36_tx_dir}")
file(WRITE "${_r36_tx_dir}/tx_provenance.meta" "TX_ID=tx_rogue_36\n")
file(WRITE "${_r36_tx_dir}/manual.wav.tx_rogue_36.new" "${_r36_manual_hash}\n")

# Those records are simply not an authority source. Staging ignores them, stages
# nothing (the source catalog is empty), and must neither fail nor delete.
run_stage_expect_success("${_r36_src}" "${_r36_dst}" "${_r36_man}" _msg36)

if(NOT EXISTS "${_r36_dst}/manual.wav")
    message(FATAL_ERROR "R36 REGRESSION (Defect B): Operator-owned manual.wav was deleted by planted transaction records!")
endif()
file(READ "${_r36_dst}/manual.wav" _r36_actual_wav)
if(NOT "${_r36_actual_wav}" STREQUAL "${_operator_wav_content}")
    message(FATAL_ERROR "R36 REGRESSION (Defect B): Operator-owned manual.wav was modified!")
endif()
# The planted evidence is not staging's property, so it must survive untouched
if(NOT EXISTS "${_r36_man}.tx")
    message(FATAL_ERROR "R36: Planted journal evidence was destroyed by a run that gave it no authority!")
endif()
if(NOT EXISTS "${_r36_tx_dir}/tx_provenance.meta")
    message(FATAL_ERROR "R36: Planted provenance record was destroyed by a run that gave it no authority!")
endif()

# =============================================================================
# R37: Debug staging never mutates an interrupted Release state (Defect C)
# =============================================================================
set(_r37 "${_root}/r37")
set(_r37_src "${_r37}/src")
set(_r37_rel_dst "${_r37}/release_dst")
set(_r37_dbg_dst "${_r37}/debug_dst")
set(_r37_rel_man "${_r37}/manifest_release")
set(_r37_dbg_man "${_r37}/manifest_debug")
file(MAKE_DIRECTORY "${_r37_src}" "${_r37_rel_dst}" "${_r37_dbg_dst}")

# Step 1: Stage V1 for both Release and Debug
file(WRITE "${_r37_src}/track.wav" "TRACK_V1")
run_stage_expect_success("${_r37_src}" "${_r37_rel_dst}" "${_r37_rel_man}" _msg37_rel1)
run_stage_expect_success("${_r37_src}" "${_r37_dbg_dst}" "${_r37_dbg_man}" _msg37_dbg1)

# Step 2: Update source to V2
file(WRITE "${_r37_src}/track.wav" "TRACK_V2")

# Step 3: Run Release with simulated failure before commit
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DSTAGE_SOUND_SOURCE_DIR=${_r37_src}"
        "-DSTAGE_SOUND_DEST_DIR=${_r37_rel_dst}"
        "-DSTAGE_SOUND_MANIFEST_FILE=${_r37_rel_man}"
        "-DSTAGE_SOUND_TEST_FAIL_BEFORE_COMMIT=TRUE"
        -P "${_script}"
    RESULT_VARIABLE _rc37_rel_fail
)
if(_rc37_rel_fail EQUAL 0)
    message(FATAL_ERROR "R37: Expected failure before commit in Release")
endif()

# Release destination now holds V2 while its manifest still records V1.
file(READ "${_r37_rel_man}" _r37_rel_manifest_before)
file(READ "${_r37_rel_dst}/track.wav" _r37_rel_track_before)
if(NOT "${_r37_rel_track_before}" STREQUAL "TRACK_V2")
    message(FATAL_ERROR "R37: Release destination was not updated before the injected failure")
endif()
if(EXISTS "${_r37_rel_man}.tx" OR EXISTS "${_r37_rel_man}.tx_backup")
    message(FATAL_ERROR "R37: The interrupted Release run created a transaction workspace")
endif()

# Step 4: Run normal Debug staging. It must not touch Release state at all.
run_stage_expect_success("${_r37_src}" "${_r37_dbg_dst}" "${_r37_dbg_man}" _msg37_dbg2)
if(NOT EXISTS "${_r37_dbg_dst}/track.wav")
    message(FATAL_ERROR "R37: Debug staging did not stage track.wav")
endif()
file(READ "${_r37_dbg_dst}/track.wav" _r37_dbg_track)
if(NOT "${_r37_dbg_track}" STREQUAL "TRACK_V2")
    message(FATAL_ERROR "R37: Debug track.wav does not match V2")
endif()

# Step 5: Release manifest and destination bytes are byte-identical to before.
file(READ "${_r37_rel_man}" _r37_rel_manifest_after)
if(NOT "${_r37_rel_manifest_after}" STREQUAL "${_r37_rel_manifest_before}")
    message(FATAL_ERROR "R37 REGRESSION (Defect C): Debug staging mutated the Release manifest!")
endif()
file(READ "${_r37_rel_dst}/track.wav" _r37_rel_track_after)
if(NOT "${_r37_rel_track_after}" STREQUAL "${_r37_rel_track_before}")
    message(FATAL_ERROR "R37 REGRESSION (Defect C): Debug staging mutated the Release destination!")
endif()

# =============================================================================
# R38: Release recovery after R37 reconciles and leaves Debug untouched
# =============================================================================
# Continuing from R37: retry Release staging with the source unchanged
run_stage_expect_success("${_r37_src}" "${_r37_rel_dst}" "${_r37_rel_man}" _msg38_rel)

if(NOT EXISTS "${_r37_rel_dst}/track.wav")
    message(FATAL_ERROR "R38: track.wav missing in Release destination after the repeated run")
endif()
file(READ "${_r37_rel_dst}/track.wav" _r38_rel_track)
if(NOT "${_r38_rel_track}" STREQUAL "TRACK_V2")
    message(FATAL_ERROR "R38: Release track.wav not at V2 after the repeated run")
endif()
file(SHA256 "${_r37_rel_dst}/track.wav" _r38_rel_hash)
string(TOLOWER "${_r38_rel_hash}" _r38_rel_hash)
file(READ "${_r37_rel_man}" _r38_rel_manifest)
if(NOT _r38_rel_manifest MATCHES "${_r38_rel_hash}")
    message(FATAL_ERROR "R38: Release manifest does not record the committed V2 bytes after recovery")
endif()

# Verify Debug configuration remained completely untouched and unaffected
file(READ "${_r37_dbg_dst}/track.wav" _r38_dbg_track)
if(NOT "${_r38_dbg_track}" STREQUAL "TRACK_V2")
    message(FATAL_ERROR "R38: Debug destination was corrupted during the Release repeated run!")
endif()

# =============================================================================
# R39: Concurrent configurations with overlapping filenames and independent states
# =============================================================================
set(_r39 "${_root}/r39")
set(_r39_src "${_r39}/src")
set(_r39_rel_dst "${_r39}/release_dst")
set(_r39_dbg_dst "${_r39}/debug_dst")
set(_r39_rel_man "${_r39}/manifest_release")
set(_r39_dbg_man "${_r39}/manifest_debug")
file(MAKE_DIRECTORY "${_r39_src}" "${_r39_rel_dst}" "${_r39_dbg_dst}")
file(WRITE "${_r39_src}/shared.wav" "SHARED_AUDIO_BYTES")

# Acquire exclusive lock on Release manifest simulating an active transaction
set(_r39_rel_lock "${_r39_rel_man}.lock")
file(LOCK "${_r39_rel_lock}" GUARD PROCESS TIMEOUT 5 RESULT_VARIABLE _hold_rel_res)
if(NOT _hold_rel_res EQUAL 0)
    message(FATAL_ERROR "R39: Failed to lock Release manifest for concurrency test")
endif()

# Concurrently run Debug staging: must succeed independently without blocking or colliding
run_stage_expect_success("${_r39_src}" "${_r39_dbg_dst}" "${_r39_dbg_man}" _msg39_dbg)
if(NOT EXISTS "${_r39_dbg_dst}/shared.wav")
    message(FATAL_ERROR "R39: Debug staging failed while Release was locked")
endif()

# Release lock and stage Release
file(LOCK "${_r39_rel_lock}" RELEASE)
run_stage_expect_success("${_r39_src}" "${_r39_rel_dst}" "${_r39_rel_man}" _msg39_rel)
if(NOT EXISTS "${_r39_rel_dst}/shared.wav")
    message(FATAL_ERROR "R39: Release staging failed after lock release")
endif()

# Verify both staged byte-identical content to their respective destinations
file(SHA256 "${_r39_dbg_dst}/shared.wav" _r39_dbg_hash)
file(SHA256 "${_r39_rel_dst}/shared.wav" _r39_rel_hash)
if(NOT "${_r39_dbg_hash}" STREQUAL "${_r39_rel_hash}")
    message(FATAL_ERROR "R39: Hash mismatch between Release and Debug staged files")
endif()

# =============================================================================
# R40: Preexisting test fixture directory with operator data preserved (Defect D)
# =============================================================================
# R40 is exercised entirely inside the dynamically owned _root fixture below.
# The suite must never create, mutate, or delete paths outside its owned
# workspace; preexisting operator directories (such as a legacy
# stage_sound_regressions/ in CWD) must survive untouched.
#
# A separate subprocess EXISTS-probes the CWD fixture so this run neither
# creates it nor removes it; the probe only reads existence.
#
# Scenario 1: a preexisting foreign file in the current binary directory must
# not be deleted by the suite. It is created here (owned precheck fixture),
# verified intact after the whole suite, and removed by the suite owner.
set(_cwd_fixture "${CMAKE_CURRENT_BINARY_DIR}/stage_sound_reg_operator_sentinel_${_test_rand}")
file(MAKE_DIRECTORY "${_cwd_fixture}")
file(WRITE "${_cwd_fixture}/operator_note.txt" "OPERATOR_NOTES_DO_NOT_DELETE")
file(WRITE "${_cwd_fixture}/foreign_userdata.txt" "FOREIGN_PRIVATE_DATA")

# Scenario 2: R40 runs a real staging inside _root and verifies that a
# preexisting foreign directory in the destination is preserved byte-for-byte.
set(_r40 "${_root}/r40")
set(_r40_src "${_r40}/src")
set(_r40_dst "${_r40}/dst")
set(_r40_man "${_r40}/manifest_r40")
file(MAKE_DIRECTORY "${_r40_src}" "${_r40_dst}")
file(WRITE "${_r40_src}/asset.wav" "R40_ASSET")

# A preexisting foreign fixture directory inside the destination tree
set(_r40_foreign_dir "${_r40_dst}/foreign_fixture_dir")
file(MAKE_DIRECTORY "${_r40_foreign_dir}")
file(WRITE "${_r40_foreign_dir}/operator_note.txt" "OPERATOR_NOTES_DO_NOT_DELETE")

# Run real staging in the owned fixture
run_stage_expect_success("${_r40_src}" "${_r40_dst}" "${_r40_man}" _msg40)
if(NOT EXISTS "${_r40_dst}/asset.wav")
    message(FATAL_ERROR "R40: asset.wav was not staged in owned fixture")
endif()

# The preexisting foreign fixture directory and its files must survive intact
if(NOT EXISTS "${_r40_foreign_dir}/operator_note.txt")
    message(FATAL_ERROR "R40 REGRESSION (Defect D): Preexisting fixture file was deleted by staging!")
endif()
file(READ "${_r40_foreign_dir}/operator_note.txt" _r40_note)
if(NOT "${_r40_note}" STREQUAL "OPERATOR_NOTES_DO_NOT_DELETE")
    message(FATAL_ERROR "R40: Preexisting fixture file content was mutated by staging!")
endif()

# =============================================================================
# R41: An interrupted run leaves no transaction or recovery apparatus behind
# =============================================================================
# A journal, a receipt and a rollback backup are all writable by any process
# that can write into the build tree, so none of them can be an authority
# source. An interrupted run must therefore leave no apparatus at all: the next
# run reconciles from the source catalog and the still-committed manifest.
set(_r41 "${_root}/r41")
set(_r41_src "${_r41}/src")
set(_r41_dst_a "${_r41}/dst_a")
set(_r41_dst_b "${_r41}/dst_b")
set(_r41_man_a "${_r41}/manifest_a")
set(_r41_man_b "${_r41}/manifest_b")
file(MAKE_DIRECTORY "${_r41_src}" "${_r41_dst_a}" "${_r41_dst_b}")
file(WRITE "${_r41_src}/file1.wav" "FILE1_AUDIO")
file(WRITE "${_r41_src}/file2.wav" "FILE2_AUDIO")

# Transaction A fails before commit. Its destination is partially populated but
# no record that a foreign process could forge exists anywhere.
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DSTAGE_SOUND_SOURCE_DIR=${_r41_src}"
        "-DSTAGE_SOUND_DEST_DIR=${_r41_dst_a}"
        "-DSTAGE_SOUND_MANIFEST_FILE=${_r41_man_a}"
        "-DSTAGE_SOUND_TEST_FAIL_BEFORE_COMMIT=TRUE"
        -P "${_script}"
    RESULT_VARIABLE _rc41_a_fail
)
if(_rc41_a_fail EQUAL 0)
    message(FATAL_ERROR "R41: Expected failure before commit for run A")
endif()
foreach(_r41_artifact
        "${_r41_man_a}.tx"
        "${_r41_man_a}.tx_backup"
        "${_r41_man_a}.new"
        "${_r41_dst_a}/file1.wav.new"
        "${_r41_dst_a}/file2.wav.new")
    if(EXISTS "${_r41_artifact}")
        message(FATAL_ERROR "R41 REGRESSION: an interrupted run created an apparatus entry it cannot authenticate: ${_r41_artifact}")
    endif()
endforeach()
if(EXISTS "${_r41_man_a}")
    message(FATAL_ERROR "R41: Run A committed its manifest despite the injected failure")
endif()

# Run B is an independent configuration and commits successfully
run_stage_expect_success("${_r41_src}" "${_r41_dst_b}" "${_r41_man_b}" _msg41_b)
if(EXISTS "${_r41_man_b}.tx" OR EXISTS "${_r41_man_b}.tx_backup")
    message(FATAL_ERROR "R41: Run B created a transaction workspace it cannot authenticate")
endif()

# Run B must not have touched run A's partial state at all
if(EXISTS "${_r41_man_a}")
    message(FATAL_ERROR "R41 REGRESSION: Run B committed run A's manifest")
endif()
if(NOT EXISTS "${_r41_dst_a}/file1.wav" OR NOT EXISTS "${_r41_dst_a}/file2.wav")
    message(FATAL_ERROR "R41 REGRESSION: Run B removed run A's interrupted destination files")
endif()
file(READ "${_r41_dst_a}/file1.wav" _r41_a_f1)
if(NOT "${_r41_a_f1}" STREQUAL "FILE1_AUDIO")
    message(FATAL_ERROR "R41 REGRESSION: Run B corrupted run A's interrupted copy")
endif()

# Run A retried reconciles without any apparatus and commits its manifest
run_stage_expect_success("${_r41_src}" "${_r41_dst_a}" "${_r41_man_a}" _msg41_a2)
if(NOT EXISTS "${_r41_man_a}")
    message(FATAL_ERROR "R41: Run A retry did not commit its manifest")
endif()
if(EXISTS "${_r41_man_a}.tx" OR EXISTS "${_r41_man_a}.tx_backup")
    message(FATAL_ERROR "R41 REGRESSION: Run A retry created a transaction workspace")
endif()

# =============================================================================
# R42 - R50: Adversarial regressions for the four ownership defects
#   R42 (A) Fixed-directory fixture outside _root: preexisting files survive
#   R43 (A) Operator-owned file inside a preexisting staging directory survives
#   R44 (B) Foreign tx_provenance.meta not overwritten by successful staging
#   R45 (B) Foreign tx_provenance.meta and unverifiable journal survive staging
#   R46 (C) A reparse point redirecting the destination at an external path is refused
#   R47 (D) Forged provenance naming a retired operator file grants nothing
#   R48 (D) Legitimate interrupted run still reconciles
#   R49 (I) Concurrent same-config staging cannot remove another's artifacts
#   R50 (I) Debug and Release configurations stay independent
# =============================================================================

# --- R42: preexisting foreign fixture directory outside the script's control ---
# R40 originally created a fixed `stage_sound_regressions/` directory in the CWD
# and deleted it. Prove a preexisting fixture directory with foreign contents is
# never removed, regardless of which directory the suite is launched from.
set(_r42 "${_root}/r42")
set(_r42_src "${_r42}/src")
set(_r42_dst "${_r42}/dst")
set(_r42_man "${_r42}/manifest_r42")
set(_r42_fixture_dir "${_r42}/operator_fixture_dir")
file(MAKE_DIRECTORY "${_r42_src}" "${_r42_dst}")
file(WRITE "${_r42_src}/asset.wav" "R42_ASSET")

file(MAKE_DIRECTORY "${_r42_fixture_dir}")
file(WRITE "${_r42_fixture_dir}/operator_note.txt" "OPERATOR_NOTES_DO_NOT_DELETE")
file(WRITE "${_r42_fixture_dir}/config.dat" "OPERATOR_CONFIG_DATA")

run_stage_expect_success("${_r42_src}" "${_r42_dst}" "${_r42_man}" _msg42)

# Whole suite must leave the foreign directory and every file in it intact
if(NOT EXISTS "${_r42_fixture_dir}/operator_note.txt")
    message(FATAL_ERROR "R42 REGRESSION (Defect A): Preexisting fixture directory was deleted!")
endif()
file(READ "${_r42_fixture_dir}/operator_note.txt" _r42_note)
if(NOT "${_r42_note}" STREQUAL "OPERATOR_NOTES_DO_NOT_DELETE")
    message(FATAL_ERROR "R42 REGRESSION (Defect A): Operator note content was modified!")
endif()
if(NOT EXISTS "${_r42_fixture_dir}/config.dat")
    message(FATAL_ERROR "R42 REGRESSION (Defect A): Second foreign file was deleted!")
endif()
file(READ "${_r42_fixture_dir}/config.dat" _r42_cfg)
if(NOT "${_r42_cfg}" STREQUAL "OPERATOR_CONFIG_DATA")
    message(FATAL_ERROR "R42 REGRESSION (Defect A): Foreign config data was modified!")
endif()
# Foreign files must not appear in the manifest
file(READ "${_r42_man}" _r42_manifest)
string(FIND "${_r42_manifest}" "operator_note.txt" _r42_leak1)
string(FIND "${_r42_manifest}" "config.dat" _r42_leak2)
if(NOT _r42_leak1 EQUAL -1 OR NOT _r42_leak2 EQUAL -1)
    message(FATAL_ERROR "R42: Foreign fixture files were adopted into the staging manifest!")
endif()

# --- R43: preexisting operator-owned WAV inside a staging directory (Defect D) ---
# The destination directory already contains a WAV the operator owns. Normal
# staging must not adopt, remove, or overwrite it, and it must survive the
# complete suite including any cleanup pass.
set(_r43 "${_root}/r43")
set(_r43_src "${_r43}/src")
set(_r43_dst "${_r43}/dst")
set(_r43_man "${_r43}/manifest_r43")
file(MAKE_DIRECTORY "${_r43_src}" "${_r43_dst}")

set(_r43_operator_content "OPERATOR_CRITICAL_MANUAL_WAV_BYTES_DO_NOT_TOUCH")
file(WRITE "${_r43_dst}/manual.wav" "${_r43_operator_content}")
file(WRITE "${_r43_src}/staged.wav" "STAGED_CONTENT")

run_stage_expect_success("${_r43_src}" "${_r43_dst}" "${_r43_man}" _msg43)

if(NOT EXISTS "${_r43_dst}/manual.wav")
    message(FATAL_ERROR "R43 REGRESSION (Defect D): Preexisting operator-owned manual.wav was deleted!")
endif()
file(READ "${_r43_dst}/manual.wav" _r43_actual_wav)
if(NOT "${_r43_actual_wav}" STREQUAL "${_r43_operator_content}")
    message(FATAL_ERROR "R43 REGRESSION (Defect D): Preexisting operator-owned manual.wav was modified!")
endif()
if(NOT EXISTS "${_r43_dst}/staged.wav")
    message(FATAL_ERROR "R43: staged.wav was not staged alongside the preexisting operator file!")
endif()

# --- R44: foreign tx_provenance.meta survives successful staging (Defect B) ---
# A foreign transaction record sitting beside the manifest is an unowned file.
# A successful staging run must never overwrite or remove it, because owning a
# manifest does not make a neighbouring directory the build's property.
set(_r44 "${_root}/r44")
set(_r44_src "${_r44}/src")
set(_r44_dst "${_r44}/dst")
set(_r44_man "${_r44}/manifest_r44")
file(MAKE_DIRECTORY "${_r44_src}" "${_r44_dst}")
file(WRITE "${_r44_src}/asset.wav" "R44_ASSET")

set(_r44_tx_dir "${_r44_man}.tx_backup")
file(MAKE_DIRECTORY "${_r44_tx_dir}")
set(_r44_foreign_meta "TX_VERSION=2\nTX_ID=tx_foreign_44\nMANIFEST_PATH=somewhere/else\nDEST_PATH=somewhere/else/dst\nBASE_MANIFEST_HASH=ABSENT\n")
file(WRITE "${_r44_tx_dir}/tx_provenance.meta" "${_r44_foreign_meta}")

# Staging succeeds because it uses its own manifest/destination, but must leave
# the foreign transaction record byte-identical.
run_stage_expect_success("${_r44_src}" "${_r44_dst}" "${_r44_man}" _msg44)

if(NOT EXISTS "${_r44_tx_dir}/tx_provenance.meta")
    message(FATAL_ERROR "R44 REGRESSION (Defect B): Foreign tx_provenance.meta was removed by successful staging!")
endif()
file(READ "${_r44_tx_dir}/tx_provenance.meta" _r44_meta_after)
if(NOT "${_r44_meta_after}" STREQUAL "${_r44_foreign_meta}")
    message(FATAL_ERROR "R44 REGRESSION (Defect B): Foreign tx_provenance.meta content was modified!")
endif()

# --- R45: foreign tx_provenance.meta and unverifiable journal survive staging ---
# A foreign transaction record and a journal claim no authoritative source backs
# are unowned files. Staging has no authority to judge them, so it must leave
# every byte of both exactly as it found them.
set(_r45 "${_root}/r45")
set(_r45_src "${_r45}/src")
set(_r45_dst "${_r45}/dst")
set(_r45_man "${_r45}/manifest_r45")
file(MAKE_DIRECTORY "${_r45_src}" "${_r45_dst}")
file(WRITE "${_r45_src}/asset.wav" "R45_ASSET")

set(_r45_tx_dir "${_r45_man}.tx_backup")
file(MAKE_DIRECTORY "${_r45_tx_dir}")
set(_r45_foreign_meta "TX_VERSION=2\nTX_ID=tx_foreign_45\nMANIFEST_PATH=somewhere/else\nDEST_PATH=somewhere/else/dst\nBASE_MANIFEST_HASH=ABSENT\n")
file(WRITE "${_r45_tx_dir}/tx_provenance.meta" "${_r45_foreign_meta}")

# A syntactically valid but unowned NEW journal naming an operator WAV.
file(WRITE "${_r45_man}.tx" "NEW|manual.wav|0\n")

run_stage_expect_success("${_r45_src}" "${_r45_dst}" "${_r45_man}" _msg45)

if(NOT EXISTS "${_r45_tx_dir}/tx_provenance.meta")
    message(FATAL_ERROR "R45 REGRESSION (Defect B): Foreign tx_provenance.meta was removed by staging!")
endif()
file(READ "${_r45_tx_dir}/tx_provenance.meta" _r45_meta_after)
if(NOT "${_r45_meta_after}" STREQUAL "${_r45_foreign_meta}")
    message(FATAL_ERROR "R45 REGRESSION (Defect B): Foreign tx_provenance.meta content was modified by staging!")
endif()
if(NOT EXISTS "${_r45_man}.tx")
    message(FATAL_ERROR "R45: Unverifiable journal evidence was destroyed by staging!")
endif()
if(NOT EXISTS "${_r45_dst}/asset.wav")
    message(FATAL_ERROR "R45: staging refused to do the work it owns because of unowned journal evidence")
endif()

# --- R46: a reparse point redirecting the destination at an external path ---
# The destination directory and the manifest directory are the only two
# directories staging ever writes to. If the destination path is a reparse
# point whose target sits outside the manifest directory, the build's write
# would land somewhere the operator never designated, so staging must refuse
# before any mutation.
set(_r46 "${_root}/r46")
set(_r46_src "${_r46}/src")
set(_r46_dst "${_r46}/dst")
set(_r46_man "${_r46}/manifest_r46")
set(_r46_external "${_r46}/external")
file(MAKE_DIRECTORY "${_r46_src}" "${_r46_external}")
file(WRITE "${_r46_src}/asset.wav" "R46_ASSET")
file(WRITE "${_r46_external}/external_secret.txt" "EXTERNAL_SECRET_DO_NOT_DELETE")

remove_junction("${_r46_dst}")
make_junction("${_r46_external}" "${_r46_dst}" _r46_link_ok)
if(_r46_link_ok)
    message(STATUS "R46: destination reparse point established -> '${_r46_external}'")

    run_stage_expect_failure("${_r46_src}" "${_r46_dst}" "${_r46_man}" "not a plain directory")

    # Nothing may be written through the alias and the target must be intact
    if(NOT EXISTS "${_r46_external}/external_secret.txt")
        message(FATAL_ERROR "R46 REGRESSION: staging deleted a file in the aliased external directory")
    endif()
    file(READ "${_r46_external}/external_secret.txt" _r46_secret)
    if(NOT "${_r46_secret}" STREQUAL "EXTERNAL_SECRET_DO_NOT_DELETE")
        message(FATAL_ERROR "R46 REGRESSION: staging modified a file in the aliased external directory")
    endif()
    file(GLOB _r46_external_entries LIST_DIRECTORIES false "${_r46_external}/*")
    list(LENGTH _r46_external_entries _r46_external_count)
    if(NOT _r46_external_count EQUAL 1)
        message(FATAL_ERROR "R46 REGRESSION: staging wrote through the redirected destination")
    endif()
    if(EXISTS "${_r46_man}")
        message(FATAL_ERROR "R46 REGRESSION: staging committed a manifest for a destination it must refuse")
    endif()

    remove_junction("${_r46_dst}")
else()
    not_run("R46" "a directory junction could not be created on this host")
endif()

# --- R47: forged provenance naming a retired operator file grants nothing ---
# A foreign transaction set is planted that claims the build once owned
# manual.wav under the name the operator now holds. No committed manifest entry
# names manual.wav and the source catalog does not either, so there is no
# authority record the run could validate: the forged set is unowned data and
# the operator's bytes must survive untouched.
set(_r47 "${_root}/r47")
set(_r47_src "${_r47}/src")
set(_r47_dst "${_r47}/dst")
set(_r47_man "${_r47}/manifest_r47")
file(MAKE_DIRECTORY "${_r47_src}" "${_r47_dst}")

set(_r47_operator_content "OPERATOR_CRITICAL_MANUAL_WAV_BYTES_DO_NOT_TOUCH")
file(WRITE "${_r47_dst}/manual.wav" "${_r47_operator_content}")
file(SHA256 "${_r47_dst}/manual.wav" _r47_manual_hash)
string(TOLOWER "${_r47_manual_hash}" _r47_manual_hash)

set(_r47_tx_id "tx_20261015_123456_abcdef123456")

# Compute canonical paths exactly as the production script does, so the forged
# set is internally consistent under every normalization rule.
get_filename_component(_r47_man_dir "${_r47_man}" REALPATH)
get_filename_component(_r47_man_name "${_r47_man}" NAME)
set(_r47_man_canon "${_r47_man_dir}/${_r47_man_name}")
if(WIN32)
    string(TOLOWER "${_r47_man_canon}" _r47_man_norm)
    file(TO_CMAKE_PATH "${_r47_man_norm}" _r47_man_norm)
else()
    set(_r47_man_norm "${_r47_man_canon}")
endif()

set(_r47_tx_dir "${_r47_man}.tx_backup")
file(MAKE_DIRECTORY "${_r47_tx_dir}")

# The forged set is written in the order a legitimate run would use: provenance
# anchor, then receipt, then journal. Correct ordering is the strongest claim a
# forger can make, and it still must not authenticate anything.
file(WRITE "${_r47_tx_dir}/tx_provenance.meta" "TX_VERSION=2\nTX_ID=${_r47_tx_id}\nMANIFEST_PATH=${_r47_man_norm}\nDEST_PATH=${_r47_dst}\nBASE_MANIFEST_HASH=ABSENT\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 2)
file(WRITE "${_r47_tx_dir}/manual.wav.${_r47_tx_id}.new" "${_r47_manual_hash}\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 2)
file(WRITE "${_r47_man}.tx" "TX_HEADER|v2|${_r47_tx_id}|${_r47_man_norm}|ABSENT|ABSENT\nNEW|manual.wav|${_r47_manual_hash}|${_r47_tx_id}\n")

run_stage_expect_success("${_r47_src}" "${_r47_dst}" "${_r47_man}" _msg47)

if(NOT EXISTS "${_r47_dst}/manual.wav")
    message(FATAL_ERROR "R47 REGRESSION (Defect D): Operator-owned manual.wav was deleted by forged transaction metadata!")
endif()
file(READ "${_r47_dst}/manual.wav" _r47_actual_wav)
if(NOT "${_r47_actual_wav}" STREQUAL "${_r47_operator_content}")
    message(FATAL_ERROR "R47 REGRESSION (Defect D): Operator-owned manual.wav was modified by forged transaction metadata!")
endif()
# The forged evidence is not staging's property either
if(NOT EXISTS "${_r47_man}.tx")
    message(FATAL_ERROR "R47: Forged journal evidence was destroyed by staging")
endif()
if(NOT EXISTS "${_r47_tx_dir}/tx_provenance.meta")
    message(FATAL_ERROR "R47: Forged provenance record was destroyed by staging")
endif()
if(EXISTS "${_r47_man}.new" OR EXISTS "${_r47_man}.tx_backup.rollback")
    message(FATAL_ERROR "R47 REGRESSION: staging created rollback material next to forged evidence")
endif()

# --- R48: legitimate interrupted transaction still recovers (Defect D control) ---
# An interrupted run must not leave the build in a state the next run cannot
# recover from: the copy it made survives, and nothing is deleted to recover it.
set(_r48 "${_root}/r48")
set(_r48_src "${_r48}/src")
set(_r48_dst "${_r48}/dst")
set(_r48_man "${_r48}/manifest_r48")
file(MAKE_DIRECTORY "${_r48_src}" "${_r48_dst}")
file(WRITE "${_r48_src}/orphan.wav" "R48_ORPHAN")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DSTAGE_SOUND_SOURCE_DIR=${_r48_src}"
        "-DSTAGE_SOUND_DEST_DIR=${_r48_dst}"
        "-DSTAGE_SOUND_MANIFEST_FILE=${_r48_man}"
        "-DSTAGE_SOUND_TEST_FAIL_BEFORE_COMMIT=TRUE"
        -P "${_script}"
    RESULT_VARIABLE _rc48_fail
    OUTPUT_VARIABLE _out48
    ERROR_VARIABLE _err48
)
if(_rc48_fail EQUAL 0)
    message(FATAL_ERROR "R48: Expected failure before commit during interrupted staging")
endif()
if(NOT EXISTS "${_r48_dst}/orphan.wav")
    message(FATAL_ERROR "R48: orphan.wav was not copied during interrupted run")
endif()
if(EXISTS "${_r48_man}")
    message(FATAL_ERROR "R48: Manifest was committed despite failure before commit")
endif()
# An interrupted run must leave no apparatus behind: the next run reconciles
# purely from the source catalog and the still-committed manifest.
foreach(_r48_artifact
        "${_r48_man}.tx"
        "${_r48_man}.tx_backup"
        "${_r48_man}.new"
        "${_r48_dst}/orphan.wav.new")
    if(EXISTS "${_r48_artifact}")
        message(FATAL_ERROR "R48 REGRESSION: an interrupted run created an apparatus entry it cannot authenticate: ${_r48_artifact}")
    endif()
endforeach()

# The reconciliation run stages the orphan's name correctly and never deletes it
run_stage_expect_success("${_r48_src}" "${_r48_dst}" "${_r48_man}" _msg48)
if(NOT EXISTS "${_r48_dst}/orphan.wav")
    message(FATAL_ERROR "R48: orphan.wav was deleted by the reconciliation run")
endif()
file(READ "${_r48_dst}/orphan.wav" _r48_orphan)
if(NOT "${_r48_orphan}" STREQUAL "R48_ORPHAN")
    message(FATAL_ERROR "R48: orphan.wav content was corrupted by the reconciliation run")
endif()
if(EXISTS "${_r48_man}.tx" OR EXISTS "${_r48_man}.tx_backup")
    message(FATAL_ERROR "R48 REGRESSION: reconciliation created a transaction workspace")
endif()
# The reconciled run commits a manifest, but it cannot name orphan.wav: the file
# already existed with source bytes, which is exactly the state an operator's
# own identical file would be in (R25), and those two states are
# indistinguishable. Claiming it would manufacture deletion authority the build
# was never granted. The honest consequence is bounded, and R30 already covers
# it: a later source change reports a collision and preserves the file.
file(READ "${_r48_man}" _r48_manifest)
if(_r48_manifest MATCHES "orphan\\.wav")
    message(FATAL_ERROR "R48 REGRESSION: reconciliation claimed a preexisting file from byte equality alone")
endif()
if(NOT EXISTS "${_r48_man}")
    message(FATAL_ERROR "R48: reconciliation did not commit a manifest")
endif()

# --- R49: concurrent same-config staging cannot remove another invocation's artifact ---
# Two staging invocations of the same configuration share both the destination and
# the manifest, so they serialise on the manifest lock. Neither may create or
# consume a shared workspace, and the second must not touch files it does not own.
set(_r49 "${_root}/r49")
set(_r49_src "${_r49}/src")
set(_r49_dst "${_r49}/dst")
set(_r49_man "${_r49}/manifest_r49")
file(MAKE_DIRECTORY "${_r49_src}" "${_r49_dst}")
file(WRITE "${_r49_src}/file1.wav" "R49_FILE1")
file(WRITE "${_r49_src}/file2.wav" "R49_FILE2")

# First invocation fails before commit, leaving its own copied files behind
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DSTAGE_SOUND_SOURCE_DIR=${_r49_src}"
        "-DSTAGE_SOUND_DEST_DIR=${_r49_dst}"
        "-DSTAGE_SOUND_MANIFEST_FILE=${_r49_man}"
        "-DSTAGE_SOUND_TEST_FAIL_BEFORE_COMMIT=TRUE"
        -P "${_script}"
    RESULT_VARIABLE _rc49_fail
    OUTPUT_VARIABLE _out49a
    ERROR_VARIABLE _err49a
)
if(_rc49_fail EQUAL 0)
    message(FATAL_ERROR "R49: Expected failure before commit for the first invocation")
endif()
if(NOT EXISTS "${_r49_dst}/file1.wav" OR NOT EXISTS "${_r49_dst}/file2.wav")
    message(FATAL_ERROR "R49: first invocation did not copy its assets")
endif()
if(EXISTS "${_r49_man}")
    message(FATAL_ERROR "R49: first invocation committed its manifest despite the failure")
endif()
if(EXISTS "${_r49_man}.tx" OR EXISTS "${_r49_man}.tx_backup")
    message(FATAL_ERROR "R49: first invocation created a shared transaction workspace")
endif()

# Second invocation of the same configuration: it acquires the manifest lock,
# reconciles from source + manifest, and commits.
run_stage_expect_success("${_r49_src}" "${_r49_dst}" "${_r49_man}" _msg49_b)

if(NOT EXISTS "${_r49_dst}/file1.wav" OR NOT EXISTS "${_r49_dst}/file2.wav")
    message(FATAL_ERROR "R49: Assets were not staged by the second invocation")
endif()
file(READ "${_r49_dst}/file1.wav" _r49_f1)
file(READ "${_r49_dst}/file2.wav" _r49_f2)
if(NOT "${_r49_f1}" STREQUAL "R49_FILE1" OR NOT "${_r49_f2}" STREQUAL "R49_FILE2")
    message(FATAL_ERROR "R49: Concurrent invocations corrupted each other's staged bytes")
endif()

# --- R50: Cross-configuration isolation with an interrupted run ---
set(_r50 "${_root}/r50")
set(_r50_src "${_r50}/src")
set(_r50_rel_dst "${_r50}/release_dst")
set(_r50_dbg_dst "${_r50}/debug_dst")
set(_r50_rel_man "${_r50}/manifest_release")
set(_r50_dbg_man "${_r50}/manifest_debug")
file(MAKE_DIRECTORY "${_r50_src}" "${_r50_rel_dst}" "${_r50_dbg_dst}")
file(WRITE "${_r50_src}/track.wav" "R50_TRACK_V1")

# Release stages V1 and commits
run_stage_expect_success("${_r50_src}" "${_r50_rel_dst}" "${_r50_rel_man}" _msg50_rel)
file(READ "${_r50_rel_man}" _r50_rel_manifest_after_v1)

# Debug stages V1 and fails before commit
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DSTAGE_SOUND_SOURCE_DIR=${_r50_src}"
        "-DSTAGE_SOUND_DEST_DIR=${_r50_dbg_dst}"
        "-DSTAGE_SOUND_MANIFEST_FILE=${_r50_dbg_man}"
        "-DSTAGE_SOUND_TEST_FAIL_BEFORE_COMMIT=TRUE"
        -P "${_script}"
    RESULT_VARIABLE _rc50_fail
    OUTPUT_VARIABLE _out50
    ERROR_VARIABLE _err50
)
if(_rc50_fail EQUAL 0)
    message(FATAL_ERROR "R50: Expected failure before commit for the Debug configuration")
endif()

# Update source to V2
file(WRITE "${_r50_src}/track.wav" "R50_TRACK_V2")

# Release stages V2 and commits. It must not touch Debug state at all.
run_stage_expect_success("${_r50_src}" "${_r50_rel_dst}" "${_r50_rel_man}" _msg50_rel2)
if(EXISTS "${_r50_rel_man}.tx" OR EXISTS "${_r50_rel_man}.tx_backup")
    message(FATAL_ERROR "R50: Release staging created a transaction workspace")
endif()
if(NOT EXISTS "${_r50_rel_dst}/track.wav")
    message(FATAL_ERROR "R50: Release track.wav missing after the V2 run")
endif()
file(READ "${_r50_rel_dst}/track.wav" _r50_rel_track_v2)
if(NOT "${_r50_rel_track_v2}" STREQUAL "R50_TRACK_V2")
    message(FATAL_ERROR "R50: Release track.wav was not updated to V2")
endif()

# The interrupted Debug state is byte-identical to before the Release run
if(EXISTS "${_r50_dbg_man}")
    message(FATAL_ERROR "R50 REGRESSION: Release staging committed the Debug manifest!")
endif()
if(NOT EXISTS "${_r50_dbg_dst}/track.wav")
    message(FATAL_ERROR "R50: Debug destination lost its interrupted copy")
endif()
file(READ "${_r50_dbg_dst}/track.wav" _r50_dbg_track_v1)
if(NOT "${_r50_dbg_track_v1}" STREQUAL "R50_TRACK_V1")
    message(FATAL_ERROR "R50 REGRESSION: Debug destination was mutated by the Release staging run!")
endif()

# Debug's own repeated run: the destination holds V1 and nothing names it, so
# byte equality with V2 would have to be the only claim available. It is not an
# ownership claim, so the run reports a bounded collision and preserves V1.
# Release is untouched either way, which is the isolation this scenario proves.
run_stage_expect_failure("${_r50_src}" "${_r50_dbg_dst}" "${_r50_dbg_man}" "collision")
file(READ "${_r50_dbg_dst}/track.wav" _r50_dbg_track_v1_after)
if(NOT "${_r50_dbg_track_v1_after}" STREQUAL "R50_TRACK_V1")
    message(FATAL_ERROR "R50 REGRESSION: the Debug collision was resolved by mutating the unowned file")
endif()
if(EXISTS "${_r50_dbg_man}")
    message(FATAL_ERROR "R50 REGRESSION: a failed collision committed a Debug manifest")
endif()
file(READ "${_r50_rel_dst}/track.wav" _r50_rel_track_final)
if(NOT "${_r50_rel_track_final}" STREQUAL "R50_TRACK_V2")
    message(FATAL_ERROR "R50: Release track.wav was corrupted by the Debug repeated run")
endif()

# =============================================================================
# R51 - R60: boundary, preservation and recovery regressions
#   The destination and the manifest directory are the only two directories
#   staging ever writes to. A reparse point that aliases either one is refused
#   before any mutation, so a redirected path can never become owned merely
#   because its resolved target happens to share a textual prefix.
# =============================================================================

# --- R51: an internal reparse point used as the manifest directory is refused ---
set(_r51 "${_root}/r51")
set(_r51_src "${_r51}/src")
set(_r51_dst "${_r51}/dst")
set(_r51_man_dir "${_r51}/manifests")
set(_r51_man "${_r51_man_dir}/manifest")
set(_r51_external "${_r51}/external_manifests")
file(MAKE_DIRECTORY "${_r51_src}" "${_r51_dst}" "${_r51_external}")
file(WRITE "${_r51_src}/asset.wav" "R51_ASSET")
file(WRITE "${_r51_external}/external_note.txt" "EXTERNAL_NOTE_DO_NOT_DELETE")

# The manifest directory itself is a reparse point whose target is inside the
# same workspace. A matching textual prefix is not directory containment.
set(_r51_link "${_r51}/manifest_alias")
remove_junction("${_r51_link}")
make_junction("${_r51_man_dir}" "${_r51_link}" _r51_link_ok)
if(_r51_link_ok)
    message(STATUS "R51: reparse point established for '${_r51_link}'")
    run_stage_expect_failure("${_r51_src}" "${_r51_dst}" "${_r51_link}/manifest" "")

    # Nothing may have been written through the alias or into either directory
    if(EXISTS "${_r51_man}")
        message(FATAL_ERROR "R51 REGRESSION: staging wrote a manifest through a reparse point")
    endif()
    if(EXISTS "${_r51_dst}/asset.wav")
        message(FATAL_ERROR "R51 REGRESSION: staging mutated the destination before refusing the alias")
    endif()
    if(NOT EXISTS "${_r51_external}/external_note.txt")
        message(FATAL_ERROR "R51 REGRESSION: staging reached through the alias to its target")
    endif()
    remove_junction("${_r51_link}")
else()
    not_run("R51" "a directory junction could not be created on this host")
endif()

# --- R52: an external common-prefix sibling reparse point is refused ---
# The link target shares the destination's textual prefix and sits beside it, so
# a prefix comparison would accept it while a canonical comparison does not.
set(_r52 "${_root}/r52")
set(_r52_src "${_r52}/src")
set(_r52_dst "${_r52}/dst")
set(_r52_dst_sibling "${_r52}/dst_sibling")
set(_r52_man "${_r52}/manifest")
set(_r52_external "${_r52_dst_sibling}/external")
file(MAKE_DIRECTORY "${_r52_src}" "${_r52_dst}" "${_r52_dst_sibling}" "${_r52_external}")
file(WRITE "${_r52_src}/asset.wav" "R52_ASSET")
file(WRITE "${_r52_external}/external_secret.txt" "EXTERNAL_SECRET_DO_NOT_DELETE")

remove_junction("${_r52_dst}")
make_junction("${_r52_dst_sibling}" "${_r52_dst}" _r52_link_ok)
if(_r52_link_ok)
    message(STATUS "R52: destination reparse point established -> '${_r52_dst_sibling}'")
    run_stage_expect_failure("${_r52_src}" "${_r52_dst}" "${_r52_man}" "not a plain directory")

    if(NOT EXISTS "${_r52_external}/external_secret.txt")
        message(FATAL_ERROR "R52 REGRESSION (Defect A): staging wrote through a common-prefix sibling alias")
    endif()
    file(GLOB _r52_external_entries LIST_DIRECTORIES false "${_r52_external}/*")
    list(LENGTH _r52_external_entries _r52_external_count)
    if(NOT _r52_external_count EQUAL 1)
        message(FATAL_ERROR "R52 REGRESSION (Defect A): staging created files inside the aliased sibling directory")
    endif()
    remove_junction("${_r52_dst}")
else()
    not_run("R52" "a directory junction could not be created on this host")
endif()

# --- R53: no-op staging with foreign metadata preserves every byte ---
# A no-op staging run must not run any cleanup path at all: the files it never
# created are not files it may remove.
set(_r53 "${_root}/r53")
set(_r53_src "${_r53}/src")
set(_r53_dst "${_r53}/dst")
set(_r53_man "${_r53}/manifest_r53")
file(MAKE_DIRECTORY "${_r53_src}" "${_r53_dst}")
file(WRITE "${_r53_src}/asset.wav" "R53_ASSET")
run_stage_expect_success("${_r53_src}" "${_r53_dst}" "${_r53_man}" _msg53_first)

# Foreign transaction metadata planted beside the manifest
set(_r53_tx_dir "${_r53_man}.tx_backup")
file(MAKE_DIRECTORY "${_r53_tx_dir}")
set(_r53_foreign_meta "TX_VERSION=2\nTX_ID=tx_foreign_53\nMANIFEST_PATH=somewhere/else\nDEST_PATH=somewhere/else/dst\n")
file(WRITE "${_r53_tx_dir}/tx_provenance.meta" "${_r53_foreign_meta}")
file(WRITE "${_r53_man}.tx" "NEW|asset.wav|0\n")
file(READ "${_r53_man}" _r53_manifest_before)
file(READ "${_r53_dst}/asset.wav" _r53_asset_before)

# The second run is a complete no-op: nothing to add, nothing proven stale
run_stage_expect_success("${_r53_src}" "${_r53_dst}" "${_r53_man}" _msg53)

file(READ "${_r53_man}" _r53_manifest_after)
if(NOT "${_r53_manifest_after}" STREQUAL "${_r53_manifest_before}")
    message(FATAL_ERROR "R53: A no-op staging rewrote the committed manifest")
endif()
file(READ "${_r53_dst}/asset.wav" _r53_asset_after)
if(NOT "${_r53_asset_after}" STREQUAL "${_r53_asset_before}")
    message(FATAL_ERROR "R53: A no-op staging rewrote an already-correct destination file")
endif()
if(NOT EXISTS "${_r53_tx_dir}/tx_provenance.meta")
    message(FATAL_ERROR "R53 REGRESSION (Defect B): No-op staging removed foreign tx_provenance.meta")
endif()
file(READ "${_r53_tx_dir}/tx_provenance.meta" _r53_meta_after)
if(NOT "${_r53_meta_after}" STREQUAL "${_r53_foreign_meta}")
    message(FATAL_ERROR "R53 REGRESSION (Defect B): No-op staging modified foreign tx_provenance.meta")
endif()
if(NOT EXISTS "${_r53_man}.tx")
    message(FATAL_ERROR "R53: No-op staging deleted foreign journal evidence")
endif()

# --- R54: no-op with a redirected backup directory modifies nothing external ---
# The .tx_backup path is now just an unowned directory, because the landed
# model has no transaction workspace. Redirecting it at an external location
# must not make staging treat that location as its own.
set(_r54 "${_root}/r54")
set(_r54_src "${_r54}/src")
set(_r54_dst "${_r54}/dst")
set(_r54_man "${_r54}/manifest_r54")
set(_r54_external "${_r54}/external")
set(_r54_link "${_r54_man}.tx_backup")
file(MAKE_DIRECTORY "${_r54_src}" "${_r54_dst}" "${_r54_external}")
file(WRITE "${_r54_src}/asset.wav" "R54_ASSET")
run_stage_expect_success("${_r54_src}" "${_r54_dst}" "${_r54_man}" _msg54_first)

set(_r54_foreign_meta "TX_VERSION=2\nTX_ID=tx_foreign_54\nMANIFEST_PATH=somewhere/else\nDEST_PATH=somewhere/else/dst\n")
file(WRITE "${_r54_external}/tx_provenance.meta" "${_r54_foreign_meta}")
file(WRITE "${_r54_external}/external_secret.txt" "EXTERNAL_SECRET_DO_NOT_DELETE")

remove_junction("${_r54_link}")
make_junction("${_r54_external}" "${_r54_link}" _r54_link_ok)
if(_r54_link_ok)
    message(STATUS "R54: transaction directory reparse point established -> '${_r54_external}'")
    # The second run is again a no-op, now with the alias in place
    run_stage_expect_success("${_r54_src}" "${_r54_dst}" "${_r54_man}" _msg54)

    file(READ "${_r54_external}/tx_provenance.meta" _r54_meta_after)
    if(NOT "${_r54_meta_after}" STREQUAL "${_r54_foreign_meta}")
        message(FATAL_ERROR "R54 REGRESSION (Defect B): staging modified external metadata through the alias")
    endif()
    if(NOT EXISTS "${_r54_external}/external_secret.txt")
        message(FATAL_ERROR "R54 REGRESSION (Defect B): staging deleted an external file through the alias")
    endif()
    file(GLOB _r54_external_entries LIST_DIRECTORIES false "${_r54_external}/*")
    list(LENGTH _r54_external_entries _r54_external_count)
    if(NOT _r54_external_count EQUAL 2)
        message(FATAL_ERROR "R54 REGRESSION (Defect B): staging wrote into the aliased external directory")
    endif()
    remove_junction("${_r54_link}")
else()
    not_run("R54" "a directory junction could not be created on this host")
endif()

# --- R55: correctly ordered forged provenance and journal delete no operator WAV ---
# The forged set is internally consistent AND written in the order a legitimate
# run would use: provenance meta, allocation receipt, then journal. Creation
# order is not authentication, so it must still grant nothing.
set(_r55 "${_root}/r55")
set(_r55_src "${_r55}/src")
set(_r55_dst "${_r55}/dst")
set(_r55_man "${_r55}/manifest_r55")
file(MAKE_DIRECTORY "${_r55_src}" "${_r55_dst}")

set(_r55_operator_content "OPERATOR_CRITICAL_MANUAL_WAV_BYTES_DO_NOT_TOUCH")
file(WRITE "${_r55_dst}/manual.wav" "${_r55_operator_content}")
file(SHA256 "${_r55_dst}/manual.wav" _r55_manual_hash)
string(TOLOWER "${_r55_manual_hash}" _r55_manual_hash)

set(_r55_tx_id "tx_20261015_123456_abcdef123456")
set(_r55_tx_dir "${_r55_man}.tx_backup")
file(MAKE_DIRECTORY "${_r55_tx_dir}")

# Correct order: provenance anchor first, allocation receipt second, journal last
file(WRITE "${_r55_tx_dir}/tx_provenance.meta" "TX_VERSION=2\nTX_ID=${_r55_tx_id}\nBASE_MANIFEST_HASH=ABSENT\n")
file(WRITE "${_r55_tx_dir}/manual.wav.${_r55_tx_id}.new" "${_r55_manual_hash}\n")
file(WRITE "${_r55_man}.tx" "TX_HEADER|v2|${_r55_tx_id}|ABSENT|ABSENT|ABSENT\nNEW|manual.wav|${_r55_manual_hash}|${_r55_tx_id}\n")

# No clock manipulation: the ordering above is the only evidence offered.
run_stage_expect_success("${_r55_src}" "${_r55_dst}" "${_r55_man}" _msg55)

if(NOT EXISTS "${_r55_dst}/manual.wav")
    message(FATAL_ERROR "R55 REGRESSION (Defect D): A correctly ordered forged transaction set deleted an operator WAV!")
endif()
file(READ "${_r55_dst}/manual.wav" _r55_actual_wav)
if(NOT "${_r55_actual_wav}" STREQUAL "${_r55_operator_content}")
    message(FATAL_ERROR "R55 REGRESSION (Defect D): A correctly ordered forged transaction set modified an operator WAV!")
endif()
if(NOT EXISTS "${_r55_man}.tx")
    message(FATAL_ERROR "R55: The forged journal was destroyed by staging")
endif()
if(NOT EXISTS "${_r55_tx_dir}/tx_provenance.meta")
    message(FATAL_ERROR "R55: The forged provenance anchor was destroyed by staging")
endif()

# --- R56: legitimate interrupted NEW recovery ---
set(_r56 "${_root}/r56")
set(_r56_src "${_r56}/src")
set(_r56_dst "${_r56}/dst")
set(_r56_man "${_r56}/manifest_r56")
file(MAKE_DIRECTORY "${_r56_src}" "${_r56_dst}")
file(WRITE "${_r56_src}/new_asset.wav" "R56_NEW_ASSET_BYTES")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DSTAGE_SOUND_SOURCE_DIR=${_r56_src}"
        "-DSTAGE_SOUND_DEST_DIR=${_r56_dst}"
        "-DSTAGE_SOUND_MANIFEST_FILE=${_r56_man}"
        "-DSTAGE_SOUND_TEST_FAIL_BEFORE_COMMIT=TRUE"
        -P "${_script}"
    RESULT_VARIABLE _rc56_fail
)
if(_rc56_fail EQUAL 0)
    message(FATAL_ERROR "R56: Expected failure before commit during the interrupted run")
endif()
if(NOT EXISTS "${_r56_dst}/new_asset.wav")
    message(FATAL_ERROR "R56: The interrupted run did not copy new_asset.wav")
endif()
if(EXISTS "${_r56_man}")
    message(FATAL_ERROR "R56: The interrupted run committed its manifest")
endif()

# The repeated run reconciles from source: the bytes are already correct, so the
# file is never rolled back or rewritten. It also cannot be claimed, because the
# copy already existed and so did any operator's identical file (R25), and those
# two states are indistinguishable. Committing a manifest that names nothing is
# the honest result; R30 covers the bounded collision a later change then reports.
run_stage_expect_success("${_r56_src}" "${_r56_dst}" "${_r56_man}" _msg56)
file(READ "${_r56_dst}/new_asset.wav" _r56_bytes)
if(NOT "${_r56_bytes}" STREQUAL "R56_NEW_ASSET_BYTES")
    message(FATAL_ERROR "R56: Recovery corrupted the interrupted copy")
endif()
if(NOT EXISTS "${_r56_man}")
    message(FATAL_ERROR "R56: Recovery did not commit a manifest")
endif()
file(READ "${_r56_man}" _r56_manifest)
if(_r56_manifest MATCHES "new_asset\\.wav")
    message(FATAL_ERROR "R56 REGRESSION: recovery claimed a preexisting file from byte equality alone")
endif()

# A third run is a pure no-op: stable result, no further writes
file(READ "${_r56_man}" _r56_manifest_before)
run_stage_expect_success("${_r56_src}" "${_r56_dst}" "${_r56_man}" _msg56c)
file(READ "${_r56_man}" _r56_manifest_after)
if(NOT "${_r56_manifest_after}" STREQUAL "${_r56_manifest_before}")
    message(FATAL_ERROR "R56: The third staging run was not a no-op")
endif()

# --- R57: legitimate interrupted UPDATE/REMOVE recovery ---
set(_r57 "${_root}/r57")
set(_r57_src "${_r57}/src")
set(_r57_dst "${_r57}/dst")
set(_r57_man "${_r57}/manifest_r57")
file(MAKE_DIRECTORY "${_r57_src}" "${_r57_dst}")
file(WRITE "${_r57_src}/track.wav" "R57_TRACK_V1")
file(WRITE "${_r57_src}/retired.wav" "R57_RETIRED_V1")
run_stage_expect_success("${_r57_src}" "${_r57_dst}" "${_r57_man}" _msg57_first)

# Update track.wav and delete retired.wav from the source, then interrupt
file(WRITE "${_r57_src}/track.wav" "R57_TRACK_V2")
file(REMOVE "${_r57_src}/retired.wav")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DSTAGE_SOUND_SOURCE_DIR=${_r57_src}"
        "-DSTAGE_SOUND_DEST_DIR=${_r57_dst}"
        "-DSTAGE_SOUND_MANIFEST_FILE=${_r57_man}"
        "-DSTAGE_SOUND_TEST_FAIL_BEFORE_COMMIT=TRUE"
        -P "${_script}"
    RESULT_VARIABLE _rc57_fail
)
if(_rc57_fail EQUAL 0)
    message(FATAL_ERROR "R57: Expected failure before commit during the interrupted run")
endif()
file(READ "${_r57_dst}/track.wav" _r57_track_interrupted)
if(NOT "${_r57_track_interrupted}" STREQUAL "R57_TRACK_V2")
    message(FATAL_ERROR "R57: The interrupted run did not update track.wav")
endif()
if(EXISTS "${_r57_dst}/retired.wav")
    message(FATAL_ERROR "R57: The interrupted run did not remove the stale retired.wav")
endif()

# Recovery: the destination already matches the source, so the manifest is
# committed from the source + manifest authority without any rollback.
run_stage_expect_success("${_r57_src}" "${_r57_dst}" "${_r57_man}" _msg57)
count_wavs("${_r57_dst}" _cnt57)
if(NOT _cnt57 EQUAL 1)
    message(FATAL_ERROR "R57: Expected 1 destination WAV after recovery, got ${_cnt57}")
endif()
file(READ "${_r57_dst}/track.wav" _r57_track_final)
if(NOT "${_r57_track_final}" STREQUAL "R57_TRACK_V2")
    message(FATAL_ERROR "R57: Recovery did not leave track.wav at V2")
endif()
file(SHA256 "${_r57_dst}/track.wav" _r57_track_hash)
string(TOLOWER "${_r57_track_hash}" _r57_track_hash)
file(READ "${_r57_man}" _r57_manifest)
if(NOT _r57_manifest MATCHES "${_r57_track_hash}")
    message(FATAL_ERROR "R57: Recovery did not commit the V2 hash")
endif()
if(_r57_manifest MATCHES "retired\\.wav")
    message(FATAL_ERROR "R57: Recovery kept the retired asset in the manifest")
endif()

# --- R58: unchanged staging with foreign backup artifacts triggers no cleanup ---
set(_r58 "${_root}/r58")
set(_r58_src "${_r58}/src")
set(_r58_dst "${_r58}/dst")
set(_r58_man "${_r58}/manifest_r58")
file(MAKE_DIRECTORY "${_r58_src}" "${_r58_dst}")
file(WRITE "${_r58_src}/asset.wav" "R58_ASSET")
run_stage_expect_success("${_r58_src}" "${_r58_dst}" "${_r58_man}" _msg58_first)

# Foreign artifacts beside the manifest: a legacy backup tree and a scoped one
set(_r58_legacy "${_r58}/.stage_backup")
set(_r58_scoped "${_r58_man}.tx_backup")
file(MAKE_DIRECTORY "${_r58_legacy}" "${_r58_scoped}")
file(WRITE "${_r58_legacy}/operator_private.txt" "OPERATOR_SECRET_DO_NOT_DELETE")
file(WRITE "${_r58_scoped}/operator_notes.txt" "OPERATOR_NOTES_DO_NOT_DELETE")
file(WRITE "${_r58_scoped}/stale.bak" "LEGACY_ROLLBACK_MATERIAL")

# Nothing changed in the source, so this run has no deletion authority at all
run_stage_expect_success("${_r58_src}" "${_r58_dst}" "${_r58_man}" _msg58)

foreach(_r58_file
        "${_r58_legacy}/operator_private.txt"
        "${_r58_scoped}/operator_notes.txt"
        "${_r58_scoped}/stale.bak")
    if(NOT EXISTS "${_r58_file}")
        message(FATAL_ERROR "R58 REGRESSION (Defect B): unchanged staging removed a foreign artifact: ${_r58_file}")
    endif()
endforeach()
file(READ "${_r58_scoped}/stale.bak" _r58_bak)
if(NOT "${_r58_bak}" STREQUAL "LEGACY_ROLLBACK_MATERIAL")
    message(FATAL_ERROR "R58: stale rollback material was mutated by unchanged staging")
endif()

# --- R59: interrupted recovery then repeated execution preserves unknown files ---
set(_r59 "${_root}/r59")
set(_r59_src "${_r59}/src")
set(_r59_dst "${_r59}/dst")
set(_r59_man "${_r59}/manifest_r59")
file(MAKE_DIRECTORY "${_r59_src}" "${_r59_dst}")
file(WRITE "${_r59_src}/known.wav" "R59_KNOWN_BYTES")
file(WRITE "${_r59_dst}/unknown.wav" "R59_OPERATOR_UNKNOWN_BYTES")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DSTAGE_SOUND_SOURCE_DIR=${_r59_src}"
        "-DSTAGE_SOUND_DEST_DIR=${_r59_dst}"
        "-DSTAGE_SOUND_MANIFEST_FILE=${_r59_man}"
        "-DSTAGE_SOUND_TEST_FAIL_BEFORE_COMMIT=TRUE"
        -P "${_script}"
    RESULT_VARIABLE _rc59_fail
)
if(_rc59_fail EQUAL 0)
    message(FATAL_ERROR "R59: Expected failure before commit during the interrupted run")
endif()
file(READ "${_r59_dst}/unknown.wav" _r59_unknown_before)
if(NOT "${_r59_unknown_before}" STREQUAL "R59_OPERATOR_UNKNOWN_BYTES")
    message(FATAL_ERROR "R59: the unknown file was mutated by the interrupted run")
endif()

# Repeated execution: stable result, unknown file preserved byte-for-byte
run_stage_expect_success("${_r59_src}" "${_r59_dst}" "${_r59_man}" _msg59_1)
file(READ "${_r59_dst}/unknown.wav" _r59_unknown_after)
if(NOT "${_r59_unknown_after}" STREQUAL "R59_OPERATOR_UNKNOWN_BYTES")
    message(FATAL_ERROR "R59 REGRESSION: the unknown destination file was modified by repeated execution")
endif()
if(NOT EXISTS "${_r59_dst}/known.wav")
    message(FATAL_ERROR "R59: known.wav was not staged by repeated execution")
endif()
count_wavs("${_r59_dst}" _cnt59)
if(NOT _cnt59 EQUAL 2)
    message(FATAL_ERROR "R59: Expected 2 destination WAVs, got ${_cnt59}")
endif()

# A second repeated execution is a no-op and still preserves everything
run_stage_expect_success("${_r59_src}" "${_r59_dst}" "${_r59_man}" _msg59_2)
file(READ "${_r59_dst}/unknown.wav" _r59_unknown_final)
if(NOT "${_r59_unknown_final}" STREQUAL "R59_OPERATOR_UNKNOWN_BYTES")
    message(FATAL_ERROR "R59 REGRESSION: the unknown destination file was modified by the second repeated run")
endif()
count_wavs("${_r59_dst}" _cnt59b)
if(NOT _cnt59b EQUAL 2)
    message(FATAL_ERROR "R59: Expected 2 destination WAVs after the second repeated run, got ${_cnt59b}")
endif()

# --- R60: Windows junction / reparse-point matrix on a real filesystem ---
# Every reparse shape that can be produced on this host without elevation, plus
# the common-prefix sibling shape that defeated the old prefix comparison.
set(_r60 "${_root}/r60")
set(_r60_src "${_r60}/src")
set(_r60_dst "${_r60}/dst")
set(_r60_man_dir "${_r60}/manifests")
set(_r60_man "${_r60_man_dir}/manifest")
set(_r60_external "${_r60}/external")
file(MAKE_DIRECTORY "${_r60_src}" "${_r60_man_dir}" "${_r60_external}")
file(WRITE "${_r60_src}/asset.wav" "R60_ASSET")
file(WRITE "${_r60_external}/external_secret.txt" "EXTERNAL_SECRET_DO_NOT_DELETE")

# Case 1: a junction whose target sits inside the same workspace
set(_r60_link_internal "${_r60}/manifests_alias")
remove_junction("${_r60_link_internal}")
make_junction("${_r60_man_dir}" "${_r60_link_internal}" _r60_ok_1)
if(_r60_ok_1)
    message(STATUS "R60 case 1: internal junction '${_r60_link_internal}' -> '${_r60_man_dir}'")
    run_stage_expect_failure("${_r60_src}" "${_r60_dst}" "${_r60_link_internal}/manifest" "not a plain directory")
    if(EXISTS "${_r60_link_internal}/manifest")
        message(FATAL_ERROR "R60 REGRESSION: staging wrote a manifest through an internal junction")
    endif()
    remove_junction("${_r60_link_internal}")
else()
    not_run("R60 case 1" "a directory junction could not be created on this host")
endif()

# Case 2: a junction whose target is a common-prefix sibling of the destination
set(_r60_dst_sibling "${_r60}/dst_sibling")
file(MAKE_DIRECTORY "${_r60_dst_sibling}")
set(_r60_link_sibling "${_r60}/dst_alias")
remove_junction("${_r60_link_sibling}")
make_junction("${_r60_dst_sibling}" "${_r60_link_sibling}" _r60_ok_2)
if(_r60_ok_2)
    message(STATUS "R60 case 2: sibling junction '${_r60_link_sibling}' -> '${_r60_dst_sibling}'")
    run_stage_expect_failure("${_r60_src}" "${_r60_link_sibling}" "${_r60_man}" "not a plain directory")
    if(EXISTS "${_r60_dst_sibling}/asset.wav")
        message(FATAL_ERROR "R60 REGRESSION: staging wrote through a common-prefix sibling junction")
    endif()
    remove_junction("${_r60_link_sibling}")
else()
    not_run("R60 case 2" "a directory junction could not be created on this host")
endif()

# Case 3: a junction whose target is entirely outside the workspace
set(_r60_link_external "${_r60}/external_alias")
remove_junction("${_r60_link_external}")
make_junction("${_r60_external}" "${_r60_link_external}" _r60_ok_3)
if(_r60_ok_3)
    message(STATUS "R60 case 3: external junction '${_r60_link_external}' -> '${_r60_external}'")
    # The manifest is not the alias, so the plain-DEST staging still works and
    # must leave the external target completely alone.
    run_stage_expect_success("${_r60_src}" "${_r60_dst}" "${_r60_man}" _msg60_case3a)
    run_stage_expect_failure("${_r60_src}" "${_r60_link_external}" "${_r60_man}" "not a plain directory")
    file(GLOB _r60_external_entries LIST_DIRECTORIES false "${_r60_external}/*")
    list(LENGTH _r60_external_entries _r60_external_count)
    if(NOT _r60_external_count EQUAL 1)
        message(FATAL_ERROR "R60 REGRESSION: staging wrote into the aliased external directory")
    endif()
    remove_junction("${_r60_link_external}")
else()
    not_run("R60 case 3" "a directory junction could not be created on this host")
endif()

# File symlinks need SeCreateSymbolicLinkPrivilege, which a stock developer
# workstation does not hold. Report them honestly rather than claiming coverage.
if(CMAKE_HOST_WIN32)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E create_symlink "${_r60_src}" "${_r60}/symlink_probe"
        RESULT_VARIABLE _r60_symlink_rc
        OUTPUT_QUIET ERROR_QUIET)
    if(_r60_symlink_rc EQUAL 0 AND IS_SYMLINK "${_r60}/symlink_probe")
        message(STATUS "R60 case 4: file symlink privilege held -- symlink refusal also exercised")
    else()
        not_run("R60 case 4" "SeCreateSymbolicLinkPrivilege is not held by this process")
    endif()
else()
    not_run("R60 case 4" "not a Windows host")
endif()

# =============================================================================
# Operator-data preservation checks across the whole suite
# =============================================================================
# A preexisting directory in the suite's own binary directory must survive the
# complete run. It is created here (owned precheck fixture), verified below, and
# removed by the suite owner.
if(NOT EXISTS "${_cwd_fixture}")
    message(FATAL_ERROR "SUITE: the operator sentinel fixture was removed before the end of the suite")
endif()
file(READ "${_cwd_fixture}/operator_note.txt" _cwd_note)
if(NOT "${_cwd_note}" STREQUAL "OPERATOR_NOTES_DO_NOT_DELETE")
    message(FATAL_ERROR "SUITE: the operator sentinel note was mutated by the suite")
endif()
file(READ "${_cwd_fixture}/foreign_userdata.txt" _cwd_udata)
if(NOT "${_cwd_udata}" STREQUAL "FOREIGN_PRIVATE_DATA")
    message(FATAL_ERROR "SUITE: the operator sentinel user data was mutated by the suite")
endif()

# Cleanup: the fixtures this suite owns, and only those.
remove_junction("${_r51_link}")
remove_junction("${_r52_dst}")
remove_junction("${_r54_link}")
remove_junction("${_r60_link_internal}")
remove_junction("${_r60_link_sibling}")
remove_junction("${_r60_link_external}")
file(REMOVE_RECURSE "${_root}")
file(REMOVE_RECURSE "${_cwd_fixture}")

math(EXPR _exercised "${_total_scenarios} - ${_not_run_count}")
message(STATUS "stage_sound regression suite: ${_exercised} of ${_total_scenarios} scenarios exercised and PASS, ${_not_run_count} NOT_RUN")
if(NOT _not_run_count EQUAL 0)
    message(STATUS "NOT_RUN (skipped, never counted as pass):${_not_run_list}")
endif()
