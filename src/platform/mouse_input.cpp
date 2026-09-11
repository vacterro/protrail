#include "mouse_input.h"

#include "button_flags.h"
#include "timestamp.h"
#include "../core/log.h"

#include <cstdlib>
#include <cstring>

namespace ptd {

namespace {

constexpr wchar_t kClassName[] = L"ProTrailMouseInput";

// Cached QPC frequency. QueryPerformanceFrequency cannot fail on Windows
// XP+ (it always writes a nonzero value), but we still check defensively
// (T-007R A1): a failed/zero frequency yields the monotonic raw-tick
// passthrough from ticks_to_ns instead of a division by zero.
int64_t qpc_freq() {
    static int64_t freq = [] {
        LARGE_INTEGER f{};
        QueryPerformanceCounter(&f); // ensure QPC is usable on this machine
        if (!QueryPerformanceFrequency(&f)) return int64_t{0};
        return f.QuadPart;
    }();
    return freq;
}

} // namespace

int64_t now_ns() {
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    // Overflow-safe split conversion (seconds + remainder). The previous
    // c.QuadPart * 1e9 / freq form overflowed after ~922 s of QPC counter
    // time at 10 MHz. See timestamp.h for the proof.
    return ticks_to_ns(c.QuadPart, qpc_freq());
}

MouseInput* MouseInput::s_instance_ = nullptr;

MouseInput::~MouseInput() {
    destroy();
}

LRESULT CALLBACK MouseInput::wnd_proc_static(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // Single instance per process; GWLP_USERDATA wiring is unnecessary.
    auto* self = s_instance_;
    if (msg == WM_INPUT && self) {
        self->handle_raw_input(lp);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool MouseInput::create(HINSTANCE instance, CursorHistory* history) {
    if (!history) return false;
    history_ = history;

    // PROTRAIL_INPUT_DIAG=1: opt-in bounded per-second summary logging.
    wchar_t buf[8]{};
    const DWORD len = GetEnvironmentVariableW(L"PROTRAIL_INPUT_DIAG", buf, 8);
    diag_log_ = len > 0 && len < 8 && wcscmp(buf, L"1") == 0;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &MouseInput::wnd_proc_static;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        ptd::log_write(ptd::LogLevel::Error, "mouse_input: RegisterClassExW failed");
        return false;
    }

    // Message-only window: never shown, never hit-testable, receives only
    // posted/registerable messages such as WM_INPUT. Owned here so input
    // collection has no coupling to the overlay renderer. Created on the
    // calling (GUI/Qt) thread; Qt's win32 dispatcher delivers WM_INPUT to
    // its WndProc with no pumping help (see class comment / A3).
    hwnd_ = CreateWindowExW(0, kClassName, L"ProTrailMouseInput", WS_OVERLAPPED,
                            0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    if (!hwnd_) {
        ptd::log_write(ptd::LogLevel::Error, "mouse_input: CreateWindowExW failed");
        return false;
    }
    s_instance_ = this;

    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01; // HID_USAGE_PAGE_GENERIC
    rid.usUsage = 0x02;     // HID_USAGE_GENERIC_MOUSE
    rid.dwFlags = RIDEV_INPUTSINK; // receive input even when not foreground
    rid.hwndTarget = hwnd_;
    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        ptd::log_write(ptd::LogLevel::Error, "mouse_input: RegisterRawInputDevices failed");
        destroy();
        return false;
    }
    registered_ = true;

    ptd::log_write(ptd::LogLevel::Info,
                   "mouse_input: raw input registered (RIDEV_INPUTSINK, message-only window, Qt-dispatched)");
    return true;
}

void MouseInput::destroy() {
    if (registered_) {
        RAWINPUTDEVICE rid{};
        rid.usUsagePage = 0x01;
        rid.usUsage = 0x02;
        rid.dwFlags = RIDEV_REMOVE;
        rid.hwndTarget = nullptr;
        RegisterRawInputDevices(&rid, 1, sizeof(rid));
        registered_ = false;
        ptd::log_write(ptd::LogLevel::Info, "mouse_input: raw input unregistered");
    }
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (s_instance_ == this) s_instance_ = nullptr;
    activity_ = nullptr;
}

MouseInput::RawInputVerdict MouseInput::classify_input_injection(
    uint32_t requested_events,
    uint32_t injected_events,
    bool input_window_valid,
    bool registration_succeeded,
    bool any_expected_activity) {
    // Environment unavailability comes ONLY from independent setup/injection
    // evidence: an unusable sink or a SendInput that injected fewer events
    // than requested. Missing WM_INPUT alone is never environment evidence.
    if (!input_window_valid || !registration_succeeded) {
        return RawInputVerdict::EnvironmentNotVerifiable;
    }
    if (injected_events < requested_events) {
        return RawInputVerdict::EnvironmentNotVerifiable;
    }
    return any_expected_activity ? RawInputVerdict::Verified
                                 : RawInputVerdict::InjectionNotDelivered;
}

void MouseInput::handle_raw_input(LPARAM lp) {
    UINT size = 0;
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, nullptr, &size,
                        sizeof(RAWINPUTHEADER)) != 0 || size == 0 || size > 512) {
        return;
    }
    BYTE buf[512];
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, buf, &size,
                        sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1)) {
        return;
    }
    const auto* raw = reinterpret_cast<const RAWINPUT*>(buf);
    if (raw->header.dwType != RIM_TYPEMOUSE) return;
    const RAWMOUSE& m = raw->data.mouse;

    const int64_t ts = now_ns();
    ++diag_events_;

    // A5: one packet may carry several button transitions (e.g. left-down
    // and right-up together). Extract and record ALL of them; none may be
    // dropped because another flag matched first. Each transition uses the
    // same authoritative GetCursorPos coordinate for this packet, taken
    // once per packet when any transition is present.
    ButtonTransition transitions[kMaxButtonTransitions];
    const int n = extract_button_transitions(m.usButtonFlags, transitions);

    POINT p{};
    bool have_pos = false;
    for (int i = 0; i < n; ++i) {
        if (!have_pos) {
            GetCursorPos(&p);
            have_pos = true;
        }
        ++button_count_;
        ++transition_count_;
        record_sample(ts, p.x, p.y, transitions[i].button, transitions[i].action);
    }
    if (n > 0) return; // wheel-only packets fall through to movement check

    // Movement activity: any relative/absolute displacement (wheel excluded).
    const bool moved = (m.lLastX != 0 || m.lLastY != 0);
    if (moved) {
        POINT mp{};
        GetCursorPos(&mp);
        ++movement_count_;
        record_sample(ts, mp.x, mp.y, MouseButton::None, ButtonAction::None);
    }
    // Wheel / other flags: intentionally ignored in MVP 02 (extensible later).
}

void MouseInput::record_sample(int64_t now_ns_, int x, int y, MouseButton button, ButtonAction action) {
    CursorSample s{};
    s.timestamp_ns = now_ns_;
    s.x = x;
    s.y = y;
    s.button = button;
    s.action = action;

    // A4: explicit push outcome from the history itself. The old
    // size-before/size-after inference counted a genuinely-new sample as
    // "coalesced" whenever the history was full and the push trimmed the
    // front (size unchanged).
    const CursorHistory::PushResult result = history_->push(s);
    if (result == CursorHistory::PushResult::Coalesced) {
        ++coalesced_count_;
    }

    diag_last_x_ = x;
    diag_last_y_ = y;
    switch (button) {
        case MouseButton::Left:   diag_last_button_ = 'L'; break;
        case MouseButton::Right:  diag_last_button_ = 'R'; break;
        case MouseButton::Middle: diag_last_button_ = 'M'; break;
        default:                  diag_last_button_ = '-'; break;
    }
    switch (action) {
        case ButtonAction::Down: diag_last_action_ = 'd'; break;
        case ButtonAction::Up:   diag_last_action_ = 'u'; break;
        default:                 diag_last_action_ = '-'; break;
    }
    if (action != ButtonAction::None) ++diag_buttons_;
    if (diag_log_) diag_tick(now_ns_);

    // B9: notify the controller AFTER the sample is durably in the
    // history. Fire for movement AND stationary button transitions -- both
    // can make trail content visible.
    if (activity_) activity_(s);
}

// Rate-limited summary: at most one log line per second, only when
// PROTRAIL_INPUT_DIAG=1. Never per-event.
void MouseInput::diag_tick(int64_t ts) {
    if (diag_window_start_ns_ == 0) {
        diag_window_start_ns_ = ts;
        diag_events_ = 0;
        diag_buttons_ = 0;
        return;
    }
    constexpr int64_t kNsPerSec = 1'000'000'000;
    if (ts - diag_window_start_ns_ >= kNsPerSec) {
        char line[128];
        const int n = _snprintf_s(line, sizeof(line), _TRUNCATE,
                                  "mouse_input diag: %llu events/s %llu btn/s last=(%d,%d) lastbtn=%c%c",
                                  static_cast<unsigned long long>(diag_events_),
                                  static_cast<unsigned long long>(diag_buttons_),
                                  diag_last_x_, diag_last_y_,
                                  diag_last_button_, diag_last_action_);
        if (n > 0) ptd::log_write(ptd::LogLevel::Info, line);
        diag_window_start_ns_ = ts;
        diag_events_ = 0;
        diag_buttons_ = 0;
    }
}

} // namespace ptd
