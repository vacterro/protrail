<!-- mirrors: intake receipts rows 1-5 sha256:ca3db7b9819c75b3 -->
<!-- projection: row = "<receipt number>|<source_kind> | <status> | <linked_work> | <actionable>/<terminal> | <disposition counts>" -->
<!-- values read from `saipen source status SRC-NNN --json`; `--` means the canonical field is null -->

# Sources (SRC-001 .. SRC-005)

Mirror of the project's durable external-intent receipts, rebuilt **by ID**.

These are the immutable authority layer: the request body is opaque data written
before anyone interprets it, and its SHA-256 is content identity. The projection
above uses only canonical structured fields, so no reading of the prose can
change a row.

| Receipt | Kind | Status | Linked work | Actionable / terminal | Dispositions |
|---|---|---|---|---|---|
| SRC-001 | implementation_mission | ACTIVE | T-013 | 1/1 | VERIFIED=1 |
| SRC-002 | user_instruction | CLOSED | T-24 | 1/1 | VERIFIED=1 |
| SRC-003 | user_instruction | CLOSED | T-25 | 1/1 | VERIFIED=1 |
| SRC-004 | review_handoff | ACTIVE | -- | 3/1 | BLOCKED=2, VERIFIED=1 |
| SRC-005 | user_instruction | ACTIVE | T-28 | 1/1 | VERIFIED=1 |

`linked_work` is the receipt's aggregate work link. `SRC-004` is `--` because it
was consumed as an acceptance signal across T-23/T-24/T-25 and then as the
direction for T-26/T-27; its per-clause links live in the coverage ledger
below, not in this one aggregate field. That is a real difference from
`SRC-001` and `SRC-005`, which each projected a single ticket and therefore
have a work link. `SRC-005` is the T-26/T-27 integrity-repair handoff, and its
single clause reached `VERIFIED` through T-28.

## Coverage ledger

Requirement IDs are stable clause identities. The numbering shape is not
uniform -- the older `SRC-004` ledger names its clauses `R1`..`R3` while
`SRC-005` names its single clause `R001` -- and both are mirrored exactly as
written, because normalizing them here would create a second identity for the
same clause.

| Clause | Class | Disposition | Work | Note |
|---|---|---|---|---|
| SRC-004:R1 | acceptance-criterion | VERIFIED | T-23 | user visual acceptance of the combined T-23 + T-24 + T-25 artifact |
| SRC-004:R2 | requirement | BLOCKED | T-26 | world-space Hold Motion Wake; the linked ticket is DONE, this clause is not |
| SRC-004:R3 | requirement | BLOCKED | T-27 | Hold FX controls; the same divergence |
| SRC-005:R001 | requirement | VERIFIED | T-28 | T-26/T-27 integrity repair; verified through the T-28 cycle |

`R2` and `R3` are deliberately **not** terminal. The work is implemented and
its automated evidence is green, but the new creative behaviour must not be
marked done before the user has looked at it, so the honest disposition was
`BLOCKED`, and nothing here is closed early to make the ledger tidy.

### A divergence this page does not smooth over

The project's two canonical sources disagree about T-26 and T-27:

| Source | Says |
|---|---|
| `.saipen/BOARD.md` | T-26 and T-27 are `DONE`, checked boxes in the `DONE` section, each with an owner, a claim time and `closure_mode: own_patch` |
| the coverage ledger | `SRC-004:R2` and `SRC-004:R3` are `BLOCKED` and both are listed in the receipt's `unresolved` set |

Both were read at the same moment and both are mirrored as they are. The
likely reading is that the tickets closed while the clause ledger was not
re-dispositioned, which would make this bookkeeping rather than a
contradiction about the product -- but that is a reading, not a fact, so it
is labelled as one. Until Core disposits them, `SRC-004` legitimately stays
`ACTIVE` with two unresolved clauses.

## Lifecycle position

The source lifecycle is `RECEIVE -> CAPTURE -> VERIFY -> LINK -> NORMALIZE ->
EXECUTE -> COVER -> REREAD -> CLOSE -> ARCHIVE/PURGE`.

| Receipt | Position |
|---|---|
| SRC-001 | COVER terminal (1/1 VERIFIED); body still hot in `intake/active/` |
| SRC-002 | CLOSED and archived; tombstone retained with its content digest |
| SRC-003 | CLOSED and archived; tombstone retained with its content digest |
| SRC-004 | EXECUTE/COVER in progress (2 clauses unresolved); body hot in `intake/active/` |
| SRC-005 | COVER terminal (1/1 VERIFIED); body still hot in `intake/active/` |

A tombstone is not deletion: `intake/tombstones/SRC-NNN.json` retains the
receipt identity, content digest, archive reference and closure timestamp, so a
closed receipt cannot be silently reopened as a fresh one.

## Sources

- `.saipen/intake/index.json`, `.saipen/intake/coverage/`, `.saipen/intake/tombstones/`
- `saipen source status SRC-NNN --json` (structured projection only)
