#include "autostart.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>

namespace ptd {

namespace {

HKEY open_run_key(REGSAM access, unsigned long& last_error) {
    HKEY key = nullptr;
    const LSTATUS status = RegOpenKeyExW(HKEY_CURRENT_USER,
                                         Win32RunKeyBackend::kRunKeyPath,
                                         0, access, &key);
    if (status != ERROR_SUCCESS) {
        last_error = static_cast<unsigned long>(status);
        return nullptr;
    }
    return key;
}

} // namespace

bool Win32RunKeyBackend::read(const std::wstring& name, std::wstring& out) {
    last_error_ = 0;
    HKEY key = open_run_key(KEY_QUERY_VALUE, last_error_);
    if (!key) return false;

    DWORD type = 0;
    DWORD bytes = 0;
    LSTATUS status = RegQueryValueExW(key, name.c_str(), nullptr, &type, nullptr, &bytes);
    if (status != ERROR_SUCCESS) {
        // ERROR_FILE_NOT_FOUND is the ordinary "not registered" answer.
        last_error_ = static_cast<unsigned long>(status);
        RegCloseKey(key);
        return false;
    }
    if (type != REG_SZ && type != REG_EXPAND_SZ) {
        last_error_ = ERROR_DATATYPE_MISMATCH;
        RegCloseKey(key);
        return false;
    }

    std::wstring buffer(bytes / sizeof(wchar_t) + 1, L'\0');
    status = RegQueryValueExW(key, name.c_str(), nullptr, &type,
                              reinterpret_cast<LPBYTE>(buffer.data()), &bytes);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        last_error_ = static_cast<unsigned long>(status);
        return false;
    }
    buffer.resize(wcslen(buffer.c_str()));
    out = buffer;
    return true;
}

bool Win32RunKeyBackend::write(const std::wstring& name, const std::wstring& command) {
    last_error_ = 0;
    HKEY key = nullptr;
    // Create without requesting any wider access than the value write needs;
    // the key already exists on every supported Windows version.
    const LSTATUS open_status = RegCreateKeyExW(HKEY_CURRENT_USER,
                                                Win32RunKeyBackend::kRunKeyPath,
                                                0, nullptr, 0, KEY_SET_VALUE,
                                                nullptr, &key, nullptr);
    if (open_status != ERROR_SUCCESS) {
        last_error_ = static_cast<unsigned long>(open_status);
        return false;
    }

    const DWORD bytes = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
    const LSTATUS status = RegSetValueExW(key, name.c_str(), 0, REG_SZ,
                                          reinterpret_cast<const BYTE*>(command.c_str()),
                                          bytes);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        last_error_ = static_cast<unsigned long>(status);
        return false;
    }
    return true;
}

bool Win32RunKeyBackend::remove(const std::wstring& name) {
    last_error_ = 0;
    HKEY key = nullptr;
    const LSTATUS open_status = RegOpenKeyExW(HKEY_CURRENT_USER,
                                              Win32RunKeyBackend::kRunKeyPath,
                                              0, KEY_SET_VALUE, &key);
    if (open_status != ERROR_SUCCESS) {
        // A missing startup key means the value cannot exist either.
        if (open_status == ERROR_FILE_NOT_FOUND) return true;
        last_error_ = static_cast<unsigned long>(open_status);
        return false;
    }

    const LSTATUS status = RegDeleteValueW(key, name.c_str());
    RegCloseKey(key);
    if (status == ERROR_FILE_NOT_FOUND) return true;  // already absent
    if (status != ERROR_SUCCESS) {
        last_error_ = static_cast<unsigned long>(status);
        return false;
    }
    return true;
}

std::vector<std::wstring> Win32RunKeyBackend::names() {
    last_error_ = 0;
    std::vector<std::wstring> out;
    HKEY key = open_run_key(KEY_QUERY_VALUE, last_error_);
    if (!key) return out;

    // Probe only: this enumeration exists for diagnostics and for the
    // test-suite proof that unrelated entries survive. The write path never
    // consults it.
    DWORD index = 0;
    for (;;) {
        wchar_t name[512]{};
        DWORD name_len = 512;
        const LSTATUS status = RegEnumValueW(key, index, name, &name_len,
                                             nullptr, nullptr, nullptr, nullptr);
        if (status != ERROR_SUCCESS) break;
        out.emplace_back(name, name_len);
        ++index;
    }
    RegCloseKey(key);
    return out;
}

bool InMemoryAutostartBackend::read(const std::wstring& name, std::wstring& out) {
    last_error_ = 0;
    if (fail_reads) {
        last_error_ = ERROR_ACCESS_DENIED;
        return false;
    }
    for (const auto& entry : values) {
        if (entry.first == name) {
            out = entry.second;
            return true;
        }
    }
    last_error_ = ERROR_FILE_NOT_FOUND;
    return false;
}

bool InMemoryAutostartBackend::write(const std::wstring& name,
                                     const std::wstring& command) {
    ++write_calls;
    last_error_ = 0;
    if (fail_writes) {
        last_error_ = ERROR_ACCESS_DENIED;
        return false;
    }
    for (auto& entry : values) {
        if (entry.first == name) {
            entry.second = command;
            return true;
        }
    }
    values.emplace_back(name, command);
    return true;
}

bool InMemoryAutostartBackend::remove(const std::wstring& name) {
    ++remove_calls;
    last_error_ = 0;
    if (fail_removes) {
        last_error_ = ERROR_ACCESS_DENIED;
        return false;
    }
    const auto it = std::find_if(values.begin(), values.end(),
                                 [&name](const auto& entry) {
                                     return entry.first == name;
                                 });
    if (it == values.end()) return true;  // already absent
    values.erase(it);
    return true;
}

std::vector<std::wstring> InMemoryAutostartBackend::names() {
    std::vector<std::wstring> out;
    out.reserve(values.size());
    for (const auto& entry : values) out.push_back(entry.first);
    return out;
}

std::wstring quote_argument(const std::wstring& argument) {
    // Windows command-line quoting (the rules CreateProcess parses with):
    //   - wrap the argument in double quotes
    //   - escape an embedded quote as \" and double the backslashes that
    //     immediately precede a quote or the closing quote
    // A path with spaces, one with an embedded quote and one ending in a
    // backslash all survive this unchanged.
    std::wstring out;
    out.push_back(L'"');
    std::size_t backslashes = 0;
    for (const wchar_t ch : argument) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
            backslashes = 0;
            continue;
        }
        out.append(backslashes, L'\\');
        backslashes = 0;
        out.push_back(ch);
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

std::vector<std::wstring> split_command_line(const std::wstring& command_line) {
    std::vector<std::wstring> args;
    std::wstring current;
    bool in_quotes = false;
    bool has_token = false;
    std::size_t backslashes = 0;

    for (std::size_t i = 0; i < command_line.size(); ++i) {
        const wchar_t ch = command_line[i];
        if (ch == L'\\') {
            ++backslashes;
            has_token = true;
            continue;
        }
        if (ch == L'"') {
            current.append(backslashes / 2, L'\\');
            if (backslashes % 2 == 1) {
                current.push_back(L'"');
            } else {
                in_quotes = !in_quotes;
            }
            backslashes = 0;
            has_token = true;
            continue;
        }
        current.append(backslashes, L'\\');
        backslashes = 0;
        if (!in_quotes && (ch == L' ' || ch == L'\t')) {
            if (has_token) {
                args.push_back(current);
                current.clear();
                has_token = false;
            }
            continue;
        }
        current.push_back(ch);
        has_token = true;
    }
    current.append(backslashes, L'\\');
    if (has_token) args.push_back(current);
    return args;
}

std::wstring current_executable_path() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD written = GetModuleFileNameW(nullptr, buffer.data(),
                                                 static_cast<DWORD>(buffer.size()));
        if (written == 0) return {};
        if (written < buffer.size()) {
            buffer.resize(written);
            return buffer;
        }
        // Truncated: grow and retry (long paths are legal).
        buffer.resize(buffer.size() * 2);
    }
}

AutostartManager::AutostartManager(AutostartBackend& backend) : backend_(backend) {}

std::wstring AutostartManager::build_command(const std::wstring& exe_path) {
    // The startup argument is the SAME token the command-line parser reads
    // (kStartupMinimizedArgument); it is quoted through the same helper even
    // though it needs no quoting, so there is exactly one quoting rule.
    return quote_argument(exe_path) + L" " + quote_argument(kStartupMinimizedArgument);
}

bool AutostartManager::is_enabled() {
    bool enabled = false;
    if (!is_enabled(enabled)) return false;
    return enabled;
}

bool AutostartManager::is_enabled(bool& out_enabled) {
    std::wstring ignored;
    if (!backend_.read(kValueName, ignored)) {
        const unsigned long err = backend_.last_error();
        if (err != 0 && err != ERROR_FILE_NOT_FOUND) {
            out_enabled = false;
            return false;
        }
        out_enabled = false;
        return true;
    }
    out_enabled = true;
    return true;
}

std::wstring AutostartManager::registered_command() {
    std::wstring value;
    if (!backend_.read(kValueName, value)) return {};
    return value;
}

bool AutostartManager::enable(const std::wstring& exe_path) {
    const std::wstring desired = build_command(exe_path);
    std::wstring current;
    if (backend_.read(kValueName, current) && current == desired) {
        // Idempotent: an already-correct registration is not rewritten.
        return true;
    }
    if (!backend_.write(kValueName, desired)) return false;
    // Verify by re-reading rather than trusting the write.
    std::wstring after;
    return backend_.read(kValueName, after) && after == desired;
}

bool AutostartManager::disable() {
    if (!backend_.remove(kValueName)) return false;
    // Verify the owned value is gone; a still-present value or read failure is a failure.
    std::wstring ignored;
    if (!backend_.read(kValueName, ignored)) {
        const unsigned long err = backend_.last_error();
        if (err != 0 && err != ERROR_FILE_NOT_FOUND) {
            return false;
        }
        return true;
    }
    return false;
}

bool AutostartManager::reconcile(const std::wstring& exe_path) {
    std::wstring current;
    if (!backend_.read(kValueName, current)) {
        const unsigned long err = backend_.last_error();
        if (err != 0 && err != ERROR_FILE_NOT_FOUND) {
            // Read failed with an error (e.g. denied access, unreadable key).
            // Surface the failure rather than claiming success.
            return false;
        }
        // Not registered: reconcile never enrolls a user who never enabled
        // the feature. Reported as success because there is nothing to fix.
        return true;
    }
    if (current == build_command(exe_path)) return true;
    return enable(exe_path);
}

bool AutostartManager::reconcile_desired(bool desired_enabled,
                                        const std::wstring& exe_path) {
    // CORE-002: enforce the persisted DESIRED state in both directions.
    // The OFF branch is what repairs a transient disable failure: the
    // preference stays OFF, but the machine is re-reconciled toward it on
    // the next start instead of being gated away by that same OFF value.
    // The OFF path can only remove ProTrail's own stable value, so it never
    // violates the rule that reconcile must not enroll a user who never
    // enabled the feature.
    if (!desired_enabled) return disable();
    if (exe_path.empty()) {
        // The ON branch needs a path. An unresolvable path is not a reason
        // to leave the machine inconsistent, so report it honestly.
        return false;
    }
    return reconcile(exe_path);
}

} // namespace ptd
