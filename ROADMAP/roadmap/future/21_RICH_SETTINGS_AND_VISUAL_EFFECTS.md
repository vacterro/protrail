# POST-MVP VISUAL EXPANSION: RICH SETTINGS / VISUAL EFFECTS

Status: APPROVED POST-MVP DIRECTION (Staged rollout: V1 -> V2 -> V3 -> V4; Next Target: V1)

## Overview

Future visual effects and enhanced settings UX planned after MVP completion.
User has explicitly approved continuing beyond the MVP stop gate into this richer UI and visual effects expansion.

### Staged Implementation Order

1. **V1 — COLORS & QUICK PRESETS** (Next approved target: T-015)
2. **V2 — TRAIL EFFECTS**
3. **V3 — CLICK EFFECTS**
4. **V4 — OPTIONAL PREVIEW / POLISH**

Do not open all four implementation tickets simultaneously. The next implementation target after MVP closure is V1 only.

## Planned Capabilities

### Color UX
- 14 quick color swatches per configurable color
- Custom QColorDialog picker
- Trail Start color
- Trail Fade color
- Modes:
  - Full
  - Start Only
  - Fade Only
  - Start -> Fade Gradient

### Trail Effect Presets
- Classic
- Soft Glow
- Comet
- Neon
- Dotted
- Pulse
- Ribbon
- Spark

### Click Effect Presets
- Ring
- Double Ring
- Ripple
- Burst
- Spark Burst
- Soft Flash
- Dot + Ring

### Fast Settings UX
- Preset buttons visible without deep navigation
- Fewer clicks for common settings
- Live preview
- Reset current section
- Clear grouping between Trail / Click / Colors / Behavior

### Advanced Parameters
- Thickness
- Length
- Fade duration
- Opacity
- Glow strength
- Smoothing
- Segment spacing
- Click radius
- Click duration
- Ring thickness
- Particle amount where applicable

## Non-Goals for First Visual Pass
Do not immediately add:
- Scripting
- User shader system
- Plugin API
- Cloud profiles
- Online preset marketplace
- Animated UI chrome
- Arbitrary particle editor
- Dozens of blend modes