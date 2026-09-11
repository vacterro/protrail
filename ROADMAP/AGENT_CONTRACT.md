# ProTrail Agent Contract

Status: ACTIVE

This file defines how implementation work must be performed.

## 1. One milestone at a time

Process roadmap files in ascending filename order.

For each milestone:

1. Read the milestone completely.
2. Inspect the current repository before changing code.
3. Implement only the scope required by that milestone.
4. Run the milestone validation.
5. Fix all failures caused by the milestone.
6. Record completion evidence in the milestone file or project work log.
7. Mark the milestone CLOSED only after acceptance criteria pass.
8. Only then continue to the next milestone.

Do not partially implement three milestones in parallel.

## 2. Preserve vertical slices

Prefer working software over scaffolding.

A milestone is not complete because interfaces or TODOs exist. It is complete when the requested behavior is observable and tested.

## 3. No premature feature expansion

Before the MVP gate, do not add:

- particles;
- spark systems;
- triangle emitters;
- rainbow modes;
- physics trails;
- app-specific profiles;
- advanced hotkeys;
- plugin systems;
- scripting;
- networking;
- accounts;
- cloud sync;
- telemetry;
- services;
- kernel drivers;
- admin-only components.

If a useful idea appears, record it in backlog. Do not expand the current implementation target.

## 4. Rendering ownership

The renderer owns rendering.

The settings UI must not contain a second implementation of trail or click effects.

If a preview is added later, it must use the same effect logic or a deliberately shared rendering path.

## 5. Input ownership

The input layer gathers input and cursor samples.

Effects must consume normalized input events and cursor samples instead of talking directly to Win32 input APIs.

## 6. Configuration ownership

The UI edits settings.

A configuration layer validates and publishes runtime configuration.

The renderer/effect engine consumes a snapshot and must not read widgets directly.

## 7. Performance is a feature

Do not trade obvious frame pacing or idle efficiency for decorative effects.

The application should stop active rendering when no effect is alive and no repaint is needed.

## 8. No hidden privilege escalation

ProTrail must run as a normal user.

Do not add:

- Windows services;
- scheduled tasks;
- drivers;
- UAC requirements;
- global DLL injection.

## 9. Failure handling

If blocked by a real platform limitation:

1. Prove the limitation.
2. Record the exact failing path and evidence.
3. Implement the smallest safe fallback.
4. Do not silently replace the architecture with a different stack.

## 10. Completion evidence

Each closed milestone should leave evidence such as:

- build command and result;
- automated tests;
- manual verification checklist;
- performance measurement where relevant;
- screenshots or logs where relevant;
- known limitations.

"Builds on my machine" is not enough evidence.
