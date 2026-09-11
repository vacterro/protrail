#pragma once

#include <cstdint>

namespace ptd {

// Normalized mouse button identity for a CursorSample transition.
enum class MouseButton : uint8_t { None = 0, Left, Right, Middle };

// Normalized button action. None marks a movement-only sample.
enum class ButtonAction : uint8_t { None = 0, Down, Up };

// One cursor observation. The coordinate space is the Windows virtual screen
// in physical pixels (GetCursorPos), which may be negative on multi-monitor
// layouts whose left/top monitor sits left of/above the primary.
//
// timestamp_ns is monotonic and high resolution (QueryPerformanceCounter
// derived); it never uses wall-clock time.
//
// action == ButtonAction::None is a movement/activity sample. Otherwise
// button/action carry exactly one normalized button transition. The struct is
// deliberately minimal but extensible (future: wheel, X buttons) without
// changing existing fields.
struct CursorSample {
    int64_t timestamp_ns = 0;
    int32_t x = 0;
    int32_t y = 0;
    MouseButton button = MouseButton::None;
    ButtonAction action = ButtonAction::None;
};

} // namespace ptd
