# ProTrail Quality Gates

## Gate A - Build hygiene

Required for every milestone:

- clean configure succeeds;
- clean build succeeds;
- no new compiler errors;
- new warnings must be understood;
- shutdown does not hang;
- no debug-only dependency is required for release behavior.

## Gate B - Overlay correctness

Required once overlay exists:

- click-through;
- no focus stealing;
- no Alt+Tab entry;
- no taskbar entry;
- correct transparency;
- correct monitor bounds;
- normal apps underneath remain interactive.

## Gate C - Visual correctness

Required once effects exist:

- trail follows cursor path without visible teleporting;
- trail decay is time-based;
- click bubble starts at the actual click position;
- effect animation is stable at different refresh rates;
- no stale trails remain after long idle.

## Gate D - Performance

Required before MVP is considered stable:

Idle:
- CPU approximately 0% in normal observation;
- GPU approximately 0% when no effect is active;
- no continuous high-frequency render loop while idle.

Active:
- no visible cursor/input lag;
- stable frame pacing;
- bounded cursor history;
- bounded active effect count;
- no unbounded allocations per frame.

## Gate E - Multi-monitor

- monitor left of primary works;
- monitor right of primary works;
- monitor above/below works if available;
- mixed DPI is handled;
- crossing monitor boundaries does not break the trail;
- display hotplug/topology change does not require app restart.

## Gate F - Persistence

- settings survive restart;
- malformed config does not crash startup;
- failed save does not destroy the previous valid config;
- defaults can be restored.

## Gate G - MVP stop gate

Do not enter future roadmap implementation until all MVP milestones are CLOSED and the user explicitly approves continuation.
