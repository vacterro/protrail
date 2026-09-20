# Board

<!-- Same checkbox ticket shape as Core (RFC § 1.2), never the OUTBOX.md
     bold-field shape (PROTOCOL.md § 2) -- that shape is for the deliverable
     leaving via OUTBOX, not for this board. Example, shown without its
     leading "- " so nothing parses it as a live ticket (a validator reading
     this file does NOT skip HTML comments):

       [ ] HUNT-001 short description | critical: true

     Real lines start with "- ", and use your own ID prefix (PROTOCOL.md
     § 3), never Core's T-###. -->

<!-- BOUNDARY: this is YOUR board. The main project has its own BOARD.md
     elsewhere -- never touch it directly, never write a ticket there
     yourself. Findings leave through kitchen/OUTBOX.md only; the main
     agent folds them into its own BOARD.md when it runs `saipen sub
     collect`, never the other way around. -->

## DOING

## TODO

## DONE
- [x] W-001 complete wiki package: eleven pages mirroring the project's canonical ID spaces (BOARD tickets 1-42, intake receipts 1-5, roadmap milestones 0-21, ClickStyle 0-10, TrailStyle 0-7, TrailSparkleMode 0-5, schema boundaries 6-10, CTest targets 1-18) plus a derived module index and a package index, each row set carrying its own `mirrors:` digest marker | verify: _verify_wiki.py pages=11 checks=32 failures=0 PASS; _red_control.py controls=4 failures=0 PASS -- red on Tickets row 40, Sources row 5, SchemaBoundaries row 10 and TestSuites row 18, the four rows the previous hard-coded projections could not see, each naming exactly its page and row and restoring byte-identical; _regen_wiki.py idempotent, second run changes nothing; source_head and source_tree_fingerprint unchanged after all writes; READY package sha256:0ed06f0353b37c67467fff669c7f7fd72129d8bf00e638c5e1c8522d6fab5257 published through the engine producer API, scan_ready 1 package 0 errors | package: kitchen/OUTBOX.md W-001 status ready
- [x] W-002 charter names a canonical mirror source this project does not have | verify: answered from saipen_home rather than guessed -- CONFORMANCE.md exists there, tests/conformance_cases.jsonl does not exist there either, saipen sub spawn copies built-in charters verbatim by design, and this project's own `Conformance: PASS` is Core's receipt ledger rather than a document; the remaining chore is a Core decision about the charter's output_contract and read order item 4, carried in .saipen/extensions/subs/_shared/inbox.md (append-only finding surface, PROTOCOL.md section 4), on wiki/Index.md under "The CONFORMANCE question, answered", and in W-001's details | package: none -- a finding is not a package, and the OUTBOX grammar requires every entry to be a complete package, so W-002 was moved out of it rather than padded with provenance it does not have

## BLOCKED
