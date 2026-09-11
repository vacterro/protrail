#include <QtTest/QTest>

#include "../src/core/cursor_history.h"
#include "../src/core/cursor_sample.h"

using ptd::ButtonAction;
using ptd::CursorHistory;
using ptd::CursorSample;
using ptd::MouseButton;

namespace {

CursorSample movement(int64_t ts, int x, int y) {
    CursorSample s;
    s.timestamp_ns = ts;
    s.x = x;
    s.y = y;
    return s;
}

CursorSample click(int64_t ts, int x, int y, MouseButton b, ButtonAction a) {
    CursorSample s;
    s.timestamp_ns = ts;
    s.x = x;
    s.y = y;
    s.button = b;
    s.action = a;
    return s;
}

} // namespace

class TestCursorHistory : public QObject {
    Q_OBJECT
private slots:
    void insertionOrdering();
    void monotonicTimestamps();
    void maxSizePruning();
    void agePruning();
    void duplicateMovementCoalescing();
    void stationaryClickPreserved();
    void rapidButtonTransitions();
    void negativeCoordinates();
    void zeroMaxSamplesFallsBack();
    void fullHistoryInsertTrimsFront();
    void fullHistoryDuplicateIsCoalesced();
};

// 1. insertion ordering
void TestCursorHistory::insertionOrdering() {
    CursorHistory h(16);
    h.push(movement(1, 10, 10));
    h.push(movement(2, 20, 20));
    h.push(movement(3, 30, 30));
    QCOMPARE(h.size(), std::size_t{3});
    for (std::size_t i = 0; i < h.size(); ++i) {
        QCOMPARE(h.at(i).timestamp_ns, qint64(i + 1));
    }
    QCOMPARE(h.last().x, 30);
}

// 2. monotonic timestamp handling
void TestCursorHistory::monotonicTimestamps() {
    CursorHistory h(16);
    h.push(movement(100, 1, 1));
    h.push(movement(200, 2, 2));
    // Out-of-order arrival must not break ordering.
    h.push(movement(150, 3, 3));
    QVERIFY(h.at(2).timestamp_ns >= h.at(1).timestamp_ns);
    QVERIFY(h.at(1).timestamp_ns >= h.at(0).timestamp_ns);
}

// 3. maximum-size pruning
void TestCursorHistory::maxSizePruning() {
    CursorHistory h(4);
    for (int i = 0; i < 100; ++i) {
        h.push(movement(i, i, 0));
    }
    QCOMPARE(h.size(), std::size_t{4});
    QCOMPARE(h.at(0).x, 96); // oldest retained
    QCOMPARE(h.last().x, 99);
}

// 4. age pruning
void TestCursorHistory::agePruning() {
    CursorHistory h(1024, 1'000'000'000); // 1 s max age
    h.push(movement(0, 1, 1));
    h.push(movement(500'000'000, 2, 2));
    h.push(movement(900'000'000, 3, 3));
    // cutoff = 1.8e9 - 1e9 = 800e6 -> samples 0 and 500e6 are stale, 900e6 stays
    h.prune_before(1'800'000'000);
    QCOMPARE(h.size(), std::size_t{1});
    QCOMPARE(h.last().timestamp_ns, qint64{900'000'000});
    // no-op when nothing is stale
    h.prune_before(950'000'000);
    QCOMPARE(h.size(), std::size_t{1});
}

// 5. duplicate movement coalescing
void TestCursorHistory::duplicateMovementCoalescing() {
    CursorHistory h(16);
    h.push(movement(1, 5, 5));
    h.push(movement(2, 5, 5)); // same position, movement-only
    QCOMPARE(h.size(), std::size_t{1});
    QCOMPARE(h.last().timestamp_ns, qint64{2}); // newest ts wins
    h.push(movement(3, 6, 6));
    QCOMPARE(h.size(), std::size_t{2});
}

// 6. stationary click preservation
void TestCursorHistory::stationaryClickPreserved() {
    CursorHistory h(16);
    h.push(movement(1, 5, 5));
    h.push(click(2, 5, 5, MouseButton::Left, ButtonAction::Down)); // stationary click
    h.push(click(3, 5, 5, MouseButton::Left, ButtonAction::Up));
    QCOMPARE(h.size(), std::size_t{3});
    QCOMPARE(h.at(1).action, ButtonAction::Down);
    QCOMPARE(h.at(2).action, ButtonAction::Up);
    QCOMPARE(h.at(1).button, MouseButton::Left);
}

// 7. rapid button transitions
void TestCursorHistory::rapidButtonTransitions() {
    CursorHistory h(64);
    for (int i = 0; i < 20; ++i) {
        h.push(click(i, 7, 7, MouseButton::Left,
                     i % 2 == 0 ? ButtonAction::Down : ButtonAction::Up));
    }
    QCOMPARE(h.size(), std::size_t{20});
    for (int i = 0; i < 20; ++i) {
        QCOMPARE(h.at(i).action,
                 i % 2 == 0 ? ButtonAction::Down : ButtonAction::Up);
    }
}

// 8. negative virtual-screen coordinates
void TestCursorHistory::negativeCoordinates() {
    CursorHistory h(16);
    h.push(movement(1, -1920, -1080));
    h.push(click(2, -1920, -1080, MouseButton::Right, ButtonAction::Down));
    QCOMPARE(h.at(0).x, -1920);
    QCOMPARE(h.at(0).y, -1080);
    QCOMPARE(h.at(1).x, -1920);
    QCOMPARE(h.at(1).button, MouseButton::Right);
}

// 9. degenerate config falls back instead of breaking
void TestCursorHistory::zeroMaxSamplesFallsBack() {
    CursorHistory h(0);
    QVERIFY(h.max_samples() > 0);
    h.push(movement(1, 1, 1));
    QCOMPARE(h.size(), std::size_t{1});
}

// T-007R A4 regression: a FULL history that trims its front must report
// Inserted for a genuinely-new sample (the old size-delta inference called
// this "coalesced" because size stayed constant).
void TestCursorHistory::fullHistoryInsertTrimsFront() {
    CursorHistory h(4);
    for (int i = 0; i < 4; ++i) {
        QCOMPARE(h.push(movement(i, i, 0)), CursorHistory::PushResult::Inserted);
    }
    QCOMPARE(h.size(), std::size_t{4});

    // New distinct position while full: front trimmed, still Inserted.
    const auto result = h.push(movement(100, 999, 1));
    QCOMPARE(result, CursorHistory::PushResult::Inserted);
    QCOMPARE(h.size(), std::size_t{4});          // size unchanged!
    QCOMPARE(h.at(0).x, 1);                      // oldest (x=0) trimmed
    QCOMPARE(h.last().x, 999);                   // new sample really stored
    QCOMPARE(h.last().timestamp_ns, qint64{100});
}

// T-007R A4 regression: a duplicate movement into a FULL history is
// genuinely Coalesced -- no insertion, no trim, newest timestamp wins.
void TestCursorHistory::fullHistoryDuplicateIsCoalesced() {
    CursorHistory h(4);
    for (int i = 0; i < 4; ++i) {
        h.push(movement(i, i, 0));
    }
    const int64_t oldest_ts_before = h.at(0).timestamp_ns;
    const auto result = h.push(movement(500, 3, 0)); // same pos as last sample
    QCOMPARE(result, CursorHistory::PushResult::Coalesced);
    QCOMPARE(h.size(), std::size_t{4});
    QCOMPARE(h.at(0).timestamp_ns, oldest_ts_before); // front untouched
    QCOMPARE(h.last().timestamp_ns, qint64{500});     // ts refreshed
}

QTEST_MAIN(TestCursorHistory)
#include "test_cursor_history.moc"
