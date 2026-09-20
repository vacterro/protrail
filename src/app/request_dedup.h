#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

namespace ptd {

// W2-004: bounded recent-request identity tracking for the single-instance
// activation/presence channels.
//
// HWND_BROADCAST delivers one copy per top-level window, so a request must be
// processed exactly once per request id. A single last-ID scalar only dedupes
// the immediately previous id, so the valid sequence A, B, A executes A twice.
// This keeps a bounded recent set large enough to cover the maximum
// readiness/ACK/handoff window and broadcast fan-out. The two channels share
// one instance because they use one request-id namespace.
class RequestDedup {
public:
    static constexpr std::size_t kBound = 64;

    // True when this request id was already processed.
    bool already_processed(std::uint64_t request_id) const {
        for (std::uint64_t seen : recent_) {
            if (seen == request_id) return true;
        }
        return false;
    }

    // Record a request id as processed, evicting the oldest beyond kBound.
    void mark_processed(std::uint64_t request_id) {
        recent_.push_back(request_id);
        while (recent_.size() > kBound) {
            recent_.pop_front();
        }
    }

    // Clear/reinitialize with the Application lifetime.
    void clear() { recent_.clear(); }

    std::size_t size() const { return recent_.size(); }

private:
    std::deque<std::uint64_t> recent_;
};

} // namespace ptd
