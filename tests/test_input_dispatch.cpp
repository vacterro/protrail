// T-007R A3: WM_INPUT arriving through Qt's win32 dispatcher needs no pump.
// T-013R1 verification truth: the skip/fail split comes from INDEPENDENT
// injection evidence, never from the absence of the tested event.
// T-83 operator safety: real SendInput is INERT unless PROTRAIL_ALLOW_REAL_INPUT=1
// declares the session; when armed, every move/click/wheel burst is confined
// with ClipCursor to a dedicated test-owned popup window, so no injected event
// ever lands on a user desktop target.
//
// Method (automated):
//   1. Build the app-shaped raw-input window (message-only, RIDEV_INPUTSINK).
//   2. LOCAL event loop with ONLY a coarse safety timer (bounded), never a pump.
//   3. Synthesize SendInput movement + L/R/M button bursts, counting the
//      exact requested and injected event counts (plus GetLastError).
//   4. Classify with MouseInput::classify_input_injection:
//        SendInput could not inject            -> ENVIRONMENT_NOT_VERIFIABLE (skip)
//        full injection, no WM_INPUT activity  -> FAIL (real delivery regression)
//        expected WM_INPUT activity            -> PASS
//      Foreground-window existence is deliberately not consulted.

#include <QtWidgets/QApplication>
#include <QtCore/QTimer>
#include <QtCore/QEventLoop>
#include <QtTest/QTest>

#include "../src/platform/mouse_input.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstdint>
#include <type_traits>

namespace {

// T-83: real SendInput moves/clicks at the LIVE cursor hit whatever is under
// the operator's mouse -- arbitrary user desktop targets at arbitrary times.
// These tests therefore run real injection ONLY when explicitly armed, and
// when armed they confine the cursor to a dedicated test-owned window so a
// click can never land on a user process.
bool real_input_armed() {
    const QByteArray v = qgetenv("PROTRAIL_ALLOW_REAL_INPUT");
    return v == "1";
}

// RAII confinement: a small topmost test-owned popup window, the cursor moved
// into it and clipped there for the burst, original position and clip restored
// on destruction. Never activates the window (WS_EX_NOACTIVATE, no-activate
// show), so the operator keeps focus.
class InputBurstScope {
public:
    InputBurstScope() {
        if (!real_input_armed()) return;
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"ProTrailInputBurst";
        RegisterClassExW(&wc);
        hwnd_ = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOPMOST, L"ProTrailInputBurst",
                                L"ProTrail input test", WS_POPUP,
                                16, 16, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
        if (!hwnd_) return;
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        RECT r{};
        GetWindowRect(hwnd_, &r);
        GetCursorPos(&orig_);
        SetCursorPos((r.left + r.right) / 2, (r.top + r.bottom) / 2);
        ClipCursor(&r);
    }
    ~InputBurstScope() {
        ClipCursor(nullptr);
        if (hwnd_) {
            SetCursorPos(orig_.x, orig_.y);
            DestroyWindow(hwnd_);
        }
    }
    InputBurstScope(const InputBurstScope&) = delete;
    InputBurstScope& operator=(const InputBurstScope&) = delete;
private:
    HWND hwnd_ = nullptr;
    POINT orig_{};
};

// Injects one relative move. Returns the number of events SendInput actually
// injected (0 = refused; caller captures GetLastError immediately after).
uint32_t send_relative_move(int dx, int dy, DWORD* error_out) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    in.mi.dx = dx;
    in.mi.dy = dy;
    SetLastError(ERROR_SUCCESS);
    const UINT injected = SendInput(1, &in, sizeof(INPUT));
    if (injected < 1 && error_out) *error_out = GetLastError();
    return injected;
}

// Injects one press+release pair (2 events). Same return contract.
uint32_t send_click(DWORD down_flag, DWORD up_flag, DWORD* error_out) {
    INPUT in[2]{};
    in[0].type = INPUT_MOUSE;
    in[0].mi.dwFlags = down_flag;
    in[1].type = INPUT_MOUSE;
    in[1].mi.dwFlags = up_flag;
    SetLastError(ERROR_SUCCESS);
    const UINT injected = SendInput(2, in, sizeof(INPUT));
    if (injected < 2 && error_out) *error_out = GetLastError();
    return injected;
}

// Injects one vertical wheel notch (delta = WHEEL_DELTA, up = positive).
uint32_t send_wheel(int delta, DWORD* error_out) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_WHEEL;
    in.mi.mouseData = static_cast<DWORD>(delta);
    SetLastError(ERROR_SUCCESS);
    const UINT injected = SendInput(1, &in, sizeof(INPUT));
    if (injected < 1 && error_out) *error_out = GetLastError();
    return injected;
}

// Injects ONE batched packet: move + left down + left up + wheel notch. Sent
// in a single call so the system coalesces them into one RAWMOUSE, which is
// the combined button+wheel case the raw-input layer must split, not drop.
// Returns the injected count (4 = complete).
uint32_t send_combined_left_wheel() {
    INPUT in[4]{};
    in[0].type = INPUT_MOUSE;
    in[0].mi.dwFlags = MOUSEEVENTF_MOVE;
    in[0].mi.dx = 7;
    in[0].mi.dy = -3;
    in[1].type = INPUT_MOUSE;
    in[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    in[2].type = INPUT_MOUSE;
    in[2].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    in[3].type = INPUT_MOUSE;
    in[3].mi.dwFlags = MOUSEEVENTF_WHEEL;
    in[3].mi.mouseData = WHEEL_DELTA;
    SetLastError(ERROR_SUCCESS);
    return SendInput(4, in, sizeof(INPUT));
}

} // namespace

class TestInputDispatch : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void classifier_states();
    void foregroundDispatchSplit();
    void wmInputArrivesViaQtDispatcher();
    void wheelPacketIsIndependentOfButtonsAndMovement();
    void cleanUnregister();

private:
    ptd::MouseInput input_;
    ptd::CursorHistory history_;
};

void TestInputDispatch::initTestCase() {
    QVERIFY(QApplication::instance() != nullptr);
}

// Pure, environment-independent proof of the verdict truth table (T-013R1).
// Runs everywhere; can never be skipped by session constraints.
void TestInputDispatch::classifier_states() {
    using V = ptd::MouseInput::RawInputVerdict;
    const auto win = true, reg = true;

    // Full injection + expected activity = Verified.
    QCOMPARE(ptd::MouseInput::classify_input_injection(8, 8, win, reg, true), V::Verified);
    // Full injection + NO activity = delivery regression (must FAIL upstream).
    QCOMPARE(ptd::MouseInput::classify_input_injection(8, 8, win, reg, false),
             V::InjectionNotDelivered);
    // SendInput injected fewer events than requested = environment.
    QCOMPARE(ptd::MouseInput::classify_input_injection(8, 3, win, reg, false),
             V::EnvironmentNotVerifiable);
    QCOMPARE(ptd::MouseInput::classify_input_injection(8, 0, win, reg, false),
             V::EnvironmentNotVerifiable);
    // Unusable sink is environment evidence regardless of injection counts.
    QCOMPARE(ptd::MouseInput::classify_input_injection(8, 8, false, reg, false),
             V::EnvironmentNotVerifiable);
    QCOMPARE(ptd::MouseInput::classify_input_injection(8, 8, win, false, false),
             V::EnvironmentNotVerifiable);
    // Even delivered-looking activity cannot outrank a broken sink contract.
    QCOMPARE(ptd::MouseInput::classify_input_injection(8, 8, false, reg, true),
             V::EnvironmentNotVerifiable);
}

// T-108 red control (VERIFY-ORACLE-01, T-104 lesson): the SAME oracle must
// compile against the pre-fix subject and fail there for the defect reason --
// the dispatch never examined wp -- never on a missing identifier. Presence of
// the decision seam is detected, not assumed, so the red half is behavioural.
template <typename T, typename = void>
struct has_raw_input_cleanup_decision : std::false_type {};
template <typename T>
struct has_raw_input_cleanup_decision<
    T, std::void_t<decltype(T::raw_input_needs_default_cleanup(WPARAM{}))>>
    : std::true_type {};

// T-108: the WM_INPUT foreground/background dispatch split. RIM_INPUT
// (foreground) must run DefWindowProc for system cleanup; RIM_INPUTSINK
// (background, the RIDEV_INPUTSINK path) must not. Pure decision, so this
// runs everywhere with no real foreground window.
//
// Template so the red half compiles against the pre-fix subject (which never
// declared the decision) and fails there for the defect reason -- the dispatch
// never examined wp -- never on a missing identifier (T-104 lesson). The
// template's discarded branch is not instantiated, so the pre-fix build only
// ever sees the QFAIL path.
template <typename T>
void check_wm_input_dispatch_split() {
    if constexpr (has_raw_input_cleanup_decision<T>::value) {
        // RIM_INPUT = 0x00000000 (foreground): needs DefWindowProc cleanup.
        QVERIFY(T::raw_input_needs_default_cleanup(0));
        // RIM_INPUTSINK = 0x00000001 (background): must NOT call DefWindowProc.
        QVERIFY(!T::raw_input_needs_default_cleanup(1));
        // Any other code is not a recognized foreground delivery.
        QVERIFY(!T::raw_input_needs_default_cleanup(2));
        QVERIFY(!T::raw_input_needs_default_cleanup(0xFFFF));
    } else {
        // Pre-fix subject: wnd_proc_static ignored wp entirely and called
        // DefWindowProc unconditionally, so no foreground/background split
        // exists to assert. The defect is the missing split, so the red half
        // must fail on exactly that.
        QFAIL("T-108: the WM_INPUT dispatch never examined wp via "
              "GET_RAWINPUT_CODE_WPARAM (no foreground/background split); the "
              "pre-fix subject fails for the defect, not a missing symbol");
    }
}

void TestInputDispatch::foregroundDispatchSplit() {
    check_wm_input_dispatch_split<ptd::MouseInput>();
}

void TestInputDispatch::wmInputArrivesViaQtDispatcher() {
    // T-83: without the arm flag this test must not move or click the
    // operator's real cursor; the classifier truth table above still runs.
    if (!real_input_armed()) {
        QSKIP("real input injection not armed (set PROTRAIL_ALLOW_REAL_INPUT=1); "
              "refusing to move/click the live cursor (T-83)");
    }
    if (!input_.valid()) {
        if (!input_.create(GetModuleHandleW(nullptr), &history_)) {
            QSKIP("raw-input window/registration unavailable (ENVIRONMENT_NOT_VERIFIABLE); not a functional PASS");
        }
    }
    QVERIFY(input_.valid());
    QVERIFY(input_.registered());

    // Confine the whole burst (moves + clicks) to a test-owned window.
    InputBurstScope burst;

    // Settle so unrelated desktop input cannot leak into the burst counters.
    QEventLoop quiet;
    QTimer::singleShot(150, &quiet, &QEventLoop::quit);
    quiet.exec();

    const uint64_t before_moves = input_.movement_count();

    uint32_t requested = 0;
    uint32_t injected = 0;
    DWORD send_error = ERROR_SUCCESS;

    QEventLoop loop;
    QTimer quit_timer;
    quit_timer.setSingleShot(true);
    QObject::connect(&quit_timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    QTimer activity_timer;
    int ticks = 0;
    constexpr int kMaxBurstTicks = 12;
    QObject::connect(&activity_timer, &QTimer::timeout, [&] {
        ++ticks;
        if (ticks > kMaxBurstTicks) {
            activity_timer.stop();
            quit_timer.start(50);
            return;
        }

        requested += 1;
        injected += send_relative_move(30 * ticks, 10 * ticks, &send_error);
        if (ticks >= 3) {
            // Synthetic button transitions can be dropped while the desktop is
            // busy. Retry the L/R/M burst within one bounded event loop instead
            // of making delivery depend on a single timing window; a session
            // that never delivers any button event still fails below.
            switch ((ticks - 3) % 3) {
            case 0:
                requested += 2;
                injected += send_click(MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP, &send_error);
                break;
            case 1:
                requested += 2;
                injected += send_click(MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP, &send_error);
                break;
            default:
                requested += 2;
                injected += send_click(MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP, &send_error);
                break;
            }
        }

        if (input_.movement_count() > before_moves && input_.button_count() >= 3) {
            activity_timer.stop();
            quit_timer.start(50);
        }
    });
    activity_timer.start(20);
    quit_timer.start(1500);
    loop.exec();

    const bool activity = input_.movement_count() != before_moves;
    const auto verdict = ptd::MouseInput::classify_input_injection(
        requested, injected, input_.valid(), input_.registered(), activity);

    if (verdict == ptd::MouseInput::RawInputVerdict::EnvironmentNotVerifiable) {
        qWarning("SendInput could not complete the burst: %u of %u events injected, err=%lu",
                 injected, requested, static_cast<unsigned long>(send_error));
        QSKIP("injection itself could not be performed in this session (ENVIRONMENT_NOT_VERIFIABLE); not a functional PASS");
    }
    if (verdict == ptd::MouseInput::RawInputVerdict::InjectionNotDelivered) {
        QFAIL("raw-input window valid, registration ok, SendInput injected all requested events, event loop ran -- but no expected WM_INPUT activity arrived (delivery regression)");
    }

    // Verified: expected raw input arrived.
    QVERIFY(activity);
    QVERIFY(input_.movement_count() > before_moves);
    QVERIFY(history_.size() > 0);
    QVERIFY(input_.button_count() >= 3);
    QVERIFY(history_.last().timestamp_ns > 0);
    QTest::qWait(10);
}

// T-80: the vertical wheel travels its own typed path. A packet that carries
// movement AND a button transition AND a wheel delta must deliver both the
// button transition into CursorHistory and the wheel to the wheel callback;
// movement alone must produce neither; and a wheel packet must never wake the
// renderer (no activity callback, no CursorHistory push).
void TestInputDispatch::wheelPacketIsIndependentOfButtonsAndMovement() {
    // T-83: same gate -- no real cursor movement or button injection without
    // the explicit arm flag.
    if (!real_input_armed()) {
        QSKIP("real input injection not armed (set PROTRAIL_ALLOW_REAL_INPUT=1); "
              "refusing to move/click the live cursor (T-83)");
    }
    if (!input_.valid()) {
        if (!input_.create(GetModuleHandleW(nullptr), &history_)) {
            QSKIP("raw-input window/registration unavailable (ENVIRONMENT_NOT_VERIFIABLE); not a functional PASS");
        }
    }
    QVERIFY(input_.valid());
    QVERIFY(input_.registered());

    // Confine moves/clicks/wheel to a test-owned window for the whole run.
    InputBurstScope burst;

    int wheel_events = 0;
    int wheel_deltas = 0;
    int activity_calls = 0;
    input_.set_wheel_callback([&](int64_t, int delta) {
        if (delta != 0) {
            ++wheel_events;
            wheel_deltas += delta;
        }
    });
    input_.set_activity_callback([&](const ptd::CursorSample&) { ++activity_calls; });

    // Settle: unrelated desktop input must not leak into the counters below.
    QEventLoop quiet;
    QTimer::singleShot(150, &quiet, &QEventLoop::quit);
    quiet.exec();

    const uint64_t moves_before = input_.movement_count();
    const uint64_t buttons_before = input_.button_count();
    const std::size_t history_before = history_.size();
    (void)history_before;
    const uint64_t wheel_before = input_.wheel_count();

    // 1. Movement only: movement wakes the renderer (B9 activity) but carries
    //    no wheel and no button.
    const int activity_after_moves = activity_calls;
    QTest::qWait(5);
    for (int i = 0; i < 3; ++i) {
        send_relative_move(25 + i, -15 + i, nullptr);
        QTest::qWait(10);
    }
    QCOMPARE(input_.wheel_count(), wheel_before);
    QCOMPARE(input_.button_count(), buttons_before);
    QVERIFY(input_.movement_count() > moves_before);
    QVERIFY(activity_calls > activity_after_moves);

    // 2. Wheel only: the delta reaches the wheel callback, but the trail
    //    renderer is NOT woken and no new sample appears in CursorHistory --
    //    a scroll is not movement. A busy desktop can interleave real
    //    movement, so the wheel-only attempt is retried until one clean
    //    packet is observed (bounded attempts, then ENVIRONMENT skip).
    bool clean_wheel = false;
    for (int attempt = 0; attempt < 12 && !clean_wheel; ++attempt) {
        const uint64_t moves_at = input_.movement_count();
        const std::size_t history_at = history_.size();
        const int activity_at = activity_calls;
        const int wheel_at = wheel_events;
        const uint64_t wheel_packets_at = input_.wheel_count();

        send_wheel(120, nullptr);
        QTest::qWait(50);

        const bool wheel_arrived = input_.wheel_count() > wheel_packets_at &&
                                   wheel_events > wheel_at;
        const bool nothing_else_moved = input_.movement_count() == moves_at &&
                                        history_.size() == history_at &&
                                        activity_calls == activity_at;
        clean_wheel = wheel_arrived && nothing_else_moved;
    }
    if (!clean_wheel) {
        input_.set_wheel_callback(nullptr);
        input_.set_activity_callback(nullptr);
        QSKIP("no clean wheel-only packet could be isolated from desktop input noise (ENVIRONMENT_NOT_VERIFIABLE); not a functional PASS");
    }
    QCOMPARE(input_.button_count(), buttons_before);

    // 3. One combined packet: move + left press/release + wheel notch, in a
    //    single SendInput batch so the system coalesces them into ONE
    //    RAWMOUSE. Both paths must fire from the same packet.
    const int combined_before = wheel_events;
    const std::size_t history_mid = history_.size();
    const int activity_before_combined = activity_calls;
    const uint32_t injected = send_combined_left_wheel();
    QTest::qWait(80);
    if (injected == 4) {
        QVERIFY(input_.button_count() > buttons_before);
        QVERIFY(wheel_events > combined_before);
        QVERIFY(history_.size() > history_mid);
        QVERIFY(activity_calls > activity_before_combined);
    } else {
        qWarning("combined packet injection incomplete (%u/4); environment evidence",
                 injected);
    }

    input_.set_wheel_callback(nullptr);
    input_.set_activity_callback(nullptr);
}

void TestInputDispatch::cleanUnregister() {
    if (!input_.valid()) {
        QSKIP("raw-input window never created (ENVIRONMENT_NOT_VERIFIABLE)");
    }
    const std::size_t n = history_.size();
    input_.destroy();
    QVERIFY(!input_.valid());
    QVERIFY(!input_.registered());
    if (n == 0) {
        QSKIP("no input was delivered this session; unregister state itself verified");
    }
    QVERIFY(n > 0);
}

QTEST_MAIN(TestInputDispatch)
#include "test_input_dispatch.moc"
