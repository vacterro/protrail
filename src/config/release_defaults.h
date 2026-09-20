#pragma once

#include "app_config.h"

namespace ptd {

// T-33 Canonical Release Defaults.
//
// There is exactly ONE semantic source of the product's default
// configuration: resources/release_defaults.json, validated at build time and
// embedded into the executable as a Qt resource (:/defaults/release_defaults.json).
// Both consumers read it through this owner:
//
//   1. fresh configuration creation (a missing or malformed user config file),
//   2. the explicit Restore Defaults / Restore All Defaults paths.
//
// Before T-33 those two disagreed: the C++ struct initializers were the
// de-facto defaults, and nothing stopped a release preset from being tuned
// separately. One reader, one authority, no second copy.
//
// The accessor returns a cached, fully validated AppConfig. The ONLY fallback
// is the struct-initializer AppConfig{} and it can be reached solely when the
// embedded resource is unreadable or malformed -- a state the build gate
// (cmake/validate_release_defaults.cmake) makes unreachable for a real build,
// and the default-coverage regression (T-35) proves can never become a second
// drifting authority.

// The canonical defaults. Parsed once, validated through the normal
// ConfigStorage pipeline, then cached.
const AppConfig& release_defaults();

// True when the embedded canonical source parsed and validated. A false here
// means the emergency struct fallback answered; tests assert it is true.
bool release_defaults_available();

// The Qt resource path the executable reads. Exposed for diagnostics and so a
// regression can prove the packaged app never reads a writable source-tree
// path.
const char* release_defaults_resource_path();

} // namespace ptd
