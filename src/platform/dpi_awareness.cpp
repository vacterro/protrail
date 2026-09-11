#include "dpi_awareness.h"

#include "../core/log.h"

// Explicit SDK authority for PROCESS_DPI_AWARENESS /
// PROCESS_PER_MONITOR_DPI_AWARE / SetProcessDpiAwareness (Windows 8.1
// fallback). Never rely on a transitive include for these.
#include <shellscalingapi.h>

#include <string>

namespace ptd {
namespace dpi {
namespace {

using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
using GetThreadDpiAwarenessContextFn = DPI_AWARENESS_CONTEXT(WINAPI*)();
using GetAwarenessFromDpiAwarenessContextFn = DPI_AWARENESS(WINAPI*)(DPI_AWARENESS_CONTEXT);
using AreDpiAwarenessContextsEqualFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT, DPI_AWARENESS_CONTEXT);
using GetDpiForWindowFn = UINT(WINAPI*)(HWND);

HMODULE user32_module() {
    static HMODULE module = GetModuleHandleW(L"user32.dll");
    return module;
}

template <typename T>
T user32_proc(const char* name) {
    const HMODULE module = user32_module();
    if (!module) return nullptr;
    return reinterpret_cast<T>(GetProcAddress(module, name));
}

Awareness from_context(DPI_AWARENESS_CONTEXT context) {
    const auto equal = user32_proc<AreDpiAwarenessContextsEqualFn>(
        "AreDpiAwarenessContextsEqual");
    if (equal) {
        if (equal(context, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
            return Awareness::PerMonitorV2;
        if (equal(context, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE))
            return Awareness::PerMonitorV1;
        if (equal(context, DPI_AWARENESS_CONTEXT_SYSTEM_AWARE))
            return Awareness::SystemAware;
        if (equal(context, DPI_AWARENESS_CONTEXT_UNAWARE) ||
            equal(context, DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED))
            return Awareness::Unaware;
    }

    const auto get_awareness = user32_proc<GetAwarenessFromDpiAwarenessContextFn>(
        "GetAwarenessFromDpiAwarenessContext");
    if (!get_awareness || !context) return Awareness::Unknown;
    switch (get_awareness(context)) {
        case DPI_AWARENESS_UNAWARE: return Awareness::Unaware;
        case DPI_AWARENESS_SYSTEM_AWARE: return Awareness::SystemAware;
        case DPI_AWARENESS_PER_MONITOR_AWARE: return Awareness::PerMonitorV1;
        default: return Awareness::Unknown;
    }
}

} // namespace

Awareness current_process_awareness() {
    // On the startup GUI thread, GetThreadDpiAwarenessContext resolves the
    // process default unless somebody explicitly changed this thread. This
    // is also the context inherited by the application's top-level HWNDs.
    const auto get_context = user32_proc<GetThreadDpiAwarenessContextFn>(
        "GetThreadDpiAwarenessContext");
    return get_context ? from_context(get_context()) : Awareness::Unknown;
}

const char* awareness_name(Awareness awareness) {
    switch (awareness) {
        case Awareness::Unaware: return "UNAWARE";
        case Awareness::SystemAware: return "SYSTEM_AWARE";
        case Awareness::PerMonitorV1: return "PER_MONITOR_AWARE_V1";
        case Awareness::PerMonitorV2: return "PER_MONITOR_AWARE_V2";
        default: return "UNKNOWN";
    }
}

bool establish_process_contract() {
    const auto set_context = user32_proc<SetProcessDpiAwarenessContextFn>(
        "SetProcessDpiAwarenessContext");
    bool set_succeeded = false;
    DWORD set_error = ERROR_CALL_NOT_IMPLEMENTED;

    if (set_context) {
        SetLastError(ERROR_SUCCESS);
        set_succeeded = set_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE;
        set_error = set_succeeded ? ERROR_SUCCESS : GetLastError();
    } else {
        // Windows 8.1 fallback. V1 still preserves the physical-pixel
        // coordinate contract; V2 remains the requested/default target.
        HMODULE shcore = LoadLibraryW(L"shcore.dll");
        if (shcore) {
            using SetProcessDpiAwarenessFn = HRESULT(WINAPI*)(PROCESS_DPI_AWARENESS);
            const auto set_legacy = reinterpret_cast<SetProcessDpiAwarenessFn>(
                GetProcAddress(shcore, "SetProcessDpiAwareness"));
            if (set_legacy) {
                const HRESULT hr = set_legacy(PROCESS_PER_MONITOR_DPI_AWARE);
                set_succeeded = SUCCEEDED(hr);
                set_error = static_cast<DWORD>(hr);
            }
            FreeLibrary(shcore);
        }
    }

    const Awareness actual = current_process_awareness();
    const bool acceptable = actual == Awareness::PerMonitorV2 ||
                            actual == Awareness::PerMonitorV1;

    std::string message = "dpi: requested PER_MONITOR_AWARE_V2, actual=";
    message += awareness_name(actual);
    message += ", setter=";
    if (set_succeeded) {
        message += "success";
    } else if (set_error == ERROR_ACCESS_DENIED) {
        message += "already-established";
    } else {
        message += "failed(" + std::to_string(set_error) + ")";
    }
    log_write(acceptable ? LogLevel::Info : LogLevel::Error, message);
    return acceptable;
}

bool window_dpi(HWND hwnd, UINT& dpi_x, UINT& dpi_y) {
    if (!hwnd) return false;
    const auto get_dpi = user32_proc<GetDpiForWindowFn>("GetDpiForWindow");
    if (!get_dpi) return false;
    const UINT dpi = get_dpi(hwnd);
    if (dpi == 0) return false;
    dpi_x = dpi;
    dpi_y = dpi;
    return true;
}

} // namespace dpi
} // namespace ptd
