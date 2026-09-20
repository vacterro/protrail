#pragma once

// T-032 Silent Windows autostart.
//
// ProTrail starts with Windows through the PER-USER Run key
// (HKCU\Software\Microsoft\Windows\CurrentVersion\Run). That choice carries
// three product obligations this component owns, and nothing else:
//
//   1. NO ELEVATION. HKCU is writable by the logged-on user, so the feature
//      never needs administrator rights and never asks for them.
//   2. ONLY OUR OWN VALUE IS EVER TOUCHED. ProTrail registers exactly one
//      value name and may create, update or delete exactly that name. It
//      never enumerates-and-rewrites the key, never edits another product's
//      entry, and never deletes the key itself.
//   3. THE REGISTERED COMMAND MUST BE RE-DERIVABLE. ProTrail ships portable,
//      so the executable can move; the command is therefore rebuilt from the
//      REAL current executable path and reconciled whenever the setting is
//      on, instead of being written once and left to rot.
//
// The registry itself is behind AutostartBackend so unit tests can drive the
// whole feature through an in-memory double. A routine CTest run must never
// modify the developer's actual Windows startup entries -- the autostart test
// suite additionally proves the real Run key is byte-identical before and
// after (see tests/test_autostart.cpp).

#include <string>
#include <utility>
#include <vector>

namespace ptd {

// The ONE startup argument that selects the silent autostart launch path.
// It lives here because the registry command builder is what mints it; the
// command-line parser (src/app/startup_mode.h) reads the same constant, so
// the written command and the parsed argument can never drift apart.
inline constexpr const wchar_t* kStartupMinimizedArgument = L"--startup-minimized";

// The complete registry surface the autostart feature needs: read one value,
// write one value, remove one value. There is deliberately no enumeration
// requirement for the WRITE path -- the manager only ever names its own
// value -- but `names()` exists so tests and diagnostics can prove that
// unrelated entries survive untouched.
class AutostartBackend {
public:
    virtual ~AutostartBackend() = default;

    // Returns true and fills `out` when `name` exists under the startup key.
    virtual bool read(const std::wstring& name, std::wstring& out) = 0;
    // Creates or replaces `name`. Returns false on failure.
    virtual bool write(const std::wstring& name, const std::wstring& command) = 0;
    // Removes `name`. Returns true when the value is gone afterwards
    // (including the already-absent case: removing what is not there is a
    // success, which is what makes disable idempotent).
    virtual bool remove(const std::wstring& name) = 0;
    // Snapshot of the value names currently present (diagnostics/tests).
    virtual std::vector<std::wstring> names() = 0;

    // Exact Win32 error of the last failed operation (0 when none).
    virtual unsigned long last_error() const { return 0; }
};

// Production backend: the per-user Run key. Never requests elevation.
class Win32RunKeyBackend : public AutostartBackend {
public:
    static constexpr const wchar_t* kRunKeyPath =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

    bool read(const std::wstring& name, std::wstring& out) override;
    bool write(const std::wstring& name, const std::wstring& command) override;
    bool remove(const std::wstring& name) override;
    std::vector<std::wstring> names() override;

    // Exact Win32 error of the last failed operation (0 when none).
    unsigned long last_error() const override { return last_error_; }

private:
    unsigned long last_error_ = 0;
};

// In-memory backend for tests and the reference semantics of the manager:
// the map is ordered, unrelated values are never rewritten, and failures can
// be injected to prove the manager reports them instead of assuming success.
class InMemoryAutostartBackend : public AutostartBackend {
public:
    bool read(const std::wstring& name, std::wstring& out) override;
    bool write(const std::wstring& name, const std::wstring& command) override;
    bool remove(const std::wstring& name) override;
    std::vector<std::wstring> names() override;
    unsigned long last_error() const override { return last_error_; }

    // Ordered value store (name -> command).
    std::vector<std::pair<std::wstring, std::wstring>> values;

    // Failure injection: when true every write/read/remove reports failure.
    bool fail_writes = false;
    bool fail_reads = false;
    bool fail_removes = false;

    // Counters proving idempotency (a repeated enable must not rewrite).
    int write_calls = 0;
    int remove_calls = 0;

    unsigned long last_error_ = 0;
};

// Quotes one command-line argument for the Windows command-line grammar:
// the argument is wrapped in double quotes and any embedded quote/backslash
// run is escaped, so a path containing spaces, quotes or a trailing
// backslash survives the round trip through CreateProcess.
std::wstring quote_argument(const std::wstring& argument);

// Unquotes/decodes a command produced by quote_argument back into its
// argument list. Used by tests to prove the quoting is reversible rather
// than merely pretty.
std::vector<std::wstring> split_command_line(const std::wstring& command_line);

// Full path of the running executable, or empty when unresolvable.
std::wstring current_executable_path();

// Owns the "Start with Windows" side effect. All methods are total: they
// return a boolean instead of throwing, and a backend failure is reported
// (never silently treated as success).
class AutostartManager {
public:
    // The ONE ProTrail-owned value name. Stable across releases: renaming it
    // would strand an enabled user's entry in the Run key forever.
    static constexpr const wchar_t* kValueName = L"ProTrail";

    // Builds the registered command: fully quoted executable path plus the
    // explicit startup argument.
    static std::wstring build_command(const std::wstring& exe_path);

    explicit AutostartManager(AutostartBackend& backend);

    // Exact Win32 error of the last failed operation (0 when none).
    unsigned long last_error() const { return backend_.last_error(); }

    // True when ProTrail's own value exists. Another product's entry with a
    // similar name is not "enabled". If a backend failure occurs, returns
    // false and records the failure in last_error().
    bool is_enabled();

    // Query variant distinguishing absent from failed. Returns true on
    // successful check (writing out_enabled), or false on backend failure.
    bool is_enabled(bool& out_enabled);

    // The currently registered command, or an empty string when absent.
    std::wstring registered_command();

    // Idempotent: an already-correct registration is not rewritten, and an
    // incorrect one is replaced. Returns true only when the registered
    // command afterwards is exactly build_command(exe_path).
    bool enable(const std::wstring& exe_path);

    // Idempotent, and scoped: removes ONLY kValueName. Returns true when the
    // value is absent afterwards. Unrelated entries are never read back,
    // rewritten or removed.
    bool disable();

    // Repairs a stale registration after the portable app moved. Does
    // nothing (returns true) when the setting is not enabled -- reconcile
    // must never enroll a user who never turned the feature on.
    bool reconcile(const std::wstring& exe_path);

    // CORE-002: desired-state reconciliation in BOTH directions.
    //   desired_enabled == true  -> reconcile the owned Run value to
    //                               exe_path (repairs a moved executable).
    //   desired_enabled == false -> idempotently REMOVE the owned Run value,
    //                               so a transient failed disable cannot
    //                               leave the machine launching ProTrail
    //                               while the preference says OFF.
    // exe_path is required only for the ON branch; an unresolvable path must
    // not prevent OFF cleanup.
    bool reconcile_desired(bool desired_enabled, const std::wstring& exe_path);

private:
    AutostartBackend& backend_;
};

} // namespace ptd
