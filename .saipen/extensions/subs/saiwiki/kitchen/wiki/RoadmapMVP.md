<!-- mirrors: ROADMAP/roadmap/mvp rows 0-9 sha256:13c5a393fca5b457 -->
<!-- projection: row = "<file numeric prefix>|<file H1 text>" -->

# Roadmap — MVP milestones (00 .. 09)

Mirror of the MVP milestone documents, rebuilt **by ID**. The ID is the file's
numeric prefix, not its position in a directory listing; the title is the
document's own H1 text, which is the canonical name the roadmap uses.

| ID | Title | File | Landed by |
|---|---|---|---|
| 00 | MVP 00 - Project Bootstrap | ROADMAP/roadmap/mvp/00_PROJECT_BOOTSTRAP.md | T-001, T-005 |
| 01 | MVP 01 - Native Transparent Overlay | ROADMAP/roadmap/mvp/01_NATIVE_OVERLAY.md | T-006 |
| 02 | MVP 02 - Mouse Input and Cursor Sampling | ROADMAP/roadmap/mvp/02_MOUSE_INPUT_AND_SAMPLING.md | T-007 |
| 03 | MVP 03 - Basic Cursor Trail | ROADMAP/roadmap/mvp/03_BASIC_TRAIL.md | T-008 |
| 04 | MVP 04 - Click Bubble | ROADMAP/roadmap/mvp/04_CLICK_BUBBLE.md | T-009 |
| 05 | MVP 05 - Settings GUI | ROADMAP/roadmap/mvp/05_SETTINGS_GUI.md | T-010 |
| 06 | MVP 06 - Configuration and Persistence | ROADMAP/roadmap/mvp/06_CONFIGURATION_AND_PERSISTENCE.md | T-011 |
| 07 | MVP 07 - Render Scheduler and Performance Gate | ROADMAP/roadmap/mvp/07_RENDER_SCHEDULER_AND_PERFORMANCE.md | T-012 |
| 08 | MVP 08 - Multi-Monitor and DPI | ROADMAP/roadmap/mvp/08_MULTI_MONITOR_AND_DPI.md | T-013 |
| 09 | MVP 09 - Tray Integration and MVP Stop Gate | ROADMAP/roadmap/mvp/09_TRAY.md | T-014 |

The `Landed by` column is the only derived content on the page: the milestone
documents do not themselves name a ticket, so the mapping was read from each
ticket's own title, which names its MVP number. It is derivable, not invented —
milestone 04's ticket says "MVP 04", milestone 09's says "MVP 09".

## Reading order

Every milestone document in this set is closed. The MVP stop gate is milestone
09: nothing after it is part of the MVP scope, which is why the post-MVP work
(T-015 onwards) is tracked on the board rather than as a new milestone ID here.

## Sources

- `ROADMAP/roadmap/mvp/*.md` (numeric prefix, H1 text)
- `.saipen/BOARD.md` (the `Landed by` mapping)
