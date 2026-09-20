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
    // click style fields (style/particle_amount); T-021: schema 5 adds
    // the trail sparkle decoration fields (sparkle_mode/amount/size/spread);
    // T-022: schema 6 adds the elemental click field (click.element_tint)
    // alongside the four new ClickStyle values Air/Fire/Water/Earth;
    // T-023: schema 7 adds TrailSparkleMode::Shards (value 5);
    // T-024: schema 8 adds the press-and-hold gesture field
    // (click.hold_enabled).
    // A schema-4 file migrates by leaving the new fields at their struct
    // defaults (sparkle Off + default amount/size/spread); a schema-5 file
    // migrates by leaving element_tint at its 0.65 default; a schema-6 file
    // migrates by keeping its sparkle mode inside the historical 0..4 set.
    // Every pre-existing Trail/Click value is loaded unchanged in all cases.
    //
    // ENUM INTRODUCTION BOUNDARIES (T-023). Each newly introduced enum value
    // is legitimate only from the schema that introduced it onward, so a
    // corrupt value persisted by an OLDER writer can never be silently
    // reinterpreted as a newly added visual effect:
    //   ClickStyle       7..10 (Air/Fire/Water/Earth) -- schema >= 6
    //   TrailSparkleMode 5     (Shards)               -- schema >= 7
    // Out-of-range-for-source-schema values repair to the documented safe
    // default (ClickStyle::Ring, TrailSparkleMode::Off). ConfigStorage owns
    // the enforcement; see config_storage.cpp.
    //
    // T-027: schema 9 adds the Hold Controls / Motion Wake block
    // (click.hold_wake_enabled + the four Hold multipliers).
    // T-032: schema 10 adds the application-level Start with Windows
    // preference (start_with_windows).
    // T-36: schema 11 adds the Advanced Motion Wake block
    // (click.wake_strength/wake_size/wake_spread/speed_response/
    // min_motion_speed_px_s/turn_accent/stop_accent).
    static constexpr int kCurrentSchemaVersion = 11;

    // Schema at which each late-introduced enum family became legitimate.
    static constexpr int kElementalClickStyleSchema = 6;  // ClickStyle 7..10
    static constexpr int kShardsSparkleModeSchema = 7;    // TrailSparkleMode 5

    // T-024 HOLD FX INTRODUCTION BOUNDARY. Unlike the enum boundaries above,
    // this one governs a BEHAVIOUR default rather than a value range, and the
    // two directions deliberately disagree:
    //
    //   fresh schema-8 default  -> Hold FX ON  (ClickConfig::hold_enabled)
    //   historical schema <= 7  -> Hold FX OFF
    //
    // A new install gets the feature. An existing user who merely upgraded
    // the executable does NOT silently acquire a new mouse gesture: their
    // config predates the field, so the absence of the key is read as OFF,
    // not as the struct default. Once they enable it and save, schema 8
    // persists hold_enabled = true and reload preserves it. ConfigStorage
    // owns the enforcement; see config_storage.cpp.
    static constexpr int kHoldFxSchema = 8;

    // T-027 HOLD CONTROLS / MOTION WAKE INTRODUCTION BOUNDARY.
    //
    // SOURCE SCHEMA AUTHORITATIVE, the same discipline the enum boundaries
    // above use: the five T-27 fields (hold_wake_enabled + the four
    // multipliers) exist only from schema 9 onward. A file written by a
    // schema <= 8 writer cannot carry T-027 semantics at all, so every one of
    // those keys is IGNORED there -- Motion Wake stays OFF and the
    // multipliers keep their struct defaults (1.0 identity, baseline Wake
    // Life). A key present in a historical file is corruption or an unknown
    // field, never a user decision.
    //
    // From schema 9 the keys load normally: present values are honoured and
    // clamped by ClickConfig::validated(), absent ones keep the schema-9
    // struct defaults, and saving writes the current schema.
    //
    // hold_enabled keeps loading according to the schema-8 rule unchanged;
    // schema 9 does NOT reinterpret it, and does NOT reinterpret any
    // historical enum value. ConfigStorage owns the enforcement; see
    // config_storage.cpp.
    static constexpr int kHoldWakeSchema = 9;

    // T-032 START WITH WINDOWS INTRODUCTION BOUNDARY.
    //
    // This is a behaviour boundary of exactly the kind the T-024 Hold FX
    // rule covers, so it migrates the same asymmetric way:
    //
    //   fresh default           -> OFF
    //   historical schema <= 9  -> OFF
    //
    // Both directions happen to agree today, and that is not an accident:
    // autostart is an OS SIDE EFFECT, so enabling it retroactively for an
    // existing user would change what their machine does at sign-in without
    // them ever choosing it. A previously written config therefore loads
    // OFF even though the key is absent, and the value only ever becomes
    // true when the user themselves turns the setting on and ProTrail
    // persists it.
    //
    // The key is honoured at ANY source schema when it is genuinely present:
    // a value the user's own machine wrote is a decision, never something to
    // re-derive. ConfigStorage owns the enforcement; see config_storage.cpp.
    static constexpr int kStartWithWindowsSchema = 10;

    // T-36 ADVANCED MOTION WAKE INTRODUCTION BOUNDARY.
    //
    // SOURCE SCHEMA AUTHORITATIVE, the same discipline as kHoldWakeSchema: the
    // seven T-36 fields exist only from schema 11 onward. A file written by a
    // schema <= 10 writer cannot carry T-36 semantics, so every one of those
    // keys is IGNORED there -- the four multipliers keep their 1.0 identity,
    // min_motion_speed stays 0 and both accents stay OFF, which reproduces the
    // accepted T-26/T-27 appearance for an upgraded user. From schema 11 the
    // keys load normally and are clamped by ClickConfig::validated().
    // ConfigStorage owns the enforcement; see config_storage.cpp.
    static constexpr int kAdvancedMotionWakeSchema = 11;

    int schema_version = kCurrentSchemaVersion;
    bool master_enabled = true;

    // T-032: register ProTrail in the per-user Windows Run key. Purely a
    // persisted user PREFERENCE: the machine-specific registry state (the
    // registered command, the executable path, the registry key path) is
    // deliberately NOT part of this configuration, so a config copied to
    // another machine can never claim to describe that machine's startup
    // entries.
    bool start_with_windows = false;
    TrailConfig trail{};
    ClickConfig click{};
    RenderConfig render{};

    static AppConfig validated(const AppConfig& in);
    bool operator==(const AppConfig&) const = default;
};

} // namespace ptd
