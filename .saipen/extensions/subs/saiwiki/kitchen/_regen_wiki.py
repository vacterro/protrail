"""Regenerate the mirrored surfaces of the saiwiki wiki (scratch tool, `_`-prefixed).

The verifier (`_verify_wiki.py`) answers "is the page still a mirror of the
canonical source". This tool answers the other half: "rebuild the mirror from
the canonical source, in one command, without hand-editing rows".

Division of labour, so the two do not become a third source of truth:

* PROJECTIONS live in `_verify_wiki.py` and are imported here verbatim. The
  generator never re-implements one; if this file and the verifier disagree
  about what the canonical rows are, the assertion in `_projection()` fails.
* MIRRORED CELLS are rendered from those projections, so a row's mirrored cell
  is produced by the same expression the verifier compares it against.
* AUTHORED CELLS (the schema page's "what became legitimate" column, the target
  page's source-file column) are derived where they can be and preserved from
  the page where they cannot, never silently blanked.
* DERIVED PROSE that restates a derived number (the ticket counts, the
  intrusive-target arithmetic, the current schema version, the package binding)
  is rewritten here, because a number restated in prose is a number that goes
  stale on its own.
* Authored prose is NOT touched by this tool. It only normalizes line endings
  across the page.

Run from the project root:
    python .saipen/extensions/subs/saiwiki/kitchen/_regen_wiki.py --dry-run
    python .saipen/extensions/subs/saiwiki/kitchen/_regen_wiki.py

Writes only inside kitchen/wiki/. The main tree is read, never written.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
WIKI = HERE / "wiki"
ROOT = HERE.parents[4]
NEWLINE = "\r\n"

SEP_ROW = re.compile(r"^\|[-: |]+\|$")
MARKER_PART = re.compile(r"(<!-- mirrors: .+? )rows \d+-\d+ sha256:[0-9a-f]{16}( -->)")

# The page heading restates the mirrored range; two formats, both three-group
# replacements over a heading that already carries the range.
H1_SPECS = {
    "Tickets.md": (
        re.compile(r"^# Tickets \(T-\d+ \.\. T-\d+\)$", re.M),
        "# Tickets (T-{first:03d} .. T-{last:03d})",
    ),
    "Sources.md": (
        re.compile(r"^# Sources \(SRC-\d+ \.\. SRC-\d+\)$", re.M),
        "# Sources (SRC-{first:03d} .. SRC-{last:03d})",
    ),
    "SchemaBoundaries.md": (
        re.compile(r"^# Config schema boundaries \(\d+ \.\. \d+\)$", re.M),
        "# Config schema boundaries ({first} .. {last})",
    ),
    "TestSuites.md": (
        re.compile(r"^# CTest targets \(\d+ \.\. \d+\)$", re.M),
        "# CTest targets ({first} .. {last})",
    ),
}

# The Index page's "Mirrors" column: authored labels, one per mirrored page.
# A page with a marker and no label here is a hard failure, not a blank cell.
INDEX_LABELS = {
    "Tickets.md": "`.saipen/BOARD.md` ticket ledger",
    "Sources.md": "intake receipts",
    "RoadmapMVP.md": "`ROADMAP/roadmap/mvp/`",
    "RoadmapFuture.md": "`ROADMAP/roadmap/future/`",
    "ClickStyles.md": "`ClickStyle` enum",
    "TrailStyles.md": "`TrailStyle` enum",
    "SparkleModes.md": "`TrailSparkleMode` enum",
    "SchemaBoundaries.md": "schema boundary constants",
    "TestSuites.md": "CTest target registrations",
    "Modules.md": "`src/` layout and include graph",
}

# The one CTest target that injects synthetic input into the real desktop.
INTRUSIVE_TARGETS = ("protrail_input_dispatch",)

# The target whose paragraph is worth writing under the table. "Newest" is NOT
# derivable from the registration ordinal -- an ordinal is a position, not a
# date, which is the whole reason the mirror is built by name -- so the target
# is named here and its ordinal derived. A name typo fails the run instead of
# producing confident prose about a target that does not exist.
NOTABLE_TARGET = "protrail_autostart_tests"

# Authored prose for a schema boundary that has NO row on the page yet. A new
# canonical boundary without an entry here fails the run, so a boundary can
# never enter the page as an empty cell.
AUTHORED_SCHEMA_NEW = {
    10: "per-user Windows autostart (`app.start_with_windows`); absent key at any schema <= 9 reads as OFF",
}

# Prose that restates a derived number, so it is generated.
SCHEMA_CURRENT_RE = re.compile(
    r"`AppConfig::kCurrentSchemaVersion` is currently \*\*\d+\*\*\."
)
TICKETS_COUNTS_HEAD = "## Counts"
TICKETS_COUNTS_TAIL = "## How to read a row"
TESTS_NOTE_HEAD = "## The intrusive target"
TESTS_NOTE_TAIL = "## Sources"
INDEX_PAGES_HEAD = "## Pages"
INDEX_PAGES_TAIL = "## One honest deviation"

# Authored prose that had to change because a fact it states changed. Each pair
# is applied to LF-normalized text; an entry whose OLD text is gone and whose NEW
# text is present is already applied (so the tool is re-runnable), and an entry
# where neither is present is a FAILURE, because a silently skipped prose edit is
# how a page keeps a stale fact while reporting success.
PROSE_EDITS: dict[str, list[tuple[str, str]]] = {
    "Tickets.md": [
        (
            "- **Provenance** lists which machine fields the row carries. `compacted` means\n"
            "  Core externalized the full record; the pointer is under the project's own\n"
            "  `recovery/board-compaction/` tree and is deliberate Core state, not a defect.",
            "- **Provenance** lists which machine fields the row carries, by one stated\n"
            "  rule rather than by hand: `compacted` first when the canonical title is\n"
            "  truncated, then the remaining field names alphabetically, `verify` prose\n"
            "  and the empty `needs` placeholder omitted, and a value shown for the two\n"
            "  keys whose value is itself an identifier (`source_receipts`,\n"
            "  `blocker_scope`). It is the one column that is neither the ID nor the\n"
            "  canonical title, so it is derived rather than transcribed. `compacted`\n"
            "  means Core externalized the full record; the pointer is under the\n"
            "  project's own `recovery/board-compaction/` tree and is deliberate Core\n"
            "  state, not a defect.",
        ),
    ],
    "Sources.md": [
        (
            "`SRC-001`, which projected a single ticket and therefore has a work link.",
            "`SRC-001` and `SRC-005`, which each projected a single ticket and therefore\n"
            "have a work link. `SRC-005` is the T-26/T-27 integrity-repair handoff, and its\n"
            "single clause reached `VERIFIED` through T-28.",
        ),
        (
            "Requirement IDs are stable clause identities of the form `SRC-NNN:RNNN`.\n"
            "\n"
            "| Clause | Class | Disposition | Work | Note |\n"
            "|---|---|---|---|---|\n"
            "| SRC-004:R1 | acceptance-criterion | VERIFIED | T-23 | user visual acceptance of the combined T-23 + T-24 + T-25 artifact |\n"
            "| SRC-004:R2 | requirement | BLOCKED | T-26 | world-space Hold Motion Wake; awaiting the user's visual confirmation |\n"
            "| SRC-004:R3 | requirement | BLOCKED | T-27 | Hold FX controls; awaiting the same visual confirmation |\n"
            "\n"
            "Clauses `R2` and `R3` are deliberately **not** terminal. The work is implemented\n"
            "and its automated evidence is green, but the new creative behaviour must not be\n"
            "marked done before the user has looked at it, so the honest disposition is\n"
            "`BLOCKED` and the source stays unresolved until then. Nothing here is closed\n"
            "early to make the ledger tidy.",
            "Requirement IDs are stable clause identities. The numbering shape is not\n"
            "uniform -- the older `SRC-004` ledger names its clauses `R1`..`R3` while\n"
            "`SRC-005` names its single clause `R001` -- and both are mirrored exactly as\n"
            "written, because normalizing them here would create a second identity for the\n"
            "same clause.\n"
            "\n"
            "| Clause | Class | Disposition | Work | Note |\n"
            "|---|---|---|---|---|\n"
            "| SRC-004:R1 | acceptance-criterion | VERIFIED | T-23 | user visual acceptance of the combined T-23 + T-24 + T-25 artifact |\n"
            "| SRC-004:R2 | requirement | BLOCKED | T-26 | world-space Hold Motion Wake; the linked ticket is DONE, this clause is not |\n"
            "| SRC-004:R3 | requirement | BLOCKED | T-27 | Hold FX controls; the same divergence |\n"
            "| SRC-005:R001 | requirement | VERIFIED | T-28 | T-26/T-27 integrity repair; verified through the T-28 cycle |\n"
            "\n"
            "`R2` and `R3` are deliberately **not** terminal. The work is implemented and\n"
            "its automated evidence is green, but the new creative behaviour must not be\n"
            "marked done before the user has looked at it, so the honest disposition was\n"
            "`BLOCKED`, and nothing here is closed early to make the ledger tidy.\n"
            "\n"
            "### A divergence this page does not smooth over\n"
            "\n"
            "The project's two canonical sources disagree about T-26 and T-27:\n"
            "\n"
            "| Source | Says |\n"
            "|---|---|\n"
            "| `.saipen/BOARD.md` | T-26 and T-27 are `DONE`, checked boxes in the `DONE` section, each with an owner, a claim time and `closure_mode: own_patch` |\n"
            "| the coverage ledger | `SRC-004:R2` and `SRC-004:R3` are `BLOCKED` and both are listed in the receipt's `unresolved` set |\n"
            "\n"
            "Both were read at the same moment and both are mirrored as they are. The\n"
            "likely reading is that the tickets closed while the clause ledger was not\n"
            "re-dispositioned, which would make this bookkeeping rather than a\n"
            "contradiction about the product -- but that is a reading, not a fact, so it\n"
            "is labelled as one. Until Core disposits them, `SRC-004` legitimately stays\n"
            "`ACTIVE` with two unresolved clauses.",
        ),
        (
            "| SRC-004 | EXECUTE/COVER in progress (2 clauses unresolved); body hot in `intake/active/` |",
            "| SRC-004 | EXECUTE/COVER in progress (2 clauses unresolved); body hot in `intake/active/` |\n"
            "| SRC-005 | COVER terminal (1/1 VERIFIED); body still hot in `intake/active/` |",
        ),
    ],
    "SchemaBoundaries.md": [
        ("## The two migration shapes", "## The migration shapes"),
        (
            "A new install gets the feature. An existing user who merely upgraded the\n"
            "executable does **not** silently acquire a new mouse gesture or a materially new\n"
            "visual behaviour: their config predates the field, so the absent key is read as\n"
            "OFF rather than as the struct default. Once they enable it and save, the current\n"
            "schema persists it and reload preserves it.",
            "A new install gets the feature. An existing user who merely upgraded the\n"
            "executable does **not** silently acquire a new mouse gesture or a materially new\n"
            "visual behaviour: their config predates the field, so the absent key is read as\n"
            "OFF rather than as the struct default. Once they enable it and save, the current\n"
            "schema persists it and reload preserves it.\n"
            "\n"
            "**Opt-in OS side effect (schema 10).** `app.start_with_windows` is the one\n"
            "boundary whose effect lands outside the configuration file, in the per-user\n"
            "Windows Run key, so its gate has to say two things at once:\n"
            "\n"
            "| Situation | Result |\n"
            "|---|---|\n"
            "| key present, any source schema | the persisted value wins; a value the user's own machine wrote is a decision, not something to re-derive |\n"
            "| key absent, source schema <= 9 | **OFF**, and the Run key is not touched |\n"
            "| key absent, fresh schema-10 config | OFF -- `start_with_windows` is defaulted `false` in the struct today, so a fresh config is opt-in like an upgraded one |\n"
            "| preference ON, registered command stale | reconciled from the real executable path on the next start |\n"
            "\n"
            "The absent-key direction is the load-bearing one: upgrading the executable\n"
            "must never enroll an existing user into Windows startup. Its counterpart is\n"
            "that the schema gate is not the only guard -- reconciliation is driven by the\n"
            "saved preference, never by the schema bump, so raising the schema cannot by\n"
            "itself change what the machine does at sign-in.\n"
            "\n"
            "That last row is also why the `--startup-minimized` argument is minted by\n"
            "the command builder and parsed from the same constant: the registry command\n"
            "and the launch mode are one contract, not two strings that have to agree.",
        ),
        (
            "The four Hold multipliers (`hold_intensity`, `hold_wake_density`,\n"
            "`hold_wake_lifetime_ms`, `hold_release_strength`) are a third shape: they are\n"
            "clamped to hard bounds and each is exactly the identity at 1.0, so a default\n"
            "config reproduces the previously accepted appearance.",
            "The four Hold multipliers (`hold_intensity`, `hold_wake_density`,\n"
            "`hold_wake_lifetime_ms`, `hold_release_strength`) are a fourth shape. They are\n"
            "gated by the schema-9 boundary like the toggles beside them -- a schema <= 8\n"
            "writer's stray key is ignored and the struct defaults stand -- and they are\n"
            "additionally clamped to hard bounds with each exactly the identity at 1.0. The\n"
            "gate is what matters: the `else` branch never assigns them, so where the toggle\n"
            "is explicitly forced OFF the multipliers simply keep the defaults that\n"
            "reproduce the previously accepted appearance, rather than half-adopting a\n"
            "behaviour the file never asked for.",
        ),
    ],
    "Modules.md": [
        (
            "| `src/app/` | main.cpp, application.{h,cpp}, single_instance.{h,cpp}, startup_paths.{h,cpp} | entry point, application lifecycle, single-instance protocol, path resolution |",
            "| `src/app/` | main.cpp, application.{h,cpp}, single_instance.{h,cpp}, startup_paths.{h,cpp}, startup_mode.{h,cpp} | entry point, application lifecycle, single-instance protocol, path resolution, startup-argument classification |",
        ),
        (
            "| `src/platform/` | mouse_input.{h,cpp}, dpi_awareness.{h,cpp}, button_flags.h, timestamp.h | raw input, DPI awareness, button-transition vocabulary, QPC timestamps |",
            "| `src/platform/` | mouse_input.{h,cpp}, dpi_awareness.{h,cpp}, autostart.{h,cpp}, button_flags.h, timestamp.h | raw input, DPI awareness, per-user autostart registry access, button-transition vocabulary, QPC timestamps |",
        ),
        (
            "No area includes `../ui/` except `app`. Nothing below `app` reaches upward.",
            "No area includes `../ui/` except `app`. Nothing below `app` reaches upward.\n"
            "\n"
            "The two files added for silent autostart add no edge that was not already\n"
            "there. `src/platform/autostart.h` includes no heading at all: the registry\n"
            "surface sits behind an interface precisely so the whole feature can be driven\n"
            "by an in-memory double, and a test can include it without dragging in `app`.",
        ),
        (
            "## Two files whose folder does not fully describe them",
            "## Three files whose folder does not fully describe them",
        ),
        (
            "- `src/render/render_color.h` appears only inside `render`. The colour type the\n"
            "  effects use does not come from here, so there is no `effects -> render` edge\n"
            "  despite the shared concept.",
            "- `src/render/render_color.h` appears only inside `render`. The colour type the\n"
            "  effects use does not come from here, so there is no `effects -> render` edge\n"
            "  despite the shared concept.\n"
            "- `src/app/startup_mode.h` is a pure command-line parser in the `app` layer\n"
            "  that includes `../platform/autostart.h` for exactly one reason: the startup\n"
            "  argument constant is minted by the registry-command builder, so the command\n"
            "  ProTrail writes and the mode it parses cannot drift apart. It pulls in no Qt\n"
            "  and no Windows header, which is why `tests/test_autostart.cpp` can include an\n"
            "  `app`-layer header directly.",
        ),
    ],
}

failures: list[str] = []


def _load(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise SystemExit(f"cannot load {path}")
    mod = importlib.util.module_from_spec(spec)
    # Registered BEFORE execution: a module that defines a dataclass resolves
    # its own `__module__` through sys.modules, and an unregistered module makes
    # `@dataclass` raise AttributeError on None.
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


V = _load(HERE / "_verify_wiki.py", "_verify_wiki")

if V.ROOT != ROOT:  # both resolve the project root; they must agree
    raise SystemExit(
        f"run from the project root: this tool resolves {ROOT}, the verifier "
        f"resolves {V.ROOT}"
    )


def saipen_home() -> Path:
    text = (ROOT / ".saipen" / "STATE.md").read_text(encoding="utf-8")
    m = re.search(r"^saipen_home:\s*\"?([^\"]+?)\"?\s*$", text, re.M)
    if m is None:
        raise SystemExit("saipen_home not found in .saipen/STATE.md")
    return Path(m.group(1))


def source_identity() -> tuple[str, str, str]:
    """(source_head, source_tree_fingerprint, role_revision) from the engine."""
    home = saipen_home()
    tools = home / "tools"
    sys.path.insert(0, str(tools))
    freshness = _load(tools / "freshness.py", "saipen_freshness")
    ident = freshness.compute_source_identity(ROOT)
    charter = ROOT / ".saipen" / "extensions" / "subs" / "saiwiki.md"
    derived = freshness.compute_role_revision(charter)
    declared = re.search(
        r"^role_revision:\s*\"(sha256:[0-9a-f]{64})\"\s*$",
        charter.read_text(encoding="utf-8"),
        re.M,
    )
    if declared is None or declared.group(1) != derived:
        # A declared revision that differs from the derived one is exactly the
        # drift PROTOCOL.md section 3.1 forbids; binding the package to it would
        # certify a charter nobody ships.
        raise SystemExit(
            f"charter role_revision mismatch: declared "
            f"{declared.group(1) if declared else None}, derived {derived}"
        )
    return ident.source_head, ident.source_tree_fingerprint, derived


# --- table surgery ------------------------------------------------------------


def table_bounds(lines: list[str], start: int = 0) -> tuple[int, int, int]:
    """(header, separator, first row index after the separator)."""
    for i in range(start, len(lines) - 1):
        if lines[i].startswith("|") and SEP_ROW.match(lines[i + 1]):
            row = i + 2
            return i, i + 1, row
    raise SystemExit("no markdown table found")


def table_end(lines: list[str], row: int) -> int:
    j = row
    while j < len(lines) and lines[j].startswith("|"):
        j += 1
    return j


def page_rows(text: str, fmt) -> dict[int, str]:
    lines = text.split("\n")
    _, _, row = table_bounds(lines)
    end = table_end(lines, row)
    out = {}
    for line in lines[row:end]:
        n = fmt(line.split("|")[1].strip())
        if n is not None:
            out[n] = line
    return out


def set_first_table(text: str, rows: list[str]) -> str:
    return set_table_at(text, 0, rows)


def set_table_after(text: str, heading: str, rows: list[str]) -> str:
    return set_table_at(text, text.index(heading), rows)


def set_table_at(text: str, start: int, rows: list[str]) -> str:
    lines = text.split("\n")
    offset = len(text[:start].split("\n")) - 1
    _, _, row = table_bounds(lines, offset)
    end = table_end(lines, row)
    if lines[row:end] == rows:
        return text
    return "\n".join(lines[:row] + rows + lines[end:])


def set_region(text: str, head: str, tail: str, body: str) -> str:
    start = text.index(head)
    end = text.index(tail, start)
    new = text[:start] + body + "\n\n" + text[end:]
    return new


def set_marker(text: str, lo: int, hi: int, digest: str) -> str:
    def sub(m: re.Match[str]) -> str:
        return f"{m.group(1)}rows {lo}-{hi} sha256:{digest}{m.group(2)}"

    new, n = MARKER_PART.subn(sub, text, count=1)
    if n != 1:
        raise SystemExit("no mirrors marker to update")
    return new


def set_h1(text: str, page: str, first: int, last: int) -> str:
    pattern, template = H1_SPECS[page]
    new, n = pattern.subn(template.format(first=first, last=last), text, count=1)
    if n != 1:
        raise SystemExit(f"{page}: heading carrying the range not found")
    return new


# --- rich derivations (asserted equal to the verifier's projection) -----------


def board_rich() -> dict[int, dict]:
    text = (ROOT / ".saipen" / "BOARD.md").read_text(encoding="utf-8")
    section = None
    out: dict[int, dict] = {}
    for line in text.splitlines():
        heading = re.match(r"^## (\w+)$", line)
        if heading:
            section = heading.group(1)
            continue
        m = re.match(r"^- \[( |x)\] (T-\d+)(?: \[(P\d)\])? (.*)$", line)
        if not m:
            continue
        checked, ident, pri, rest = m.group(1), m.group(2), m.group(3), m.group(4)
        title = rest.split(" | ")[0].strip()
        # Machine fields only, and parsing STOPS at `verify`: the verify prose
        # is free text that may itself contain " | ", so anything after it is
        # not a field at all. Unknown keys are ignored rather than guessed at,
        # which is why this is a whitelist and not "everything with a colon".
        known = {
            "detail_ref",
            "owner",
            "claim_time",
            "closure_mode",
            "source_receipts",
            "user_explicit",
            "blocker",
            "blocker_scope",
        }
        fields: dict[str, str] = {}
        for part in rest.split(" | ")[1:]:
            key, _, value = part.partition(":")
            key = key.strip()
            if key == "verify":
                break
            if key in known:
                fields[key] = value.strip()
        num = int(ident.split("-")[1])
        out[num] = {
            "id": ident,
            "state": "DONE" if checked == "x" else (section or "?"),
            "pri": pri or "--",
            "title": title,
            "fields": fields,
        }
    assert_projection(
        "BOARD.md",
        [(n, r["title"]) for n, r in sorted(out.items())],
        V.board_rows(),
    )
    return out


def assert_projection(name: str, mine: list[tuple[int, str]], theirs: list[tuple[int, str]]) -> None:
    if mine != theirs:
        raise SystemExit(
            f"{name}: generator derivation and verifier projection disagree\n"
            f"  generator: {mine[:3]}...\n  verifier : {theirs[:3]}..."
        )


def provenance(fields: dict[str, str], title: str) -> str:
    """Machine fields the row carries, in a rule-stated order.

    `compacted` first when the canonical title is truncated; `needs` (an empty
    placeholder) and `verify` (prose) are omitted; everything else is listed by
    name, alphabetically, with `key: value` for the two keys whose value is
    itself an identifier.
    """
    parts = ["compacted"] if title.endswith("...") else []
    valued = {"source_receipts", "blocker_scope"}
    for key in sorted(fields):
        parts.append(f"{key}: {fields[key]}" if key in valued else key)
    return ", ".join(parts) if parts else "--"


def run_collapse(nums: list[int], ids: dict[int, str]) -> str:
    """Collapse contiguous IDs, but never across a zero-padding style change.

    The ledger switched from `T-001` to `T-23` at T-23; collapsing across that
    boundary would print `T-001 .. T-31`, which reads as a typo and hides the
    change. Runs stop where the canonical ID text changes shape.
    """

    def padded(n: int) -> bool:
        return ids[n] != f"T-{n}"

    runs: list[str] = []
    i = 0
    while i < len(nums):
        j = i
        while (
            j + 1 < len(nums)
            and nums[j + 1] == nums[j] + 1
            and padded(nums[j + 1]) == padded(nums[i])
        ):
            j += 1
        if j - i >= 2:
            runs.append(f"{ids[nums[i]]} .. {ids[nums[j]]}")
        else:
            runs.extend(ids[nums[k]] for k in range(i, j + 1))
        i = j + 1
    return ", ".join(runs)


def source_rich() -> list[tuple[int, list[str]]]:
    """Rows for the receipts page: (id, six rendered cells without pipes)."""
    rich = []
    for i in V.source_ids():
        out = subprocess.run(
            ["cmd", "/c", str(V.SAIPEN), "source", "status", f"SRC-{i:03d}", "--json"],
            capture_output=True,
            text=True,
        ).stdout
        d = json.loads(out)
        if not d.get("ok"):
            raise SystemExit(f"SRC-{i:03d} on disk but ledger says {d}")
        c = d["coverage"]
        disp = ", ".join(f"{k}={v}" for k, v in sorted(c["dispositions"].items()))
        rich.append(
            (
                i,
                [
                    f"SRC-{i:03d}",
                    d["source_kind"],
                    d["status"],
                    d["linked_work"] or "--",
                    f"{c['actionable']}/{c['terminal']}",
                    disp,
                ],
            )
        )
    assert_projection(
        "intake receipts",
        [(n, " | ".join(cells[1:6])) for n, cells in rich],
        V.source_rows(),
    )
    return rich


def test_rich() -> list[tuple[int, list[str]]]:
    """CTest targets with their source files, read from CMakeLists.txt."""
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    lines = cmake.splitlines()
    exe_blocks: dict[str, list[str]] = {}
    current = None
    for line in lines:
        m = re.search(r"add_executable\((\S+)", line)
        if m:
            current = m.group(1)
            exe_blocks[current] = []
            continue
        if current is not None:
            for src in re.findall(r"\btests/[\w./-]+", line):
                if src not in exe_blocks[current]:
                    exe_blocks[current].append(src)
    rich = []
    for ordinal, m in enumerate(re.finditer(r"add_test\(NAME (\S+)", cmake), start=1):
        target = m.group(1)
        sources = exe_blocks.get(target, [])
        if not sources:
            failures.append(f"TestSuites.md: no test source found for target {target}")
            sources = ["--"]
        rich.append((ordinal, [str(ordinal), target, ", ".join(sources)]))
    assert_projection(
        "CTest targets",
        [(n, cells[1]) for n, cells in rich],
        V.test_rows(),
    )
    return rich


def schema_rich() -> list[tuple[int, list[str]]]:
    """Schema boundaries: number + constant name derived, description authored."""
    existing = page_rows(
        (WIKI / "SchemaBoundaries.md").read_text(encoding="utf-8"), V.f_number
    )
    described: dict[int, str] = {}
    for num, line in existing.items():
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) >= 3 and cells[2]:
            described[num] = cells[2]
    rich = []
    for num, name in V.schema_rows():
        text = described.get(num) or AUTHORED_SCHEMA_NEW.get(num)
        if not text:
            # Fails the run rather than shipping a boundary with no description:
            # an empty cell here is exactly the "forgot to document it" case.
            raise SystemExit(
                f"SchemaBoundaries.md: boundary {num} (`{name}`) has no description "
                f"-- add it to AUTHORED_SCHEMA_NEW in this tool"
            )
        rich.append((num, [str(num), f"`{name}`", text]))
    return rich


# --- page writers -------------------------------------------------------------


def write(path: Path, text: str, dry: bool, summary: list[str]) -> None:
    """Write LF-normalized text out as CRLF, and only when it differs.

    The comparison is made against the LF form because reading normalizes
    newlines; comparing the CRLF form to a normalized read would report every
    page as rewritten on every run.
    """
    lf = text.replace("\r\n", "\n")
    if not lf.endswith("\n"):
        lf += "\n"
    data = lf.replace("\n", NEWLINE).encode("utf-8")
    # Compared as RAW BYTES. Reading normalizes newlines, so comparing decoded
    # text would call a file "unchanged" while it sat there with the wrong line
    # convention -- which is how four pages ended up LF in an otherwise CRLF
    # tree and the tool reported success on every run.
    if path.exists() and path.read_bytes() == data:
        summary.append(f"{path.name}: unchanged")
        return
    summary.append(f"{path.name}: rewritten ({len(data)} bytes, CRLF)")
    if not dry:
        path.write_bytes(data)


def page(path: str) -> str:
    return (WIKI / path).read_text(encoding="utf-8").replace("\r\n", "\n")


def gen_tickets(dry: bool, summary: list[str]) -> None:
    rich = board_rich()
    nums = sorted(rich)
    rows = [
        f"| {rich[n]['id']} | {rich[n]['state']} | {rich[n]['pri']} | "
        f"{rich[n]['title']} | {provenance(rich[n]['fields'], rich[n]['title'])} |"
        for n in nums
    ]
    text = page("Tickets.md")
    text = set_first_table(text, rows)
    text = set_marker(text, nums[0], nums[-1], V.digest([(n, rich[n]["title"]) for n in nums]))
    text = set_h1(text, "Tickets.md", nums[0], nums[-1])

    ids = {n: rich[n]["id"] for n in nums}
    counts: list[tuple[str, list[int]]] = []
    for state in ("DONE", "TODO", "BLOCKED", "DOING"):
        members = [n for n in nums if rich[n]["state"] == state]
        counts.append((state, members))
    body = [
        TICKETS_COUNTS_HEAD,
        "",
        "| State | Count | IDs |",
        "|---|---|---|",
    ]
    for state, members in counts:
        ids_text = run_collapse(members, ids) if members else "--"
        counts_text = str(len(members))
        body.append(f"| {state} | {counts_text} | {ids_text} |")
    body += [
        "",
        f"The ID space is contiguous: {nums[0]}..{nums[-1]} with no gaps, which is the",
        "invariant Core's own allocator guarantees. A missing ID here would mean the",
        "mirror skipped a row, not that the ticket never existed.",
    ]
    text = set_region(text, TICKETS_COUNTS_HEAD, TICKETS_COUNTS_TAIL, "\n".join(body))
    write(WIKI / "Tickets.md", text, dry, summary)


def gen_sources(dry: bool, summary: list[str]) -> None:
    rich = source_rich()
    nums = [n for n, _ in rich]
    rows = [
        "| " + " | ".join(cells) + " |" for _, cells in rich
    ]
    text = page("Sources.md")
    text = set_first_table(text, rows)
    text = set_marker(
        text,
        nums[0],
        nums[-1],
        V.digest([(n, " | ".join(cells[1:6])) for n, cells in rich]),
    )
    text = set_h1(text, "Sources.md", nums[0], nums[-1])
    write(WIKI / "Sources.md", text, dry, summary)


def gen_schema(dry: bool, summary: list[str]) -> None:
    rich = schema_rich()
    nums = [n for n, _ in rich]
    rows = ["| " + " | ".join(cells) + " |" for _, cells in rich]
    text = page("SchemaBoundaries.md")
    text = set_first_table(text, rows)
    text = set_marker(
        text, nums[0], nums[-1], V.digest([(n, name) for n, name in V.schema_rows()])
    )
    text = set_h1(text, "SchemaBoundaries.md", nums[0], nums[-1])

    current = re.search(
        r"kCurrentSchemaVersion = (\d+);",
        (ROOT / "src" / "config" / "app_config.h").read_text(encoding="utf-8"),
    )
    if current is None:
        raise SystemExit("kCurrentSchemaVersion not found")
    replaced, n = SCHEMA_CURRENT_RE.subn(
        f"`AppConfig::kCurrentSchemaVersion` is currently **{current.group(1)}**.",
        text,
        count=1,
    )
    if n != 1:
        failures.append("SchemaBoundaries.md: current-schema sentence not found")
    else:
        text = replaced
    write(WIKI / "SchemaBoundaries.md", text, dry, summary)


def gen_tests(dry: bool, summary: list[str]) -> None:
    rich = test_rich()
    nums = [n for n, _ in rich]
    targets = {cells[1]: n for n, cells in rich}
    rows = ["| " + " | ".join(cells) + " |" for _, cells in rich]
    text = page("TestSuites.md")
    text = set_first_table(text, rows)
    text = set_marker(
        text, nums[0], nums[-1], V.digest([(n, cells[1]) for n, cells in rich])
    )
    text = set_h1(text, "TestSuites.md", nums[0], nums[-1])

    missing = [t for t in (*INTRUSIVE_TARGETS, NOTABLE_TARGET) if t not in targets]
    if missing:
        raise SystemExit(f"TestSuites.md: target(s) named here no longer registered: {missing}")
    excluded = list(INTRUSIVE_TARGETS)
    total = len(nums)
    kept = total - len(excluded)
    body = [
        TESTS_NOTE_HEAD,
        "",
        f"`{excluded[0]}` (#{targets[excluded[0]]}) is the one target that injects synthetic",
        "input into the real desktop. It is a legitimate CTest target and it is",
        "registered like every other, but day-to-day verification runs exclude it and",
        "require explicit authorization to include it:",
        "",
        "```",
        "ctest --test-dir <build> -C Release --output-on-failure -E "
        + " ".join(excluded),
        "```",
        "",
        f"Under that exclusion the suite is **{kept}/{total}**, not {total}/{total}. A report claiming",
        f"{kept} of {total} without saying the intrusive target was excluded is ambiguous",
        "about which suite actually ran.",
        "",
        "## The autostart target",
        "",
        f"`{NOTABLE_TARGET}` (#{targets[NOTABLE_TARGET]}) was added with the silent-autostart work and",
        "is a non-intrusive target, so it runs in the ordinary suite. It drives the",
        "autostart platform component through an in-memory registry backend and reads",
        "the real per-user Run key before and after the run, which is what proves a",
        "routine CTest pass cannot enroll the machine.",
    ]
    text = set_region(text, TESTS_NOTE_HEAD, TESTS_NOTE_TAIL, "\n".join(body))
    write(WIKI / "TestSuites.md", text, dry, summary)


def gen_index(dry: bool, summary: list[str]) -> None:
    head, fingerprint, role = source_identity()
    text = page("Index.md")
    for label, pattern in (
        ("source_head", re.compile(r"(\| source_head \| `)[0-9a-f]+(` \|)")),
        (
            "source_tree_fingerprint",
            re.compile(r"(\| source_tree_fingerprint \| `)git-delta-v1:[0-9a-f]{64}(` \|)"),
        ),
        ("role_revision", re.compile(r"(\| role_revision \| `)sha256:[0-9a-f]+(` \|)")),
    ):
        value = {"source_head": head, "source_tree_fingerprint": fingerprint, "role_revision": role}[label]
        text, n = pattern.subn(lambda m, v=value: m.group(1) + v + m.group(2), text, count=1)
        if n != 1:
            failures.append(f"Index.md: {label} binding not found")

    rows = []
    for path in sorted(WIKI.glob("*.md")):
        if path.name == "Index.md":
            continue
        body = path.read_text(encoding="utf-8")
        label = INDEX_LABELS.get(path.name)
        if label is None:
            raise SystemExit(f"Index.md: no authored label for {path.name}")
        m = V.MARKER.search(body)
        if m:
            rows.append(
                f"| [{path.name}]({path.name}) | {label} | {m.group(2)} .. {m.group(3)} | `{m.group(4)}` |"
            )
        else:
            rows.append(f"| [{path.name}]({path.name}) | {label} | -- | derived, no marker |")
    # The binding table is the FIRST table on the page. Replacing the first
    # table unconditionally overwrote it with the page list -- the page list is
    # the table under its own heading, and is addressed by that heading.
    text = set_table_after(text, INDEX_PAGES_HEAD, rows)
    write(WIKI / "Index.md", text, dry, summary)


def gen_prose(dry: bool, summary: list[str]) -> None:
    """Apply the authored prose edits.

    Two rules, both learned the hard way on this package:

    * Idempotence is checked NEW-FIRST. Most of these edits are insertions, so
      the replacement CONTAINS the anchor; testing `old in text` first re-applies
      the insertion on every run and the page grows a duplicate paragraph each
      time. An earlier version of this tool did exactly that.
    * A doubled insertion is therefore repaired rather than re-applied: the
      anchor followed by its newly inserted text twice is collapsed to once.
      Repairing is reported, never silent.
    """
    for name, edits in sorted(PROSE_EDITS.items()):
        text = page(name)
        applied, skipped, repaired = 0, 0, 0
        for old, new in edits:
            if old in new and new != old:
                inserted = new[len(old):]
                doubled = old + inserted + inserted
                if doubled in text:
                    text = text.replace(doubled, old + inserted)
                    repaired += 1
            if new in text:
                skipped += 1
            elif old in text:
                text = text.replace(old, new, 1)
                applied += 1
            else:
                failures.append(
                    f"{name}: prose anchor not found and replacement absent -- "
                    f"the page and this tool have drifted apart: {old[:60]!r}"
                )
        if applied or skipped or repaired:
            write(WIKI / name, text, dry, summary)
            summary.append(
                f"{name}: prose edits applied={applied} already-present={skipped} "
                f"doubled-insertions-repaired={repaired}"
            )


def normalize_pages(dry: bool, summary: list[str]) -> None:
    """Force LF/CRLF uniformity on every page, including ones never generated.

    An edited page can end up with mixed line endings, and a producer-local
    artifact that is half one and half the other is a diff nobody can read.
    """
    for path in sorted(WIKI.glob("*.md")):
        write(path, page(path.name), dry, summary)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()
    summary: list[str] = []
    gen_tickets(args.dry_run, summary)
    gen_sources(args.dry_run, summary)
    gen_schema(args.dry_run, summary)
    gen_tests(args.dry_run, summary)
    gen_index(args.dry_run, summary)
    gen_prose(args.dry_run, summary)
    normalize_pages(args.dry_run, summary)
    print("WIKI_REGEN", "(dry run)" if args.dry_run else "")
    for line in summary:
        print("  ", line)
    if failures:
        for f in failures:
            print("  FAIL:", f)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
