// T-007R A5: pure multi-transition RAWINPUT button-flag parser tests.
// No Qt, no Win32 runtime calls (only RI_MOUSE_* constants from the
// included headers), so this drags no Win32 runtime dependency into the
// unit-test binary.

#include "../src/platform/button_flags.h"

#include <cstdio>

namespace {

int g_failures = 0;
int g_checks = 0;

void expect_transitions(USHORT flags, int want_count,
                        const ptd::ButtonTransition* want, const char* what) {
    ++g_checks;
    ptd::ButtonTransition got[ptd::kMaxButtonTransitions] = {};
    const int count = ptd::extract_button_transitions(flags, got);
    if (count != want_count) {
        ++g_failures;
        std::printf("FAIL %s: count=%d want=%d\n", what, count, want_count);
        return;
    }
    for (int i = 0; i < want_count; ++i) {
        if (got[i].button != want[i].button || got[i].action != want[i].action) {
            ++g_failures;
            std::printf("FAIL %s: transition %d mismatch\n", what, i);
            return;
        }
    }
}

void expect_true(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s\n", what);
    }
}

using ptd::ButtonAction;
using ptd::ButtonTransition;
using ptd::MouseButton;

const ButtonTransition kLD = {MouseButton::Left, ButtonAction::Down};
const ButtonTransition kLU = {MouseButton::Left, ButtonAction::Up};
const ButtonTransition kRD = {MouseButton::Right, ButtonAction::Down};
const ButtonTransition kRU = {MouseButton::Right, ButtonAction::Up};
const ButtonTransition kMD = {MouseButton::Middle, ButtonAction::Down};
const ButtonTransition kMU = {MouseButton::Middle, ButtonAction::Up};

} // namespace

int main() {
    using ptd::extract_button_transitions;
    using ptd::kMaxButtonTransitions;

    expect_transitions(0, 0, nullptr, "no flags");
    expect_transitions(RI_MOUSE_LEFT_BUTTON_DOWN, 1, &kLD, "single left down");
    expect_transitions(RI_MOUSE_LEFT_BUTTON_UP, 1, &kLU, "single left up");
    expect_transitions(RI_MOUSE_RIGHT_BUTTON_DOWN, 1, &kRD, "single right down");
    expect_transitions(RI_MOUSE_RIGHT_BUTTON_UP, 1, &kRU, "single right up");
    expect_transitions(RI_MOUSE_MIDDLE_BUTTON_DOWN, 1, &kMD, "single middle down");
    expect_transitions(RI_MOUSE_MIDDLE_BUTTON_UP, 1, &kMU, "single middle up");

    // THE A5 REGRESSION: old parser returned Left-Down and silently dropped
    // Right-Up from the same packet.
    const ButtonTransition kLRmix[] = {kLD, kRU};
    expect_transitions(RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_UP,
                       2, kLRmix, "left-down + right-up in one packet");

    const ButtonTransition kMRmix[] = {kRD, kMU};
    expect_transitions(RI_MOUSE_RIGHT_BUTTON_DOWN | RI_MOUSE_MIDDLE_BUTTON_UP,
                       2, kMRmix, "right-down + middle-up in one packet");

    // All six at once (a packet can in principle carry all flags).
    const ButtonTransition kAll[] = {kLD, kLU, kRD, kRU, kMD, kMU};
    expect_transitions(0x003F, 6, kAll, "all L/R/M down+up bits in one packet");

    // Wheel and X-button bits must not disturb button parsing.
    const ButtonTransition kWheelLD[] = {kLD};
    expect_transitions(0x0401, 1, kWheelLD, "wheel flag does not stop parsing");

    // Stationary-transition validity: a button-only packet (no movement)
    // still yields its transition -- the caller supplies GetCursorPos.
    ptd::ButtonTransition stationary[kMaxButtonTransitions] = {};
    expect_true(extract_button_transitions(RI_MOUSE_MIDDLE_BUTTON_UP,
                                           stationary) == 1,
                "middle-up extracted (stationary packet case)");

    // Capacity constant sanity.
    expect_true(kMaxButtonTransitions == 6, "capacity constant is 6");

    std::printf("test_button_flags: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
