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
KnownFolderProbeFn g_known_folder_probe = nullptr;

bool production_known_folder_probe(std::wstring& out) {
    wchar_t* raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) {
        out.assign(raw);
        CoTaskMemFree(raw);
        return true;
    }
    return false;
}

std::string path_to_utf8(const std::filesystem::path& path) {
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::filesystem::path fallback_base_dir() {
    const std::wstring exe_dir = executable_directory();
    return exe_dir.empty() ? std::filesystem::path(L".")
                           : std::filesystem::path(exe_dir);
}

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

void set_known_folder_probe_for_tests(KnownFolderProbeFn probe) {
    std::lock_guard lock(g_mutex);
    g_known_folder_probe = probe;
}

bool local_app_data_folder(std::wstring& out) {
    KnownFolderProbeFn probe = nullptr;
    {
        std::lock_guard lock(g_mutex);
        probe = g_known_folder_probe;
    }
    return (probe ? probe : production_known_folder_probe)(out);
}

std::wstring executable_directory() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD written = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (written == 0 || written >= MAX_PATH) return {};
    return std::filesystem::path(buffer).parent_path().wstring();
}

std::wstring default_log_path() {
    std::wstring base;
    if (local_app_data_folder(base) && !base.empty()) {
        std::filesystem::path dir = base;
        dir /= L"ProTrail";
        return (dir / L"protrail.log").wstring();
    }
    // T-030: a probe failure must never silently resolve the log into the
    // process working directory (launcher-controlled and different between
    // launches). Log it and use the ONE documented deterministic fallback:
    // the executable's own directory.
    const std::filesystem::path fallback = fallback_base_dir() / L"protrail.log";
    log_write(LogLevel::Warn,
              "log: %LOCALAPPDATA% unavailable; using executable-relative fallback " +
                  path_to_utf8(fallback));
    return fallback.wstring();
}

bool is_log_initialized() {
    std::lock_guard lock(g_mutex);
    return g_initialized;
}

void reset_log_for_tests() {
    std::lock_guard lock(g_mutex);
    g_initialized = false;
    g_path.clear();
    g_custom_sink = nullptr;
    g_known_folder_probe = nullptr;
}

bool log_init(const std::wstring& explicit_path) {
    // T-030: resolve the default path BEFORE taking the sink lock. The
    // known-folder probe helper takes the same lock, and relocking a
    // std::mutex on one thread throws resource_deadlock_would_occur.
    {
        std::lock_guard lock(g_mutex);
        if (g_initialized) return true;
    }
    std::wstring path = explicit_path;
    if (path.empty()) path = default_log_path();

    const std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            log_write(LogLevel::Error,
                      "log: failed to create log directory " + path_to_utf8(parent) +
                          ": " + ec.message());
            return false;
        }
    }

    std::lock_guard lock(g_mutex);
    if (g_initialized) return true;
    g_path = std::move(path);
    g_initialized = true;
    return true;
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
