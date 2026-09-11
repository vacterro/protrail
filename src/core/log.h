#pragma once

#include <string>
#include <string_view>

namespace ptd {

enum class LogLevel { Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4 };

// Resolves default production log path: %LOCALAPPDATA%\ProTrail\protrail.log
std::wstring default_log_path();

// Initializes the global log sink (file under %LOCALAPPDATA%\ProTrail +
// stdout). Idempotent; safe to call from anywhere after init.
void log_init(const std::wstring& explicit_path = {});

// Writes one line to the sink; no-op before log_init.
void log_write(LogLevel level, const std::string_view& message);

// Test/integration observable log sink hook.
using LogSinkFn = void(*)(LogLevel, const std::string_view&);
void set_log_sink(LogSinkFn sink);
void clear_log_sink();

} // namespace ptd
