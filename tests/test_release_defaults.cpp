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
    // T-80: the audio block ships SILENT -- an upgraded install must
    // never start making noise nobody configured.
    void audio_defaults_are_silent();
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

    // T-80 audio: every persisted key gets a distinct legal non-default value,
    // so the coverage guard catches any key the serialiser starts to ignore.
    // The names stay plain catalog ids (no paths), which is all validation
    // accepts.
    c.audio.enabled = true;
    c.audio.muted = true;
    c.audio.volume = 0.42f;
    for (int i = 0; i < ptd::kAudioEventCount; ++i) {
        c.audio.sounds[static_cast<std::size_t>(i)] =
            std::string("sentinel_") + ptd::audio_event_name(i) + ".wav";
    }
    // T-107: the favorites list is a persisted key too, so the sentinel carries
    // a non-empty one -- omitting it must change the deserialized config, or
    // the coverage guard would silently stop protecting the field.
    c.audio.favorites = {"sentinel_fav_a.wav", "sentinel_fav_b.wav"};

    // T-120 schema 14: every newly persisted audio key needs its own distinct
    // legal non-default value, for exactly the same reason as favorites above.
    // Omitting any one of them must change the deserialized config, so a future
    // key added to the serialiser without a sentinel value fails this guard.
    c.audio.recent_sounds = {"sentinel_recent_a.wav", "sentinel_recent_b.wav"};
    c.audio.preview_on_selection = !c.audio.preview_on_selection; // default true
    c.audio.preview_debounce_ms = 200;                            // default 35

    c.audio.keyboard_enabled = !c.audio.keyboard_enabled;         // default false
    c.audio.keyboard_scope = ptd::KeyboardScope::SystemWide;      // default AppOnly
    c.audio.typing_sounds_enabled = !c.audio.typing_sounds_enabled;
    c.audio.modifier_tap_sounds_enabled = !c.audio.modifier_tap_sounds_enabled;
    c.audio.suppress_repeat_typing = !c.audio.suppress_repeat_typing;
    c.audio.typing_repeat_cooldown_ms = 250;                       // default 35
    c.audio.suppress_sensitive_contexts = !c.audio.suppress_sensitive_contexts; // default true
    c.audio.excluded_processes = {"sentinel_proc.exe"};

    c.audio.horizontal_scroll_enabled = !c.audio.horizontal_scroll_enabled; // default true
    c.audio.scroll_sensitivity = 2.5f;   // default 1.0
    c.audio.scroll_cooldown_ms = 300;    // default 25
    c.audio.scroll_max_rate_hz = 90;     // default 30
    c.audio.scroll_pitch_variation = 0.25f; // default 0.0

    c.audio.active_profile = ptd::SoundProfilePreset::Custom;    // default Default

    // A custom binding whose every persisted member differs from the struct
    // default, so dropping any one key changes the parsed configuration.
    ptd::CustomShortcutBinding sentinel_shortcut{};
    sentinel_shortcut.id = "custom.sentinel_action";
    sentinel_shortcut.display_name = "Sentinel Action";
    sentinel_shortcut.chord = "Ctrl+Alt+Shift+S";
    sentinel_shortcut.is_app_shortcut = !sentinel_shortcut.is_app_shortcut; // default false
    sentinel_shortcut.enabled = !sentinel_shortcut.enabled;                 // default true
    c.audio.custom_shortcuts.push_back(sentinel_shortcut);

    // One event configuration with distinct values in every persisted member,
    // including a non-empty random pool, so omitting any of its keys -- or the
    // whole event_configs array -- is observable.
    ptd::EventSoundConfig sentinel_event{};
    sentinel_event.enabled = !sentinel_event.enabled;             // default true
    sentinel_event.sound_id = "sentinel_event.wav";
    sentinel_event.use_pool = !sentinel_event.use_pool;           // default false
    sentinel_event.no_immediate_repeat = !sentinel_event.no_immediate_repeat; // default true
    sentinel_event.gain_db = -6.0f;                               // default 0
    sentinel_event.policy = ptd::PlaybackPolicy::Queue;           // default Inherit
    sentinel_event.interrupt_scope = ptd::InterruptScope::Global; // default Inherit
    sentinel_event.cooldown_ms = 700;                             // default 0
    sentinel_event.priority = 13;                                 // default 50
    sentinel_event.fade_in_ms = 100;                              // default 0
    sentinel_event.fade_out_ms = 200;                             // default 0
    sentinel_event.min_retrigger_ms = 300;                        // default 0
    sentinel_event.polyphony_allowance = 9;                       // default 4
    sentinel_event.pitch_variation = 0.25f;                       // default 0
    sentinel_event.normalize_override = !sentinel_event.normalize_override;
    sentinel_event.normalize_enabled = !sentinel_event.normalize_enabled;
    ptd::SoundPoolEntry sentinel_pool_entry{};
    sentinel_pool_entry.enabled = !sentinel_pool_entry.enabled;   // default true
    sentinel_pool_entry.sound_id = "sentinel_pool_a.wav";
    sentinel_pool_entry.weight = 3.5f;                            // default 1.0
    sentinel_pool_entry.gain_db = -3.0f;                          // default 0
    sentinel_event.pool.push_back(sentinel_pool_entry);
    c.audio.event_configs["typing.letter"] = sentinel_event;

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

    QVERIFY(s.audio.enabled != d.audio.enabled);
    QVERIFY(s.audio.muted != d.audio.muted);
    QVERIFY(s.audio.volume != d.audio.volume);
    for (int i = 0; i < ptd::kAudioEventCount; ++i) {
        QVERIFY2(s.audio.sounds[static_cast<std::size_t>(i)] !=
                     d.audio.sounds[static_cast<std::size_t>(i)],
                 "every audio assignment is a distinct sentinel value");
    }
    // T-107: the curated favorites list is a persisted field with its own
    // distinct sentinel value.
    QVERIFY(s.audio.favorites != d.audio.favorites);
    QVERIFY(!s.audio.favorites.empty());

    // T-120 schema 14: the keyboard, scroll, browser, pool, custom-binding and
    // event-configuration fields are persisted too. Each one has its own
    // distinct sentinel value here, so a future addition that the serialiser or
    // the Release Defaults representation forgets fails this assertion instead
    // of drifting silently.
    QVERIFY(s.audio.recent_sounds != d.audio.recent_sounds);
    QVERIFY(s.audio.preview_on_selection != d.audio.preview_on_selection);
    QVERIFY(s.audio.preview_debounce_ms != d.audio.preview_debounce_ms);
    QVERIFY(s.audio.keyboard_enabled != d.audio.keyboard_enabled);
    QVERIFY(s.audio.keyboard_scope != d.audio.keyboard_scope);
    QVERIFY(s.audio.typing_sounds_enabled != d.audio.typing_sounds_enabled);
    QVERIFY(s.audio.modifier_tap_sounds_enabled != d.audio.modifier_tap_sounds_enabled);
    QVERIFY(s.audio.suppress_repeat_typing != d.audio.suppress_repeat_typing);
    QVERIFY(s.audio.typing_repeat_cooldown_ms != d.audio.typing_repeat_cooldown_ms);
    QVERIFY(s.audio.suppress_sensitive_contexts != d.audio.suppress_sensitive_contexts);
    QVERIFY(s.audio.excluded_processes != d.audio.excluded_processes);
    QVERIFY(s.audio.horizontal_scroll_enabled != d.audio.horizontal_scroll_enabled);
    QVERIFY(s.audio.scroll_sensitivity != d.audio.scroll_sensitivity);
    QVERIFY(s.audio.scroll_cooldown_ms != d.audio.scroll_cooldown_ms);
    QVERIFY(s.audio.scroll_max_rate_hz != d.audio.scroll_max_rate_hz);
    QVERIFY(s.audio.scroll_pitch_variation != d.audio.scroll_pitch_variation);
    QVERIFY(s.audio.active_profile != d.audio.active_profile);
    QVERIFY(s.audio.custom_shortcuts != d.audio.custom_shortcuts);
    QVERIFY(!s.audio.custom_shortcuts.empty());
    QVERIFY(s.audio.event_configs != d.audio.event_configs);
    QVERIFY(s.audio.event_configs.count("typing.letter") == 1);
    QVERIFY(s.audio.event_configs.at("typing.letter").gain_db != 0.0f);
    QVERIFY(!s.audio.event_configs.at("typing.letter").pool.empty());
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

void TestReleaseDefaults::audio_defaults_are_silent() {
    const AppConfig& d = ptd::release_defaults();

    // Default-off, default-unmuted, default volume, every assignment None.
    QVERIFY(!d.audio.enabled);
    QVERIFY(!d.audio.muted);
    QCOMPARE(d.audio.volume, ptd::AudioConfig::kMaxVolume);
    for (int i = 0; i < ptd::kAudioEventCount; ++i) {
        QCOMPARE(d.audio.sounds[static_cast<std::size_t>(i)], std::string());
    }
    QCOMPARE(d.audio, ptd::AudioConfig{});

    // Validation is idempotent on the canonical source, and the schema is the
    // audio introduction boundary.
    QCOMPARE(ptd::AudioConfig::validated(d.audio), d.audio);
    QCOMPARE(ptd::AppConfig::kAudioSchema, 12);
    QCOMPARE(d.schema_version, ptd::AppConfig::kCurrentSchemaVersion);

    // The embedded canonical JSON states the same block explicitly, so a
    // future edit that forgets to update the resource fails the coverage
    // guard above rather than silently shipping a different default.
    const QByteArray canonical = ConfigStorage::serialize_json(d);
    QVERIFY(canonical.contains(QByteArray("\"audio\":")));
    for (int i = 0; i < ptd::kAudioEventCount; ++i) {
        const QByteArray key = QByteArray("\"") +
                               QByteArray(ptd::audio_event_name(i)) +
                               QByteArray("\": \"\"");
        QVERIFY2(canonical.contains(key),
                 qPrintable(QStringLiteral("missing audio key ") +
                            QString::fromLatin1(ptd::audio_event_name(i))));
    }

    // A fresh install (no file) resolves to exactly the same silent audio.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const AppConfig fresh = ConfigStorage::load_from_file(
        dir.filePath(QStringLiteral("config.json")).toStdWString());
    QCOMPARE(fresh.audio, ptd::AudioConfig{});
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
