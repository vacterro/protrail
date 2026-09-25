#pragma once

#include "app_config.h"

#include <string>
#include <optional>
#include <functional>
#include <tuple>
#include <system_error>
#include <QByteArray>
#include <QString>

namespace ptd {

// CORE-001: the provenance of a configuration load.
//
// `load_from_file` used to collapse every source state into one bare
// AppConfig, so the caller could not tell a fresh install from a protected
// future-schema or unreadable source. Application then treated ALL of them as
// ordinary writable state, and its unconditional shutdown save could destroy
// the only copy of user data.
enum class ConfigLoadStatus {
    // No file existed: a fresh install. Safe to write.
    MissingFresh,
    // A current-schema file loaded unchanged. Safe to write.
    LoadedCurrent,
    // An older supported schema loaded and will be upgraded on save. Safe.
    LoadedMigrated,
    // Malformed file whose bytes were secured as `<path>.corrupt`. Safe.
    MalformedBackedUp,
    // Malformed file whose backup FAILED: the original bytes are the only
    // copy and must never be overwritten. Persistence disabled.
    MalformedBackupFailed,
    // A valid file written by a NEWER schema. Valid-but-unsupported, never
    // corrupt: it is not renamed, not rewritten, and unknown fields are not
    // discarded. Persistence disabled.
    UnsupportedFutureSchema,
    // The file could not be opened/read. Persistence disabled.
    ReadFailure,
};

struct ConfigLoadResult {
    AppConfig config;
    ConfigLoadStatus status = ConfigLoadStatus::MissingFresh;
    // True only for MissingFresh / LoadedCurrent / LoadedMigrated /
    // MalformedBackedUp. Every save path must respect this.
    bool persistence_allowed = true;
};

class ConfigStorage {
public:
    // Resolves default persistence path: %LOCALAPPDATA%\ProTrail\config.json
    static std::wstring default_config_path();

    // Serializes AppConfig to formatted JSON byte array.
    static QByteArray serialize_json(const AppConfig& config);

    // Deserializes JSON byte array to AppConfig; validates all values.
    // Returns std::nullopt on syntax / schema failure.
    static std::optional<AppConfig> deserialize_json(const QByteArray& json, QString* error_msg = nullptr);

    // CORE-001: status-bearing load. Distinguishes fresh, current, migrated,
    // malformed-backed-up, malformed-backup-failed, unsupported-future-schema
    // and read-failure, and reports whether writing is currently allowed.
    // A schema_version greater than kCurrentSchemaVersion is treated as
    // valid-but-unsupported: it is not renamed and not rewritten.
    static ConfigLoadResult load_from_file_result(const std::wstring& path = default_config_path());

    // Loads config from file. Compatibility wrapper returning the config from
    // load_from_file_result(); callers that must honour write protection use
    // load_from_file_result() instead.
    static AppConfig load_from_file(const std::wstring& path = default_config_path());

    // Atomically saves config to file via temporary file + atomic replace.
    // Creates parent directory if it does not exist.
    // Returns true on success, false on error.
    static bool save_to_file(const AppConfig& config, const std::wstring& path = default_config_path());

    // CORE-001 probe seam: force the filesystem existence check to return a
    // non-zero error_code for this path, exercising the protected probe-error
    // branch deterministically. Empty means no injection.
    using FilesystemProbeFn = std::function<std::tuple<bool, std::error_code>(const std::wstring&)>;
    static void set_filesystem_probe_for_tests(FilesystemProbeFn fn);
    static void clear_filesystem_probe_for_tests();

    // CORE-001 test seam: force an open/read failure for exactly this path so
    // the ReadFailure provenance can be exercised deterministically. Never
    // consulted by production code paths.
    static void set_read_failure_path_for_tests(const std::wstring& path);
    static void clear_read_failure_path_for_tests();

    // W2-002 test seam: force save_to_file() to fail deterministically for
    // exactly the given path (an empty path arms every save), so an
    // Application-level regression can prove the persistence-first autostart
    // transaction never mutates the Run key from an uncommitted preference.
    // Production behaviour is unchanged when the seam is disabled.
    static void set_save_failure_path_for_tests(const std::wstring& path);
    static void clear_save_failure_path_for_tests();

    // W2-005 test seam: count durable load attempts so a regression can prove
    // Application startup reads configuration exactly once.
    static void reset_load_count_for_tests();
    static int load_count_for_tests();
};

} // namespace ptd
