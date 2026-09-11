#include "app_config.h"

namespace ptd {

AppConfig AppConfig::validated(const AppConfig& in) {
    AppConfig out = in;
    if (out.schema_version <= 0) {
        out.schema_version = kCurrentSchemaVersion;
    }
    out.trail = TrailConfig::validated(out.trail);
    out.click = ClickConfig::validated(out.click);
    return out;
}

} // namespace ptd
