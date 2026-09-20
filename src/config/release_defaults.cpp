#include "release_defaults.h"

#include "config_storage.h"
#include "../core/log.h"

#include <QByteArray>
#include <QFile>

#include <mutex>

namespace ptd {

namespace {

constexpr const char* kResourcePath = ":/defaults/release_defaults.json";

struct DefaultsCache {
    std::once_flag once;
    AppConfig config{};
    bool available = false;
};

DefaultsCache& cache() {
    static DefaultsCache instance;
    return instance;
}

void load_defaults(DefaultsCache& c) {
    QFile file(QString::fromLatin1(kResourcePath));
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        // Unreachable in a real build: AUTORCC embeds the file and the
        // configure-time gate refuses a missing or invalid source. The struct
        // fallback exists only so a corrupted runtime resource cannot make
        // the product unlaunchable, and the coverage regression proves it is
        // never a second authority.
        log_write(LogLevel::Error,
                  "release_defaults: embedded canonical source is unreadable; "
                  "using the emergency struct fallback");
        c.config = AppConfig::validated(AppConfig{});
        c.available = false;
        return;
    }

    const QByteArray bytes = file.readAll();
    file.close();

    QString err;
    auto parsed = ConfigStorage::deserialize_json(bytes, &err);
    if (!parsed) {
        log_write(LogLevel::Error,
                  "release_defaults: embedded canonical source is malformed ("
                      + err.toStdString()
                      + "); using the emergency struct fallback");
        c.config = AppConfig::validated(AppConfig{});
        c.available = false;
        return;
    }

    c.config = AppConfig::validated(*parsed);
    c.available = true;
}

DefaultsCache& resolved_cache() {
    DefaultsCache& c = cache();
    std::call_once(c.once, [&c] { load_defaults(c); });
    return c;
}

} // namespace

const AppConfig& release_defaults() {
    return resolved_cache().config;
}

bool release_defaults_available() {
    return resolved_cache().available;
}

const char* release_defaults_resource_path() {
    return kResourcePath;
}

} // namespace ptd
