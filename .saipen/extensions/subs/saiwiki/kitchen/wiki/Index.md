<!-- wiki package index: no mirrors marker. This page carries the package
     binding, not a mirrored row set. Each row-set page carries its own marker. -->

# ProTrail wiki

A wiki of the project's **canonical ID spaces**, produced by saiwiki and bound to
exactly one source identity.

## Package binding

| Field | Value |
|---|---|
| producer | saiwiki |
| source_head | `1fc09f09410da3493ed8f7ffca621ca0467ca259` |
| source_tree_fingerprint | `git-delta-v1:6cad7aa60e33f8538ad4ad6c2f1aae1e4c7d00a99d8a4591425097a00247080a` |
| role_revision | `sha256:54a42475a124ab0f27e83d600a284a9cc54d9668029c4828cfc48512b031df13` |
| discovery model | `git-delta-v1` |

The source fingerprint covers the working-tree delta from HEAD, excluding
`.saipen/`. Every page in this wiki lives under `.saipen/extensions/subs/saiwiki/`,
so producing or editing this wiki cannot invalidate its own binding.

## Pages

| Page | Mirrors | Range | Digest |
|---|---|---|---|
| [ClickStyles.md](ClickStyles.md) | `ClickStyle` enum | 0 .. 10 | `9ce452d65f8d6e24` |
| [Modules.md](Modules.md) | `src/` layout and include graph | -- | derived, no marker |
| [RoadmapFuture.md](RoadmapFuture.md) | `ROADMAP/roadmap/future/` | 10 .. 21 | `6fc94cb38a3f5b5a` |
| [RoadmapMVP.md](RoadmapMVP.md) | `ROADMAP/roadmap/mvp/` | 0 .. 9 | `13c5a393fca5b457` |
| [SchemaBoundaries.md](SchemaBoundaries.md) | schema boundary constants | 6 .. 10 | `b45282ef63053766` |
| [Sources.md](Sources.md) | intake receipts | 1 .. 5 | `ca3db7b9819c75b3` |
| [SparkleModes.md](SparkleModes.md) | `TrailSparkleMode` enum | 0 .. 5 | `d1f5f79b7e064b6c` |
| [TestSuites.md](TestSuites.md) | CTest target registrations | 1 .. 18 | `452b1744c11b1d43` |
| [Tickets.md](Tickets.md) | `.saipen/BOARD.md` ticket ledger | 1 .. 42 | `2d4faad8113f423c` |
| [TrailStyles.md](TrailStyles.md) | `TrailStyle` enum | 0 .. 7 | `167af58625992845` |

Each row-set page carries its own
`<!-- mirrors: <source> rows <lo>-<hi> sha256:<16 hex> -->` marker. The digest is
over the page's declared projection, which each page states in the comment
directly beneath its marker. **Equal row counts are not the test.** A page built
by POSITION rather than by ID is the defect the marker exists to catch: rows can
number the same while naming different things.

## How this package is rebuilt

```text
python .saipen/extensions/subs/saiwiki/kitchen/_regen_wiki.py --dry-run
python .saipen/extensions/subs/saiwiki/kitchen/_regen_wiki.py
python .saipen/extensions/subs/saiwiki/kitchen/_verify_wiki.py
```

`_regen_wiki.py` re-derives every table, marker, derived heading and the package
binding above from the canonical sources, and applies the authored prose edits
whose facts changed. `_verify_wiki.py` re-derives the same projections
independently, parses each page body back into rows, compares them cell by cell,
and runs a per-page known-bad control. Both are `_`-prefixed scratch and are not
part of the integrated payload (PROTOCOL.md section 1).

## The CONFORMANCE question, answered

This project has **no `CONFORMANCE.md` and no `tests/conformance_cases.jsonl`**,
while the local saiwiki charter's `output_contract` names a "CONFORMANCE digest"
and its required read order names `CONFORMANCE.md`. That mismatch was filed as an
open question (W-002) rather than guessed at. It is now answered from this
project's own evidence:

| Evidence | What it shows |
|---|---|
| `grep -rl CONFORMANCE` over the working tree returns exactly one file: the charter itself | no such document exists here |
| the SAIPEN home does ship `CONFORMANCE.md` | the wording has a real source, and that source is the home repo |
| `saipen sub spawn` copies built-in charters into every project verbatim, by design | the local charter is inherited text, not a decision this project made |
| this project's own `saipen` reports `Conformance: PASS` | a different artifact: Core's receipt ledger under `.saipen/recovery/conformance/` |

So the charter wording is inherited rather than something this project is
failing, and the discipline it asks for is still owed an execution: every page
here applies the **same marker mechanism** to a canonical ID source this project
actually has, and names the source it mirrored. Whether the local charter's
`output_contract` and read order should be corrected for consumer projects is a
Core decision -- this producer does not rewrite its own charter -- and it travels
as W-002's instruction instead of a silent patch.

## Sources

- `.saipen/BOARD.md`, `.saipen/intake/`, `ROADMAP/`, `src/`, `CMakeLists.txt`
- `tools/freshness.py` (source identity and role revision)
