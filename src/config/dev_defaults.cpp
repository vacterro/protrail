#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "dev_defaults.h"
#include "config_storage.h"
#include "release_defaults.h"
#include "../core/log.h"

#include <QByteArray>
#include <QFile>
#include <QString>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace ptd {

namespace {

static std::optional<bool> g_dev_build_override;
static std::optional<std::filesystem::path> g_presets_dir_override;
static std::optional<std::filesystem::path> g_canonical_path_override;
// W2-004 one-shot promotion fault injection. Read-and-cleared on entry to
// promote_to_release_defaults(); production leaves it None.
static PromoteFault g_promote_fault = PromoteFault::None;

std::string sanitize_preset_name(const std::string& input) {
    if (input.empty()) {
        const auto now = std::chrono::system_clock::now();
        const std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_s(&tm, &t);
        std::ostringstream ss;
        ss << "preset_" << std::put_time(&tm, "%Y%m%d_%H%M%S");
        return ss.str();
    }
    std::string clean;
    for (char c : input) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-') {
            clean += c;
        } else if (c == ' ' || c == '.') {
            clean += '_';
        }
    }
    if (clean.empty()) {
        clean = "dev_preset";
    }
    return clean;
}

template <typename T>
void check_field_diff(std::vector<std::string>& out, const char* name, const T& cur, const T& ref) {
    if constexpr (std::is_floating_point_v<T>) {
        if (std::abs(cur - ref) > 0.0001) {
            std::ostringstream ss;
            ss << name << ": " << ref << " -> " << cur;
            out.push_back(ss.str());
        }
    } else if constexpr (std::is_same_v<T, bool>) {
        if (cur != ref) {
            std::ostringstream ss;
            ss << name << ": " << (ref ? "true" : "false") << " -> " << (cur ? "true" : "false");
            out.push_back(ss.str());
        }
    } else {
        if (cur != ref) {
            std::ostringstream ss;
            ss << name << ": " << static_cast<long long>(ref) << " -> " << static_cast<long long>(cur);
            out.push_back(ss.str());
        }
    }
}

void check_color_diff(std::vector<std::string>& out, const char* name,
                      uint8_t cr, uint8_t cg, uint8_t cb,
                      uint8_t rr, uint8_t rg, uint8_t rb) {
    if (cr != rr || cg != rg || cb != rb) {
        std::ostringstream ss;
        ss << name << ": RGB(" << static_cast<int>(rr) << "," << static_cast<int>(rg) << "," << static_cast<int>(rb)
           << ") -> RGB(" << static_cast<int>(cr) << "," << static_cast<int>(cg) << "," << static_cast<int>(cb) << ")";
        out.push_back(ss.str());
    }
}

} // namespace

bool is_dev_build() {
    if (g_dev_build_override.has_value()) {
        return *g_dev_build_override;
    }
#if defined(PROTRAIL_DEV_BUILD) && (PROTRAIL_DEV_BUILD != 0)
    return true;
#else
    return false;
#endif
}

void set_dev_build_override_for_tests(std::optional<bool> override_val) {
    g_dev_build_override = override_val;
}

void set_dev_presets_dir_for_tests(const std::filesystem::path& dir) {
    g_presets_dir_override = dir;
}

void reset_dev_presets_dir_for_tests() {
    g_presets_dir_override.reset();
}

void set_canonical_release_defaults_path_for_tests(const std::filesystem::path& path) {
    g_canonical_path_override = path;
}

void reset_canonical_release_defaults_path_for_tests() {
    g_canonical_path_override.reset();
}

void set_promote_fault_for_tests(PromoteFault fault) {
    g_promote_fault = fault;
}

void reset_promote_fault_for_tests() {
    g_promote_fault = PromoteFault::None;
}

std::filesystem::path dev_presets_dir() {
    if (g_presets_dir_override.has_value()) {
        return *g_presets_dir_override;
    }
#if defined(PROTRAIL_SOURCE_DIR)
    return std::filesystem::path(QString::fromUtf8(PROTRAIL_SOURCE_DIR).toStdWString()) / "dev" / "presets";
#else
    return std::filesystem::current_path() / "dev" / "presets";
#endif
}

std::filesystem::path canonical_release_defaults_path() {
    if (g_canonical_path_override.has_value()) {
        return *g_canonical_path_override;
    }
#if defined(PROTRAIL_SOURCE_DIR)
    return std::filesystem::path(QString::fromUtf8(PROTRAIL_SOURCE_DIR).toStdWString()) / "resources" / "release_defaults.json";
#else
    return std::filesystem::current_path() / "resources" / "release_defaults.json";
#endif
}

std::vector<std::string> list_dev_presets() {
    std::vector<std::string> presets;
    std::filesystem::path dir = dev_presets_dir();
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
        return presets;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (entry.is_regular_file(ec) && entry.path().extension() == ".json") {
            presets.push_back(entry.path().stem().string());
        }
    }
    std::sort(presets.begin(), presets.end());
    return presets;
}

bool save_dev_preset(const std::string& name, const AppConfig& config, std::string* out_err) {
    if (!is_dev_build()) {
        if (out_err) *out_err = "Developer preset saving is disabled in release builds";
        return false;
    }
    std::string base_name = sanitize_preset_name(name);
    std::filesystem::path dir = dev_presets_dir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        if (out_err) *out_err = "Failed to create presets directory: " + ec.message();
        return false;
    }
    std::filesystem::path target_file = dir / (base_name + ".json");
    std::filesystem::path temp_file = dir / (base_name + ".json.tmp." + std::to_string(GetCurrentProcessId()));

    AppConfig validated = AppConfig::validated(config);
    QByteArray bytes = ConfigStorage::serialize_json(validated);

    QFile file(QString::fromStdWString(temp_file.wstring()));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (out_err) *out_err = "Failed to open temporary file for write: " + file.errorString().toStdString();
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        file.close();
        std::filesystem::remove(temp_file, ec);
        if (out_err) *out_err = "Failed to write complete preset data";
        return false;
    }
    file.flush();
    file.close();

    if (!MoveFileExW(temp_file.c_str(), target_file.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temp_file, ec);
        if (out_err) *out_err = "Failed to atomically rename preset file";
        return false;
    }
    return true;
}

std::optional<AppConfig> load_dev_preset(const std::string& name, std::string* out_err) {
    std::string clean = name;
    if (clean.size() >= 5 && clean.substr(clean.size() - 5) == ".json") {
        clean = clean.substr(0, clean.size() - 5);
    }
    std::filesystem::path path = dev_presets_dir() / (clean + ".json");
    QFile file(QString::fromStdWString(path.wstring()));
    if (!file.open(QIODevice::ReadOnly)) {
        if (out_err) *out_err = "Preset file not found or unreadable: " + path.string();
        return std::nullopt;
    }
    QByteArray bytes = file.readAll();
    file.close();

    QString parse_err;
    auto parsed = ConfigStorage::deserialize_json(bytes, &parse_err);
    if (!parsed) {
        if (out_err) *out_err = parse_err.toStdString();
        return std::nullopt;
    }
    return AppConfig::validated(*parsed);
}

std::vector<std::string> diff_configs_lines(const AppConfig& cur, const AppConfig& ref) {
    std::vector<std::string> diffs;

    check_field_diff(diffs, "schema_version", cur.schema_version, ref.schema_version);
    check_field_diff(diffs, "master_enabled", cur.master_enabled, ref.master_enabled);
    check_field_diff(diffs, "start_with_windows", cur.start_with_windows, ref.start_with_windows);

    // Trail
    check_field_diff(diffs, "trail.enabled", cur.trail.enabled, ref.trail.enabled);
    check_field_diff(diffs, "trail.color_mode", static_cast<int>(cur.trail.color_mode), static_cast<int>(ref.trail.color_mode));
    check_color_diff(diffs, "trail.start_color",
                     cur.trail.start_color_r, cur.trail.start_color_g, cur.trail.start_color_b,
                     ref.trail.start_color_r, ref.trail.start_color_g, ref.trail.start_color_b);
    check_color_diff(diffs, "trail.fade_color",
                     cur.trail.fade_color_r, cur.trail.fade_color_g, cur.trail.fade_color_b,
                     ref.trail.fade_color_r, ref.trail.fade_color_g, ref.trail.fade_color_b);
    check_field_diff(diffs, "trail.style", static_cast<int>(cur.trail.style), static_cast<int>(ref.trail.style));
    check_field_diff(diffs, "trail.glow_strength", cur.trail.glow_strength, ref.trail.glow_strength);
    check_field_diff(diffs, "trail.segment_spacing_px", cur.trail.segment_spacing_px, ref.trail.segment_spacing_px);
    check_field_diff(diffs, "trail.head_thickness_px", cur.trail.head_thickness_px, ref.trail.head_thickness_px);
    check_field_diff(diffs, "trail.tail_thickness_px", cur.trail.tail_thickness_px, ref.trail.tail_thickness_px);
    check_field_diff(diffs, "trail.taper_strength", cur.trail.taper_strength, ref.trail.taper_strength);
    check_field_diff(diffs, "trail.lifetime_ms", cur.trail.lifetime_ms, ref.trail.lifetime_ms);
    check_field_diff(diffs, "trail.base_opacity", cur.trail.base_opacity, ref.trail.base_opacity);
    check_field_diff(diffs, "trail.smoothing", cur.trail.smoothing, ref.trail.smoothing);
    check_field_diff(diffs, "trail.fade_start", cur.trail.fade_start, ref.trail.fade_start);
    check_field_diff(diffs, "trail.fade_curve", static_cast<int>(cur.trail.fade_curve), static_cast<int>(ref.trail.fade_curve));
    check_field_diff(diffs, "trail.sparkle_mode", static_cast<int>(cur.trail.sparkle_mode), static_cast<int>(ref.trail.sparkle_mode));
    check_field_diff(diffs, "trail.sparkle_amount", cur.trail.sparkle_amount, ref.trail.sparkle_amount);
    check_field_diff(diffs, "trail.sparkle_size_px", cur.trail.sparkle_size_px, ref.trail.sparkle_size_px);
    check_field_diff(diffs, "trail.sparkle_spread_px", cur.trail.sparkle_spread_px, ref.trail.sparkle_spread_px);

    // Click
    check_field_diff(diffs, "click.enabled", cur.click.enabled, ref.click.enabled);
    check_field_diff(diffs, "click.trigger_left", cur.click.trigger_left, ref.click.trigger_left);
    check_field_diff(diffs, "click.trigger_right", cur.click.trigger_right, ref.click.trigger_right);
    check_field_diff(diffs, "click.trigger_middle", cur.click.trigger_middle, ref.click.trigger_middle);
    check_color_diff(diffs, "click.color",
                     cur.click.color_r, cur.click.color_g, cur.click.color_b,
                     ref.click.color_r, ref.click.color_g, ref.click.color_b);
    check_field_diff(diffs, "click.style", static_cast<int>(cur.click.style), static_cast<int>(ref.click.style));
    check_field_diff(diffs, "click.particle_amount", cur.click.particle_amount, ref.click.particle_amount);
    check_field_diff(diffs, "click.element_tint", cur.click.element_tint, ref.click.element_tint);
    check_field_diff(diffs, "click.hold_enabled", cur.click.hold_enabled, ref.click.hold_enabled);
    check_field_diff(diffs, "click.hold_wake_enabled", cur.click.hold_wake_enabled, ref.click.hold_wake_enabled);
    check_field_diff(diffs, "click.hold_intensity", cur.click.hold_intensity, ref.click.hold_intensity);
    check_field_diff(diffs, "click.hold_wake_density", cur.click.hold_wake_density, ref.click.hold_wake_density);
    check_field_diff(diffs, "click.hold_wake_lifetime_ms", cur.click.hold_wake_lifetime_ms, ref.click.hold_wake_lifetime_ms);
    check_field_diff(diffs, "click.hold_release_strength", cur.click.hold_release_strength, ref.click.hold_release_strength);
    // T-36 Advanced Motion Wake.
    check_field_diff(diffs, "click.wake_strength", cur.click.wake_strength, ref.click.wake_strength);
    check_field_diff(diffs, "click.wake_size", cur.click.wake_size, ref.click.wake_size);
    check_field_diff(diffs, "click.wake_spread", cur.click.wake_spread, ref.click.wake_spread);
    check_field_diff(diffs, "click.speed_response", cur.click.speed_response, ref.click.speed_response);
    check_field_diff(diffs, "click.min_motion_speed_px_s", cur.click.min_motion_speed_px_s, ref.click.min_motion_speed_px_s);
    check_field_diff(diffs, "click.turn_accent", cur.click.turn_accent, ref.click.turn_accent);
    check_field_diff(diffs, "click.stop_accent", cur.click.stop_accent, ref.click.stop_accent);
    check_field_diff(diffs, "click.start_radius_px", cur.click.start_radius_px, ref.click.start_radius_px);
    check_field_diff(diffs, "click.end_radius_px", cur.click.end_radius_px, ref.click.end_radius_px);
    check_field_diff(diffs, "click.duration_ms", cur.click.duration_ms, ref.click.duration_ms);
    check_field_diff(diffs, "click.base_opacity", cur.click.base_opacity, ref.click.base_opacity);
    check_field_diff(diffs, "click.outline_thickness_px", cur.click.outline_thickness_px, ref.click.outline_thickness_px);
    check_field_diff(diffs, "click.fill_opacity", cur.click.fill_opacity, ref.click.fill_opacity);
    check_field_diff(diffs, "click.easing", static_cast<int>(cur.click.easing), static_cast<int>(ref.click.easing));

    // Render
    check_field_diff(diffs, "render.diagnostic_primitives", cur.render.diagnostic_primitives, ref.render.diagnostic_primitives);

    return diffs;
}

std::string diff_configs(const AppConfig& cur, const AppConfig& ref) {
    auto lines = diff_configs_lines(cur, ref);
    if (lines.empty()) {
        return "No differences from Release Defaults.";
    }
    std::ostringstream ss;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) ss << "\n";
        ss << lines[i];
    }
    return ss.str();
}

PromoteResult promote_to_release_defaults(const AppConfig& config) {
    // W2-004: read-and-clear the one-shot fault so it affects only this call.
    const PromoteFault fault = g_promote_fault;
    g_promote_fault = PromoteFault::None;

    if (!is_dev_build()) {
        log_write(LogLevel::Error, "promote_to_release_defaults: refused in production release build");
        return {false, "Release build cannot write the repository defaults file."};
    }

    // The controller passes the complete canonical snapshot. Do not rebuild
    // or sanitize it from UI fields: every persisted field, including the
    // render section, belongs in the source defaults representation.
    AppConfig sanitized = AppConfig::validated(config);
    sanitized.schema_version = AppConfig::kCurrentSchemaVersion;

    std::filesystem::path target = canonical_release_defaults_path();
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) {
        return {false, "Failed to create directory for release defaults: " + ec.message()};
    }

    std::filesystem::path temp_path = target;
    temp_path += L".tmp." + std::to_wstring(GetCurrentProcessId());

    QByteArray bytes = ConfigStorage::serialize_json(sanitized);

    // Validate the exact bytes before touching the existing canonical file.
    // This also makes the post-write proof deterministic and keeps malformed
    // serialization failures fail-closed.
    QString preflight_err;
    auto preflight = ConfigStorage::deserialize_json(bytes, &preflight_err);
    if (!preflight || *preflight != sanitized) {
        return {false, "Serialized defaults failed preflight semantic verification: "
                      + (preflight ? std::string("semantic mismatch")
                                   : preflight_err.toStdString())};
    }

    QByteArray previous_bytes;
    bool had_previous = false;
    QFile previous_file(QString::fromStdWString(target.wstring()));
    if (previous_file.exists()) {
        if (!previous_file.open(QIODevice::ReadOnly)) {
            return {false, "Failed to read the existing release defaults before promotion: "
                          + previous_file.errorString().toStdString()};
        }
        previous_bytes = previous_file.readAll();
        previous_file.close();
        had_previous = true;
    }

    QFile file(QString::fromStdWString(temp_path.wstring()));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return {false, "Failed to create temporary file for atomic promotion: " + file.errorString().toStdString()};
    }
    if (file.write(bytes) != bytes.size()) {
        file.close();
        std::filesystem::remove(temp_path, ec);
        return {false, "Failed to write complete serialized defaults"};
    }
    if (!file.flush()) {
        file.close();
        std::filesystem::remove(temp_path, ec);
        return {false, "Failed to flush temporary release defaults file"};
    }
    // W2-004 seam: model a temporary-file flush failure BEFORE canonical
    // replacement. The canonical target is never touched, so existing bytes
    // (or absence) must remain byte-identical.
    if (fault == PromoteFault::TempFlush) {
        file.close();
        std::filesystem::remove(temp_path, ec);
        return {false, "Failed to flush temporary release defaults file"};
    }
    file.close();

    // Atomic replacement
    if (!MoveFileExW(temp_path.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temp_path, ec);
        return {false, "Failed to atomically replace release defaults file"};
    }

    // Re-read and verify roundtrip equality. If this unexpectedly fails,
    // restore the prior bytes atomically so the canonical source is never
    // left in a partially trusted state. Restoration itself is verified; if
    // it fails the canonical file must not be misreported as restored.
    auto restore_previous = [&]() -> bool {
        if (!had_previous) {
            // No previous file: remove the newly-written unverified target so
            // absence remains the canonical prior state.
            std::filesystem::remove(target, ec);
            if (ec) {
                return false;
            }
            return true;
        }
        std::filesystem::path restore = target;
        restore += L".restore." + std::to_wstring(GetCurrentProcessId());
        QFile rf(QString::fromStdWString(restore.wstring()));
        if (!rf.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        if (rf.write(previous_bytes) != previous_bytes.size()) { rf.close(); return false; }
        // W2-004 seam: model rollback's own write/flush failure.
        if (fault == PromoteFault::RollbackWriteFlush) {
            rf.close();
            std::filesystem::remove(restore, ec);
            return false;
        }
        if (!rf.flush()) { rf.close(); std::filesystem::remove(restore, ec); return false; }
        rf.close();
        // W2-004 seam: model rollback's replacement (MoveFileEx) failure.
        if (fault == PromoteFault::RollbackMove) {
            std::filesystem::remove(restore, ec);
            return false;
        }
        const bool ok = MoveFileExW(restore.c_str(), target.c_str(),
                                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (!ok) {
            std::filesystem::remove(restore, ec);
            return false;
        }
        // Verify the restored bytes match what we expected.
        QFile vf(QString::fromStdWString(target.wstring()));
        if (!vf.open(QIODevice::ReadOnly)) return false;
        const QByteArray back = vf.readAll();
        vf.close();
        return back == previous_bytes;
    };

    QFile verify_file(QString::fromStdWString(target.wstring()));
    // W2-004 seam: model the canonical file being unreadable after replace.
    const bool post_replace_read_ok =
        (fault != PromoteFault::PostReplaceRead) &&
        verify_file.open(QIODevice::ReadOnly);
    if (!post_replace_read_ok) {
        if (had_previous) {
            const bool restored = restore_previous();
            if (!restored) {
                return {false, "Failed to re-read release defaults file after write; RESTORATION FAILED -- canonical source may be untrusted; manual intervention required: " + target.string()};
            }
        } else {
            const bool removed = restore_previous();
            if (!removed) {
                return {false, "Failed to re-read newly created release defaults file; removal of unverified target FAILED -- canonical source may be untrusted: " + target.string()};
            }
        }
        return {false, "Failed to re-read release defaults file after write; previous defaults restored."};
    }
    QByteArray read_bytes = verify_file.readAll();
    verify_file.close();

    QString err;
    auto reloaded = ConfigStorage::deserialize_json(read_bytes, &err);
    // W2-004 seam: model a post-replace semantic verification failure. The
    // rollback-fault cases also force the verify failure so the rollback path
    // is actually entered.
    const bool force_verify_fail =
        fault == PromoteFault::SemanticVerify ||
        fault == PromoteFault::RollbackWriteFlush ||
        fault == PromoteFault::RollbackMove;
    const bool semantic_ok =
        !force_verify_fail && reloaded && (*reloaded == sanitized);
    if (!semantic_ok) {
        const bool restored = restore_previous();
        if (!restored) {
            return {false, "Roundtrip equality verification failed; RESTORATION FAILED -- canonical source may be untrusted; manual intervention required: " + target.string()};
        }
        return {false, "Roundtrip equality verification failed; previous defaults restored."};
    }

    log_write(LogLevel::Info, "promote_to_release_defaults: successfully promoted to canonical release defaults");
    return {true, "Successfully set current canonical settings as Release Defaults and verified every persisted field. The new source defaults apply after the next rebuild/restart."};
}

} // namespace ptd
