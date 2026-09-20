"""Internal verification for the saiwiki wiki package (scratch tool, `_`-prefixed).

A mirror marker is only a gate if it can go red, and it only proves what it
actually compares. An earlier version of this script checked only that the
marker's digest matched a fresh digest of the canonical source -- which says
nothing about whether the page BODY mirrors those rows. A page could drift from
its own marker and still report green. The check below therefore does three
things per page:

1. re-derive the declared projection from the canonical source;
2. parse the page body back into rows and compare them, cell by cell, to the
   derived rows -- missing rows, extra rows and edited titles all fail;
3. run a deliberate known-bad control on the PARSED PAGE ROWS, so a parser that
   silently matches everything cannot report green.

A PROJECTION must also be derived, not enumerated. T-032 found two projections
that were hard-coded lists rather than derivations: `source_rows()` iterated
`range(1, 5)` and `schema_rows()` named four constants by hand. Both pages
reported green while a real canonical row (SRC-005, `kStartWithWindowsSchema`)
was invisible to them -- a gate can only miss what it was never pointed at. A
projection whose scope is a literal list is a second source of truth, so both
now derive their ID set from the project's own files, and a gap in the result
fails instead of being skipped.

Run from the project root:
    python .saipen/extensions/subs/saiwiki/kitchen/_verify_wiki.py
Read-only against the main tree. Writes nothing.
"""

from __future__ import annotations

import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(".").resolve()
WIKI = ROOT / ".saipen" / "extensions" / "subs" / "saiwiki" / "kitchen" / "wiki"
SAIPEN = Path.home() / ".config" / "opencode" / "skills" / "saipen" / "bin" / "saipen.cmd"

MARKER = re.compile(r"<!-- mirrors: (.+?) rows (\d+)-(\d+) sha256:([0-9a-f]{16}) -->")

# The heading of each generated page restates the mirrored range. Two groups:
# first ID, last ID.
H1_TICKETS = re.compile(r"^# Tickets \(T-(\d+) \.\. T-(\d+)\)$", re.M)
H1_SOURCES = re.compile(r"^# Sources \(SRC-(\d+) \.\. SRC-(\d+)\)$", re.M)
H1_SCHEMA = re.compile(r"^# Config schema boundaries \((\d+) \.\. (\d+)\)$", re.M)
H1_TESTS = re.compile(r"^# CTest targets \((\d+) \.\. (\d+)\)$", re.M)

failures: list[str] = []
checks = 0


def digest(rows: list[tuple[int, str]]) -> str:
    payload = "\n".join("%d|%s" % r for r in rows)
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()[:16]


def parse_page_rows(text: str, fmt) -> dict[int, str]:
    """Return {id: full table line} for the FIRST table after the marker.

    Only the first table is the mirror. A page may legitimately carry further
    tables (a lifecycle position, a schema gate); parsing all of them made a
    secondary table's bare IDs collide with the mirror's, so the scope is
    explicitly "the first table", not "every table that looks like one".
    """
    started = False
    found: dict[int, str] = {}
    for line in text.splitlines():
        if not line.startswith("| "):
            if started and line.strip() == "":
                break
            continue
        started = True
        cell = line.split("|")[1].strip()
        n = fmt(cell)
        if n is not None:
            if n in found:
                raise ValueError(f"duplicate row for id {n}")
            found[n] = line
    return found


def cells(line: str, lo: int, hi: int) -> str:
    """The mirrored projection of one row line, as this page renders it.

    Comparison is EQUALITY, not substring containment: a row that keeps its
    canonical title and merely gains extra trailing words is a drifted row, and
    a containment check would call it green.
    """
    parts = [p.strip().replace("`", "") for p in line.strip().strip("|").split("|")]
    return " | ".join(parts[lo:hi])


def check(
    page: str,
    rows: list[tuple[int, str]],
    fmt,
    span: tuple[int, int],
    h1: "re.Pattern[str] | None" = None,
) -> None:
    global checks
    text = (WIKI / page).read_text(encoding="utf-8")

    m = MARKER.search(text)
    if m is None:
        failures.append(f"{page}: no mirrors marker")
        return
    lo, hi, declared = int(m.group(2)), int(m.group(3)), m.group(4)

    # (1b) any range restated in the page's own heading needs the same check as
    # the marker. In T-032 the heading and the marker went stale TOGETHER (both
    # said 1..17 while CMakeLists registered 18 targets), so a heading that
    # restates the range in prose is a second place to drift and is checked
    # separately rather than trusted because the marker agreed with it once.
    if h1 is not None:
        checks += 1
        hm = h1.search(text)
        if hm is None:
            failures.append(f"{page}: H1 range not found by {h1.pattern!r}")
        elif (int(hm.group(1)), int(hm.group(2))) != (lo, hi):
            failures.append(
                f"{page}: H1 range says {hm.group(1)}..{hm.group(2)}; the marker "
                f"claims {lo}..{hi}"
            )

    # (1) marker vs canonical source
    checks += 1
    actual = digest(rows)
    if actual != declared:
        failures.append(
            f"{page}: marker declares {declared}; canonical rows digest to {actual}"
        )
    if [r[0] for r in rows] != list(range(lo, hi + 1)):
        failures.append(
            f"{page}: marker claims rows {lo}-{hi}; canonical IDs are "
            f"{[r[0] for r in rows]}"
        )

    # (2) page body vs canonical rows
    checks += 1
    try:
        parsed = parse_page_rows(text, fmt)
    except ValueError as exc:
        failures.append(f"{page}: page rows unparseable -- {exc}")
        return
    if set(parsed) != {r[0] for r in rows}:
        missing = sorted({r[0] for r in rows} - set(parsed))
        extra = sorted(set(parsed) - {r[0] for r in rows})
        failures.append(f"{page}: page/canonical ID mismatch missing={missing} extra={extra}")
    for n, title in rows:
        line = parsed.get(n)
        if line is None:
            continue
        got = cells(line, *span)
        if got != title:
            failures.append(
                f"{page}: row {n} drifted\n          page      : {got[:90]!r}\n"
                f"          canonical : {title[:90]!r}"
            )

    # (3) known-bad control against the PARSED PAGE, not a fresh in-memory list.
    # The control REPLACES the row's canonical title, because appending text to
    # the line leaves the title as a substring and the control would pass while
    # proving nothing -- a control that cannot fail is not a control.
    checks += 1
    if parsed:
        victim = sorted(parsed)[0]
        victim_title = dict(rows).get(victim, "")
        mutated_line = parsed[victim].replace(victim_title, "KNOWN-BAD-CONTROL")
        mutated = text.replace(parsed[victim], mutated_line)
        if mutated == text:
            failures.append(f"{page}: known-bad control could not mutate row {victim}")
        else:
            mp = parse_page_rows(mutated, fmt)
            if cells(mp.get(victim, ""), *span) == victim_title:
                failures.append(
                    f"{page}: KNOWN-BAD control did not make row {victim} fail -- the "
                    f"checker is not comparing page content"
                )


# --- ID-cell formatters: how each page renders a row ID in its first cell ------


def f_ticket(cell: str):
    m = re.fullmatch(r"(T-\d+)", cell)
    return int(m.group(1).split("-")[1]) if m else None


def f_source(cell: str):
    m = re.fullmatch(r"(SRC-\d+)", cell)
    return int(m.group(1).split("-")[1]) if m else None


def f_number(cell: str):
    return int(cell) if re.fullmatch(r"\d+", cell) else None


# --- canonical projections -----------------------------------------------------


def board_rows() -> list[tuple[int, str]]:
    rows = []
    for line in (ROOT / ".saipen" / "BOARD.md").read_text(encoding="utf-8").splitlines():
        m = re.match(r"^- \[( |x)\] (T-\d+)(?: \[(P\d)\])? (.*)$", line)
        if not m:
            continue
        rows.append(
            (int(m.group(2).split("-")[1]), m.group(4).split(" | ")[0].strip())
        )
    return sorted(rows)


def source_ids() -> list[int]:
    """The receipt IDs this project actually has, derived from its own files.

    Both the live and the retired receipts count: a closed receipt is archived,
    not deleted, and its row is still part of the ID space this page mirrors.
    """
    ids: set[int] = set()
    for base in (
        ROOT / ".saipen" / "intake" / "active",
        ROOT / ".saipen" / "archive" / "source",
    ):
        for f in base.glob("SRC-*.md"):
            m = re.fullmatch(r"SRC-(\d+)", f.stem)
            if m:
                ids.add(int(m.group(1)))
    if not ids:
        raise AssertionError("no SRC-* receipt found under intake/active or archive/source")
    return sorted(ids)


def source_rows() -> list[tuple[int, str]]:
    rows = []
    for i in source_ids():
        out = subprocess.run(
            ["cmd", "/c", str(SAIPEN), "source", "status", f"SRC-{i:03d}", "--json"],
            capture_output=True,
            text=True,
        ).stdout
        d = json.loads(out)
        if not d.get("ok"):
            # A receipt file exists but the ledger refuses it: a real mismatch,
            # not a row to skip. Skipping is how a page stays green while it is
            # wrong.
            raise AssertionError(f"SRC-{i:03d} present on disk, ledger says {d}")
        c = d["coverage"]
        disp = ", ".join(f"{k}={v}" for k, v in sorted(c["dispositions"].items()))
        cells = [
            d["source_kind"],
            d["status"],
            d["linked_work"] or "--",
            f"{c['actionable']}/{c['terminal']}",
            disp,
        ]
        rows.append((i, " | ".join(cells)))
    return rows


def roadmap_rows(sub: str) -> list[tuple[int, str]]:
    rows = []
    for f in sorted((ROOT / "ROADMAP" / "roadmap" / sub).glob("*.md")):
        h1 = f.read_text(encoding="utf-8").splitlines()[0].lstrip("# ").strip()
        rows.append((int(f.name[:2]), h1))
    return sorted(rows)


def enum_rows(path: str, enum: str) -> list[tuple[int, str]]:
    text = (ROOT / path).read_text(encoding="utf-8")
    block = re.search(r"enum class %s : uint8_t \{(.*?)\n\};" % enum, text, re.S)
    if block is None:
        raise AssertionError(f"{enum} not found in {path}")
    return sorted(
        (int(m.group(2)), m.group(1))
        for m in re.finditer(r"^\s+(\w+) = (\d+),", block.group(1), re.M)
    )


def schema_rows() -> list[tuple[int, str]]:
    """Every named schema boundary, by regex over the constant declarations.

    The names are NOT listed here. A literal name list can only ever confirm
    the boundaries somebody remembered to add to it, which is exactly how
    `kStartWithWindowsSchema = 10` stayed invisible to this page while the
    marker still read as green.
    """
    text = (ROOT / "src" / "config" / "app_config.h").read_text(encoding="utf-8")
    rows = [
        (int(m.group(2)), m.group(1))
        for m in re.finditer(
            r"static constexpr int (k\w*Schema)\s*=\s*(\d+);", text
        )
    ]
    if not rows:
        raise AssertionError("no k*Schema boundary constant found in app_config.h")
    return sorted(rows)


def test_rows() -> list[tuple[int, str]]:
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    return [(i + 1, m.group(1)) for i, m in enumerate(re.finditer(r"add_test\(NAME (\S+)", text))]


def main() -> int:
    global checks
    # span = the cell range each page uses for its mirrored projection
    check("Tickets.md", board_rows(), f_ticket, (3, 4), H1_TICKETS)
    check("Sources.md", source_rows(), f_source, (1, 6), H1_SOURCES)
    check("RoadmapMVP.md", roadmap_rows("mvp"), f_number, (1, 2))
    check("RoadmapFuture.md", roadmap_rows("future"), f_number, (1, 2))
    check("ClickStyles.md", enum_rows("src/effects/click_config.h", "ClickStyle"), f_number, (1, 2))
    check("TrailStyles.md", enum_rows("src/effects/trail_config.h", "TrailStyle"), f_number, (1, 2))
    check("SparkleModes.md", enum_rows("src/effects/trail_config.h", "TrailSparkleMode"), f_number, (1, 2))
    check("SchemaBoundaries.md", schema_rows(), f_number, (1, 2), H1_SCHEMA)
    check("TestSuites.md", test_rows(), f_number, (1, 2), H1_TESTS)

    checks += 1
    index = (WIKI / "Index.md").read_text(encoding="utf-8")
    for page in sorted(WIKI.glob("*.md")):
        if page.name == "Index.md":
            continue
        m = MARKER.search(page.read_text(encoding="utf-8"))
        if m and m.group(4) not in index:
            failures.append(f"Index.md does not list {page.name}'s digest {m.group(4)}")

    print(
        f"WIKI_VERIFY pages={len(list(WIKI.glob('*.md')))} checks={checks} failures={len(failures)}"
    )
    for f in failures:
        print("  FAIL:", f)
    print("WIKI_VERIFY_RESULT:", "PASS" if not failures else "FAIL")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
