#pragma once

#include "cursor_sample.h"

#include <cstddef>
#include <cstdint>
#include <deque>

namespace ptd {

// Bounded, time-ordered cursor sample storage for effect layers.
// Single-threaded by design: samples are produced and consumed on the
// UI/message thread that owns the raw-input window.
class CursorHistory {
public:
    // Explicit push outcome (T-007R A4). The previous size-delta inference
    // misclassified as "coalesced" any push into a FULL history that trimmed
    // an old sample (size unchanged). Callers must branch on this result,
    // not on size comparison.
    enum class PushResult {
        Inserted,   // new sample stored (history may have trimmed its front)
        Coalesced,  // movement-only duplicate of the newest sample; newest ts won
    };

    // max_samples must be > 0; default 512.
    // max_age_ns == 0 disables age-based pruning (default).
    explicit CursorHistory(std::size_t max_samples = 512, int64_t max_age_ns = 0);

    CursorHistory(const CursorHistory&) = delete;
    CursorHistory& operator=(const CursorHistory&) = delete;

    // Inserts a sample. Movement-only duplicates of the newest sample's
    // position coalesce into that sample (newest timestamp wins). Button
    // transitions are never coalesced away. Out-of-order timestamps are
    // clamped to the newest stored timestamp to preserve ordering.
    // Returns the explicit outcome; never infer it from size().
    PushResult push(const CursorSample& sample);

    // Drops samples older than now - max_age_ns. No-op when max_age_ns == 0.
    void prune_before(int64_t now_ns);

    // Convenience: age pruning followed by size trimming.
    void prune(int64_t now_ns);

    void clear();

    std::size_t size() const { return samples_.size(); }
    bool empty() const { return samples_.empty(); }
    const CursorSample& at(std::size_t index) const;
    const CursorSample& last() const;
    const std::deque<CursorSample>& samples() const { return samples_; }

    std::size_t max_samples() const { return max_samples_; }
    int64_t max_age_ns() const { return max_age_ns_; }

private:
    void trim_size();

    std::size_t max_samples_;
    int64_t max_age_ns_;
    std::deque<CursorSample> samples_;
};

} // namespace ptd