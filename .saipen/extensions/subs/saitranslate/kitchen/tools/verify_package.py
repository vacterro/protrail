"""Producer-side verification for the saitranslate package.

Reads only: the project README (source of truth) and this kitchen's own
locale/mirror surfaces. Proves the two things the charter requires of a
delivered locale surface:

  1. it carries `<!-- source-digest: README.md sha256:<16 hex> -->`;
  2. that digest equals the digest of the CURRENT README.md, so nothing
     delivered is a stale or silently refreshed translation.

Also reports every locale in the 32+Дед surface that is still missing, so
partial coverage is stated as a fact rather than rounded up.

Usage: python verify_package.py <project_root> <out_txt>
"""

import hashlib
import re
import sys
from pathlib import Path

# No .resolve(): this workspace sits on a virtual drive that realpath mangles.
ROOT = Path(__file__).parents[1]  # .../kitchen
KITCHEN = ROOT

# The 32-language surface plus the Дед voice (phases/translate.md).
SURFACE = [
    ("en", "English"), ("ru", "Russian"), ("et", "Estonian"),
    ("uk", "Ukrainian"), ("ja", "Japanese"), ("ded", "Дед"),
    ("de", "German"), ("fr", "French"), ("es", "Spanish"), ("it", "Italian"),
    ("pt", "Portuguese"), ("nl", "Dutch"), ("pl", "Polish"),
    ("sv", "Swedish"), ("da", "Danish"), ("fi", "Finnish"),
    ("no", "Norwegian"), ("zh", "Chinese"), ("ko", "Korean"), ("th", "Thai"),
    ("vi", "Vietnamese"), ("ar", "Arabic"), ("he", "Hebrew"),
    ("tr", "Turkish"), ("hi", "Hindi"), ("id", "Indonesian"),
    ("el", "Greek"), ("cs", "Czech"), ("ro", "Romanian"),
    ("hu", "Hungarian"), ("bg", "Bulgarian"), ("sk", "Slovak"),
    ("hr", "Croatian"),
]
MIRRORS = {
    "et": "README.ee.md",
    "ded": "README.ded.md",
    "ja": "README.ja.md",
    "ru": "README.ru.md",
    "uk": "README.uk.md",
}
MARKER = re.compile(r"<!-- source-digest: README\.md sha256:([0-9a-f]{16}) -->")


def main() -> int:
    project_root = Path(sys.argv[1])
    out_path = Path(sys.argv[2])
    readme = (project_root / "README.md").read_bytes()
    digest = hashlib.sha256(readme).hexdigest()[:16]

    lines: list[str] = []
    failures = 0
    delivered = 0
    missing: list[str] = []

    lines.append(f"source: README.md sha256:{digest} ({len(readme)} bytes)")
    lines.append("")
    for code, name in SURFACE:
        locale_file = KITCHEN / "locales" / code / "README.md"
        if not locale_file.is_file():
            missing.append(f"{code} ({name})")
            continue
        delivered += 1
        text = locale_file.read_text(encoding="utf-8")
        found = MARKER.findall(text)
        ok = found == [digest]
        mirror = MIRRORS.get(code)
        mirror_state = "-"
        if mirror is not None:
            mirror_file = KITCHEN / "mirrors" / mirror
            if not mirror_file.is_file():
                mirror_state = "MISSING"
                ok = False
            else:
                same = mirror_file.read_bytes() == locale_file.read_bytes()
                mirror_state = "identical" if same else "DIVERGED"
                ok = ok and same
        if not ok:
            failures += 1
        lines.append(
            f"{'ok  ' if ok else 'FAIL'} {code:<3} {name:<12} "
            f"digest={'match' if found == [digest] else found or 'none'} "
            f"mirror={mirror_state} bytes={locale_file.stat().st_size}"
        )

    lines.append("")
    lines.append(f"delivered locales: {delivered}/{len(SURFACE)}")
    lines.append(f"missing locales: {len(missing)}")
    for item in missing:
        lines.append(f"  pending {item}")
    lines.append("")
    lines.append(f"VERIFY: {'PASS' if failures == 0 else 'FAIL'} ({failures} failing surface(s))")
    lines.append(
        "VERDICT: draft -- default set (en, ru, et, uk, ja, ded) currently; "
        f"{len(missing)} locales remain"
    )

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
