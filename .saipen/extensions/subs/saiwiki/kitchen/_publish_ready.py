"""Publish the wiki as a machine-readable READY package (scratch tool, `_`-prefixed).

`kitchen/OUTBOX.md` is the PROTOCOL.md section 2 door, and it is what a human
reads. It is NOT what `saipen collect saiwiki` consumes: the targeted
complete-package path scans `.saipen/extensions/subs/saiwiki/READY/*.json`
(`producer.StagingGeneration.scan_ready`), so a package that exists only as
markdown reads as `NOT_READY` and Core answers "Not ready: run qq first."

This tool therefore builds the JSON artifact through the engine's OWN API rather
than by hand-writing JSON: `StagingGeneration.begin()` stages every payload under
`.prepare-staging/<generation>/payload/`, `set_package()` binds the identity, and
`publish()` revalidates every declared dependency, then promotes the generation
with one atomic replace. A hand-written JSON file would have to reproduce
`package_identity`, `dependency_fp` and the payload encoding -- a second
implementation of the engine's own contract, which is exactly the drift this
project keeps repairing.

Declared write targets are the eleven page paths, which live INSIDE this
producer's own namespace. That is deliberate: the integration DESTINATION is
Core's decision (this project has no docs tree), so the package declares the
artifacts it owns and carries their bytes, and the OUTBOX instructions say the
destination is Core's to choose. Declaring an invented main-tree destination here
would pre-empt a decision that is not the producer's.

Run from the project root:
    python .saipen/extensions/subs/saiwiki/kitchen/_publish_ready.py --dry-run
    python .saipen/extensions/subs/saiwiki/kitchen/_publish_ready.py
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
NS = HERE.parent
WIKI = HERE / "wiki"
ROOT = HERE.parents[4]
DISCOVERY_MODEL = "git-delta-v1"

# The canonical sources these pages are derived from. Declared as read_set, so
# the engine refuses to publish if any of them moved while the pages were being
# built, and collection refuses a package whose sources have since drifted.
READ_SOURCES = [
    ".saipen/BOARD.md",
    ".saipen/intake/index.json",
    ".saipen/intake/coverage/SRC-001.json",
    ".saipen/intake/coverage/SRC-004.json",
    ".saipen/intake/coverage/SRC-005.json",
    ".saipen/extensions/subs/PROTOCOL.md",
    ".saipen/extensions/subs/saiwiki.md",
    "CMakeLists.txt",
    "src/config/app_config.h",
    "src/effects/click_config.h",
    "src/effects/trail_config.h",
]


def load(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    state = (ROOT / ".saipen" / "STATE.md").read_text(encoding="utf-8")
    home = re.search(r'^saipen_home:\s*"?([^"]+?)"?\s*$', state, re.M)
    if home is None:
        raise SystemExit("saipen_home not found in .saipen/STATE.md")
    tools = Path(home.group(1)) / "tools"
    sys.path.insert(0, str(tools))
    freshness = load(tools / "freshness.py", "saipen_freshness")
    from saipen_engine import producer as P

    identity = freshness.compute_source_identity(ROOT)
    charter = ROOT / ".saipen" / "extensions" / "subs" / "saiwiki.md"
    role_revision = freshness.compute_role_revision(charter)

    read_sources = sorted(set(READ_SOURCES) | {p for p in _roadmap_paths(ROOT)})
    read_set = {}
    for rel in read_sources:
        digest = P.file_sha256(ROOT / rel)
        if digest == "sha256:absent":
            raise SystemExit(f"declared read source is missing: {rel}")
        read_set[rel] = digest

    pages = sorted(WIKI.glob("*.md"))
    if not pages:
        raise SystemExit("no wiki pages to publish")
    write_set = {}
    payloads = {}
    for path in pages:
        rel = path.relative_to(ROOT).as_posix()
        payloads[rel] = path.read_bytes()
        write_set[rel] = P.file_sha256(path)

    namespace = NS
    epoch = P.ProducerEpoch.current(namespace)
    print(f"namespace        {namespace}")
    print(f"source_head      {identity.source_head}")
    print(f"fingerprint      {identity.source_tree_fingerprint}")
    print(f"role_revision    {role_revision}")
    print(f"epoch            {epoch}")
    print(f"read_set         {len(read_set)} declared sources")
    print(f"write_set        {len(write_set)} payload files")
    if args.dry_run:
        print("DRY RUN: nothing staged, nothing published")
        return 0

    generation = P.StagingGeneration(namespace, "saiwiki").begin()
    for rel, content in payloads.items():
        generation.add_payload(rel, content)
    package = P.ProducerPackage(
        producer="saiwiki",
        role_revision=role_revision,
        base_source_head=identity.source_head,
        base_source_tree_fingerprint=identity.source_tree_fingerprint,
        base_discovery_model=DISCOVERY_MODEL,
        scope=(
            "wiki: the whole maintained page set, eleven pages covering every "
            "canonical ID space (tickets, receipts, roadmap milestones, style "
            "enums, schema boundaries, CTest targets) plus a derived module map "
            "and the package index"
        ),
        read_set=read_set,
        write_set=write_set,
        epoch=epoch,
    )
    print(f"package_identity {package.package_identity}")
    generation.set_package(package)
    result = generation.publish()
    print("publish          " + repr(result))
    if not result.get("ok"):
        return 1

    for identity_gone in retire_superseded(P, namespace, keep=package.package_identity):
        print(f"retired          {identity_gone} -> SETTLED/ (superseded)")

    ready, errors = P.StagingGeneration.scan_ready(namespace, materialize_payloads=False)
    print(f"scan_ready       {len(ready)} package(s), {len(errors)} error(s)")
    for item in ready:
        print(f"  {item.package_identity} status={item.status} scope={item.scope[:48]}")
    for err in errors:
        print("  ERROR:", err)
    return 0


def retire_superseded(P, namespace: Path, *, keep: str) -> list[str]:
    """Move any OTHER READY package out of the hot set, through the engine.

    `keep` is the identity just published and is never retired: an earlier
    version of this tool walked every file in READY/ and moved the package it
    had itself just published straight to SETTLED, leaving zero live packages
    and a retired copy of a live identity.

    Files are named by package identity, so republishing produces a second file
    rather than replacing the first, and two READY packages for one producer is
    ambiguous evidence. The engine owns the transition -- `_retire_ready_package`
    proves ownership, re-reads the artifact, requires it to be byte-consistent
    with what it is retiring, then moves it to SETTLED/ atomically -- so this
    calls that rather than unlinking a file itself.

    Retirement runs AFTER a successful publish: if publishing fails, the previous
    evidence stays exactly where it was.
    """
    retired: list[str] = []
    ready_dir = namespace / "READY"
    if not ready_dir.is_dir():
        return retired
    for path in sorted(ready_dir.glob("*.json")):
        try:
            identity = json.loads(path.read_text(encoding="utf-8")).get("package_identity")
        except (OSError, UnicodeDecodeError, json.JSONDecodeError):
            continue
        if not identity or identity == keep:
            continue
        package = P.StagingGeneration.ready_package(namespace, identity)
        if package is None:
            continue
        P._retire_ready_package(package, supersede_older=False)
        retired.append(identity)
    return retired


def _roadmap_paths(root: Path) -> list[str]:
    out = []
    for sub in ("mvp", "future"):
        base = root / "ROADMAP" / "roadmap" / sub
        if base.is_dir():
            out.extend(
                p.relative_to(root).as_posix() for p in sorted(base.glob("*.md"))
            )
    return out


if __name__ == "__main__":
    sys.exit(main())
