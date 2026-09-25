#include "render_scheduler.h"
#include "../core/log.h"
#include "../platform/mouse_input.h"

#include <QChronoTimer>
#include <chrono>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <psapi.h>

#include <algorithm>
#include <cstdio>

namespace ptd {

RenderScheduler::RenderScheduler(QObject* parent) {
    // PERF-004: QChronoTimer (Qt 6.8) schedules the EXACT chrono interval.
    // The previous code computed a nanosecond interval and then discarded it
    // for a rounded integer-millisecond start(), so 60 Hz ran at ~58.8 Hz and
    // 120 Hz at 125 Hz. frame_interval_ns_ is the pacing authority now.
    timer_ = new QChronoTimer(parent);
    timer_->setTimerType(Qt::PreciseTimer);
    QObject::connect(timer_, &QChronoTimer::timeout, [this]() {
        const int64_t now = now_ns();
        const bool live = content_check_ ? content_check_(now) : false;
        on_timer_tick(live);
    });

    // Initial detection of display refresh rate
    set_target_fps(detect_display_refresh_rate());
}

RenderScheduler::~RenderScheduler() {
    force_idle();
}

int RenderScheduler::detect_display_refresh_rate() {
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm)) {
        if (dm.dmDisplayFrequency >= 30 && dm.dmDisplayFrequency <= 360) {
            return static_cast<int>(dm.dmDisplayFrequency);
        }
    }
    return 60; // Safe default
}

void RenderScheduler::set_target_fps(int fps) {
    if (fps <= 0) {
        fps = detect_display_refresh_rate();
    }
    const int next_target_fps = std::clamp(fps, 30, 360);
    if (next_target_fps == target_fps_) return;
    target_fps_ = next_target_fps;
    metrics_.target_fps = target_fps_;
    update_pacing_interval();
}

void RenderScheduler::update_pacing_interval() {
    frame_interval_ns_ = 1'000'000'000LL / target_fps_;
    // PERF-004: frame_interval_ms_ remains a diagnostic/legacy accessor only;
    // it no longer drives runtime scheduling (see schedule_timer()).
    frame_interval_ms_ = std::max(1, static_cast<int>(1000.0 / target_fps_ + 0.5));
    // QChronoTimer::setInterval() rearms an active timer. Do this only after
    // a real target transition; the current callback retains its old cadence.
    if (timer_ && state_ != SchedulerState::Idle) {
        timer_->setInterval(std::chrono::nanoseconds(frame_interval_ns_));
        ++timer_arm_count_;
    }
}

// PERF-004: schedule from the EXACT chrono interval. Active-only operation and
// the immediate first frame on wake() are both preserved by the call sites.
void RenderScheduler::schedule_timer() {
    timer_->setInterval(std::chrono::nanoseconds(frame_interval_ns_));
    timer_->start();
    ++timer_arm_count_;
}

void RenderScheduler::wake() {
    const int64_t now = now_ns();

    if (state_ == SchedulerState::Idle) {
        transition_to(SchedulerState::Active);
        metrics_.total_bursts++;
        metrics_.current_burst_start_ns = now;
        last_frame_time_ns_ = now;
        window_start_ns_ = now;
        window_frame_count_ = 0;
        window_frame_time_sum_us_ = 0.0;
        window_peak_us_ = 0.0;

        if (diag_enabled_) {
            log_write(LogLevel::Info, "render: active (scheduler wake)");
        }

        schedule_timer();

        // Render first frame immediately; do not wait for the first timer tick
        if (callback_) {
            const double dt = static_cast<double>(frame_interval_ns_) / 1'000'000'000.0;
            callback_(now, dt, FrameAction::RenderContent);
        }
        emit_perf_diag_if_due(now);
    } else if (state_ == SchedulerState::PresentingClear) {
        // Interrupted while clearing; re-enter active immediately
        transition_to(SchedulerState::Active);
    }
}

void RenderScheduler::on_timer_tick(bool has_live_content) {
    if (state_ == SchedulerState::Idle) {
        timer_->stop();
        return;
    }

    const int64_t now = now_ns();
    double dt = 0.0;
    if (last_frame_time_ns_ > 0) {
        dt = static_cast<double>(now - last_frame_time_ns_) / 1'000'000'000.0;
        // Clamp delta-time to avoid physics/animation explosion on stall
        if (dt < 0.0) dt = 0.0;
        if (dt > 0.1) dt = 0.1;
    } else {
        dt = static_cast<double>(frame_interval_ns_) / 1'000'000'000.0;
    }
    last_frame_time_ns_ = now;

    if (has_live_content) {
        if (state_ != SchedulerState::Active) {
            transition_to(SchedulerState::Active);
        }
        if (callback_) {
            callback_(now, dt, FrameAction::RenderContent);
        }
    } else {
        if (state_ == SchedulerState::Active) {
            // Content just died: transition to PresentingClear and draw exactly 1 transparent frame
            transition_to(SchedulerState::PresentingClear);
            if (callback_) {
                callback_(now, dt, FrameAction::RenderClear);
            }
        } else if (state_ == SchedulerState::PresentingClear) {
            // Final transparent frame was already presented; now sleep
            transition_to(SchedulerState::Idle);
            timer_->stop();
            metrics_.total_active_ns += (now - metrics_.current_burst_start_ns);
            metrics_.current_burst_duration_ns = now - metrics_.current_burst_start_ns;

            sample_memory();

            if (diag_enabled_) {
                const int64_t burst_ms = metrics_.current_burst_duration_ns / 1'000'000;
                char buf[256];
                snprintf(buf, sizeof(buf),
                         "render: idle (burst duration=%lldms, frames=%llu, ws=%.2fMB)",
                         static_cast<long long>(burst_ms),
                         static_cast<unsigned long long>(metrics_.total_frames),
                         static_cast<double>(metrics_.working_set_bytes) / (1024.0 * 1024.0));
                log_write(LogLevel::Info, buf);
            }

            if (callback_) {
                callback_(now, dt, FrameAction::Sleep);
            }
        }
    }

    emit_perf_diag_if_due(now);
}

void RenderScheduler::force_idle() {
    if (timer_) {
        timer_->stop();
    }
    state_ = SchedulerState::Idle;
    last_frame_time_ns_ = 0;
}

void RenderScheduler::transition_to(SchedulerState new_state) {
    state_ = new_state;
}

void RenderScheduler::begin_frame() {
    frame_begin_ns_ = now_ns();
}

void RenderScheduler::end_frame() {
    const int64_t end_ns = now_ns();
    const double duration_us = static_cast<double>(end_ns - frame_begin_ns_) / 1'000.0;

    metrics_.total_frames++;
    window_frame_count_++;
    window_frame_time_sum_us_ += duration_us;
    if (duration_us > window_peak_us_) {
        window_peak_us_ = duration_us;
    }
}

void RenderScheduler::sample_memory() {
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        metrics_.working_set_bytes = pmc.WorkingSetSize;
    }
}

void RenderScheduler::emit_perf_diag_if_due(int64_t now_ns) {
    if (window_start_ns_ == 0) {
        window_start_ns_ = now_ns;
        return;
    }
    const int64_t elapsed_ns = now_ns - window_start_ns_;
    if (elapsed_ns >= 1'000'000'000LL) {
        const double elapsed_s = static_cast<double>(elapsed_ns) / 1'000'000'000.0;
        metrics_.measured_fps = static_cast<double>(window_frame_count_) / elapsed_s;
        metrics_.avg_frame_time_us = (window_frame_count_ > 0)
            ? (window_frame_time_sum_us_ / window_frame_count_) : 0.0;
        metrics_.peak_frame_time_us = window_peak_us_;
        sample_memory();

        if (diag_enabled_) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "perf: fps=%.1f (target %dHz), frame_avg=%.2fms, frame_peak=%.2fms, bursts=%llu, ws=%.2fMB",
                     metrics_.measured_fps, target_fps_,
                     metrics_.avg_frame_time_us / 1000.0,
                     metrics_.peak_frame_time_us / 1000.0,
                     static_cast<unsigned long long>(metrics_.total_bursts),
                     static_cast<double>(metrics_.working_set_bytes) / (1024.0 * 1024.0));
            log_write(LogLevel::Info, buf);
        }

        window_start_ns_ = now_ns;
        window_frame_count_ = 0;
        window_frame_time_sum_us_ = 0.0;
        window_peak_us_ = 0.0;
    }
}

} // namespace ptd
