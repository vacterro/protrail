#pragma once

#include "trail_config.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ptd {

// Canonical 14-color effect palette (T-015 Phase 7).
// These colors are effect domain content, not Golden Default UI chrome.
struct PaletteColor {
    const char* name;
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

inline constexpr std::array<PaletteColor, 14> kEffectPalette = {{
    { "White",   255, 255, 255 },
    { "Red",     255,  64,  64 },
    { "Orange",  255, 128,  32 },
    { "Amber",   255, 190,  32 },
    { "Yellow",  255, 255,   0 },
    { "Lime",    180, 255,  32 },
    { "Green",    64, 220,  96 },
    { "Mint",     64, 255, 190 },
    { "Cyan",      0, 200, 255 },
    { "Sky",      64, 160, 255 },
    { "Blue",     64,  96, 255 },
    { "Violet",  150,  80, 255 },
    { "Magenta", 255,  64, 220 },
    { "Pink",    255, 120, 170 },
}};

inline constexpr int find_palette_index(uint8_t r, uint8_t g, uint8_t b) {
    for (std::size_t i = 0; i < kEffectPalette.size(); ++i) {
        if (kEffectPalette[i].r == r &&
            kEffectPalette[i].g == g &&
            kEffectPalette[i].b == b) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Global visual color presets (T-015 Phase 11).
// Applies Trail + Click colors only.
struct ColorPreset {
    const char* name;
    TrailColorMode trail_mode;
    uint8_t trail_start_r, trail_start_g, trail_start_b;
    uint8_t trail_fade_r,  trail_fade_g,  trail_fade_b;
    uint8_t click_r, click_g, click_b;
};

inline constexpr std::array<ColorPreset, 6> kColorPresets = {{
    { "Classic", TrailColorMode::Full,     255, 255,   0, 255, 255,   0,   0, 200, 255 },
    { "Fire",    TrailColorMode::Gradient, 255, 240,  96, 255,  64,  24, 255, 128,  32 },
    { "Ice",     TrailColorMode::Gradient, 224, 250, 255,  64, 128, 255,  64, 220, 255 },
    { "Neon",    TrailColorMode::Gradient,   0, 255, 220, 255,  64, 220,   0, 200, 255 },
    { "Toxic",   TrailColorMode::Gradient, 200, 255,  32,  48, 180,  64, 180, 255,  32 },
    { "Violet",  TrailColorMode::Gradient, 255,  96, 240, 112,  64, 255, 180,  96, 255 },
}};

} // namespace ptd
