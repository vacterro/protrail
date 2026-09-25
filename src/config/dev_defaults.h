#pragma once

#include "app_config.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ptd {

// T-34: Developer Release Defaults authoring and preset system.
// Available only in developer builds (PROTRAIL_DEV_BUILD != 0).
// In production Release builds, ordinary users never see developer authoring
// controls and a release build cannot write the repository defaults file.

// Returns true if running in a dev build.
// In Release builds, returns false unless overridden by tests.
bool is_dev_build();

// Test-seams
void set_dev_build_override_for_tests(std::optional<bool> override_val);
void set_dev_presets_dir_for_tests(const std::filesystem::path& dir);
void reset_dev_presets_dir_for_tests();
void set_canonical_release_defaults_path_for_tests(const std::filesystem::path& path);
void reset_canonical_release_defaults_path_for_tests();

// W2-004 deterministic promotion fault-injection seam. Each value forces
// exactly one transaction stage of promote_to_release_defaults() to fail so
// its rollback semantics can be exercised without depending on permissions,
// timing, or external Windows state. None (default) leaves production
// behaviour unchanged. The fault is one-shot: promote_to_release_defaults()
// clears it on entry after reading it, so a single armed fault affects only
// the next promotion.
enum class PromoteFault {
    None,
    TempFlush,          // temporary-file flush fails before canonical replace
    PostReplaceRead,    // canonical file cannot be re-opened after replace
    SemanticVerify,     // post-replace roundtrip semantic verification fails
    RollbackWriteFlush, // rollback's own write/flush fails
    RollbackMove,       // rollback's MoveFileEx replacement fails
};
void set_promote_fault_for_tests(PromoteFault fault);
void reset_promote_fault_for_tests();

// Presets management (under dev/presets)
std::filesystem::path dev_presets_dir();
std::filesystem::path canonical_release_defaults_path();

std::vector<std::string> list_dev_presets();
bool save_dev_preset(const std::string& name, const AppConfig& config, std::string* out_err = nullptr);
std::optional<AppConfig> load_dev_preset(const std::string& name, std::string* out_err = nullptr);

// Textual diff engine
std::vector<std::string> diff_configs_lines(const AppConfig& current, const AppConfig& reference);
std::string diff_configs(const AppConfig& current, const AppConfig& reference);

// Atomic promotion
struct PromoteResult {
    bool success = false;
    std::string message;
};

PromoteResult promote_to_release_defaults(const AppConfig& config);

} // namespace ptd
