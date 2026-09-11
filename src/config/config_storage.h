#pragma once

#include "app_config.h"

#include <string>
#include <optional>
#include <QByteArray>
#include <QString>

namespace ptd {

class ConfigStorage {
public:
    // Resolves default persistence path: %LOCALAPPDATA%\ProTrail\config.json
    static std::wstring default_config_path();

    // Serializes AppConfig to formatted JSON byte array.
    static QByteArray serialize_json(const AppConfig& config);

    // Deserializes JSON byte array to AppConfig; validates all values.
    // Returns std::nullopt on syntax / schema failure.
    static std::optional<AppConfig> deserialize_json(const QByteArray& json, QString* error_msg = nullptr);

    // Loads config from file.
    // - If file does not exist: returns default AppConfig.
    // - If file is malformed: renames path to path + L".corrupt", logs warning, returns default AppConfig.
    // - If file is valid: returns validated AppConfig.
    static AppConfig load_from_file(const std::wstring& path = default_config_path());

    // Atomically saves config to file via temporary file + atomic replace.
    // Creates parent directory if it does not exist.
    // Returns true on success, false on error.
    static bool save_to_file(const AppConfig& config, const std::wstring& path = default_config_path());
};

} // namespace ptd
