#pragma once

// T-032: how this process was started.
//
// The distinction is a PRODUCT contract, not a cosmetic one:
//
//   Normal              a user launched ProTrail (double click, shortcut,
//                       Start menu). Open the ProTrail product window.
//   AutostartMinimized  Windows launched ProTrail from the per-user Run
//                       entry. Initialize services, tray and effect/render
//                       infrastructure, but NEVER show or activate the
//                       product window: no window flash, no taskbar
//                       button, no stolen keyboard focus. The tray icon is
//                       how the user reaches the product from there.
//
// Detection is a pure function of the command line so it is unit-testable
// without launching a process, and the silent path is a decision the caller
// reads off startup_mode_requests_product_window() rather than a scattered
// if-statement.

#include "../platform/autostart.h"

#include <string_view>

namespace ptd {

enum class StartupMode {
    Normal,
    AutostartMinimized,
};

// Tokenizes a Windows command line (double quotes honored, embedded quotes
// escaped) and returns AutostartMinimized when the startup argument is
// present anywhere after argv[0]. The argument match is case-insensitive:
// a registry command written by an older build in different casing must not
// silently become a visible window launch.
StartupMode parse_startup_mode(std::wstring_view command_line);

// The ONE authority for "may this launch show the product window by
// itself?". A manual launch opens the product window; an autostart launch
// does not, and the tray remains the only way in until the user asks.
constexpr bool startup_mode_requests_product_window(StartupMode mode) {
    return mode == StartupMode::Normal;
}

// True for the tray-only / non-activating launch path. Named separately so
// call sites read as intent rather than as an enum comparison.
constexpr bool startup_mode_is_silent(StartupMode mode) {
    return mode == StartupMode::AutostartMinimized;
}

} // namespace ptd
