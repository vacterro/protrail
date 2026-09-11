#pragma once

// Pure RAWINPUT button-flag parsing (T-007R A5).
//
// Defect being fixed: a single RAWINPUT packet's usButtonFlags word can
// carry MORE than one button transition (e.g. left-down arriving in the
// same packet as right-up at high event rates). The MVP 02 parser returned
// after the first matching flag, so every other transition in the packet
// silently disappeared.
//
// This parser preserves ALL left/right/middle down/up bits present in the
// packet, in flag-bit order. It is a pure function with no Win32 runtime
// calls (only the RI_MOUSE_* constants), so it is safe to unit test.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstdint>

#include "../core/cursor_sample.h"

namespace ptd {

// One normalized button transition extracted from a RAWINPUT packet.
struct ButtonTransition {
    MouseButton button;
    ButtonAction action;
};

// Upper bound: L/R/M x down/up = 6 transitions in one packet.
constexpr int kMaxButtonTransitions = 6;

// Extracts all L/R/M down/up transitions present in `flags`. Returns the
// number of entries written to `out` (0..kMaxButtonTransitions), in
// flag-bit order (left-down, left-up, right-down, right-up, middle-down,
// middle-up). Wheel / X-button bits are intentionally ignored (MVP 02
// scope); they do not stop button parsing.
inline int extract_button_transitions(USHORT flags,
                                      ButtonTransition (&out)[kMaxButtonTransitions]) {
    static_assert(RI_MOUSE_LEFT_BUTTON_DOWN   == 0x0001 &&
                  RI_MOUSE_LEFT_BUTTON_UP     == 0x0002 &&
                  RI_MOUSE_RIGHT_BUTTON_DOWN  == 0x0004 &&
                  RI_MOUSE_RIGHT_BUTTON_UP    == 0x0008 &&
                  RI_MOUSE_MIDDLE_BUTTON_DOWN == 0x0010 &&
                  RI_MOUSE_MIDDLE_BUTTON_UP   == 0x0020,
                  "RI_MOUSE flag values changed; revisit the table below");

    struct Entry { USHORT flag; MouseButton button; ButtonAction action; };
    static constexpr Entry kTable[] = {
        {RI_MOUSE_LEFT_BUTTON_DOWN,   MouseButton::Left,   ButtonAction::Down},
        {RI_MOUSE_LEFT_BUTTON_UP,     MouseButton::Left,   ButtonAction::Up},
        {RI_MOUSE_RIGHT_BUTTON_DOWN,  MouseButton::Right,  ButtonAction::Down},
        {RI_MOUSE_RIGHT_BUTTON_UP,    MouseButton::Right,  ButtonAction::Up},
        {RI_MOUSE_MIDDLE_BUTTON_DOWN, MouseButton::Middle, ButtonAction::Down},
        {RI_MOUSE_MIDDLE_BUTTON_UP,   MouseButton::Middle, ButtonAction::Up},
    };

    int count = 0;
    for (const Entry& e : kTable) {
        if (count >= kMaxButtonTransitions) break;
        if (flags & e.flag) {
            out[count].button = e.button;
            out[count].action = e.action;
            ++count;
        }
    }
    return count;
}

} // namespace ptd
