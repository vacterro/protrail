#include "log.h"

#include <Windows.h>
#include <ShlObj.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace ptd {

namespace {

std::mutex g_mutex;
std::filesystem::path g_path;
bool g_initialized = false;
LogSinkFn g_custom_sink = nullptr;

const char* level_text(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

} // namespace

void set_log_sink(LogSinkFn sink) {
    std::lock_guard lock(g_mutex);
    g_custom_sink = sink;
}

void clear_log_sink() {
    std::lock_guard lock(g_mutex);
    g_custom_sink = nullptr;
}

std::wstring default_log_path() {
    wchar_t* raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) {
        std::filesystem::path dir = raw;
        CoTaskMemFree(raw);
        dir /= L"ProTrail";
        return (dir / L"protrail.log").wstring();
    }
    return L"protrail.log";
}

void log_init(const std::wstring& explicit_path) {
    std::lock_guard lock(g_mutex);
    if (g_initialized) return;
    if (!explicit_path.empty()) {
        g_path = explicit_path;
    } else {
        g_path = default_log_path();
    }
    std::error_code ec;
    std::filesystem::create_directories(g_path.parent_path(), ec);
    g_initialized = true;
}

void log_write(LogLevel level, const std::string_view& message) {
    namespace chr = std::chrono;
    auto now = chr::system_clock::now();
    auto time = chr::system_clock::to_time_t(now);
    tm local{};
    localtime_s(&local, &time);
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &local);

    char line[1024];
    int n = _snprintf_s(line, sizeof(line), _TRUNCATE,
                        "[%s] [%s] %.*s\n", ts, level_text(level),
                        static_cast<int>(message.size()), message.data());
    if (n <= 0) return;

    std::printf("%s", line);
    std::fflush(stdout);

    std::lock_guard lock(g_mutex);
    if (g_custom_sink) {
        g_custom_sink(level, message);
    }
    if (g_initialized && !g_path.empty()) {
        std::ofstream f(g_path, std::ios::app);
        if (f) f.write(line, n);
    }
    OutputDebugStringA(line);
}

} // namespace ptd
