#pragma once

#include "../effects/trail_config.h"
#include "../effects/click_config.h"

namespace ptd {

struct RenderConfig {
    bool diagnostic_primitives = false;

    bool operator==(const RenderConfig&) const = default;
};

struct AppConfig {
    // T-016: schema 3 adds trail style fields; T-017: schema 4 adds
    // click style fields (style/particle_amount).
    static constexpr int kCurrentSchemaVersion = 4;

    int schema_version = kCurrentSchemaVersion;
    bool master_enabled = true;
    TrailConfig trail{};
    ClickConfig click{};
    RenderConfig render{};

    static AppConfig validated(const AppConfig& in);
    bool operator==(const AppConfig&) const = default;
};

} // namespace ptd
