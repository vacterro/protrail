#include "config_storage.h"
#include "../core/log.h"

#include <Windows.h>
#include <ShlObj.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace ptd {

std::wstring ConfigStorage::default_config_path() {
    wchar_t* raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) {
        std::filesystem::path dir = raw;
        CoTaskMemFree(raw);
        dir /= L"ProTrail";
        dir /= L"config.json";
        return dir.wstring();
    }
    return L"config.json";
}

QByteArray ConfigStorage::serialize_json(const AppConfig& config) {
    QJsonObject root;
    root["schema_version"] = config.schema_version;
    root["master_enabled"] = config.master_enabled;

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
            const int st = c["style"].toInt();
            if (st >= 0 && st <= 6) {
                cfg.click.style = static_cast<ClickStyle>(st);
            }
        }
        if (c.contains("particle_amount") && c["particle_amount"].isDouble())
            cfg.click.particle_amount = static_cast<uint8_t>(
                std::clamp(c["particle_amount"].toInt(), 0, 255));
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

AppConfig ConfigStorage::load_from_file(const std::wstring& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return AppConfig{};
    }

    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open()) {
        ptd::log_write(ptd::LogLevel::Warn, "config: failed to open file for reading, returning defaults");
        return AppConfig{};
    }

    std::string data((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
    in.close();

    QString err;
    auto opt = deserialize_json(QByteArray::fromRawData(data.data(), static_cast<qsizetype>(data.size())), &err);
    if (!opt) {
        ptd::log_write(ptd::LogLevel::Warn, "config: malformed JSON, backing up to .corrupt and returning defaults");
        const std::wstring corrupt_path = path + L".corrupt";
        std::filesystem::remove(corrupt_path, ec);
        std::filesystem::rename(path, corrupt_path, ec);
        return AppConfig{};
    }

    return *opt;
}

bool ConfigStorage::save_to_file(const AppConfig& config, const std::wstring& path) {
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
