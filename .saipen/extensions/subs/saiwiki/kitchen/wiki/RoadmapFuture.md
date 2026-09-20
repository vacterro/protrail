<!-- mirrors: ROADMAP/roadmap/future rows 10-21 sha256:6fc94cb38a3f5b5a -->
<!-- projection: row = "<file numeric prefix>|<file H1 text>" -->

# Roadmap — future scope (10 .. 21)

Mirror of the future-scope documents, rebuilt **by ID**. Same discipline as the
MVP set: the ID is the file's numeric prefix, the title is the document's own H1.

| ID | Title | File | Status on this page |
|---|---|---|---|
| 10 | FUTURE 10 - Profiles | ROADMAP/roadmap/future/10_PROFILES.md | not started |
| 11 | FUTURE 11 - Generalized Effect Engine | ROADMAP/roadmap/future/11_EFFECT_ENGINE_GENERALIZATION.md | not started |
| 12 | FUTURE 12 - Optional Particle Layer | ROADMAP/roadmap/future/12_PARTICLES.md | partially realized post-MVP |
| 13 | FUTURE 13 - Advanced Click Effects | ROADMAP/roadmap/future/13_ADVANCED_CLICK_EFFECTS.md | partially realized post-MVP |
| 14 | FUTURE 14 - Trail Styles | ROADMAP/roadmap/future/14_TRAIL_STYLES.md | partially realized post-MVP |
| 15 | FUTURE 15 - Color and Gradient System | ROADMAP/roadmap/future/15_COLOR_AND_GRADIENTS.md | partially realized post-MVP |
| 16 | FUTURE 16 - Optional Physics Modes | ROADMAP/roadmap/future/16_PHYSICS_MODES.md | not started |
| 17 | FUTURE 17 - Global Hotkeys | ROADMAP/roadmap/future/17_HOTKEYS.md | not started |
| 18 | FUTURE 18 - Application Rules and Exclusions | ROADMAP/roadmap/future/18_APPLICATION_RULES_AND_EXCLUSIONS.md | not started |
| 19 | FUTURE 19 - Advanced Diagnostics | ROADMAP/roadmap/future/19_DIAGNOSTICS.md | not started |
| 20 | FUTURE 20 - Resilience and Packaging | ROADMAP/roadmap/future/20_RESILIENCE_AND_PACKAGING.md | partially realized post-MVP |
| 21 | POST-MVP VISUAL EXPANSION: RICH SETTINGS / VISUAL EFFECTS | ROADMAP/roadmap/future/21_RICH_SETTINGS_AND_VISUAL_EFFECTS.md | **active** |

## Note on ID 21

ID 21's H1 does not follow the `FUTURE NN - <name>` shape the other twelve use;
it reads `POST-MVP VISUAL EXPANSION: RICH SETTINGS / VISUAL EFFECTS`. That is
the document's own text and is mirrored as-is rather than normalized to match
its neighbours — normalizing it would hide the fact that this document was
written as the live expansion plan rather than as a speculative one, which is
exactly what its status reflects.

## How the `Status on this page` column was derived

The roadmap documents carry no machine status field, so this column is derived
from work already closed on the board and is labelled as derived:

- `partially realized` — the corresponding capability now exists in some form
  after the MVP (trail styles T-016, click styles T-017/T-022, colours and
  gradients T-015, packaging and resilience T-018/T-020).
- `active` — richer settings and visual effects is the thread the post-MVP
  tickets T-015 .. T-027 are actually working.
- `not started` — no closed ticket corresponds to the document.

No future document is marked done. None of them has been closed on the board,
and a roadmap document being partially covered by later work is not the same as
the document's own scope being met.

## Sources

- `ROADMAP/roadmap/future/*.md` (numeric prefix, H1 text)
- `.saipen/BOARD.md` (derived status column only)
