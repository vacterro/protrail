#include "application.h"
#include "startup_paths.h"

#include "../core/log.h"
#include "../platform/dpi_awareness.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    const auto paths = ptd::resolve_startup_paths();
    if (!paths.valid) {
        // T-017R2: fail closed if smoke auto-exit is armed without an isolated state dir
        return 2;
    }

    HANDLE single_instance_mutex = nullptr;
    if (!paths.is_smoke_mode) {
        // T-018: enforce single running instance in normal mode
        single_instance_mutex = CreateMutexW(nullptr, FALSE, L"Local\\ProTrail_SingleInstance_Mutex");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            // Already running: notify existing instance to show its settings window
            const UINT wm_activate = RegisterWindowMessageW(L"ProTrail_ActivateInstance");
            PostMessageW(HWND_BROADCAST, wm_activate, 0, 0);
            if (single_instance_mutex) {
                CloseHandle(single_instance_mutex);
            }
            return 0;
        }
    }

    // Explicit DPI contract before QApplication or ANY top-level HWND.
    // Logging starts here with the resolved path so early evidence is not lost.
    ptd::log_init(paths.log_path);
    if (!ptd::dpi::establish_process_contract())
        ptd::log_write(ptd::LogLevel::Warn,
                       "dpi: physical-pixel contract could not be verified");

    int rc = 0;
    {
        Application app(paths.config_path);
        if (!app.initialize()) {
            if (single_instance_mutex) CloseHandle(single_instance_mutex);
            return 1;
        }
        rc = app.run();
        app.shutdown();
    }

    if (single_instance_mutex) {
        CloseHandle(single_instance_mutex);
    }
    return rc;
}
