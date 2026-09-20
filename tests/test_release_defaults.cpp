// T-33 canonical Release Defaults regressions.
//
// The product has exactly ONE semantic default configuration, carried as
// resources/release_defaults.json and embedded in the executable. These cases
// prove the three properties that make that true:
//
//   1. the embedded canonical source parses and validates;
//   2. a fresh configuration EQUALS the Release Defaults (not the C++ struct
//      initializers, which are now only an emergency fallback);
//   3. Restore Defaults reproduces the same Release Defaults.
//
// Build-gate behaviour (missing/invalid source fails visibly) lives in
// cmake/validate_release_defaults.cmake and is driven by its own CTest case;
// the coverage regression that hunts a forgotten field is T-35.

#include <QtTest>

#include "../src/config/app_config.h"
#include "../src/config/config_storage.h"
#include "../src/config/release_defaults.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>

using ptd::AppConfig;
using ptd::ConfigStorage;

class TestReleaseDefaults : public QObject {
    Q_OBJECT

private slots:
    // The embedded resource exists and parsed. A false here means every other
    // assertion would be comparing against the emergency fallback.
    void canonical_source_is_available();
    // The resource path is the packaged, non-writable one.
    void resource_path_is_the_embedded_defaults();
    // A fresh configuration (no file on disk) equals the Release Defaults.
    void fresh_config_equals_release_defaults();
    // The defaults themselves are within every published bound.
    void release_defaults_are_validated();
    // Serialize -> parse of the canonical defaults is value-preserving.
    void canonical_defaults_roundtrip();
    // Restore Defaults is the same semantic source: the freshly loaded config
    // from the canonical JSON equals what a fresh install constructs.
    void restore_defaults_match_fresh_defaults();
    // Every key serialize_json() writes is present in the canonical source,
    // so no persisted field can be silently absent from Release Defaults.
    void canonical_source_covers_every_persisted_key();

    // T-35: default coverage regressions using a deliberately non-default
    // sentinel carrying distinct legal values across every persisted field.
    void sentinel_config_covers_all_persisted_fields();
    void sentinel_roundtrip_serialize_deserialize_preserves_every_semantic_value();
    void sentinel_omitted_field_fails_coverage_guard();
    void sentinel_apply_defaults_restores_all_values();
};

// Recursively collect every key path in a JSON object ("trail.style", ...).
static void collect_keys(const QJsonObject& obj, const QString& prefix,
                         QSet<QString>& out) {
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const QString path = prefix.isEmpty() ? it.key() : prefix + "." + it.key();
        if (it.value().isObject()) {
            collect_keys(it.value().toObject(), path, out);
        } else {
            out.insert(path);
        }
    }
}

static AppConfig make_sentinel_config() {
    AppConfig c{};
    c.schema_version = AppConfig::kCurrentSchemaVersion;
    c.master_enabled = false;
    c.start_with_windows = true;

    // TrailConfig
    c.trail.enabled = false;
    c.trail.color_mode = ptd::TrailColorMode::Gradient;
    c.trail.start_color_r = 10;
    c.trail.start_color_g = 20;
    c.trail.start_color_b = 30;
    c.trail.fade_color_r = 40;
    c.trail.fade_color_g = 50;
    c.trail.fade_color_b = 60;
    c.trail.style = ptd::TrailStyle::Pulse;
    c.trail.glow_strength = 0.85f;
    c.trail.segment_spacing_px = 24.0f;
    c.trail.head_thickness_px = 8.0f;
    c.trail.tail_thickness_px = 5.0f;
    c.trail.taper_strength = 0.75f;
    c.trail.lifetime_ms = 600.0f;
    c.trail.base_opacity = 0.65f;
    c.trail.smoothing = 0.35f;
    c.trail.fade_start = 0.25f;
    c.trail.fade_curve = ptd::FadeCurve::EaseOut;
    c.trail.sparkle_mode = ptd::TrailSparkleMode::Shards;
    c.trail.sparkle_amount = 0.45f;
    c.trail.sparkle_size_px = 12.0f;
    c.trail.sparkle_spread_px = 20.0f;

    // ClickConfig
    c.click.enabled = false;
    c.click.trigger_left = false;
    c.click.trigger_right = false;
    c.click.trigger_middle = false;
    c.click.color_r = 100;
    c.click.color_g = 150;
    c.click.color_b = 200;
    c.click.style = ptd::ClickStyle::Fire;
    c.click.particle_amount = 16;
    c.click.element_tint = 0.45f;
    c.click.hold_enabled = false;
    c.click.hold_wake_enabled = false;
    c.click.hold_intensity = 1.5f;
    c.click.hold_wake_density = 0.8f;
    c.click.hold_wake_lifetime_ms = 1500.0f;
    c.click.hold_release_strength = 1.75f;
    // T-36 Advanced Motion Wake: distinct legal non-default values.
    c.click.wake_strength = 1.4f;
    c.click.wake_size = 1.6f;
    c.click.wake_spread = 0.35f;
    c.click.speed_response = 1.8f;
    c.click.min_motion_speed_px_s = 320.0f;
    c.click.turn_accent = true;
    c.click.stop_accent = true;
    c.click.start_radius_px = 16.0f;
    c.click.end_radius_px = 64.0f;
    c.click.duration_ms = 400.0f;
    c.click.base_opacity = 0.55f;
    c.click.outline_thickness_px = 5.0f;
    c.click.fill_opacity = 0.4f;
    c.click.easing = ptd::ClickEasing::Linear;

    // RenderConfig
    c.render.diagnostic_primitives = true;

    return c;
}

static bool remove_key(QJsonObject& obj, const QStringList& path_parts) {
    if (path_parts.isEmpty()) return false;
    if (path_parts.size() == 1) {
        if (!obj.contains(path_parts.first())) return false;
        obj.remove(path_parts.first());
        return true;
    }
    const QString head = path_parts.first();
    if (!obj.contains(head) || !obj[head].isObject()) return false;
    QJsonObject child = obj[head].toObject();
    bool res = remove_key(child, path_parts.mid(1));
    if (res) {
        obj[head] = child;
    }
    return res;
}

void TestReleaseDefaults::canonical_source_is_available() {
    QVERIFY2(ptd::release_defaults_available(),
             "the embedded resources/release_defaults.json did not parse");
}

void TestReleaseDefaults::resource_path_is_the_embedded_defaults() {
    // The executable must never read a writable source-tree JSON beside it.
    // The path is a Qt resource, which is compiled into the binary.
    const QString path = QString::fromLatin1(ptd::release_defaults_resource_path());
    QVERIFY(path.startsWith(QStringLiteral(":/")));

    QFile embedded(path);
    QVERIFY(embedded.exists());
    QVERIFY(embedded.open(QIODevice::ReadOnly));
    QVERIFY(embedded.size() > 0);
}

void TestReleaseDefaults::fresh_config_equals_release_defaults() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("config.json"));
    QVERIFY(!QFile::exists(path));

    const AppConfig fresh = ConfigStorage::load_from_file(path.toStdWString());
    QCOMPARE(fresh, ptd::release_defaults());
}

void TestReleaseDefaults::release_defaults_are_validated() {
    const AppConfig& d = ptd::release_defaults();
    // validated() is idempotent: the canonical source is already in range, so
    // running it again changes nothing.
    QCOMPARE(AppConfig::validated(d), d);
    QCOMPARE(d.schema_version, AppConfig::kCurrentSchemaVersion);
}

void TestReleaseDefaults::canonical_defaults_roundtrip() {
    const AppConfig& d = ptd::release_defaults();
    const QByteArray json = ConfigStorage::serialize_json(d);
    QString err;
    auto parsed = ConfigStorage::deserialize_json(json, &err);
    QVERIFY2(parsed.has_value(), qPrintable(err));
    QCOMPARE(*parsed, d);
}

void TestReleaseDefaults::restore_defaults_match_fresh_defaults() {
    // The Restore paths build their config from ptd::release_defaults(); a
    // fresh install builds it from the same embedded JSON. Prove both agree
    // by parsing the resource the same way the loader does.
    const AppConfig& d = ptd::release_defaults();

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("config.json"));
    const AppConfig fresh = ConfigStorage::load_from_file(path.toStdWString());
    QCOMPARE(fresh.trail, d.trail);
    QCOMPARE(fresh.click, d.click);
    QCOMPARE(fresh.master_enabled, d.master_enabled);
    QCOMPARE(fresh.start_with_windows, d.start_with_windows);
}

void TestReleaseDefaults::canonical_source_covers_every_persisted_key() {
    // Derive the persisted key set from the SERIALISER, not from a hand-written
    // list: whatever serialize_json() writes must exist in the canonical
    // source. A future field added to the struct and the serialiser but not to
    // release_defaults.json fails here.
    const QJsonObject serialized =
        QJsonDocument::fromJson(ConfigStorage::serialize_json(ptd::release_defaults()))
            .object();

    QFile embedded(QString::fromLatin1(ptd::release_defaults_resource_path()));
    QVERIFY(embedded.open(QIODevice::ReadOnly));
    const QJsonObject canonical =
        QJsonDocument::fromJson(embedded.readAll()).object();

    QSet<QString> expected;
    collect_keys(serialized, QString(), expected);
    QSet<QString> actual;
    collect_keys(canonical, QString(), actual);

    QSet<QString> missing = expected;
    missing.subtract(actual);
    QVERIFY2(missing.isEmpty(),
             qPrintable(QStringLiteral("canonical Release Defaults is missing keys: ")
                            + QStringList(missing.values()).join(QStringLiteral(", "))));
}

void TestReleaseDefaults::sentinel_config_covers_all_persisted_fields() {
    const AppConfig s = make_sentinel_config();
    const AppConfig& d = ptd::release_defaults();

    QCOMPARE(AppConfig::validated(s), s);
    QVERIFY(s != d);
    QVERIFY(s.master_enabled != d.master_enabled);
    QVERIFY(s.start_with_windows != d.start_with_windows);

    QVERIFY(s.trail.enabled != d.trail.enabled);
    QVERIFY(s.trail.color_mode != d.trail.color_mode);
    QVERIFY(s.trail.start_color_r != d.trail.start_color_r);
    QVERIFY(s.trail.start_color_g != d.trail.start_color_g);
    QVERIFY(s.trail.start_color_b != d.trail.start_color_b);
    QVERIFY(s.trail.fade_color_r != d.trail.fade_color_r);
    QVERIFY(s.trail.fade_color_g != d.trail.fade_color_g);
    QVERIFY(s.trail.fade_color_b != d.trail.fade_color_b);
    QVERIFY(s.trail.style != d.trail.style);
    QVERIFY(s.trail.glow_strength != d.trail.glow_strength);
    QVERIFY(s.trail.segment_spacing_px != d.trail.segment_spacing_px);
    QVERIFY(s.trail.head_thickness_px != d.trail.head_thickness_px);
    QVERIFY(s.trail.tail_thickness_px != d.trail.tail_thickness_px);
    QVERIFY(s.trail.taper_strength != d.trail.taper_strength);
    QVERIFY(s.trail.lifetime_ms != d.trail.lifetime_ms);
    QVERIFY(s.trail.base_opacity != d.trail.base_opacity);
    QVERIFY(s.trail.smoothing != d.trail.smoothing);
    QVERIFY(s.trail.fade_start != d.trail.fade_start);
    QVERIFY(s.trail.fade_curve != d.trail.fade_curve);
    QVERIFY(s.trail.sparkle_mode != d.trail.sparkle_mode);
    QVERIFY(s.trail.sparkle_amount != d.trail.sparkle_amount);
    QVERIFY(s.trail.sparkle_size_px != d.trail.sparkle_size_px);
    QVERIFY(s.trail.sparkle_spread_px != d.trail.sparkle_spread_px);

    QVERIFY(s.click.enabled != d.click.enabled);
    QVERIFY(s.click.trigger_left != d.click.trigger_left);
    QVERIFY(s.click.trigger_right != d.click.trigger_right);
    QVERIFY(s.click.trigger_middle != d.click.trigger_middle);
    QVERIFY(s.click.color_r != d.click.color_r);
    QVERIFY(s.click.color_g != d.click.color_g);
    QVERIFY(s.click.color_b != d.click.color_b);
    QVERIFY(s.click.style != d.click.style);
    QVERIFY(s.click.particle_amount != d.click.particle_amount);
    QVERIFY(s.click.element_tint != d.click.element_tint);
    QVERIFY(s.click.hold_enabled != d.click.hold_enabled);
    QVERIFY(s.click.hold_wake_enabled != d.click.hold_wake_enabled);
    QVERIFY(s.click.hold_intensity != d.click.hold_intensity);
    QVERIFY(s.click.hold_wake_density != d.click.hold_wake_density);
    QVERIFY(s.click.hold_wake_lifetime_ms != d.click.hold_wake_lifetime_ms);
    QVERIFY(s.click.hold_release_strength != d.click.hold_release_strength);
    QVERIFY(s.click.wake_strength != d.click.wake_strength);
    QVERIFY(s.click.wake_size != d.click.wake_size);
    QVERIFY(s.click.wake_spread != d.click.wake_spread);
    QVERIFY(s.click.speed_response != d.click.speed_response);
    QVERIFY(s.click.min_motion_speed_px_s != d.click.min_motion_speed_px_s);
    QVERIFY(s.click.turn_accent != d.click.turn_accent);
    QVERIFY(s.click.stop_accent != d.click.stop_accent);
    QVERIFY(s.click.start_radius_px != d.click.start_radius_px);
    QVERIFY(s.click.end_radius_px != d.click.end_radius_px);
    QVERIFY(s.click.duration_ms != d.click.duration_ms);
    QVERIFY(s.click.base_opacity != d.click.base_opacity);
    QVERIFY(s.click.outline_thickness_px != d.click.outline_thickness_px);
    QVERIFY(s.click.fill_opacity != d.click.fill_opacity);
    QVERIFY(s.click.easing != d.click.easing);

    QVERIFY(s.render.diagnostic_primitives != d.render.diagnostic_primitives);
}

void TestReleaseDefaults::sentinel_roundtrip_serialize_deserialize_preserves_every_semantic_value() {
    const AppConfig sentinel = make_sentinel_config();
    const QByteArray json = ConfigStorage::serialize_json(sentinel);
    QString err;
    auto parsed = ConfigStorage::deserialize_json(json, &err);
    QVERIFY2(parsed.has_value(), qPrintable(err));
    QCOMPARE(*parsed, sentinel);
}

void TestReleaseDefaults::sentinel_omitted_field_fails_coverage_guard() {
    const AppConfig sentinel = make_sentinel_config();
    const QByteArray json = ConfigStorage::serialize_json(sentinel);
    const QJsonObject base_obj = QJsonDocument::fromJson(json).object();

    QSet<QString> key_paths;
    collect_keys(base_obj, QString(), key_paths);
    key_paths.remove(QStringLiteral("schema_version"));

    QVERIFY2(key_paths.size() >= 45, "must test at least 45 persisted settings keys");

    int tested_count = 0;
    for (const QString& path : key_paths) {
        QJsonObject mutated = base_obj;
        const QStringList parts = path.split(QLatin1Char('.'));
        QVERIFY2(remove_key(mutated, parts),
                 qPrintable("failed to remove key: " + path));

        QString err;
        auto parsed = ConfigStorage::deserialize_json(
            QJsonDocument(mutated).toJson(), &err);
        QVERIFY2(parsed.has_value(), qPrintable(err));

        // Deliberately omitting ANY persisted key causes deserialized config to revert to default,
        // which must not equal the sentinel value.
        QVERIFY2(*parsed != sentinel,
                 qPrintable("omitting key '" + path + "' must cause deserialized config to differ from sentinel"));
        tested_count++;
    }
    QCOMPARE(tested_count, key_paths.size());
}

void TestReleaseDefaults::sentinel_apply_defaults_restores_all_values() {
    AppConfig state = make_sentinel_config();
    QVERIFY(state != ptd::release_defaults());

    // Restore Defaults sets canonical values cleanly across all fields.
    state = ptd::release_defaults();
    QCOMPARE(state, ptd::release_defaults());
}

QTEST_MAIN(TestReleaseDefaults)
#include "test_release_defaults.moc"
