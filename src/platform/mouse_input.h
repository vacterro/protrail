#pragma once

#include "monotonic_clock.h"

#include "../core/cursor_sample.h"
#include "../core/cursor_history.h"

#include <functional>
#include <cstdint>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace ptd {

// Dedicated raw-input listener for mouse activity. Owns its own hidden
// message-only HWND (HWND_MESSAGE parent) registered with RIDEV_INPUTSINK
// so input collection is independent from the transparent overlay window and
// keeps working while another application is foreground.
//
// Event-loop ownership (T-007R A3, replaces the MVP 02 8 ms pump contract):
//   The raw-input HWND is created on the same thread that runs the Qt
//   event loop. Qt's win32 event dispatcher dispatches ALL messages of its
//   owning thread (including message-only windows) via its internal
//   PeekMessage/GetMessage loop, so WM_INPUT arrives at this WndProc
//   naturally: Qt (native event filter sees it first) -> DefWindowProc ->
//   wnd_proc_static -> handle_raw_input. There is NO polling timer and NO
//   manual PeekMessage pump anymore. Verified by the
//   protrail_input_dispatch test (SendInput-automated: no pump() call).
//
// Flow (T-007R contract):
//   WM_INPUT (dispatched by Qt) -> parse ALL button flags -> GetCursorPos
//   (authoritative position for the packet) -> CursorSample(s) ->
//   CursorHistory.
//
// A RAWINPUT packet can carry more than one button transition; every
// transition found is emitted (A5 fix). The vertical wheel travels a separate
// typed path (T-80): it is never folded into the activity sample stream and
// never wakes the renderer.
//
// Renderer independence (B9): MouseInput never renders and never touches
// effects. It exposes an optional activity callback: after each push into
// CursorHistory the callback fires with the newest sample. Application
// uses it to wake the active-only trail render scheduler. If no callback
// is set, MouseInput only fills the history.
//
// No per-event disk logging in normal operation (performance rule); the
// optional PROTRAIL_INPUT_DIAG=1 mode emits one bounded summary line per
// second.
//
// Non-copyable; destroy() is idempotent.
class MouseInput {
public:
    // MVP 08 Phase 0 truth: a non-interactive desktop where RIDEV_INPUTSINK
    // never delivers (RDP fallback, locked screen, Session 0) is not a
    // functional PASS; the caller must label it ENVIRONMENT_NOT_VERIFIABLE.
    //
    // T-013R1 truth contract: the verdict requires INDEPENDENT injection
    // evidence. Absence of the tested event proves nothing by itself.
    enum class RawInputVerdict {
        // Expected WM_INPUT activity arrived after a fully-injected burst.
        Verified,
        // Injection reported full success (window valid, registration ok,
        // SendInput injected every requested event, bounded event loop ran)
        // but no expected activity arrived: a real raw-input delivery
        // regression. The test must FAIL, never skip.
        InjectionNotDelivered,
        // Injection itself could not be attempted or completed (raw-input
        // window/registration unavailable, SendInput refused or injected
        // fewer events than requested). Environment unavailability is only
        // ever derived from injection evidence, never from missing input.
        EnvironmentNotVerifiable,
    };

    // B9: generic activity notification. Receives the newest normalized
    // sample after it has been stored in CursorHistory.
    using ActivityCallback = std::function<void(const CursorSample&)>;

    // T-80: independent wheel notification, deliberately NOT an activity
    // sample. A wheel packet never feeds CursorHistory (it is not movement)
    // and never wakes the trail renderer; it only carries the signed vertical
    // delta so the audio engine can accumulate it into bounded events.
    // Positive = wheel up. Delivered on the GUI thread from the raw-input
    // WndProc, so the receiver must be non-blocking.
    using WheelCallback = std::function<void(int64_t timestamp_ns, int vertical_delta)>;

    // T-120: horizontal wheel notification (RI_MOUSE_HWHEEL). Positive = right, negative = left.
    using HorizontalWheelCallback = std::function<void(int64_t timestamp_ns, int horizontal_delta)>;

    // T-120: auxiliary mouse button notification (RI_MOUSE_BUTTON_4_DOWN/UP, RI_MOUSE_BUTTON_5_DOWN/UP).
    // Carries the canonical event id ("mouse.x1_down", "mouse.x1_up", "mouse.x2_down", "mouse.x2_up").
    using AuxButtonCallback = std::function<void(int64_t timestamp_ns, const char* canonical_event_id)>;

    MouseInput() = default;
    ~MouseInput();

    MouseInput(const MouseInput&) = delete;
    MouseInput& operator=(const MouseInput&) = delete;

    bool create(HINSTANCE instance, CursorHistory* history);
    void destroy();

    bool valid() const { return hwnd_ != nullptr; }
    // True when RIDEV_INPUTSINK registration succeeded (independent
    // precondition evidence for the input-dispatch verdict).
    bool registered() const { return registered_; }

    // B9: set/clear the activity callback (must be called on the same
    // thread that owns the raw-input window).
    void set_activity_callback(ActivityCallback cb) { activity_ = std::move(cb); }

    // T-80: set/clear the wheel callback (same thread rule as above).
    void set_wheel_callback(WheelCallback cb) { wheel_ = std::move(cb); }

    // T-120: set/clear horizontal wheel and auxiliary button callbacks.
    void set_horizontal_wheel_callback(HorizontalWheelCallback cb) { hwheel_ = std::move(cb); }
    void set_aux_button_callback(AuxButtonCallback cb) { aux_button_ = std::move(cb); }

    // Test/observability counters. Not diagnostics-logged.
    uint64_t movement_count() const { return movement_count_; }
    uint64_t button_count() const { return button_count_; }
    // T-80: packets that carried a vertical wheel delta, and deltas delivered.
    uint64_t wheel_count() const { return wheel_count_; }
    uint64_t wheel_delta_count() const { return wheel_delta_count_; }
    uint64_t hwheel_count() const { return hwheel_count_; }
    uint64_t aux_button_count() const { return aux_button_count_; }
    // Real coalescing count, taken from CursorHistory::PushResult -- not
    // inferred from history size (A4 fix).
    uint64_t coalesced_count() const { return coalesced_count_; }
    uint64_t transition_count() const { return transition_count_; }

    // Pure classifier for the input-dispatch verification contract. The
    // caller supplies independent evidence: what it requested from SendInput,
    // what SendInput actually injected, and whether the raw-input sink was
    // usable. Foreground-window existence is deliberately NOT an input.
    static RawInputVerdict classify_input_injection(uint32_t requested_events,
                                                    uint32_t injected_events,
                                                    bool input_window_valid,
                                                    bool registration_succeeded,
                                                    bool any_expected_activity);

    // Pure WM_INPUT delivery decision. RIM_INPUT (foreground) requires the
    // system cleanup that DefWindowProc performs; RIM_INPUTSINK (background,
    // the RIDEV_INPUTSINK path) does not. Pure so the foreground/background
    // dispatch split can be proven without a real foreground window.
    static bool raw_input_needs_default_cleanup(WPARAM wp);

private:
    static LRESULT CALLBACK wnd_proc_static(HWND, UINT, WPARAM, LPARAM);
    static MouseInput* s_instance_;

    void handle_raw_input(LPARAM lp);
    void record_sample(int64_t now_ns, int x, int y, MouseButton button, ButtonAction action);
    void diag_tick(int64_t now_ns);

    HWND hwnd_ = nullptr;
    CursorHistory* history_ = nullptr;
    bool registered_ = false;
    ActivityCallback activity_;
    WheelCallback wheel_;
    HorizontalWheelCallback hwheel_;
    AuxButtonCallback aux_button_;

    // Diagnostic counters (always accumulated in memory; cheap).
    uint64_t movement_count_ = 0;
    uint64_t button_count_ = 0;
    uint64_t wheel_count_ = 0;
    uint64_t wheel_delta_count_ = 0;
    uint64_t hwheel_count_ = 0;
    uint64_t aux_button_count_ = 0;
    uint64_t coalesced_count_ = 0;
    uint64_t transition_count_ = 0;  // A5: every transition preserved in history

    // PROTRAIL_INPUT_DIAG=1: one rate-limited summary line per second.
    bool diag_log_ = false;
    int64_t diag_window_start_ns_ = 0;
    uint64_t diag_events_ = 0;
    uint64_t diag_buttons_ = 0;
    int32_t diag_last_x_ = 0;
    int32_t diag_last_y_ = 0;
    char diag_last_button_ = '-';
    char diag_last_action_ = '-';
};

} // namespace ptd
