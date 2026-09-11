// T-007R A3: WM_INPUT arriving through Qt's win32 dispatcher needs no pump.
// T-013R1 verification truth: the skip/fail split comes from INDEPENDENT
// injection evidence, never from the absence of the tested event.
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

namespace {

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

} // namespace

class TestInputDispatch : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void classifier_states();
    void wmInputArrivesViaQtDispatcher();
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

void TestInputDispatch::wmInputArrivesViaQtDispatcher() {
    if (!input_.valid()) {
        if (!input_.create(GetModuleHandleW(nullptr), &history_)) {
            QSKIP("raw-input window/registration unavailable (ENVIRONMENT_NOT_VERIFIABLE); not a functional PASS");
        }
    }
    QVERIFY(input_.valid());
    QVERIFY(input_.registered());

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
    QObject::connect(&activity_timer, &QTimer::timeout, [&] {
        ++ticks;
        if (ticks <= 6) {
            requested += 1;
            injected += send_relative_move(30 * ticks, 10 * ticks, &send_error);
            if (ticks == 3) {
                requested += 2;
                injected += send_click(MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP, &send_error);
            }
            if (ticks == 4) {
                requested += 2;
                injected += send_click(MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP, &send_error);
            }
            if (ticks == 5) {
                requested += 2;
                injected += send_click(MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP, &send_error);
            }
        } else {
            activity_timer.stop();
            quit_timer.start(50);
        }
    });
    activity_timer.start(20);
    quit_timer.start(400);
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
