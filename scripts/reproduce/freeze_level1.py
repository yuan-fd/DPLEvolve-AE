#!/usr/bin/env python3
"""Freeze completed Level 1 calibration reviews into an immutable Level 2 packet."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


def tree_digest(root: Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        digest.update(path.relative_to(root).as_posix().encode())
        digest.update(b"\0")
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    return digest.hexdigest()


def find_records(round_root: Path) -> list[Path]:
    records = []
    for path in round_root.rglob("*.md"):
        name = path.name.lower()
        if "review" in name or name == "knowledge_card.md":
            records.append(path)
    return sorted(records)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--state-root", required=True, type=Path)
    parser.add_argument("--round", action="append", required=True, dest="rounds")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--children-per-case", required=True, type=int)
    args = parser.parse_args()

    sections = [
        "# Frozen ReviewDSE Level 1 Evidence",
        "",
        "This packet was generated before Level 2 target search. Level 2 may read",
        "it as prior mechanism/source-start evidence but must not modify it.",
        "",
        f"Calibration breadth: {args.children_per_case} Students per case, one iteration.",
        "The main paper does not state this Level 1 breadth; record the author-time",
        "value in the AE appendix before claiming exact search-process reproduction.",
        "",
        "## Complete Source Starts",
        "",
    ]
    seed_root = args.state_root / "seed_sources"
    for name in ("framework_dpl_evolve", "diamond_dpl_evolve", "default_negotiation_dpl_evolve"):
        source = seed_root / name
        if not (source / "CMakeLists.txt").is_file():
            raise SystemExit(f"[ERROR] missing prepared source start: {source}")
        sections.append(f"- `{name}`: SHA-256 tree `{tree_digest(source)}`; source `{source}`")

    record_count = 0
    for round_id in args.rounds:
        round_root = args.state_root / round_id / "teacher_rounds"
        if not round_root.is_dir():
            raise SystemExit(f"[ERROR] missing calibration round: {round_root}")
        records = find_records(round_root)
        if not records:
            raise SystemExit(f"[ERROR] no Teacher review or Student knowledge cards in {round_root}")
        sections.extend(["", f"## Calibration Round `{round_id}`", ""])
        for path in records:
            text = path.read_text(encoding="utf-8", errors="replace").strip()
            if not text:
                continue
            record_count += 1
            sections.extend(
                [
                    f"### Record `{path.relative_to(round_root)}`",
                    "",
                    text,
                    "",
                ]
            )

    if record_count == 0:
        raise SystemExit("[ERROR] Level 1 freeze found no non-empty reviewed records")
    sections.extend(["", f"Reviewed records frozen: {record_count}", ""])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(sections), encoding="utf-8")
    print(f"[PASS] froze {record_count} Level 1 records: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
