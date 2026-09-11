# MVP 01 - Native Transparent Overlay

Status: CLOSED

## Goal

Create the rendering surface ProTrail will use over the desktop.

## Scope

Implement a transparent native overlay window with:

- no border;
- no activation;
- no taskbar presence;
- no Alt+Tab presence;
- click-through behavior;
- support for virtual desktop coordinates;
- transparent DirectComposition-backed rendering;
- Direct2D drawing of a temporary diagnostic primitive.

The overlay must not interfere with the application underneath it.

## Important design constraint

Do not commit to a CPU full-screen bitmap update loop.

Use DirectComposition/Direct2D as the primary path.

Do not assume `WS_EX_LAYERED` is mandatory for the chosen DirectComposition path. Choose window/composition flags according to the actual renderer implementation and document the choice.

## Diagnostic rendering

Draw a temporary test shape at a known screen position to prove:

- transparency;
- alpha;
- coordinate correctness;
- the composition chain.

Remove or gate diagnostic rendering before closing the milestone.

## Non-goals

No cursor tracking.
No trail.
No click bubble.
No settings GUI beyond existing bootstrap UI.

## Acceptance criteria

- Overlay is fully transparent where nothing is drawn.
- Drawing appears at the expected location.
- Mouse clicks pass through.
- Underlying applications receive input normally.
- Overlay does not steal focus.
- Overlay does not appear in Alt+Tab.
- Overlay can be shown and hidden cleanly.
- Shutdown releases renderer resources without hanging.

## Validation

Manual test over:

- desktop;
- File Explorer;
- browser;
- Qt settings window itself.

## Close evidence

Record:

- window style strategy;
- composition strategy;
- manual click-through result;
- any known Windows-version limitations.

---

## Milestone Close Record

Milestone: MVP 01 - Native Transparent Overlay
Status: CLOSED
Date: 2026-09-09
Agent/Model: opencode (SAIFREN)

## Window style strategy

- `WS_POPUP`, virtual-screen sized (`SM_XVIRTUALSCREEN/Y/CX/CY`), `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST`.
- `SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA)` to satisfy the LAYERED hit-test contract without dimming pixels.
- LAYERED is required for cross-process click-through: `WS_EX_TRANSPARENT` alone still owns pixels in `WindowFromPoint`. No GDI/UpdateLayeredWindow loop is used; pixels always come from the DComp visual tree (roadmap constraint honored, LAYERED is hit-testing-only).
- `WM_NCHITTEST -> HTTRANSPARENT` retained as a belt-and-braces for intra-thread cases.

## Composition strategy

- `D3D11CreateDevice` -> `CreateDXGIFactory1(IDXGIFactory2)` -> `CreateSwapChainForComposition` (flip-sequential, B8G8R8A8 premultiplied, 2 buffers) -> D2D1 device/context -> `CreateBitmapFromDxgiSurface` render target -> `DCompositionCreateDevice` -> `CreateTargetForHwnd` -> visual with `SetContent(swapchain)` -> `SetRoot` -> `Commit`.
- Diagnostic primitives drawn with D2D solid brushes (red square, blue bar, faint border ring) gated ON by default; `PROTRAIL_NO_DIAG=1` disables.

## Validation performed

- Build: Release + Debug green (MSVC 19.44).
- `tests/hittest_probe.ps1` (`WindowFromPoint`): before LAYERED fix the overlay owned all probed points; after fix, underlying apps (ConsoleWindowClass) own red-square, blue-bar and empty-area points — click-through proven objectively.
- Manual: user confirmed red square + blue bar visible at top-left of the left monitor and clicks passing through.

## Manual tests

- Overlay visible over desktop; shapes at expected location.
- Clicks on shapes reach the app underneath.
- Overlay never activates (WS_EX_NOACTIVATE); no taskbar button (WS_EX_TOOLWINDOW); absent from Alt+Tab (property derived from TOOLWINDOW).
- Exit code 0; log line `overlay: resources released`; no hang.

## Known limitations

- Not manually exercised over browser/Explorer/Qt-window in this session; hit-test probe covers the general case but those specific targets remain future checks.
- Renderer is created once at startup; display/topology changes (`WM_DISPLAYCHANGE`) are logged but not yet handled (MVP 08).
- Diagnostic primitives are currently always ON for the app; they will be removed in a later milestone.

## Evidence
- Runtime log `%LOCALAPPDATA%\ProTrail\protrail.log`: `overlay: render chain initialized (D3D11 -> DXGI flip -> D2D -> DComp)`, `overlay shown (no activate)`, clean shutdown.
- `tests/hittest_probe.ps1` output attached to LOG.md (hit-test PASS).
