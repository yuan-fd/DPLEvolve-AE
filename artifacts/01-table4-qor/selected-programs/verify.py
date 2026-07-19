#!/usr/bin/env python3
"""Validate frozen source/input provenance and optional replay output."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path


def tree_digest(root: Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        digest.update(path.relative_to(root).as_posix().encode())
        digest.update(b"\0")
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    return digest.hexdigest()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--orfs-root", type=Path)
    parser.add_argument("--require-inputs", action="store_true")
    parser.add_argument("--case")
    parser.add_argument("--objective", choices=["hpwl", "ghr"], default="hpwl")
    parser.add_argument("--results", type=Path)
    args = parser.parse_args()

    manifest = json.loads((args.root / "manifest.json").read_text())
    programs = manifest["programs"]
    cases = {item["case"] for item in programs}
    if len(programs) != 9 or len(cases) != 9:
        raise SystemExit("[ERROR] manifest must contain nine unique cases")
    if args.case and args.case not in cases:
        raise SystemExit(f"[ERROR] unknown case: {args.case}")

    source_errors = []
    for item in programs:
        for objective in ("hpwl", "ghr"):
            track = item["tracks"][objective]
            source = (
                args.root / "inputs" / "programs" / objective
                / item["program"] / "dpl_evolve"
            )
            actual = tree_digest(source) if source.is_dir() else "missing"
            if actual != track["tree_sha256"]:
                source_errors.append(
                    f"{item['case']}/{objective}: expected {track['tree_sha256']}, got {actual}"
                )
    if source_errors:
        raise SystemExit("[ERROR] frozen source validation failed:\n  " + "\n  ".join(source_errors))
    print("[PASS] 18 HPWL/GHR frozen source programs match their SHA-256 digests")

    missing = []
    unpinned = []
    if args.orfs_root:
        for item in programs:
            if args.case and item["case"] != args.case:
                continue
            odb = (
                args.orfs_root
                / "flow" / "results" / item["platform"] / item["design"]
                / manifest["flow_variant"] / manifest["input_stage"]
            )
            if not odb.is_file():
                missing.append(f"{item['case']}: {odb}")
                continue
            expected_hash = item.get("input_odb_sha256")
            if expected_hash and sha256(odb) != expected_hash:
                raise SystemExit(f"[ERROR] {item['case']}: input ODB checksum mismatch: {odb}")
            if not expected_hash:
                unpinned.append(item["case"])
        if missing:
            denominator = 1 if args.case else 9
            print(f"[BLOCKED] exact paper ODB inputs missing: {len(missing)}/{denominator}")
            for entry in missing:
                print(f"  {entry}")
        else:
            print("[PASS] all nine paper-path ODB inputs are present")
        if unpinned:
            print("[WARN] present but no archived checksum: " + ", ".join(unpinned))

    if args.results:
        rows = list(csv.DictReader(args.results.open(newline=""), delimiter="\t"))
        if len(rows) != 1:
            raise SystemExit(f"[ERROR] expected one replay result, found {len(rows)}")
        row = rows[0]
        item = next(item for item in programs if item["case"] == args.case)
        track = item["tracks"][args.objective]
        if row["status"] != "PASS":
            raise SystemExit(f"[ERROR] replay status is {row['status']}")
        actual_hpwl = float(row["hpwl_after_micron"])
        drift_percent = abs(actual_hpwl - track["expected_hpwl"]) / track["expected_hpwl"] * 100
        if drift_percent > 0.05:
            raise SystemExit(
                f"[ERROR] replay HPWL drift {drift_percent:.4f}% exceeds 0.05% tolerance"
            )
        print(
            f"[PASS] {args.case}/{args.objective} replay HPWL={actual_hpwl:.1f}; "
            f"archived={track['expected_hpwl']:.1f}; drift={drift_percent:.4f}%"
        )

    if args.require_inputs and missing:
        return 2
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, KeyError, ValueError, csv.Error) as exc:
        raise SystemExit(f"[ERROR] {exc}") from None
