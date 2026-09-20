"""Gate-integrity control for the wiki verifier (scratch tool, `_`-prefixed).

The verifier already runs a known-bad control per page against its PARSED page
rows, so a parser that matches everything cannot report green. That control is
in-memory. This script runs the other half: it corrupts a REAL row on a REAL
page, runs the verifier as a subprocess, requires the failure to name exactly the
page and row it corrupted, and then restores the byte-identical original.

The four rows it corrupts are deliberately the ones that were INVISIBLE to the
pre-T-032 projections -- the ticket ledger past row 27, the fifth receipt, the
schema-10 boundary and the eighteenth CTest target. A projection whose scope is a
hard-coded list reports green while the row it was never pointed at is wrong, so
the control has to target precisely those rows.

Run from the project root:
    python .saipen/extensions/subs/saiwiki/kitchen/_red_control.py
Writes each page, then restores it. Nothing is left modified.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
WIKI = HERE / "wiki"
VERIFY = HERE / "_verify_wiki.py"

# (page, first-cell regex, mirrored cell index, human name of the row)
CONTROLS = [
    # The ledger switched ID style at T-23, so the row really is `T-40`.
    ("Tickets.md", re.compile(r"T-40$"), 3, "ticket 40 (past the old 1..27 range)"),
    ("Sources.md", re.compile(r"SRC-005$"), 1, "receipt 5 (past the old range(1,5))"),
    ("SchemaBoundaries.md", re.compile(r"10$"), 1, "schema 10 (not in the old name list)"),
    ("TestSuites.md", re.compile(r"18$"), 1, "CTest target 18 (added after the old 17)"),
]

failures: list[str] = []


def verify() -> str:
    out = subprocess.run(
        [sys.executable, str(VERIFY)], capture_output=True, text=True, cwd=HERE.parents[4]
    )
    return out.stdout + out.stderr


def row_line(lines: list[str], first_cell: re.Pattern[str]) -> int:
    for i, line in enumerate(lines):
        if not line.startswith("|"):
            continue
        if first_cell.fullmatch(line.split("|")[1].strip()):
            return i
    raise SystemExit(f"no row matching {first_cell.pattern}")


for page, first_cell, index, name in CONTROLS:
    path = WIKI / page
    # Byte-level, deliberately. Reading as text normalizes newlines, and writing
    # the normalized text back would silently convert a CRLF page to LF -- a
    # control that mutates the file beyond the row it corrupts is not a control.
    original_bytes = path.read_bytes()
    newline = b"\r\n" if b"\r\n" in original_bytes else b"\n"
    text = original_bytes.decode("utf-8")
    lines = text.replace("\r\n", "\n").split("\n")
    i = row_line(lines, first_cell)
    cells = lines[i].strip().strip("|").split("|")
    cells[index] = " KNOWN-BAD-CONTROL "
    lines[i] = "|" + "|".join(cells) + "|"
    path.write_bytes("\n".join(lines).replace("\n", newline.decode("ascii")).encode("utf-8"))
    try:
        report = verify()
    finally:
        path.write_bytes(original_bytes)

    if "WIKI_VERIFY_RESULT: FAIL" not in report:
        failures.append(f"{page}/{name}: corruption did NOT fail the verifier")
        continue
    if f"{page}: row" not in report or "drifted" not in report:
        failures.append(
            f"{page}/{name}: verifier failed for some other reason -- not the "
            f"corrupted row: {report.strip().splitlines()[1:3]}"
        )
        continue
    print(f"  RED ok: {page} {name}")
    print("        " + report.strip().splitlines()[1].strip())

after = verify()
if "WIKI_VERIFY_RESULT: PASS" not in after:
    failures.append(f"restore left the wiki red:\n{after}")
for page, _first_cell, _index, _name in CONTROLS:
    if b"\r\n" not in (WIKI / page).read_bytes():
        failures.append(f"{page}: restore left the page with LF line endings")

print(f"RED_CONTROL controls={len(CONTROLS)} failures={len(failures)}")
for f in failures:
    print("  FAIL:", f)
print("RED_CONTROL_RESULT:", "PASS" if not failures else "FAIL")
sys.exit(1 if failures else 0)
