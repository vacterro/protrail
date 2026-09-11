#pragma once

namespace ptd {

struct NormalizedRgb {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

// Pure helper: normalizes an 8-bit SDR channel in 0..255 to 0.0..1.0.
// Clamps defensively: values <= 0.0f or NaN -> 0.0f; values >= 255.0f -> 1.0f.
inline constexpr float normalize_color_channel(float c) noexcept {
    if (c != c || c <= 0.0f) return 0.0f;
    if (c >= 255.0f) return 1.0f;
    return c / 255.0f;
}

// Pure helper: normalizes click RGB channels from 0..255 to 0.0..1.0 exactly once.
// Used by both bubble outline/fill and particle rendering in OverlayWindow.
inline constexpr NormalizedRgb normalize_click_rgb(float r, float g, float b) noexcept {
    return NormalizedRgb{
        normalize_color_channel(r),
        normalize_color_channel(g),
        normalize_color_channel(b)
    };
}

} // namespace ptd
