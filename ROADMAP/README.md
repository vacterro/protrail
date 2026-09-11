# ProTrail Roadmap Package

ProTrail is a native Windows cursor-effects application.

Primary MVP visual target:

1. A smooth, thin cursor trail.
2. A clean click bubble/ripple at the click point.
3. No particles, sparks, triangles, glow storms, or physics gimmicks in the MVP.
4. Native Windows rendering with low latency and near-zero idle cost.
5. A configurable desktop GUI.

## Chosen stack

- Language: C++20 or newer
- Platform: Windows 10/11 x64
- Windowing/input: Win32
- Rendering: Direct2D
- Composition: DirectComposition
- Settings GUI: Qt 6 Widgets
- Build system: CMake
- Compiler: MSVC
- Configuration: JSON
- Package target: portable build first, installer later

## Core architecture

```text
Windows Input
    |
    v
Cursor Sampler
    |
    v
Cursor History
    |
    +--------> Trail Effect
    |
    +--------> Click Bubble Effect
                    |
                    v
               Effect Engine
                    |
                    v
              Direct2D Renderer
                    |
                    v
             DirectComposition
                    |
                    v
       Transparent Per-Monitor Overlay

Qt Widgets GUI
    |
    v
Settings Store
    |
    v
Runtime Config Snapshot
    |
    v
Effect Engine
```

## Execution rule

The agent must execute the roadmap strictly file-by-file.

Start with `AGENT_CONTRACT.md`, then process the MVP milestones in filename order.

Do not skip ahead merely because later work looks more interesting.

The MVP stop gate is after milestone `09_TRAY.md`.

Everything in `roadmap/future/` is backlog and must not be implemented before the MVP gate is passed unless the user explicitly lifts that gate.

## MVP completion definition

The first usable ProTrail build must:

- show a smooth cursor trail globally;
- show a click bubble globally;
- remain click-through and non-activating;
- work across multiple monitors;
- expose live settings;
- persist settings safely;
- live in the system tray;
- have no visible cursor/input lag;
- consume negligible resources when idle;
- recover cleanly from display changes and renderer recreation;
- not require administrator privileges.

The project should become boringly reliable before it becomes visually extravagant.
