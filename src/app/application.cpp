#include "application.h"
#include "request_dedup.h"
#include "single_instance.h"
#include "../platform/autostart.h"
#include "../core/log.h"
#include "../core/cursor_history.h"
#include "../platform/dpi_awareness.h"
#include "../platform/mouse_input.h"
#include "../render/overlay_manager.h"
#include "../effects/trail_effect.h"
#include "../effects/click_bubble_effect.h"
#include "../ui/settings_window.h"
#include "../ui/main_window.h"
#include "../ui/tray_icon.h"
#include "../config/app_config.h"
#include "../config/config_storage.h"
#include "../config/dev_defaults.h"
#include "../config/release_defaults.h"
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
#include <cstdint>
#include <memory>
#include <functional>

// MVP 05 Phase M: config structs cross Qt signals; register the value
// types so queued connections (and QSignalSpy) can copy them.
Q_DECLARE_METATYPE(ptd::TrailConfig)
Q_DECLARE_METATYPE(ptd::ClickConfig)

namespace {

// T-018: native event filter to handle inter-process single-instance
// activation. T-018R3: the handler receives the claimant's request
// identity carried by the registered message so the owner can acknowledge
// the exact request AFTER the activation handling executed.
class InstanceActivationFilter : public QAbstractNativeEventFilter {
public:
    using Handler = std::function<void(WPARAM, LPARAM)>;
    explicit InstanceActivationFilter(UINT msg, Handler handler)
        : msg_(msg), handler_(std::move(handler)) {}

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override {
        if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
            auto* m = static_cast<const MSG*>(message);
            if (m && m->message == msg_) {
                if (handler_) handler_(m->wParam, m->lParam);
                if (result) *result = 0;
                return true;
            }
        }
        return false;
    }

private:
    UINT msg_ = 0;
    Handler handler_;
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
    // T-37: compact Main Essentials surface. A VIEW over the same canonical
    // AppConfig; its publications are wired into the same controller paths.
    std::unique_ptr<ptd::ui::MainWindow> main_window;
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

    // T-018R3 / W2-004: request identity dedup for HWND_BROADCAST fan-out.
    // One bounded recent-request set shared by the interactive and quiet
    // channels, because they use one request-id namespace. GUI-thread only.
    ptd::RequestDedup activation_dedup;

    // T-018: non-owning reference to the production SingleInstance owner.
    // nullptr in smoke mode. Set by set_single_instance() before run().
    ptd::SingleInstance* single_instance = nullptr;

    // T-018: native event filter for single-instance activation.
    std::unique_ptr<InstanceActivationFilter> activation_filter;
    // T-032: the quiet presence channel (autostart hand-over that must not
    // open any window).
    std::unique_ptr<InstanceActivationFilter> presence_filter;

    // T-032: how this process was launched.
    ptd::StartupMode startup_mode = ptd::StartupMode::Normal;

    // T-032: the Start with Windows side effect. The production backend is
    // the per-user Run key; tests may inject an in-memory double.
    std::unique_ptr<ptd::Win32RunKeyBackend> autostart_backend_owned;
    ptd::AutostartBackend* autostart_backend = nullptr;
    std::unique_ptr<ptd::AutostartManager> autostart;

    // CORE-001: whether writing configuration is currently allowed. False for
    // unsupported-future-schema, failed malformed backup, or read failure.
    // Every save path (including shutdown) must respect it.
    bool persistence_allowed = true;

    // PERF-002: restartable single-shot debounce for high-frequency visual
    // edits. One authority shared by Trail and Click, never one timer each.
    std::unique_ptr<QTimer> save_debounce;
    int save_debounce_ms = 200;
    bool config_dirty = false;
    int save_invocations = 0;
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
    // CORE-001: the status-bearing load carries the write-protection
    // provenance; a protected source (future schema, failed malformed backup,
    // read failure) disables persistence for the whole Application lifetime.
    {
        const ptd::ConfigLoadResult load =
            ptd::ConfigStorage::load_from_file_result(d_->config_path);
        d_->app_config = load.config;
        d_->persistence_allowed = load.persistence_allowed;
    }
    d_->master_enabled = d_->app_config.master_enabled;
    d_->trail_config = d_->app_config.trail;
    d_->click_config = d_->app_config.click;
    d_->trail_effect.set_config(d_->trail_config);
    d_->click_effect.set_config(d_->click_config);

    // T-032: the Start with Windows side effect. The production backend is
    // the per-user Run key; a test may inject an in-memory double instead so
    // no routine run can modify the developer's real startup entries.
    if (!d_->autostart_backend) {
        d_->autostart_backend_owned = std::make_unique<ptd::Win32RunKeyBackend>();
        d_->autostart_backend = d_->autostart_backend_owned.get();
    }
    d_->autostart = std::make_unique<ptd::AutostartManager>(*d_->autostart_backend);
    // Reconcile BEFORE anything else can change the setting: a portable
    // ProTrail that was moved must repair its own registration on the next
    // launch, but only when the user actually enabled the feature.
    reconcile_autostart();

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

    // T-018R1: the supplied SingleInstance object is the ONE activation
    // message authority -- production activation uses the message
    // identifier registered by the owner, never a hidden constant here.
    // Smoke mode has no SingleInstance and never participates.
    if (qapp && d_->single_instance) {
        const UINT wm_activate = d_->single_instance->activation_message();
        if (wm_activate == 0) {
            app_log(4, "single_instance: activation message not registered; activation disabled");
        } else {
            if (!ChangeWindowMessageFilter(wm_activate, 1 /* MSGFLT_ADD */)) {
                app_log(3, "single_instance: ChangeWindowMessageFilter failed with error "
                        + std::to_string(GetLastError()));
            }
            d_->activation_filter = std::make_unique<InstanceActivationFilter>(
                wm_activate, [this](WPARAM wparam, LPARAM lparam) {
                    // HWND_BROADCAST delivers one copy per top-level window;
                    // the activation is processed once per request id. W2-004:
                    // a bounded recent-request set, not a single last-ID
                    // scalar, so interleaved copies (A, B, A) cannot re-admit
                    // an already-processed request.
                    const std::uint64_t request_id =
                        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(lparam)) << 32)
                        | static_cast<std::uint32_t>(wparam);
                    if (request_id == 0 || d_->activation_dedup.already_processed(request_id)) {
                        return;
                    }
                    // Mark BEFORE user-visible handling so a re-entrant copy
                    // cannot execute show_settings() twice.
                    d_->activation_dedup.mark_processed(request_id);

                    // T-018R3 test seam (deterministic harness gate only):
                    // models an owner whose event loop leaves before it can
                    // process a queued activation -- the request is received
                    // but never acknowledged, so the claimant must take over.
                    wchar_t seam[8]{};
                    const DWORD seam_len = GetEnvironmentVariableW(
                        L"PROTRAIL_TEST_ACTIVATION_DEFER_EXIT", seam, 8);
                    if (seam_len > 0 && seam_len < 8 && wcscmp(seam, L"1") == 0) {
                        app_log(2, "single_instance: test seam -- activation received without acknowledgement; owner exit pending");
                        request_exit();
                        return;
                    }
                    app_log(2, "ProTrail instance activation requested");
                    // 1) Process the activation for real.
                    show_main();
                    // 2) Acknowledge THIS exact request only after the
                    //    handling executed. A failed acknowledgement is
                    //    logged with the exact Win32 error; the claimant
                    //    then never reports Delivered.
                    if (d_->single_instance) {
                        if (!ptd::SingleInstance::acknowledge_request(request_id)) {
                            app_log(4, "single_instance: activation acknowledgement failed for request "
                                    + std::to_string(request_id));
                        }
                    }
                });
            qapp->installNativeEventFilter(d_->activation_filter.get());
        }
    }

    // T-032: the QUIET presence channel. A Windows autostart launch that
    // arrives while ProTrail already runs must hand its presence over and
    // exit -- it must NOT open, raise or activate the Main/Settings window
    // and must not steal focus. That is a different message, not a
    // suppressed activation, so the owner never evaluates a show at all.
    //
    // The dedup id is shared with the activation channel on purpose: both
    // carry the same per-request identity space, and a broadcast fan-out
    // copy must be processed exactly once on either channel.
    if (qapp && d_->single_instance) {
        const UINT wm_presence = d_->single_instance->presence_message();
        if (wm_presence == 0) {
            app_log(3, "single_instance: presence message not registered; quiet autostart hand-over unavailable");
        } else {
            if (!ChangeWindowMessageFilter(wm_presence, 1 /* MSGFLT_ADD */)) {
                app_log(3, "single_instance: ChangeWindowMessageFilter(presence) failed with error "
                        + std::to_string(GetLastError()));
            }
            d_->presence_filter = std::make_unique<InstanceActivationFilter>(
                wm_presence, [this](WPARAM wparam, LPARAM lparam) {
                    const std::uint64_t request_id =
                        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(lparam)) << 32)
                        | static_cast<std::uint32_t>(wparam);
                    if (request_id == 0) return;
                    // W2-004: a duplicate copy of an already-processed request
                    // must not repeat user-visible handling, but retrying the
                    // ACK for the SAME id is useful and safe -- the claimant
                    // may still be waiting, and the ACK can only represent a
                    // callback that already executed.
                    if (!d_->activation_dedup.already_processed(request_id)) {
                        d_->activation_dedup.mark_processed(request_id);
                        app_log(2, "single_instance: quiet presence request received; acknowledged without window activation");
                    }
                    // The ACK is what lets the claimant report Delivered, so
                    // it must happen here exactly like on the interactive
                    // channel -- only the window handling differs.
                    if (d_->single_instance) {
                        if (!ptd::SingleInstance::acknowledge_request(request_id)) {
                            app_log(4, "single_instance: quiet presence acknowledgement failed for request "
                                    + std::to_string(request_id));
                        }
                    }
                });
            qapp->installNativeEventFilter(d_->presence_filter.get());
        }
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
        // T-021: ONE gate for both effects (see trail_content_live /
        // click_content_live); the master toggle still outranks it.
        if (!d_->master_enabled) return false;
        return trail_content_live(now) || click_content_live(now);
    });
    d_->scheduler->set_frame_callback(
        [this](int64_t now_ns, double delta_time_s, ptd::FrameAction action) {
            (void)delta_time_s;
            if (!d_->overlay_mgr) return;

            if (action == ptd::FrameAction::RenderContent) {
                if (!d_->master_enabled) return;
                d_->scheduler->begin_frame();
                d_->cursor_history.prune(now_ns);
                // T-024 lost-Up recovery. The logical hold state is
                // reconciled against what the OS says the physical buttons
                // are actually doing, which is how a dropped raw-input Up,
                // a focus change or an alt-tab mid-drag is recovered --
                // NOT by an arbitrary maximum hold duration.
                //
                // This runs ONLY while hold records exist, and only on the
                // already-active render scheduler: there is no idle polling
                // timer anywhere in the product, and an idle ProTrail still
                // generates zero wakeups.
                if (d_->click_effect.has_hold_records()) {
                    d_->click_effect.reconcile_physical_buttons(
                        (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0,
                        (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0,
                        (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0);
                }
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

    // W2-005: the durable configuration is loaded EXACTLY ONCE, during
    // initialize(), which already populated app_config / master / trail /
    // click and the effects from ONE status-bearing snapshot. This used to
    // perform a SECOND independent load here, so snapshot A controlled
    // startup side effects (autostart reconcile) while snapshot B controlled
    // Settings/runtime/persistence -- a time-of-check/time-of-use split. The
    // initialize() snapshot is canonical for the whole lifetime; an explicit
    // runtime reload, if ever wanted, is a separate controller operation.

    // MVP 05: real SettingsWindow with Golden Default theme, live controls
    // for Trail/Click/Master, validated config publication.
    d_->settings = std::make_unique<ptd::ui::SettingsWindow>(
        d_->trail_config, d_->click_config, d_->master_enabled,
        d_->app_config.start_with_windows);
    d_->settings->set_developer_defaults_controller(true);
    // T-37: the compact Main Essentials surface is a VIEW over the same
    // canonical AppConfig. It is populated silently from the startup
    // snapshot; every user change flows through the SAME controller paths
    // Settings uses, and cross-surface sync is silent in both directions.
    // Startup behaviour is unchanged: the Main essentials surface is shown
    // by the same startup contract that already governs Settings.
    d_->main_window = std::make_unique<ptd::ui::MainWindow>();
    d_->main_window->set_from_app_config(d_->app_config);
    // Manual launch opens the compact product home. Autostart remains truly
    // tray-only: neither surface is shown or activated.
    if (ptd::startup_mode_requests_settings(d_->startup_mode)) {
        d_->main_window->show();
        app_log(2, "Main window shown (product home)");
    } else {
        app_log(2, "autostart: silent startup -- tray-only, no product window shown");
    }

    // MVP 09: system tray icon with canonical master state and hide-on-close restore
    d_->tray_icon = std::make_unique<ptd::ui::TrayIcon>(d_->master_enabled);
    d_->tray_icon->show();
    app_log(2, "System tray icon initialized");

    QObject::connect(d_->tray_icon.get(), &ptd::ui::TrayIcon::home_requested,
                     [this] { show_main(); });
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
                // T-37: silent cross-surface sync (Settings -> Main).
                if (d_->main_window) d_->main_window->set_from_app_config(d_->app_config);
                // PERF-002: preview immediate; durable write debounced.
                request_deferred_save();
                if (d_->master_enabled && d_->trail_config.enabled)
                    start_effect_rendering();
            });
    QObject::connect(d_->settings.get(), &ptd::ui::SettingsWindow::click_config_changed,
            [this](const ptd::ClickConfig& c) {
                d_->click_config = c;
                d_->app_config.click = c;
                d_->click_effect.set_config(c);
                // T-37: silent cross-surface sync (Settings -> Main).
                if (d_->main_window) d_->main_window->set_from_app_config(d_->app_config);
                // PERF-002: preview immediate; durable write debounced.
                request_deferred_save();
                if (d_->master_enabled && d_->click_config.enabled)
                    start_effect_rendering();
            });
    // T-015: presets publish ONE coherent (Trail, Click) pair -- update
    // both canonical states, both effects, save ONCE, wake rendering ONCE.
    // No intermediate half-states, no double save, no double render wake.
    // CORE-003 + W2-002: ONE bulk AppConfig transaction for Restore All /
    // apply_config / dev presets -- never a sequence of narrow partial
    // updates with intermediate persistence.
    QObject::connect(d_->settings.get(), &ptd::ui::SettingsWindow::app_config_applied,
            [this](const ptd::AppConfig& cfg) {
                apply_app_config_transaction(cfg);
                if (d_->main_window) d_->main_window->set_from_app_config(d_->app_config);
            });
    QObject::connect(d_->settings.get(), &ptd::ui::SettingsWindow::preset_applied,
            [this](const ptd::TrailConfig& t, const ptd::ClickConfig& c) {
                const bool was_trail_enabled = d_->trail_config.enabled;
                d_->trail_config = t;
                d_->click_config = c;
                d_->app_config.trail = t;
                d_->app_config.click = c;
                d_->trail_effect.set_config(t);
                d_->click_effect.set_config(c);
                save_config();
                // T-021: a preset publishes both configs wholesale, so a Trail
                // enable flip inside it carries the same history hygiene as
                // the checkbox -- the transition is detected here, not skipped.
                apply_trail_toggle_transition(was_trail_enabled, t.enabled);
                if (d_->main_window) d_->main_window->set_from_app_config(d_->app_config);
                if (d_->master_enabled && (t.enabled || c.enabled))
                    start_effect_rendering();
            });
    QObject::connect(d_->settings.get(), &ptd::ui::SettingsWindow::master_enabled_changed,
            [this](bool on) {
                set_master_enabled(on);
                if (d_->tray_icon) d_->tray_icon->set_master_enabled(on);
                save_config();
            });
    // T-032: Start with Windows. The registry side effect and the saved
    // preference are applied as one operation so the checkbox can never
    // claim a machine state the Run key does not have.
    QObject::connect(d_->settings.get(), &ptd::ui::SettingsWindow::start_with_windows_changed,
            [this](bool on) { apply_start_with_windows(on); });
    QObject::connect(d_->settings.get(), &ptd::ui::SettingsWindow::trail_enabled_changed,
            [this](bool on) {
                // T-021: the canonical child-toggle transition (config +
                // persistence + history hygiene) lives in the controller, not
                // in this wiring lambda.
                set_trail_enabled(on);
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

    // T-37: the Main Essentials surface publishes the SAME signals as
    // Settings; wire each to the identical controller path so one user
    // action yields exactly ONE coherent persisted update, and push the
    // canonical state silently back into BOTH surfaces afterwards. The
    // silent cross-surface push is what keeps them synchronized without a
    // callback loop.
    QObject::connect(d_->main_window.get(), &ptd::ui::MainWindow::trail_config_changed,
            [this](const ptd::TrailConfig& c) {
                const bool was_enabled = d_->trail_config.enabled;
                d_->trail_config = c;
                d_->app_config.trail = c;
                d_->trail_effect.set_config(c);
                apply_trail_toggle_transition(was_enabled, c.enabled);
                if (d_->settings) d_->settings->sync_from_app_config(d_->app_config);
                request_deferred_save();
                if (d_->master_enabled && d_->trail_config.enabled)
                    start_effect_rendering();
            });
    QObject::connect(d_->main_window.get(), &ptd::ui::MainWindow::click_config_changed,
            [this](const ptd::ClickConfig& c) {
                const bool was_enabled = d_->click_config.enabled;
                d_->click_config = c;
                d_->app_config.click = c;
                d_->click_effect.set_config(c);
                if (was_enabled && !c.enabled) d_->click_effect.clear();
                if (d_->settings) d_->settings->sync_from_app_config(d_->app_config);
                request_deferred_save();
                if (d_->master_enabled && d_->click_config.enabled)
                    start_effect_rendering();
            });
    QObject::connect(d_->main_window.get(), &ptd::ui::MainWindow::master_enabled_changed,
            [this](bool on) {
                set_master_enabled(on);
                if (d_->tray_icon) d_->tray_icon->set_master_enabled(on);
                if (d_->settings) d_->settings->set_master_enabled(on);
                d_->app_config.master_enabled = on;
                save_config();
            });
    QObject::connect(d_->main_window.get(), &ptd::ui::MainWindow::start_with_windows_changed,
            [this](bool on) { apply_start_with_windows(on); });
    QObject::connect(d_->main_window.get(), &ptd::ui::MainWindow::advanced_settings_requested,
            [this] { show_settings(); });
    QObject::connect(d_->main_window.get(), &ptd::ui::MainWindow::restore_defaults_requested,
            [this] {
                apply_app_config_transaction(ptd::release_defaults());
                if (d_->settings) d_->settings->sync_from_app_config(d_->app_config);
                if (d_->main_window) d_->main_window->set_from_app_config(d_->app_config);
            });

    // T-37 / Target D: the developer defaults action has exactly ONE owner in
    // the application. Both surfaces -- the Settings developer panel and the
    // compact developer-only Main action -- publish the same request and both
    // are routed to this one controller operation, so neither can ever be a
    // second defaults authority.
    QObject::connect(d_->settings.get(), &ptd::ui::SettingsWindow::set_current_as_release_defaults_requested,
            [this] { set_current_as_release_defaults(); });
    QObject::connect(d_->main_window.get(), &ptd::ui::MainWindow::set_current_as_release_defaults_requested,
            [this] { set_current_as_release_defaults(); });

    // T-018R1/T-018R2: the canonical windows exist and the activation
    // receiver is installed -- publish owner readiness, then consume any
    // activation that arrived BEFORE the receiver existed.

    if (d_->single_instance) {
        if (!d_->single_instance->mark_ready()) {
            // T-018R2: readiness publication is an essential protocol
            // operation. Fail closed through the controlled shutdown path
            // instead of running an instance whose activation can never be
            // delivered correctly. run() returns before the event loop;
            // main() still executes shutdown() (which begins the
            // single-instance shutdown handshake first).
            app_log(4, "single_instance: readiness publication failed (Win32 error "
                    + std::to_string(d_->single_instance->last_error())
                    + "); failing closed via controlled shutdown");
            return 1;
        }
        if (d_->single_instance->consume_startup_activation_request()) {
            app_log(2, "single_instance: startup activation consumed; activating Main window");
            show_main();
        }
    }

    return qapp ? qapp->exec() : 0;
}

void Application::on_mouse_activity(const ptd::CursorSample& newest) {
    // Phase R: master OFF -> no new trail rendering, no new bubbles, no
    // scheduler wake. Raw input remains registered (cheap to reverse).
    if (!d_->master_enabled) return;

    // B8: input starts/wakes rendering. Called on the GUI thread from the
    // raw-input WndProc path.
    d_->last_activity_ns = newest.timestamp_ns;

    // T-009 C3 + T-024: the controller routes EVERY normalized transition
    // into the click effect, not only Down. Before T-024 this called
    // on_button_down() and nothing else, which left the entire press-and-hold
    // lifecycle as dead code -- no Up could ever end a hold, and a drag could
    // never move one. The effect still checks enabled/trigger itself; the
    // spawn position is the one stored IN each sample, never a later cursor
    // position.
    bool spawned = false;
    switch (newest.action) {
        case ptd::ButtonAction::Down:
            spawned = d_->click_effect.on_button_down(newest);
            break;
        case ptd::ButtonAction::Up:
            // Ends an ACTIVE hold and spawns the release payoff at the Up
            // coordinate. A candidate that never reached the activation
            // threshold is simply discarded and spawns nothing: a fast
            // ordinary click must stay exactly one ordinary click.
            spawned = d_->click_effect.on_button_up(newest);
            break;
        case ptd::ButtonAction::None:
        default:
            // A held button that the user drags takes its aura along.
            d_->click_effect.on_cursor_moved(newest);
            break;
    }

    // C10/C11 + T-024: wake the shared active-only scheduler when either
    // effect can produce live content. Movement wakes only for the trail; a
    // spawned bubble wakes even with the trail disabled. has_live_content()
    // additionally covers an active hold and -- critically -- a CANDIDATE
    // still waiting to be promoted: with a short configured click duration
    // the Down bubble can expire before the activation threshold, and a
    // stationary press would otherwise never activate without the user
    // jiggling the mouse.
    if (d_->trail_config.enabled || spawned
        || d_->click_effect.has_live_content(newest.timestamp_ns)) {
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

// T-021: the single Trail live-content gate. Identical arithmetic to the
// frame/content path this replaced -- extracted so the scheduler content
// check, the tick and the lifecycle snapshot can never disagree.
bool Application::trail_content_live(int64_t now_ns) const {
    if (!d_->trail_config.enabled || d_->cursor_history.empty()) return false;
    const int64_t lifetime_ns =
        static_cast<int64_t>(d_->trail_config.lifetime_ms * 1'000'000.0);
    return (now_ns - d_->cursor_history.last().timestamp_ns) <= lifetime_ns;
}

bool Application::click_content_live(int64_t now_ns) const {
    return d_->click_effect.has_live_content(now_ns);
}

// T-021: canonical child Trail toggle. Persists trail.enabled exactly like
// every other config publication, then applies the lifecycle transition.
void Application::set_trail_enabled(bool on) {
    if (d_->trail_config.enabled == on) return;
    const bool was_enabled = d_->trail_config.enabled;
    d_->trail_config.enabled = on;
    d_->app_config.trail.enabled = on;
    d_->trail_effect.set_config(d_->trail_config);
    save_config();
    apply_trail_toggle_transition(was_enabled, on);
}

bool Application::trail_enabled() const {
    return d_->trail_config.enabled;
}

// T-021: both directions drop every sample the input layer collected while
// Trail was off, so no disabled-period movement can ever render as trail.
void Application::apply_trail_toggle_transition(bool was_enabled, bool now_enabled) {
    if (was_enabled == now_enabled) return;

    d_->cursor_history.clear();

    const int64_t now = ptd::now_ns();
    // Click bubble content is INDEPENDENT of the Trail toggle: a live bubble
    // keeps the shared scheduler awake (its frames draw no trail, because the
    // history is now empty).
    const bool click_live = d_->master_enabled && click_content_live(now);

    if (now_enabled) {
        // Fresh start: trail rendering resumes on the first movement received
        // after the re-enable (on_mouse_activity wakes the scheduler). A live
        // click bubble keeps its own wake.
        if (click_live) start_effect_rendering();
        return;
    }

    if (click_live) return;

    // Trail off with nothing else to animate: one transparent clear frame so
    // no trail stays frozen on screen, then sleep (the same contract the
    // master OFF path uses).
    if (d_->master_enabled && d_->overlay_mgr) {
        d_->overlay_mgr->render_frame(d_->trail_effect, d_->cursor_history,
                                      d_->trail_config, d_->click_effect,
                                      d_->click_config, now);
    }
    stop_effect_rendering();
}

void Application::record_cursor_sample(const ptd::CursorSample& sample) {
    // T-021: the complete input path MouseInput::record_sample implements --
    // push into the shared history, then notify the controller (B9). Kept in
    // lock-step with it, so a controller-level lifecycle regression exercises
    // the production lifecycle rather than a copy of it.
    d_->cursor_history.push(sample);
    on_mouse_activity(sample);
}

Application::LifecycleSnapshot Application::lifecycle_snapshot(int64_t now_ns) const {
    LifecycleSnapshot snapshot;
    snapshot.master_enabled = d_->master_enabled;
    snapshot.trail_enabled = d_->trail_config.enabled;
    snapshot.click_enabled = d_->click_config.enabled;
    snapshot.trail_history_size = d_->cursor_history.size();
    snapshot.trail_content_live = d_->master_enabled && trail_content_live(now_ns);
    snapshot.click_content_live = d_->master_enabled && click_content_live(now_ns);
    return snapshot;
}

const ptd::CursorHistory& Application::trail_history() const {
    return d_->cursor_history;
}

void Application::render_effect_tick() {
    if (!d_->scheduler) return;
    const int64_t now = ptd::now_ns();
    const bool live = trail_content_live(now) || click_content_live(now);
    d_->scheduler->on_timer_tick(d_->master_enabled && live);
}

void Application::stop_effect_rendering() {
    if (d_->scheduler) {
        d_->scheduler->force_idle();
    }
}

void Application::show_main() {
    if (!d_->main_window) return;
    if (d_->main_window->isMinimized()) {
        d_->main_window->showNormal();
    } else {
        d_->main_window->show();
    }
    d_->main_window->raise();
    d_->main_window->activateWindow();
    HWND hwnd = reinterpret_cast<HWND>(d_->main_window->winId());
    if (hwnd) SetForegroundWindow(hwnd);
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
    if (hwnd) SetForegroundWindow(hwnd);
}

void Application::set_current_as_release_defaults() {
    // Flush the single pending persistence transaction before taking the
    // canonical full snapshot. No UI reconstruction is involved.
    flush_pending_save();
    // The canonical FULL configuration is Application::app_config -- the
    // complete persisted semantic state, not a reconstruction from whichever
    // controls a window happens to expose. A field added to AppConfig later
    // is therefore captured automatically, because the serializer, not a UI
    // snapshot function, defines what "every setting" means.
    const ptd::AppConfig snapshot = ptd::AppConfig::validated(d_->app_config);
    const ptd::PromoteResult result = ptd::promote_to_release_defaults(snapshot);
    // The result is reported on the Settings developer panel AND logged, so a
    // promotion triggered from the compact Main action is never silent when
    // the developer panel happens to be closed.
    app_log(result.success ? 2 : 4,
            std::string("release_defaults: ") + result.message);
    if (d_->settings) {
        d_->settings->show_dev_status(QString::fromStdString(result.message));
    }
}

void Application::set_startup_mode(ptd::StartupMode mode) {
    d_->startup_mode = mode;
}

void Application::set_autostart_backend_for_tests(ptd::AutostartBackend* backend) {
    d_->autostart_backend = backend;
}

// T-032: register/remove ProTrail's own Run entry and persist the matching
// preference in ONE operation, so what the user sees in Settings is what the
// machine will actually do at the next sign-in.
void Application::apply_start_with_windows(bool on) {
    if (d_->app_config.start_with_windows == on && d_->autostart) {
        // Already consistent: still reconcile when ON so a moved executable
        // is repaired rather than silently left pointing at a dead path.
        if (on) reconcile_autostart();
        return;
    }
    d_->app_config.start_with_windows = on;

    if (d_->autostart) {
        const std::wstring exe = ptd::current_executable_path();
        const bool ok = on ? d_->autostart->enable(exe) : d_->autostart->disable();
        if (!ok) {
            // The PREFERENCE is still saved: it is the user's recorded
            // choice, and the next start reconciles the machine toward it.
            // The failure is loud, never silent.
            app_log(4, on ? "autostart: could not register ProTrail in the "
                            "per-user Run key; the preference is saved and "
                            "will be reconciled on the next start"
                          : "autostart: could not remove the ProTrail entry "
                            "from the per-user Run key; the preference is "
                            "saved and will be reconciled on the next start");
        } else {
            app_log(2, on ? "autostart: ProTrail registered to start with Windows"
                          : "autostart: ProTrail removed from Windows startup");
        }
    }
    save_config();
}

// CORE-003 + W2-002: the single owner for a whole-AppConfig state change.
// One logical transition: old state -> validated new state, with the Trail
// history-hygiene and Click clear lifecycle applied exactly once, the
// Start-with-Windows desired state reconciled through CORE-002's bidirectional
// path, and ONE persistence commit for the final complete configuration.
void Application::apply_app_config_transaction(const ptd::AppConfig& cfg) {
    const ptd::AppConfig next = ptd::AppConfig::validated(cfg);

    const bool was_master = d_->master_enabled;
    const bool was_trail_enabled = d_->trail_config.enabled;
    const bool was_click_enabled = d_->click_config.enabled;
    const bool was_start_with_windows = d_->app_config.start_with_windows;

    d_->master_enabled = next.master_enabled;
    d_->trail_config = next.trail;
    d_->click_config = next.click;
    d_->app_config = next;
    d_->trail_effect.set_config(d_->trail_config);
    d_->click_effect.set_config(d_->click_config);
    if (d_->tray_icon) d_->tray_icon->set_master_enabled(d_->master_enabled);

    // Master edge: clearing rules, exactly once.
    if (was_master != d_->master_enabled) {
        if (d_->master_enabled) {
            app_log(2, "ProTrail master enable: ON");
        } else {
            app_log(2, "ProTrail master enable: OFF (clearing effects)");
        }
        d_->cursor_history.clear();
        d_->click_effect.clear();
    }

    // Trail edge: the canonical CORE-003 history hygiene, exactly once.
    apply_trail_toggle_transition(was_trail_enabled, d_->trail_config.enabled);

    // Click edge: W2-002 requires bubbles/holds/wake cleared immediately on a
    // Click ON->OFF transition, not merely on the next set_config.
    const bool was_click_on = was_click_enabled && was_master;
    const bool now_click_on = d_->click_config.enabled && d_->master_enabled;
    if (was_click_on && !now_click_on) {
        d_->click_effect.clear();
    }

    // Start-with-Windows: reconcile the DESIRED state through CORE-002's
    // bidirectional path (registry side effect + preference are one op).
    if (was_start_with_windows != d_->app_config.start_with_windows) {
        if (d_->autostart) {
            const std::wstring exe = ptd::current_executable_path();
            const bool desired = d_->app_config.start_with_windows;
            if (!d_->autostart->reconcile_desired(desired, exe)) {
                app_log(4, desired
                                ? "autostart: could not register ProTrail in the per-user Run key"
                                : "autostart: could not remove the ProTrail entry from the per-user Run key");
            }
        }
    }

    // ONE persistence commit for the final complete configuration.
    save_config();

    // Wake the scheduler only as the FINAL state requires.
    const bool want_render = d_->master_enabled
        && (d_->trail_config.enabled || d_->click_config.enabled
            || d_->click_effect.has_live_content(ptd::now_ns()));
    if (want_render) {
        start_effect_rendering();
    } else {
        if (d_->master_enabled && d_->overlay_mgr) {
            // One transparent clear frame when the final state has nothing live.
            d_->overlay_mgr->render_frame(d_->trail_effect, d_->cursor_history,
                                          d_->trail_config, d_->click_effect,
                                          d_->click_config, ptd::now_ns());
        }
        stop_effect_rendering();
    }
}

void Application::reconcile_autostart() {
    if (!d_->autostart) return;
    // CORE-002: reconcile the DESIRED state in BOTH directions. When the
    // preference is ON the owned Run value follows the current executable;
    // when it is OFF the owned value is idempotently removed, so a transient
    // disable failure is repaired on the next start instead of being gated
    // away by that same OFF preference. The executable path is needed only
    // for the ON branch, so an unresolvable path never blocks OFF cleanup.
    const bool desired = d_->app_config.start_with_windows;
    const std::wstring exe = ptd::current_executable_path();
    if (d_->autostart->reconcile_desired(desired, exe)) {
        app_log(2, desired
                        ? "autostart: registered command reconciled with the current executable"
                        : "autostart: ProTrail entry confirmed absent from Windows startup");
    } else {
        app_log(4, desired
                        ? "autostart: registered command could NOT be reconciled"
                        : "autostart: ProTrail entry could NOT be removed from Windows startup");
    }
}

void Application::set_single_instance(ptd::SingleInstance* si) {
    // T-018R1: stores the non-owning authority used across the whole
    // production lifecycle: activation message id (filter), readiness
    // publication (mark_ready) and startup activation consumption. nullptr
    // in smoke mode.
    d_->single_instance = si;
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
    // T-018R2: begin the single-instance shutdown handshake BEFORE the
    // activation receiver is removed -- readiness is revoked and the
    // takeover beacon raised while the receiver still exists, so no
    // claimant can interpret this owner as activation-ready from here on.
    if (d_->single_instance) {
        d_->single_instance->begin_shutdown();
    }
    // PERF-002: flush any pending debounced visual edit synchronously so a
    // final slider value is never discarded on exit.
    flush_pending_save();
    save_config();
    if (d_->activation_filter || d_->presence_filter) {
        QApplication* qapp_owner = d_->qt ? d_->qt.get()
                                         : qobject_cast<QApplication*>(QApplication::instance());
        if (qapp_owner) {
            if (d_->activation_filter) {
                qapp_owner->removeNativeEventFilter(d_->activation_filter.get());
            }
            if (d_->presence_filter) {
                qapp_owner->removeNativeEventFilter(d_->presence_filter.get());
            }
        }
        d_->activation_filter.reset();
        d_->presence_filter.reset();
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
    d_->main_window.reset();
    if (d_->qt) {
        d_->qt.reset();
    }
    app_log(2, "ProTrail shutdown complete");
}

bool Application::persistence_allowed() const {
    return d_->persistence_allowed;
}

bool Application::save_config_for_tests() {
    return save_config();
}

ptd::ui::MainWindow* Application::main_window_for_tests() const {
    return d_->main_window.get();
}

ptd::ui::SettingsWindow* Application::settings_window_for_tests() const {
    return d_->settings.get();
}

ptd::ui::TrayIcon* Application::tray_icon_for_tests() const {
    return d_->tray_icon.get();
}

// PERF-002: schedule a single coalesced durable write. The visual/effect state
// is already updated; only the filesystem write is deferred. Discrete and
// lifecycle operations call save_config() directly and remain immediate.
void Application::request_deferred_save() {
    if (!d_->save_debounce) {
        d_->save_debounce = std::make_unique<QTimer>();
        d_->save_debounce->setSingleShot(true);
        d_->save_debounce->setTimerType(Qt::CoarseTimer);
        QObject::connect(d_->save_debounce.get(), &QTimer::timeout,
                         [this] { flush_pending_save(); });
    }
    d_->config_dirty = true;
    d_->save_debounce->start(d_->save_debounce_ms);
}

void Application::flush_pending_save() {
    if (d_->save_debounce) d_->save_debounce->stop();
    if (!d_->config_dirty) return;
    if (save_config()) {
        d_->config_dirty = false;
    }
}

void Application::set_save_debounce_ms_for_tests(int ms) {
    d_->save_debounce_ms = ms < 0 ? 0 : ms;
}

int Application::save_invocation_count_for_tests() const {
    return d_->save_invocations;
}

void Application::reset_save_invocation_count_for_tests() {
    d_->save_invocations = 0;
}

bool Application::config_dirty_for_tests() const {
    return d_->config_dirty;
}
bool Application::save_config() {
    // CORE-001: a protected source (unsupported future schema, malformed file
    // whose backup failed, or an unreadable file) must never be overwritten --
    // not by a Settings change, and not by the shutdown save. A blocked save
    // is explicit in the log and touches nothing.
    if (!d_->persistence_allowed) {
        app_log(4, "config: persistence is disabled for this protected "
                   "configuration source; the save was blocked and the "
                   "original file is untouched");
        return false;
    }
    d_->app_config.master_enabled = d_->master_enabled;
    d_->app_config.trail = d_->trail_config;
    d_->app_config.click = d_->click_config;
    // T-029: the write result is CONSUMED, never dropped. Every settings
    // change and the shutdown save route through here, so one explicit
    // settings-context error is the difference between "the user believes
    // their change is safe" and a silent loss on the next start.
    ++d_->save_invocations;
    if (!ptd::ConfigStorage::save_to_file(d_->app_config, d_->config_path)) {
        app_log(4, "config: settings change NOT persisted -- the running "
                   "session keeps the new values but they will be lost on "
                   "restart");
        return false;
    }
    // The durable bytes now match the canonical state exactly, so nothing is
    // outstanding any more. Without this, a visual edit burst left the
    // debounce armed, and the discrete operation that followed (Restore
    // Defaults, a dev preset, a master toggle) wrote the SAME state a second
    // time when the debounce expired -- one logical publication, two writes.
    // The pending timer, if any, now finds nothing dirty and does nothing.
    d_->config_dirty = false;
    return true;
}
