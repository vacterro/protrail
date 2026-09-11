// T-007R A1/A2: overflow-safe QPC tick -> nanosecond conversion tests.
// Pure unit tests: no Qt, no Win32 runtime calls.
//
// These tests PROVE the timestamp contract (monotonic nanosecond output for
// long simulated uptimes). They deliberately do not rely on the
// CursorHistory test suite.

#include "../src/platform/timestamp.h"

#include <cstdio>
#include <cstdint>

namespace {

int g_failures = 0;
int g_checks = 0;

void expect_ns(long long ticks, long long freq, long long expected,
               const char* what) {
    ++g_checks;
    const long long got = static_cast<long long>(
        ptd::ticks_to_ns(static_cast<int64_t>(ticks), static_cast<int64_t>(freq)));
    if (got != expected) {
        ++g_failures;
        std::printf("FAIL %s: ticks=%lld freq=%lld got=%lld expected=%lld\n",
                    what, ticks, freq, got, expected);
    }
}

void expect_true(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s\n", what);
    }
}

// Monotonicity across an ascending tick sequence with a fixed frequency.
void expect_monotonic(long long freq, long long step, long long first,
                      long long last, const char* what) {
    ++g_checks;
    int64_t prev = ptd::ticks_to_ns(static_cast<int64_t>(first),
                                    static_cast<int64_t>(freq));
    for (long long t = first + step; t <= last; t += step) {
        const int64_t cur = ptd::ticks_to_ns(static_cast<int64_t>(t),
                                             static_cast<int64_t>(freq));
        if (cur <= prev) {
            ++g_failures;
            std::printf("FAIL %s: not strictly monotonic at ticks=%lld\n", what, t);
            return;
        }
        prev = cur;
    }
}

constexpr long long kNsPerSec = 1'000'000'000LL;

} // namespace

int main() {
    const long long kFreq10MHz = 10'000'000; // typical QPC frequency

    // Required A2 cases at the canonical 10 MHz frequency.
    expect_ns(0, kFreq10MHz, 0, "zero ticks");
    expect_ns(kFreq10MHz, kFreq10MHz, kNsPerSec, "one second");
    expect_ns(kFreq10MHz / 2, kFreq10MHz, kNsPerSec / 2, "half second");
    expect_ns(1, kFreq10MHz, 100, "single tick at 10 MHz = 100 ns");
    expect_ns(15'000'000'000LL, kFreq10MHz, 1'500'000'000'000LL,
              "beyond 15 minutes of counter time");
    expect_ns(36'000'000'000LL, kFreq10MHz, 3600LL * kNsPerSec, "one hour");
    expect_ns(864'000'000'000LL, kFreq10MHz, 86'400LL * kNsPerSec, "one day");
    expect_ns(6'048'000'000'000LL, kFreq10MHz, 604'800LL * kNsPerSec,
              "seven days");

    // Non-power-of-ten frequency rounding: 3'333'333 Hz, 1 tick.
    // Exact value: 1e9 / 3'333'333 = 300.000030000003 ns -> truncated.
    expect_ns(1, 3'333'333, 300, "truncating frequency conversion");

    // This value OVERFLOWS the previous implementation:
    // 100'000'000'000 ticks * 1e9 = 1e20 >> INT64_MAX (9.22e18).
    // Old code produced signed-overflow garbage; new code must return
    // exactly 10'000 seconds in ns. (UBSan-safe: old form was
    // multiplication-overflow UB, new form never multiplies ticks.)
    expect_ns(100'000'000'000LL, kFreq10MHz, 10'000LL * kNsPerSec,
              "would-overflow legacy conversion");

    // Ten million seconds (~115.7 days): legacy result would be 1e22.
    expect_ns(100'000'000'000'000LL, kFreq10MHz, 10'000'000LL * kNsPerSec,
              "multi-week uptime");

    // Defensive frequency handling (A1): QPF failure semantics.
    bool ok = true;
    expect_true(ptd::ticks_to_ns(123, 0, &ok) == 123 && !ok,
                "freq==0 -> monotonic raw-tick passthrough, freq_ok=false");
    ok = true;
    expect_true(ptd::ticks_to_ns(123, -5, &ok) == 123 && !ok,
                "negative freq -> passthrough, freq_ok=false");
    ok = false;
    expect_true(ptd::ticks_to_ns(123, kFreq10MHz, &ok) != 123 && ok,
                "valid freq reported as ok");

    // Extreme-but-valid monotonic sequences.
    expect_monotonic(kFreq10MHz, 1, 0, 2'000'000, "monotonic at single-tick steps");
    expect_monotonic(kFreq10MHz, 987'654'321, 0, 9'000'000'000'000LL,
                     "monotonic across multi-day jumps");
    expect_monotonic(1, 1, 0, 2'000'000, "monotonic at freq==1");
    expect_monotonic(0, 1, 0, 1'000'000, "passthrough stays monotonic");
    expect_monotonic(3'000'000'000LL, 7, 0, 14'000'000'000,
                     "monotonic at 3 GHz (sub-ns defensive branch)");

    // Legacy-overflow region must also stay strictly monotonic (no wrap):
    // simulate uptime-scale counter growth from 922 s onwards, one-second
    // steps for 1000 s beyond the old overflow point.
    {
        ++g_checks;
        int64_t prev = ptd::ticks_to_ns(9'220'000'000LL, kFreq10MHz);
        for (long long t = 9'220'000'001LL; t <= 10'220'000'000LL; ++t) {
            const int64_t cur = ptd::ticks_to_ns(t, kFreq10MHz);
            if (cur <= prev) {
                ++g_failures;
                std::printf("FAIL overflow-region monotonicity at %lld\n", t);
                break;
            }
            prev = cur;
        }
    }

    std::printf("test_timestamp: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
