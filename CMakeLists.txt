cmake_minimum_required(VERSION 3.24)

# The root RELEASE_VERSION file is the ONE release-version authority. project(), the
# Windows VERSIONINFO resource and the packaging pipeline all derive from it,
# so a release can never ship binary metadata that disagrees with its tag.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/RELEASE_VERSION" PROTRAIL_VERSION LIMIT_COUNT 1)
string(STRIP "${PROTRAIL_VERSION}" PROTRAIL_VERSION)
if(NOT PROTRAIL_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR
        "RELEASE_VERSION_INVALID: RELEASE_VERSION must hold MAJOR.MINOR.PATCH, got '${PROTRAIL_VERSION}'")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/RELEASE_VERSION")

project(ProTrail
    VERSION ${PROTRAIL_VERSION}
    DESCRIPTION "Native Windows cursor effects application"
    LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# T-010R Repair 4: checkbox tick mark ships as an embedded Qt resource.
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
    # Warnings as errors, UTF-8 source, conformant preprocessor.
    add_compile_options(/W4 /WX /EHsc /utf-8 /Zc:preprocessor /permissive- /FS)
    # Reproducible PE output: no link timestamp or other per-build noise, so
    # a rebuild from the release tag yields byte-identical binaries.
    add_compile_options(/Brepro)
    add_link_options(/Brepro)
endif()

# T-34: Developer Release Defaults panel in dev builds only
option(PROTRAIL_DEV_BUILD "Enable developer tools and defaults authoring panel" OFF)
if(PROTRAIL_DEV_BUILD)
    add_compile_definitions(PROTRAIL_DEV_BUILD=1)
else()
    add_compile_definitions($<$<CONFIG:Debug>:PROTRAIL_DEV_BUILD=1>)
endif()
# The checkout path is only meaningful to the developer defaults panel. It is
# defined for developer builds alone, so a production Release binary never
# embeds the machine-specific source path.
if(PROTRAIL_DEV_BUILD)
    add_compile_definitions(PROTRAIL_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
else()
    add_compile_definitions(
        $<$<CONFIG:Debug>:PROTRAIL_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}">)
endif()

# ---- Windows product resource (VERSIONINFO + application icon) ----
# The approved product icon has exactly one home. Its presence is a build
# input; its absence keeps developer builds working with the generated
# fallback, while the official release pipeline configures with
# PROTRAIL_REQUIRE_FINAL_ICON=ON so a binary without final branding cannot
# be produced by accident.
option(PROTRAIL_REQUIRE_FINAL_ICON
    "Fail configure unless the approved product icon is present" OFF)
set(PROTRAIL_ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/branding/protrail.ico")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/windows/protrail.rc.in")
if(EXISTS "${PROTRAIL_ICON_FILE}")
    set(PROTRAIL_HAS_FINAL_ICON 1)
    file(TO_CMAKE_PATH "${PROTRAIL_ICON_FILE}" _protrail_icon_rc_path)
    set(PROTRAIL_RC_ICON_LINE "IDI_ICON1 ICON \"${_protrail_icon_rc_path}\"")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${PROTRAIL_ICON_FILE}")
else()
    set(PROTRAIL_HAS_FINAL_ICON 0)
    set(PROTRAIL_RC_ICON_LINE "// USER_PRODUCT_ICON_PENDING: resources/branding/protrail.ico absent")
    if(PROTRAIL_REQUIRE_FINAL_ICON)
        message(FATAL_ERROR
            "USER_PRODUCT_ICON_PENDING: ${PROTRAIL_ICON_FILE} is required for an official binary release")
    endif()
endif()
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/windows/protrail.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/protrail.rc"
    @ONLY)
set(PROTRAIL_WINDOWS_RC "${CMAKE_CURRENT_BINARY_DIR}/protrail.rc")

# ---- Qt ----
# Developer convenience only: a local Qt install is a valid prefix when it
# exists. CI supplies its own Qt (install-qt-action exports Qt6_DIR), and an
# absent hardcoded path must never shadow the environment's real one.
if(EXISTS "C:/Qt/6.8.0/msvc2022_64")
    list(APPEND CMAKE_PREFIX_PATH "C:/Qt/6.8.0/msvc2022_64")
endif()
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Test)
qt_standard_project_setup()

# ---- T-33 canonical Release Defaults build gate ----
# The ONE semantic default configuration is resources/release_defaults.json.
# A missing or malformed source must fail the build visibly: silently shipping
# unknown fallback values is the exact failure this gate removes. The same
# script is wired into CTest with a deliberately broken file, so the gate
# itself is proven rather than asserted.
set(PROTRAIL_RELEASE_DEFAULTS
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/release_defaults.json")
# AUTORCC embeds the canonical source; the .qrc sits beside the JSON so its
# relative <file> entry resolves without a path hack.
set(PROTRAIL_RELEASE_DEFAULTS_QRC
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/release_defaults.qrc")
# The gate must re-run whenever the canonical source changes; without this the
# JSON is not a configure input and an edited-but-invalid file would sail
# through until the next unrelated reconfigure.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PROTRAIL_RELEASE_DEFAULTS}")
if(NOT EXISTS "${PROTRAIL_RELEASE_DEFAULTS}")
    message(FATAL_ERROR
        "RELEASE_DEFAULTS_MISSING: ${PROTRAIL_RELEASE_DEFAULTS} does not exist")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}"
            -D DEFAULTS_FILE=${PROTRAIL_RELEASE_DEFAULTS}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/validate_release_defaults.cmake"
    RESULT_VARIABLE _defaults_rc
    OUTPUT_VARIABLE _defaults_out
    ERROR_VARIABLE _defaults_err)
if(NOT _defaults_rc EQUAL 0)
    message(FATAL_ERROR
        "RELEASE_DEFAULTS_INVALID: ${_defaults_out}${_defaults_err}")
endif()


# ---- Sources ----
add_executable(protrail
    src/app/main.cpp
    src/app/application.cpp
    src/app/application.h
    src/app/topology_retry.h
    src/app/single_instance.h
    src/app/single_instance.cpp
    src/app/startup_paths.h
    src/app/startup_paths.cpp
    src/app/startup_mode.h
    src/app/startup_mode.cpp
    src/platform/autostart.h
    src/platform/autostart.cpp
    src/core/log.cpp
    src/core/log.h
    src/core/cursor_sample.h
    src/core/cursor_history.cpp
    src/core/cursor_history.h
    src/platform/dpi_awareness.h
    src/platform/dpi_awareness.cpp
    src/platform/mouse_input.cpp
    src/platform/mouse_input.h
    src/platform/button_flags.h
    src/platform/timestamp.h
    src/render/overlay_window.cpp
    src/render/overlay_window.h
    src/render/render_color.h
    src/effects/trail_config.h
    src/effects/trail_effect.h
    src/effects/trail_effect.cpp
    src/effects/click_config.h
    src/effects/click_bubble_effect.h
    src/effects/click_bubble_effect.cpp
    src/ui/theme.h
    src/ui/theme.cpp
    src/ui/check.xpm
    src/ui/theme.qrc
    src/ui/settings_window.h
    src/ui/settings_window.cpp
    src/ui/tray_icon.h
    src/ui/tray_icon.cpp
    src/ui/branding.h
    src/ui/branding.cpp
    "${PROTRAIL_WINDOWS_RC}"
    src/config/app_config.h
    src/config/app_config.cpp
    src/config/config_storage.h
    src/config/config_storage.cpp
    src/config/release_defaults.h
    src/config/release_defaults.cpp
    src/config/dev_defaults.h
    src/config/dev_defaults.cpp
    "${PROTRAIL_RELEASE_DEFAULTS_QRC}"
    src/render/render_scheduler.h
    src/render/render_scheduler.cpp
    src/render/overlay_manager.h
    src/render/overlay_manager.cpp
    src/render/screen_map.h
    src/render/deferred_coalescer.h
)

target_link_libraries(protrail PRIVATE
    Qt6::Widgets
    d2d1
    dcomp
    d3d11
    dxgi
    user32
    psapi
    advapi32
)

target_include_directories(protrail PRIVATE src)

# MVP 05: settings_window.h declares Q_OBJECT classes; AUTOMOC is enabled
# by qt_standard_project_setup() above, but the .cpp includes its own
# "settings_window.moc" at the end, so ensure the target has AUTOMOC.
set_property(TARGET protrail PROPERTY AUTOMOC ON)

set_target_properties(protrail PROPERTIES
    WIN32_EXECUTABLE ON
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)

# ---- Tests ----
enable_testing()
qt_standard_project_setup()

qt_add_executable(protrail_tests
    tests/test_log.cpp
)
target_link_libraries(protrail_tests PRIVATE Qt6::Widgets Qt6::Test)
set_target_properties(protrail_tests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME protrail_tests COMMAND protrail_tests)
set_tests_properties(protrail_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

qt_add_executable(protrail_history_tests
    tests/test_cursor_history.cpp
    src/core/cursor_history.cpp
)
target_link_libraries(protrail_history_tests PRIVATE Qt6::Test)
set_target_properties(protrail_history_tests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME protrail_history_tests COMMAND protrail_history_tests)

# T-007R: pure timestamp-conversion tests (no Qt, no Win32 runtime).
qt_add_executable(protrail_timestamp_tests
    tests/test_timestamp.cpp
)
set_target_properties(protrail_timestamp_tests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME protrail_timestamp_tests COMMAND protrail_timestamp_tests)

# T-007R: pure multi-transition button-flag parser tests (no Qt, no Win32
# runtime calls; only RI_MOUSE_* constants).
qt_add_executable(protrail_button_flags_tests
    tests/test_button_flags.cpp
)
set_target_properties(protrail_button_flags_tests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME protrail_button_flags_tests COMMAND protrail_button_flags_tests)

# T-007R A3: proves WM_INPUT arrives through the Qt win32 event dispatcher
# with NO pump and NO polling timer (SendInput-automated).
qt_add_executable(protrail_input_dispatch
    tests/test_input_dispatch.cpp
    src/platform/dpi_awareness.cpp
    src/platform/mouse_input.cpp
    src/core/cursor_history.cpp
    src/core/log.cpp
)
target_link_libraries(protrail_input_dispatch PRIVATE Qt6::Test user32)
set_target_properties(protrail_input_dispatch PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME protrail_input_dispatch COMMAND protrail_input_dispatch)

# T-008: pure trail-math tests (no Qt, no D2D runtime; only D2D types).
qt_add_executable(protrail_trail_tests
    tests/test_trail_effect.cpp
    src/effects/trail_effect.cpp
    src/core/cursor_history.cpp
)
target_link_libraries(protrail_trail_tests PRIVATE d2d1)
set_target_properties(protrail_trail_tests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME protrail_trail_tests COMMAND protrail_trail_tests)

# T-019: pure continuous-stroke cap-policy regressions (no Qt, no D2D
# runtime). Drives real TrailEffect output through the production sink
# contract and asserts the flat-flat/flat-round/round-round decisions.
qt_add_executable(protrail_stroke_policy_tests
    tests/test_trail_stroke_policy.cpp
    src/effects/trail_effect.cpp
    src/core/cursor_history.cpp
)
target_link_libraries(protrail_stroke_policy_tests PRIVATE d2d1)
set_target_properties(protrail_stroke_policy_tests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME protrail_stroke_policy_tests COMMAND protrail_stroke_policy_tests)

# T-009: pure click-bubble math tests (no Qt, no D2D runtime; only effect
# headers). Covers C12 items 1-17.
qt_add_executable(protrail_click_tests
    tests/test_click_bubble_effect.cpp
    src/effects/click_bubble_effect.cpp
)
set_target_properties(protrail_click_tests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME protrail_click_tests COMMAND protrail_click_tests)

# T-026 Hold Motion Wake / T-027 Hold Controls: pure structural and
# determinism tests for the detached world-space wake layer. No Qt and no D2D
# runtime; the effect's own sink contract is the observation point.
qt_add_executable(protrail_hold_wake_tests
    tests/test_hold_wake.cpp
    src/effects/click_bubble_effect.cpp
)
set_target_properties(protrail_hold_wake_tests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME protrail_hold_wake_tests COMMAND protrail_hold_wake_tests)

# T-010 MVP 05: settings-window binding tests (QtTest; needs the QApplication
# platform offscreen so the tests run headless in CI/agent shells).
qt_add_executable(protrail_gui_tests
    tests/test_settings_window.cpp
    src/ui/theme.cpp
    src/ui/check.xpm
    src/ui/theme.qrc
    src/ui/settings_window.cpp
    src/config/app_config.cpp
    src/config/config_storage.cpp
    src/config/release_defaults.cpp
    src/config/dev_defaults.cpp
    "${PROTRAIL_RELEASE_DEFAULTS_QRC}"
    src/core/log.cpp
    src/effects/trail_effect.cpp
    src/effects/click_bubble_effect.cpp
    src/core/cursor_history.cpp
)
target_link_libraries(protrail_gui_tests PRIVATE Qt6::Widgets Qt6::Test d2d1)
set_target_properties(protrail_gui_tests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
# T-010R Repair 5: deterministic headless contract. QT_QPA_PLATFORM=offscreen
# scoped to THIS test only via CTest test properties -- the runtime
# application's platform behavior is not mutated globally.
add_test(NAME protrail_gui_tests COMMAND protrail_gui_tests)
set_tests_properties(protrail_gui_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Unified-window coverage lives in protrail_gui_tests and protrail_tray_tests.

# T-011 MVP 06: configuration & persistence tests.
qt_add_executable(protrail_config_tests
    tests/test_config.cpp
    src/config/app_config.cpp
    src/config/config_storage.cpp
    src/config/release_defaults.cpp
    src/config/dev_defaults.cpp
    src/app/startup_paths.cpp
    "${PROTRAIL_RELEASE_DEFAULTS_QRC}"
    src/core/log.cpp
)
target_link_libraries(protrail_config_tests PRIVATE Qt6::Core Qt6::Test)
set_target_properties(protrail_config_tests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME protrail_config_tests COMMAND protrail_config_tests)

# T-012 MVP 07: render scheduler & performance tests.
qt_add_executable(protrail_scheduler_tests
    tests/test_render_scheduler.cpp
    src/render/render_scheduler.cpp
    src/platform/mouse_input.cpp
    src/core/cursor_history.cpp
    src/core/log.cpp
)
target_link_libraries(protrail_scheduler_tests PRIVATE Qt6::Core Qt6::Test user32 psapi)
set_target_properties(protrail_scheduler_tests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME protrail_scheduler_tests COMMAND protrail_scheduler_tests)

# T-013 MVP 08: multi-monitor and DPI tests.
qt_add_executable(protrail_multimonitor_tests
    tests/test_multi_monitor.cpp
    src/render/overlay_manager.cpp
    src/render/overlay_window.cpp
    src/platform/dpi_awareness.cpp
    src/effects/trail_effect.cpp
    src/effects/click_bubble_effect.cpp
    src/core/cursor_history.cpp
    src/core/log.cpp
)
target_link_libraries(protrail_multimonitor_tests PRIVATE Qt6::Core Qt6::Test user32 d2d1 dcomp d3d11 dxgi)
set_target_properties(protrail_multimonitor_tests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME protrail_multimonitor_tests COMMAND protrail_multimonitor_tests)

# T-014 MVP 09 / T-017R1: tray integration & lifecycle tests.
# T-021: the application-shaped source set is named ONCE here and consumed by
# both controller-level targets (tray lifecycle + Trail child-toggle
# lifecycle), so neither can drift from what the shipped application is.
set(PROTRAIL_APP_SOURCES
    src/app/application.h
    src/app/application.cpp
    src/app/topology_retry.h
    src/app/single_instance.h
    src/app/single_instance.cpp
    src/app/startup_paths.h
    src/app/startup_paths.cpp
    src/app/startup_mode.h
    src/app/startup_mode.cpp
    src/platform/autostart.h
    src/platform/autostart.cpp
    src/render/overlay_manager.h
    src/render/overlay_manager.cpp
    src/render/overlay_window.h
    src/render/overlay_window.cpp
    src/render/render_color.h
    src/render/render_scheduler.h
    src/render/render_scheduler.cpp
    src/render/screen_map.h
    src/render/deferred_coalescer.h
    src/platform/dpi_awareness.h
    src/platform/dpi_awareness.cpp
    src/platform/mouse_input.h
    src/platform/mouse_input.cpp
    src/ui/tray_icon.h
    src/ui/tray_icon.cpp
    src/ui/branding.h
    src/ui/branding.cpp
    "${PROTRAIL_WINDOWS_RC}"
    src/ui/settings_window.h
    src/ui/settings_window.cpp
    src/ui/theme.h
    src/ui/theme.cpp
    src/ui/check.xpm
    src/ui/theme.qrc
    src/config/app_config.h
    src/config/app_config.cpp
    src/config/config_storage.h
    src/config/config_storage.cpp
    src/config/release_defaults.h
    src/config/release_defaults.cpp
    src/config/dev_defaults.h
    src/config/dev_defaults.cpp
    "${PROTRAIL_RELEASE_DEFAULTS_QRC}"
    src/core/log.h
    src/core/log.cpp
    src/effects/trail_effect.h
    src/effects/trail_effect.cpp
    src/effects/click_bubble_effect.h
    src/effects/click_bubble_effect.cpp
    src/core/cursor_history.h
    src/core/cursor_history.cpp
)

qt_add_executable(protrail_tray_tests
    tests/test_tray.cpp
    ${PROTRAIL_APP_SOURCES}
)
target_link_libraries(protrail_tray_tests PRIVATE
    Qt6::Widgets
    Qt6::Test
    d2d1
    dcomp
    d3d11
    dxgi
    user32
    psapi
    advapi32
)
set_target_properties(protrail_tray_tests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
# Branding regressions assert the linked icon resource agrees with configure.
target_compile_definitions(protrail_tray_tests PRIVATE
    PROTRAIL_EXPECT_FINAL_ICON=${PROTRAIL_HAS_FINAL_ICON})
add_test(NAME protrail_tray_tests COMMAND protrail_tray_tests)
set_tests_properties(protrail_tray_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# T-021: Trail child-toggle lifecycle freshness. Controller-level regressions
# run INSIDE the production event loop (real SettingsWindow -> signal ->
# controller wiring) and assert against the application's own CursorHistory,
# including a real TrailEffect geometry build.
qt_add_executable(protrail_trail_lifecycle_tests
    tests/test_trail_lifecycle.cpp
    ${PROTRAIL_APP_SOURCES}
)
target_link_libraries(protrail_trail_lifecycle_tests PRIVATE
    Qt6::Widgets
    Qt6::Test
    d2d1
    dcomp
    d3d11
    dxgi
    user32
    psapi
    advapi32
)
set_target_properties(protrail_trail_lifecycle_tests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME protrail_trail_lifecycle_tests COMMAND protrail_trail_lifecycle_tests)
set_tests_properties(protrail_trail_lifecycle_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# T-021: deterministic sparkle overlay regressions. Pure effect-model tests
# (determinism, mode character, hard bounds); no GUI, no COM, no window.
qt_add_executable(protrail_sparkle_tests
    tests/test_trail_sparkles.cpp
    src/effects/trail_effect.cpp
    src/core/cursor_history.cpp
)
target_link_libraries(protrail_sparkle_tests PRIVATE
    Qt6::Core
    Qt6::Test
    d2d1
)
set_target_properties(protrail_sparkle_tests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME protrail_sparkle_tests COMMAND protrail_sparkle_tests)

# T-33: canonical Release Defaults regressions. Proves the embedded source is
# readable, that a fresh configuration equals it, and that the canonical
# defaults roundtrip through the normal persistence pipeline.
qt_add_executable(protrail_release_defaults_tests
    tests/test_release_defaults.cpp
    src/config/app_config.cpp
    src/config/config_storage.cpp
    src/config/release_defaults.cpp
    src/config/dev_defaults.cpp
    "${PROTRAIL_RELEASE_DEFAULTS_QRC}"
    src/core/log.cpp
)
target_link_libraries(protrail_release_defaults_tests PRIVATE Qt6::Core Qt6::Test)
set_target_properties(protrail_release_defaults_tests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME protrail_release_defaults_tests COMMAND protrail_release_defaults_tests)

# T-33 build-gate regression: the SAME validator the configure step runs must
# FAIL on a deliberately broken defaults source. This is what stops the gate
# from silently degrading into a no-op.
set(PROTRAIL_BROKEN_DEFAULTS
    "${CMAKE_CURRENT_BINARY_DIR}/release_defaults_broken.json")
file(WRITE "${PROTRAIL_BROKEN_DEFAULTS}" "{ \"schema_version\": 1, \"trail\": {} }\n")
add_test(NAME protrail_release_defaults_gate
    COMMAND "${CMAKE_COMMAND}"
            -D DEFAULTS_FILE=${PROTRAIL_BROKEN_DEFAULTS}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/validate_release_defaults.cmake")
set_tests_properties(protrail_release_defaults_gate PROPERTIES WILL_FAIL TRUE)

# T-33: the gate must also PASS on the real canonical source, so a broken
# validator cannot be mistaken for a healthy one.
add_test(NAME protrail_release_defaults_gate_accepts_canonical
    COMMAND "${CMAKE_COMMAND}"
            -D DEFAULTS_FILE=${PROTRAIL_RELEASE_DEFAULTS}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/validate_release_defaults.cmake")

# Release identity: RELEASE_VERSION, README badge, newest CHANGELOG entry and the
# release notes must name one version (see cmake/check_release_identity.cmake).
add_test(NAME protrail_release_identity
    COMMAND "${CMAKE_COMMAND}"
            -D SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_release_identity.cmake")

# T-48: the two script-driven gate tests above are registered OUTSIDE the
# executables loop below, so they would otherwise carry CTest's default
# infinite timeout -- a validator that never returns would hang the suite
# instead of failing it. The finite bound is applied to EVERY registration.
set_tests_properties(
    protrail_release_defaults_gate
    protrail_release_defaults_gate_accepts_canonical
    protrail_release_identity
    PROPERTIES TIMEOUT 300)


# through the in-memory registry backend (see the header comment of the test
# file); the suite additionally proves the REAL per-user Run key is
# byte-identical before and after, so a routine CTest run can never enroll
# the developer's machine into Windows startup.
qt_add_executable(protrail_autostart_tests
    tests/test_autostart.cpp
    src/platform/autostart.cpp
    src/app/startup_mode.cpp
)
target_link_libraries(protrail_autostart_tests PRIVATE
    Qt6::Core
    Qt6::Test
    advapi32
)
set_target_properties(protrail_autostart_tests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME protrail_autostart_tests COMMAND protrail_autostart_tests)

# W2-004: bounded per-request identity dedup for the single-instance channels.
qt_add_executable(protrail_request_dedup_tests
    tests/test_request_dedup.cpp
)
target_link_libraries(protrail_request_dedup_tests PRIVATE Qt6::Core Qt6::Test)
set_target_properties(protrail_request_dedup_tests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME protrail_request_dedup_tests COMMAND protrail_request_dedup_tests)

# T-018R1: single-instance protocol regressions. Unique per-test kernel
# object names; no production globals.
qt_add_executable(protrail_single_instance_tests
    tests/test_single_instance.cpp
    src/app/single_instance.cpp
    src/core/log.cpp
)
target_link_libraries(protrail_single_instance_tests PRIVATE Qt6::Core Qt6::Test user32)
set_target_properties(protrail_single_instance_tests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME protrail_single_instance_tests COMMAND protrail_single_instance_tests)

# T-019: CTest-launched executables need Qt runtime DLLs on PATH regardless
# of the ambient agent shell (which does not export the Qt bin directory).
# ENVIRONMENT_MODIFICATION prepends without clobbering the per-test
# QT_QPA_PLATFORM=offscreen ENVIRONMENT properties above.
get_target_property(_qt_core_dll Qt6::Core LOCATION)
get_filename_component(_qt_bin_dir "${_qt_core_dll}" DIRECTORY)
foreach(_test protrail_tests protrail_history_tests protrail_timestamp_tests
             protrail_button_flags_tests protrail_input_dispatch
             protrail_trail_tests protrail_stroke_policy_tests
             protrail_click_tests protrail_hold_wake_tests
             protrail_gui_tests protrail_config_tests
             protrail_scheduler_tests protrail_multimonitor_tests
             protrail_tray_tests protrail_single_instance_tests
             protrail_trail_lifecycle_tests protrail_sparkle_tests
             protrail_release_defaults_tests
             protrail_autostart_tests
             protrail_request_dedup_tests)
    set_tests_properties(${_test} PROPERTIES
        TIMEOUT 300
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}")
endforeach()
