# MVP 04 - Click Bubble

Status: CLOSED (T-009, built 2026-09-10 / user VISUAL PASS 2026-09-10).

## Goal

Add a clean click effect at the exact global cursor position.

## Visual target

On mouse-down:

1. A compact cyan/white bubble or ring appears.
2. It expands quickly.
3. It fades smoothly.
4. It disappears completely.

The effect must feel restrained and polished.

## Scope

Implement:

- `ClickBubbleEffect`;
- effect creation on mouse button down;
- click-position capture;
- radius animation;
- opacity animation;
- configurable color;
- configurable duration;
- configurable final size;
- configurable outline thickness.

Use time-based easing.

A cubic ease-out is a good initial choice.

## Defaults

Suggested initial tuning:

- start radius: 8 px;
- end radius: 24-28 px;
- duration: 200-300 ms;
- high initial opacity;
- transparent end state.

## Effect lifecycle

Every click bubble must have a bounded lifecycle.

No completed effect may remain in the active list.

## Acceptance criteria

- left click bubble appears at the real click point;
- stationary clicks work;
- rapid repeated clicks work;
- multiple simultaneous live bubbles do not corrupt state;
- completed bubbles are removed;
- effect timing does not depend on frame rate.

## Non-goals

No particle burst.
No sound.
No alternate click themes.
No separate left/right styles yet.

## Close evidence

Record animation equation/easing and rapid-click test result.

Evidence (T-009, built 2026-09-10; automated proof done, visual proof
requires a human pass for C14's 10-point checklist):

- Files changed:
  - src/effects/click_config.h (new) -- validated ClickConfig (cyan
    0,200,255 defaults; 8->26 px, 250 ms, 0.85, 2.5 px).
  - src/effects/click_bubble_effect.h/cpp (new) -- pure animation
    engine (no D2D), bounded vector with 128 cap (C9), pure
    progress/radius/opacity math.
  - src/render/overlay_window.h/cpp -- ClickBubbleSink + bubble brushes
    (cached once, only SetColor per bubble, C7); render_frame extended
    to draw trail -> bubbles in the same BeginDraw/Clear/EndDraw frame
    (BeginDraw, Clear, trail build_geometry, click_bubble draw, EndDraw,
    Present, Commit); transform reuse for C8.
  - src/app/application.h/cpp -- ClickBubbleEffect instance, click_config,
    `on_mouse_activity` now calls click_effect.on_button_down(sample)
    (C3; disabled guard C17), shared active-only scheduler
    (trail_live || bubbles_live -> render; otherwise one final clear
    and timer stops; C10/C11 -- bubbles animate even with trail
    disabled and no cursor history).
  - CMakeLists.txt -- added click_bubble_effect sources to protrail and
    new test target protrail_click_tests; added smoothing regression
    blocks to test_trail_effect.cpp.

- Animation equation (C5, pure of elapsed monotonic time; never
  pixels-per-frame):
  - progress          = clamp((now - start) / duration, 0..1)
  - ease_out_cubic    = 1 - (1 - progress)^3   (curve spec C5, fast
    start / gentle landing / no overshoot)
  - radius            = start_radius + (end_radius - start_radius) * ease
    (monotone non-decreasing; start_radius at birth, end_radius at
    expiry)
  - opacity           = base_opacity * (1 - progress)
    (base_opacity at birth, zero at/after expiry)
  - A bubble is live while progress < 1; prune() removes expired, cap
    policy keeps memory bounded.

- Click-creation contract (C3): Application.on_button_down ->
  ClickBubbleEffect.on_button_down(sample) -> spawn(x,y,timestamp_ns)
  from the CursorSample's own position; only ButtonAction::Down spawns
  (Up/move filtered), position frozen even if the mouse moves after.

- Sinks/boundary (C7/C8): ClickBubbleSink (reserve_bubbles_hint,
  add_bubble(cx,cy,radius,thickness,r,g,b,alpha)); overlay-local
  transform = screen - origin (negative virtual coords work).

- Build: Debug + Release both succeed; /W4 /WX.

- Tests (all PASS):
  - protrail_tests 0 (bootstrap/log)
  - protrail_history_tests 0
  - protrail_timestamp_tests 20/20 PASS
  - protrail_button_flags_tests 13/13 PASS
  - protrail_input_dispatch PASS
  - protrail_trail_tests 4271 checks (all original + T-008A smoothing
    ladder 0/0.25/0.5/1.0 differ/monotone + per-level corner bound + NaN
    check)
  - protrail_click_tests 107 checks (17 C12 items: spawn/coords/
    progress 0/0.5/1, radius start/end/monotone, opacity 0.85->0,
    pruning, 5 rapid distinct bubbles, 128 cap with oldest evicted,
    negative coords, frame-rate independence, malformed config clamped,
    disabled spawns nothing, normalized-input contract, draw sink
    sanity)

- Rapid-click automated proof: 5 rapid Down events produce 5 distinct
  bubbles (each with own timestamp); cap stress at 256 injects culls to
  128 with oldest evicted; expired bubbles pruned and omitted from draw.

- Click-through regression (C13): hittest_probe.ps1 PASS inside and
  outside the overlay (WindowFromPoint resolves to underlying app, not
  ProTrailOverlay); input_soak burst PASS (synthetic movement + L/R/M
  events received via RIDEV_INPUTSINK while another window was
  foreground).

- Scheduling: shared active-only timer (C10/C11); verified via scheduler
  contract in tests (bubbles animate while no cursor history).
  App still cleanly shuts down; no orphan process.

- Visual verification: automated math PASS; real pixel check still
  needed for C14's items 1-9 (bubbles expanding concentric on real
  click, fading completely, anchored, rapid coexist). Automated suite
  covers time math, lifecycle, transform, and bounds; human must confirm
  the visual -- leave next action on MVP 04 visual verification.
  Queue MVP 05 Settings GUI only after that PASS.
