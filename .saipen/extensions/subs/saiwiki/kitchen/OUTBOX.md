# OUTBOX

<!-- One entry, deliberately. `subs.py` requires EVERY non-legacy entry to carry
     the complete-package field set (OUTBOX_COMPLETE_FIELDS) whatever its status,
     and `verified` to be a one-line closed verdict -- so a finding that is not a
     package cannot live here at all. W-002 (the CONFORMANCE chore) was therefore
     moved to the sanctioned append-only finding surface,
     .saipen/extensions/subs/_shared/inbox.md, and is also carried on
     kitchen/wiki/Index.md and on this producer's own BOARD.md. -->

## W-001: complete wiki package -- canonical ID spaces of ProTrail

- **status:** ready
- **summary:** An eleven-page wiki mirroring this project's canonical ID spaces (tickets, source receipts, roadmap milestones, style enums, schema boundaries, CTest targets), rebuilt as a fresh package bound to the CURRENT source identity after the previous package went stale, with a repair to two projections that could not see a new canonical row.
- **main_project_refs:** [.saipen/BOARD.md, .saipen/intake/, ROADMAP/roadmap/, src/effects/click_config.h, src/effects/trail_config.h, src/config/app_config.h, CMakeLists.txt, src/]
- **critical:** false
- **severity:** P2
- **producer:** saiwiki
- **source_head:** 1fc09f09410da3493ed8f7ffca621ca0467ca259
- **source_tree_fingerprint:** git-delta-v1:6cad7aa60e33f8538ad4ad6c2f1aae1e4c7d00a99d8a4591425097a00247080a
- **role_revision:** sha256:54a42475a124ab0f27e83d600a284a9cc54d9668029c4828cfc48512b031df13
- **coverage:** the whole maintained wiki, eleven pages, no sampled subset --
  wiki/Tickets.md mirrors `.saipen/BOARD.md` rows 1-42 digest 2d4faad8113f423c;
  wiki/Sources.md mirrors intake receipts rows 1-5 digest ca3db7b9819c75b3;
  wiki/RoadmapMVP.md mirrors ROADMAP/roadmap/mvp rows 0-9 digest 13c5a393fca5b457;
  wiki/RoadmapFuture.md mirrors ROADMAP/roadmap/future rows 10-21 digest 6fc94cb38a3f5b5a;
  wiki/ClickStyles.md mirrors the ClickStyle enum rows 0-10 digest 9ce452d65f8d6e24;
  wiki/TrailStyles.md mirrors the TrailStyle enum rows 0-7 digest 167af58625992845;
  wiki/SparkleModes.md mirrors the TrailSparkleMode enum rows 0-5 digest d1f5f79b7e064b6c;
  wiki/SchemaBoundaries.md mirrors the schema boundary constants rows 6-10 digest b45282ef63053766;
  wiki/TestSuites.md mirrors the CTest registrations rows 1-18 digest 452b1744c11b1d43;
  wiki/Modules.md is a derived module and include-graph map and deliberately carries no marker;
  wiki/Index.md carries the package binding, the rebuild procedure and the answered CONFORMANCE finding.
- **payload:** these eleven files, all under .saipen/extensions/subs/saiwiki/kitchen/wiki/ : Index.md, Tickets.md, Sources.md, RoadmapMVP.md, RoadmapFuture.md, ClickStyles.md, TrailStyles.md, SparkleModes.md, SchemaBoundaries.md, TestSuites.md, Modules.md.
  Machine-readable publication of the same set: .saipen/extensions/subs/saiwiki/READY/sha256_0ed06f0353b37c67467fff669c7f7fd72129d8bf00e638c5e1c8522d6fab5257.json (package_identity sha256:0ed06f0353b37c67467fff669c7f7fd72129d8bf00e638c5e1c8522d6fab5257), staged and published through the engine's own producer API with 33 declared read sources and 11 payload files; the superseded sha256:f5aa190c... artifact was moved to SETTLED/ by the engine's retirement path, not deleted.
  Not part of the payload, `_`-prefixed scratch by PROTOCOL.md section 1: `_verify_wiki.py` (gate), `_regen_wiki.py` (rebuild), `_red_control.py` (gate-integrity control), `_publish_ready.py` (package staging).
- **verified:** PASS -- _verify_wiki.py pages=11 checks=32 failures=0 exit 0; _red_control.py controls=4 failures=0; _regen_wiki.py re-run changes nothing; scan_ready 1 package 0 errors
- **instructions:** 1. The integration DESTINATION is Core's decision: this project has no docs tree, and the package deliberately declares the eleven kitchen paths as its write targets rather than inventing a destination. Copy them wherever the documentation should live; the four scratch tools stay behind. 2. Do not hand-edit mirrored rows -- edit the canonical source (BOARD.md, CMakeLists.txt, app_config.h, the style headers, ROADMAP/) and re-run `_regen_wiki.py`. A hand edit is caught by the marker, but it is still the wrong door. 3. If the payload's destination lands outside `.saipen/`, every page's binding goes stale by construction: re-run `qq` afterwards. 4. Decide the CONFORMANCE chore carried in `_shared/inbox.md` and on wiki/Index.md. 5. The previous package is superseded, not edited: its binding names a source_tree_fingerprint that no longer exists.
- **details:**
  **Why this is a fresh package and not an edit.** The previous W-001 package was bound to `source_tree_fingerprint git-delta-v1:141993d4...` and mirrored 27 tickets, 4 receipts, schema boundaries 6-9 and 17 CTest targets. The project has since moved to 42 tickets, 5 receipts, boundary 10 and 18 targets, and the working-tree delta changed (T-032's autostart work is uncommitted). PROTOCOL.md section 6 classifies that as stale evidence, so the package was rebuilt rather than reused. `source_head` is unchanged (`1fc09f0...`), which is exactly why the fingerprint, not HEAD, is the freshness input that mattered here.
  ---
  **Two projections could not see a new canonical row.** `_verify_wiki.py` enumerated source receipts with `range(1, 5)` and named its schema boundaries in a four-name literal list. Both pages therefore reported GREEN while SRC-005 and `kStartWithWindowsSchema = 10` were invisible -- a gate can only miss what it was never pointed at, and a projection whose scope is a literal list is a second source of truth. Both now derive their ID set from the project's own files (active plus archived receipts; every `k*Schema` constant by regex), a gap in the derived set fails instead of being skipped, and the heading that restates each range in prose is checked against the marker -- the old heading and the old marker had gone stale together, so neither could catch the other. That repair is why this package's row counts changed on pages whose content did not: Sources gained row 5, SchemaBoundaries row 10, TestSuites row 18.
  ---
  **A correction inside the mirror's own arithmetic.** CTest targets are mirrored by registration ordinal, and an inserted target shifts every later ordinal: `protrail_autostart_tests` was added at #17, so the old page's row 17 (`protrail_single_instance_tests`) was both drifted and misleading. The page states which invariant is positional and which is durable; the NAME is the identity, the ordinal is not. The same distinction is why the ticket page mirrors IDs rather than positions.
  ---
  **A divergence between two canonical sources, left visible.** `.saipen/BOARD.md` now has T-26 and T-27 checked DONE, while the coverage ledger still lists `SRC-004:R2` and `SRC-004:R3` as BLOCKED and unresolved. Both were read at the same moment and both are mirrored as they are, with the divergence named in its own subsection. The likely reading -- tickets closed, clause ledger not re-dispositioned -- is labelled as a reading rather than asserted as fact.
  ---
  **Derived versus authored, stated per column.** Mirrored cells are produced by the same projection functions the verifier compares against. Two columns are authored and labelled as such: the schema page's "what became legitimate at this schema" and the prose that restates a derived number (ticket counts, the intrusive-target arithmetic, the current schema version, the package binding). A boundary with no authored description FAILS the rebuild rather than shipping an empty cell.
  ---
  **Two publication defects, found by publishing and recorded rather than tidied away.** First, the package was published before the pages' line endings were normalized, so its payload held LF bytes; republishing after normalization produced a second artifact instead of replacing the first, and its identity changed. Second, the retirement step that clears superseded evidence walked every file in `READY/` and moved the just-published package to `SETTLED/` as well, leaving zero live packages and a retired copy of a live identity. Both were fixed -- retirement now skips the identity it just published, and the READY artifact is compared as raw bytes so a line-ending difference can never hide behind a normalized read -- and the mis-retired duplicate was removed because a `SETTLED` copy of a live package contradicts the live one. The genuinely superseded artifact stays in `SETTLED/`.
  ---
  **One engine fact worth carrying forward.** `saipen collect <producer>` is not a command in this version; the targeted path is the `qqq` macro (`saipen.py` dispatches `qq` to `ensure_producer_ready`, `qqq` to `collect_and_ship_producer`), and it consumes `READY/*.json` through `producer.StagingGeneration.scan_ready`. A producer that writes only `kitchen/OUTBOX.md` therefore reads as `NOT_READY` no matter how complete its markdown is. Related and instructive: the OUTBOX grammar requires every non-legacy entry to be a COMPLETE package, so a finding cannot be filed here at all -- which is how this producer learned where a finding actually belongs.
  ---
  **Boundary disclosure.** Every file this producer wrote is inside `.saipen/extensions/subs/saiwiki/` -- the eleven pages, four `_`-prefixed scratch tools, the `READY/` artifact, the `SETTLED/` superseded artifact and the transient `.prepare-staging/` generation that publication removed -- with ONE documented exception: one appended line in `.saipen/extensions/subs/_shared/inbox.md`, which PROTOCOL.md section 4 names as the append-only finding surface for findings that are not packages (appended, never edited). Two further side effects outside the project are disclosed rather than hidden: publishing through the engine imports `saipen_engine` from `saipen_home`, which can refresh the home's `tools/**/__pycache__` bytecode cache, and the main tree was otherwise read only (git commands read HEAD; no source file, board or log was touched by this producer).
