// T-011 MVP 06: Configuration & Persistence regression tests.
// T-015: schema 2 (dual trail colors + color_mode) with schema-1 migration.
// Validates:
// 1. AppConfig default values and validation clamping.
// 2. Full JSON serialization & deserialization roundtrip.
// 3. Schema version preservation & normalization.
// 4. Clamping of corrupt/out-of-range JSON values.
// 5. Missing file fallback to clean defaults.
// 6. Malformed JSON recovery (backup to .corrupt, fallback to defaults, no crash).
// 7. Atomic file save (no partial file, tmp cleanup, correct content).
// 8. Restart simulation (save -> reload survives exactly).
// 9. Schema-1 -> schema-2 trail color migration (visual identity).
// 10. All four schema-2 color modes roundtrip.

#include "../src/config/app_config.h"
#include "../src/config/config_storage.h"
#include "../src/config/release_defaults.h"
#include "../src/config/dev_defaults.h"
#include "../src/app/startup_paths.h"
#include "../src/core/log.h"

#include <QtTest/QtTest>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <vector>

namespace {
// T-030: captured log messages for the fallback assertions.
std::vector<std::string> g_captured_logs;
int g_known_folder_probe_calls = 0;

bool counting_known_folder_failure_probe(std::wstring& out) {
    ++g_known_folder_probe_calls;
    out.clear();
    return false;
}
}

class TestConfig : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    void defaults_have_valid_ranges_and_schema();
    void roundtrip_serialization_matches_exactly();
    void all_four_color_modes_roundtrip();
    void out_of_range_values_are_clamped_on_deserialize();
    void missing_file_returns_defaults_cleanly();
    void malformed_file_backs_up_and_returns_defaults();
    void atomic_save_creates_clean_file();
    void restart_simulation_persists_all_settings();
    void master_toggle_preserves_child_preferences_across_restart();
    void master_toggle_preserves_inverse_child_preferences_across_restart();

    // T-015 schema migration.
    void schema1_yellow_migration();
    void schema1_arbitrary_rgb_migration();
    void migration_preserves_non_color_fields();
    // T-021 audit: renamed from the stale ..._schema_version_3 name --
    // the contract is "the persisted schema version equals the CURRENT
    // schema version", which is 5 since T-021.
    void save_after_migration_writes_current_schema_version();
    void preset_values_survive_restart();
    void all_trail_styles_roundtrip();
    void invalid_fade_enum_repairs_to_smooth();
    void schema2_to_schema3_style_defaults();

    // T-017 schema 4 click style tests.
    void schema3_to_schema4_click_defaults();
    void all_click_styles_roundtrip();
    void click_particle_amount_bounds();

    // T-021 schema 5 sparkle decoration tests.
    void schema4_to_schema5_sparkle_defaults();
    void all_sparkle_modes_roundtrip();
    // T-022 Elemental Click VFX: schema 5 -> 6 + elemental styles.
    void schema5_to_schema6_element_tint_default();
    void all_click_styles_roundtrip_with_element_tint();
    void invalid_click_style_and_tint_recover();
    void invalid_sparkle_enum_repairs_to_off();
    void sparkle_value_bounds_and_nan_repair();

    // T-023 schema 7: version-aware enum introduction boundaries. A value
    // that was invalid when the file was written must NEVER be reinterpreted
    // as a newly introduced effect just because a later schema added it.
    void sparkle_shards_enum_is_schema_gated();
    void elemental_click_styles_are_schema_gated();

    // T-024 schema 8: the Hold FX introduction boundary. Unlike the enum
    // boundaries above this one governs a BEHAVIOUR default, and the two
    // directions deliberately disagree (fresh ON, historical OFF).
    void hold_fx_schema8_migration_and_roundtrip();
    void hold_controls_schema9_migration_and_roundtrip();

    // T-032 schema 10: Start with Windows. An OS SIDE EFFECT migrates even
    // more conservatively than a visual behaviour: an existing user is
    // never enrolled into Windows startup by an executable upgrade.
    void start_with_windows_schema10_migration_and_roundtrip();

    // T-030: a %LOCALAPPDATA% probe failure must produce a logged,
    // deterministic fallback, never a silent working-directory path.
    void default_paths_fail_deterministic_fallback();
    void long_module_path_keeps_state_fallbacks_aligned();
    void module_path_failure_uses_absolute_shared_fallback();

    // T-029: a failed persistence write must be observable, never silent.
    void persistence_failure_is_observable();

    // T-38: malformed config backup failure must report error and preserve original file.
    void malformed_backup_failure_reports_error_and_preserves_file();

    // CORE-001: load provenance must distinguish fresh/current/migrated/
    // malformed-backed-up/backup-failed/future-schema/read-failure, and a
    // protected source must never become ordinary writable state.
    void load_result_classifies_every_provenance();
    void filesystem_probe_error_is_protected();
    void schema_provenance_requires_supported_integral_tag();
    void future_schema_is_unsupported_not_corrupt();
    void malformed_backup_failure_disables_persistence();
    void read_failure_disables_persistence();

    // T-41: log directory creation failure must report error and not mark sink initialized.
    void log_init_directory_creation_failure_is_reported();
    void log_init_normal_path_creates_directory_and_writes_file();
    void startup_logging_bootstrap_uses_resolved_explicit_path();
    void startup_log_failure_is_terminal_for_normal_and_smoke();

    // T-34: Developer Release Defaults & Dev Presets
    void dev_diff_exhaustive_field_coverage();
    void dev_diff_reports_no_diff_for_identical();
    void dev_preset_lifecycle_isolated();
    void dev_promote_lifecycle_and_safeguards();
    void dev_promote_rollback_under_injected_failures();

private:
    std::filesystem::path test_dir_;
};

void TestConfig::initTestCase() {
    test_dir_ = std::filesystem::temp_directory_path() / "protrail_test_config_dir";
    std::error_code ec;
    std::filesystem::remove_all(test_dir_, ec);
    std::filesystem::create_directories(test_dir_, ec);
}

void TestConfig::cleanupTestCase() {
    std::error_code ec;
    std::filesystem::remove_all(test_dir_, ec);
}

void TestConfig::defaults_have_valid_ranges_and_schema() {
    ptd::AppConfig cfg{};
    QCOMPARE(cfg.schema_version, ptd::AppConfig::kCurrentSchemaVersion);
    QCOMPARE(cfg.master_enabled, true);
    QCOMPARE(cfg.trail.enabled, true);
    QCOMPARE(cfg.click.enabled, true);

    // T-015 default visual contract: mode Full, Start = Fade = 255,255,0.
    QCOMPARE(cfg.trail.color_mode, ptd::TrailColorMode::Full);
    QCOMPARE(cfg.trail.start_color_r, 255);
    QCOMPARE(cfg.trail.start_color_g, 255);
    QCOMPARE(cfg.trail.start_color_b, 0);
    QCOMPARE(cfg.trail.fade_color_r, 255);
    QCOMPARE(cfg.trail.fade_color_g, 255);
    QCOMPARE(cfg.trail.fade_color_b, 0);
    // T-016 style defaults: approved Classic baseline.
    QCOMPARE(cfg.trail.style, ptd::TrailStyle::Classic);
    QCOMPARE(cfg.trail.glow_strength, 0.5f);
    QCOMPARE(cfg.trail.segment_spacing_px, 12.0f);

    ptd::AppConfig validated = ptd::AppConfig::validated(cfg);
    QCOMPARE(validated.schema_version, ptd::AppConfig::kCurrentSchemaVersion);
    QCOMPARE(validated.trail.head_thickness_px, 3.0f);
    QCOMPARE(validated.click.start_radius_px, 8.0f);
}

void TestConfig::roundtrip_serialization_matches_exactly() {
    ptd::AppConfig original{};
    original.master_enabled = false;

    original.trail.enabled = true;
    original.trail.color_mode = ptd::TrailColorMode::Gradient;
    original.trail.start_color_r = 12;
    original.trail.start_color_g = 34;
    original.trail.start_color_b = 56;
    original.trail.fade_color_r = 210;
    original.trail.fade_color_g = 198;
    original.trail.fade_color_b = 76;
    original.trail.head_thickness_px = 7.5f;
    original.trail.tail_thickness_px = 2.0f;
    original.trail.taper_strength = 0.8f;
    original.trail.lifetime_ms = 450.0f;
    original.trail.base_opacity = 0.75f;
    original.trail.smoothing = 0.22f;
    original.trail.fade_start = 0.2f;
    original.trail.fade_curve = ptd::FadeCurve::Linear;

    original.click.enabled = true;
    original.click.trigger_left = true;
    original.click.trigger_right = false;
    original.click.trigger_middle = true;
    original.click.color_r = 99;
    original.click.color_g = 88;
    original.click.color_b = 77;
    original.click.start_radius_px = 12.0f;
    original.click.end_radius_px = 45.0f;
    original.click.duration_ms = 350.0f;
    original.click.base_opacity = 0.6f;
    original.click.outline_thickness_px = 3.5f;
    original.click.fill_opacity = 0.4f;
    original.click.easing = ptd::ClickEasing::EaseOut;

    original.render.diagnostic_primitives = true;

    const QByteArray json = ptd::ConfigStorage::serialize_json(original);
    QVERIFY(!json.isEmpty());

    QString err;
    auto deserialized = ptd::ConfigStorage::deserialize_json(json, &err);
    QVERIFY2(deserialized.has_value(), err.toUtf8().constData());

    const auto& res = *deserialized;
    QCOMPARE(res.schema_version, ptd::AppConfig::kCurrentSchemaVersion);
    QCOMPARE(res.master_enabled, original.master_enabled);

    QCOMPARE(res.trail.enabled, original.trail.enabled);
    QCOMPARE(res.trail.color_mode, original.trail.color_mode);
    QCOMPARE(res.trail.start_color_r, original.trail.start_color_r);
    QCOMPARE(res.trail.start_color_g, original.trail.start_color_g);
    QCOMPARE(res.trail.start_color_b, original.trail.start_color_b);
    QCOMPARE(res.trail.fade_color_r, original.trail.fade_color_r);
    QCOMPARE(res.trail.fade_color_g, original.trail.fade_color_g);
    QCOMPARE(res.trail.fade_color_b, original.trail.fade_color_b);
    QCOMPARE(res.trail.head_thickness_px, original.trail.head_thickness_px);
    QCOMPARE(res.trail.tail_thickness_px, original.trail.tail_thickness_px);
    QCOMPARE(res.trail.taper_strength, original.trail.taper_strength);
    QCOMPARE(res.trail.lifetime_ms, original.trail.lifetime_ms);
    QCOMPARE(res.trail.base_opacity, original.trail.base_opacity);
    QCOMPARE(res.trail.smoothing, original.trail.smoothing);
    QCOMPARE(res.trail.fade_start, original.trail.fade_start);
    QCOMPARE(res.trail.fade_curve, original.trail.fade_curve);

    QCOMPARE(res.click.enabled, original.click.enabled);
    QCOMPARE(res.click.trigger_left, original.click.trigger_left);
    QCOMPARE(res.click.trigger_right, original.click.trigger_right);
    QCOMPARE(res.click.trigger_middle, original.click.trigger_middle);
    QCOMPARE(res.click.color_r, original.click.color_r);
    QCOMPARE(res.click.color_g, original.click.color_g);
    QCOMPARE(res.click.color_b, original.click.color_b);
    QCOMPARE(res.click.start_radius_px, original.click.start_radius_px);
    QCOMPARE(res.click.end_radius_px, original.click.end_radius_px);
    QCOMPARE(res.click.duration_ms, original.click.duration_ms);
    QCOMPARE(res.click.base_opacity, original.click.base_opacity);
    QCOMPARE(res.click.outline_thickness_px, original.click.outline_thickness_px);
    QCOMPARE(res.click.fill_opacity, original.click.fill_opacity);
    QCOMPARE(res.click.easing, original.click.easing);

    QCOMPARE(res.render.diagnostic_primitives, original.render.diagnostic_primitives);
}

void TestConfig::all_four_color_modes_roundtrip() {
    for (const ptd::TrailColorMode mode : {
             ptd::TrailColorMode::Full,
             ptd::TrailColorMode::StartAccent,
             ptd::TrailColorMode::FadeAccent,
             ptd::TrailColorMode::Gradient}) {
        ptd::AppConfig original{};
        original.trail.color_mode = mode;
        original.trail.start_color_r = 1;
        original.trail.start_color_g = 2;
        original.trail.start_color_b = 3;
        original.trail.fade_color_r = 250;
        original.trail.fade_color_g = 251;
        original.trail.fade_color_b = 252;

        const QByteArray json = ptd::ConfigStorage::serialize_json(original);
        QString err;
        auto deserialized = ptd::ConfigStorage::deserialize_json(json, &err);
        QVERIFY2(deserialized.has_value(), err.toUtf8().constData());
        QCOMPARE(deserialized->trail.color_mode, mode);
        QCOMPARE(deserialized->trail.start_color_r, 1);
        QCOMPARE(deserialized->trail.start_color_g, 2);
        QCOMPARE(deserialized->trail.start_color_b, 3);
        QCOMPARE(deserialized->trail.fade_color_r, 250);
        QCOMPARE(deserialized->trail.fade_color_g, 251);
        QCOMPARE(deserialized->trail.fade_color_b, 252);
    }
}

void TestConfig::out_of_range_values_are_clamped_on_deserialize() {
    const char* bad_json = R"({
        "schema_version": -5,
        "master_enabled": true,
        "trail": {
            "start_color_r": 999,
            "start_color_g": -30,
            "fade_color_b": 512,
            "color_mode": 17,
            "head_thickness_px": 5000.0,
            "tail_thickness_px": -10.0,
            "lifetime_ms": 1.0,
            "base_opacity": 25.0
        },
        "click": {
            "start_radius_px": 100.0,
            "end_radius_px": 10.0,
            "duration_ms": 10000.0
        }
    })";

    QString err;
    auto opt = ptd::ConfigStorage::deserialize_json(QByteArray(bad_json), &err);
    QVERIFY(opt.has_value());

    const auto& res = *opt;
    // Schema version recovered to the current schema
    QCOMPARE(res.schema_version, ptd::AppConfig::kCurrentSchemaVersion);
    // Start/Fade colors clamp into 0..255
    QCOMPARE(res.trail.start_color_r, 255);
    QCOMPARE(res.trail.start_color_g, 0);
    QCOMPARE(res.trail.fade_color_b, 255);
    // Invalid color mode repairs to Full
    QCOMPARE(res.trail.color_mode, ptd::TrailColorMode::Full);
    // Thickness clamped to kMaxThicknessPx (40.0)
    QCOMPARE(res.trail.head_thickness_px, ptd::TrailConfig::kMaxThicknessPx);
    // Tail clamped to kMinThicknessPx (0.5)
    QCOMPARE(res.trail.tail_thickness_px, ptd::TrailConfig::kMinThicknessPx);
    // Lifetime clamped to kMinLifetimeMs (50.0)
    QCOMPARE(res.trail.lifetime_ms, ptd::TrailConfig::kMinLifetimeMs);
    // Opacity clamped to 1.0
    QCOMPARE(res.trail.base_opacity, 1.0f);
    // Click duration clamped to kMaxDurationMs (2000.0)
    QCOMPARE(res.click.duration_ms, ptd::ClickConfig::kMaxDurationMs);
    // Click end radius clamped so end >= start
    QVERIFY(res.click.end_radius_px >= res.click.start_radius_px);
}

void TestConfig::missing_file_returns_defaults_cleanly() {
    const std::wstring non_existent = (test_dir_ / "does_not_exist.json").wstring();
    ptd::AppConfig cfg = ptd::ConfigStorage::load_from_file(non_existent);
    ptd::AppConfig def{};
    QCOMPARE(cfg.schema_version, def.schema_version);
    QCOMPARE(cfg.trail.head_thickness_px, def.trail.head_thickness_px);
    QCOMPARE(cfg.click.duration_ms, def.click.duration_ms);
}

void TestConfig::malformed_file_backs_up_and_returns_defaults() {
    const std::filesystem::path bad_file = test_dir_ / "corrupt_config.json";
    {
        std::ofstream out(bad_file, std::ios::trunc);
        out << "{ this is not valid json! @#$%^&* }";
    }
    QVERIFY(std::filesystem::exists(bad_file));

    ptd::AppConfig cfg = ptd::ConfigStorage::load_from_file(bad_file.wstring());
    ptd::AppConfig def{};
    // Defaults returned
    QCOMPARE(cfg.schema_version, def.schema_version);
    QCOMPARE(cfg.trail.head_thickness_px, def.trail.head_thickness_px);

    // Bad file backed up to .corrupt
    const std::filesystem::path backup_file = test_dir_ / "corrupt_config.json.corrupt";
    QVERIFY(std::filesystem::exists(backup_file));
}

void TestConfig::atomic_save_creates_clean_file() {
    const std::filesystem::path target = test_dir_ / "subfolder" / "saved_config.json";
    ptd::AppConfig cfg{};
    cfg.trail.head_thickness_px = 14.0f;
    cfg.click.duration_ms = 800.0f;

    const bool ok = ptd::ConfigStorage::save_to_file(cfg, target.wstring());
    QVERIFY(ok);
    QVERIFY(std::filesystem::exists(target));

    // .tmp file must NOT remain
    const std::filesystem::path tmp_file = test_dir_ / "subfolder" / "saved_config.json.tmp";
    QVERIFY(!std::filesystem::exists(tmp_file));

    // Reload and verify
    ptd::AppConfig loaded = ptd::ConfigStorage::load_from_file(target.wstring());
    QCOMPARE(loaded.trail.head_thickness_px, 14.0f);
    QCOMPARE(loaded.click.duration_ms, 800.0f);
}

void TestConfig::restart_simulation_persists_all_settings() {
    const std::filesystem::path target = test_dir_ / "session_config.json";

    // Session 1: customize settings and save
    {
        ptd::AppConfig session1{};
        session1.master_enabled = false;
        session1.trail.color_mode = ptd::TrailColorMode::FadeAccent;
        session1.trail.start_color_r = 101;
        session1.trail.start_color_g = 202;
        session1.trail.start_color_b = 33;
        session1.trail.fade_color_r = 44;
        session1.trail.fade_color_g = 55;
        session1.trail.fade_color_b = 66;
        session1.trail.lifetime_ms = 777.0f;
        session1.click.trigger_left = false;
        session1.click.start_radius_px = 15.0f;
        session1.click.end_radius_px = 35.0f;

        QVERIFY(ptd::ConfigStorage::save_to_file(session1, target.wstring()));
    }

    // Session 2: start app, load from disk
    {
        ptd::AppConfig session2 = ptd::ConfigStorage::load_from_file(target.wstring());
        QCOMPARE(session2.master_enabled, false);
        QCOMPARE(session2.trail.color_mode, ptd::TrailColorMode::FadeAccent);
        QCOMPARE(session2.trail.start_color_r, 101);
        QCOMPARE(session2.trail.start_color_g, 202);
        QCOMPARE(session2.trail.start_color_b, 33);
        QCOMPARE(session2.trail.fade_color_r, 44);
        QCOMPARE(session2.trail.fade_color_g, 55);
        QCOMPARE(session2.trail.fade_color_b, 66);
        QCOMPARE(session2.trail.lifetime_ms, 777.0f);
        QCOMPARE(session2.click.trigger_left, false);
        QCOMPARE(session2.click.start_radius_px, 15.0f);
        QCOMPARE(session2.click.end_radius_px, 35.0f);
    }
}

void TestConfig::master_toggle_preserves_child_preferences_across_restart() {
    const std::filesystem::path target = test_dir_ / "master_child_preservation.json";

    // Initial state:
    // master = true
    // trail.enabled = true
    // click.enabled = false
    ptd::AppConfig app_config{};
    app_config.master_enabled = true;
    app_config.trail.enabled = true;
    app_config.click.enabled = false;

    // Runtime state mirrors Application::Impl
    bool runtime_master = app_config.master_enabled;
    ptd::TrailConfig runtime_trail = app_config.trail;
    ptd::ClickConfig runtime_click = app_config.click;

    // Action: master -> false
    // Preferred invariant: mutate canonical runtime state first, then persist snapshot
    runtime_master = false;
    app_config.master_enabled = runtime_master;
    // Crucial contract (Defect 2): Master OFF must NOT rewrite user's stored Trail or Click preferences!
    app_config.trail = runtime_trail;
    app_config.click = runtime_click;

    // Persist
    QVERIFY(ptd::ConfigStorage::save_to_file(app_config, target.wstring()));

    // Restart / load
    ptd::AppConfig loaded = ptd::ConfigStorage::load_from_file(target.wstring());
    QCOMPARE(loaded.master_enabled, false);
    QCOMPARE(loaded.trail.enabled, true);
    QCOMPARE(loaded.click.enabled, false);

    // Then: master -> true
    loaded.master_enabled = true;

    // Expected effective runtime:
    // trail_effective = master_enabled && trail.enabled -> true
    // click_effective = master_enabled && click.enabled -> false
    const bool trail_effective = loaded.master_enabled && loaded.trail.enabled;
    const bool click_effective = loaded.master_enabled && loaded.click.enabled;

    QCOMPARE(trail_effective, true);
    QCOMPARE(click_effective, false);
}

void TestConfig::master_toggle_preserves_inverse_child_preferences_across_restart() {
    const std::filesystem::path target = test_dir_ / "master_inverse_child_preservation.json";

    // Initial inverse state:
    // master = true
    // trail.enabled = false
    // click.enabled = true
    ptd::AppConfig app_config{};
    app_config.master_enabled = true;
    app_config.trail.enabled = false;
    app_config.click.enabled = true;

    bool runtime_master = app_config.master_enabled;
    ptd::TrailConfig runtime_trail = app_config.trail;
    ptd::ClickConfig runtime_click = app_config.click;

    // Action: master -> false
    runtime_master = false;
    app_config.master_enabled = runtime_master;
    app_config.trail = runtime_trail;
    app_config.click = runtime_click;

    // Persist
    QVERIFY(ptd::ConfigStorage::save_to_file(app_config, target.wstring()));

    // Restart / load
    ptd::AppConfig loaded = ptd::ConfigStorage::load_from_file(target.wstring());
    QCOMPARE(loaded.master_enabled, false);
    QCOMPARE(loaded.trail.enabled, false);
    QCOMPARE(loaded.click.enabled, true);

    // Then: master -> true
    loaded.master_enabled = true;

    // Expected effective runtime:
    // trail_effective = master_enabled && trail.enabled -> false
    // click_effective = master_enabled && click.enabled -> true
    const bool trail_effective = loaded.master_enabled && loaded.trail.enabled;
    const bool click_effective = loaded.master_enabled && loaded.click.enabled;

    QCOMPARE(trail_effective, false);
    QCOMPARE(click_effective, true);
}

// ---- T-015 Phase 9: schema-1 -> schema-2 trail color migration ----

namespace {
// Build a schema-1 config JSON with the given trail RGB and optional
// extra trail members (raw JSON text injected verbatim).
QByteArray schema1_json(int r, int g, int b, const char* extra_trail = "") {
    QByteArray trail =
        "        \"enabled\": true,\n"
        "        \"color_r\": " + QByteArray::number(r) +
        ",\n        \"color_g\": " + QByteArray::number(g) +
        ",\n        \"color_b\": " + QByteArray::number(b);
    if (extra_trail && *extra_trail) {
        trail += QByteArray(",\n") + extra_trail;
    }
    return QByteArray(
        "{\n"
        "    \"schema_version\": 1,\n"
        "    \"master_enabled\": true,\n"
        "    \"trail\": {\n" +
        trail +
        "\n    },\n"
        "    \"click\": {\n"
        "        \"enabled\": true,\n"
        "        \"color_r\": 0,\n"
        "        \"color_g\": 200,\n"
        "        \"color_b\": 255\n"
        "    },\n"
        "    \"render\": {\n"
        "        \"diagnostic_primitives\": false\n"
        "    }\n"
        "}\n");
}
} // namespace

void TestConfig::schema1_yellow_migration() {
    QString err;
    auto opt = ptd::ConfigStorage::deserialize_json(schema1_json(255, 255, 0), &err);
    QVERIFY2(opt.has_value(), err.toUtf8().constData());

    const auto& res = *opt;
    // Visually identical: Start = Fade = old RGB, mode = Full.
    QCOMPARE(res.trail.color_mode, ptd::TrailColorMode::Full);
    QCOMPARE(res.trail.start_color_r, 255);
    QCOMPARE(res.trail.start_color_g, 255);
    QCOMPARE(res.trail.start_color_b, 0);
    QCOMPARE(res.trail.fade_color_r, 255);
    QCOMPARE(res.trail.fade_color_g, 255);
    QCOMPARE(res.trail.fade_color_b, 0);
}

void TestConfig::schema1_arbitrary_rgb_migration() {
    QString err;
    auto opt = ptd::ConfigStorage::deserialize_json(schema1_json(12, 34, 56), &err);
    QVERIFY2(opt.has_value(), err.toUtf8().constData());

    const auto& res = *opt;
    QCOMPARE(res.trail.color_mode, ptd::TrailColorMode::Full);
    // Start == Fade == migrated RGB.
    QCOMPARE(res.trail.start_color_r, 12);
    QCOMPARE(res.trail.start_color_g, 34);
    QCOMPARE(res.trail.start_color_b, 56);
    QCOMPARE(res.trail.fade_color_r, 12);
    QCOMPARE(res.trail.fade_color_g, 34);
    QCOMPARE(res.trail.fade_color_b, 56);

    // Out-of-range schema-1 RGB clamps before migration.
    auto clamped = ptd::ConfigStorage::deserialize_json(schema1_json(999, -5, 300), &err);
    QVERIFY(clamped.has_value());
    QCOMPARE(clamped->trail.start_color_r, 255);
    QCOMPARE(clamped->trail.start_color_g, 0);
    QCOMPARE(clamped->trail.start_color_b, 255);
    QCOMPARE(clamped->trail.fade_color_r, 255);
    QCOMPARE(clamped->trail.fade_color_g, 0);
    QCOMPARE(clamped->trail.fade_color_b, 255);
}

void TestConfig::migration_preserves_non_color_fields() {
    const char* extra =
        "        \"head_thickness_px\": 9.5,\n"
        "        \"tail_thickness_px\": 1.5,\n"
        "        \"taper_strength\": 0.7,\n"
        "        \"lifetime_ms\": 640.0,\n"
        "        \"base_opacity\": 0.8,\n"
        "        \"smoothing\": 0.25,\n"
        "        \"fade_start\": 0.3,\n"
        "        \"fade_curve\": 1,\n"
        "        \"enabled\": true";
    QString err;
    auto opt = ptd::ConfigStorage::deserialize_json(schema1_json(10, 20, 30, extra), &err);
    QVERIFY2(opt.has_value(), err.toUtf8().constData());

    const auto& res = *opt;
    QCOMPARE(res.trail.head_thickness_px, 9.5f);
    QCOMPARE(res.trail.tail_thickness_px, 1.5f);
    QCOMPARE(res.trail.taper_strength, 0.7f);
    QCOMPARE(res.trail.lifetime_ms, 640.0f);
    QCOMPARE(res.trail.base_opacity, 0.8f);
    QCOMPARE(res.trail.smoothing, 0.25f);
    QCOMPARE(res.trail.fade_start, 0.3f);
    QCOMPARE(res.trail.fade_curve, ptd::FadeCurve::Smooth);
    QCOMPARE(res.trail.enabled, true);
    // Old master/effect/render preferences survive migration.
    QCOMPARE(res.master_enabled, true);
    QCOMPARE(res.click.color_r, 0);
    QCOMPARE(res.click.color_g, 200);
    QCOMPARE(res.click.color_b, 255);
    QCOMPARE(res.render.diagnostic_primitives, false);
}

void TestConfig::save_after_migration_writes_current_schema_version() {
    QString err;
    auto migrated = ptd::ConfigStorage::deserialize_json(schema1_json(90, 80, 70), &err);
    QVERIFY2(migrated.has_value(), err.toUtf8().constData());

    const std::filesystem::path target = test_dir_ / "migrated_config.json";
    QVERIFY(ptd::ConfigStorage::save_to_file(*migrated, target.wstring()));

    // The persisted file itself must declare the current schema version.
    std::ifstream in(target, std::ios::in | std::ios::binary);
    std::string data((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    in.close();
    const std::string version_line =
        "\"schema_version\": " + std::to_string(ptd::AppConfig::kCurrentSchemaVersion);
    QVERIFY(data.find(version_line) != std::string::npos);

    // And it reloads with the migrated dual colors intact.
    auto reloaded = ptd::ConfigStorage::load_from_file(target.wstring());
    QCOMPARE(reloaded.schema_version, ptd::AppConfig::kCurrentSchemaVersion);
    QCOMPARE(reloaded.trail.color_mode, ptd::TrailColorMode::Full);
    QCOMPARE(reloaded.trail.start_color_r, 90);
    QCOMPARE(reloaded.trail.fade_color_b, 70);
}

void TestConfig::preset_values_survive_restart() {
    // Preset-shaped config (Fire-like) must survive a save/restart cycle
    // exactly: mode + both trail colors + click color.
    const std::filesystem::path target = test_dir_ / "preset_session.json";
    {
        ptd::AppConfig s1{};
        s1.trail.color_mode = ptd::TrailColorMode::Gradient;
        s1.trail.start_color_r = 255; s1.trail.start_color_g = 240; s1.trail.start_color_b = 96;
        s1.trail.fade_color_r = 255;  s1.trail.fade_color_g = 64;   s1.trail.fade_color_b = 24;
        s1.click.color_r = 255;       s1.click.color_g = 128;       s1.click.color_b = 32;
        QVERIFY(ptd::ConfigStorage::save_to_file(s1, target.wstring()));
    }
    ptd::AppConfig s2 = ptd::ConfigStorage::load_from_file(target.wstring());
    QCOMPARE(s2.trail.color_mode, ptd::TrailColorMode::Gradient);
    QCOMPARE(s2.trail.start_color_r, 255);
    QCOMPARE(s2.trail.start_color_g, 240);
    QCOMPARE(s2.trail.start_color_b, 96);
    QCOMPARE(s2.trail.fade_color_r, 255);
    QCOMPARE(s2.trail.fade_color_g, 64);
    QCOMPARE(s2.trail.fade_color_b, 24);
    QCOMPARE(s2.click.color_r, 255);
    QCOMPARE(s2.click.color_g, 128);
    QCOMPARE(s2.click.color_b, 32);
}

// T-016: all eight trail styles roundtrip with style parameters.
void TestConfig::all_trail_styles_roundtrip() {
    for (const ptd::TrailStyle style : {
             ptd::TrailStyle::Classic, ptd::TrailStyle::SoftGlow,
             ptd::TrailStyle::Comet,   ptd::TrailStyle::Neon,
             ptd::TrailStyle::Dotted,  ptd::TrailStyle::Pulse,
             ptd::TrailStyle::Ribbon,  ptd::TrailStyle::Spark}) {
        ptd::AppConfig original{};
        original.trail.style = style;
        original.trail.glow_strength = 0.75f;
        original.trail.segment_spacing_px = 24.0f;

        const QByteArray json = ptd::ConfigStorage::serialize_json(original);
        QString err;
        auto deserialized = ptd::ConfigStorage::deserialize_json(json, &err);
        QVERIFY2(deserialized.has_value(), err.toUtf8().constData());
        QCOMPARE(deserialized->trail.style, style);
        QCOMPARE(deserialized->trail.glow_strength, 0.75f);
        QCOMPARE(deserialized->trail.segment_spacing_px, 24.0f);
    }
}

// T-020 Phase 8: an invalid fade enum repairs to the current safe product
// default (T-019 Smooth), and an explicitly stored Linear stays Linear.
void TestConfig::invalid_fade_enum_repairs_to_smooth() {
    ptd::TrailConfig bad{};
    bad.fade_curve = static_cast<ptd::FadeCurve>(200);
    QCOMPARE(ptd::TrailConfig::validated(bad).fade_curve,
             ptd::FadeCurve::Smooth);

    // Persisted valid Linear must never be rewritten by validation.
    ptd::AppConfig original{};
    original.trail.fade_curve = ptd::FadeCurve::Linear;
    const QByteArray json = ptd::ConfigStorage::serialize_json(original);
    QString err;
    auto deserialized = ptd::ConfigStorage::deserialize_json(json, &err);
    QVERIFY2(deserialized.has_value(), err.toUtf8().constData());
    QCOMPARE(deserialized->trail.fade_curve, ptd::FadeCurve::Linear);

    // The same repair applies on the deserialize path.
    const char* bad_fade_json = R"({
        "schema_version": 4,
        "trail": { "fade_curve": 9 },
        "click": { "enabled": true },
        "render": { "diagnostic_primitives": false }
    })";
    auto opt = ptd::ConfigStorage::deserialize_json(QByteArray(bad_fade_json), &err);
    QVERIFY2(opt.has_value(), err.toUtf8().constData());
    QCOMPARE(opt->trail.fade_curve, ptd::FadeCurve::Smooth);
}

// T-016: a schema-2 file without style fields loads with the approved
// Classic defaults (glow 0.5, spacing 12, style Classic).
void TestConfig::schema2_to_schema3_style_defaults() {
    const char* schema2_json = R"({
        "schema_version": 2,
        "master_enabled": true,
        "trail": {
            "enabled": true,
            "color_mode": 3,
            "start_color_r": 255, "start_color_g": 240, "start_color_b": 96,
            "fade_color_r": 255, "fade_color_g": 64, "fade_color_b": 24,
            "head_thickness_px": 5.0,
            "lifetime_ms": 500.0
        },
        "click": { "enabled": true },
        "render": { "diagnostic_primitives": false }
    })";

    QString err;
    auto opt = ptd::ConfigStorage::deserialize_json(QByteArray(schema2_json), &err);
    QVERIFY2(opt.has_value(), err.toUtf8().constData());
    QCOMPARE(opt->trail.style, ptd::TrailStyle::Classic);
    QCOMPARE(opt->trail.glow_strength, 0.5f);
    QCOMPARE(opt->trail.segment_spacing_px, 12.0f);
    // Non-style fields untouched by the migration.
    QCOMPARE(opt->trail.color_mode, ptd::TrailColorMode::Gradient);
    QCOMPARE(opt->trail.start_color_g, 240);
    QCOMPARE(opt->trail.fade_color_b, 24);
    QCOMPARE(opt->trail.head_thickness_px, 5.0f);
    QCOMPARE(opt->trail.lifetime_ms, 500.0f);

    // Out-of-range style values repair to Classic / clamps.
    const char* bad_style_json = R"({
        "schema_version": 3,
        "trail": { "style": 42, "glow_strength": 99.0, "segment_spacing_px": -5 }
    })";
    auto bad = ptd::ConfigStorage::deserialize_json(QByteArray(bad_style_json), &err);
    QVERIFY(bad.has_value());
    QCOMPARE(bad->trail.style, ptd::TrailStyle::Classic);
    QCOMPARE(bad->trail.glow_strength, 1.0f);
    QCOMPARE(bad->trail.segment_spacing_px, ptd::TrailConfig::kMinSegmentSpacingPx);
}

void TestConfig::schema3_to_schema4_click_defaults() {
    const char* schema3_json = R"({
        "schema_version": 3,
        "master_enabled": true,
        "trail": {
            "enabled": true,
            "style": 3,
            "glow_strength": 0.8,
            "segment_spacing_px": 16.0,
            "head_thickness_px": 4.5,
            "color_mode": 1
        },
        "click": {
            "enabled": true,
            "trigger_left": true,
            "trigger_right": false,
            "trigger_middle": true,
            "color_r": 0, "color_g": 220, "color_b": 250,
            "start_radius_px": 10.0,
            "end_radius_px": 35.0,
            "duration_ms": 300.0,
            "base_opacity": 0.9,
            "outline_thickness_px": 3.0,
            "fill_opacity": 0.2,
            "easing": 1
        },
        "render": { "diagnostic_primitives": false }
    })";

    QString err;
    auto opt = ptd::ConfigStorage::deserialize_json(QByteArray(schema3_json), &err);
    QVERIFY2(opt.has_value(), err.toUtf8().constData());
    // Click style defaults to Ring, particle_amount defaults to 8:
    QCOMPARE(opt->click.style, ptd::ClickStyle::Ring);
    QCOMPARE(opt->click.particle_amount, uint8_t(8));
    // Previous click settings unchanged:
    QCOMPARE(opt->click.trigger_left, true);
    QCOMPARE(opt->click.trigger_right, false);
    QCOMPARE(opt->click.trigger_middle, true);
    QCOMPARE(opt->click.color_r, uint8_t(0));
    QCOMPARE(opt->click.color_g, uint8_t(220));
    QCOMPARE(opt->click.color_b, uint8_t(250));
    QCOMPARE(opt->click.start_radius_px, 10.0f);
    QCOMPARE(opt->click.end_radius_px, 35.0f);
    QCOMPARE(opt->click.duration_ms, 300.0f);
    QCOMPARE(opt->click.base_opacity, 0.9f);
    QCOMPARE(opt->click.outline_thickness_px, 3.0f);
    QCOMPARE(opt->click.fill_opacity, 0.2f);
    QCOMPARE(opt->click.easing, ptd::ClickEasing::Smooth);
    // Unrelated trail settings preserved:
    QCOMPARE(opt->trail.style, ptd::TrailStyle::Neon);
    QCOMPARE(opt->trail.glow_strength, 0.8f);
    QCOMPARE(opt->trail.segment_spacing_px, 16.0f);
    QCOMPARE(opt->trail.head_thickness_px, 4.5f);
    QCOMPARE(opt->trail.color_mode, ptd::TrailColorMode::StartAccent);

    // Malformed click style recovers to Ring, and particle amount clamps:
    const char* bad_click_json = R"({
        "schema_version": 4,
        "click": { "style": 99, "particle_amount": 250 }
    })";
    auto bad = ptd::ConfigStorage::deserialize_json(QByteArray(bad_click_json), &err);
    QVERIFY(bad.has_value());
    QCOMPARE(bad->click.style, ptd::ClickStyle::Ring);
    QCOMPARE(bad->click.particle_amount, static_cast<uint8_t>(ptd::ClickConfig::kMaxParticleAmount));
}

void TestConfig::all_click_styles_roundtrip() {
    for (const ptd::ClickStyle style : {
             ptd::ClickStyle::Ring,       ptd::ClickStyle::DoubleRing,
             ptd::ClickStyle::Ripple,     ptd::ClickStyle::Burst,
             ptd::ClickStyle::SparkBurst, ptd::ClickStyle::SoftFlash,
             ptd::ClickStyle::DotRing}) {
        ptd::AppConfig original{};
        original.click.style = style;
        original.click.particle_amount = 16;
        original.trail.style = ptd::TrailStyle::Spark;
        original.trail.segment_spacing_px = 20.0f;

        const QByteArray json = ptd::ConfigStorage::serialize_json(original);
        QString err;
        auto deserialized = ptd::ConfigStorage::deserialize_json(json, &err);
        QVERIFY2(deserialized.has_value(), err.toUtf8().constData());
        QCOMPARE(deserialized->click.style, style);
        QCOMPARE(deserialized->click.particle_amount, uint8_t(16));
        QCOMPARE(deserialized->trail.style, ptd::TrailStyle::Spark);
        QCOMPARE(deserialized->trail.segment_spacing_px, 20.0f);
    }
}

void TestConfig::click_particle_amount_bounds() {
    ptd::ClickConfig c{};
    c.particle_amount = 0;
    QCOMPARE(ptd::ClickConfig::validated(c).particle_amount, uint8_t(0));
    c.particle_amount = ptd::ClickConfig::kMaxParticleAmount;
    QCOMPARE(ptd::ClickConfig::validated(c).particle_amount, static_cast<uint8_t>(ptd::ClickConfig::kMaxParticleAmount));
    c.particle_amount = 100;
    QCOMPARE(ptd::ClickConfig::validated(c).particle_amount, static_cast<uint8_t>(ptd::ClickConfig::kMaxParticleAmount));
}

// ---- T-021 schema 5: sparkle decoration fields ----

void TestConfig::schema4_to_schema5_sparkle_defaults() {
    // A schema-4 file has NO sparkle keys: loading it must keep every
    // pre-existing value and leave the new fields at the documented
    // migration defaults (mode Off + default amount/size/spread).
    const char* schema4_json = R"({
        "schema_version": 4,
        "master_enabled": false,
        "trail": {
            "enabled": true,
            "color_mode": 3,
            "start_color_r": 255, "start_color_g": 80, "start_color_b": 40,
            "fade_color_r": 200, "fade_color_g": 0, "fade_color_b": 255,
            "style": 2,
            "glow_strength": 0.6,
            "segment_spacing_px": 18.0,
            "head_thickness_px": 6.5,
            "tail_thickness_px": 2.25,
            "taper_strength": 0.4,
            "lifetime_ms": 500.0,
            "base_opacity": 0.8,
            "smoothing": 0.9,
            "fade_start": 0.3,
            "fade_curve": 1
        },
        "click": {
            "enabled": false,
            "trigger_left": true, "trigger_right": false,
            "trigger_middle": true,
            "color_r": 30, "color_g": 144, "color_b": 255,
            "style": 3,
            "particle_amount": 12,
            "start_radius_px": 12.5,
            "end_radius_px": 40.0,
            "duration_ms": 350.0,
            "base_opacity": 0.7,
            "outline_thickness_px": 3.5,
            "fill_opacity": 0.2,
            "easing": 2
        },
        "render": { "diagnostic_primitives": true }
    })";
    QString err;
    auto opt = ptd::ConfigStorage::deserialize_json(QByteArray(schema4_json), &err);
    QVERIFY2(opt.has_value(), err.toUtf8().constData());
    QCOMPARE(opt->schema_version, 4);  // loaded as-is ...
    // ... with the documented 4 -> 5 migration defaults:
    QCOMPARE(opt->trail.sparkle_mode, ptd::TrailSparkleMode::Off);
    QCOMPARE(opt->trail.sparkle_amount, ptd::TrailConfig::kDefaultSparkleAmount);
    QCOMPARE(opt->trail.sparkle_size_px, ptd::TrailConfig::kDefaultSparkleSizePx);
    QCOMPARE(opt->trail.sparkle_spread_px, ptd::TrailConfig::kDefaultSparkleSpreadPx);
    // Every pre-existing value byte-semantically unchanged (T-021 audit:
    // the complete Trail, Click, render and master inventory, not a sample).
    QCOMPARE(opt->master_enabled, false);
    QCOMPARE(opt->trail.enabled, true);
    QCOMPARE(opt->trail.color_mode, ptd::TrailColorMode::Gradient);
    QCOMPARE(opt->trail.start_color_r, uint8_t(255));
    QCOMPARE(opt->trail.start_color_g, uint8_t(80));
    QCOMPARE(opt->trail.start_color_b, uint8_t(40));
    QCOMPARE(opt->trail.fade_color_r, uint8_t(200));
    QCOMPARE(opt->trail.fade_color_g, uint8_t(0));
    QCOMPARE(opt->trail.fade_color_b, uint8_t(255));
    QCOMPARE(opt->trail.style, ptd::TrailStyle::Comet);
    QCOMPARE(opt->trail.glow_strength, 0.6f);
    QCOMPARE(opt->trail.segment_spacing_px, 18.0f);
    QCOMPARE(opt->trail.head_thickness_px, 6.5f);
    QCOMPARE(opt->trail.tail_thickness_px, 2.25f);
    QCOMPARE(opt->trail.taper_strength, 0.4f);
    QCOMPARE(opt->trail.lifetime_ms, 500.0f);
    QCOMPARE(opt->trail.base_opacity, 0.8f);
    QCOMPARE(opt->trail.smoothing, 0.9f);
    QCOMPARE(opt->trail.fade_start, 0.3f);
    QCOMPARE(opt->trail.fade_curve, ptd::FadeCurve::Smooth);
    QCOMPARE(opt->click.enabled, false);
    QCOMPARE(opt->click.trigger_left, true);
    QCOMPARE(opt->click.trigger_right, false);
    QCOMPARE(opt->click.trigger_middle, true);
    QCOMPARE(opt->click.color_r, uint8_t(30));
    QCOMPARE(opt->click.color_g, uint8_t(144));
    QCOMPARE(opt->click.color_b, uint8_t(255));
    QCOMPARE(opt->click.style, ptd::ClickStyle::Burst);
    QCOMPARE(opt->click.particle_amount, uint8_t(12));
    QCOMPARE(opt->click.start_radius_px, 12.5f);
    QCOMPARE(opt->click.end_radius_px, 40.0f);
    QCOMPARE(opt->click.duration_ms, 350.0f);
    QCOMPARE(opt->click.base_opacity, 0.7f);
    QCOMPARE(opt->click.outline_thickness_px, 3.5f);
    QCOMPARE(opt->click.fill_opacity, 0.2f);
    QCOMPARE(opt->click.easing, ptd::ClickEasing::EaseOut);
    QCOMPARE(opt->render.diagnostic_primitives, true);

    // Saving migrates: the file rewrites at the CURRENT schema version WITH
    // the sparkle keys, and a reload roundtrips the same sparkle defaults.
    // T-022: the expected version is read from kCurrentSchemaVersion instead
    // of a literal, so adding schema 6 (click.element_tint) cannot silently
    // turn this migration assertion into a stale constant check.
    const QByteArray saved = ptd::ConfigStorage::serialize_json(*opt);
    QVERIFY(saved.contains(QByteArray("\"schema_version\": ")
                           + QByteArray::number(ptd::AppConfig::kCurrentSchemaVersion)));
    QVERIFY(saved.contains("sparkle_mode"));
    auto reloaded = ptd::ConfigStorage::deserialize_json(saved, &err);
    QVERIFY2(reloaded.has_value(), err.toUtf8().constData());
    QCOMPARE(reloaded->schema_version, ptd::AppConfig::kCurrentSchemaVersion);
    QCOMPARE(reloaded->trail.sparkle_mode, ptd::TrailSparkleMode::Off);
    QCOMPARE(reloaded->trail.head_thickness_px, 6.5f);
    QCOMPARE(reloaded->trail.lifetime_ms, 500.0f);
    QCOMPARE(reloaded->trail.fade_color_b, uint8_t(255));
    QCOMPARE(reloaded->click.style, ptd::ClickStyle::Burst);
}

void TestConfig::all_sparkle_modes_roundtrip() {
    const ptd::TrailSparkleMode modes[] = {
        ptd::TrailSparkleMode::Off,
        ptd::TrailSparkleMode::Stardust,
        ptd::TrailSparkleMode::Twinkle,
        ptd::TrailSparkleMode::Glitter,
        ptd::TrailSparkleMode::Firefly,
        ptd::TrailSparkleMode::Shards,
    };
    for (const ptd::TrailSparkleMode mode : modes) {
        ptd::AppConfig cfg{};
        cfg.trail.sparkle_mode = mode;
        cfg.trail.sparkle_amount = 0.3f;
        cfg.trail.sparkle_size_px = 6.5f;
        cfg.trail.sparkle_spread_px = 24.0f;
        const QByteArray json = ptd::ConfigStorage::serialize_json(cfg);
        QString err;
        auto back = ptd::ConfigStorage::deserialize_json(json, &err);
        QVERIFY2(back.has_value(), err.toUtf8().constData());
        QCOMPARE(back->trail.sparkle_mode, mode);
        QCOMPARE(back->trail.sparkle_amount, 0.3f);
        QCOMPARE(back->trail.sparkle_size_px, 6.5f);
        QCOMPARE(back->trail.sparkle_spread_px, 24.0f);
        QCOMPARE(back->schema_version, ptd::AppConfig::kCurrentSchemaVersion);
    }
}

void TestConfig::invalid_sparkle_enum_repairs_to_off() {
    // validated(): an out-of-range persisted sparkle_mode repairs to Off.
    ptd::TrailConfig bad{};
    bad.sparkle_mode = static_cast<ptd::TrailSparkleMode>(99);
    QCOMPARE(ptd::TrailConfig::validated(bad).sparkle_mode,
             ptd::TrailSparkleMode::Off);

    // The same repair on the deserialize path.
    const char* bad_json = R"({
        "schema_version": 5,
        "trail": { "sparkle_mode": 7, "sparkle_amount": 0.5 },
        "click": { "enabled": true },
        "render": { "diagnostic_primitives": false }
    })";
    QString err;
    auto opt = ptd::ConfigStorage::deserialize_json(QByteArray(bad_json), &err);
    QVERIFY2(opt.has_value(), err.toUtf8().constData());
    QCOMPARE(opt->trail.sparkle_mode, ptd::TrailSparkleMode::Off);
    QCOMPARE(opt->trail.sparkle_amount, 0.5f);
}

void TestConfig::sparkle_value_bounds_and_nan_repair() {
    // Out-of-range clamps to the documented bounds:
    ptd::TrailConfig low{};
    low.sparkle_amount = -2.0f;
    low.sparkle_size_px = 0.0f;
    low.sparkle_spread_px = -1.0f;
    const auto clamped_low = ptd::TrailConfig::validated(low);
    QCOMPARE(clamped_low.sparkle_amount, ptd::TrailConfig::kMinSparkleAmount);
    QCOMPARE(clamped_low.sparkle_size_px, ptd::TrailConfig::kMinSparkleSizePx);
    QCOMPARE(clamped_low.sparkle_spread_px, ptd::TrailConfig::kMinSparkleSpreadPx);

    ptd::TrailConfig high{};
    high.sparkle_amount = 5.0f;
    high.sparkle_size_px = 400.0f;
    high.sparkle_spread_px = 999.0f;
    const auto clamped_high = ptd::TrailConfig::validated(high);
    QCOMPARE(clamped_high.sparkle_amount, ptd::TrailConfig::kMaxSparkleAmount);
    QCOMPARE(clamped_high.sparkle_size_px, ptd::TrailConfig::kMaxSparkleSizePx);
    QCOMPARE(clamped_high.sparkle_spread_px, ptd::TrailConfig::kMaxSparkleSpreadPx);

    // NaN is not a value: repairs to the product defaults.
    ptd::TrailConfig nan_cfg{};
    nan_cfg.sparkle_amount = std::numeric_limits<float>::quiet_NaN();
    nan_cfg.sparkle_size_px = std::numeric_limits<float>::quiet_NaN();
    nan_cfg.sparkle_spread_px = std::numeric_limits<float>::quiet_NaN();
    const auto repaired = ptd::TrailConfig::validated(nan_cfg);
    QCOMPARE(repaired.sparkle_amount, ptd::TrailConfig::kDefaultSparkleAmount);
    QCOMPARE(repaired.sparkle_size_px, ptd::TrailConfig::kDefaultSparkleSizePx);
    QCOMPARE(repaired.sparkle_spread_px, ptd::TrailConfig::kDefaultSparkleSpreadPx);
}

// ---- T-022 Elemental Click VFX: schema 5 -> 6 ----

// A schema-5 file has no click.element_tint key at all. Loading it must
// leave every stored value untouched and fill element_tint from the struct
// default (0.65) -- that IS the whole 5 -> 6 migration. Saving then rewrites
// the file at the current schema version WITH the new key.
void TestConfig::schema5_to_schema6_element_tint_default() {
    const QByteArray schema5 = R"({
        "schema_version": 5,
        "master_enabled": true,
        "click": {
            "enabled": true,
            "style": 3,
            "particle_amount": 12,
            "color_r": 30, "color_g": 144, "color_b": 255,
            "end_radius_px": 40.0,
            "duration_ms": 350.0
        }
    })";

    QString err;
    auto opt = ptd::ConfigStorage::deserialize_json(schema5, &err);
    QVERIFY2(opt.has_value(), err.toUtf8().constData());

    // Pre-existing click values survive the migration unchanged.
    QCOMPARE(opt->click.style, ptd::ClickStyle::Burst);
    QCOMPARE(opt->click.particle_amount, uint8_t(12));
    QCOMPARE(opt->click.color_r, uint8_t(30));
    QCOMPARE(opt->click.end_radius_px, 40.0f);
    QCOMPARE(opt->click.duration_ms, 350.0f);
    // The new field arrives at its documented default.
    QCOMPARE(opt->click.element_tint, 0.65f);

    const QByteArray saved = ptd::ConfigStorage::serialize_json(*opt);
    QVERIFY(saved.contains(QByteArray("\"schema_version\": ")
                           + QByteArray::number(ptd::AppConfig::kCurrentSchemaVersion)));
    QVERIFY(saved.contains("element_tint"));
    auto back = ptd::ConfigStorage::deserialize_json(saved, &err);
    QVERIFY2(back.has_value(), err.toUtf8().constData());
    QCOMPARE(back->click.element_tint, 0.65f);
    QCOMPARE(back->click.style, ptd::ClickStyle::Burst);
}

// Every click style -- the seven T-017 ones and the four elemental ones --
// survives a save/load roundtrip, and element_tint roundtrips with them.
void TestConfig::all_click_styles_roundtrip_with_element_tint() {
    const ptd::ClickStyle styles[] = {
        ptd::ClickStyle::Ring,       ptd::ClickStyle::DoubleRing,
        ptd::ClickStyle::Ripple,     ptd::ClickStyle::Burst,
        ptd::ClickStyle::SparkBurst, ptd::ClickStyle::SoftFlash,
        ptd::ClickStyle::DotRing,    ptd::ClickStyle::Air,
        ptd::ClickStyle::Fire,       ptd::ClickStyle::Water,
        ptd::ClickStyle::Earth,
    };
    for (const ptd::ClickStyle style : styles) {
        ptd::AppConfig cfg{};
        cfg.click.style = style;
        cfg.click.element_tint = 0.42f;
        cfg.click.particle_amount = 17;
        const QByteArray json = ptd::ConfigStorage::serialize_json(cfg);
        QString err;
        auto back = ptd::ConfigStorage::deserialize_json(json, &err);
        QVERIFY2(back.has_value(), err.toUtf8().constData());
        QCOMPARE(back->click.style, style);
        QCOMPARE(back->click.element_tint, 0.42f);
        QCOMPARE(back->click.particle_amount, uint8_t(17));
        QCOMPARE(back->schema_version, ptd::AppConfig::kCurrentSchemaVersion);
    }
}

// A style id past Earth, and an out-of-range tint, must both be repaired
// rather than reaching the renderer.
void TestConfig::invalid_click_style_and_tint_recover() {
    const QByteArray bad = R"({
        "schema_version": 6,
        "click": { "style": 99, "element_tint": 7.5 }
    })";
    QString err;
    auto opt = ptd::ConfigStorage::deserialize_json(bad, &err);
    QVERIFY2(opt.has_value(), err.toUtf8().constData());
    const ptd::AppConfig v = ptd::AppConfig::validated(*opt);
    QCOMPARE(v.click.style, ptd::ClickStyle::Ring);
    QCOMPARE(v.click.element_tint, 1.0f);

    const QByteArray negative = R"({
        "schema_version": 6,
        "click": { "style": 10, "element_tint": -3.0 }
    })";
    auto opt2 = ptd::ConfigStorage::deserialize_json(negative, &err);
    QVERIFY2(opt2.has_value(), err.toUtf8().constData());
    const ptd::AppConfig v2 = ptd::AppConfig::validated(*opt2);
    QCOMPARE(v2.click.style, ptd::ClickStyle::Earth);  // 10 is valid
    QCOMPARE(v2.click.element_tint, 0.0f);
}

// ---- T-023: version-aware enum migration boundaries ----
//
// Adding an enumerator must not rewrite the meaning of configurations that
// already exist on disk. Before schema 7 a persisted sparkle_mode of 5 was
// out of range and the documented contract repaired it to Off; before
// schema 6 a persisted click style of 7..10 was out of range and repaired
// to Ring. Both repairs must survive the addition of Shards/elementals.

namespace {

QByteArray config_json(int schema, const char* trail_body,
                      const char* click_body,
                      const char* root_extra = nullptr) {
    QByteArray json = QByteArray(R"({ "schema_version": )")
         + QByteArray::number(schema);
    // T-032: optional root-level keys, so a test can state an
    // application-level preference (start_with_windows) as part of the
    // record rather than as a struct default.
    if (root_extra && root_extra[0] != '\0') {
        json += QByteArray(R"(, )") + root_extra;
    }
    json += QByteArray(R"(, "trail": { )") + trail_body
          + QByteArray(R"( }, "click": { )") + click_body
          + QByteArray(R"( } })");
    return json;
}

} // namespace

void TestConfig::sparkle_shards_enum_is_schema_gated() {
    QString err;

    // Schema 6 predates Shards: 5 is historical corruption -> Off.
    auto s6 = ptd::ConfigStorage::deserialize_json(
        config_json(6, "\"sparkle_mode\": 5, \"sparkle_amount\": 0.5",
                    "\"enabled\": true"), &err);
    QVERIFY2(s6.has_value(), err.toUtf8().constData());
    QCOMPARE(s6->trail.sparkle_mode, ptd::TrailSparkleMode::Off);
    // The rest of the record is untouched by the repair.
    QCOMPARE(s6->trail.sparkle_amount, 0.5f);

    // Schema 5 likewise.
    auto s5 = ptd::ConfigStorage::deserialize_json(
        config_json(5, "\"sparkle_mode\": 5", "\"enabled\": true"), &err);
    QVERIFY2(s5.has_value(), err.toUtf8().constData());
    QCOMPARE(s5->trail.sparkle_mode, ptd::TrailSparkleMode::Off);

    // Legitimate historical modes still load unchanged from an old schema.
    auto s5_firefly = ptd::ConfigStorage::deserialize_json(
        config_json(5, "\"sparkle_mode\": 4", "\"enabled\": true"), &err);
    QVERIFY2(s5_firefly.has_value(), err.toUtf8().constData());
    QCOMPARE(s5_firefly->trail.sparkle_mode, ptd::TrailSparkleMode::Firefly);

    // Schema 7 introduced Shards: 5 IS Shards from there on.
    auto s7 = ptd::ConfigStorage::deserialize_json(
        config_json(ptd::AppConfig::kShardsSparkleModeSchema,
                    "\"sparkle_mode\": 5", "\"enabled\": true"), &err);
    QVERIFY2(s7.has_value(), err.toUtf8().constData());
    QCOMPARE(s7->trail.sparkle_mode, ptd::TrailSparkleMode::Shards);
    QCOMPARE(ptd::AppConfig::kShardsSparkleModeSchema, 7);

    // Invalid at the CURRENT schema still repairs to Off.
    auto current_bad = ptd::ConfigStorage::deserialize_json(
        config_json(ptd::AppConfig::kCurrentSchemaVersion,
                    "\"sparkle_mode\": 6", "\"enabled\": true"), &err);
    QVERIFY2(current_bad.has_value(), err.toUtf8().constData());
    QCOMPARE(current_bad->trail.sparkle_mode, ptd::TrailSparkleMode::Off);

    // Full save/load roundtrip at the current schema keeps Shards.
    ptd::AppConfig cfg{};
    cfg.trail.sparkle_mode = ptd::TrailSparkleMode::Shards;
    const QByteArray saved = ptd::ConfigStorage::serialize_json(cfg);
    QVERIFY(saved.contains(QByteArray("\"schema_version\": ")
                           + QByteArray::number(ptd::AppConfig::kCurrentSchemaVersion)));
    auto back = ptd::ConfigStorage::deserialize_json(saved, &err);
    QVERIFY2(back.has_value(), err.toUtf8().constData());
    QCOMPARE(back->trail.sparkle_mode, ptd::TrailSparkleMode::Shards);
    QCOMPARE(back->schema_version, ptd::AppConfig::kCurrentSchemaVersion);

    // A migrated OLD file saves at the current schema, and only THEN does 5
    // mean Shards -- the migration itself never invents the new mode.
    const QByteArray remigrated = ptd::ConfigStorage::serialize_json(*s6);
    auto reloaded = ptd::ConfigStorage::deserialize_json(remigrated, &err);
    QVERIFY2(reloaded.has_value(), err.toUtf8().constData());
    QCOMPARE(reloaded->schema_version, ptd::AppConfig::kCurrentSchemaVersion);
    QCOMPARE(reloaded->trail.sparkle_mode, ptd::TrailSparkleMode::Off);
}

void TestConfig::elemental_click_styles_are_schema_gated() {
    QString err;

    // Schema 5 predates the elemental styles: 7..10 repair to Ring.
    for (const int bad_style : {7, 8, 9, 10}) {
        const QByteArray body =
            QByteArray("\"style\": ") + QByteArray::number(bad_style);
        auto old = ptd::ConfigStorage::deserialize_json(
            config_json(5, "\"enabled\": true", body.constData()), &err);
        QVERIFY2(old.has_value(), err.toUtf8().constData());
        QCOMPARE(old->click.style, ptd::ClickStyle::Ring);
    }

    // The legacy 0..6 styles still load unchanged from schema 5.
    auto legacy = ptd::ConfigStorage::deserialize_json(
        config_json(5, "\"enabled\": true", "\"style\": 6"), &err);
    QVERIFY2(legacy.has_value(), err.toUtf8().constData());
    QCOMPARE(legacy->click.style, ptd::ClickStyle::DotRing);

    // Schema 6 introduced them: 7..10 load as Air/Fire/Water/Earth.
    const ptd::ClickStyle expected[] = {
        ptd::ClickStyle::Air, ptd::ClickStyle::Fire,
        ptd::ClickStyle::Water, ptd::ClickStyle::Earth};
    for (int i = 0; i < 4; ++i) {
        const QByteArray body =
            QByteArray("\"style\": ") + QByteArray::number(7 + i);
        auto modern = ptd::ConfigStorage::deserialize_json(
            config_json(ptd::AppConfig::kElementalClickStyleSchema,
                        "\"enabled\": true", body.constData()), &err);
        QVERIFY2(modern.has_value(), err.toUtf8().constData());
        QCOMPARE(modern->click.style, expected[i]);
    }
    QCOMPARE(ptd::AppConfig::kElementalClickStyleSchema, 6);

    // Invalid at the CURRENT schema still repairs to Ring.
    auto current_bad = ptd::ConfigStorage::deserialize_json(
        config_json(ptd::AppConfig::kCurrentSchemaVersion,
                    "\"enabled\": true", "\"style\": 11"), &err);
    QVERIFY2(current_bad.has_value(), err.toUtf8().constData());
    QCOMPARE(current_bad->click.style, ptd::ClickStyle::Ring);
}


// ---- T-024 schema 8: Hold FX introduction boundary ----
//
// hold_enabled changes how the MOUSE BEHAVES, not merely how an effect
// looks, so the migration is asymmetric on purpose: a new install gets the
// gesture, an existing user who only upgraded the executable does not.
void TestConfig::hold_fx_schema8_migration_and_roundtrip() {
    QString err;

    // T-027 added a LATER boundary (schema 9) without reinterpreting this one.
    QVERIFY(ptd::AppConfig::kCurrentSchemaVersion >= 9);
    QCOMPARE(ptd::AppConfig::kHoldFxSchema, 8);

    // Fresh defaults: Hold FX ON.
    const ptd::AppConfig fresh{};
    QCOMPARE(fresh.schema_version, ptd::AppConfig::kCurrentSchemaVersion);
    QVERIFY(fresh.click.hold_enabled);

    // HISTORICAL config (schema <= 7, key absent) -> OFF. Upgrading the
    // executable must not hand an existing user a brand-new gesture.
    for (int schema = 1; schema <= 7; ++schema) {
        auto old = ptd::ConfigStorage::deserialize_json(
            config_json(schema, "\"enabled\": true",
                        "\"enabled\": true, \"end_radius_px\": 37.0"), &err);
        QVERIFY2(old.has_value(), err.toUtf8().constData());
        QVERIFY2(!old->click.hold_enabled,
                 qPrintable(QStringLiteral("schema %1 must migrate Hold FX OFF")
                                .arg(schema)));
        // The repair touches nothing else in the record.
        QCOMPARE(old->click.end_radius_px, 37.0f);
        QVERIFY(old->click.enabled);
    }

    // Schema 8 with the key absent is the FRESH case, not the historical
    // one: the struct default (ON) stands.
    auto s8_missing = ptd::ConfigStorage::deserialize_json(
        config_json(8, "\"enabled\": true", "\"enabled\": true"), &err);
    QVERIFY2(s8_missing.has_value(), err.toUtf8().constData());
    QVERIFY(s8_missing->click.hold_enabled);

    // An explicit key is always honoured, at any schema -- it is the user's
    // own recorded choice, never something to be re-derived.
    auto s8_off = ptd::ConfigStorage::deserialize_json(
        config_json(8, "\"enabled\": true",
                    "\"enabled\": true, \"hold_enabled\": false"), &err);
    QVERIFY2(s8_off.has_value(), err.toUtf8().constData());
    QVERIFY(!s8_off->click.hold_enabled);

    auto s8_on = ptd::ConfigStorage::deserialize_json(
        config_json(8, "\"enabled\": true",
                    "\"enabled\": true, \"hold_enabled\": true"), &err);
    QVERIFY2(s8_on.has_value(), err.toUtf8().constData());
    QVERIFY(s8_on->click.hold_enabled);

    auto s7_explicit = ptd::ConfigStorage::deserialize_json(
        config_json(7, "\"enabled\": true",
                    "\"enabled\": true, \"hold_enabled\": true"), &err);
    QVERIFY2(s7_explicit.has_value(), err.toUtf8().constData());
    QVERIFY(s7_explicit->click.hold_enabled);

    // After the user enables Hold FX and saves: the current schema persists
    // it and a reload preserves it, in both directions.
    for (const bool want : {true, false}) {
        ptd::AppConfig cfg{};
        cfg.click.hold_enabled = want;
        const QByteArray saved = ptd::ConfigStorage::serialize_json(cfg);
        QVERIFY(saved.contains(QByteArray("\"schema_version\": ")
                               + QByteArray::number(ptd::AppConfig::kCurrentSchemaVersion)));
        QVERIFY(saved.contains(QByteArray("\"hold_enabled\":")));
        auto back = ptd::ConfigStorage::deserialize_json(saved, &err);
        QVERIFY2(back.has_value(), err.toUtf8().constData());
        QCOMPARE(back->click.hold_enabled, want);
        QCOMPARE(back->schema_version, ptd::AppConfig::kCurrentSchemaVersion);
    }

    // Round-tripping a MIGRATED historical config writes the current schema
    // with the migrated OFF value recorded explicitly, so the next load is
    // stable and never re-migrates.
    {
        auto migrated = ptd::ConfigStorage::deserialize_json(
            config_json(6, "\"enabled\": true", "\"enabled\": true"), &err);
        QVERIFY2(migrated.has_value(), err.toUtf8().constData());
        QVERIFY(!migrated->click.hold_enabled);
        const QByteArray saved = ptd::ConfigStorage::serialize_json(*migrated);
        auto back = ptd::ConfigStorage::deserialize_json(saved, &err);
        QVERIFY2(back.has_value(), err.toUtf8().constData());
        QVERIFY(!back->click.hold_enabled);
        QCOMPARE(back->schema_version, ptd::AppConfig::kCurrentSchemaVersion);
    }

    // T-024 removed the hidden hold_charge_ms knob. It must never come back: a
    // persisted value no UI can edit is exactly what that milestone forbade.
    // (hold_intensity is a DIFFERENT field: T-027 made it a real user-facing
    // control, so it is legitimately persisted now.)
    {
        const QByteArray saved = ptd::ConfigStorage::serialize_json(fresh);
        QVERIFY(!saved.contains(QByteArray("hold_charge_ms")));
    }
}

// T-027 Hold Controls / Motion Wake: schema 9 boundary, multiplier
// migration, and persistence. The two field kinds migrate DIFFERENTLY on
// purpose, so both directions are asserted.
void TestConfig::hold_controls_schema9_migration_and_roundtrip() {
    QString err;

    QVERIFY(ptd::AppConfig::kCurrentSchemaVersion >= 9);
    QCOMPARE(ptd::AppConfig::kHoldWakeSchema, 9);

    // Fresh install: Hold FX ON, Motion Wake ON, every multiplier at the
    // documented baseline.
    {
        const ptd::AppConfig fresh{};
        QCOMPARE(fresh.schema_version, ptd::AppConfig::kCurrentSchemaVersion);
        QVERIFY(fresh.click.hold_enabled);
        QVERIFY(fresh.click.hold_wake_enabled);
        QCOMPARE(fresh.click.hold_intensity, 1.0f);
        QCOMPARE(fresh.click.hold_wake_density, 1.0f);
        QCOMPARE(fresh.click.hold_wake_lifetime_ms,
                 ptd::ClickConfig::kDefaultHoldWakeLifetimeMs);
        QCOMPARE(fresh.click.hold_release_strength, 1.0f);
        // The behaviour boundary is unchanged: schema 9 does not reinterpret
        // the schema-8 Hold FX rule.
        QCOMPARE(ptd::AppConfig::kHoldFxSchema, 8);
    }

    // HISTORICAL (schema <= 8) with the key absent -> Motion Wake OFF. Motion
    // Wake is a materially new visual behaviour, so upgrading the executable
    // must not hand an existing user one they never agreed to.
    for (int schema = 1; schema <= 8; ++schema) {
        auto old = ptd::ConfigStorage::deserialize_json(
            config_json(schema, "\"enabled\": true",
                        "\"enabled\": true, \"end_radius_px\": 37.0"), &err);
        QVERIFY2(old.has_value(), err.toUtf8().constData());
        QVERIFY2(!old->click.hold_wake_enabled,
                 qPrintable(QStringLiteral("schema %1 must migrate Motion Wake OFF")
                                .arg(schema)));
        // The migration touches nothing else in the record...
        QCOMPARE(old->click.end_radius_px, 37.0f);
        QVERIFY(old->click.enabled);
        // ...and the MULTIPLIERS are pure retunes, so an absent key keeps the
        // identity default at every source schema.
        QCOMPARE(old->click.hold_intensity, 1.0f);
        QCOMPARE(old->click.hold_wake_density, 1.0f);
        QCOMPARE(old->click.hold_wake_lifetime_ms,
                 ptd::ClickConfig::kDefaultHoldWakeLifetimeMs);
        QCOMPARE(old->click.hold_release_strength, 1.0f);
    }

    // A FRESH schema-9 config with the key absent keeps the struct default.
    {
        auto s9_missing = ptd::ConfigStorage::deserialize_json(
            config_json(9, "\"enabled\": true", "\"enabled\": true"), &err);
        QVERIFY2(s9_missing.has_value(), err.toUtf8().constData());
        QVERIFY(s9_missing->click.hold_wake_enabled);
    }

    // T-027 INTEGRITY REPAIR: the introduction boundary is SOURCE-SCHEMA
    // authoritative. A schema <= 8 file predates the whole T-027 block, so
    // even an EXPLICIT key is ignored there -- an unknown or corrupt field
    // from an older writer must never acquire T-027 semantics after an
    // executable upgrade. Same discipline as every enum boundary above.
    for (const int schema : {5, 7, 8}) {
        auto hostile = ptd::ConfigStorage::deserialize_json(
            config_json(schema, "\"enabled\": true",
                        "\"enabled\": true, \"hold_wake_enabled\": true,"
                        " \"hold_intensity\": 2.0, \"hold_wake_density\": 2.0,"
                        " \"hold_wake_lifetime_ms\": 2500,"
                        " \"hold_release_strength\": 2.0,"
                        " \"end_radius_px\": 41.0"), &err);
        QVERIFY2(hostile.has_value(), err.toUtf8().constData());
        QVERIFY2(!hostile->click.hold_wake_enabled,
                 qPrintable(QStringLiteral("schema %1 must ignore the T-027 keys")
                                .arg(schema)));
        QCOMPARE(hostile->click.hold_intensity, 1.0f);
        QCOMPARE(hostile->click.hold_wake_density, 1.0f);
        QCOMPARE(hostile->click.hold_wake_lifetime_ms,
                 ptd::ClickConfig::kDefaultHoldWakeLifetimeMs);
        QCOMPARE(hostile->click.hold_release_strength, 1.0f);
        // The refused block touches nothing else in the record.
        QCOMPARE(hostile->click.end_radius_px, 41.0f);
        QVERIFY(hostile->click.enabled);
    }

    // Schema 9 introduced the block: the exact same keys ARE honoured there.
    {
        auto modern = ptd::ConfigStorage::deserialize_json(
            config_json(9, "\"enabled\": true",
                        "\"enabled\": true, \"hold_wake_enabled\": true,"
                        " \"hold_intensity\": 2.0, \"hold_wake_density\": 2.0,"
                        " \"hold_wake_lifetime_ms\": 2500,"
                        " \"hold_release_strength\": 2.0"), &err);
        QVERIFY2(modern.has_value(), err.toUtf8().constData());
        QVERIFY(modern->click.hold_wake_enabled);
        QCOMPARE(modern->click.hold_intensity, 2.0f);
        QCOMPARE(modern->click.hold_wake_density, 2.0f);
        QCOMPARE(modern->click.hold_wake_lifetime_ms, 2500.0f);
        QCOMPARE(modern->click.hold_release_strength, 2.0f);
    }

    // Enabling Motion Wake and saving persists it: schema 9, key present, and
    // a reload preserves it in both directions.
    for (const bool want : {true, false}) {
        ptd::AppConfig cfg{};
        cfg.click.hold_wake_enabled = want;
        const QByteArray saved = ptd::ConfigStorage::serialize_json(cfg);
        QVERIFY(saved.contains(QByteArray("\"schema_version\": ")
                               + QByteArray::number(ptd::AppConfig::kCurrentSchemaVersion)));
        QVERIFY(saved.contains(QByteArray("\"hold_wake_enabled\":")));
        auto back = ptd::ConfigStorage::deserialize_json(saved, &err);
        QVERIFY2(back.has_value(), err.toUtf8().constData());
        QCOMPARE(back->click.hold_wake_enabled, want);
        QCOMPARE(back->schema_version, ptd::AppConfig::kCurrentSchemaVersion);
    }

    // The four multipliers round-trip exactly and are persisted.
    {
        ptd::AppConfig cfg{};
        cfg.click.hold_intensity = 1.75f;
        cfg.click.hold_wake_density = 0.5f;
        cfg.click.hold_wake_lifetime_ms = 1500.0f;
        cfg.click.hold_release_strength = 1.6f;
        const QByteArray saved = ptd::ConfigStorage::serialize_json(cfg);
        for (const char* key : {"hold_intensity", "hold_wake_density",
                                "hold_wake_lifetime_ms", "hold_release_strength"}) {
            QVERIFY2(saved.contains(QByteArray(key)), key);
        }
        auto back = ptd::ConfigStorage::deserialize_json(saved, &err);
        QVERIFY2(back.has_value(), err.toUtf8().constData());
        QCOMPARE(back->click.hold_intensity, 1.75f);
        QCOMPARE(back->click.hold_wake_density, 0.5f);
        QCOMPARE(back->click.hold_wake_lifetime_ms, 1500.0f);
        QCOMPARE(back->click.hold_release_strength, 1.6f);
    }

    // Out-of-range and non-finite persisted values are repaired, never
    // trusted: no slider may drive the renderer out of its published range.
    {
        const QByteArray hostile = config_json(
            9, "\"enabled\": true",
            "\"enabled\": true, \"hold_intensity\": 99.0,"
            " \"hold_wake_density\": 0.0, \"hold_wake_lifetime_ms\": 1.0e9,"
            " \"hold_release_strength\": -3.0");
        auto back = ptd::ConfigStorage::deserialize_json(hostile, &err);
        QVERIFY2(back.has_value(), err.toUtf8().constData());
        QCOMPARE(back->click.hold_intensity, ptd::ClickConfig::kMaxHoldIntensity);
        QCOMPARE(back->click.hold_wake_density,
                 ptd::ClickConfig::kMinHoldWakeDensity);
        QCOMPARE(back->click.hold_wake_lifetime_ms,
                 ptd::ClickConfig::kMaxHoldWakeLifetimeMs);
        QCOMPARE(back->click.hold_release_strength,
                 ptd::ClickConfig::kMinHoldReleaseStrength);
    }

    // The Hold Controls block must not disturb any previously accepted Click
    // or Trail value: this is the migration's whole obligation.
    {
        const QByteArray legacy = config_json(
            7, "\"enabled\": true, \"style\": 3",
            "\"enabled\": true, \"style\": 4, \"particle_amount\": 17,"
            " \"end_radius_px\": 41.0");
        auto back = ptd::ConfigStorage::deserialize_json(legacy, &err);
        QVERIFY2(back.has_value(), err.toUtf8().constData());
        QCOMPARE(static_cast<int>(back->click.style),
                 static_cast<int>(ptd::ClickStyle::SparkBurst));
        QCOMPARE(static_cast<int>(back->click.particle_amount), 17);
        QCOMPARE(back->click.end_radius_px, 41.0f);
        QCOMPARE(static_cast<int>(back->trail.style),
                 static_cast<int>(ptd::TrailStyle::Neon));
        // The new fields arrive as their documented defaults (Motion Wake
        // OFF for a historical file), never as a reinterpretation.
        QVERIFY(!back->click.hold_wake_enabled);
        QCOMPARE(back->click.hold_intensity, 1.0f);
    }
}

// T-032 Start with Windows: the schema-10 boundary and its persistence.
//
// This preference differs from every earlier schema bump in one way that
// matters: turning it on changes what the OPERATING SYSTEM does at sign-in,
// not merely how an effect looks. So the migration is deliberately maximal:
// an existing configuration is never enrolled, in either direction, and the
// only way the value ever becomes true is the user choosing it.
void TestConfig::start_with_windows_schema10_migration_and_roundtrip() {
    QString err;

    QVERIFY(ptd::AppConfig::kCurrentSchemaVersion >= 10);
    QCOMPARE(ptd::AppConfig::kStartWithWindowsSchema, 10);

    // Fresh install: OFF. Autostart is never a silent default surprise.
    const ptd::AppConfig fresh{};
    QVERIFY(!fresh.start_with_windows);

    // HISTORICAL (schema <= 9, key absent) -> OFF. Upgrading the executable
    // must not hand an existing user a new OS side effect.
    for (int schema = 1; schema <= 9; ++schema) {
        auto old = ptd::ConfigStorage::deserialize_json(
            config_json(schema, "\"enabled\": true", "\"enabled\": true"), &err);
        QVERIFY2(old.has_value(), err.toUtf8().constData());
        QVERIFY2(!old->start_with_windows,
                 qPrintable(QStringLiteral(
                     "schema %1 must migrate Start with Windows OFF").arg(schema)));
    }

    // An explicit key is honoured at ANY source schema: it is a decision the
    // user's own machine recorded, never something to re-derive.
    for (const bool want : {true, false}) {
        const QByteArray body = want ? "\"start_with_windows\": true"
                                     : "\"start_with_windows\": false";
        auto stated = ptd::ConfigStorage::deserialize_json(
            config_json(9, "\"enabled\": true", "\"enabled\": true", body), &err);
        QVERIFY2(stated.has_value(), err.toUtf8().constData());
        QCOMPARE(stated->start_with_windows, want);

        auto current = ptd::ConfigStorage::deserialize_json(
            config_json(ptd::AppConfig::kCurrentSchemaVersion,
                        "\"enabled\": true", "\"enabled\": true", body), &err);
        QVERIFY2(current.has_value(), err.toUtf8().constData());
        QCOMPARE(current->start_with_windows, want);
    }

    // Round trip in both directions at the current schema.
    for (const bool want : {true, false}) {
        ptd::AppConfig cfg{};
        cfg.start_with_windows = want;
        const QByteArray saved = ptd::ConfigStorage::serialize_json(cfg);
        QVERIFY(saved.contains(QByteArray("\"start_with_windows\":")));
        auto back = ptd::ConfigStorage::deserialize_json(saved, &err);
        QVERIFY2(back.has_value(), err.toUtf8().constData());
        QCOMPARE(back->start_with_windows, want);
        QCOMPARE(back->schema_version, ptd::AppConfig::kCurrentSchemaVersion);
    }

    // A MIGRATED historical config saves the migrated OFF explicitly, so the
    // next load is stable and never re-migrates.
    {
        auto migrated = ptd::ConfigStorage::deserialize_json(
            config_json(9, "\"enabled\": true", "\"enabled\": true"), &err);
        QVERIFY2(migrated.has_value(), err.toUtf8().constData());
        QVERIFY(!migrated->start_with_windows);
        auto back = ptd::ConfigStorage::deserialize_json(
            ptd::ConfigStorage::serialize_json(*migrated), &err);
        QVERIFY2(back.has_value(), err.toUtf8().constData());
        QVERIFY(!back->start_with_windows);
    }

    // MACHINE STATE IS NOT PRODUCT PREFERENCE: the registered command, the
    // executable path and the registry key path are never persisted.
    {
        const QByteArray saved = ptd::ConfigStorage::serialize_json(fresh);
        QVERIFY(!saved.contains(QByteArray("registry")));
        QVERIFY(!saved.contains(QByteArray("Run\"")));
        QVERIFY(!saved.contains(QByteArray("executable")));
    }

    // The schema-10 boundary reinterprets no earlier field.
    {
        const QByteArray legacy = config_json(
            9, "\"enabled\": true, \"style\": 5",
            "\"enabled\": true, \"hold_wake_enabled\": true,"
            " \"hold_intensity\": 1.5");
        auto back = ptd::ConfigStorage::deserialize_json(legacy, &err);
        QVERIFY2(back.has_value(), err.toUtf8().constData());
        QCOMPARE(static_cast<int>(back->trail.style),
                 static_cast<int>(ptd::TrailStyle::Pulse));
        QVERIFY(back->click.hold_wake_enabled);
        QCOMPARE(back->click.hold_intensity, 1.5f);
        QVERIFY(!back->start_with_windows);
    }
}

// T-030: a %LOCALAPPDATA% probe failure must never silently resolve user
// state into the process working directory. Both default paths log and fall
// back to the executable's own directory, deterministically.
void TestConfig::default_paths_fail_deterministic_fallback() {
    g_captured_logs.clear();
    ptd::set_log_sink([](ptd::LogLevel, const std::string_view& message) {
        g_captured_logs.emplace_back(message);
    });
    ptd::set_known_folder_probe_for_tests(
        +[](std::wstring& out) { out.clear(); return false; });

    const std::wstring exe_dir = ptd::executable_directory();
    QVERIFY2(!exe_dir.empty(),
             "the executable directory must resolve in the test environment");

    const std::wstring cfg_path = ptd::ConfigStorage::default_config_path();
    const std::wstring log_path = ptd::default_log_path();
    QVERIFY(std::filesystem::path(cfg_path).is_absolute());
    QVERIFY(std::filesystem::path(log_path).is_absolute());
    QVERIFY(std::filesystem::path(cfg_path)
            == std::filesystem::path(exe_dir) / "config.json");
    QVERIFY(std::filesystem::path(log_path)
            == std::filesystem::path(exe_dir) / "protrail.log");

    bool cfg_logged = false;
    bool log_logged = false;
    for (const std::string& line : g_captured_logs) {
        if (line.find("config: %LOCALAPPDATA% unavailable") != std::string::npos) {
            cfg_logged = true;
        }
        if (line.find("log: %LOCALAPPDATA% unavailable") != std::string::npos) {
            log_logged = true;
        }
    }
    QVERIFY2(cfg_logged, "the config fallback must be logged");
    QVERIFY2(log_logged, "the log fallback must be logged");

    ptd::set_known_folder_probe_for_tests(nullptr);
    ptd::clear_log_sink();

    // The production probe is restored: the documented %LOCALAPPDATA% path.
    std::wstring base;
    QVERIFY2(ptd::local_app_data_folder(base),
             "the production known-folder probe must be restored");
    QVERIFY(std::filesystem::path(ptd::ConfigStorage::default_config_path())
            == std::filesystem::path(base) / "ProTrail" / "config.json");
    QVERIFY(std::filesystem::path(ptd::default_log_path())
            == std::filesystem::path(base) / "ProTrail" / "protrail.log");
}

void TestConfig::long_module_path_keeps_state_fallbacks_aligned() {
    struct ProbeReset {
        ~ProbeReset() {
            ptd::clear_module_path_query_for_tests();
            ptd::set_known_folder_probe_for_tests(nullptr);
            ptd::clear_log_sink();
        }
    } reset_probes;

    g_captured_logs.clear();
    ptd::set_log_sink([](ptd::LogLevel, const std::string_view& message) {
        g_captured_logs.emplace_back(message);
    });
    ptd::set_known_folder_probe_for_tests(
        +[](std::wstring& out) { out.clear(); return false; });

    std::wstring module_path = std::filesystem::temp_directory_path().wstring();
    for (int i = 0; i < 18; ++i) {
        module_path += L"\\segment_0123456789abcdef";
    }
    module_path += L"\\ProTrail.exe";
    QVERIFY(module_path.size() > 260);
    const std::filesystem::path expected_dir =
        std::filesystem::path(module_path).parent_path();

    std::size_t query_calls = 0;
    ptd::set_module_path_query_for_tests(
        [module_path, &query_calls](wchar_t* buffer, std::size_t capacity) {
            ++query_calls;
            const std::size_t copied = module_path.size() < capacity
                ? module_path.size() : capacity;
            std::copy_n(module_path.data(), copied, buffer);
            return module_path.size() >= capacity ? capacity : module_path.size();
        });

    QVERIFY(std::filesystem::path(ptd::executable_directory()) == expected_dir);
    QCOMPARE(query_calls, std::size_t{2});

    query_calls = 0;
    const std::filesystem::path config_path(ptd::ConfigStorage::default_config_path());
    const std::filesystem::path log_path(ptd::default_log_path());
    const ptd::StartupPaths startup = ptd::resolve_startup_paths(nullptr, nullptr);
    QVERIFY(startup.valid);
    QVERIFY(config_path.is_absolute());
    QVERIFY(log_path.is_absolute());
    QVERIFY(config_path == expected_dir / "config.json");
    QVERIFY(log_path == expected_dir / "protrail.log");
    QVERIFY(std::filesystem::path(startup.config_path) == config_path);
    QVERIFY(std::filesystem::path(startup.log_path) == log_path);
    QCOMPARE(query_calls, std::size_t{8});

    bool config_fallback_logged = false;
    bool log_fallback_logged = false;
    for (const std::string& line : g_captured_logs) {
        config_fallback_logged |=
            line.find("config: %LOCALAPPDATA% unavailable") != std::string::npos;
        log_fallback_logged |=
            line.find("log: %LOCALAPPDATA% unavailable") != std::string::npos;
    }
    QVERIFY(config_fallback_logged);
    QVERIFY(log_fallback_logged);
}

void TestConfig::module_path_failure_uses_absolute_shared_fallback() {
    struct ProbeReset {
        ~ProbeReset() {
            ptd::clear_module_path_query_for_tests();
            ptd::set_known_folder_probe_for_tests(nullptr);
            ptd::clear_log_sink();
        }
    } reset_probes;

    g_captured_logs.clear();
    ptd::set_log_sink([](ptd::LogLevel, const std::string_view& message) {
        g_captured_logs.emplace_back(message);
    });
    ptd::set_known_folder_probe_for_tests(
        +[](std::wstring& out) { out.clear(); return false; });
    ptd::set_module_path_query_for_tests(
        [](wchar_t*, std::size_t) { return std::size_t{0}; });

    QVERIFY(ptd::executable_directory().empty());
    const std::filesystem::path expected_dir = std::filesystem::temp_directory_path();
    const std::filesystem::path config_path(ptd::ConfigStorage::default_config_path());
    const std::filesystem::path log_path(ptd::default_log_path());
    const ptd::StartupPaths startup = ptd::resolve_startup_paths(nullptr, nullptr);

    QVERIFY(config_path.is_absolute());
    QVERIFY(log_path.is_absolute());
    QVERIFY(config_path == expected_dir / "config.json");
    QVERIFY(log_path == expected_dir / "protrail.log");
    QVERIFY(config_path.parent_path() == log_path.parent_path());
    QVERIFY(std::filesystem::path(startup.config_path) == config_path);
    QVERIFY(std::filesystem::path(startup.log_path) == log_path);

    bool config_failure_logged = false;
    bool log_failure_logged = false;
    for (const std::string& line : g_captured_logs) {
        config_failure_logged |=
            line.find("config: executable path resolution failed") != std::string::npos;
        log_failure_logged |=
            line.find("log: executable path resolution failed") != std::string::npos;
    }
    QVERIFY(config_failure_logged);
    QVERIFY(log_failure_logged);
}

// T-029: a failing persistence target must be OBSERVABLE -- the save reports
// non-success and emits an error line instead of silently pretending the
// settings were stored.
void TestConfig::persistence_failure_is_observable() {
    const auto dir =
        std::filesystem::temp_directory_path() / "protrail_test_persist_fail";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto blocker = dir / "blocker";
    {
        std::ofstream file(blocker);
        QVERIFY(file.good());
        file << "not a directory";
    }
    // The target's parent is a FILE, so the atomic write can never succeed.
    const auto bad_path = (blocker / "config.json").native();

    g_captured_logs.clear();
    ptd::set_log_sink([](ptd::LogLevel, const std::string_view& message) {
        g_captured_logs.emplace_back(message);
    });
    const bool saved =
        ptd::ConfigStorage::save_to_file(ptd::AppConfig{}, bad_path);
    ptd::clear_log_sink();

    QVERIFY2(!saved, "a write to an impossible target must report failure");
    bool failure_logged = false;
    for (const std::string& line : g_captured_logs) {
        if (line.find("config:") != std::string::npos) {
            failure_logged = true;
        }
    }
    QVERIFY2(failure_logged, "the persistence failure must be logged");
    std::filesystem::remove_all(dir, ec);
}

// T-38: when .corrupt backup cannot be created (destination collision),
// load_from_file must report the failure, NOT emit the success line,
// preserve the original user file intact, and return release defaults.
void TestConfig::malformed_backup_failure_reports_error_and_preserves_file() {
    g_captured_logs.clear();
    ptd::set_log_sink([](ptd::LogLevel, const std::string_view& message) {
        g_captured_logs.emplace_back(message);
    });

    const std::filesystem::path bad_file = test_dir_ / "bad_config_collision.json";
    const std::string bad_content = "{ invalid json content: 12345, missing closing bracket ";
    {
        std::ofstream out(bad_file, std::ios::trunc);
        out << bad_content;
    }
    QVERIFY(std::filesystem::exists(bad_file));

    // Collision: bad_file + ".corrupt" is a non-empty directory
    const std::filesystem::path corrupt_dest = test_dir_ / "bad_config_collision.json.corrupt";
    std::filesystem::create_directories(corrupt_dest / "blocker");
    QVERIFY(std::filesystem::is_directory(corrupt_dest));

    const ptd::AppConfig cfg = ptd::ConfigStorage::load_from_file(bad_file.wstring());
    ptd::clear_log_sink();

    // Defaults returned
    QCOMPARE(cfg, ptd::release_defaults());

    // Original file preserved intact
    QVERIFY(std::filesystem::exists(bad_file));
    std::ifstream in(bad_file);
    std::string current_content((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
    QCOMPARE(current_content, bad_content);

    // Logging: error reported, success line suppressed
    bool success_logged = false;
    bool failure_logged = false;
    for (const std::string& line : g_captured_logs) {
        if (line.find("backing up to .corrupt and returning defaults") != std::string::npos) {
            success_logged = true;
        }
        if (line.find("failed to back up to .corrupt") != std::string::npos) {
            failure_logged = true;
        }
    }
    QVERIFY2(!success_logged, "success line must NOT be emitted on backup failure");
    QVERIFY2(failure_logged, "failure must be reported in log");

    std::error_code ec;
    std::filesystem::remove_all(corrupt_dest, ec);
    std::filesystem::remove(bad_file, ec);
}

// CORE-001: status-bearing load must classify each provenance and set the
// write-permission flag correctly.
void TestConfig::load_result_classifies_every_provenance() {
    // MissingFresh
    {
        const std::wstring missing = (test_dir_ / "core001_missing.json").wstring();
        QVERIFY(!std::filesystem::exists(missing));
        const auto r = ptd::ConfigStorage::load_from_file_result(missing);
        QCOMPARE(static_cast<int>(r.status), static_cast<int>(ptd::ConfigLoadStatus::MissingFresh));
        QVERIFY(r.persistence_allowed);
        QCOMPARE(r.config, ptd::release_defaults());
        QVERIFY(ptd::ConfigStorage::save_to_file(r.config, missing));
        QVERIFY(std::filesystem::exists(missing));
        const auto saved = ptd::ConfigStorage::load_from_file_result(missing);
        QCOMPARE(static_cast<int>(saved.status), static_cast<int>(ptd::ConfigLoadStatus::LoadedCurrent));
        QVERIFY(saved.persistence_allowed);
    }
    // LoadedCurrent
    {
        const std::filesystem::path current = test_dir_ / "core001_current.json";
        QVERIFY(ptd::ConfigStorage::save_to_file(ptd::AppConfig{}, current.wstring()));
        const auto r = ptd::ConfigStorage::load_from_file_result(current.wstring());
        QCOMPARE(static_cast<int>(r.status), static_cast<int>(ptd::ConfigLoadStatus::LoadedCurrent));
        QVERIFY(r.persistence_allowed);
    }
    // LoadedMigrated: an older schema loads and is upgraded on save.
    {
        const std::filesystem::path older = test_dir_ / "core001_older.json";
        {
            std::ofstream out(older, std::ios::trunc);
            out << "{ \"schema_version\": 1, \"master_enabled\": true, "
                   "\"trail\": { \"enabled\": true } }";
        }
        const auto r = ptd::ConfigStorage::load_from_file_result(older.wstring());
        QCOMPARE(static_cast<int>(r.status), static_cast<int>(ptd::ConfigLoadStatus::LoadedMigrated));
        QVERIFY(r.persistence_allowed);
    }
    // MalformedBackedUp
    {
        const std::filesystem::path bad = test_dir_ / "core001_bad.json";
        {
            std::ofstream out(bad, std::ios::trunc);
            out << "{ not valid json ";
        }
        const auto r = ptd::ConfigStorage::load_from_file_result(bad.wstring());
        QCOMPARE(static_cast<int>(r.status), static_cast<int>(ptd::ConfigLoadStatus::MalformedBackedUp));
        QVERIFY(r.persistence_allowed);
        QVERIFY(std::filesystem::exists(bad.wstring() + L".corrupt"));
    }
}

// CORE-001: a future schema is VALID-BUT-UNSUPPORTED. It must not be renamed
// to .corrupt, not rewritten, and persistence must be disabled so the unknown
// future fields survive.
void TestConfig::future_schema_is_unsupported_not_corrupt() {
    const std::filesystem::path future = test_dir_ / "core001_future.json";
    const std::string body =
        "{ \"schema_version\": 12, \"master_enabled\": true, "
        "\"unknown_future_object\": { \"x\": 1, \"y\": [2, 3] }, "
        "\"trail\": { \"enabled\": true }, "
        "\"click\": { \"enabled\": true } }";
    {
        std::ofstream out(future, std::ios::trunc);
        out << body;
    }

    const auto r = ptd::ConfigStorage::load_from_file_result(future.wstring());
    QCOMPARE(static_cast<int>(r.status), static_cast<int>(ptd::ConfigLoadStatus::UnsupportedFutureSchema));
    QVERIFY(!r.persistence_allowed);

    // Original bytes are byte-identical and no .corrupt file was created.
    QVERIFY(std::filesystem::exists(future));
    QVERIFY(!std::filesystem::exists(future.wstring() + L".corrupt"));
    std::ifstream in(future, std::ios::binary);
    const std::string after((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
    QCOMPARE(after, body);
}

// CORE-001: when the malformed-file backup FAILS, the original is the only
// copy. Persistence must be disabled so it can never be overwritten.
void TestConfig::malformed_backup_failure_disables_persistence() {
    const std::filesystem::path bad = test_dir_ / "core001_badbackup.json";
    const std::string body = "{ malformed and unbackupable ";
    {
        std::ofstream out(bad, std::ios::trunc);
        out << body;
    }
    // Collision: a non-empty directory sits at the .corrupt destination so
    // rename() fails.
    const std::filesystem::path corrupt = test_dir_ / "core001_badbackup.json.corrupt";
    std::filesystem::create_directories(corrupt / "blocker");

    const auto r = ptd::ConfigStorage::load_from_file_result(bad.wstring());
    QCOMPARE(static_cast<int>(r.status), static_cast<int>(ptd::ConfigLoadStatus::MalformedBackupFailed));
    QVERIFY(!r.persistence_allowed);

    std::ifstream in(bad, std::ios::binary);
    const std::string after((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
    QCOMPARE(after, body);

    std::error_code ec;
    std::filesystem::remove_all(corrupt, ec);
    std::filesystem::remove(bad, ec);
}

// CORE-001: a read failure is PROTECTED, not an ordinary default.
void TestConfig::read_failure_disables_persistence() {
    // A directory at the config path makes open() fail without the test seam.
    const std::filesystem::path as_dir = test_dir_ / "core001_readfail.json";
    std::error_code ec;
    std::filesystem::remove_all(as_dir, ec);
    std::filesystem::create_directories(as_dir, ec);

    const auto r = ptd::ConfigStorage::load_from_file_result(as_dir.wstring());
    QCOMPARE(static_cast<int>(r.status), static_cast<int>(ptd::ConfigLoadStatus::ReadFailure));
    QVERIFY(!r.persistence_allowed);

    // Deterministic I/O seam variant.
    const std::filesystem::path seam = test_dir_ / "core001_seam.json";
    {
        std::ofstream out(seam, std::ios::trunc);
        out << "{}";
    }
    ptd::ConfigStorage::set_read_failure_path_for_tests(seam.wstring());
    const auto seam_result = ptd::ConfigStorage::load_from_file_result(seam.wstring());
    ptd::ConfigStorage::clear_read_failure_path_for_tests();
    QCOMPARE(static_cast<int>(seam_result.status), static_cast<int>(ptd::ConfigLoadStatus::ReadFailure));
    QVERIFY(!seam_result.persistence_allowed);

    std::filesystem::remove_all(as_dir, ec);
}

// T-41: when log directory creation fails, log_init reports failure, logs the error,
// and does not mark the sink initialized (avoiding silent discard).
void TestConfig::log_init_directory_creation_failure_is_reported() {
    ptd::reset_log_for_tests();
    g_captured_logs.clear();
    ptd::set_log_sink([](ptd::LogLevel, const std::string_view& message) {
        g_captured_logs.emplace_back(message);
    });

    const auto blocker = test_dir_ / "log_blocker_file";
    {
        std::ofstream out(blocker);
        QVERIFY(out.good());
        out << "blocker";
    }
    QVERIFY(std::filesystem::is_regular_file(blocker));

    // The target's parent has a regular file in its path, so create_directories fails.
    const auto uncreatable_log = (blocker / "sub" / "protrail.log").wstring();

    const bool init_res = ptd::log_init(uncreatable_log);
    QVERIFY2(!init_res, "log_init must return false when directory creation fails");
    QVERIFY2(!ptd::is_log_initialized(), "log sink must not be marked initialized");

    bool failure_logged = false;
    for (const std::string& line : g_captured_logs) {
        if (line.find("log: failed to create log directory") != std::string::npos) {
            failure_logged = true;
        }
    }
    QVERIFY2(failure_logged, "failure to create directory must be logged at Error level");

    ptd::clear_log_sink();
    ptd::reset_log_for_tests();
    std::error_code ec;
    std::filesystem::remove(blocker, ec);
}

// T-41: normal log_init creates parent directory, sets initialized, and log_write writes to file.
void TestConfig::log_init_normal_path_creates_directory_and_writes_file() {
    ptd::reset_log_for_tests();
    const auto log_dir = test_dir_ / "normal_log_dir";
    std::error_code ec;
    std::filesystem::remove_all(log_dir, ec);
    const auto log_file = (log_dir / "protrail.log").wstring();

    const bool init_res = ptd::log_init(log_file);
    QVERIFY2(init_res, "log_init must succeed for valid path");
    QVERIFY2(ptd::is_log_initialized(), "log sink must be marked initialized");
    QVERIFY2(std::filesystem::is_directory(log_dir), "log directory must be created");

    ptd::log_write(ptd::LogLevel::Info, "t41_test_message_verify_file_write");

    QVERIFY2(std::filesystem::exists(log_file), "log file must be created on write");
    std::ifstream in(log_file);
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    QVERIFY2(content.find("t41_test_message_verify_file_write") != std::string::npos,
             "log file must contain the written message");

    ptd::reset_log_for_tests();
    std::filesystem::remove_all(log_dir, ec);
}

void TestConfig::filesystem_probe_error_is_protected() {
    const std::filesystem::path existing = test_dir_ / "core001_probe_error.json";
    ptd::AppConfig original = ptd::release_defaults();
    original.master_enabled = false;
    original.click.duration_ms = 1234;
    QVERIFY(ptd::ConfigStorage::save_to_file(original, existing.wstring()));

    const auto read_bytes = [](const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>());
    };
    const std::string before = read_bytes(existing);
    QVERIFY(!before.empty());

    const std::wstring protected_path = existing.wstring();
    ptd::ConfigStorage::set_filesystem_probe_for_tests(
        [protected_path](const std::wstring& path) {
            if (path == protected_path) {
                return std::tuple<bool, std::error_code>{
                    false, std::make_error_code(std::errc::permission_denied)};
            }
            std::error_code ec;
            const bool exists = std::filesystem::exists(path, ec);
            return std::tuple<bool, std::error_code>{exists, ec};
        });
    struct ProbeReset {
        ~ProbeReset() { ptd::ConfigStorage::clear_filesystem_probe_for_tests(); }
    } reset_probe;

    const auto result = ptd::ConfigStorage::load_from_file_result(protected_path);
    QCOMPARE(static_cast<int>(result.status), static_cast<int>(ptd::ConfigLoadStatus::ReadFailure));
    QVERIFY(!result.persistence_allowed);
    QVERIFY(std::filesystem::exists(existing));
    QCOMPARE(read_bytes(existing), before);
    QVERIFY(!std::filesystem::exists(existing.wstring() + L".corrupt"));
}

void TestConfig::schema_provenance_requires_supported_integral_tag() {
    struct SchemaCase {
        const char* label;
        std::string json;
    };
    const std::vector<SchemaCase> cases = {
        {"missing", R"({"master_enabled":true})"},
        {"string", R"({"schema_version":"11","master_enabled":true})"},
        {"object", R"({"schema_version":{"value":11},"master_enabled":true})"},
        {"boolean", R"({"schema_version":true,"master_enabled":true})"},
        {"non_integral", R"({"schema_version":10.5,"master_enabled":true})"},
        {"future", "{\"schema_version\":"
                    + std::to_string(ptd::AppConfig::kCurrentSchemaVersion + 1)
                    + ",\"master_enabled\":true}"},
    };

    const auto read_bytes = [](const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>());
    };
    for (const SchemaCase& test_case : cases) {
        const std::filesystem::path path =
            test_dir_ / (std::string("core003_") + test_case.label + ".json");
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            QVERIFY(out.good());
            out.write(test_case.json.data(),
                      static_cast<std::streamsize>(test_case.json.size()));
            out.close();
            QVERIFY(out.good());
        }
        const std::string before = read_bytes(path);
        const auto result = ptd::ConfigStorage::load_from_file_result(path.wstring());
        QVERIFY2(result.status == ptd::ConfigLoadStatus::UnsupportedFutureSchema,
                 test_case.label);
        QVERIFY(!result.persistence_allowed);
        QCOMPARE(result.config, ptd::release_defaults());
        QCOMPARE(read_bytes(path), before);
        QVERIFY(!std::filesystem::exists(path.wstring() + L".corrupt"));
    }
}

void TestConfig::startup_logging_bootstrap_uses_resolved_explicit_path() {
    ptd::reset_log_for_tests();
    g_known_folder_probe_calls = 0;
    ptd::set_known_folder_probe_for_tests(
        &counting_known_folder_failure_probe);

    const auto log_dir = test_dir_ / "startup_explicit_log";
    std::error_code ec;
    std::filesystem::remove_all(log_dir, ec);
    ptd::StartupPaths paths{};
    paths.valid = true;
    paths.is_smoke_mode = true;
    paths.log_path = (log_dir / "protrail.log").wstring();

    QCOMPARE(ptd::initialize_startup_logging(paths),
             ptd::StartupLogBootstrapStatus::Ready);
    QVERIFY(ptd::is_log_initialized());
    QCOMPARE(g_known_folder_probe_calls, 0);
    ptd::log_write(ptd::LogLevel::Info, "startup_explicit_path_probe");
    QVERIFY(std::filesystem::exists(paths.log_path));
    std::ifstream in(paths.log_path);
    const std::string content((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    QVERIFY(content.find("startup_explicit_path_probe") != std::string::npos);

    ptd::reset_log_for_tests();
    std::filesystem::remove_all(log_dir, ec);
}

void TestConfig::startup_log_failure_is_terminal_for_normal_and_smoke() {
    ptd::reset_log_for_tests();
    g_known_folder_probe_calls = 0;
    ptd::set_known_folder_probe_for_tests(
        &counting_known_folder_failure_probe);
    const auto blocker = test_dir_ / "startup_log_blocker";
    {
        std::ofstream out(blocker, std::ios::trunc);
        QVERIFY(out.good());
        out << "file blocks the explicit log directory";
    }

    for (const bool smoke_mode : {false, true}) {
        ptd::StartupPaths paths{};
        paths.valid = true;
        paths.is_smoke_mode = smoke_mode;
        paths.log_path = (blocker / "child" / "protrail.log").wstring();
        QCOMPARE(ptd::initialize_startup_logging(paths),
                 ptd::StartupLogBootstrapStatus::Failed);
        QCOMPARE(ptd::kStartupLogFailureExitCode, 2);
        QVERIFY(!ptd::is_log_initialized());
    }
    QCOMPARE(g_known_folder_probe_calls, 0);

    ptd::reset_log_for_tests();
    std::error_code ec;
    std::filesystem::remove(blocker, ec);
}

void TestConfig::dev_diff_reports_no_diff_for_identical() {
    const ptd::AppConfig base = ptd::release_defaults();
    const ptd::AppConfig same = base;
    QCOMPARE(ptd::diff_configs_lines(same, base).size(), static_cast<size_t>(0));
    QCOMPARE(ptd::diff_configs(same, base), std::string("No differences from Release Defaults."));
}

void TestConfig::dev_diff_exhaustive_field_coverage() {
    const ptd::AppConfig base = ptd::release_defaults();

    auto assert_one_diff = [&](const ptd::AppConfig& mod, const std::string& field_name) {
        auto lines = ptd::diff_configs_lines(mod, base);
        QCOMPARE(lines.size(), static_cast<size_t>(1));
        QVERIFY2(lines[0].find(field_name) != std::string::npos,
                 ("Expected diff to contain field " + field_name + " but got: " + lines[0]).c_str());
    };

    // 1. master_enabled
    {
        ptd::AppConfig m = base;
        m.master_enabled = !base.master_enabled;
        assert_one_diff(m, "master_enabled");
    }
    // 2. start_with_windows
    {
        ptd::AppConfig m = base;
        m.start_with_windows = !base.start_with_windows;
        assert_one_diff(m, "start_with_windows");
    }
    // 3. trail.enabled
    {
        ptd::AppConfig m = base;
        m.trail.enabled = !base.trail.enabled;
        assert_one_diff(m, "trail.enabled");
    }
    // 4. trail.color_mode
    {
        ptd::AppConfig m = base;
        m.trail.color_mode = (base.trail.color_mode == ptd::TrailColorMode::Full) ? ptd::TrailColorMode::StartAccent : ptd::TrailColorMode::Full;
        assert_one_diff(m, "trail.color_mode");
    }
    // 5. trail.start_color
    {
        ptd::AppConfig m = base;
        m.trail.start_color_r = (base.trail.start_color_r == 255) ? 0 : 255;
        assert_one_diff(m, "trail.start_color");
    }
    // 6. trail.fade_color
    {
        ptd::AppConfig m = base;
        m.trail.fade_color_b = (base.trail.fade_color_b == 255) ? 0 : 255;
        assert_one_diff(m, "trail.fade_color");
    }
    // 7. trail.style
    {
        ptd::AppConfig m = base;
        m.trail.style = (base.trail.style == ptd::TrailStyle::Classic) ? ptd::TrailStyle::Neon : ptd::TrailStyle::Classic;
        assert_one_diff(m, "trail.style");
    }
    // 8. trail.glow_strength
    {
        ptd::AppConfig m = base;
        m.trail.glow_strength = 0.99f;
        assert_one_diff(m, "trail.glow_strength");
    }
    // 9. trail.segment_spacing_px
    {
        ptd::AppConfig m = base;
        m.trail.segment_spacing_px = 33.0f;
        assert_one_diff(m, "trail.segment_spacing_px");
    }
    // 10. trail.head_thickness_px
    {
        ptd::AppConfig m = base;
        m.trail.head_thickness_px = 9.0f;
        assert_one_diff(m, "trail.head_thickness_px");
    }
    // 11. trail.tail_thickness_px
    {
        ptd::AppConfig m = base;
        m.trail.tail_thickness_px = 7.0f;
        assert_one_diff(m, "trail.tail_thickness_px");
    }
    // 12. trail.taper_strength
    {
        ptd::AppConfig m = base;
        m.trail.taper_strength = 0.88f;
        assert_one_diff(m, "trail.taper_strength");
    }
    // 13. trail.lifetime_ms
    {
        ptd::AppConfig m = base;
        m.trail.lifetime_ms = 999.0f;
        assert_one_diff(m, "trail.lifetime_ms");
    }
    // 14. trail.base_opacity
    {
        ptd::AppConfig m = base;
        m.trail.base_opacity = 0.33f;
        assert_one_diff(m, "trail.base_opacity");
    }
    // 15. trail.smoothing
    {
        ptd::AppConfig m = base;
        m.trail.smoothing = 0.12f;
        assert_one_diff(m, "trail.smoothing");
    }
    // 16. trail.fade_start
    {
        ptd::AppConfig m = base;
        m.trail.fade_start = 0.45f;
        assert_one_diff(m, "trail.fade_start");
    }
    // 17. trail.fade_curve
    {
        ptd::AppConfig m = base;
        m.trail.fade_curve = (base.trail.fade_curve == ptd::FadeCurve::Smooth) ? ptd::FadeCurve::Linear : ptd::FadeCurve::Smooth;
        assert_one_diff(m, "trail.fade_curve");
    }
    // 18. trail.sparkle_mode
    {
        ptd::AppConfig m = base;
        m.trail.sparkle_mode = (base.trail.sparkle_mode == ptd::TrailSparkleMode::Off) ? ptd::TrailSparkleMode::Twinkle : ptd::TrailSparkleMode::Off;
        assert_one_diff(m, "trail.sparkle_mode");
    }
    // 19. trail.sparkle_amount
    {
        ptd::AppConfig m = base;
        m.trail.sparkle_amount = 0.44f;
        assert_one_diff(m, "trail.sparkle_amount");
    }
    // 20. trail.sparkle_size_px
    {
        ptd::AppConfig m = base;
        m.trail.sparkle_size_px = 8.8f;
        assert_one_diff(m, "trail.sparkle_size_px");
    }
    // 21. trail.sparkle_spread_px
    {
        ptd::AppConfig m = base;
        m.trail.sparkle_spread_px = 25.0f;
        assert_one_diff(m, "trail.sparkle_spread_px");
    }
    // 22. click.enabled
    {
        ptd::AppConfig m = base;
        m.click.enabled = !base.click.enabled;
        assert_one_diff(m, "click.enabled");
    }
    // 23. click.trigger_left
    {
        ptd::AppConfig m = base;
        m.click.trigger_left = !base.click.trigger_left;
        assert_one_diff(m, "click.trigger_left");
    }
    // 24. click.trigger_right
    {
        ptd::AppConfig m = base;
        m.click.trigger_right = !base.click.trigger_right;
        assert_one_diff(m, "click.trigger_right");
    }
    // 25. click.trigger_middle
    {
        ptd::AppConfig m = base;
        m.click.trigger_middle = !base.click.trigger_middle;
        assert_one_diff(m, "click.trigger_middle");
    }
    // 26. click.color
    {
        ptd::AppConfig m = base;
        m.click.color_r = (base.click.color_r == 255) ? 0 : 255;
        assert_one_diff(m, "click.color");
    }
    // 27. click.style
    {
        ptd::AppConfig m = base;
        m.click.style = (base.click.style == ptd::ClickStyle::Ring) ? ptd::ClickStyle::Ripple : ptd::ClickStyle::Ring;
        assert_one_diff(m, "click.style");
    }
    // 28. click.particle_amount
    {
        ptd::AppConfig m = base;
        m.click.particle_amount = 16;
        assert_one_diff(m, "click.particle_amount");
    }
    // 29. click.element_tint
    {
        ptd::AppConfig m = base;
        m.click.element_tint = 0.12f;
        assert_one_diff(m, "click.element_tint");
    }
    // 30. click.hold_enabled
    {
        ptd::AppConfig m = base;
        m.click.hold_enabled = !base.click.hold_enabled;
        assert_one_diff(m, "click.hold_enabled");
    }
    // 31. click.hold_wake_enabled
    {
        ptd::AppConfig m = base;
        m.click.hold_wake_enabled = !base.click.hold_wake_enabled;
        assert_one_diff(m, "click.hold_wake_enabled");
    }
    // 32. click.hold_intensity
    {
        ptd::AppConfig m = base;
        m.click.hold_intensity = 1.75f;
        assert_one_diff(m, "click.hold_intensity");
    }
    // 33. click.hold_wake_density
    {
        ptd::AppConfig m = base;
        m.click.hold_wake_density = 2.5f;
        assert_one_diff(m, "click.hold_wake_density");
    }
    // 34. click.hold_wake_lifetime_ms
    {
        ptd::AppConfig m = base;
        m.click.hold_wake_lifetime_ms = 1200.0f;
        assert_one_diff(m, "click.hold_wake_lifetime_ms");
    }
    // 35. click.hold_release_strength
    {
        ptd::AppConfig m = base;
        m.click.hold_release_strength = 0.5f;
        assert_one_diff(m, "click.hold_release_strength");
    }
    // 35a. click.wake_strength (T-36)
    {
        ptd::AppConfig m = base;
        m.click.wake_strength = 1.75f;
        assert_one_diff(m, "click.wake_strength");
    }
    // 35b. click.wake_size (T-36)
    {
        ptd::AppConfig m = base;
        m.click.wake_size = 1.4f;
        assert_one_diff(m, "click.wake_size");
    }
    // 35c. click.wake_spread (T-36)
    {
        ptd::AppConfig m = base;
        m.click.wake_spread = 0.3f;
        assert_one_diff(m, "click.wake_spread");
    }
    // 35d. click.speed_response (T-36)
    {
        ptd::AppConfig m = base;
        m.click.speed_response = 0.25f;
        assert_one_diff(m, "click.speed_response");
    }
    // 35e. click.min_motion_speed_px_s (T-36)
    {
        ptd::AppConfig m = base;
        m.click.min_motion_speed_px_s = 250.0f;
        assert_one_diff(m, "click.min_motion_speed_px_s");
    }
    // 35f. click.turn_accent (T-36)
    {
        ptd::AppConfig m = base;
        m.click.turn_accent = !base.click.turn_accent;
        assert_one_diff(m, "click.turn_accent");
    }
    // 35g. click.stop_accent (T-36)
    {
        ptd::AppConfig m = base;
        m.click.stop_accent = !base.click.stop_accent;
        assert_one_diff(m, "click.stop_accent");
    }
    // 36. click.start_radius_px
    {
        ptd::AppConfig m = base;
        m.click.start_radius_px = 15.0f;
        assert_one_diff(m, "click.start_radius_px");
    }
    // 37. click.end_radius_px
    {
        ptd::AppConfig m = base;
        m.click.end_radius_px = 50.0f;
        assert_one_diff(m, "click.end_radius_px");
    }
    // 38. click.duration_ms
    {
        ptd::AppConfig m = base;
        m.click.duration_ms = 600.0f;
        assert_one_diff(m, "click.duration_ms");
    }
    // 39. click.base_opacity
    {
        ptd::AppConfig m = base;
        m.click.base_opacity = 0.55f;
        assert_one_diff(m, "click.base_opacity");
    }
    // 40. click.outline_thickness_px
    {
        ptd::AppConfig m = base;
        m.click.outline_thickness_px = 5.0f;
        assert_one_diff(m, "click.outline_thickness_px");
    }
    // 41. click.fill_opacity
    {
        ptd::AppConfig m = base;
        m.click.fill_opacity = 0.44f;
        assert_one_diff(m, "click.fill_opacity");
    }
    // 42. click.easing
    {
        ptd::AppConfig m = base;
        m.click.easing = (base.click.easing == ptd::ClickEasing::EaseOut) ? ptd::ClickEasing::Linear : ptd::ClickEasing::EaseOut;
        assert_one_diff(m, "click.easing");
    }
    // 43. render.diagnostic_primitives
    {
        ptd::AppConfig m = base;
        m.render.diagnostic_primitives = !base.render.diagnostic_primitives;
        assert_one_diff(m, "render.diagnostic_primitives");
    }
}

void TestConfig::dev_preset_lifecycle_isolated() {
    const auto presets_dir = test_dir_ / "test_presets_isolated";
    std::error_code ec;
    std::filesystem::remove_all(presets_dir, ec);
    ptd::set_dev_presets_dir_for_tests(presets_dir);

    // 1. Release mode: save_dev_preset must fail
    ptd::set_dev_build_override_for_tests(false);
    ptd::AppConfig cfg = ptd::release_defaults();
    cfg.trail.glow_strength = 0.85f;
    std::string err;
    QVERIFY(!ptd::save_dev_preset("fail_preset", cfg, &err));
    QVERIFY(!err.empty());

    // 2. Dev mode: save named preset
    ptd::set_dev_build_override_for_tests(true);
    QVERIFY(ptd::save_dev_preset("preset_one", cfg, &err));
    QVERIFY(std::filesystem::exists(presets_dir / "preset_one.json"));

    // 3. Load named preset
    auto loaded = ptd::load_dev_preset("preset_one", &err);
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->trail.glow_strength, 0.85f);
    QCOMPARE(*loaded, cfg);

    // 4. Save timestamped preset (empty name)
    QVERIFY(ptd::save_dev_preset("", cfg, &err));

    // 5. List presets
    auto list = ptd::list_dev_presets();
    QVERIFY(list.size() >= 2);
    QVERIFY(std::find(list.begin(), list.end(), "preset_one") != list.end());

    // Clean up
    ptd::reset_dev_presets_dir_for_tests();
    ptd::set_dev_build_override_for_tests(std::nullopt);
    std::filesystem::remove_all(presets_dir, ec);
}

void TestConfig::dev_promote_lifecycle_and_safeguards() {
    const auto target_file = test_dir_ / "rel_defaults_promote_test.json";
    std::error_code ec;
    std::filesystem::remove(target_file, ec);
    ptd::set_canonical_release_defaults_path_for_tests(target_file);

    ptd::AppConfig cfg = ptd::release_defaults();
    cfg.trail.glow_strength = 0.92f;
    cfg.render.diagnostic_primitives = true; // runtime-only state

    // 1. Refused in release build
    ptd::set_dev_build_override_for_tests(false);
    auto res_rel = ptd::promote_to_release_defaults(cfg);
    QVERIFY(!res_rel.success);
    QVERIFY(!std::filesystem::exists(target_file));

    // 2. Succeeds in dev build, captures the complete canonical state, atomically replaces and verifies
    ptd::set_dev_build_override_for_tests(true);
    auto res_dev = ptd::promote_to_release_defaults(cfg);
    QVERIFY2(res_dev.success, res_dev.message.c_str());
    QVERIFY(std::filesystem::exists(target_file));

    // 3. Re-read target and verify equality
    auto reloaded = ptd::ConfigStorage::load_from_file(target_file.wstring());
    QCOMPARE(reloaded.trail.glow_strength, 0.92f);
    // The complete canonical snapshot includes diagnostic primitives.
    QCOMPARE(reloaded.render.diagnostic_primitives, true);

    // Clean up
    ptd::reset_canonical_release_defaults_path_for_tests();
    ptd::set_dev_build_override_for_tests(std::nullopt);
    std::filesystem::remove(target_file, ec);
}

void TestConfig::dev_promote_rollback_under_injected_failures() {
    // W2-004: prove the checked transaction stages. Every recoverable failure
    // must leave a pre-existing canonical file byte-identical, or restore the
    // prior "absent" state when there was none; rollback failure is reported
    // distinctly and never claims a false "restored" success.
    const auto target_file = test_dir_ / "rel_defaults_rollback_test.json";
    std::error_code ec;
    ptd::set_canonical_release_defaults_path_for_tests(target_file);
    ptd::set_dev_build_override_for_tests(true);

    const auto read_bytes = [](const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>());
    };

    // Establish a known-good canonical file (the "previous" bytes) whose
    // glow_strength differs from the promotion candidate so a silent overwrite
    // would be detectable.
    ptd::AppConfig prior = ptd::release_defaults();
    prior.trail.glow_strength = 0.11f;
    QVERIFY(ptd::promote_to_release_defaults(prior).success);
    QVERIFY(std::filesystem::exists(target_file));
    const std::string prior_disk = read_bytes(target_file);
    QVERIFY(!prior_disk.empty());

    ptd::AppConfig candidate = ptd::release_defaults();
    candidate.trail.glow_strength = 0.93f;

    // --- With a pre-existing canonical file: each recoverable failure must
    // leave the prior bytes byte-identical and NOT claim success. ---

    // 1. Temp flush failure BEFORE canonical replace: canonical untouched.
    ptd::set_promote_fault_for_tests(ptd::PromoteFault::TempFlush);
    auto r_flush = ptd::promote_to_release_defaults(candidate);
    QVERIFY(!r_flush.success);
    QCOMPARE(read_bytes(target_file), prior_disk);

    // 2. Post-replace canonical read failure: rollback restores prior bytes.
    ptd::set_promote_fault_for_tests(ptd::PromoteFault::PostReplaceRead);
    auto r_read = ptd::promote_to_release_defaults(candidate);
    QVERIFY(!r_read.success);
    QVERIFY(std::filesystem::exists(target_file));
    QCOMPARE(read_bytes(target_file), prior_disk);

    // 3. Post-replace semantic verification failure: rollback restores prior.
    ptd::set_promote_fault_for_tests(ptd::PromoteFault::SemanticVerify);
    auto r_sem = ptd::promote_to_release_defaults(candidate);
    QVERIFY(!r_sem.success);
    QCOMPARE(read_bytes(target_file), prior_disk);
    // A successful rollback reports "restored", not a rollback-failure hard error.
    QVERIFY(r_sem.message.find("restored") != std::string::npos);
    QVERIFY(r_sem.message.find("RESTORATION FAILED") == std::string::npos);

    // 4. Rollback write/flush failure during semantic-verify rollback: the
    //    result must be a DISTINCT hard failure, never a false "restored".
    ptd::set_promote_fault_for_tests(ptd::PromoteFault::RollbackWriteFlush);
    auto r_rbwf = ptd::promote_to_release_defaults(candidate);
    QVERIFY(!r_rbwf.success);
    QVERIFY(r_rbwf.message.find("RESTORATION FAILED") != std::string::npos);

    // Recover the canonical file to a trusted state for the next stage.
    QVERIFY(ptd::promote_to_release_defaults(prior).success);
    QCOMPARE(read_bytes(target_file), prior_disk);

    // 5. Rollback MoveFileEx failure: also a distinct hard failure.
    ptd::set_promote_fault_for_tests(ptd::PromoteFault::RollbackMove);
    auto r_rbmv = ptd::promote_to_release_defaults(candidate);
    QVERIFY(!r_rbmv.success);
    QVERIFY(r_rbmv.message.find("RESTORATION FAILED") != std::string::npos);

    // 6. Happy path still yields full semantic equality.
    ptd::reset_promote_fault_for_tests();
    auto r_ok = ptd::promote_to_release_defaults(candidate);
    QVERIFY2(r_ok.success, r_ok.message.c_str());
    auto reloaded = ptd::ConfigStorage::load_from_file(target_file.wstring());
    QCOMPARE(reloaded.trail.glow_strength, 0.93f);

    // --- With NO pre-existing canonical file: a recoverable post-replace
    // failure must restore the prior "absent" state, not leave an unverified
    // canonical target. ---
    std::filesystem::remove(target_file, ec);
    QVERIFY(!std::filesystem::exists(target_file));
    ptd::set_promote_fault_for_tests(ptd::PromoteFault::SemanticVerify);
    auto r_absent = ptd::promote_to_release_defaults(candidate);
    QVERIFY(!r_absent.success);
    QVERIFY(!std::filesystem::exists(target_file)); // absence restored

    ptd::reset_promote_fault_for_tests();
    ptd::reset_canonical_release_defaults_path_for_tests();
    ptd::set_dev_build_override_for_tests(std::nullopt);
    std::filesystem::remove(target_file, ec);
}

QTEST_MAIN(TestConfig)
#include "test_config.moc"

