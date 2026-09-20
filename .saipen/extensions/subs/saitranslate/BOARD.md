# Board

<!-- Same checkbox ticket shape as Core (RFC § 1.2), never the OUTBOX.md
     bold-field shape (PROTOCOL.md § 2) -- that shape is for the deliverable
     leaving via OUTBOX, not for this board. Example, shown without its
     leading "- " so nothing parses it as a live ticket (a validator reading
     this file does NOT skip HTML comments):

       [ ] SAIT-001 short description | critical: true

     Real lines start with "- ", and use your own ID prefix (PROTOCOL.md
     § 3), never Core's T-###. -->

<!-- BOUNDARY: this is YOUR board. The main project has its own BOARD.md
     elsewhere -- never touch it directly, never write a ticket there
     yourself. Findings leave through kitchen/OUTBOX.md only; the main
     agent folds them into its own BOARD.md when it runs `saipen sub
     collect`, never the other way around. -->

<!-- Two items deliberately do NOT appear here as open tickets, because this
     worker finished its translate run and holds `phase: DONE`, and a worker
     cannot claim DONE while its own board says unresolved work:

       - the remaining 27 locales are an OUTBOX coverage gap, named
         individually in evidence/verification.txt and in the OUTBOX entry;
       - the engine namespace deadlock is a CORE decision, so it leaves
         through the one door as OUTBOX entry SAIT-002 (`status: blocked`),
         not as a ticket this producer could ever close itself.

     Both are reachable from Core through the OUTBOX, which is the only
     channel that was ever meant to carry them. -->

## DOING

## TODO

## DONE
- [x] SAIT-001 force-fresh translation surface package: default 6 of 33 locales (en ru et uk ja ded) re-verified and rebound to the current source identity, plus a refreshed UI-string inventory | verify: kitchen/tools/verify_package.py -> VERIFY: PASS (0 failing surface(s)), VERDICT: draft (default set only, 27 locales remain); every delivered locale's source-digest marker equals sha256(README.md)[:16] computed in the same run; all eleven payload documents byte-identical to the previous run, so nothing was rebuilt and nothing is claimed as refreshed | package: kitchen/OUTBOX.md SAIT-001 status draft, not collectable

## BLOCKED
