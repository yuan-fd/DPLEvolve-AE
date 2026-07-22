#!/usr/bin/env python3
"""Record hashes and provenance for freshly generated Table 4 input ODBs."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from datetime import datetime, timezone
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def revision(repo: Path) -> dict[str, str]:
    def git(*args: str) -> str:
        return subprocess.check_output(["git", "-C", str(repo), *args], text=True).strip()
    return {"commit": git("rev-parse", "HEAD"), "tree": git("rev-parse", "HEAD^{tree}")}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--orfs-root", required=True, type=Path)
    parser.add_argument("--flow-variant", required=True)
    parser.add_argument("--selected-manifest", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--case", action="append", dest="cases")
    args = parser.parse_args()

    selected = json.loads(args.selected_manifest.read_text(encoding="utf-8"))
    requested = set(args.cases or [item["case"] for item in selected["programs"]])
    inputs = []
    for item in selected["programs"]:
        if item["case"] not in requested:
            continue
        path = (
            args.orfs_root / "flow" / "results" / item["platform"] / item["design"]
            / args.flow_variant / selected["input_stage"]
        )
        constraint = path.with_name(selected.get("constraint_stage", "2_floorplan.sdc"))
        if not path.is_file():
            raise SystemExit(f"[ERROR] generated input missing: {path}")
        if not constraint.is_file():
            raise SystemExit(f"[ERROR] generated constraint missing: {constraint}")
        inputs.append(
            {
                "case": item["case"],
                "platform": item["platform"],
                "design": item["design"],
                "path": str(path),
                "size_bytes": path.stat().st_size,
                "sha256": sha256(path),
                "constraint_path": str(constraint),
                "constraint_size_bytes": constraint.stat().st_size,
                "constraint_sha256": sha256(constraint),
            }
        )
    if len(inputs) != len(requested):
        raise SystemExit("[ERROR] one or more requested cases are absent from the Table 4 manifest")
    record = {
        "schema_version": 1,
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "flow_variant": args.flow_variant,
        "input_stage": selected["input_stage"],
        "constraint_stage": selected.get("constraint_stage", "2_floorplan.sdc"),
        "orfs": revision(args.orfs_root),
        "openroad": revision(args.orfs_root / "tools" / "OpenROAD"),
        "inputs": inputs,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"[PASS] recorded {len(inputs)} generated ODB hashes: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
