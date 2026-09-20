#pragma once

#include <cstdint>
#include <functional>
#include <memory>

class QChronoTimer;
class QObject;

namespace ptd {

enum class SchedulerState {
    Idle,            // Timer stopped. Zero wakeups.
    Active,          // Timer running at target cadence. Rendering frames.
    PresentingClear  // Content expired; presenting 1 final transparent clear frame before entering Idle.
};

enum class FrameAction {
    Skip,            // Too early or master disabled.
    RenderContent,   // Content is alive, render normal frame.
    RenderClear,     // Content just died, render final transparent clear frame.
    Sleep            // Final clear frame presented, stop timer and sleep.
};

struct RenderMetrics {
    uint64_t total_frames = 0;
    uint64_t total_bursts = 0;
    uint64_t dropped_frames = 0;
    int64_t total_active_ns = 0;
    int64_t current_burst_start_ns = 0;
    int64_t current_burst_duration_ns = 0;

    double target_fps = 60.0;
    double measured_fps = 0.0;
    double avg_frame_time_us = 0.0;
    double peak_frame_time_us = 0.0;
    size_t working_set_bytes = 0;
};

// Formal active-only render scheduler (MVP 07).
// Controls frame pacing near the monitor's active refresh rate,
// guarantees 0 wakeups when idle, stable delta-time calculation,
// and samples performance counters.
class RenderScheduler {
public:
    using FrameCallback = std::function<void(int64_t frame_time_ns, double delta_time_s, FrameAction action)>;
    using ContentCheck = std::function<bool(int64_t now_ns)>;

    explicit RenderScheduler(QObject* parent = nullptr);
    ~RenderScheduler();

    RenderScheduler(const RenderScheduler&) = delete;
    RenderScheduler& operator=(const RenderScheduler&) = delete;

    // Query hardware monitor refresh rate via Win32 EnumDisplaySettings.
    // Clamped to [30, 360] Hz; fallback default is 60 Hz.
    static int detect_display_refresh_rate();

    // Configure target frame rate. If <= 0, automatically detects from display.
    void set_target_fps(int fps);
    int target_fps() const { return target_fps_; }
    int frame_interval_ms() const { return frame_interval_ms_; }
    int64_t frame_interval_ns() const { return frame_interval_ns_; }

    // Start/stop scheduler callback.
    void set_frame_callback(FrameCallback cb) { callback_ = std::move(cb); }

    // Set callback to check if effects have live content.
    void set_content_check(ContentCheck fn) { content_check_ = std::move(fn); }

    // Diagnostics opt-in (PROTRAIL_PERF_DIAG=1 or PROTRAIL_RENDER_DIAG=1).
    void set_diag_enabled(bool enabled) { diag_enabled_ = enabled; }
    bool diag_enabled() const { return diag_enabled_; }

    // Wake the scheduler when new input or effect activity arrives.
    // Transitions Idle -> Active, starts QTimer, and renders immediate frame.
    void wake();

    // Evaluate tick. Called by QTimer or immediate wake.
    // `has_live_content`: true if trail or click bubbles are active.
    void on_timer_tick(bool has_live_content);

    // Force stops the scheduler immediately (e.g. on master disable or shutdown).
    // Presents no clear; caller should do clear or reset state.
    void force_idle();

    SchedulerState state() const { return state_; }
    bool is_active() const { return state_ != SchedulerState::Idle; }

    const RenderMetrics& metrics() const { return metrics_; }

    // Frame timing hooks for instrumentation:
    void begin_frame();
    void end_frame();

private:
    void transition_to(SchedulerState new_state);
    void update_pacing_interval();
    void schedule_timer();
    void emit_perf_diag_if_due(int64_t now_ns);
    void sample_memory();

    SchedulerState state_ = SchedulerState::Idle;
    int target_fps_ = 60;
    int frame_interval_ms_ = 16;
    int64_t frame_interval_ns_ = 16'666'666;

    QChronoTimer* timer_ = nullptr;
    FrameCallback callback_;
    ContentCheck content_check_;
    bool diag_enabled_ = false;

    int64_t last_frame_time_ns_ = 0;
    int64_t frame_begin_ns_ = 0;

    RenderMetrics metrics_{};

    // Rolling 1-second window stats for FPS & duration
    int64_t window_start_ns_ = 0;
    uint32_t window_frame_count_ = 0;
    double window_frame_time_sum_us_ = 0.0;
    double window_peak_us_ = 0.0;
};

} // namespace ptd
