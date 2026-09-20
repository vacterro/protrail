#pragma once

#include <string>
#include <string_view>

namespace ptd {

enum class LogLevel { Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4 };

// Resolves default production log path: %LOCALAPPDATA%\ProTrail\protrail.log
std::wstring default_log_path();

// T-030: the ONE %LOCALAPPDATA% probe used for user-state paths, with a test
// seam. A null probe (the default) restores the production
// SHGetKnownFolderPath call. Returns true and fills `out` on success. When it
// fails, callers MUST use a logged deterministic fallback instead of a
// working-directory-relative name.
using KnownFolderProbeFn = bool (*)(std::wstring& out);
void set_known_folder_probe_for_tests(KnownFolderProbeFn probe);
bool local_app_data_folder(std::wstring& out);

// T-030: directory of the running executable, or empty when unresolvable.
// The single deterministic base for user-state fallback paths.
std::wstring executable_directory();

// Initializes the global log sink (file under %LOCALAPPDATA%\ProTrail +
// stdout). Idempotent; safe to call from anywhere after init. Returns true on
// success (or if already initialized), false if directory creation failed.
bool log_init(const std::wstring& explicit_path = {});

// Writes one line to the sink; no-op before log_init.
void log_write(LogLevel level, const std::string_view& message);

// Test/integration observable log sink hook.
using LogSinkFn = void(*)(LogLevel, const std::string_view&);
void set_log_sink(LogSinkFn sink);
void clear_log_sink();

// Test seam to query and reset log initialization state.
bool is_log_initialized();
void reset_log_for_tests();

} // namespace ptd
