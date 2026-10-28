#include "monotonic_clock.h"

#include "timestamp.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace ptd {
namespace {

// Cached QPC frequency. QueryPerformanceFrequency cannot fail on Windows
// XP+ (it always writes a nonzero value), but we still check defensively
// (T-007R A1): a failed/zero frequency yields the monotonic raw-tick
// passthrough from ticks_to_ns instead of a division by zero.
int64_t qpc_frequency() {
    static int64_t freq = [] {
        LARGE_INTEGER f{};
        QueryPerformanceCounter(&f); // ensure QPC is usable on this machine
        if (!QueryPerformanceFrequency(&f)) return int64_t{0};
        return f.QuadPart;
    }();
    return freq;
}

} // namespace

int64_t qpc_freq() { return qpc_frequency(); }

int64_t now_ns() {
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    // Overflow-safe split conversion (seconds + remainder). The previous
    // c.QuadPart * 1e9 / freq form overflowed after ~922 s of QPC counter
    // time at 10 MHz. See timestamp.h for the proof.
    return ticks_to_ns(c.QuadPart, qpc_frequency());
}

} // namespace ptd
