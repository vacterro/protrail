// T-018R1/R2/R3 single-instance protocol regressions. Every test uses
// UNIQUE per-test kernel object names (test tag + pid) so the suite never
// touches production global names and never collides with itself.
//
// T-018R3: Delivered requires a per-request acknowledgement. The owner
// side of the protocol is exercised through ptd::SingleInstance::
// acknowledge_request(); claimant notifications run on dedicated threads
// because kernel mutex ownership is thread-affine (exactly like the
// cross-process production case).

#include "../src/app/single_instance.h"

#include <QtTest/QtTest>
#include <QElapsedTimer>

#include <string>
#include <thread>

namespace {

ptd::SingleInstanceConfig unique_config(const wchar_t* tag) {
    ptd::SingleInstanceConfig c;
    const std::wstring base =
        std::wstring(L"Local\\ProTrail_T018R3_") + tag + L"_" +
        std::to_wstring(GetCurrentProcessId());
    c.mutex_name = base + L"_Mutex";
    c.ready_event_name = base + L"_Ready";
    c.activate_event_name = base + L"_Activate";
    c.shutdown_event_name = base + L"_Shutdown";
    // A real ProTrail process can be running during CTest. Give the
    // broadcast message its own test namespace as well as the kernel
    // objects, or production will ACK a test claimant it does not own.
    c.message_name = base + L"_Message";
    return c;
}

bool event_signaled(const std::wstring& name) {
    HANDLE h = OpenEventW(SYNCHRONIZE, FALSE, name.c_str());
    if (!h) return false;
    const DWORD w = WaitForSingleObject(h, 0);
    CloseHandle(h);
    return w == WAIT_OBJECT_0;
}

bool event_openable(const std::wstring& name) {
    HANDLE h = OpenEventW(SYNCHRONIZE, FALSE, name.c_str());
    if (!h) return false;
    CloseHandle(h);
    return true;
}

// Generic owner-thread fixture: claims the protocol on its own thread and
// executes test-ordered commands (mark_ready / begin_shutdown / release).
struct OwnerCtx {
    const ptd::SingleInstanceConfig* cfg = nullptr;
    HANDLE started = nullptr;
    HANDLE cmd = nullptr;        // mark_ready + consume startup intent
    HANDLE readyDone = nullptr;
    HANDLE shutdownEvt = nullptr; // begin_shutdown
    HANDLE shutdownDone = nullptr;
    HANDLE releaseEvt = nullptr;  // release + exit
    int claimOk = 0;
    int readyOk = 0;
    int consumed = -1;
};

DWORD WINAPI owner_thread_main(LPVOID raw) {
    auto* c = static_cast<OwnerCtx*>(raw);
    ptd::SingleInstance owner(*c->cfg);
    c->claimOk = owner.claim() == ptd::ClaimStatus::FirstOwner ? 1 : 0;
    SetEvent(c->started);
    HANDLE waits[3] = {c->cmd, c->shutdownEvt, c->releaseEvt};
    for (;;) {
        const DWORD w = WaitForMultipleObjects(3, waits, FALSE, 30000);
        if (w == WAIT_OBJECT_0) {
            ResetEvent(c->cmd); // one-shot command: consume before acting
            c->readyOk = owner.mark_ready() ? 1 : 0;
            // Startup durable-intent consumption happens right after
            // readiness, exactly like Application::run().
            c->consumed = owner.consume_startup_activation_request() ? 1 : 0;
            SetEvent(c->readyDone);
        } else if (w == WAIT_OBJECT_0 + 1) {
            ResetEvent(c->shutdownEvt); // one-shot command
            owner.begin_shutdown();
            SetEvent(c->shutdownDone);
        } else {
            break; // release command (or 30 s safety timeout)
        }
    }
    owner.release();
    return 0;
}

// Runs one notify_existing_owner() call on its own thread (claimants in
// production are separate processes; mutex ownership is thread-affine).
struct NotifyRun {
    ptd::SingleInstance* si = nullptr;
    DWORD wait_ready_ms = 5000;
    DWORD ack_timeout_ms = 5000;
    ptd::NotifyStatus status = ptd::NotifyStatus::Error;
    std::thread t;

    NotifyRun(ptd::SingleInstance& s, DWORD wr, DWORD ack)
        : si(&s), wait_ready_ms(wr), ack_timeout_ms(ack) {}
    void start() {
        t = std::thread([this] {
            status = si->notify_existing_owner(wait_ready_ms, ack_timeout_ms);
        });
    }
    void join() {
        if (t.joinable()) t.join();
    }
    ~NotifyRun() { if (t.joinable()) t.join(); }
};

bool wait_for_request_id(ptd::SingleInstance& si) {
    for (int i = 0; i < 5000 && si.last_request_id() == 0; ++i)
        QTest::qWait(2);
    return si.last_request_id() != 0;
}

} // namespace

class TestSingleInstance : public QObject {
    Q_OBJECT
private slots:
    // Ownership basics (T-018R1/R2 preserved).
    void first_claimant_becomes_owner();
    void second_claimant_returns_already_running_and_never_becomes_owner();
    void release_allows_a_later_claimant_to_become_owner();
    void activation_message_is_the_single_authority();
    void essential_primitive_failure_fails_claim_closed();
    void shutdown_revokes_readiness_and_raises_takeover_beacon();
    void transport_invariant_holds_while_ready_is_advertised();
    void mark_ready_without_ownership_fails();

    // T-018R3 acknowledgement protocol.
    void ready_owner_activation_is_acknowledged_and_delivered();
    void activation_without_ack_is_never_delivered();
    void shutdown_after_post_before_ack_routes_to_handoff();
    void owner_dies_after_post_before_ack_claimant_takes_over();
    void owner_dies_while_waiting_ready_claimant_takes_over();
    // W2-001: mutex takeover must not depend on the optional shutdown beacon.
    void missing_shutdown_beacon_still_reclaims_on_owner_release();
    void missing_shutdown_beacon_still_reclaims_on_abandonment();
    void slow_ready_owner_creates_no_duplicate_runtime();
    void startup_durable_request_is_consumed_normally();
    void concurrent_request_acks_do_not_cross();
    void acknowledge_request_unknown_id_fails_truthfully();
    void handoff_is_bounded_when_old_owner_still_owns();

    // T-018R2 handoff behavior preserved.
    void claimant_during_shutdown_is_not_delivered_and_takes_over();
    void exactly_one_owner_exists_after_handoff();
    void abandoned_mutex_takeover_via_wait_for_ownership();

    // T-42: ResetEvent safety-critical fail-closed regressions.
    void reset_event_failure_in_claim_fails_closed();
    void reset_event_failure_in_complete_takeover_fails_closed();
    void reset_event_failure_in_begin_shutdown_and_release_is_observable();
};

void TestSingleInstance::first_claimant_becomes_owner() {
    ptd::SingleInstance si(unique_config(L"first_owner"));
    QCOMPARE(si.claim(), ptd::ClaimStatus::FirstOwner);
    QVERIFY(si.is_owner());
    QCOMPARE(si.owner_state(), ptd::OwnerState::Starting);
    QVERIFY(si.activation_message() >= 0xC000 && si.activation_message() <= 0xFFFF);
}

void TestSingleInstance::second_claimant_returns_already_running_and_never_becomes_owner() {
    const auto cfg = unique_config(L"second_claimant");
    ptd::SingleInstance owner(cfg);
    QCOMPARE(owner.claim(), ptd::ClaimStatus::FirstOwner);

    ptd::SingleInstance second(cfg);
    QCOMPARE(second.claim(), ptd::ClaimStatus::AlreadyRunning);
    QVERIFY(!second.is_owner());
}

void TestSingleInstance::release_allows_a_later_claimant_to_become_owner() {
    const auto cfg = unique_config(L"release_reclaim");
    {
        ptd::SingleInstance owner(cfg);
        QCOMPARE(owner.claim(), ptd::ClaimStatus::FirstOwner);
        owner.release();
        QVERIFY(!owner.is_owner());
    }
    ptd::SingleInstance late(cfg);
    QCOMPARE(late.claim(), ptd::ClaimStatus::FirstOwner);
    QVERIFY(late.is_owner());
}

void TestSingleInstance::activation_message_is_the_single_authority() {
    const auto cfg = unique_config(L"message_authority");
    ptd::SingleInstance owner(cfg);
    QCOMPARE(owner.claim(), ptd::ClaimStatus::FirstOwner);

    ptd::SingleInstance second(cfg);
    QCOMPARE(second.claim(), ptd::ClaimStatus::AlreadyRunning);

    QVERIFY(owner.activation_message() != 0);
    QCOMPARE(second.activation_message(), owner.activation_message());
    QVERIFY(owner.activation_message() >= 0xC000 && owner.activation_message() <= 0xFFFF);
}

void TestSingleInstance::essential_primitive_failure_fails_claim_closed() {
    const auto cfg = unique_config(L"fail_closed");
    // A mutex already occupies the activation event's name: the transport
    // creation inside claim() must fail and the claim must report Error,
    // never a partially initialized FirstOwner.
    HANDLE blocker = CreateMutexW(nullptr, FALSE, cfg.activate_event_name.c_str());
    QVERIFY(blocker != nullptr);

    ptd::SingleInstance si(cfg);
    QCOMPARE(si.claim(), ptd::ClaimStatus::Error);
    QVERIFY(!si.is_owner());
    QVERIFY(si.last_error() != 0);

    CloseHandle(blocker);

    ptd::SingleInstance fresh(cfg);
    QCOMPARE(fresh.claim(), ptd::ClaimStatus::FirstOwner);
    QVERIFY(fresh.is_owner());
}

void TestSingleInstance::shutdown_revokes_readiness_and_raises_takeover_beacon() {
    const auto cfg = unique_config(L"shutdown_state");
    ptd::SingleInstance owner(cfg);
    QCOMPARE(owner.claim(), ptd::ClaimStatus::FirstOwner);
    QVERIFY(owner.mark_ready());
    QCOMPARE(owner.owner_state(), ptd::OwnerState::Ready);
    QVERIFY(event_signaled(cfg.ready_event_name));

    owner.begin_shutdown();
    QCOMPARE(owner.owner_state(), ptd::OwnerState::ShuttingDown);
    QVERIFY(!event_signaled(cfg.ready_event_name));   // readiness revoked
    QVERIFY(event_signaled(cfg.shutdown_event_name)); // beacon raised

    // Idempotent.
    owner.begin_shutdown();
    QCOMPARE(owner.owner_state(), ptd::OwnerState::ShuttingDown);
}

void TestSingleInstance::transport_invariant_holds_while_ready_is_advertised() {
    const auto cfg = unique_config(L"invariant");
    ptd::SingleInstance owner(cfg);
    QCOMPARE(owner.claim(), ptd::ClaimStatus::FirstOwner);
    QVERIFY(owner.mark_ready());

    QVERIFY(event_signaled(cfg.ready_event_name));
    QVERIFY(event_openable(cfg.activate_event_name));
    QVERIFY(event_openable(cfg.shutdown_event_name));

    owner.begin_shutdown();
    QVERIFY(!event_signaled(cfg.ready_event_name));
    QVERIFY(event_openable(cfg.activate_event_name));
    QVERIFY(event_signaled(cfg.shutdown_event_name));

    owner.release();
    QVERIFY(!event_openable(cfg.ready_event_name));
    QVERIFY(!event_openable(cfg.activate_event_name));
    QVERIFY(!event_openable(cfg.shutdown_event_name));
}

void TestSingleInstance::mark_ready_without_ownership_fails() {
    ptd::SingleInstance si(unique_config(L"mark_ready_fail"));
    QVERIFY(!si.is_owner());
    QVERIFY(!si.mark_ready());
}

// Phase 8 case 1: ready owner + processed request -> exact ACK -> Delivered.
void TestSingleInstance::ready_owner_activation_is_acknowledged_and_delivered() {
    const auto cfg = unique_config(L"ack_delivered");
    OwnerCtx ctx;
    ctx.cfg = &cfg;
    ctx.started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.cmd = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.readyDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.shutdownEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.shutdownDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.releaseEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    QVERIFY(ctx.started && ctx.cmd && ctx.readyDone && ctx.shutdownEvt && ctx.shutdownDone && ctx.releaseEvt);
    HANDLE t = CreateThread(nullptr, 0, owner_thread_main, &ctx, 0, nullptr);
    QVERIFY(t != nullptr);
    QVERIFY(WaitForSingleObject(ctx.started, 5000) == WAIT_OBJECT_0);
    QCOMPARE(ctx.claimOk, 1);
    SetEvent(ctx.cmd); // owner becomes Ready
    QVERIFY(WaitForSingleObject(ctx.readyDone, 5000) == WAIT_OBJECT_0);

    ptd::SingleInstance claimant(cfg);
    QCOMPARE(claimant.claim(), ptd::ClaimStatus::AlreadyRunning);

    NotifyRun run(claimant, 5000, 5000);
    run.start();
    QVERIFY(wait_for_request_id(claimant));
    // Owner processes the activation callback, then acknowledges.
    QVERIFY(ptd::SingleInstance::acknowledge_request(claimant.last_request_id()));
    run.join();
    QCOMPARE(run.status, ptd::NotifyStatus::Delivered);

    SetEvent(ctx.releaseEvt);
    QVERIFY(WaitForSingleObject(t, 5000) == WAIT_OBJECT_0);
    CloseHandle(t);
    CloseHandle(ctx.started); CloseHandle(ctx.cmd); CloseHandle(ctx.readyDone);
    CloseHandle(ctx.shutdownEvt); CloseHandle(ctx.shutdownDone); CloseHandle(ctx.releaseEvt);
}

// Phase 8 case 2 + 10: post succeeds but nobody processes/acknowledges the
// request -> NOT Delivered, bounded, no hang.
void TestSingleInstance::activation_without_ack_is_never_delivered() {
    const auto cfg = unique_config(L"no_ack");
    OwnerCtx ctx;
    ctx.cfg = &cfg;
    ctx.started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.cmd = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.readyDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.shutdownEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.shutdownDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.releaseEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE t = CreateThread(nullptr, 0, owner_thread_main, &ctx, 0, nullptr);
    QVERIFY(t != nullptr);
    QVERIFY(WaitForSingleObject(ctx.started, 5000) == WAIT_OBJECT_0);
    SetEvent(ctx.cmd);
    QVERIFY(WaitForSingleObject(ctx.readyDone, 5000) == WAIT_OBJECT_0);

    ptd::SingleInstance claimant(cfg);
    QCOMPARE(claimant.claim(), ptd::ClaimStatus::AlreadyRunning);

    QElapsedTimer timer;
    timer.start();
    const auto result = claimant.notify_existing_owner(1000, 300);
    QVERIFY(timer.elapsed() < 5000); // bounded
    QCOMPARE(result, ptd::NotifyStatus::Timeout);
    QVERIFY(result != ptd::NotifyStatus::Delivered);
    QVERIFY(claimant.last_request_id() != 0);
    QVERIFY(!claimant.is_owner());

    SetEvent(ctx.releaseEvt);
    QVERIFY(WaitForSingleObject(t, 5000) == WAIT_OBJECT_0);
    CloseHandle(t);
    CloseHandle(ctx.started); CloseHandle(ctx.cmd); CloseHandle(ctx.readyDone);
    CloseHandle(ctx.shutdownEvt); CloseHandle(ctx.shutdownDone); CloseHandle(ctx.releaseEvt);
}

// Phase 8 case 3: shutdown enters after the post but before the ACK ->
// claimant routes to the ownership handoff, never Delivered.
void TestSingleInstance::shutdown_after_post_before_ack_routes_to_handoff() {
    const auto cfg = unique_config(L"shutdown_mid_ack");
    OwnerCtx ctx;
    ctx.cfg = &cfg;
    ctx.started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.cmd = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.readyDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.shutdownEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.shutdownDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.releaseEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE t = CreateThread(nullptr, 0, owner_thread_main, &ctx, 0, nullptr);
    QVERIFY(t != nullptr);
    QVERIFY(WaitForSingleObject(ctx.started, 5000) == WAIT_OBJECT_0);
    SetEvent(ctx.cmd);
    QVERIFY(WaitForSingleObject(ctx.readyDone, 5000) == WAIT_OBJECT_0);

    ptd::SingleInstance claimant(cfg);
    QCOMPARE(claimant.claim(), ptd::ClaimStatus::AlreadyRunning);

    NotifyRun run(claimant, 5000, 5000);
    run.start();
    QVERIFY(wait_for_request_id(claimant));
    SetEvent(ctx.shutdownEvt); // owner begins shutdown after the post
    QVERIFY(WaitForSingleObject(ctx.shutdownDone, 5000) == WAIT_OBJECT_0);
    run.join();
    QCOMPARE(run.status, ptd::NotifyStatus::OwnerShuttingDown);

    // Owner completes teardown; the claimant performs the takeover.
    SetEvent(ctx.releaseEvt);
    QVERIFY(WaitForSingleObject(t, 5000) == WAIT_OBJECT_0);
    QVERIFY(claimant.wait_for_ownership(5000));
    QVERIFY(claimant.is_owner());

    CloseHandle(t);
    CloseHandle(ctx.started); CloseHandle(ctx.cmd); CloseHandle(ctx.readyDone);
    CloseHandle(ctx.shutdownEvt); CloseHandle(ctx.shutdownDone); CloseHandle(ctx.releaseEvt);
}

// Phase 8 case 4: owner dies after the post but before the ACK -> the
// claimant becomes the owner (production path, no manual takeover call).
void TestSingleInstance::owner_dies_after_post_before_ack_claimant_takes_over() {
    const auto cfg = unique_config(L"death_mid_ack");
    OwnerCtx ctx;
    ctx.cfg = &cfg;
    ctx.started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.cmd = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.readyDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.shutdownEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.shutdownDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.releaseEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE t = CreateThread(nullptr, 0, owner_thread_main, &ctx, 0, nullptr);
    QVERIFY(t != nullptr);
    QVERIFY(WaitForSingleObject(ctx.started, 5000) == WAIT_OBJECT_0);
    SetEvent(ctx.cmd);
    QVERIFY(WaitForSingleObject(ctx.readyDone, 5000) == WAIT_OBJECT_0);

    ptd::SingleInstance claimant(cfg);
    QCOMPARE(claimant.claim(), ptd::ClaimStatus::AlreadyRunning);

    NotifyRun run(claimant, 5000, 5000);
    run.start();
    QVERIFY(wait_for_request_id(claimant));
    // Abnormal owner death while the claimant waits for the ACK.
    QVERIFY(TerminateThread(t, 1));
    run.join();
    QCOMPARE(run.status, ptd::NotifyStatus::OwnershipReclaimed);
    QVERIFY(claimant.is_owner());
    QCOMPARE(claimant.owner_state(), ptd::OwnerState::Starting);
    QVERIFY(!event_signaled(cfg.ready_event_name)); // rebuilt, not falsely ready

    CloseHandle(t);
    CloseHandle(ctx.started); CloseHandle(ctx.cmd); CloseHandle(ctx.readyDone);
    CloseHandle(ctx.shutdownEvt); CloseHandle(ctx.shutdownDone); CloseHandle(ctx.releaseEvt);
}

// Phase 8 case 5 / Defect A: owner dies while the claimant waits for
// readiness -> claimant takes the abandoned ownership instead of returning
// OwnerStarting for a reclaimable protocol.
void TestSingleInstance::owner_dies_while_waiting_ready_claimant_takes_over() {
    const auto cfg = unique_config(L"death_before_ready");
    OwnerCtx ctx;
    ctx.cfg = &cfg;
    ctx.started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.cmd = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.readyDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.shutdownEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.shutdownDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.releaseEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE t = CreateThread(nullptr, 0, owner_thread_main, &ctx, 0, nullptr);
    QVERIFY(t != nullptr);
    QVERIFY(WaitForSingleObject(ctx.started, 5000) == WAIT_OBJECT_0);
    // Owner deliberately never marks ready.

    ptd::SingleInstance claimant(cfg);
    QCOMPARE(claimant.claim(), ptd::ClaimStatus::AlreadyRunning);

    NotifyRun run(claimant, 5000, 5000);
    run.start();
    QTest::qWait(50); // claimant is inside the readiness wait
    QVERIFY(TerminateThread(t, 1)); // abnormal death while starting
    run.join();
    QCOMPARE(run.status, ptd::NotifyStatus::OwnershipReclaimed);
    QVERIFY(claimant.is_owner());
    QVERIFY(!event_signaled(cfg.ready_event_name));

    CloseHandle(t);
    CloseHandle(ctx.started); CloseHandle(ctx.cmd); CloseHandle(ctx.readyDone);
    CloseHandle(ctx.shutdownEvt); CloseHandle(ctx.shutdownDone); CloseHandle(ctx.releaseEvt);
}

// W2-001: the claimant cannot open the owner's shutdown beacon, so the wait
// set must still watch the protocol mutex. A clean owner release while the
// claimant waits for readiness must yield OwnershipReclaimed, never Error.
void TestSingleInstance::missing_shutdown_beacon_still_reclaims_on_owner_release() {
    const auto owner_cfg = unique_config(L"w2001_release");
    // The claimant shares the mutex/ready/activate names but carries a
    // DIFFERENT shutdown name, so OpenEventW(shutdown) returns null for it --
    // exactly the missing-beacon state the pre-repair array mishandled.
    ptd::SingleInstanceConfig claimant_cfg = owner_cfg;
    claimant_cfg.shutdown_event_name = owner_cfg.shutdown_event_name + L"_Absent";

    OwnerCtx ctx;
    ctx.cfg = &owner_cfg;
    ctx.started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.cmd = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.readyDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.shutdownEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.shutdownDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.releaseEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE t = CreateThread(nullptr, 0, owner_thread_main, &ctx, 0, nullptr);
    QVERIFY(t != nullptr);
    QVERIFY(WaitForSingleObject(ctx.started, 5000) == WAIT_OBJECT_0);
    // Owner never marks ready: the claimant stays inside the readiness wait.

    ptd::SingleInstance claimant(claimant_cfg);
    QCOMPARE(claimant.claim(), ptd::ClaimStatus::AlreadyRunning);

    NotifyRun run(claimant, 5000, 5000);
    run.start();
    QTest::qWait(50);              // claimant inside the readiness wait
    QVERIFY(!event_openable(claimant_cfg.shutdown_event_name));
    SetEvent(ctx.releaseEvt);      // clean owner release -> mutex freed
    run.join();

    QCOMPARE(run.status, ptd::NotifyStatus::OwnershipReclaimed);
    QVERIFY(claimant.is_owner());

    QVERIFY(WaitForSingleObject(t, 5000) == WAIT_OBJECT_0);
    CloseHandle(t);
    CloseHandle(ctx.started); CloseHandle(ctx.cmd); CloseHandle(ctx.readyDone);
    CloseHandle(ctx.shutdownEvt); CloseHandle(ctx.shutdownDone); CloseHandle(ctx.releaseEvt);
}

// W2-001: same missing-beacon state, but the owner DIES owning the mutex.
// The claimant must observe abandonment through the compacted mutex index.
void TestSingleInstance::missing_shutdown_beacon_still_reclaims_on_abandonment() {
    const auto owner_cfg = unique_config(L"w2001_abandon");
    ptd::SingleInstanceConfig claimant_cfg = owner_cfg;
    claimant_cfg.shutdown_event_name = owner_cfg.shutdown_event_name + L"_Absent";

    OwnerCtx ctx;
    ctx.cfg = &owner_cfg;
    ctx.started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.cmd = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.readyDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.shutdownEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.shutdownDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.releaseEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE t = CreateThread(nullptr, 0, owner_thread_main, &ctx, 0, nullptr);
    QVERIFY(t != nullptr);
    QVERIFY(WaitForSingleObject(ctx.started, 5000) == WAIT_OBJECT_0);

    ptd::SingleInstance claimant(claimant_cfg);
    QCOMPARE(claimant.claim(), ptd::ClaimStatus::AlreadyRunning);

    NotifyRun run(claimant, 5000, 5000);
    run.start();
    QTest::qWait(50);
    QVERIFY(!event_openable(claimant_cfg.shutdown_event_name));
    QVERIFY(TerminateThread(t, 1)); // abnormal death while owning the protocol
    run.join();

    QCOMPARE(run.status, ptd::NotifyStatus::OwnershipReclaimed);
    QVERIFY(claimant.is_owner());

    CloseHandle(t);
    CloseHandle(ctx.started); CloseHandle(ctx.cmd); CloseHandle(ctx.readyDone);
    CloseHandle(ctx.shutdownEvt); CloseHandle(ctx.shutdownDone); CloseHandle(ctx.releaseEvt);
}
// Phase 8 case 6: owner alive and positively owning but slow to become
// Ready -> OwnerStarting, durable intent retained, no duplicate runtime.
void TestSingleInstance::slow_ready_owner_creates_no_duplicate_runtime() {
    const auto cfg = unique_config(L"slow_ready");
    OwnerCtx ctx;
    ctx.cfg = &cfg;
    ctx.started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.cmd = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.readyDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.shutdownEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.shutdownDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.releaseEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE t = CreateThread(nullptr, 0, owner_thread_main, &ctx, 0, nullptr);
    QVERIFY(t != nullptr);
    QVERIFY(WaitForSingleObject(ctx.started, 5000) == WAIT_OBJECT_0);

    ptd::SingleInstance claimant(cfg);
    QCOMPARE(claimant.claim(), ptd::ClaimStatus::AlreadyRunning);
    const auto result = claimant.notify_existing_owner(300, 300);
    QCOMPARE(result, ptd::NotifyStatus::OwnerStarting);
    QVERIFY(!claimant.is_owner());

    // Exactly one owner: a third claimant still sees the live owner.
    ptd::SingleInstance third(cfg);
    QCOMPARE(third.claim(), ptd::ClaimStatus::AlreadyRunning);
    QVERIFY(!third.is_owner());

    SetEvent(ctx.releaseEvt);
    QVERIFY(WaitForSingleObject(t, 5000) == WAIT_OBJECT_0);
    CloseHandle(t);
    CloseHandle(ctx.started); CloseHandle(ctx.cmd); CloseHandle(ctx.readyDone);
    CloseHandle(ctx.shutdownEvt); CloseHandle(ctx.shutdownDone); CloseHandle(ctx.releaseEvt);
}

// Phase 6/8 case 7: durable startup intent survives until the owner becomes
// Ready and is consumed exactly once (T-018R1 behavior preserved).
void TestSingleInstance::startup_durable_request_is_consumed_normally() {
    const auto cfg = unique_config(L"durable_intent");
    OwnerCtx ctx;
    ctx.cfg = &cfg;
    ctx.started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.cmd = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.readyDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.shutdownEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.shutdownDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.releaseEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE t = CreateThread(nullptr, 0, owner_thread_main, &ctx, 0, nullptr);
    QVERIFY(t != nullptr);
    QVERIFY(WaitForSingleObject(ctx.started, 5000) == WAIT_OBJECT_0);

    ptd::SingleInstance claimant(cfg);
    QCOMPARE(claimant.claim(), ptd::ClaimStatus::AlreadyRunning);
    QCOMPARE(claimant.notify_existing_owner(300, 300), ptd::NotifyStatus::OwnerStarting);

    // Owner becomes Ready and consumes the retained startup intent.
    SetEvent(ctx.cmd);
    QVERIFY(WaitForSingleObject(ctx.readyDone, 5000) == WAIT_OBJECT_0);
    QCOMPARE(ctx.readyOk, 1);
    QCOMPARE(ctx.consumed, 1);

    SetEvent(ctx.releaseEvt);
    QVERIFY(WaitForSingleObject(t, 5000) == WAIT_OBJECT_0);
    CloseHandle(t);
    CloseHandle(ctx.started); CloseHandle(ctx.cmd); CloseHandle(ctx.readyDone);
    CloseHandle(ctx.shutdownEvt); CloseHandle(ctx.shutdownDone); CloseHandle(ctx.releaseEvt);
}

// Phase 8 case 8: concurrent claimants get their own acknowledgements; no
// request can consume another request's ACK.
void TestSingleInstance::concurrent_request_acks_do_not_cross() {
    const auto cfg = unique_config(L"concurrent_acks");
    OwnerCtx ctx;
    ctx.cfg = &cfg;
    ctx.started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.cmd = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.readyDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.shutdownEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.shutdownDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.releaseEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE t = CreateThread(nullptr, 0, owner_thread_main, &ctx, 0, nullptr);
    QVERIFY(t != nullptr);
    QVERIFY(WaitForSingleObject(ctx.started, 5000) == WAIT_OBJECT_0);
    SetEvent(ctx.cmd);
    QVERIFY(WaitForSingleObject(ctx.readyDone, 5000) == WAIT_OBJECT_0);

    ptd::SingleInstance l1(cfg);
    ptd::SingleInstance l2(cfg);
    QCOMPARE(l1.claim(), ptd::ClaimStatus::AlreadyRunning);
    QCOMPARE(l2.claim(), ptd::ClaimStatus::AlreadyRunning);

    NotifyRun r1(l1, 5000, 5000);
    NotifyRun r2(l2, 5000, 5000);
    r1.start();
    r2.start();
    QVERIFY(wait_for_request_id(l1));
    QVERIFY(wait_for_request_id(l2));
    QVERIFY(l1.last_request_id() != l2.last_request_id()); // unique identities

    // Acknowledge L2 first: exactly L2 is satisfied. L1 keeps waiting on
    // its own (still unsignaled) ACK event -- a foreign ACK cannot cross.
    QVERIFY(ptd::SingleInstance::acknowledge_request(l2.last_request_id()));
    r2.join();
    QCOMPARE(r2.status, ptd::NotifyStatus::Delivered);

    // Then L1: its own ACK satisfies it.
    QVERIFY(ptd::SingleInstance::acknowledge_request(l1.last_request_id()));
    r1.join();
    QCOMPARE(r1.status, ptd::NotifyStatus::Delivered);

    SetEvent(ctx.releaseEvt);
    QVERIFY(WaitForSingleObject(t, 5000) == WAIT_OBJECT_0);
    CloseHandle(t);
    CloseHandle(ctx.started); CloseHandle(ctx.cmd); CloseHandle(ctx.readyDone);
    CloseHandle(ctx.shutdownEvt); CloseHandle(ctx.shutdownDone); CloseHandle(ctx.releaseEvt);
}

// Phase 8 case 9: acknowledgement failure is truthful (never pretended).
void TestSingleInstance::acknowledge_request_unknown_id_fails_truthfully() {
    QVERIFY(!ptd::SingleInstance::acknowledge_request(0));
    QVERIFY(!ptd::SingleInstance::acknowledge_request(0x123456789ABCDEFull));
}

// Phase 8 case 10 / T-018R2: the handoff wait stays bounded while a live
// owner owns the protocol, then takeover succeeds after release.
void TestSingleInstance::handoff_is_bounded_when_old_owner_still_owns() {
    const auto cfg = unique_config(L"handoff_bounded");
    OwnerCtx ctx;
    ctx.cfg = &cfg;
    ctx.started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.cmd = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.readyDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.shutdownEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.shutdownDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.releaseEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE t = CreateThread(nullptr, 0, owner_thread_main, &ctx, 0, nullptr);
    QVERIFY(t != nullptr);
    QVERIFY(WaitForSingleObject(ctx.started, 5000) == WAIT_OBJECT_0);
    SetEvent(ctx.cmd);
    QVERIFY(WaitForSingleObject(ctx.readyDone, 5000) == WAIT_OBJECT_0);

    ptd::SingleInstance claimant(cfg);
    QCOMPARE(claimant.claim(), ptd::ClaimStatus::AlreadyRunning);

    QElapsedTimer timer;
    timer.start();
    QVERIFY(!claimant.wait_for_ownership(150));
    QVERIFY(timer.elapsed() < 5000); // bounded, no hang
    QVERIFY(!claimant.is_owner());

    SetEvent(ctx.releaseEvt);
    QVERIFY(WaitForSingleObject(t, 5000) == WAIT_OBJECT_0);
    QVERIFY(claimant.wait_for_ownership(5000));
    QVERIFY(claimant.is_owner());

    CloseHandle(t);
    CloseHandle(ctx.started); CloseHandle(ctx.cmd); CloseHandle(ctx.readyDone);
    CloseHandle(ctx.shutdownEvt); CloseHandle(ctx.shutdownDone); CloseHandle(ctx.releaseEvt);
}

// T-018R2 preserved: a beacon-signaled owner is never delivered to; the
// claimant hands off once the old owner releases.
void TestSingleInstance::claimant_during_shutdown_is_not_delivered_and_takes_over() {
    const auto cfg = unique_config(L"shutdown_handoff");
    OwnerCtx ctx;
    ctx.cfg = &cfg;
    ctx.started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.cmd = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.readyDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.shutdownEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.shutdownDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.releaseEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE t = CreateThread(nullptr, 0, owner_thread_main, &ctx, 0, nullptr);
    QVERIFY(t != nullptr);
    QVERIFY(WaitForSingleObject(ctx.started, 5000) == WAIT_OBJECT_0);
    SetEvent(ctx.cmd);
    QVERIFY(WaitForSingleObject(ctx.readyDone, 5000) == WAIT_OBJECT_0);

    ptd::SingleInstance claimant(cfg);
    QCOMPARE(claimant.claim(), ptd::ClaimStatus::AlreadyRunning);

    SetEvent(ctx.shutdownEvt);
    QVERIFY(WaitForSingleObject(ctx.shutdownDone, 5000) == WAIT_OBJECT_0);
    QCOMPARE(claimant.notify_existing_owner(5000, 5000), ptd::NotifyStatus::OwnerShuttingDown);

    SetEvent(ctx.releaseEvt);
    QVERIFY(WaitForSingleObject(t, 5000) == WAIT_OBJECT_0);
    QVERIFY(claimant.wait_for_ownership(5000));
    QVERIFY(claimant.is_owner());
    QVERIFY(!event_signaled(cfg.ready_event_name));
    QVERIFY(!event_signaled(cfg.shutdown_event_name));

    CloseHandle(t);
    CloseHandle(ctx.started); CloseHandle(ctx.cmd); CloseHandle(ctx.readyDone);
    CloseHandle(ctx.shutdownEvt); CloseHandle(ctx.shutdownDone); CloseHandle(ctx.releaseEvt);
}

// T-018R2 preserved: exactly one owner exists after a handoff.
void TestSingleInstance::exactly_one_owner_exists_after_handoff() {
    const auto cfg = unique_config(L"single_after_handoff");
    OwnerCtx ctx;
    ctx.cfg = &cfg;
    ctx.started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.cmd = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.readyDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.shutdownEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.shutdownDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.releaseEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE t = CreateThread(nullptr, 0, owner_thread_main, &ctx, 0, nullptr);
    QVERIFY(t != nullptr);
    QVERIFY(WaitForSingleObject(ctx.started, 5000) == WAIT_OBJECT_0);
    SetEvent(ctx.cmd);
    QVERIFY(WaitForSingleObject(ctx.readyDone, 5000) == WAIT_OBJECT_0);

    ptd::SingleInstance next(cfg);
    QCOMPARE(next.claim(), ptd::ClaimStatus::AlreadyRunning);
    SetEvent(ctx.shutdownEvt);
    QVERIFY(WaitForSingleObject(ctx.shutdownDone, 5000) == WAIT_OBJECT_0);
    SetEvent(ctx.releaseEvt);
    QVERIFY(WaitForSingleObject(t, 5000) == WAIT_OBJECT_0);
    QVERIFY(next.wait_for_ownership(5000));
    QVERIFY(next.is_owner());

    // A further claimant sees exactly one (the new) owner.
    ptd::SingleInstance third(cfg);
    QCOMPARE(third.claim(), ptd::ClaimStatus::AlreadyRunning);
    QVERIFY(!third.is_owner());

    CloseHandle(t);
    CloseHandle(ctx.started); CloseHandle(ctx.cmd); CloseHandle(ctx.readyDone);
    CloseHandle(ctx.shutdownEvt); CloseHandle(ctx.shutdownDone); CloseHandle(ctx.releaseEvt);
}

// T-018R2 preserved: the single-object handoff path reports mutex
// abandonment explicitly and recovers ownership.
void TestSingleInstance::abandoned_mutex_takeover_via_wait_for_ownership() {
    const auto cfg = unique_config(L"abandoned");
    OwnerCtx ctx;
    ctx.cfg = &cfg;
    ctx.started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.cmd = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.readyDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.shutdownEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.shutdownDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.releaseEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE t = CreateThread(nullptr, 0, owner_thread_main, &ctx, 0, nullptr);
    QVERIFY(t != nullptr);
    QVERIFY(WaitForSingleObject(ctx.started, 5000) == WAIT_OBJECT_0);

    ptd::SingleInstance claimant(cfg);
    QCOMPARE(claimant.claim(), ptd::ClaimStatus::AlreadyRunning);

    // Abnormal owner death while the claimant already holds a mutex handle.
    QVERIFY(TerminateThread(t, 1));
    QVERIFY(claimant.wait_for_ownership(5000));
    QVERIFY(claimant.is_owner());
    QVERIFY(event_openable(cfg.ready_event_name));
    QVERIFY(!event_signaled(cfg.ready_event_name));

    CloseHandle(t);
    CloseHandle(ctx.started); CloseHandle(ctx.cmd); CloseHandle(ctx.readyDone);
    CloseHandle(ctx.shutdownEvt); CloseHandle(ctx.shutdownDone); CloseHandle(ctx.releaseEvt);
}

void TestSingleInstance::reset_event_failure_in_claim_fails_closed() {
    const auto cfg = unique_config(L"reset_claim_fail");
    ptd::set_reset_event_seam_for_tests([](HANDLE) -> BOOL {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    });

    ptd::SingleInstance si(cfg);
    QCOMPARE(si.claim(), ptd::ClaimStatus::Error);
    QVERIFY(!si.is_owner());
    QCOMPARE(si.last_error(), static_cast<DWORD>(ERROR_INVALID_HANDLE));

    ptd::set_reset_event_seam_for_tests(nullptr);

    // Fail-closed verification: mutex was NOT left held; a new claimant can become FirstOwner.
    ptd::SingleInstance fresh(cfg);
    QCOMPARE(fresh.claim(), ptd::ClaimStatus::FirstOwner);
    QVERIFY(fresh.is_owner());
}

void TestSingleInstance::reset_event_failure_in_complete_takeover_fails_closed() {
    const auto cfg = unique_config(L"reset_takeover_fail");
    OwnerCtx ctx;
    ctx.cfg = &cfg;
    ctx.started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.cmd = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.readyDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.shutdownEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.shutdownDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx.releaseEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE t = CreateThread(nullptr, 0, owner_thread_main, &ctx, 0, nullptr);
    QVERIFY(t != nullptr);
    QVERIFY(WaitForSingleObject(ctx.started, 5000) == WAIT_OBJECT_0);

    ptd::SingleInstance claimant(cfg);
    QCOMPARE(claimant.claim(), ptd::ClaimStatus::AlreadyRunning);

    // Arm ResetEvent failure seam before owner releases and claimant takes over.
    ptd::set_reset_event_seam_for_tests([](HANDLE) -> BOOL {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    });

    // Owner releases cleanly.
    SetEvent(ctx.releaseEvt);
    WaitForSingleObject(t, 5000);

    // Claimant waits for ownership; complete_takeover must fail fail-closed.
    const bool took_over = claimant.wait_for_ownership(5000);
    QVERIFY(!took_over);
    QVERIFY(!claimant.is_owner());
    QCOMPARE(claimant.last_error(), static_cast<DWORD>(ERROR_INVALID_HANDLE));

    ptd::set_reset_event_seam_for_tests(nullptr);

    // After fail-closed takeover failure, mutex must not remain held: a third instance can claim FirstOwner.
    ptd::SingleInstance third(cfg);
    QCOMPARE(third.claim(), ptd::ClaimStatus::FirstOwner);
    QVERIFY(third.is_owner());

    CloseHandle(t);
    CloseHandle(ctx.started); CloseHandle(ctx.cmd); CloseHandle(ctx.readyDone);
    CloseHandle(ctx.shutdownEvt); CloseHandle(ctx.shutdownDone); CloseHandle(ctx.releaseEvt);
}

void TestSingleInstance::reset_event_failure_in_begin_shutdown_and_release_is_observable() {
    const auto cfg = unique_config(L"reset_shutdown_fail");
    ptd::SingleInstance owner(cfg);
    QCOMPARE(owner.claim(), ptd::ClaimStatus::FirstOwner);
    QVERIFY(owner.mark_ready());

    ptd::set_reset_event_seam_for_tests([](HANDLE) -> BOOL {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    });

    owner.begin_shutdown();
    QCOMPARE(owner.last_error(), static_cast<DWORD>(ERROR_INVALID_HANDLE));
    // Shutdown beacon is still raised even if ready_event reset failed.
    QVERIFY(event_signaled(cfg.shutdown_event_name));

    owner.release();
    QCOMPARE(owner.last_error(), static_cast<DWORD>(ERROR_INVALID_HANDLE));
    QVERIFY(!owner.is_owner());

    ptd::set_reset_event_seam_for_tests(nullptr);

    // Mutex was relinquished by release: a new instance can claim FirstOwner.
    ptd::SingleInstance next(cfg);
    QCOMPARE(next.claim(), ptd::ClaimStatus::FirstOwner);
    QVERIFY(next.is_owner());
}

QTEST_MAIN(TestSingleInstance)
#include "test_single_instance.moc"
