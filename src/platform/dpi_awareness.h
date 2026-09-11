#pragma once

// MVP 08 Phase 1/5: explicit Windows process DPI-awareness contract and
// per-window DPI queries.
//
// The contract must be established BEFORE QApplication (or any top-level
// window) exists, so that GetCursorPos returns physical virtual-desktop
// pixels, monitor RECTs and native overlay HWND coordinates are all
// physical pixels, and Qt Settings stays correctly DPI-aware.
//
// Target: DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2.
// If the process awareness was already set (Qt platform init or manifest),
// the API refuses the change; we then QUERY the actual context and verify
// it instead of blindly re-setting.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace ptd {
namespace dpi {

enum class Awareness {
    Unknown = 0,
    Unaware = 1,
    SystemAware = 2,
    PerMonitorV1 = 3,
    PerMonitorV2 = 4,
};

// Establish the process contract (call once, before QApplication).
// Returns true when the process is Per-Monitor aware (V2 preferred,
// V1 accepted as already-locked legacy context). Logs the truthful result
// either way.
bool establish_process_contract();

// Query the CURRENT process DPI awareness (no mutation).
Awareness current_process_awareness();

const char* awareness_name(Awareness a);

// Window-specific DPI (Phase 5): authoritative only after the HWND exists.
// Returns false when the OS API is unavailable (pre-1607) and leaves the
// outputs untouched.
bool window_dpi(HWND hwnd, UINT& dpi_x, UINT& dpi_y);

} // namespace dpi
} // namespace ptd
