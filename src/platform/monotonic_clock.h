#pragma once

#include <cstdint>

namespace ptd {

// Monotonic high-resolution clock: QueryPerformanceCounter-derived nanoseconds
// via the overflow-safe split conversion (timestamp.h). Never wall-clock.
//
// The clock used to live in src/platform/mouse_input.cpp, which made it
// unavailable to every translation unit that needs a monotonic timestamp
// without pulling in the raw-input window: the audio engine, the scheduler
// and their test targets all call now_ns() and none of them register input.
int64_t now_ns();

// Cached QueryPerformanceFrequency. QueryPerformanceFrequency cannot fail on
// Windows XP+ (it always writes a nonzero value), but we still check
// defensively (T-007R A1): a failed/zero frequency yields the monotonic
// raw-tick passthrough from ticks_to_ns instead of a division by zero.
int64_t qpc_freq();

} // namespace ptd
