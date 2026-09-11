#pragma once

#include <functional>

namespace ptd {

// Coalesces a burst into one pending deferred operation. `post` must arrange
// asynchronous execution (Qt QTimer::singleShot(0) in production); `work`
// may destroy the notification source only after its WndProc returned.
class DeferredCoalescer {
public:
    bool request(const std::function<void(std::function<void()>)>& post,
                 std::function<void()> work) {
        if (pending_) return false;
        pending_ = true;
        post([this, work = std::move(work)] {
            pending_ = false;
            work();
        });
        return true;
    }

    bool pending() const { return pending_; }
    void cancel() { pending_ = false; }

private:
    bool pending_ = false;
};

} // namespace ptd
