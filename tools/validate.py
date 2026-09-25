"""Project-local entry point for the canonical SAIPEN validator."""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path


_SOURCE_REF = re.compile(
    r"(?<![A-Za-z0-9_./-])"
    r"((?:src|tests|resources|cmake|tools)/"
    r"[A-Za-z0-9_./-]+\.(?:cpp|cc|cxx|h|hpp|qrc|xpm|ps1|py|cmake|json|rc|in|ico|png|svg))"
)


def _check_source_closure(root: Path) -> int:
    cmake = root / "CMakeLists.txt"
    if not cmake.is_file():
        print("SOURCE_CLOSURE: CMakeLists.txt missing", file=sys.stderr)
        return 1
    paths = sorted(set(_SOURCE_REF.findall(cmake.read_text(encoding="utf-8-sig"))))
    missing = [path for path in paths if not (root / path).is_file()]
    if missing:
        print("SOURCE_CLOSURE: CMake references missing files: " + ", ".join(missing), file=sys.stderr)
        return 1
    print(f"SOURCE_CLOSURE_OK: {len(paths)} CMake-referenced files present")
    return 0


def _candidate_homes(root: Path) -> list[Path]:
    candidates: list[Path] = []
    env_home = os.environ.get("SAIPEN_HOME")
    if env_home:
        candidates.append(Path(env_home))

    state = root / ".saipen" / "STATE.md"
    if state.is_file():
        for line in state.read_text(encoding="utf-8-sig").splitlines():
            if line.startswith("saipen_home:"):
                value = line.split(":", 1)[1].strip().strip('"')
                if value:
                    candidates.append(Path(value))
                break

    candidates.extend(
        [
            Path.home() / ".config" / "opencode" / "skills" / "saipen",
            Path.home() / ".agents" / "skills" / "saipen",
        ]
    )
    return candidates


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gate", required=True)
    parser.add_argument("--project-root", default=".")
    args = parser.parse_args()

    root = Path(args.project_root).resolve()
    if args.gate.lower() == "ship" and _check_source_closure(root) != 0:
        return 1
    for home in _candidate_homes(root):
        validator = home / "tools" / "validate.py"
        if validator.is_file() and validator.resolve() != Path(__file__).resolve():
            return subprocess.run(
                [
                    sys.executable,
                    str(validator),
                    "--project-root",
                    str(root),
                    "--gate",
                    args.gate,
                ],
                cwd=root,
                check=False,
            ).returncode

    print("canonical SAIPEN validator not found", file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
