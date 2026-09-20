#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <string>

namespace ptd {

struct SingleInstanceConfig {
    std::wstring mutex_name = L"Local\\ProTrail_SingleInstance_Mutex";
    std::wstring ready_event_name = L"Local\\ProTrail_InstanceReady_Event";
    std::wstring activate_event_name = L"Local\\ProTrail_Activate_Event";
    std::wstring shutdown_event_name = L"Local\\ProTrail_Shutdown_Event";
    std::wstring message_name = L"ProTrail_ActivateInstance";
    // T-032: a second registered message for a launch that only wants to
    // hand its presence over -- a Windows autostart launch that finds an
    // owner must NOT request the Main/Settings window, so requesting it and
    // suppressing it later is not good enough. Same request identity, same
    // per-request ACK, different owner handler.
    std::wstring presence_message_name = L"ProTrail_PresenceInstance";
};

// T-032: what the claimant is asking the existing owner for.
//   Interactive  restore/activate the Main/Settings window (today's
//                behaviour, every manual launch).
//   Quiet        acknowledge that a ProTrail instance is alive; never
//                show, raise or activate any window and never steal focus.
//                A quiet request does not set the durable activation
//                intent either, so an owner still starting does not
//                consume a phantom activation and open its window.
enum class NotifyIntent {
    Interactive,
    Quiet,
};

enum class ClaimStatus {
    FirstOwner,
    AlreadyRunning,
    Error
};

// T-018R3: activation outcomes are terminal and evidenced.
//
// Delivered          the existing owner PROCESSED the activation callback:
//                    the claimant's unique per-request ACK event was
//                    signaled by the owner after it restored/activated the
//                    canonical SettingsWindow. PostMessageW success alone
//                    is never delivery.
// OwnerStarting      owner alive and positively owning the protocol for
//                    the whole readiness budget (the wait watches the
//                    mutex), but not Ready yet; durable intent retained.
// OwnerShuttingDown  owner revoked readiness (begin_shutdown); claimant
//                    must hand off through wait_for_ownership().
// Timeout            bounded budget expired without any terminal outcome;
//                    never means delivered.
// Error              transport failure (event open/set/create/post).
// OwnershipReclaimed the previous owner released or abandoned the mutex
//                    while this claimant waited; this object completed the
//                    takeover and IS the new owner -- run the Application.
enum class NotifyStatus {
    Delivered,
    OwnerStarting,
    OwnerShuttingDown,
    Timeout,
    Error,
    OwnershipReclaimed
};

// T-018R2: explicit owner lifecycle state. Meaningful only while this
// object owns the protocol.
enum class OwnerState {
    Starting,
    Ready,
    ShuttingDown
};

// T-018R2 protocol invariants (I1..I4: real mutex ownership; signaled
// readiness implies usable transport+receiver; non-delivered outcomes
// route to handoff; transport rebuilt+reset under ownership) extended by
// T-018R3:
//
//  I5  Delivered requires a per-request acknowledgement. Every activation
//      request carries a unique identity (claimant PID + nonce) passed
//      through the registered message; the owner creates the matching
//      uniquely-named ACK event per request and signals it only AFTER the
//      activation callback executed. Concurrent requests can never
//      consume each other's acknowledgement.
//  I6  A claimant never exits on an unacknowledged post. Every wait
//      (readiness AND acknowledgement) also watches the shutdown beacon
//      and the protocol mutex, so owner shutdown or owner death during
//      any phase deterministically routes the claimant into ownership
//      takeover instead of a lost launch.
class SingleInstance {
public:
    explicit SingleInstance(SingleInstanceConfig config = {});
    ~SingleInstance();

    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    // Claim with real ownership semantics (CreateMutexW bInitialOwner=TRUE);
    // transport created before the ownership attempt, safety-critical
    // signaling reset under ownership (I4). A claimant keeps its unowned
    // mutex handle as the handoff gate. Any essential-primitive failure
    // releases every created handle and returns ClaimStatus::Error.
    ClaimStatus claim();

    // Existing-owner activation with acknowledgement (T-018R3).
    // wait_ready_ms: bounded budget for the existing owner to become Ready
    //   (the wait also watches the shutdown beacon and the mutex).
    // ack_timeout_ms: bounded budget for the owner to process the posted
    //   activation and signal this request's ACK event.
    NotifyStatus notify_existing_owner(DWORD wait_ready_ms = 5000,
                                       DWORD ack_timeout_ms = 5000,
                                       NotifyIntent intent = NotifyIntent::Interactive);

    // Deterministic bounded handoff (claimant side): wait for the previous
    // owner to release the mutex (WAIT_OBJECT_0) or die owning it
    // (WAIT_ABANDONED), then rebuild the transport under ownership and
    // become the new owner. false = bounded budget expired with a live
    // owner still owning (never start a second runtime).
    bool wait_for_ownership(DWORD timeout_ms);

    // Owner-side protocol (consumed by Application::run).
    bool mark_ready();
    bool consume_startup_activation_request();

    // T-018R2: begin shutdown BEFORE the activation receiver is removed:
    // revokes readiness, raises the takeover beacon. Idempotent; ignored
    // for non-owners.
    void begin_shutdown();

    // T-018R3: owner side. Signal the ACK event of the exact request
    // identified by (wparam, lparam) from the registered activation
    // message. MUST be called only after the activation handling executed.
    // Returns false (with exact Win32 error logged) if the request's ACK
    // event cannot be opened or signaled -- the claimant will then never
    // report Delivered.
    static bool acknowledge_request(std::uint64_t request_id);

    // Identity of the most recent activation request issued by this
    // claimant object (0 = none). Diagnostic/test visibility.
    std::uint64_t last_request_id() const { return last_request_id_.load(std::memory_order_acquire); }

    UINT activation_message() const { return wm_activate_; }
    // T-032: identifier of the quiet presence message (0 = unregistered).
    UINT presence_message() const { return wm_presence_; }
    DWORD last_error() const { return last_error_; }
    bool is_owner() const { return is_owner_; }
    OwnerState owner_state() const { return owner_state_; }

    // Teardown: readiness revoked defensively FIRST, transport handles
    // next, mutex released LAST (no claimant can acquire the mutex while
    // any transport handle of this owner still exists).
    void release();

private:
    static std::wstring ack_event_name(std::uint64_t request_id);
    static std::uint64_t make_request_id();
    // Called when this process has just ACQUIRED the mutex (released or
    // abandoned by the previous owner): rebuild transport under ownership,
    // reset safety-critical signaling, become owner.
    bool complete_takeover(bool abandoned);
    // True if the named shutdown beacon is observable signaled.
    bool shutdown_signaled();

    SingleInstanceConfig config_;
    HANDLE ready_event_ = nullptr;
    HANDLE activate_event_ = nullptr;
    HANDLE shutdown_event_ = nullptr;
    HANDLE mutex_ = nullptr;
    UINT wm_activate_ = 0;
    UINT wm_presence_ = 0;
    DWORD last_error_ = 0;
    bool is_owner_ = false;
    OwnerState owner_state_ = OwnerState::Starting;
    std::atomic<std::uint64_t> last_request_id_{0};
};

// Test seam for ResetEvent failure injection.
using ResetEventFn = BOOL (*)(HANDLE);
void set_reset_event_seam_for_tests(ResetEventFn seam);

} // namespace ptd
