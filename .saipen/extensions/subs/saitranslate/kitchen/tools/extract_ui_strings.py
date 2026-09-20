"""Inventory the project's real user-visible string literals.

Read-only evidence for the saitranslate coverage report: ProTrail has no i18n
layer (no .ts/.qm files, no QTranslator in the build), so the UI-strings
surface has no locale format to fill. This tool does not invent keys or a
bundle -- it lists the English literals that actually exist in the source,
with file:line and the file digest they came from.

Usage: python extract_ui_strings.py <project_root> <out_json>
"""

import hashlib
import json
import re
import sys
from pathlib import Path

# Files that actually paint or name user-visible text.
SOURCES = [
    "src/ui/settings_window.cpp",
    "src/ui/tray_icon.cpp",
    "src/app/application.cpp",
    "src/app/main.cpp",
]

# Contexts that produce something a user reads.
# The project's own text-creating helpers count as UI contexts too: those
# literals ARE the labels a user reads, even though no widget is built on
# that line.
CONTEXT = re.compile(
    r"(?:new QLabel|new QPushButton|new QCheckBox|QLabel\(|QPushButton\(|"
    r"QCheckBox\(|make_check\(|make_row_label\(|begin_section\(|"
    r"new SliderSpin\(|SliderSpin\(|new ColorEditor\(|ColorEditor\(|"
    r"setText\(|setWindowTitle\(|setToolTip\(|setAccessibleName\(|"
    r"setTitle\(|addAction\(|tr\(|"
    r"QMessageBox::(?:information|warning|critical))"
)
LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')

# Chrome/paths/log plumbing rather than user-facing UI text.
EXCLUDE = re.compile(
    r"[#%;{}]|QPushButton\[|::|\.cpp|\.h$|\\|/|^[a-z0-9_]+$|^rgba?\(|^px$|"
    r"^[0-9.]+ ?(?:px|ms|%)$|^https?://"
)


def literals_in(text: str) -> list[str]:
    out: list[str] = []
    for line_no, line in enumerate(text.splitlines(), start=1):
        if not CONTEXT.search(line):
            continue
        for raw in LITERAL.findall(line):
            value = raw.strip()
            if len(value) < 2 or EXCLUDE.search(value):
                continue
            if not any(ch.isalpha() for ch in value):
                continue
            if not any(ch.isspace() for ch in value) and len(value) < 4:
                continue
            out.append(f"L{line_no}: {value}")
    return out


def main() -> int:
    # Deliberately NOT .resolve(): this workspace lives on a virtual drive
    # letter that realpath mangles, while plain relative access works.
    root = Path(sys.argv[1])
    if not (root / "src").is_dir():
        print(f"not a ProTrail checkout: {root}", file=sys.stderr)
        return 2
    out_path = Path(sys.argv[2])
    report: dict[str, object] = {
        "surface": "ui-strings",
        "integratable": False,
        "reason": (
            "ProTrail ships no locale layer (.ts/.qm absent, no QTranslator "
            "installation in the build), so there is no existing locale format "
            "to fill. This inventory is read-only evidence for a future Core "
            "i18n ticket; it is deliberately NOT a bundle and must not be "
            "integrated as one."
        ),
        "files": [],
        "literal_count": 0,
    "method": "line-based scan of the declared source files for literals in UI contexts; a multi-line literal is recorded on its first line",
    }
    total = 0
    for rel in SOURCES:
        path = root / rel
        if not path.is_file():
            report["files"].append({"path": rel, "error": "missing"})
            continue
        data = path.read_bytes()
        found = literals_in(data.decode("utf-8", "replace"))
        total += len(found)
        report["files"].append(
            {
                "path": rel,
                "sha256": hashlib.sha256(data).hexdigest(),
                "literal_count": len(found),
                "literals": found,
            }
        )
    report["literal_count"] = total
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(f"ui strings: {total} literals from {len(report['files'])} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
