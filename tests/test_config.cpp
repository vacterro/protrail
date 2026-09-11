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

#include <QtTest/QtTest>
#include <filesystem>
#include <fstream>

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
    void save_after_migration_writes_schema_version_3();
    void preset_values_survive_restart();
    void all_trail_styles_roundtrip();
    void schema2_to_schema3_style_defaults();

    // T-017 schema 4 click style tests.
    void schema3_to_schema4_click_defaults();
    void all_click_styles_roundtrip();
    void click_particle_amount_bounds();

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
    original.trail.smoothing = 0.65f;
    original.trail.fade_start = 0.2f;
    original.trail.fade_curve = ptd::FadeCurve::EaseOut;

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

void TestConfig::save_after_migration_writes_schema_version_3() {
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

QTEST_MAIN(TestConfig)
#include "test_config.moc"
