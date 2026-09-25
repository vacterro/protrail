// T-032 Silent Windows autostart regressions.
//
// TWO RULES GOVERN THIS SUITE.
//
//  1. Every test drives the IN-MEMORY backend. A routine CTest run must
//     never modify the developer's real Windows startup entries, so the
//     suite additionally reads the real per-user Run key before and after
//     and proves it is unchanged (real_run_key_is_untouched_by_this_suite).
//
//  2. The manager's promises are behavioural, not structural: enable writes
//     the exact quoted command (and only rewrites when it actually differs),
//     disable removes exactly one owned value and never touches a neighbour,
//     and both are idempotent. A verification by re-read backs every write,
//     so a backend that lies cannot produce a false success.

#include "../src/platform/autostart.h"
#include "../src/app/startup_mode.h"

#include <QTest>

#include <string>
#include <vector>

namespace {

constexpr const wchar_t* kOtherProduct = L"SomeOtherApp";
constexpr const wchar_t* kOtherCommand = L"C:\\Other\\other.exe --minimized";

bool contains(const std::vector<std::wstring>& haystack, const std::wstring& needle) {
    for (const auto& item : haystack) {
        if (item == needle) return true;
    }
    return false;
}

} // namespace

class TestAutostart : public QObject {
    Q_OBJECT

private slots:
    // ---- command construction / quoting ----

    void command_quotes_a_path_containing_spaces() {
        const std::wstring exe = L"C:\\Program Files\\ProTrail Portable\\protrail.exe";
        const std::wstring command = ptd::AutostartManager::build_command(exe);

        // Fully quoted executable path: an unquoted path with a space would
        // be split into two arguments by CreateProcess and Windows would try
        // to launch C:\Program.
        QVERIFY(command.find(L"\"C:\\Program Files\\ProTrail Portable\\protrail.exe\"")
                == 0);
        // Explicit startup argument, and nothing else.
        QVERIFY(command.find(ptd::kStartupMinimizedArgument) != std::wstring::npos);

        const std::vector<std::wstring> args = ptd::split_command_line(command);
        QCOMPARE(args.size(), std::size_t(2));
        QCOMPARE(args[0], exe);
        QCOMPARE(args[1], std::wstring(ptd::kStartupMinimizedArgument));
    }

    // A portable ZIP may be extracted anywhere, including a non-ASCII user
    // path; the registered command must carry that exact path unchanged.
    void command_roundtrips_a_unicode_portable_path() {
        const std::wstring exe =
            L"C:\\Users\\J\u00FCrgen\\Pro Trail \u6E2C\u8A66\\nested\\protrail.exe";
        const std::wstring command = ptd::AutostartManager::build_command(exe);
        const std::vector<std::wstring> args = ptd::split_command_line(command);
        QCOMPARE(args.size(), std::size_t(2));
        QCOMPARE(args[0], exe);
        QCOMPARE(args[1], std::wstring(ptd::kStartupMinimizedArgument));

        // Registered from an older extraction, then launched from the Unicode
        // location: reconcile follows the real executable path exactly.
        ptd::InMemoryAutostartBackend backend;
        ptd::AutostartManager manager(backend);
        QVERIFY(manager.enable(L"C:\\Downloads\\ProTrail\\protrail.exe"));
        QVERIFY(manager.reconcile_desired(true, exe));
        QCOMPARE(manager.registered_command(), command);
    }

    void command_quoting_survives_quotes_and_trailing_backslashes() {
        const std::vector<std::wstring> hostile = {
            L"C:\\plain\\protrail.exe",
            L"C:\\with space\\protrail.exe",
            L"C:\\quote\"inside\\protrail.exe",
            L"C:\\trailing\\",
            L"C:\\mixed \\\"quoted\\\" tail\\",
        };
        for (const auto& exe : hostile) {
            const std::wstring command = ptd::AutostartManager::build_command(exe);
            const std::vector<std::wstring> args = ptd::split_command_line(command);
            QCOMPARE(args.size(), std::size_t(2));
            QCOMPARE(args[0], exe);
            QCOMPARE(args[1], std::wstring(ptd::kStartupMinimizedArgument));
        }
    }

    // ---- enable / disable semantics ----

    void enable_creates_the_owned_command_and_is_idempotent() {
        ptd::InMemoryAutostartBackend backend;
        ptd::AutostartManager manager(backend);
        const std::wstring exe = L"C:\\Apps\\ProTrail\\protrail.exe";

        QVERIFY(!manager.is_enabled());
        QVERIFY(manager.enable(exe));
        QVERIFY(manager.is_enabled());
        QCOMPARE(manager.registered_command(),
                 ptd::AutostartManager::build_command(exe));
        QCOMPARE(backend.write_calls, 1);

        // Repeated enable: already correct, so no rewrite and still true.
        for (int i = 0; i < 5; ++i) {
            QVERIFY(manager.enable(exe));
        }
        QCOMPARE(backend.write_calls, 1);
        QCOMPARE(manager.registered_command(),
                 ptd::AutostartManager::build_command(exe));
    }

    void disable_removes_only_the_owned_value() {
        ptd::InMemoryAutostartBackend backend;
        backend.values.emplace_back(kOtherProduct, kOtherCommand);
        backend.values.emplace_back(L"Unrelated", L"C:\\nope.exe");
        const std::vector<std::pair<std::wstring, std::wstring>> neighbours =
            backend.values;

        ptd::AutostartManager manager(backend);
        QVERIFY(manager.enable(L"C:\\Apps\\protrail.exe"));
        QVERIFY(manager.disable());

        QVERIFY(!manager.is_enabled());
        QCOMPARE(manager.registered_command(), std::wstring());
        // Unrelated startup entries are byte-identical: no enumeration and
        // rewrite, no accidental neighbour deletion.
        QCOMPARE(backend.values, neighbours);
        QCOMPARE(backend.names().size(), std::size_t(2));
        QVERIFY(!contains(backend.names(), std::wstring(ptd::AutostartManager::kValueName)));
    }

    void disable_is_idempotent() {
        ptd::InMemoryAutostartBackend backend;
        ptd::AutostartManager manager(backend);

        // Disabling what was never enabled is a success, not an error.
        QVERIFY(manager.disable());
        QVERIFY(manager.disable());

        QVERIFY(manager.enable(L"C:\\Apps\\protrail.exe"));
        QVERIFY(manager.disable());
        QVERIFY(manager.disable());
        QVERIFY(manager.disable());
        QVERIFY(!manager.is_enabled());
    }

    void moved_executable_reconciles_the_owned_command() {
        ptd::InMemoryAutostartBackend backend;
        ptd::AutostartManager manager(backend);
        const std::wstring old_exe = L"C:\\Old Location\\protrail.exe";
        const std::wstring new_exe = L"D:\\New Location\\ProTrail\\protrail.exe";

        QVERIFY(manager.enable(old_exe));
        QCOMPARE(manager.registered_command(),
                 ptd::AutostartManager::build_command(old_exe));

        // The setting is ON and the app now runs from somewhere else: the
        // registration must follow the real executable, not the stale path.
        QVERIFY(manager.reconcile(new_exe));
        QCOMPARE(manager.registered_command(),
                 ptd::AutostartManager::build_command(new_exe));

        // Reconciling an already-correct registration changes nothing.
        const int writes_before = backend.write_calls;
        QVERIFY(manager.reconcile(new_exe));
        QCOMPARE(backend.write_calls, writes_before);
    }

    void reconcile_never_enrolls_a_user_who_did_not_enable_it() {
        ptd::InMemoryAutostartBackend backend;
        ptd::AutostartManager manager(backend);

        QVERIFY(manager.reconcile(L"C:\\Apps\\protrail.exe"));
        QVERIFY(!manager.is_enabled());
        QCOMPARE(backend.write_calls, 0);
        QVERIFY(backend.values.empty());
    }

    // ---- failure reporting ----

    void backend_failures_are_reported_not_swallowed() {
        ptd::InMemoryAutostartBackend backend;
        ptd::AutostartManager manager(backend);

        backend.fail_writes = true;
        QVERIFY(!manager.enable(L"C:\\Apps\\protrail.exe"));
        QVERIFY(!manager.is_enabled());

        backend.fail_writes = false;
        QVERIFY(manager.enable(L"C:\\Apps\\protrail.exe"));

        backend.fail_removes = true;
        QVERIFY(!manager.disable());
        QVERIFY(manager.is_enabled());  // still registered, honestly reported

        backend.fail_removes = false;
        QVERIFY(manager.disable());
    }

    // T-39: a failing backend read must be distinguished from not-registered.
    // Reconcile and is_enabled report failure, and reconcile never enrolls
    // a user whose preference is OFF.
    void failing_read_reports_failure_rather_than_not_registered() {
        ptd::InMemoryAutostartBackend backend;
        ptd::AutostartManager manager(backend);
        const std::wstring exe = L"C:\\Apps\\protrail.exe";

        // Normal state: enabled
        QVERIFY(manager.enable(exe));
        bool enabled = false;
        QVERIFY(manager.is_enabled(enabled));
        QVERIFY(enabled);
        QVERIFY(manager.is_enabled());
        QCOMPARE(manager.last_error(), 0ul);

        // Inject read failure
        backend.fail_reads = true;

        // reconcile must report failure, NOT false success
        QVERIFY(!manager.reconcile(exe));
        QVERIFY(manager.last_error() != 0);

        // is_enabled query variant must report failure, NOT false not-registered
        enabled = true;
        QVERIFY(!manager.is_enabled(enabled));
        QVERIFY(manager.last_error() != 0);

        // 0-arg is_enabled returns false, and last_error surfaces failure
        QVERIFY(!manager.is_enabled());
        QVERIFY(manager.last_error() != 0);

        // Clear read failure: registration still intact
        backend.fail_reads = false;
        QVERIFY(manager.is_enabled(enabled));
        QVERIFY(enabled);
        QVERIFY(manager.is_enabled());

        // When preference is OFF (unregistered): reconcile never enrolls user
        QVERIFY(manager.disable());
        QVERIFY(backend.values.empty());
        QVERIFY(manager.reconcile(exe));
        QVERIFY(manager.is_enabled(enabled));
        QVERIFY(!enabled);
        QVERIFY(backend.values.empty());
        QCOMPARE(backend.write_calls, 1);
    }

    // ---- CORE-002: bidirectional desired-state reconciliation ----

    void reconcile_desired_off_removes_a_stale_owned_value() {
        ptd::InMemoryAutostartBackend backend;
        backend.values.emplace_back(kOtherProduct, kOtherCommand);
        ptd::AutostartManager manager(backend);
        QVERIFY(manager.enable(L"C:\\Apps\\protrail.exe"));
        QVERIFY(manager.is_enabled());

        // Preference is OFF: the stale owned value must be removed even though
        // reconcile_autostart() used to return early on OFF.
        QVERIFY(manager.reconcile_desired(false, L""));
        QVERIFY(!manager.is_enabled());
        // Unrelated values untouched.
        QCOMPARE(backend.values.size(), std::size_t(1));
        QVERIFY(contains(backend.names(), std::wstring(kOtherProduct)));
    }

    void reconcile_desired_off_without_owned_value_never_writes() {
        ptd::InMemoryAutostartBackend backend;
        ptd::AutostartManager manager(backend);
        QVERIFY(manager.reconcile_desired(false, L""));
        QVERIFY(!manager.is_enabled());
        QCOMPARE(backend.write_calls, 0);
        QVERIFY(backend.values.empty());
    }

    void reconcile_desired_on_reconciles_a_moved_executable() {
        ptd::InMemoryAutostartBackend backend;
        ptd::AutostartManager manager(backend);
        const std::wstring old_exe = L"C:\\Old\\protrail.exe";
        const std::wstring new_exe = L"D:\\New\\protrail.exe";
        QVERIFY(manager.enable(old_exe));
        QVERIFY(manager.reconcile_desired(true, new_exe));
        QCOMPARE(manager.registered_command(),
                 ptd::AutostartManager::build_command(new_exe));
    }

    // The failure/recovery regression: a disable that fails while the
    // preference is OFF must be repaired by the NEXT reconcile_desired(false).
    void reconcile_desired_off_retries_a_failed_removal() {
        ptd::InMemoryAutostartBackend backend;
        ptd::AutostartManager manager(backend);
        QVERIFY(manager.enable(L"C:\\Apps\\protrail.exe"));

        // Immediate removal fails: the machine is out of sync with the OFF
        // preference, but the Run value survives.
        backend.fail_removes = true;
        QVERIFY(!manager.reconcile_desired(false, L""));
        QVERIFY(manager.is_enabled());

        // Next start: failure cleared, reconcile enforces the OFF desire.
        backend.fail_removes = false;
        QVERIFY(manager.reconcile_desired(false, L""));
        QVERIFY(!manager.is_enabled());
    }

    // ---- startup-mode detection ----

    void startup_argument_selects_the_silent_mode() {
        // The command ProTrail itself registers must parse back as silent.
        const std::wstring registered =
            ptd::AutostartManager::build_command(L"C:\\Program Files\\ProTrail\\protrail.exe");
        QCOMPARE(ptd::parse_startup_mode(registered), ptd::StartupMode::AutostartMinimized);
        QVERIFY(ptd::startup_mode_is_silent(ptd::parse_startup_mode(registered)));

        // A manual launch is anything without the argument.
        QCOMPARE(ptd::parse_startup_mode(L"C:\\ProTrail\\protrail.exe"),
                 ptd::StartupMode::Normal);
        QCOMPARE(ptd::parse_startup_mode(L""), ptd::StartupMode::Normal);
        QCOMPARE(ptd::parse_startup_mode(L"\"C:\\a b\\protrail.exe\" --other-flag"),
                 ptd::StartupMode::Normal);

        // The argument is honoured wherever it appears, and its casing does
        // not matter (an older build may have written it differently).
        QCOMPARE(ptd::parse_startup_mode(L"C:\\protrail.exe --startup-minimized"),
                 ptd::StartupMode::AutostartMinimized);
        QCOMPARE(ptd::parse_startup_mode(L"\"C:\\protrail.exe\" --STARTUP-MINIMIZED"),
                 ptd::StartupMode::AutostartMinimized);
        QCOMPARE(ptd::parse_startup_mode(
                     L"C:\\protrail.exe --verbose --startup-minimized --other"),
                 ptd::StartupMode::AutostartMinimized);
    }

    void autostart_mode_never_requests_the_product_window() {
        QVERIFY(ptd::startup_mode_requests_product_window(ptd::StartupMode::Normal));
        QVERIFY(!ptd::startup_mode_requests_product_window(ptd::StartupMode::AutostartMinimized));
        QVERIFY(ptd::startup_mode_is_silent(ptd::StartupMode::AutostartMinimized));
        QVERIFY(!ptd::startup_mode_is_silent(ptd::StartupMode::Normal));
    }

    // ---- the real registry is never touched by this suite ----

    void real_run_key_is_untouched_by_this_suite() {
        ptd::Win32RunKeyBackend real;
        std::wstring before_value;
        const bool existed_before = real.read(ptd::AutostartManager::kValueName, before_value);
        const std::vector<std::wstring> before_names = real.names();

        // Exercise the WHOLE manager surface through the in-memory backend,
        // including the paths that "would" write.
        ptd::InMemoryAutostartBackend backend;
        ptd::AutostartManager manager(backend);
        manager.enable(L"C:\\Program Files\\ProTrail\\protrail.exe");
        manager.reconcile(L"D:\\moved\\protrail.exe");
        manager.disable();
        manager.enable(L"C:\\Program Files\\ProTrail\\protrail.exe");
        QVERIFY(manager.is_enabled());

        ptd::Win32RunKeyBackend after;
        std::wstring after_value;
        const bool exists_after = after.read(ptd::AutostartManager::kValueName, after_value);
        const std::vector<std::wstring> after_names = after.names();

        QCOMPARE(exists_after, existed_before);
        QCOMPARE(after_value, before_value);
        QCOMPARE(after_names, before_names);
    }

    void real_backend_reads_the_per_user_run_key_without_elevation() {
        // Read-only probe of the production backend: it must be usable
        // without administrator rights and must report "absent" as a plain
        // false rather than an error. This is what makes the feature
        // elevation-free by construction.
        ptd::Win32RunKeyBackend real;
        std::wstring value;
        const bool present = real.read(ptd::AutostartManager::kValueName, value);
        if (present) {
            QVERIFY(!value.empty());
            QVERIFY(value.find(L"protrail") != std::wstring::npos
                    || value.find(L"ProTrail") != std::wstring::npos);
        }
        // `names()` must succeed regardless (the key exists on every
        // supported Windows version), proving the probe itself is not the
        // thing that needs elevation.
        const std::vector<std::wstring> names = real.names();
        (void)names;
        QCOMPARE(real.last_error(), 0UL);
    }
};

QTEST_MAIN(TestAutostart)
#include "test_autostart.moc"
