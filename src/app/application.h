#pragma once

#include <atomic>
#include <memory>
#include <string>

namespace ptd { struct CursorSample; }

// B8/C10: active-only render scheduling lives here. There is NO permanent
// render timer: rendering runs on a GUI-thread QTimer ONLY while effect
// content is alive (input wake -> frames until the trail lifetime AND all
// click bubbles expire -> one final transparent clear -> timer stops).
// Idle ProTrail has zero timer wakeups. The formal performance scheduler
// remains MVP 07.
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

    const std::wstring& config_path() const;

    static Application& instance();

private:
    // B9: input-layer activity -> controller (renderer stays independent).
    void on_mouse_activity(const ptd::CursorSample& newest);
    // B8: one effect frame; stops the render timer after the final
    // transparent frame has been presented.
    void render_effect_tick();
    // B8: wake/stop the active-only render timer.
    void start_effect_rendering();
    void stop_effect_rendering();
    // MVP 05 Phase R: master enable/disable with deterministic clear.
    void set_master_enabled(bool on);
    // MVP 06: persist current config snapshot to disk.
    void save_config();
    // MVP 09: show/restore SettingsWindow from tray or startup.
    void show_settings();

    struct Impl;
    std::unique_ptr<Impl> d_;
    static std::atomic<Application*> s_instance_;
};
