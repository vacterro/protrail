#include "single_instance.h"
#include "../core/log.h"

#include <algorithm>

namespace ptd {

namespace {

// Per-request ACK event namespace (T-018R3, I5). The request identity is
// globally unique (claimant PID + high-resolution nonce), so concurrent
// claimants can never collide or consume each other's acknowledgement.
constexpr wchar_t kAckNamePrefix[] = L"Local\\ProTrail_ActivationAck_";

ResetEventFn g_reset_event_seam = nullptr;

// W2-001: ONE owner for wait-set construction. The pre-repair code computed
// logical handle counts/indexes as though optional handles had been compacted
// but never actually compacted the physical array, so a missing shutdown
// beacon left a null handle inside the wait and excluded the protocol mutex.
// Build the array by APPENDING only non-null handles and record each real
// index at the moment it is appended.
struct WaitSet {
    HANDLE handles[3] = {nullptr, nullptr, nullptr};
    DWORD count = 0;
    int ready_index = -1;    // readiness or ack event
    int shutdown_index = -1; // optional shutdown beacon
    int mutex_index = -1;    // protocol mutex (present whenever mutex_ exists)
};

WaitSet build_wait_set(HANDLE primary, HANDLE shutdown, HANDLE mutex) {
    WaitSet set;
    if (primary) {
        set.ready_index = static_cast<int>(set.count);
        set.handles[set.count++] = primary;
    }
    if (shutdown) {
        set.shutdown_index = static_cast<int>(set.count);
        set.handles[set.count++] = shutdown;
    }
    if (mutex) {
        set.mutex_index = static_cast<int>(set.count);
        set.handles[set.count++] = mutex;
    }
    return set;
}

BOOL call_reset_event(HANDLE h) {
    if (g_reset_event_seam) {
        return g_reset_event_seam(h);
    }
    return ResetEvent(h);
}

} // namespace

void set_reset_event_seam_for_tests(ResetEventFn seam) {
    g_reset_event_seam = seam;
}

SingleInstance::SingleInstance(SingleInstanceConfig config)
    : config_(std::move(config)) {}

SingleInstance::~SingleInstance() {
    release();
}

std::wstring SingleInstance::ack_event_name(std::uint64_t request_id) {
    return std::wstring(kAckNamePrefix) + std::to_wstring(request_id);
}

std::uint64_t SingleInstance::make_request_id() {
    LARGE_INTEGER tick{};
    QueryPerformanceCounter(&tick);
    // High 32 bits: claimant PID (per-process uniqueness).
    // Low 32 bits: QPC nonce (per-request uniqueness inside a process).
    return (static_cast<std::uint64_t>(GetCurrentProcessId()) << 32)
         | static_cast<std::uint32_t>(tick.LowPart);
}

bool SingleInstance::acknowledge_request(std::uint64_t request_id) {
    if (request_id == 0) {
        ptd::log_write(ptd::LogLevel::Error, "single_instance: acknowledge_request called with zero request id");
        return false;
    }
    const std::wstring name = ack_event_name(request_id);
    SetLastError(ERROR_SUCCESS);
    HANDLE ack = OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str());
    if (!ack) {
        const DWORD err = GetLastError();
        ptd::log_write(ptd::LogLevel::Error, "single_instance: ACK open failed for request " + std::to_string(request_id) + " with error " + std::to_string(err));
        return false;
    }
    const BOOL ok = SetEvent(ack);
    const DWORD err = GetLastError();
    CloseHandle(ack);
    if (!ok) {
        ptd::log_write(ptd::LogLevel::Error, "single_instance: ACK SetEvent failed for request " + std::to_string(request_id) + " with error " + std::to_string(err));
        return false;
    }
    ptd::log_write(ptd::LogLevel::Info, "single_instance: activation request " + std::to_string(request_id) + " acknowledged");
    return true;
}

ClaimStatus SingleInstance::claim() {
    // The registered activation message is an essential protocol primitive:
    // without it the owner filter and the claimant broadcast cannot agree
    // on an identifier. Fail closed.
    if (wm_activate_ == 0) {
        wm_activate_ = RegisterWindowMessageW(config_.message_name.c_str());
        if (wm_activate_ == 0) {
            last_error_ = GetLastError();
            ptd::log_write(ptd::LogLevel::Error, "single_instance: RegisterWindowMessageW failed with error " + std::to_string(last_error_));
            return ClaimStatus::Error;
        }
    }
    // T-032: the quiet presence channel is an essential protocol primitive
    // too -- without it an autostart launch could not hand over its presence
    // without asking for the UI, so fail closed exactly like the first one.
    if (wm_presence_ == 0) {
        wm_presence_ = RegisterWindowMessageW(config_.presence_message_name.c_str());
        if (wm_presence_ == 0) {
            last_error_ = GetLastError();
            ptd::log_write(ptd::LogLevel::Error, "single_instance: RegisterWindowMessageW(presence) failed with error " + std::to_string(last_error_));
            return ClaimStatus::Error;
        }
    }

    // Activation transport first (I4): an owner always establishes the
    // transport before the ownership attempt. CreateEventW on an existing
    // object keeps its previous signaling state, so the new owner resets
    // the safety-critical bits explicitly below.
    SetLastError(ERROR_SUCCESS);
    ready_event_ = CreateEventW(nullptr, TRUE, FALSE, config_.ready_event_name.c_str());
    if (!ready_event_) {
        last_error_ = GetLastError();
        ptd::log_write(ptd::LogLevel::Error, "single_instance: CreateEventW ready_event failed with error " + std::to_string(last_error_));
        return ClaimStatus::Error;
    }

    SetLastError(ERROR_SUCCESS);
    activate_event_ = CreateEventW(nullptr, FALSE, FALSE, config_.activate_event_name.c_str());
    if (!activate_event_) {
        last_error_ = GetLastError();
        ptd::log_write(ptd::LogLevel::Error, "single_instance: CreateEventW activate_event failed with error " + std::to_string(last_error_));
        CloseHandle(ready_event_);
        ready_event_ = nullptr;
        return ClaimStatus::Error;
    }

    SetLastError(ERROR_SUCCESS);
    shutdown_event_ = CreateEventW(nullptr, TRUE, FALSE, config_.shutdown_event_name.c_str());
    if (!shutdown_event_) {
        last_error_ = GetLastError();
        ptd::log_write(ptd::LogLevel::Error, "single_instance: CreateEventW shutdown_event failed with error " + std::to_string(last_error_));
        CloseHandle(activate_event_);
        activate_event_ = nullptr;
        CloseHandle(ready_event_);
        ready_event_ = nullptr;
        return ClaimStatus::Error;
    }

    // Real ownership semantics (I1): bInitialOwner=TRUE grants ownership
    // atomically at creation. ERROR_ALREADY_EXISTS means the existing
    // mutex stays owned by the other process -- this object becomes a
    // claimant that keeps the mutex handle as its later takeover gate.
    SetLastError(ERROR_SUCCESS);
    mutex_ = CreateMutexW(nullptr, TRUE, config_.mutex_name.c_str());
    if (mutex_ == nullptr) {
        last_error_ = GetLastError();
        ptd::log_write(ptd::LogLevel::Error, "single_instance: CreateMutexW failed with error " + std::to_string(last_error_));
        CloseHandle(shutdown_event_);
        shutdown_event_ = nullptr;
        CloseHandle(activate_event_);
        activate_event_ = nullptr;
        CloseHandle(ready_event_);
        ready_event_ = nullptr;
        return ClaimStatus::Error;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Claimant: drop the transport handles (the live owner keeps the
        // objects alive), keep the unowned mutex handle for the bounded
        // handoff, and never become owner.
        CloseHandle(shutdown_event_);
        shutdown_event_ = nullptr;
        CloseHandle(activate_event_);
        activate_event_ = nullptr;
        CloseHandle(ready_event_);
        ready_event_ = nullptr;
        is_owner_ = false;
        return ClaimStatus::AlreadyRunning;
    }

    // Fresh owner: reset stale signaling deterministically while owning
    // (I4). The activation event is intentionally left as-is: retained
    // activation intent survives an owner death and is consumed by the
    // next owner's startup path.
    if (!call_reset_event(ready_event_)) {
        last_error_ = GetLastError();
        ptd::log_write(ptd::LogLevel::Error, "single_instance: ResetEvent ready_event failed in claim with error " + std::to_string(last_error_));
        CloseHandle(shutdown_event_);
        shutdown_event_ = nullptr;
        CloseHandle(activate_event_);
        activate_event_ = nullptr;
        CloseHandle(ready_event_);
        ready_event_ = nullptr;
        ReleaseMutex(mutex_);
        CloseHandle(mutex_);
        mutex_ = nullptr;
        is_owner_ = false;
        return ClaimStatus::Error;
    }
    if (!call_reset_event(shutdown_event_)) {
        last_error_ = GetLastError();
        ptd::log_write(ptd::LogLevel::Error, "single_instance: ResetEvent shutdown_event failed in claim with error " + std::to_string(last_error_));
        CloseHandle(shutdown_event_);
        shutdown_event_ = nullptr;
        CloseHandle(activate_event_);
        activate_event_ = nullptr;
        CloseHandle(ready_event_);
        ready_event_ = nullptr;
        ReleaseMutex(mutex_);
        CloseHandle(mutex_);
        mutex_ = nullptr;
        is_owner_ = false;
        return ClaimStatus::Error;
    }
    is_owner_ = true;
    owner_state_ = OwnerState::Starting;
    return ClaimStatus::FirstOwner;
}

bool SingleInstance::shutdown_signaled() {
    HANDLE sd = OpenEventW(SYNCHRONIZE, FALSE, config_.shutdown_event_name.c_str());
    if (!sd) return false; // no beacon observable: not a shutdown signal
    const DWORD w = WaitForSingleObject(sd, 0);
    const BOOL ok = CloseHandle(sd);
    if (!ok) {
        last_error_ = GetLastError();
    }
    return w == WAIT_OBJECT_0;
}

bool SingleInstance::complete_takeover(bool abandoned) {
    // This process just acquired the protocol mutex (released or abandoned
    // by the previous owner). Rebuild the transport under ownership (I4).
    SetLastError(ERROR_SUCCESS);
    ready_event_ = CreateEventW(nullptr, TRUE, FALSE, config_.ready_event_name.c_str());
    activate_event_ = CreateEventW(nullptr, FALSE, FALSE, config_.activate_event_name.c_str());
    shutdown_event_ = CreateEventW(nullptr, TRUE, FALSE, config_.shutdown_event_name.c_str());
    if (!ready_event_ || !activate_event_ || !shutdown_event_) {
        last_error_ = GetLastError();
        ptd::log_write(ptd::LogLevel::Error, "single_instance: transport rebuild after handoff failed with error " + std::to_string(last_error_));
        if (ready_event_) { CloseHandle(ready_event_); ready_event_ = nullptr; }
        if (activate_event_) { CloseHandle(activate_event_); activate_event_ = nullptr; }
        if (shutdown_event_) { CloseHandle(shutdown_event_); shutdown_event_ = nullptr; }
        ReleaseMutex(mutex_);
        CloseHandle(mutex_);
        mutex_ = nullptr;
        is_owner_ = false;
        return false;
    }
    if (!call_reset_event(ready_event_)) {
        last_error_ = GetLastError();
        ptd::log_write(ptd::LogLevel::Error, "single_instance: ResetEvent ready_event failed in complete_takeover with error " + std::to_string(last_error_));
        CloseHandle(ready_event_); ready_event_ = nullptr;
        CloseHandle(activate_event_); activate_event_ = nullptr;
        CloseHandle(shutdown_event_); shutdown_event_ = nullptr;
        ReleaseMutex(mutex_);
        CloseHandle(mutex_);
        mutex_ = nullptr;
        is_owner_ = false;
        return false;
    }
    if (!call_reset_event(shutdown_event_)) {
        last_error_ = GetLastError();
        ptd::log_write(ptd::LogLevel::Error, "single_instance: ResetEvent shutdown_event failed in complete_takeover with error " + std::to_string(last_error_));
        CloseHandle(ready_event_); ready_event_ = nullptr;
        CloseHandle(activate_event_); activate_event_ = nullptr;
        CloseHandle(shutdown_event_); shutdown_event_ = nullptr;
        ReleaseMutex(mutex_);
        CloseHandle(mutex_);
        mutex_ = nullptr;
        is_owner_ = false;
        return false;
    }
    is_owner_ = true;
    owner_state_ = OwnerState::Starting;
    ptd::log_write(ptd::LogLevel::Info, std::string("single_instance: ownership reclaimed after previous owner ") + (abandoned ? "abnormally terminated" : "released") + "; starting ProTrail");
    return true;
}

NotifyStatus SingleInstance::notify_existing_owner(DWORD wait_ready_ms,
                                                   DWORD ack_timeout_ms,
                                                   NotifyIntent intent) {
    // A shutdown-signaled owner cannot receive activation: readiness is
    // already revoked and the receiver is going away. Route the claimant
    // to the deterministic handoff immediately.
    if (shutdown_signaled()) {
        return NotifyStatus::OwnerShuttingDown;
    }

    if (wm_activate_ == 0) {
        wm_activate_ = RegisterWindowMessageW(config_.message_name.c_str());
        if (wm_activate_ == 0) {
            last_error_ = GetLastError();
            ptd::log_write(ptd::LogLevel::Error, "single_instance: RegisterWindowMessageW failed in notify with error " + std::to_string(last_error_));
            return NotifyStatus::Error;
        }
    }
    if (wm_presence_ == 0) {
        wm_presence_ = RegisterWindowMessageW(config_.presence_message_name.c_str());
    }

    // 1) Durable activation intent: set while the owner may still be
    //    initializing. The owner consumes this event once its receiver is
    //    ready, so activation that arrives during startup is not lost
    //    (T-018R1 behavior preserved).
    //
    //    T-032: a QUIET request deliberately does NOT set it. That event is
    //    consumed by Application::run as "open Settings now", so setting it
    //    from an autostart launch would make an owner that is still starting
    //    pop its window -- exactly the behaviour a silent launch forbids.
    if (intent == NotifyIntent::Interactive) {
        SetLastError(ERROR_SUCCESS);
        HANDLE act_evt = OpenEventW(EVENT_MODIFY_STATE, FALSE, config_.activate_event_name.c_str());
        if (!act_evt) {
            last_error_ = GetLastError();
            ptd::log_write(ptd::LogLevel::Error, "single_instance: OpenEventW activate_event failed with error " + std::to_string(last_error_));
            return NotifyStatus::Error;
        }
        const BOOL set_ok = SetEvent(act_evt);
        const DWORD set_err = GetLastError();
        CloseHandle(act_evt);
        if (!set_ok) {
            last_error_ = set_err;
            ptd::log_write(ptd::LogLevel::Error, "single_instance: SetEvent(activate) failed with error " + std::to_string(set_err));
            return NotifyStatus::Error;
        }
    }

    // 2) Wait for readiness while watching BOTH the shutdown beacon and
    //    the protocol mutex (I6): if the owner begins shutdown or dies
    //    while starting, this claimant routes to the handoff instead of
    //    ever returning a blind OwnerStarting for a reclaimable protocol.
    SetLastError(ERROR_SUCCESS);
    HANDLE rdy_evt = OpenEventW(SYNCHRONIZE, FALSE, config_.ready_event_name.c_str());
    if (!rdy_evt) {
        last_error_ = GetLastError();
        ptd::log_write(ptd::LogLevel::Error, "single_instance: OpenEventW ready_event failed with error " + std::to_string(last_error_));
        return NotifyStatus::Error;
    }
    HANDLE sd_evt = OpenEventW(SYNCHRONIZE, FALSE, config_.shutdown_event_name.c_str());
    // W2-001: compact the wait set by appending only real handles, so the
    // mutex stays watched whenever it exists -- independent of whether the
    // optional shutdown beacon could be opened.
    const WaitSet wait = build_wait_set(rdy_evt, sd_evt, mutex_);
    const DWORD handle_count = wait.count;
    const int mutex_index = wait.mutex_index;
    const int shutdown_index = wait.shutdown_index;
    const DWORD readiness_wait = WaitForMultipleObjects(
        handle_count, wait.handles, FALSE, wait_ready_ms);
    CloseHandle(rdy_evt);
    if (sd_evt) CloseHandle(sd_evt);

    // Terminal outcomes for the readiness phase (I6): Ready, shutdown
    // beacon, or the owner releasing/abandoning the protocol mutex.
    if (readiness_wait == WAIT_OBJECT_0) {
        // readiness signaled; fall through to the delivery phase. The
        // owner may still have begun shutdown in the meantime -- a
        // revoked-readiness owner is never delivered to (I2).
        if (shutdown_signaled()) {
            return NotifyStatus::OwnerShuttingDown;
        }
    } else if (shutdown_index >= 0
               && readiness_wait == WAIT_OBJECT_0 + static_cast<DWORD>(shutdown_index)) {
        return NotifyStatus::OwnerShuttingDown;
    } else if (mutex_index >= 0
               && readiness_wait == WAIT_OBJECT_0 + static_cast<DWORD>(mutex_index)) {
        // Owner released (clean exit) while this claimant waited for
        // readiness: become the owner (I6). The multi-object wait already
        // acquired the mutex.
        return complete_takeover(false)
                   ? NotifyStatus::OwnershipReclaimed
                   : NotifyStatus::Error;
    } else if (readiness_wait >= WAIT_ABANDONED_0
               && readiness_wait < WAIT_ABANDONED_0 + static_cast<DWORD>(handle_count)
               && readiness_wait - WAIT_ABANDONED_0 == static_cast<DWORD>(mutex_index)) {
        // Multi-object abandonment report (WAIT_ABANDONED_0 + index): the
        // owner DIED owning the protocol. This process was granted
        // ownership -- reclaim it (I6, Defect A closed at protocol level:
        // never a blind OwnerStarting for a reclaimable protocol).
        return complete_takeover(true)
                   ? NotifyStatus::OwnershipReclaimed
                   : NotifyStatus::Error;
    } else if (readiness_wait == WAIT_TIMEOUT) {
        if (shutdown_signaled()) return NotifyStatus::OwnerShuttingDown;
        if (wait_ready_ms == 0) return NotifyStatus::Timeout;
        // Owner still owns the protocol (mutex watched the whole
        // budget): positive liveness evidence behind this status.
        ptd::log_write(ptd::LogLevel::Warn, "single_instance: owner not ready within " + std::to_string(wait_ready_ms) + " ms; activation intent retained");
        return NotifyStatus::OwnerStarting;
    } else {
        last_error_ = GetLastError();
        ptd::log_write(ptd::LogLevel::Error, "single_instance: readiness wait failed (raw=" + std::to_string(readiness_wait) + ") with error " + std::to_string(last_error_));
        return NotifyStatus::Error;
    }

    // 3) Delivery request: unique per-request ACK event (I5).
    const std::uint64_t request_id = make_request_id();
    SetLastError(ERROR_SUCCESS);
    HANDLE ack_evt = CreateEventW(nullptr, TRUE, FALSE, ack_event_name(request_id).c_str());
    if (!ack_evt) {
        last_error_ = GetLastError();
        ptd::log_write(ptd::LogLevel::Error, "single_instance: CreateEventW ack failed with error " + std::to_string(last_error_));
        return NotifyStatus::Error;
    }
    last_request_id_.store(request_id, std::memory_order_release);

    if (shutdown_signaled()) {
        CloseHandle(ack_evt);
        return NotifyStatus::OwnerShuttingDown;
    }

    const WPARAM wparam = static_cast<WPARAM>(request_id & 0xFFFFFFFFu);
    const LPARAM lparam = static_cast<LPARAM>((request_id >> 32) & 0xFFFFFFFFu);
    // T-032: the message IS the intent -- the owner handler for the quiet
    // channel never touches a window. A quiet notify whose presence message
    // could not be registered degrades to Error rather than to an
    // activation the user never asked for.
    const UINT channel = (intent == NotifyIntent::Quiet) ? wm_presence_ : wm_activate_;
    if (channel == 0) {
        last_error_ = ERROR_INVALID_PARAMETER;
        ptd::log_write(ptd::LogLevel::Error, "single_instance: requested notify channel is not registered");
        CloseHandle(ack_evt);
        return NotifyStatus::Error;
    }
    SetLastError(ERROR_SUCCESS);
    if (!PostMessageW(HWND_BROADCAST, channel, wparam, lparam)) {
        last_error_ = GetLastError();
        ptd::log_write(ptd::LogLevel::Error, "single_instance: PostMessageW(HWND_BROADCAST) failed with error " + std::to_string(last_error_));
        CloseHandle(ack_evt);
        return NotifyStatus::Error;
    }

    // 4) Bounded terminal-outcome wait (I6): the request is delivered only
    //    when ITS ack event is signaled by the owner. Owner shutdown or
    //    owner death routes to the handoff; a bare timeout is never
    //    reported as delivered.
    HANDLE sd_evt2 = OpenEventW(SYNCHRONIZE, FALSE, config_.shutdown_event_name.c_str());
    // W2-001: the ack wait uses the SAME compact construction, so the mutex is
    // watched even when the shutdown beacon is unavailable.
    const WaitSet ack_set = build_wait_set(ack_evt, sd_evt2, mutex_);
    const DWORD ack_count = ack_set.count;
    const int ack_mutex_index = ack_set.mutex_index;
    const int ack_shutdown_index = ack_set.shutdown_index;
    const DWORD ack_wait = WaitForMultipleObjects(
        ack_count, ack_set.handles, FALSE, ack_timeout_ms);
    CloseHandle(ack_evt);
    if (sd_evt2) CloseHandle(sd_evt2);

    if (ack_wait == WAIT_OBJECT_0) {
        // The owner executed the activation callback and acknowledged THIS
        // request (log evidence emitted by acknowledge_request).
        return NotifyStatus::Delivered;
    }
    if (ack_shutdown_index >= 0
        && ack_wait == WAIT_OBJECT_0 + static_cast<DWORD>(ack_shutdown_index)) {
        return NotifyStatus::OwnerShuttingDown;
    }
    if (ack_mutex_index >= 0
        && ack_wait == WAIT_OBJECT_0 + static_cast<DWORD>(ack_mutex_index)) {
        // The owner's mutex became free during the ack wait (clean
        // release): take ownership instead of reporting delivery.
        return complete_takeover(false)
                   ? NotifyStatus::OwnershipReclaimed
                   : NotifyStatus::Error;
    }
    if (ack_wait >= WAIT_ABANDONED_0
        && ack_wait < WAIT_ABANDONED_0 + static_cast<DWORD>(ack_count)
        && ack_wait - WAIT_ABANDONED_0 == static_cast<DWORD>(ack_mutex_index)) {
        // Owner died owning the protocol during the ack wait: takeover.
        return complete_takeover(true)
                   ? NotifyStatus::OwnershipReclaimed
                   : NotifyStatus::Error;
    }
    if (ack_wait == WAIT_TIMEOUT) {
        ptd::log_write(ptd::LogLevel::Warn, "single_instance: activation request " + std::to_string(request_id) + " not acknowledged within " + std::to_string(ack_timeout_ms) + " ms; NOT claiming delivery");
        return NotifyStatus::Timeout;
    }
    last_error_ = GetLastError();
    ptd::log_write(ptd::LogLevel::Error, "single_instance: ack wait failed (raw=" + std::to_string(ack_wait) + ") with error " + std::to_string(last_error_));
    return NotifyStatus::Error;
}

bool SingleInstance::wait_for_ownership(DWORD timeout_ms) {
    if (is_owner_) return true;
    if (!mutex_) return false;

    // Deterministic handoff gate (I1): released by a cleanly exiting owner
    // or abandoned by a dead one. No sleeps, no polling, kernel-ordered.
    const DWORD w = WaitForSingleObject(mutex_, timeout_ms);
    if (w == WAIT_TIMEOUT) {
        last_error_ = WAIT_TIMEOUT;
        ptd::log_write(ptd::LogLevel::Warn, "single_instance: bounded handoff expired; a live owner still owns the protocol, launch exits without starting a second runtime");
        return false;
    }
    if (w != WAIT_OBJECT_0 && w != WAIT_ABANDONED) {
        last_error_ = GetLastError();
        ptd::log_write(ptd::LogLevel::Error, "single_instance: WaitForSingleObject(mutex) failed with error " + std::to_string(last_error_));
        return false;
    }
    return complete_takeover(w == WAIT_ABANDONED);
}

bool SingleInstance::mark_ready() {
    if (!is_owner_ || !ready_event_) {
        ptd::log_write(ptd::LogLevel::Error, "single_instance: mark_ready without ownership/ready_event");
        return false;
    }
    if (!SetEvent(ready_event_)) {
        last_error_ = GetLastError();
        ptd::log_write(ptd::LogLevel::Error, "single_instance: SetEvent(ready) failed with error " + std::to_string(last_error_));
        return false;
    }
    owner_state_ = OwnerState::Ready;
    return true;
}

bool SingleInstance::consume_startup_activation_request() {
    if (!activate_event_) return false;
    const DWORD res = WaitForSingleObject(activate_event_, 0);
    return res == WAIT_OBJECT_0;
}

void SingleInstance::begin_shutdown() {
    if (!is_owner_) return;
    owner_state_ = OwnerState::ShuttingDown;
    // I2 order: revoke the readiness advertisement FIRST, then raise the
    // takeover beacon. After this call no claimant can interpret this
    // owner as activation-ready, and claimants have a deterministic gate.
    if (ready_event_) {
        if (!call_reset_event(ready_event_)) {
            last_error_ = GetLastError();
            ptd::log_write(ptd::LogLevel::Error, "single_instance: ResetEvent ready_event failed in begin_shutdown with error " + std::to_string(last_error_));
            CloseHandle(ready_event_);
            ready_event_ = nullptr;
        }
    }
    if (shutdown_event_) SetEvent(shutdown_event_);
    ptd::log_write(ptd::LogLevel::Info, "single_instance: owner shutdown began; readiness revoked");
}

void SingleInstance::release() {
    // Defensive revocation first (I2): a teardown that skipped
    // begin_shutdown() still cannot leave "ready" advertised.
    if (ready_event_) {
        if (!call_reset_event(ready_event_)) {
            last_error_ = GetLastError();
            ptd::log_write(ptd::LogLevel::Error, "single_instance: ResetEvent ready_event failed in release with error " + std::to_string(last_error_));
        }
    }
    if (is_owner_) owner_state_ = OwnerState::ShuttingDown;

    if (activate_event_) {
        CloseHandle(activate_event_);
        activate_event_ = nullptr;
    }
    if (ready_event_) {
        CloseHandle(ready_event_);
        ready_event_ = nullptr;
    }
    if (shutdown_event_) {
        CloseHandle(shutdown_event_);
        shutdown_event_ = nullptr;
    }
    // Ownership relinquished LAST: no claimant can acquire the mutex while
    // any transport handle of this owner still exists.
    if (mutex_) {
        if (is_owner_) ReleaseMutex(mutex_);
        CloseHandle(mutex_);
        mutex_ = nullptr;
    }
    is_owner_ = false;
}

} // namespace ptd
