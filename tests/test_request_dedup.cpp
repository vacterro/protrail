// W2-004: bounded per-request identity dedup for the single-instance
// activation/presence channels.
//
// The defect: a single `last_acked_activation` scalar only dedupes the
// immediately previous id, so the valid interleaved sequence A, B, A (which
// HWND_BROADCAST fan-out produces from concurrent claimants) executes A twice.
// RequestDedup keeps a bounded recent set so each unique id executes once.

#include "../src/app/request_dedup.h"

#include <QtTest/QtTest>

class TestRequestDedup : public QObject {
    Q_OBJECT
private slots:
    void sequential_distinct_ids_are_all_processed();
    void interleaved_aba_is_processed_once();
    void repeated_abba_pattern_yields_one_execution_per_id();
    void bound_evicts_only_the_oldest();
    void clear_resets_the_authority();
};

void TestRequestDedup::sequential_distinct_ids_are_all_processed() {
    ptd::RequestDedup dedup;
    int executions = 0;
    for (std::uint64_t id = 1; id <= 10; ++id) {
        if (!dedup.already_processed(id)) {
            dedup.mark_processed(id);
            ++executions;
        }
    }
    QCOMPARE(executions, 10);
}

void TestRequestDedup::interleaved_aba_is_processed_once() {
    ptd::RequestDedup dedup;
    int a_executions = 0;
    int b_executions = 0;

    auto handle = [&](std::uint64_t id) {
        if (dedup.already_processed(id)) return;
        dedup.mark_processed(id);
        if (id == 0xA) ++a_executions;
        if (id == 0xB) ++b_executions;
    };

    handle(0xA);
    handle(0xB);
    handle(0xA);   // second broadcast copy of A -- must NOT execute again
    QCOMPARE(a_executions, 1);
    QCOMPARE(b_executions, 1);
}

void TestRequestDedup::repeated_abba_pattern_yields_one_execution_per_id() {
    ptd::RequestDedup dedup;
    int a = 0, b = 0;
    auto handle = [&](std::uint64_t id) {
        if (dedup.already_processed(id)) return;
        dedup.mark_processed(id);
        if (id == 0xA) ++a;
        if (id == 0xB) ++b;
    };
    // Broadcast fan-out can deliver A, B, A, B across several windows.
    handle(0xA); handle(0xB); handle(0xA); handle(0xB);
    QCOMPARE(a, 1);
    QCOMPARE(b, 1);
}

void TestRequestDedup::bound_evicts_only_the_oldest() {
    ptd::RequestDedup dedup;
    for (std::uint64_t id = 1; id <= ptd::RequestDedup::kBound; ++id) {
        dedup.mark_processed(id);
    }
    QCOMPARE(dedup.size(), ptd::RequestDedup::kBound);
    QVERIFY(dedup.already_processed(1));
    QVERIFY(dedup.already_processed(ptd::RequestDedup::kBound));

    // One more id evicts the oldest (1), keeps the rest.
    dedup.mark_processed(ptd::RequestDedup::kBound + 1);
    QCOMPARE(dedup.size(), ptd::RequestDedup::kBound);
    QVERIFY(!dedup.already_processed(1));
    QVERIFY(dedup.already_processed(2));
    QVERIFY(dedup.already_processed(ptd::RequestDedup::kBound + 1));
}

void TestRequestDedup::clear_resets_the_authority() {
    ptd::RequestDedup dedup;
    dedup.mark_processed(0xA);
    QVERIFY(dedup.already_processed(0xA));
    dedup.clear();
    QVERIFY(!dedup.already_processed(0xA));
    QCOMPARE(dedup.size(), std::size_t(0));
}

QTEST_MAIN(TestRequestDedup)
#include "test_request_dedup.moc"
