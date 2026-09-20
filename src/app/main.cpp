#include "application.h"
#include "single_instance.h"
#include "startup_mode.h"
#include "startup_paths.h"

#include "../core/log.h"
#include "../platform/dpi_awareness.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    // T-032: how we were launched, decided ONCE from the real command line.
    // The registry command ProTrail writes carries kStartupMinimizedArgument,
    // so this is the only place that reads it.
    const ptd::StartupMode startup_mode = ptd::parse_startup_mode(GetCommandLineW());
    const bool silent_startup = ptd::startup_mode_is_silent(startup_mode);

    const auto paths = ptd::resolve_startup_paths();
    if (!paths.valid) {
        // T-017R2: fail closed if smoke auto-exit is armed without an isolated state dir
        return 2;
    }

    // T-018R1: the log file exists before the single-instance protocol
    // runs, so claim/notify evidence is durable, not only on stdout.
    ptd::log_init(paths.log_path);
    if (!ptd::dpi::establish_process_contract())
        ptd::log_write(ptd::LogLevel::Warn,
                       "dpi: physical-pixel contract could not be verified");

    ptd::log_write(ptd::LogLevel::Info, silent_startup
        ? "startup: autostart launch detected; silent tray-only mode"
        : "startup: manual launch");

    ptd::SingleInstance single_instance;
    if (!paths.is_smoke_mode) {
        // T-018: enforce single running instance in normal mode
        const auto status = single_instance.claim();
        if (status == ptd::ClaimStatus::Error) {
            ptd::log_write(ptd::LogLevel::Error, "single_instance: claim failed; refusing to run production");
            return 1;
        }
        if (status == ptd::ClaimStatus::AlreadyRunning) {
            // T-018R3: an activation outcome is terminal and evidenced.
            // Delivered = the owner processed the activation callback and
            // acknowledged THIS request. Every other outcome routes through
            // the bounded mutex handoff gate: if the previous owner has
            // shut down, died, or released the protocol, THIS launch takes
            // ownership; only a positively-live owner (still owning the
            // mutex after the bounded budget) ends the launch without a
            // second runtime. A claimant never exits merely because an
            // activation was queued but not processed.
            // T-032: a silent autostart launch asks for PRESENCE, not for
            // the UI. Sending an interactive activation here would make an
            // already-running ProTrail pop its window at sign-in, which is
            // exactly what a silent launch must not do. The request
            // identity and the ACK protocol are identical on both
            // channels; only the owner's window handling differs.
            const auto outcome = single_instance.notify_existing_owner(
                5000, 5000,
                silent_startup ? ptd::NotifyIntent::Quiet
                               : ptd::NotifyIntent::Interactive);
            switch (outcome) {
                case ptd::NotifyStatus::Delivered:
                    ptd::log_write(ptd::LogLevel::Info, silent_startup
                        ? "single_instance: presence acknowledged by existing owner; no window activated"
                        : "single_instance: activation acknowledged by existing owner");
                    return 0;
                case ptd::NotifyStatus::OwnershipReclaimed:
                    ptd::log_write(ptd::LogLevel::Info, "single_instance: previous owner gone during activation; ownership reclaimed");
                    break;
                case ptd::NotifyStatus::OwnerShuttingDown:
                    ptd::log_write(ptd::LogLevel::Warn, "single_instance: existing owner is shutting down; waiting for ownership handoff");
                    break;
                case ptd::NotifyStatus::OwnerStarting:
                    ptd::log_write(ptd::LogLevel::Info, "single_instance: existing owner still starting; durable activation intent retained; watching ownership");
                    break;
                case ptd::NotifyStatus::Timeout:
                    ptd::log_write(ptd::LogLevel::Warn, "single_instance: activation not acknowledged; watching ownership");
                    break;
                case ptd::NotifyStatus::Error:
                    ptd::log_write(ptd::LogLevel::Error, "single_instance: activation delivery failed; watching ownership");
                    break;
            }
            // Uniform authoritative gate (bounded, no infinite waits): a
            // released or abandoned mutex is reclaimed; a live owner keeps
            // ownership and covers the launch (durable intent / live
            // runtime), never a second runtime.
            if (!single_instance.wait_for_ownership(10000)) {
                return 0;
            }
            ptd::log_write(ptd::LogLevel::Info, "single_instance: ownership reclaimed; starting ProTrail");
        }
    }

    int rc = 0;
    {
        Application app(paths.config_path);
        app.set_startup_mode(startup_mode);
        if (!app.initialize()) {
            return 1;
        }
        if (!paths.is_smoke_mode) {
            app.set_single_instance(&single_instance);
        }
        rc = app.run();
        app.shutdown();
    }

    return rc;
}
