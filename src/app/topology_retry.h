#pragma once

#include "../render/overlay_manager.h"

#include <QTimer>

#include <algorithm>
#include <functional>

namespace ptd {

// Owns the application's one bounded topology retry timer. The application
// supplies the deferred/coalesced refresh operation; this class owns only the
// backoff and convergence lifecycle so that policy runs can be tested without
// depending on physical display creation.
class TopologyRetry {
public:
    struct Policy {
        int max_attempts = 5;
        int base_delay_ms = 1000;
        int max_delay_ms = 30000;
    };

    using RetryDue = std::function<void()>;
    using AttemptScheduled = std::function<void(int, int)>;
    using Exhausted = std::function<void()>;

    explicit TopologyRetry(RetryDue retry_due, Policy policy = {},
                           AttemptScheduled attempt_scheduled = {},
                           Exhausted exhausted = {})
        : retry_due_(std::move(retry_due)),
          attempt_scheduled_(std::move(attempt_scheduled)),
          exhausted_(std::move(exhausted)), policy_(policy) {
        policy_.max_attempts = std::max(0, policy_.max_attempts);
        policy_.base_delay_ms = std::max(1, policy_.base_delay_ms);
        policy_.max_delay_ms = std::max(policy_.base_delay_ms,
                                        policy_.max_delay_ms);
        timer_.setSingleShot(true);
        timer_.setTimerType(Qt::CoarseTimer);
        QObject::connect(&timer_, &QTimer::timeout, &timer_, [this] {
            if (retry_due_) retry_due_();
        });
    }

    TopologyRetry(const TopologyRetry&) = delete;
    TopologyRetry& operator=(const TopologyRetry&) = delete;

    void observe(OverlayManager::Convergence result) {
        if (result == OverlayManager::Convergence::Complete ||
            result == OverlayManager::Convergence::Unchanged) {
            attempts_ = 0;
            timer_.stop();
            return;
        }

        if (attempts_ >= policy_.max_attempts) {
            timer_.stop();
            if (exhausted_) exhausted_();
            return;
        }

        const int shift = std::min(attempts_, 30);
        const auto scaled = static_cast<long long>(policy_.base_delay_ms) << shift;
        const int delay = static_cast<int>(std::min<long long>(scaled,
                                                               policy_.max_delay_ms));
        ++attempts_;
        if (attempt_scheduled_) attempt_scheduled_(attempts_, delay);
        timer_.start(delay);
    }

    void cancel() { timer_.stop(); }
    bool pending() const { return timer_.isActive(); }
    int attempts() const { return attempts_; }

private:
    RetryDue retry_due_;
    AttemptScheduled attempt_scheduled_;
    Exhausted exhausted_;
    Policy policy_;
    QTimer timer_;
    int attempts_ = 0;
};

} // namespace ptd
