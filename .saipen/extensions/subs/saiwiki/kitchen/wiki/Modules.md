<!-- derived page: no mirrors marker. A directory listing is positional, and a
     positional digest would certify the wrong invariant. This page is an
     explicitly-derived index; its authority is the filesystem, not this text. -->

# Source modules

**Derived page.** Unlike the other pages in this wiki, this one has no `mirrors:`
marker on purpose. Its rows come from a directory listing, which is a positional
projection, and a digest over positions would certify exactly the property the
mirror marker exists to refuse. Treat this page as an orientation aid; the
authority is `src/`.

## Layout

| Area | Files | Responsibility |
|---|---|---|
| `src/app/` | main.cpp, application.{h,cpp}, single_instance.{h,cpp}, startup_paths.{h,cpp}, startup_mode.{h,cpp} | entry point, application lifecycle, single-instance protocol, path resolution, startup-argument classification |
| `src/core/` | cursor_history.{h,cpp}, cursor_sample.h, log.{h,cpp} | bounded cursor sample history, logging |
| `src/platform/` | mouse_input.{h,cpp}, dpi_awareness.{h,cpp}, autostart.{h,cpp}, button_flags.h, timestamp.h | raw input, DPI awareness, per-user autostart registry access, button-transition vocabulary, QPC timestamps |
| `src/render/` | overlay_manager.{h,cpp}, overlay_window.{h,cpp}, render_scheduler.{h,cpp}, screen_map.h, trail_stroke_policy.h, deferred_coalescer.h, render_color.h | per-monitor overlay windows, frame scheduling, monitor geometry, stroke policy |
| `src/effects/` | click_bubble_effect.{h,cpp}, click_config.h, trail_effect.{h,cpp}, trail_config.h, effect_palette.h | the two visual effect families and their persisted configuration |
| `src/config/` | app_config.{h,cpp}, config_storage.{h,cpp} | the persisted config model and its schema-gated load/save |
| `src/ui/` | settings_window.{h,cpp}, theme.{h,cpp}, tray_icon.{h,cpp}, theme.qrc, check.xpm | settings GUI, theme, tray integration |

## Verified include graph

Read from the `#include "..."` directives in each area, not assumed:

```
core     -> (nothing)
effects  -> core
config   -> core, effects
platform -> (nothing)
render   -> core, effects, platform
ui       -> effects
app      -> config, core, effects, platform, render, ui
```

Two consequences worth knowing, because they are the opposite of the first
instinct:

- **`render` depends on `effects`, not the reverse.** The renderer includes
  `click_bubble_effect.h` and `trail_effect.h` and asks them for geometry.
  `effects` includes only `core`. So the geometry contract is defined by
  `effects` and consumed by `render`; a change to an effect's geometry API is a
  renderer-visible change.
- **`config` depends on `effects`** for the enum and bound vocabulary it
  persists. That is why a new style or a new bound is a `config` change too, and
  why the schema gates live beside the enums they gate.

No area includes `../ui/` except `app`. Nothing below `app` reaches upward.

The two files added for silent autostart add no edge that was not already
there. `src/platform/autostart.h` includes no heading at all: the registry
surface sits behind an interface precisely so the whole feature can be driven
by an in-memory double, and a test can include it without dragging in `app`.

## Three files whose folder does not fully describe them

- `src/effects/effect_palette.h` is included by `src/ui/settings_window.cpp` and
  by two test files, never by another `effects` file. It is a shared palette
  vocabulary, not an effect implementation, which is why its consumer is the
  settings UI.
- `src/render/render_color.h` appears only inside `render`. The colour type the
  effects use does not come from here, so there is no `effects -> render` edge
  despite the shared concept.
- `src/app/startup_mode.h` is a pure command-line parser in the `app` layer
  that includes `../platform/autostart.h` for exactly one reason: the startup
  argument constant is minted by the registry-command builder, so the command
  ProTrail writes and the mode it parses cannot drift apart. It pulls in no Qt
  and no Windows header, which is why `tests/test_autostart.cpp` can include an
  `app`-layer header directly.

## Sources

- `src/` (directory listing and `#include "..."` directives, read only)
