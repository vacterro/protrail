# ProTrail Architecture

## Process model

Use one normal desktop process for the MVP.

Do not introduce helper services or child daemons.

Recommended logical components:

```text
Application
|- Platform
|  |- WindowHost
|  |- MonitorManager
|  |- MouseInput
|  `- Timing
|
|- Core
|  |- CursorSampler
|  |- CursorHistory
|  |- EffectEngine
|  `- RuntimeConfig
|
|- Render
|  |- D2DDevice
|  |- CompositionHost
|  |- MonitorOverlay
|  `- RenderScheduler
|
|- Effects
|  |- TrailEffect
|  `- ClickBubbleEffect
|
|- UI
|  |- SettingsWindow
|  `- TrayController
|
`- Persistence
   `- SettingsStore
```

## Threading policy

Start simple.

Preferred MVP model:

- UI/Win32 message thread owns windows and Qt GUI.
- Rendering may begin on the same thread if performance is sufficient.
- Introduce a render thread only when measured evidence shows it is useful.

Do not create threads as decoration.

If a render thread is introduced:

- runtime configuration must be snapshot-based;
- input events must cross through a bounded thread-safe queue or latest-state exchange;
- shutdown must be deterministic;
- no Qt widget access may occur off the GUI thread.

## Rendering backend

Primary:

- Direct2D for vector geometry and antialiasing.
- DirectComposition for GPU-backed transparent composition.

Avoid a giant CPU bitmap updated through a full-desktop copy every frame.

## Overlay model

Preferred production model:

- one transparent overlay HWND per monitor;
- no activation;
- no taskbar entry;
- no Alt+Tab entry;
- click-through;
- virtual desktop aware;
- per-monitor DPI aware.

Do not assume monitor coordinates begin at `(0, 0)`.

## Effect time model

All animation is time-based.

Use a monotonic high-resolution clock such as QueryPerformanceCounter.

Never make animation speed depend on frame count.

## Device-loss policy

Rendering resources must be recreatable.

Separate:

- device-independent state;
- device-dependent Direct2D resources;
- per-monitor composition resources.

On device loss or display topology change, recreate only the affected rendering resources.

## Coordinate policy

Store cursor samples in virtual-screen physical pixel coordinates.

Convert to per-monitor local coordinates only when rendering to a specific overlay.

Document every coordinate transform.

## Configuration policy

Runtime settings are validated before publication.

Bad persisted values must fall back safely instead of crashing the renderer.

## Logging policy

Keep logging lightweight.

Useful categories:

- startup;
- display topology;
- renderer recreation;
- input registration;
- settings load/save;
- fatal errors.

Do not log every mouse sample in normal builds.
