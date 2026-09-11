#pragma once

#include <cstdint>

namespace ptd {

// Overflow-safe QueryPerformanceCounter tick -> nanosecond conversion
// (T-007R A1/A2). Pure function: no Win32 calls, unit-testable.
//
// Root cause being fixed: the naive form
//
//     ticks * 1'000'000'000 / freq
//
// overflows int64_t once ticks > INT64_MAX / 1e9 (~9.22e9 ticks). At the
// usual 10 MHz QPC frequency that is ~922 s of counter time. QPC reflects
// system-uptime-scale counter values, not time since ProTrail startup, so
// the naive conversion produces garbage (even negative) timestamps on any
// normally long-running system.
//
// Strategy:
//   seconds   = ticks / freq              (exact)
//   remainder = ticks % freq              (exact, 0 <= remainder < freq)
//   ns        = seconds * 1'000'000'000
//             + remainder * 1'000'000'000 / freq
//
// remainder * 1e9 is exact in int64 for freq <= 1e9 (product <= 1e18) and
// still exact for freq < 2e9 (product < 2e18). Real QPC frequencies on
// Windows are <= 10 MHz, comfortably inside the exact range. For a
// hypothetical sub-nanosecond tick (freq >= 2e9, never produced by QPC
// today) the remainder is truncated first; the result stays monotonic and
// loses less than one nanosecond per second.
//
// seconds * 1e9 would itself overflow only beyond ~292 years of counter
// time at freq == 1; documented physical bound, not guarded.
//
// freq == 0 (QueryPerformanceFrequency failure) is handled defensively:
// raw ticks are returned unchanged. They remain strictly monotonic -- the
// timestamp contract ProTrail depends on -- they are just not scaled to
// nanoseconds. *freq_ok reports which case happened.
inline int64_t ticks_to_ns(int64_t ticks, int64_t freq, bool* freq_ok = nullptr) {
    constexpr int64_t kNsPerSecond = 1'000'000'000;

    if (freq_ok) *freq_ok = freq > 0;
    if (freq <= 0) return ticks;  // QPF failed: monotonic passthrough

    const int64_t seconds   = ticks / freq;
    const int64_t remainder = ticks % freq;

    if (freq < 2 * kNsPerSecond) {
        // Exact range: remainder * 1e9 < 2e18 fits int64_t.
        return seconds * kNsPerSecond + (remainder * kNsPerSecond) / freq;
    }

    // Defensive sub-nanosecond tick: divide first, stay monotonic, lose
    // strictly less than 1 ns per second. ticks_per_ns >= 2 here, so no
    // division by zero.
    const int64_t ticks_per_ns = freq / kNsPerSecond;
    return seconds * kNsPerSecond + remainder / ticks_per_ns;
}

} // namespace ptd
