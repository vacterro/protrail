#include "application.h"
#include "../core/log.h"
#include "../core/cursor_history.h"
#include "../platform/dpi_awareness.h"
#include "../platform/mouse_input.h"
#include "../render/overlay_manager.h"
#include "../effects/trail_effect.h"
#include "../effects/click_bubble_effect.h"
#include "../ui/settings_window.h"
#include "../ui/tray_icon.h"
#include "../config/app_config.h"
#include "../config/config_storage.h"
#include "../render/render_scheduler.h"
#include "../render/deferred_coalescer.h"

#include <QApplication>
#include <QFont>
#include <QString>
#include <QTimer>
#include <QMetaType>
#include <QAbstractNativeEventFilter>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <atomic>
#include <memory>
#include <functional>

// MVP 05 Phase M: config structs cross Qt signals; register the value
// types so queued connections (and QSignalSpy) can copy them.
Q_DECLARE_METATYPE(ptd::TrailConfig)
Q_DECLARE_METATYPE(ptd::ClickConfig)

namespace {

// T-018: native event filter to handle inter-process single-instance activation
class InstanceActivationFilter : public QAbstractNativeEventFilter {
public:
    explicit InstanceActivationFilter(UINT msg, std::function<void()> cb)
        : msg_(msg), cb_(std::move(cb)) {}

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override {
        if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
            auto* m = static_cast<const MSG*>(message);
            if (m && m->message == msg_) {
                if (cb_) cb_();
                if (result) *result = 0;
                return true;
            }
        }
        return false;
    }

private:
    UINT msg_ = 0;
    std::function<void()> cb_;
};

void app_log(int level, const std::string_view& message) {
    ptd::LogLevel lv = ptd::LogLevel::Info;
    switch (level) {
        case 0: lv = ptd::LogLevel::Trace; break;
        case 1: lv = ptd::LogLevel::Debug; break;
        case 2: lv = ptd::LogLevel::Info; break;
        case 3: lv = ptd::LogLevel::Warn; break;
        case 4: lv = ptd::LogLevel::Error; break;
    }
    ptd::log_write(lv, message);
}

bool verify_d2d_link() {
    Microsoft::WRL::ComPtr<ID2D1Factory> factory;
    D2D1_FACTORY_OPTIONS opts{};
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                   __uuidof(ID2D1Factory), &opts,
                                   reinterpret_cast<void**>(factory.GetAddressOf()));
    if (FAILED(hr)) {
        app_log(4, "D2D1CreateFactory failed");
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    HRESULT d3d = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                    D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 1,
                                    D3D11_SDK_VERSION, device.GetAddressOf(),
                                    nullptr, nullptr);
    if (FAILED(d3d)) {
        app_log(4, "D3D11CreateDevice failed");
        return false;
    }
    if (FAILED(device.As(&dxgi_device))) {
        app_log(4, "QueryInterface(IDXGIDevice) failed");
        return false;
    }

    Microsoft::WRL::ComPtr<IDCompositionDevice> dcomp;
    HRESULT dc = DCompositionCreateDevice(dxgi_device.Get(), __uuidof(IDCompositionDevice),
                                          reinterpret_cast<void**>(dcomp.GetAddressOf()));
    if (FAILED(dc)) {
        app_log(4, "DCompositionCreateDevice failed");
        return false;
    }

    app_log(2, "Direct2D + DirectComposition link verified");
    return true;
}

} // namespace

struct Application::Impl {
    std::unique_ptr<QApplication> qt;
    std::unique_ptr<ptd::ui::SettingsWindow> settings;
    std::unique_ptr<ptd::ui::TrayIcon> tray_icon;
    std::unique_ptr<ptd::OverlayManager> overlay_mgr;
    std::unique_ptr<ptd::MouseInput> mouse_input;
    ptd::CursorHistory cursor_history;

    // MVP 07: formal active-only RenderScheduler with refresh-rate frame pacing.
    std::unique_ptr<ptd::RenderScheduler> scheduler;
    bool render_diag = false;
    ptd::TrailEffect trail_effect;
    ptd::ClickConfig click_config;    // setters only, no persistence
    ptd::ClickBubbleEffect click_effect;
    int64_t last_activity_ns = 0;
    ptd::TrailConfig trail_config;    // setters only (B2), no persistence

    // MVP 05 Phase D/R: master enable. OFF -> no new spawns/rendering,
    // visible effects cleared, timer stopped. Raw input stays registered
    // (cheap to reverse).
    bool master_enabled = true;

    // MVP 06: durable configuration snapshot.
    ptd::AppConfig app_config;

    // T-013R1: the production coalescing authority for WM_DISPLAYCHANGE /
    // WM_DPICHANGED bursts. First notification schedules ONE deferred
    // reconciliation; further notifications before the drain are absorbed.
    ptd::DeferredCoalescer topology_coalescer;

    // T-017R1: application-owned config path (default or isolated for tests).
    std::wstring config_path;

    // T-018: native event filter for single-instance activation.
    std::unique_ptr<InstanceActivationFilter> activation_filter;
};

std::atomic<Application*> Application::s_instance_{nullptr};

Application::Application(std::wstring config_path)
    : d_(std::make_unique<Impl>()) {
    d_->config_path = config_path.empty()
        ? ptd::ConfigStorage::default_config_path()
        : std::move(config_path);
}

Application::~Application() {
    s_instance_.store(nullptr, std::memory_order_release);
}

Application& Application::instance() {
    return *s_instance_.load(std::memory_order_acquire);
}

const std::wstring& Application::config_path() const {
    return d_->config_path;
}

bool Application::initialize() {
    s_instance_.store(this, std::memory_order_release);

    ptd::log_init();
    app_log(2, "ProTrail starting");

    if (!verify_d2d_link()) {
        app_log(3, "D2D/DComp verification failed at startup; continuing");
    }

    // T-017R1: load durable configuration snapshot during initialization so
    // that lifecycle runs without run() do not save uninitialized defaults.
    d_->app_config = ptd::ConfigStorage::load_from_file(d_->config_path);
    d_->master_enabled = d_->app_config.master_enabled;
    d_->trail_config = d_->app_config.trail;
    d_->click_config = d_->app_config.click;
    d_->trail_effect.set_config(d_->trail_config);
    d_->click_effect.set_config(d_->click_config);

    return true;
}

int Application::run() {
    int argc = 0;
    if (!QApplication::instance()) {
        d_->qt = std::make_unique<QApplication>(argc, nullptr);
    }
    QApplication* qapp = d_->qt ? d_->qt.get() : qobject_cast<QApplication*>(QApplication::instance());
    if (qapp) {
        qapp->setQuitOnLastWindowClosed(false);
    }

    // UI.md iron law 1: Verdana, non-antialiased, everywhere, app-wide.
    // (SettingsWindow re-asserts the same strategy on its own font so a
    // future QWidget parent font cannot soften the chrome text.)
    QFont app_font(QStringLiteral("Verdana"), 12);
    app_font.setStyleStrategy(QFont::NoAntialias);
    QApplication::setFont(app_font);

    // T-018: install single-instance activation filter
    if (qapp) {
        const UINT wm_activate = RegisterWindowMessageW(L"ProTrail_ActivateInstance");
        ChangeWindowMessageFilter(wm_activate, 1 /* MSGFLT_ADD */);
        d_->activation_filter = std::make_unique<InstanceActivationFilter>(
            wm_activate, [this] {
                app_log(2, "ProTrail instance activation requested");
                show_settings();
            });
        qapp->installNativeEventFilter(d_->activation_filter.get());
    }

    // Test-only smoke hook (T-017R1 Phase 4): if PROTRAIL_SMOKE_AUTO_EXIT_MS is set,
    // schedule graceful application exit through the Qt event loop.
    // Full lifecycle executes: Qt loop ends -> run() returns -> shutdown() executes -> natural exit.
    wchar_t smoke_buf[16]{};
    const DWORD smoke_len = GetEnvironmentVariableW(L"PROTRAIL_SMOKE_AUTO_EXIT_MS", smoke_buf, 16);
    if (smoke_len > 0 && smoke_len < 16) {
        const int smoke_ms = _wtoi(smoke_buf);
        if (smoke_ms > 0 && qapp) {
            app_log(2, "smoke auto-exit hook armed");
            QTimer::singleShot(smoke_ms, qapp, [this] {
                app_log(2, "smoke auto-exit hook triggered");
                request_exit();
            });
        }
    }

    // Diagnostic rendering is opt-in (MVP 01 closure hygiene): enable with
    // PROTRAIL_DIAG=1; normal startup draws nothing.
    wchar_t diag_buf[8]{};
    const DWORD diag_len = GetEnvironmentVariableW(L"PROTRAIL_DIAG", diag_buf, 8);
    const bool diag = diag_len > 0 && diag_len < 8 && wcscmp(diag_buf, L"1") == 0;

    const auto dpi_actual = ptd::dpi::current_process_awareness();
    app_log(2, std::string("dpi: process awareness ") + ptd::dpi::awareness_name(dpi_actual));

    // Opt-in render & perf diagnostics (MVP 07): PROTRAIL_RENDER_DIAG=1 or PROTRAIL_PERF_DIAG=1
    wchar_t rdiag_buf[8]{};
    const DWORD rdiag_len = GetEnvironmentVariableW(L"PROTRAIL_RENDER_DIAG", rdiag_buf, 8);
    wchar_t pdiag_buf[8]{};
    const DWORD pdiag_len = GetEnvironmentVariableW(L"PROTRAIL_PERF_DIAG", pdiag_buf, 8);
    d_->render_diag = (rdiag_len > 0 && rdiag_len < 8 && wcscmp(rdiag_buf, L"1") == 0)
                   || (pdiag_len > 0 && pdiag_len < 8 && wcscmp(pdiag_buf, L"1") == 0);

    // MVP 07: formal RenderScheduler with display refresh rate pacing
    d_->scheduler = std::make_unique<ptd::RenderScheduler>(qapp);
    d_->scheduler->set_diag_enabled(d_->render_diag);
    d_->scheduler->set_content_check([this](int64_t now) {
        if (!d_->master_enabled) return false;
        const int64_t lifetime_ns =
            static_cast<int64_t>(d_->trail_config.lifetime_ms * 1'000'000.0);
        const bool trail_live = d_->trail_config.enabled
            && !d_->cursor_history.empty()
            && (now - d_->cursor_history.last().timestamp_ns) <= lifetime_ns;
        const bool bubbles_live = d_->click_effect.has_live_content(now);
        return trail_live || bubbles_live;
    });
    d_->scheduler->set_frame_callback(
        [this](int64_t now_ns, double delta_time_s, ptd::FrameAction action) {
            (void)delta_time_s;
            if (!d_->overlay_mgr) return;

            if (action == ptd::FrameAction::RenderContent) {
                if (!d_->master_enabled) return;
                d_->scheduler->begin_frame();
                d_->cursor_history.prune(now_ns);
                d_->click_effect.prune(now_ns);
                d_->overlay_mgr->render_frame(d_->trail_effect, d_->cursor_history,
                                              d_->trail_config, d_->click_effect,
                                              d_->click_config, now_ns);
                d_->scheduler->end_frame();
            } else if (action == ptd::FrameAction::RenderClear) {
                d_->scheduler->begin_frame();
                d_->overlay_mgr->render_frame(d_->trail_effect, d_->cursor_history,
                                              d_->trail_config, d_->click_effect,
                                              d_->click_config, now_ns);
                d_->scheduler->end_frame();
            }
        });

    HINSTANCE inst = GetModuleHandleW(nullptr);

    // MVP 02: dedicated raw-input listener -> GetCursorPos -> CursorHistory.
    d_->mouse_input = std::make_unique<ptd::MouseInput>();
    if (d_->mouse_input->create(inst, &d_->cursor_history)) {
        app_log(2, "mouse input layer started (raw input + GetCursorPos sampling)");
        // B9: input layer -> controller -> render scheduler. MouseInput
        // knows nothing about effects or rendering.
        d_->mouse_input->set_activity_callback(
            [this](const ptd::CursorSample& s) { on_mouse_activity(s); });
    } else {
        app_log(3, "mouse input creation failed; continuing without input sampling");
    }

    // T-007R A3: the 8 ms input-pump QTimer is gone. WM_INPUT for the
    // message-only raw-input HWND is dispatched by Qt's win32 event
    // dispatcher (it pumps all messages of this thread), verified by the
    // protrail_input_dispatch test and the manual global-input checks.
    // No polling timer remains: idle ProTrail generates zero timer wakeups.

    d_->overlay_mgr = std::make_unique<ptd::OverlayManager>();

    // WM_DISPLAYCHANGE/WM_DPICHANGED callback is intentionally lightweight:
    // request deferred work through the production DeferredCoalescer and
    // return from WndProc. Only the queued lambda may reconcile/destroy
    // windows, and it runs from the Qt event loop -- after every triggering
    // WndProc returned. Lifetime: this Application outlives the event loop
    // (main.cpp owns it past run()), shutdown() clears the callback and
    // cancels the coalescer before any teardown, so a queued callback can
    // never dereference destroyed Application/OverlayManager state.
    ptd::OverlayWindow::set_display_change_callback([this, qapp] {
        if (!qapp) return;
        d_->topology_coalescer.request(
            [qapp](std::function<void()> work) {
                QTimer::singleShot(0, qapp, std::move(work));
            },
            [this] {
                if (d_->overlay_mgr) d_->overlay_mgr->refresh_topology();
            });
    });

    if (d_->overlay_mgr->create(inst, diag)) {
        d_->overlay_mgr->show();
        app_log(2, diag ? "overlay manager initialized with diagnostic primitives"
                        : "overlay manager initialized (no diagnostics)");
    } else {
        app_log(3, "overlay manager creation failed; continuing without overlay");
    }

    // MVP 06: load durable configuration snapshot.
    d_->app_config = ptd::ConfigStorage::load_from_file(d_->config_path);
    d_->master_enabled = d_->app_config.master_enabled;
    d_->trail_config = d_->app_config.trail;
    d_->click_config = d_->app_config.click;
    d_->trail_effect.set_config(d_->trail_config);
    d_->click_effect.set_config(d_->click_config);

    // MVP 05: real SettingsWindow with Golden Default theme, live controls
    // for Trail/Click/Master, validated config publication.
    d_->settings = std::make_unique<ptd::ui::SettingsWindow>(
        d_->trail_config, d_->click_config, d_->master_enabled);
    d_->settings->show();
    app_log(2, "Settings window shown (Golden Default theme)");

    // MVP 09: system tray icon with canonical master state and hide-on-close restore
    d_->tray_icon = std::make_unique<ptd::ui::TrayIcon>(d_->master_enabled);
    d_->tray_icon->show();
    app_log(2, "System tray icon initialized");

    QObject::connect(d_->tray_icon.get(), &ptd::ui::TrayIcon::settings_requested,
                     [this] { show_settings(); });
    QObject::connect(d_->tray_icon.get(), &ptd::ui::TrayIcon::master_enabled_toggled,
                     [this](bool on) {
                         set_master_enabled(on);
                         if (d_->settings) d_->settings->set_master_enabled(on);
                         save_config();
                     });
    QObject::connect(d_->tray_icon.get(), &ptd::ui::TrayIcon::exit_requested,
                     [this] {
                         app_log(2, "ProTrail exit requested via tray");
                         request_exit();
                     });

    // Config publication (Phase M): SettingsWindow emits validated configs.
    // Application publishes to effects and wakes the scheduler if visual
    // refresh is needed.
    QObject::connect(d_->settings.get(), &ptd::ui::SettingsWindow::trail_config_changed,
            [this](const ptd::TrailConfig& c) {
                d_->trail_config = c;
                d_->app_config.trail = c;
                d_->trail_effect.set_config(c);
                save_config();
                if (d_->master_enabled && d_->trail_config.enabled)
                    start_effect_rendering();
            });
    QObject::connect(d_->settings.get(), &ptd::ui::SettingsWindow::click_config_changed,
            [this](const ptd::ClickConfig& c) {
                d_->click_config = c;
                d_->app_config.click = c;
                d_->click_effect.set_config(c);
                save_config();
                if (d_->master_enabled && d_->click_config.enabled)
                    start_effect_rendering();
            });
    // T-015: presets publish ONE coherent (Trail, Click) pair -- update
    // both canonical states, both effects, save ONCE, wake rendering ONCE.
    // No intermediate half-states, no double save, no double render wake.
    QObject::connect(d_->settings.get(), &ptd::ui::SettingsWindow::preset_applied,
            [this](const ptd::TrailConfig& t, const ptd::ClickConfig& c) {
                d_->trail_config = t;
                d_->click_config = c;
                d_->app_config.trail = t;
                d_->app_config.click = c;
                d_->trail_effect.set_config(t);
                d_->click_effect.set_config(c);
                save_config();
                if (d_->master_enabled && (t.enabled || c.enabled))
                    start_effect_rendering();
            });
    QObject::connect(d_->settings.get(), &ptd::ui::SettingsWindow::master_enabled_changed,
            [this](bool on) {
                set_master_enabled(on);
                if (d_->tray_icon) d_->tray_icon->set_master_enabled(on);
                save_config();
            });
    QObject::connect(d_->settings.get(), &ptd::ui::SettingsWindow::trail_enabled_changed,
            [this](bool on) {
                if (d_->trail_config.enabled != on) {
                    d_->trail_config.enabled = on;
                    d_->app_config.trail.enabled = on;
                    d_->trail_effect.set_config(d_->trail_config);
                    save_config();
                    if (d_->master_enabled && on) start_effect_rendering();
                }
            });
    QObject::connect(d_->settings.get(), &ptd::ui::SettingsWindow::click_enabled_changed,
            [this](bool on) {
                if (d_->click_config.enabled != on) {
                    d_->click_config.enabled = on;
                    d_->app_config.click.enabled = on;
                    d_->click_effect.set_config(d_->click_config);
                    if (!on) {
                        // Phase R: disabling click clears visible bubbles now.
                        d_->click_effect.clear();
                        if (d_->master_enabled && !d_->trail_config.enabled)
                            start_effect_rendering(); // ensure final clear frame
                    }
                    save_config();
                    if (d_->master_enabled && on) start_effect_rendering();
                }
            });

    return qapp ? qapp->exec() : 0;
}

void Application::on_mouse_activity(const ptd::CursorSample& newest) {
    // Phase R: master OFF -> no new trail rendering, no new bubbles, no
    // scheduler wake. Raw input remains registered (cheap to reverse).
    if (!d_->master_enabled) return;

    // B8: input starts/wakes rendering. Called on the GUI thread from the
    // raw-input WndProc path.
    d_->last_activity_ns = newest.timestamp_ns;

    // T-009 C3: normalized Down transition -> bubble spawn. The effect
    // checks enabled + action itself; the spawn position is the one stored
    // IN this click sample (never a later cursor position). Up/movement
    // samples return false and spawn nothing.
    const bool spawned = d_->click_effect.on_button_down(newest);

    // C10/C11: wake the shared active-only scheduler when either effect
    // can produce live content. Movement wakes only for the trail; a
    // Down-spawned bubble wakes even with the trail disabled.
    if (d_->trail_config.enabled || spawned) {
        start_effect_rendering();
    }
}

// MVP 05 Phase R: master enable/disable.
// OFF: clear visible effects deterministically (no frozen trail/bubble),
//      present a transparent clear, stop the timer, and refuse new
//      spawns/renders. Re-enable does not resurrect stale history.
void Application::set_master_enabled(bool on) {
    if (d_->master_enabled == on) return;
    d_->master_enabled = on;
    d_->app_config.master_enabled = on;
    if (d_->tray_icon) d_->tray_icon->set_master_enabled(on);
    if (on) {
        app_log(2, "ProTrail master enable: ON");
        // Clear history & bubbles so no stale input resurrects.
        d_->cursor_history.clear();
        d_->click_effect.clear();
        return;
    }
    app_log(2, "ProTrail master enable: OFF (clearing effects)");
    d_->cursor_history.clear();
    d_->click_effect.clear();
    // One final transparent clear so nothing frozen stays on screen.
    if (d_->overlay_mgr) {
        d_->overlay_mgr->render_frame(d_->trail_effect, d_->cursor_history,
                                      d_->trail_config, d_->click_effect,
                                      d_->click_config, ptd::now_ns());
    }
    stop_effect_rendering();
}

void Application::start_effect_rendering() {
    if (d_->scheduler) {
        d_->scheduler->wake();
    }
}

void Application::render_effect_tick() {
    if (!d_->scheduler) return;
    const int64_t now = ptd::now_ns();
    const int64_t lifetime_ns =
        static_cast<int64_t>(d_->trail_config.lifetime_ms * 1'000'000.0);
    const bool trail_live = d_->trail_config.enabled
        && !d_->cursor_history.empty()
        && (now - d_->cursor_history.last().timestamp_ns) <= lifetime_ns;
    const bool bubbles_live = d_->click_effect.has_live_content(now);
    d_->scheduler->on_timer_tick(d_->master_enabled && (trail_live || bubbles_live));
}

void Application::stop_effect_rendering() {
    if (d_->scheduler) {
        d_->scheduler->force_idle();
    }
}

void Application::show_settings() {
    if (!d_->settings) return;
    if (d_->settings->isMinimized()) {
        d_->settings->showNormal();
    } else {
        d_->settings->show();
    }
    d_->settings->raise();
    d_->settings->activateWindow();
    HWND hwnd = reinterpret_cast<HWND>(d_->settings->winId());
    if (hwnd) {
        SetForegroundWindow(hwnd);
    }
}

void Application::request_exit() {
    app_log(2, "ProTrail exit requested");
    if (d_->qt) {
        d_->qt->quit();
    } else if (QApplication::instance()) {
        QApplication::instance()->quit();
    }
}

void Application::shutdown() {
    app_log(2, "ProTrail shutting down");
    save_config();
    if (d_->activation_filter) {
        if (d_->qt) {
            d_->qt->removeNativeEventFilter(d_->activation_filter.get());
        } else if (QApplication::instance()) {
            QApplication::instance()->removeNativeEventFilter(d_->activation_filter.get());
        }
        d_->activation_filter.reset();
    }
    ptd::OverlayWindow::clear_display_change_callback();
    d_->topology_coalescer.cancel();
    stop_effect_rendering();
    if (d_->scheduler) {
        d_->scheduler.reset();
    }
    if (d_->overlay_mgr) {
        d_->overlay_mgr->hide();
        d_->overlay_mgr->destroy();
        d_->overlay_mgr.reset();
    }
    if (d_->mouse_input) {
        d_->mouse_input->destroy();
        d_->mouse_input.reset();
    }
    d_->cursor_history.clear();
    d_->click_effect.clear();
    if (d_->tray_icon) {
        d_->tray_icon->hide();
        d_->tray_icon.reset();
    }
    d_->settings.reset();
    if (d_->qt) {
        d_->qt.reset();
    }
    app_log(2, "ProTrail shutdown complete");
}

void Application::save_config() {
    d_->app_config.master_enabled = d_->master_enabled;
    d_->app_config.trail = d_->trail_config;
    d_->app_config.click = d_->click_config;
    ptd::ConfigStorage::save_to_file(d_->app_config, d_->config_path);
}
