#pragma once

#include <string>

namespace ptd {

struct StartupPaths {
    bool valid = true;
    bool is_smoke_mode = false;
    std::wstring config_path;
    std::wstring log_path;
    std::string error_message;
};

// Pure deterministic path resolution for testing and runtime.
// When smoke_auto_exit_str is non-null/non-empty and parses to > 0:
//   - If smoke_state_dir_str is non-null/non-empty:
//       valid = true, config_path = <dir>\config.json, log_path = <dir>\protrail.log
//   - Else:
//       valid = false (fail-closed: smoke auto-exit armed without isolated state dir)
// Else (smoke mode not active):
//   - smoke_state_dir_str is ignored;
//   - valid = true, config_path = ConfigStorage::default_config_path(), log_path = default_log_path()
StartupPaths resolve_startup_paths(
    const wchar_t* smoke_auto_exit_str,
    const wchar_t* smoke_state_dir_str);

// Environment-reading overload for production wWinMain().
StartupPaths resolve_startup_paths();

} // namespace ptd
