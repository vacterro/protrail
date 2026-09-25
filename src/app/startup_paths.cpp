#include "startup_paths.h"
#include "../config/config_storage.h"
#include "../core/log.h"

#include <filesystem>
#include <cwchar>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace ptd {

StartupPaths resolve_startup_paths(
    const wchar_t* smoke_auto_exit_str,
    const wchar_t* smoke_state_dir_str) {

    bool smoke_active = false;
    if (smoke_auto_exit_str && smoke_auto_exit_str[0] != L'\0') {
        wchar_t* end = nullptr;
        const long val = std::wcstol(smoke_auto_exit_str, &end, 10);
        if (val > 0) {
            smoke_active = true;
        }
    }

    if (smoke_active) {
        if (!smoke_state_dir_str || smoke_state_dir_str[0] == L'\0') {
            // Case 2: Smoke auto-exit without state override -> fail closed
            StartupPaths sp;
            sp.valid = false;
            sp.is_smoke_mode = true;
            sp.config_path = {};
            sp.log_path = {};
            sp.error_message = "PROTRAIL_SMOKE_AUTO_EXIT_MS is active but PROTRAIL_SMOKE_STATE_DIR is not set; failing closed to prevent production state mutation";
            return sp;
        }

        // Case 3: Smoke auto-exit plus valid PROTRAIL_SMOKE_STATE_DIR
        const std::filesystem::path base_dir = smoke_state_dir_str;
        StartupPaths sp;
        sp.valid = true;
        sp.is_smoke_mode = true;
        sp.config_path = (base_dir / L"config.json").wstring();
        sp.log_path = (base_dir / L"protrail.log").wstring();
        sp.error_message = {};
        return sp;
    }

    // Case 1 & Case 4: Normal mode (smoke override ignored even if present)
    StartupPaths sp;
    sp.valid = true;
    sp.is_smoke_mode = false;
    sp.config_path = ConfigStorage::default_config_path();
    sp.log_path = default_log_path();
    sp.error_message = {};
    return sp;
}

StartupPaths resolve_startup_paths() {
    wchar_t exit_buf[32]{};
    const DWORD exit_len = GetEnvironmentVariableW(L"PROTRAIL_SMOKE_AUTO_EXIT_MS", exit_buf, 32);
    const wchar_t* exit_ptr = (exit_len > 0 && exit_len < 32) ? exit_buf : nullptr;

    std::wstring dir_str;
    const DWORD dir_len = GetEnvironmentVariableW(L"PROTRAIL_SMOKE_STATE_DIR", nullptr, 0);
    if (dir_len > 1) {
        dir_str.resize(dir_len);
        GetEnvironmentVariableW(L"PROTRAIL_SMOKE_STATE_DIR", dir_str.data(), dir_len);
        while (!dir_str.empty() && dir_str.back() == L'\0') {
            dir_str.pop_back();
        }
    }
    const wchar_t* dir_ptr = !dir_str.empty() ? dir_str.c_str() : nullptr;

    return resolve_startup_paths(exit_ptr, dir_ptr);
}

StartupLogBootstrapStatus initialize_startup_logging(
    const StartupPaths& paths) {
    if (!paths.valid || paths.log_path.empty()) {
        return StartupLogBootstrapStatus::Failed;
    }
    return log_init(paths.log_path)
        ? StartupLogBootstrapStatus::Ready
        : StartupLogBootstrapStatus::Failed;
}

} // namespace ptd
