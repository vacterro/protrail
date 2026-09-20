# Changelog

## [0.1.0] - Source-only release preparation

- Added the compact ProTrail home surface with the Essentials controls and made
  it the manual-launch product home; an autostart launch stays tray-only.
- Kept the complete advanced Trail, Sparkle, Click, Hold, Motion Wake, startup
  and developer surfaces in Settings, reachable from the home window.
- Unified home and Settings edits through the canonical
  `Application::app_config` transaction path, with silent two-way
  synchronization and a single persistence commit per user operation.
- Added the developer-only `Set Defaults` action to the home surface for
  convenience; it routes to the same canonical Release Defaults promotion the
  Settings developer panel uses, and capture is proven to include configuration
  fields no surface control exposes.
- Fixed: a completed durable configuration write left the deferred-save pending
  flag set, so an expired debounce could rewrite an already-persisted state.
- Added direct home-surface regressions covering widget identity, silent
  population, one-publication-per-action, the semantic field each control owns,
  contextual enable states, hide-to-tray close, startup-mode window selection,
  single-instance restoration, window duplication and canonical Restore
  Defaults.
- Added Windows CI, contribution/security guidance, issue and pull-request
  templates, repository hygiene rules and generated-artifact ignores.
- Documented multi-monitor DPI behavior, tray lifecycle, autostart behavior,
  configuration storage, the repository layout and source build instructions.

No official Windows executable, installer, portable package or other binary is
attached. Final binary release is blocked by
`FINAL_BINARY_RELEASE_BLOCKED: USER_PRODUCT_ICON_PENDING`.
