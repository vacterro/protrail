#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace ptd {

// Coalesces a burst into one pending deferred operation. `post` must arrange
// asynchronous execution (Qt QTimer::singleShot(0) in production); `work`
// may destroy the notification source only after its WndProc returned.
//
// W2-003: cancellation is real, not a flag reset. Posted work carries a
// generation and a shared state, so:
//   - cancel() invalidates the outstanding generation: its queued closure
//     becomes a no-op and can never execute after teardown;
//   - destruction invalidates every outstanding closure without requiring a
//     callback to dereference a destroyed DeferredCoalescer;
//   - a request accepted AFTER a cancel belongs to a new generation, and
//     draining the older closure neither clears nor executes the newer one.
class DeferredCoalescer {
public:
    DeferredCoalescer() : state_(std::make_shared<State>()) {}

    DeferredCoalescer(const DeferredCoalescer&) = delete;
    DeferredCoalescer& operator=(const DeferredCoalescer&) = delete;

    ~DeferredCoalescer() { cancel(); }

    bool request(const std::function<void(std::function<void()>)>& post,
                 std::function<void()> work) {
        if (state_->pending) return false;
        state_->pending = true;
        const std::uint64_t generation = state_->generation;
        std::shared_ptr<State> state = state_;
        post([state, generation, work = std::move(work)] {
            // A cancelled/superseded closure is a no-op: it must not run work
            // nor clear a newer generation's pending flag.
            if (generation != state->generation) return;
            state->pending = false;
            work();
        });
        return true;
    }

    bool pending() const { return state_->pending; }

    void cancel() {
        ++state_->generation;
        state_->pending = false;
    }

private:
    struct State {
        bool pending = false;
        std::uint64_t generation = 0;
    };
    std::shared_ptr<State> state_;
};

} // namespace ptd
