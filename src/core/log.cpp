#include "log.h"

#include <Windows.h>
#include <ShlObj.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <utility>

namespace ptd {

namespace {

std::mutex g_mutex;
std::filesystem::path g_path;
bool g_initialized = false;
LogSinkFn g_custom_sink = nullptr;
KnownFolderProbeFn g_known_folder_probe = nullptr;
ModulePathQueryForTests g_module_path_query_for_tests;

bool production_known_folder_probe(std::wstring& out) {
    wchar_t* raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) {
        out.assign(raw);
        CoTaskMemFree(raw);
        return true;
    }
    return false;
}

std::size_t query_module_path(wchar_t* buffer, std::size_t capacity) {
    ModulePathQueryForTests query;
    {
        std::lock_guard lock(g_mutex);
        query = g_module_path_query_for_tests;
    }
    if (query) return query(buffer, capacity);
    if (capacity > (std::numeric_limits<DWORD>::max)()) return 0;
    return static_cast<std::size_t>(GetModuleFileNameW(
        nullptr, buffer, static_cast<DWORD>(capacity)));
}

std::string path_to_utf8(const std::filesystem::path& path) {
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::filesystem::path fallback_base_dir() {
    const std::wstring exe_dir = executable_directory();
    if (exe_dir.empty()) {
        // Genuine resolution failure must not silently use CWD; use an absolute
        // fallback (the same known-folder result) or report failure.
        ptd::log_write(LogLevel::Error,
                       "log: executable path resolution failed; fallback is absolute, not CWD");
        return std::filesystem::temp_directory_path();
    }
    return std::filesystem::path(exe_dir);
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

void set_module_path_query_for_tests(ModulePathQueryForTests query) {
    std::lock_guard lock(g_mutex);
    g_module_path_query_for_tests = std::move(query);
}

void clear_module_path_query_for_tests() {
    std::lock_guard lock(g_mutex);
    g_module_path_query_for_tests = {};
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
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const std::size_t written = query_module_path(buffer.data(), buffer.size());
        if (written == 0) return {};
        if (written < buffer.size()) {
            buffer.resize(written);
            return std::filesystem::path(buffer).parent_path().wstring();
        }
        buffer.resize(buffer.size() * 2);
    }
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
    g_module_path_query_for_tests = {};
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
