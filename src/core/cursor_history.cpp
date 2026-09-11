#include "cursor_history.h"

namespace ptd {

CursorHistory::CursorHistory(std::size_t max_samples, int64_t max_age_ns)
    : max_samples_(max_samples > 0 ? max_samples : 512), max_age_ns_(max_age_ns) {}

CursorHistory::PushResult CursorHistory::push(const CursorSample& sample) {
    // Clamp out-of-order timestamps so storage stays monotonic. Zero and
    // negative deltas are allowed for a single pair; only ordering is
    // enforced.
    CursorSample s = sample;
    if (!samples_.empty() && s.timestamp_ns < samples_.back().timestamp_ns) {
        s.timestamp_ns = samples_.back().timestamp_ns;
    }

    const bool is_button = s.action != ButtonAction::None;

    // Coalesce duplicate movement: the newest sample already records this
    // position. Button transitions at a stationary cursor are preserved.
    if (!is_button && !samples_.empty()
        && samples_.back().action == ButtonAction::None
        && samples_.back().x == s.x && samples_.back().y == s.y) {
        samples_.back().timestamp_ns = s.timestamp_ns;
        return PushResult::Coalesced;
    }

    samples_.push_back(s);
    trim_size();
    return PushResult::Inserted;
}

void CursorHistory::prune_before(int64_t now_ns) {
    if (max_age_ns_ <= 0) return;
    const int64_t cutoff = now_ns - max_age_ns_;
    while (!samples_.empty() && samples_.front().timestamp_ns < cutoff) {
        samples_.pop_front();
    }
}

void CursorHistory::prune(int64_t now_ns) {
    prune_before(now_ns);
    trim_size();
}

void CursorHistory::clear() { samples_.clear(); }

const CursorSample& CursorHistory::at(std::size_t index) const {
    return samples_[index];
}

const CursorSample& CursorHistory::last() const {
    return samples_.back();
}

void CursorHistory::trim_size() {
    while (samples_.size() > max_samples_) {
        samples_.pop_front();
    }
}

} // namespace ptd
