#include "config_storage.h"
#include "release_defaults.h"
#include "../core/log.h"

#include <Windows.h>
#include <ShlObj.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <tuple>

namespace ptd {

namespace {
 // CORE-001 test seam: one path whose open/read is forced to fail. Empty in
 // production; never consulted unless a test armed it.
 std::wstring g_forced_read_failure_path;
 // W2-002 test seam: one path whose save is forced to fail (empty = every
 // save). Empty in production; never consulted unless a test armed it.
 std::wstring g_forced_save_failure_path;
 bool g_forced_save_failure_armed = false;
  // CORE-001 probe seam: injected existence check result + error_code.
 ConfigStorage::FilesystemProbeFn g_probe_fn = nullptr;
 // W2-005 test seam: durable load attempt counter.
 int g_load_count = 0;

// Extracts the numeric schema_version from raw JSON bytes without applying any
// current-schema semantics. Used to detect a future schema BEFORE it is
// treated as ordinary/corrupt content. Returns std::nullopt when the document
// is not an object or carries no numeric schema_version.
std::optional<int> peek_schema_version(const QByteArray& json) {
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (doc.isNull() || !doc.isObject()) return std::nullopt;
    const QJsonObject root = doc.object();
    if (root.contains("schema_version") && root["schema_version"].isDouble()) {
        return root["schema_version"].toInt();
    }
    return std::nullopt;
}
} // namespace

void ConfigStorage::set_read_failure_path_for_tests(const std::wstring& path) {
    g_forced_read_failure_path = path;
}

void ConfigStorage::clear_read_failure_path_for_tests() {
    g_forced_read_failure_path.clear();
}

void ConfigStorage::set_save_failure_path_for_tests(const std::wstring& path) {
    g_forced_save_failure_path = path;
    g_forced_save_failure_armed = true;
}

void ConfigStorage::clear_save_failure_path_for_tests() {
    g_forced_save_failure_path.clear();
    g_forced_save_failure_armed = false;
}

void ConfigStorage::set_filesystem_probe_for_tests(FilesystemProbeFn fn) {
    g_probe_fn = std::move(fn);
}

void ConfigStorage::clear_filesystem_probe_for_tests() {
    g_probe_fn = nullptr;
}

// W2-005 test seam.
void ConfigStorage::reset_load_count_for_tests() {
    g_load_count = 0;
}

int ConfigStorage::load_count_for_tests() {
    return g_load_count;
}

std::wstring ConfigStorage::default_config_path() {
    std::wstring base;
    if (ptd::local_app_data_folder(base) && !base.empty()) {
        std::filesystem::path dir = base;
        dir /= L"ProTrail";
        dir /= L"config.json";
        return dir.wstring();
    }
    // T-030: a known-folder failure must never silently turn user config into
    // a working-directory-relative file (the process working directory is
    // launcher-controlled and can differ between launches). Log the failure
    // and use the ONE documented deterministic fallback: beside the
    // executable.
    const std::wstring exe_dir = ptd::executable_directory();
    if (exe_dir.empty()) {
        ptd::log_write(ptd::LogLevel::Error,
                       "config: executable path resolution failed; using absolute fallback");
    }
    const std::filesystem::path fallback =
        (exe_dir.empty() ? std::filesystem::temp_directory_path()
                         : std::filesystem::path(exe_dir)) / L"config.json";
    const std::u8string u8 = fallback.u8string();
    ptd::log_write(ptd::LogLevel::Warn,
        std::string("config: %LOCALAPPDATA% unavailable; using "
                    "executable-relative fallback ") +
            std::string(reinterpret_cast<const char*>(u8.data()), u8.size()));
    return fallback.wstring();
}

QByteArray ConfigStorage::serialize_json(const AppConfig& config) {
    QJsonObject root;
    root["schema_version"] = config.schema_version;
    root["master_enabled"] = config.master_enabled;
    // T-032 schema 10: the persisted Start with Windows PREFERENCE. The
    // registered command and the executable path are machine state and are
    // deliberately not written here.
    root["start_with_windows"] = config.start_with_windows;

    QJsonObject trail;
    trail["enabled"] = config.trail.enabled;
    trail["color_mode"] = static_cast<int>(config.trail.color_mode);
    trail["start_color_r"] = config.trail.start_color_r;
    trail["start_color_g"] = config.trail.start_color_g;
    trail["start_color_b"] = config.trail.start_color_b;
    trail["fade_color_r"] = config.trail.fade_color_r;
    trail["fade_color_g"] = config.trail.fade_color_g;
    trail["fade_color_b"] = config.trail.fade_color_b;
    trail["style"] = static_cast<int>(config.trail.style);
    trail["glow_strength"] = static_cast<double>(config.trail.glow_strength);
    trail["segment_spacing_px"] = static_cast<double>(config.trail.segment_spacing_px);
    trail["head_thickness_px"] = static_cast<double>(config.trail.head_thickness_px);
    trail["tail_thickness_px"] = static_cast<double>(config.trail.tail_thickness_px);
    trail["taper_strength"] = static_cast<double>(config.trail.taper_strength);
    trail["lifetime_ms"] = static_cast<double>(config.trail.lifetime_ms);
    trail["base_opacity"] = static_cast<double>(config.trail.base_opacity);
    trail["smoothing"] = static_cast<double>(config.trail.smoothing);
    trail["fade_start"] = static_cast<double>(config.trail.fade_start);
    trail["fade_curve"] = static_cast<int>(config.trail.fade_curve);
    // T-021 schema 5: sparkle decoration overlay.
    trail["sparkle_mode"] = static_cast<int>(config.trail.sparkle_mode);
    trail["sparkle_amount"] = static_cast<double>(config.trail.sparkle_amount);
    trail["sparkle_size_px"] = static_cast<double>(config.trail.sparkle_size_px);
    trail["sparkle_spread_px"] = static_cast<double>(config.trail.sparkle_spread_px);
    root["trail"] = trail;

    QJsonObject click;
    click["enabled"] = config.click.enabled;
    click["trigger_left"] = config.click.trigger_left;
    click["trigger_right"] = config.click.trigger_right;
    click["trigger_middle"] = config.click.trigger_middle;
    click["color_r"] = config.click.color_r;
    click["color_g"] = config.click.color_g;
    click["color_b"] = config.click.color_b;
    click["style"] = static_cast<int>(config.click.style);
    click["particle_amount"] = config.click.particle_amount;
    click["element_tint"] = config.click.element_tint;  // T-022 schema 6
    click["hold_enabled"] = config.click.hold_enabled;  // T-024 schema 8
    // T-026/T-027 schema 9: Hold Controls / Motion Wake.
    click["hold_wake_enabled"] = config.click.hold_wake_enabled;
    click["hold_intensity"] = static_cast<double>(config.click.hold_intensity);
    click["hold_wake_density"] = static_cast<double>(config.click.hold_wake_density);
    click["hold_wake_lifetime_ms"] =
        static_cast<double>(config.click.hold_wake_lifetime_ms);
    click["hold_release_strength"] =
        static_cast<double>(config.click.hold_release_strength);
    // T-36 schema 11: Advanced Motion Wake.
    click["wake_strength"] = static_cast<double>(config.click.wake_strength);
    click["wake_size"] = static_cast<double>(config.click.wake_size);
    click["wake_spread"] = static_cast<double>(config.click.wake_spread);
    click["speed_response"] = static_cast<double>(config.click.speed_response);
    click["min_motion_speed_px_s"] =
        static_cast<double>(config.click.min_motion_speed_px_s);
    click["turn_accent"] = config.click.turn_accent;
    click["stop_accent"] = config.click.stop_accent;
    click["start_radius_px"] = static_cast<double>(config.click.start_radius_px);
    click["end_radius_px"] = static_cast<double>(config.click.end_radius_px);
    click["duration_ms"] = static_cast<double>(config.click.duration_ms);
    click["base_opacity"] = static_cast<double>(config.click.base_opacity);
    click["outline_thickness_px"] = static_cast<double>(config.click.outline_thickness_px);
    click["fill_opacity"] = static_cast<double>(config.click.fill_opacity);
    click["easing"] = static_cast<int>(config.click.easing);
    root["click"] = click;

    QJsonObject render;
    render["diagnostic_primitives"] = config.render.diagnostic_primitives;
    root["render"] = render;

    // After a migrated or edited config is saved, write schema version 2 (T-015 Phase 6).
    root["schema_version"] = AppConfig::kCurrentSchemaVersion;

    QJsonDocument doc(root);
    return doc.toJson(QJsonDocument::Indented);
}

std::optional<AppConfig> ConfigStorage::deserialize_json(const QByteArray& json, QString* error_msg) {
    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (doc.isNull() || !doc.isObject()) {
        if (error_msg) *error_msg = err.errorString();
        return std::nullopt;
    }

    QJsonObject root = doc.object();
    AppConfig cfg{};

    if (root.contains("schema_version") && root["schema_version"].isDouble()) {
        cfg.schema_version = root["schema_version"].toInt();
    }
    if (root.contains("master_enabled") && root["master_enabled"].isBool()) {
        cfg.master_enabled = root["master_enabled"].toBool();
    }
    // T-032 schema 10: Start with Windows. An explicit key is honoured at any
    // source schema -- it is a decision the user's own machine recorded.
    // When the key is absent the configuration predates the feature (or is
    // being created fresh) and the value stays OFF: upgrading the executable
    // must never enroll an existing user into Windows startup. ConfigStorage
    // owns the enforcement; AppConfig::kStartWithWindowsSchema documents the
    // boundary.
    if (root.contains("start_with_windows") && root["start_with_windows"].isBool()) {
        cfg.start_with_windows = root["start_with_windows"].toBool();
    } else if (cfg.schema_version < AppConfig::kStartWithWindowsSchema) {
        cfg.start_with_windows = false;
    }

    if (root.contains("trail") && root["trail"].isObject()) {
        QJsonObject t = root["trail"].toObject();
        if (t.contains("enabled") && t["enabled"].isBool())
            cfg.trail.enabled = t["enabled"].toBool();

        // Schema 1 migration (T-015 Phase 6) vs Schema 2+ (start/fade dual colors + color_mode)
        if (t.contains("color_r") && t["color_r"].isDouble()) {
            // color_r = R, color_g = G, color_b = B
            // migrate to:
            // start/head = R,G,B; fade/tail = R,G,B; mode = Full
            const uint8_t r = static_cast<uint8_t>(std::clamp(t["color_r"].toInt(), 0, 255));
            uint8_t g = 255;
            if (t.contains("color_g") && t["color_g"].isDouble())
                g = static_cast<uint8_t>(std::clamp(t["color_g"].toInt(), 0, 255));
            uint8_t b = 0;
            if (t.contains("color_b") && t["color_b"].isDouble())
                b = static_cast<uint8_t>(std::clamp(t["color_b"].toInt(), 0, 255));

            cfg.trail.start_color_r = r;
            cfg.trail.start_color_g = g;
            cfg.trail.start_color_b = b;
            cfg.trail.fade_color_r = r;
            cfg.trail.fade_color_g = g;
            cfg.trail.fade_color_b = b;
            cfg.trail.color_mode = TrailColorMode::Full;
        } else {
            if (t.contains("start_color_r") && t["start_color_r"].isDouble())
                cfg.trail.start_color_r = static_cast<uint8_t>(std::clamp(t["start_color_r"].toInt(), 0, 255));
            if (t.contains("start_color_g") && t["start_color_g"].isDouble())
                cfg.trail.start_color_g = static_cast<uint8_t>(std::clamp(t["start_color_g"].toInt(), 0, 255));
            if (t.contains("start_color_b") && t["start_color_b"].isDouble())
                cfg.trail.start_color_b = static_cast<uint8_t>(std::clamp(t["start_color_b"].toInt(), 0, 255));

            if (t.contains("fade_color_r") && t["fade_color_r"].isDouble())
                cfg.trail.fade_color_r = static_cast<uint8_t>(std::clamp(t["fade_color_r"].toInt(), 0, 255));
            if (t.contains("fade_color_g") && t["fade_color_g"].isDouble())
                cfg.trail.fade_color_g = static_cast<uint8_t>(std::clamp(t["fade_color_g"].toInt(), 0, 255));
            if (t.contains("fade_color_b") && t["fade_color_b"].isDouble())
                cfg.trail.fade_color_b = static_cast<uint8_t>(std::clamp(t["fade_color_b"].toInt(), 0, 255));

            if (t.contains("color_mode") && t["color_mode"].isDouble()) {
                const int cm = t["color_mode"].toInt();
                if (cm >= 0 && cm <= 3) {
                    cfg.trail.color_mode = static_cast<TrailColorMode>(cm);
                } else {
                    cfg.trail.color_mode = TrailColorMode::Full;
                }
            }
        }

        if (t.contains("style") && t["style"].isDouble()) {
            const int st = t["style"].toInt();
            if (st >= 0 && st <= 7) {
                cfg.trail.style = static_cast<TrailStyle>(st);
            }
        }
        if (t.contains("glow_strength") && t["glow_strength"].isDouble())
            cfg.trail.glow_strength = static_cast<float>(t["glow_strength"].toDouble());
        if (t.contains("segment_spacing_px") && t["segment_spacing_px"].isDouble())
            cfg.trail.segment_spacing_px = static_cast<float>(t["segment_spacing_px"].toDouble());

        if (t.contains("head_thickness_px") && t["head_thickness_px"].isDouble())
            cfg.trail.head_thickness_px = static_cast<float>(t["head_thickness_px"].toDouble());
        if (t.contains("tail_thickness_px") && t["tail_thickness_px"].isDouble())
            cfg.trail.tail_thickness_px = static_cast<float>(t["tail_thickness_px"].toDouble());
        if (t.contains("taper_strength") && t["taper_strength"].isDouble())
            cfg.trail.taper_strength = static_cast<float>(t["taper_strength"].toDouble());
        if (t.contains("lifetime_ms") && t["lifetime_ms"].isDouble())
            cfg.trail.lifetime_ms = static_cast<float>(t["lifetime_ms"].toDouble());
        if (t.contains("base_opacity") && t["base_opacity"].isDouble())
            cfg.trail.base_opacity = static_cast<float>(t["base_opacity"].toDouble());
        if (t.contains("smoothing") && t["smoothing"].isDouble())
            cfg.trail.smoothing = static_cast<float>(t["smoothing"].toDouble());
        if (t.contains("fade_start") && t["fade_start"].isDouble())
            cfg.trail.fade_start = static_cast<float>(t["fade_start"].toDouble());
        if (t.contains("fade_curve") && t["fade_curve"].isDouble()) {
            const int fc = t["fade_curve"].toInt();
            if (fc >= 0 && fc <= 2)
                cfg.trail.fade_curve = static_cast<FadeCurve>(fc);
        }

        // T-021 schema 5: sparkle decoration fields. A schema-4 file simply
        // has none of these keys, so the struct defaults stand (mode Off +
        // default amount/size/spread) -- that IS the documented 4 -> 5
        // migration, and it touches no pre-existing field. Invalid enum
        // values are repaired by TrailConfig::validated() below.
        //
        // T-023 schema 7: TrailSparkleMode::Shards (5) is NEW. Before
        // schema 7 the only legitimate sparkle modes were 0..4, and the
        // documented safety contract repaired anything else to Off. So a
        // file WRITTEN BY AN OLDER SCHEMA that carries sparkle_mode 5 is
        // corruption, not Shards, and must keep repairing to Off --
        // otherwise adding an enumerator silently rewrites the meaning of
        // configurations that already exist on disk. The accepted range is
        // therefore keyed to the SOURCE schema, never to the current enum.
        if (t.contains("sparkle_mode") && t["sparkle_mode"].isDouble()) {
            const int sm = t["sparkle_mode"].toInt();
            const int max_sparkle_mode =
                cfg.schema_version >= AppConfig::kShardsSparkleModeSchema
                    ? static_cast<int>(TrailSparkleMode::Shards)
                    : static_cast<int>(TrailSparkleMode::Firefly);
            if (sm >= 0 && sm <= max_sparkle_mode) {
                cfg.trail.sparkle_mode = static_cast<TrailSparkleMode>(sm);
            }
            // Out of range for the SOURCE schema: the struct default (Off)
            // stands, which is exactly the pre-T-023 repair behavior.
        }
        if (t.contains("sparkle_amount") && t["sparkle_amount"].isDouble())
            cfg.trail.sparkle_amount = static_cast<float>(t["sparkle_amount"].toDouble());
        if (t.contains("sparkle_size_px") && t["sparkle_size_px"].isDouble())
            cfg.trail.sparkle_size_px = static_cast<float>(t["sparkle_size_px"].toDouble());
        if (t.contains("sparkle_spread_px") && t["sparkle_spread_px"].isDouble())
            cfg.trail.sparkle_spread_px = static_cast<float>(t["sparkle_spread_px"].toDouble());
    }

    if (root.contains("click") && root["click"].isObject()) {
        QJsonObject c = root["click"].toObject();
        if (c.contains("enabled") && c["enabled"].isBool())
            cfg.click.enabled = c["enabled"].toBool();
        if (c.contains("trigger_left") && c["trigger_left"].isBool())
            cfg.click.trigger_left = c["trigger_left"].toBool();
        if (c.contains("trigger_right") && c["trigger_right"].isBool())
            cfg.click.trigger_right = c["trigger_right"].toBool();
        if (c.contains("trigger_middle") && c["trigger_middle"].isBool())
            cfg.click.trigger_middle = c["trigger_middle"].toBool();
        if (c.contains("color_r") && c["color_r"].isDouble())
            cfg.click.color_r = static_cast<uint8_t>(std::clamp(c["color_r"].toInt(), 0, 255));
        if (c.contains("color_g") && c["color_g"].isDouble())
            cfg.click.color_g = static_cast<uint8_t>(std::clamp(c["color_g"].toInt(), 0, 255));
        if (c.contains("color_b") && c["color_b"].isDouble())
            cfg.click.color_b = static_cast<uint8_t>(std::clamp(c["color_b"].toInt(), 0, 255));
        if (c.contains("start_radius_px") && c["start_radius_px"].isDouble())
            cfg.click.start_radius_px = static_cast<float>(c["start_radius_px"].toDouble());
        if (c.contains("end_radius_px") && c["end_radius_px"].isDouble())
            cfg.click.end_radius_px = static_cast<float>(c["end_radius_px"].toDouble());
        if (c.contains("duration_ms") && c["duration_ms"].isDouble())
            cfg.click.duration_ms = static_cast<float>(c["duration_ms"].toDouble());
        if (c.contains("base_opacity") && c["base_opacity"].isDouble())
            cfg.click.base_opacity = static_cast<float>(c["base_opacity"].toDouble());
        if (c.contains("outline_thickness_px") && c["outline_thickness_px"].isDouble())
            cfg.click.outline_thickness_px = static_cast<float>(c["outline_thickness_px"].toDouble());
        if (c.contains("fill_opacity") && c["fill_opacity"].isDouble())
            cfg.click.fill_opacity = static_cast<float>(c["fill_opacity"].toDouble());
        if (c.contains("style") && c["style"].isDouble()) {
            // T-022: the valid range widens from 0..6 to 0..10 for the four
            // elemental styles (Air 7, Fire 8, Water 9, Earth 10). Anything
            // outside the range is ignored here and additionally repaired by
            // ClickConfig::validated() -> Ring.
            //
            // T-023: that widening is gated on the SOURCE schema for the
            // same reason as sparkle_mode above. A schema <= 5 writer had no
            // elemental styles at all, so 7..10 in such a file is corruption
            // and repairs to Ring; only schema >= 6 may load them.
            const int st = c["style"].toInt();
            const int max_click_style =
                cfg.schema_version >= AppConfig::kElementalClickStyleSchema
                    ? static_cast<int>(ClickStyle::Earth)
                    : static_cast<int>(ClickStyle::DotRing);
            if (st >= 0 && st <= max_click_style) {
                cfg.click.style = static_cast<ClickStyle>(st);
            }
        }
        if (c.contains("particle_amount") && c["particle_amount"].isDouble())
            cfg.click.particle_amount = static_cast<uint8_t>(
                std::clamp(c["particle_amount"].toInt(), 0, 255));
        // T-022 schema 6: elemental hue-bias strength. A schema-5 file has no
        // such key, so the struct default (0.65) stands -- that IS the
        // documented 5 -> 6 migration, and it touches no pre-existing field.
        // Out-of-range values are clamped by ClickConfig::validated() below.
        if (c.contains("element_tint") && c["element_tint"].isDouble())
            cfg.click.element_tint = static_cast<float>(c["element_tint"].toDouble());
        // T-024 schema 8: press-and-hold. The MIGRATION is asymmetric on
        // purpose and is the whole reason this needs a schema bump at all --
        // hold_enabled changes how the mouse BEHAVES, not just how it looks.
        //
        //   key present            -> honour it (the user's own choice)
        //   key absent, schema <=7 -> OFF, because that config was written
        //                             before the gesture existed and its
        //                             owner never agreed to it
        //   key absent, schema  >=8 -> the struct default (ON), which is the
        //                             fresh-install case
        //
        // Reading the absent key as the struct default in the schema <= 7
        // case would hand an existing user a brand-new gesture purely for
        // upgrading the executable. That is exactly what this avoids.
        if (c.contains("hold_enabled") && c["hold_enabled"].isBool()) {
            cfg.click.hold_enabled = c["hold_enabled"].toBool();
        } else if (cfg.schema_version < AppConfig::kHoldFxSchema) {
            cfg.click.hold_enabled = false;
        }
        // T-027 schema 9: Hold Controls / Motion Wake. The SOURCE SCHEMA is
        // authoritative for the whole block, exactly like the enum boundaries
        // above: every key below was introduced at schema 9, so a key that
        // appears in a file written by a schema <= 8 writer is corruption or
        // an unknown field and must never acquire T-027 semantics after an
        // executable upgrade.
        //
        //   source schema <= 8 -> the entire block is ignored: Motion Wake
        //                         stays OFF (a materially new behaviour is
        //                         never handed to an existing user), and the
        //                         four multipliers keep their documented
        //                         identity defaults (1.0 / baseline Wake Life)
        //   source schema >= 9 -> every key is honoured; absent keys keep the
        //                         schema-9 struct defaults, and ClickConfig::
        //                         validated() clamps whatever was loaded
        if (cfg.schema_version >= AppConfig::kHoldWakeSchema) {
            if (c.contains("hold_wake_enabled") && c["hold_wake_enabled"].isBool())
                cfg.click.hold_wake_enabled = c["hold_wake_enabled"].toBool();
            if (c.contains("hold_intensity") && c["hold_intensity"].isDouble())
                cfg.click.hold_intensity =
                    static_cast<float>(c["hold_intensity"].toDouble());
            if (c.contains("hold_wake_density") && c["hold_wake_density"].isDouble())
                cfg.click.hold_wake_density =
                    static_cast<float>(c["hold_wake_density"].toDouble());
            if (c.contains("hold_wake_lifetime_ms") && c["hold_wake_lifetime_ms"].isDouble())
                cfg.click.hold_wake_lifetime_ms =
                    static_cast<float>(c["hold_wake_lifetime_ms"].toDouble());
            if (c.contains("hold_release_strength") && c["hold_release_strength"].isDouble())
                cfg.click.hold_release_strength =
                    static_cast<float>(c["hold_release_strength"].toDouble());
        } else {
            cfg.click.hold_wake_enabled = false;
        }
        // T-36 schema 11: Advanced Motion Wake. SOURCE SCHEMA AUTHORITATIVE,
        // exactly like the T-27 block above: a schema <= 10 writer cannot
        // carry these semantics, so the keys are ignored there and the
        // struct defaults (identity multipliers, 0 gate, accents OFF)
        // reproduce the accepted T-26/T-27 appearance for an upgraded user.
        if (cfg.schema_version >= AppConfig::kAdvancedMotionWakeSchema) {
            if (c.contains("wake_strength") && c["wake_strength"].isDouble())
                cfg.click.wake_strength =
                    static_cast<float>(c["wake_strength"].toDouble());
            if (c.contains("wake_size") && c["wake_size"].isDouble())
                cfg.click.wake_size = static_cast<float>(c["wake_size"].toDouble());
            if (c.contains("wake_spread") && c["wake_spread"].isDouble())
                cfg.click.wake_spread =
                    static_cast<float>(c["wake_spread"].toDouble());
            if (c.contains("speed_response") && c["speed_response"].isDouble())
                cfg.click.speed_response =
                    static_cast<float>(c["speed_response"].toDouble());
            if (c.contains("min_motion_speed_px_s")
                && c["min_motion_speed_px_s"].isDouble())
                cfg.click.min_motion_speed_px_s =
                    static_cast<float>(c["min_motion_speed_px_s"].toDouble());
            if (c.contains("turn_accent") && c["turn_accent"].isBool())
                cfg.click.turn_accent = c["turn_accent"].toBool();
            if (c.contains("stop_accent") && c["stop_accent"].isBool())
                cfg.click.stop_accent = c["stop_accent"].toBool();
        }
        if (c.contains("easing") && c["easing"].isDouble()) {
            const int ea = c["easing"].toInt();
            if (ea >= 0 && ea <= 2)
                cfg.click.easing = static_cast<ClickEasing>(ea);
        }
    }

    if (root.contains("render") && root["render"].isObject()) {
        QJsonObject r = root["render"].toObject();
        if (r.contains("diagnostic_primitives") && r["diagnostic_primitives"].isBool())
            cfg.render.diagnostic_primitives = r["diagnostic_primitives"].toBool();
    }

    return AppConfig::validated(cfg);
}

ConfigLoadResult ConfigStorage::load_from_file_result(const std::wstring& path) {
    ++g_load_count;  // W2-005 test seam
    ConfigLoadResult result;
    result.config = release_defaults();

    // CORE-001 test seam: a forced read failure is a ReadFailure, never a
    // silent fallback that would then be persisted over the source.
    if (!g_forced_read_failure_path.empty() && path == g_forced_read_failure_path) {
        ptd::log_write(ptd::LogLevel::Error,
                       "config: forced read failure (test seam); persistence disabled");
        result.status = ConfigLoadStatus::ReadFailure;
        result.persistence_allowed = false;
        return result;
    }

    std::error_code ec;
    bool exists = false;
    if (g_probe_fn) {
        auto [inj_exists, inj_ec] = g_probe_fn(path);
        exists = inj_exists;
        ec = inj_ec;
    } else {
        exists = std::filesystem::exists(path, ec);
    }
    if (!exists) {
        if (ec) {
            // CORE-001: filesystem probe error -- cannot establish absence.
            // Treat as protected source: persistence disabled, original untouched.
            ptd::log_write(ptd::LogLevel::Error,
                           "config: filesystem existence probe failed (" +
                               ec.message() +
                               "); persistence disabled to protect potential source");
            result.status = ConfigLoadStatus::ReadFailure;
            result.persistence_allowed = false;
            return result;
        }
        // T-33: a fresh configuration is the canonical Release Defaults, not
        // the C++ struct initializers. One semantic source of truth. A fresh
        // install is ordinary writable state.
        result.status = ConfigLoadStatus::MissingFresh;
        result.persistence_allowed = true;
        return result;
    }

    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open()) {
        // CORE-001: an unreadable existing config is PROTECTED. The fallback
        // is a runtime convenience only and must never overwrite the source.
        ptd::log_write(ptd::LogLevel::Error,
                       "config: failed to open file for reading; persistence "
                       "disabled to protect the unread source");
        result.status = ConfigLoadStatus::ReadFailure;
        result.persistence_allowed = false;
        return result;
    }

    std::string data((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
    in.close();

    const QByteArray bytes =
        QByteArray::fromRawData(data.data(), static_cast<qsizetype>(data.size()));

    // CORE-001: a future schema is valid-but-unsupported, NOT corrupt. Detect
    // it BEFORE any current-schema interpretation so an older executable can
    // never rewrite schema 11+ as schema 10, discard unknown future fields, or
    // rename the file to .corrupt.
    const std::optional<int> source_schema = peek_schema_version(bytes);
    if (source_schema && *source_schema > AppConfig::kCurrentSchemaVersion) {
        ptd::log_write(ptd::LogLevel::Warn,
                       "config: file schema " + std::to_string(*source_schema) +
                           " is newer than supported schema " +
                           std::to_string(AppConfig::kCurrentSchemaVersion) +
                           "; loaded as unsupported future schema, persistence "
                           "disabled, original bytes preserved");
        result.status = ConfigLoadStatus::UnsupportedFutureSchema;
        result.persistence_allowed = false;
        return result;
    }

    // CORE-003: an existing persisted file whose schema provenance is absent or
    // wrong-type must NOT silently become writable current schema. Only three
    // cases are writable: genuinely absent on a fresh install (handled above),
    // numeric historical/current/future schema. Any other schema metadata is
    // protected (persistence disabled) so a later save cannot canonicalize it.
    {
        const QJsonDocument probe = QJsonDocument::fromJson(bytes);
        if (!probe.isNull() && probe.isObject()) {
            const QJsonObject root = probe.object();
            if (root.contains("schema_version") &&
                !root["schema_version"].isDouble()) {
                ptd::log_write(ptd::LogLevel::Error,
                               "config: schema_version has non-numeric type; "
                               "persistence disabled to protect unknown provenance");
                result.status = ConfigLoadStatus::UnsupportedFutureSchema;
                result.persistence_allowed = false;
                return result;
            }
            if (!root.contains("schema_version")) {
                ptd::log_write(ptd::LogLevel::Error,
                               "config: existing file has no schema_version; "
                               "persistence disabled (legacy-less provenance)");
                result.status = ConfigLoadStatus::UnsupportedFutureSchema;
                result.persistence_allowed = false;
                return result;
            }
            // Numeric but non-integral (e.g. 10.5) after toInt truncation gives
            // wrong schema; treat as wrong-type.
            if (root["schema_version"].toDouble() !=
                static_cast<double>(root["schema_version"].toInt())) {
                ptd::log_write(ptd::LogLevel::Error,
                               "config: schema_version non-integral; "
                               "persistence disabled");
                result.status = ConfigLoadStatus::UnsupportedFutureSchema;
                result.persistence_allowed = false;
                return result;
            }
        }
    }

    QString err;
    auto opt = deserialize_json(bytes, &err);
    if (!opt) {
        const std::wstring corrupt_path = path + L".corrupt";
        std::error_code remove_ec;
        std::filesystem::remove(corrupt_path, remove_ec);
        std::error_code rename_ec;
        std::filesystem::rename(path, corrupt_path, rename_ec);
        if (rename_ec) {
            // CORE-001: the malformed original is now the ONLY copy. It must
            // remain byte-identical; persistence is disabled.
            ptd::log_write(ptd::LogLevel::Error,
                           "config: malformed JSON, failed to back up to .corrupt; "
                           "persistence disabled to protect the original");
            result.status = ConfigLoadStatus::MalformedBackupFailed;
            result.persistence_allowed = false;
            return result;
        }
        ptd::log_write(ptd::LogLevel::Warn,
                       "config: malformed JSON, backing up to .corrupt and returning defaults");
        result.status = ConfigLoadStatus::MalformedBackedUp;
        result.persistence_allowed = true;
        return result;
    }

    result.config = *opt;
    if (opt->schema_version < AppConfig::kCurrentSchemaVersion) {
        result.status = ConfigLoadStatus::LoadedMigrated;
    } else {
        result.status = ConfigLoadStatus::LoadedCurrent;
    }
    result.persistence_allowed = true;
    return result;
}

AppConfig ConfigStorage::load_from_file(const std::wstring& path) {
    return load_from_file_result(path).config;
}

bool ConfigStorage::save_to_file(const AppConfig& config, const std::wstring& path) {
    // W2-002 deterministic failure seam: an armed empty path fails every save;
    // an armed non-empty path fails only that exact target. Production leaves
    // it disarmed, so this branch is never taken outside a test.
    if (g_forced_save_failure_armed &&
        (g_forced_save_failure_path.empty() || g_forced_save_failure_path == path)) {
        ptd::log_write(ptd::LogLevel::Error,
                       "config: save forced to fail by test seam");
        return false;
    }
    const std::filesystem::path target(path);
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);

    const QByteArray json = serialize_json(config);
    const std::wstring tmp_path = path + L".tmp";

    {
        std::ofstream out(tmp_path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            ptd::log_write(ptd::LogLevel::Error, "config: failed to open temporary file for writing");
            return false;
        }
        out.write(json.constData(), json.size());
        out.flush();
        out.close();
        if (!out.good()) {
            std::filesystem::remove(tmp_path, ec);
            ptd::log_write(ptd::LogLevel::Error, "config: stream write error on temporary file");
            return false;
        }
    }

    // Atomic replace on Windows
    if (ReplaceFileW(path.c_str(), tmp_path.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
        return true;
    }

    // If destination does not exist yet, MoveFileEx with replace
    if (GetLastError() == ERROR_FILE_NOT_FOUND) {
        if (MoveFileExW(tmp_path.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return true;
        }
    }

    // Fallback: filesystem rename
    std::filesystem::rename(tmp_path, target, ec);
    if (ec) {
        ptd::log_write(ptd::LogLevel::Error, "config: failed atomic replace to destination");
        return false;
    }
    return true;
}

} // namespace ptd
