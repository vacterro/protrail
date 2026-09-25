#pragma once

#include "startup_mode.h"
#include "../config/app_config.h"
#include "../render/overlay_manager.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace ptd {
class CursorHistory;
struct CursorSample;
} // namespace ptd

// B8/C10: active-only render scheduling lives here. There is NO permanent
// render timer: rendering runs on a GUI-thread QTimer ONLY while effect
// content is alive (input wake -> frames until the trail lifetime AND all
// click bubbles expire -> one final transparent clear -> timer stops).
// Idle ProTrail has zero timer wakeups. The formal performance scheduler
// remains MVP 07.
namespace ptd {
class SingleInstance;
}

namespace ptd {
namespace ui {
class SettingsWindow;
class TrayIcon;
} // namespace ui
} // namespace ptd

class Application {
public:
    explicit Application(std::wstring config_path = {});
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    bool initialize();
    int run();
    void shutdown();
    void request_exit();
    void set_single_instance(ptd::SingleInstance* si);
    void show_main();
    void set_current_as_release_defaults();

    // T-032: how this process was launched. StartupMode::AutostartMinimized
    // (Windows Run entry) initializes every service and the tray but never
    // shows the ProTrail window; StartupMode::Normal preserves the
    // existing user-facing launch behaviour. Set before run().
    void set_startup_mode(ptd::StartupMode mode);

    // Isolated (smoke) launches: route the Start with Windows side effect to a
    // caller-owned backend so the run never reads, rewrites or removes the
    // real per-user Run entry of the operator. Must be set before initialize().
    void set_autostart_backend(ptd::AutostartBackend* backend);

    // T-032 test seam: inject a non-owning autostart backend (an in-memory
    // double) so a controller-level run can exercise the autostart wiring
    // without touching the real per-user Run key. nullptr restores the
    // production Win32 backend. Must be set before initialize().
    void set_autostart_backend_for_tests(ptd::AutostartBackend* backend);

    // W2-001 integration seams. Tests may inject one synthetic topology and
    // short retry delays before run(); production keeps native defaults.
    void set_overlay_manager_for_tests(std::unique_ptr<ptd::OverlayManager> manager);
    void set_topology_retry_policy_for_tests(int max_attempts,
                                             int base_delay_ms,
                                             int max_delay_ms);
    bool topology_retry_pending_for_tests() const;

    const std::wstring& config_path() const;

    // CORE-001: whether writing configuration is currently allowed for the
    // loaded source (false for unsupported-future-schema, failed malformed
    // backup, or read failure). Read-only for lifecycle regressions.
    bool persistence_allowed() const;

    // CORE-001 test seam: invoke the ordinary settings persistence path
    // exactly as a Settings publication would, returning its result, so a
    // regression can prove a protected source is never overwritten.
    bool save_config_for_tests();

    // PERF-002: durable persistence is debounced for high-frequency visual
    // edits (slider drags): the effect/UI updates stay immediate, while the
    // on-disk write happens once after the drag settles. Lifecycle/discrete
    // operations still save immediately, and shutdown flushes any pending
    // dirty state synchronously.
     enum class FlushResult : int { NonePending = 0, FlushedOk = 1, FlushedError = 2 };
    void request_deferred_save();
    FlushResult flush_pending_save();
    // Test seams.
    void set_save_debounce_ms_for_tests(int ms);
    int save_invocation_count_for_tests() const;
    void reset_save_invocation_count_for_tests();
    bool config_dirty_for_tests() const;

    // The one canonical ProTrail product window.
    ptd::ui::SettingsWindow* product_window_for_tests() const;
    ptd::ui::TrayIcon* tray_icon_for_tests() const;

    static Application& instance();

    // ---- T-021: Trail enable-lifecycle freshness (controller contract) ----
    //
    // CursorHistory is SHARED with the input layer: MouseInput::record_sample
    // pushes every movement/button sample BEFORE it notifies this controller,
    // so samples keep arriving while the CHILD Trail toggle is off. Only the
    // master toggle had clear-on-transition hygiene; without the equivalent
    // child rule, re-enabling Trail made the freshly woken scheduler render the
    // whole disabled-period path as a new trail.
    //
    //   enabled  -> disabled : drop the history immediately and present one
    //                          transparent clear frame so no trail stays
    //                          frozen. Click bubbles are untouched -- they are
    //                          independent of the Trail toggle.
    //   disabled -> enabled  : drop it again, so ONLY movement received after
    //                          the re-enable can create trail geometry.
    //
    // This is the canonical child-toggle transition (the SettingsWindow
    // signal forwards here) and it persists trail.enabled like every other
    // config publication.
    void set_trail_enabled(bool on);
    bool trail_enabled() const;

    // T-021: the complete input-ingestion path MouseInput drives -- push into
    // the shared history, then notify the controller (B9). Exposed so
    // controller-level lifecycle regressions exercise the REAL transitions
    // against the REAL store instead of a copy that can drift from production.
    void record_cursor_sample(const ptd::CursorSample& sample);

    // T-021: read-only lifecycle state the frame callback and the scheduler
    // content check render from. The two `*_content_live` flags are already
    // gated by the master toggle, so they answer "would this draw now?".
    struct LifecycleSnapshot {
        bool master_enabled = true;
        bool trail_enabled = true;
        bool click_enabled = true;
        std::size_t trail_history_size = 0;
        bool trail_content_live = false;
        bool click_content_live = false;
    };
    LifecycleSnapshot lifecycle_snapshot(int64_t now_ns) const;

    // T-021: the shared input store itself, so a regression can drive the
    // production TrailEffect with the controller's real history.
    const ptd::CursorHistory& trail_history() const;

private:
    // B9: input-layer activity -> controller (renderer stays independent).
    void on_mouse_activity(const ptd::CursorSample& newest);
    // B8: one effect frame; stops the render timer after the final
    // transparent frame has been presented.
    void render_effect_tick();
    // B8: wake/stop the active-only render timer.
    void start_effect_rendering();
    void stop_effect_rendering();
    // T-021: the ONE live-content gate -- the frame callback, the scheduler
    // content check and the lifecycle snapshot all evaluate it here; a second
    // copy of this arithmetic is exactly how they drift apart.
    bool trail_content_live(int64_t now_ns) const;
    bool click_content_live(int64_t now_ns) const;
    // T-021: shared body of both Trail toggle directions. Pure lifecycle:
    // callers own config publication and persistence.
    void apply_trail_toggle_transition(bool was_enabled, bool now_enabled);
    // MVP 05 Phase R: master enable/disable with deterministic clear.
    void set_master_enabled(bool on);
    // T-032: apply the Start with Windows preference to the machine. The
    // registry side effect and the persistence are ONE operation, so the
    // saved preference and the actual Run entry cannot drift apart.
    void apply_start_with_windows(bool on);
    // T-032: idempotent registration reconcile against the real current
    // executable path. Only ever runs when the preference is ON.
    void reconcile_autostart();
    // CORE-003 + W2-002: ONE Application-level transaction for a whole-
    // AppConfig operation (Restore All / apply_config / dev preset). Captures
    // the OLD state, installs the complete NEW config, applies the Master /
    // Trail / Click enable-edge invariants exactly once, reconciles the
    // Start-with-Windows desired state, persists ONCE, and wakes rendering
    // only as the final state requires.
    void apply_app_config_transaction(const ptd::AppConfig& cfg);
    // MVP 06: persist current config snapshot to disk. T-029: the result is
    // returned AND a failed persistence is logged with settings context --
    // a config write failure must never be swallowed. CORE-001: every save
    // path, including shutdown, respects the load provenance protection --
    // a blocked save is explicit and never touches a protected source.
    bool save_config();

    // W2-001: single reconciliation authority + bounded retry lifecycle.
    // PERF-001: scheduler pacing prepared to consume PERF-002's exact per-overlay
    // primitive assignment — MIL-C gate, full closure waits for PERF-002/003.
    void handle_topology_convergence(ptd::OverlayManager::Convergence result);
    void perform_topology_retry();
    void update_scheduler_target_fps();


    struct Impl;
    std::unique_ptr<Impl> d_;
    static std::atomic<Application*> s_instance_;
};
