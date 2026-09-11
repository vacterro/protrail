cmake_minimum_required(VERSION 3.24)

project(ProTrail
    VERSION 0.1.0
    DESCRIPTION "Native Windows cursor effects application"
    LANGUAGES CXX)

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
    add_compile_options(/W4 /WX /utf-8 /Zc:preprocessor /permissive- /FS)
endif()

# ---- Qt ----
list(APPEND CMAKE_PREFIX_PATH "C:/Qt/6.8.0/msvc2022_64")
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Test)
qt_standard_project_setup()

# ---- Sources ----
add_executable(protrail
    src/app/main.cpp
    src/app/application.cpp
    src/app/application.h
    src/app/startup_paths.h
    src/app/startup_paths.cpp
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
    src/config/app_config.h
    src/config/app_config.cpp
    src/config/config_storage.h
    src/config/config_storage.cpp
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

qt_add_executable(protrail_history_tests
    tests/test_cursor_history.cpp
    src/core/cursor_history.cpp
)
target_link_libraries(protrail_history_tests PRIVATE Qt6::Test)
target_include_directories(protrail_history_tests PRIVATE .)
set_target_properties(protrail_history_tests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME protrail_history_tests COMMAND protrail_history_tests)

# T-007R: pure timestamp-conversion tests (no Qt, no Win32 runtime).
qt_add_executable(protrail_timestamp_tests
    tests/test_timestamp.cpp
)
target_include_directories(protrail_timestamp_tests PRIVATE .)
set_target_properties(protrail_timestamp_tests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME protrail_timestamp_tests COMMAND protrail_timestamp_tests)

# T-007R: pure multi-transition button-flag parser tests (no Qt, no Win32
# runtime calls; only RI_MOUSE_* constants).
qt_add_executable(protrail_button_flags_tests
    tests/test_button_flags.cpp
)
target_include_directories(protrail_button_flags_tests PRIVATE .)
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
target_include_directories(protrail_input_dispatch PRIVATE .)
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
target_include_directories(protrail_trail_tests PRIVATE .)
set_target_properties(protrail_trail_tests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME protrail_trail_tests COMMAND protrail_trail_tests)

# T-009: pure click-bubble math tests (no Qt, no D2D runtime; only effect
# headers). Covers C12 items 1-17.
qt_add_executable(protrail_click_tests
    tests/test_click_bubble_effect.cpp
    src/effects/click_bubble_effect.cpp
)
target_include_directories(protrail_click_tests PRIVATE .)
set_target_properties(protrail_click_tests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME protrail_click_tests COMMAND protrail_click_tests)

# T-010 MVP 05: settings-window binding tests (QtTest; needs the QApplication
# platform offscreen so the tests run headless in CI/agent shells).
qt_add_executable(protrail_gui_tests
    tests/test_settings_window.cpp
    src/ui/theme.cpp
    src/ui/check.xpm
    src/ui/theme.qrc
    src/ui/settings_window.cpp
    src/effects/trail_effect.cpp
    src/effects/click_bubble_effect.cpp
    src/core/cursor_history.cpp
)
target_link_libraries(protrail_gui_tests PRIVATE Qt6::Widgets Qt6::Test d2d1)
target_include_directories(protrail_gui_tests PRIVATE .)
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

# T-011 MVP 06: configuration & persistence tests.
qt_add_executable(protrail_config_tests
    tests/test_config.cpp
    src/config/app_config.cpp
    src/config/config_storage.cpp
    src/core/log.cpp
)
target_link_libraries(protrail_config_tests PRIVATE Qt6::Core Qt6::Test)
target_include_directories(protrail_config_tests PRIVATE .)
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
target_include_directories(protrail_scheduler_tests PRIVATE .)
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
target_include_directories(protrail_multimonitor_tests PRIVATE .)
set_target_properties(protrail_multimonitor_tests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME protrail_multimonitor_tests COMMAND protrail_multimonitor_tests)

# T-014 MVP 09 / T-017R1: tray integration & lifecycle tests.
qt_add_executable(protrail_tray_tests
    tests/test_tray.cpp
    src/app/application.h
    src/app/application.cpp
    src/app/startup_paths.h
    src/app/startup_paths.cpp
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
    src/core/log.h
    src/core/log.cpp
    src/effects/trail_effect.h
    src/effects/trail_effect.cpp
    src/effects/click_bubble_effect.h
    src/effects/click_bubble_effect.cpp
    src/core/cursor_history.h
    src/core/cursor_history.cpp
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
)
target_include_directories(protrail_tray_tests PRIVATE .)
set_target_properties(protrail_tray_tests PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME protrail_tray_tests COMMAND protrail_tray_tests)
set_tests_properties(protrail_tray_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

