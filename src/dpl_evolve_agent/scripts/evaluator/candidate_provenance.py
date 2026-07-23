#!/usr/bin/env python3
"""Capture and verify the source/binary/evaluator chain for one candidate."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import stat
import subprocess
from pathlib import Path
from typing import Any


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _git_blob_id(data: bytes) -> str:
    digest = hashlib.sha1()
    digest.update(f"blob {len(data)}\0".encode())
    digest.update(data)
    return digest.hexdigest()


def source_worktree_fingerprint(repo: Path) -> str:
    raw = subprocess.check_output(
        ["git", "-C", str(repo), "ls-files", "-z", "--cached", "--others", "--exclude-standard"]
    )
    records: list[tuple[bytes, bytes, bytes]] = []
    for raw_name in raw.split(b"\0"):
        if not raw_name:
            continue
        name = os.fsdecode(raw_name)
        path = repo / name
        mode = path.lstat().st_mode
        if stat.S_ISLNK(mode):
            git_mode = b"120000"
            data = os.readlink(path).encode()
        elif stat.S_ISREG(mode):
            git_mode = b"100755" if mode & stat.S_IXUSR else b"100644"
            data = path.read_bytes()
        else:
            continue
        records.append((raw_name, git_mode, _git_blob_id(data).encode()))
    digest = hashlib.sha256()
    for name, mode, blob in sorted(records):
        digest.update(mode + b" " + name + b"\0" + blob + b"\n")
    return digest.hexdigest()


def source_ref_fingerprint(repo: Path, ref: str) -> str:
    raw = subprocess.check_output(
        ["git", "-C", str(repo), "ls-tree", "-rz", "--full-tree", ref]
    )
    records: list[tuple[bytes, bytes, bytes]] = []
    for record in raw.split(b"\0"):
        if not record:
            continue
        metadata, name = record.split(b"\t", 1)
        mode, object_type, object_id = metadata.split(b" ", 2)
        if object_type != b"blob":
            continue
        records.append((name, mode, object_id))
    digest = hashlib.sha256()
    for name, mode, blob in sorted(records):
        digest.update(mode + b" " + name + b"\0" + blob + b"\n")
    return digest.hexdigest()


def protected_snapshot(paths: list[Path]) -> list[dict[str, Any]]:
    return [
        {
            "path": str(path.resolve()),
            "sha256": file_sha256(path),
            "size_bytes": path.stat().st_size,
        }
        for path in paths
    ]


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def capture_build(args: argparse.Namespace) -> int:
    source = args.source.resolve()
    binary = args.binary.resolve()
    payload = {
        "schema_version": 1,
        "status": "complete",
        "source_repo": str(source),
        "source_fingerprint": source_worktree_fingerprint(source),
        "binary": str(binary),
        "binary_sha256": file_sha256(binary),
        "binary_size_bytes": binary.stat().st_size,
    }
    write_json(args.output, payload)
    return 0


def start_evaluation(args: argparse.Namespace) -> int:
    build = load_json(args.build_provenance)
    source_hash = source_worktree_fingerprint(args.source.resolve())
    binary_hash = file_sha256(args.binary.resolve())
    problems = []
    if build.get("source_fingerprint") != source_hash:
        problems.append("source_changed_since_build")
    if build.get("binary_sha256") != binary_hash:
        problems.append("binary_changed_since_build")
    payload = {
        "schema_version": 1,
        "status": "ready" if not problems else "invalid",
        "problems": problems,
        "source_repo": str(args.source.resolve()),
        "source_fingerprint": source_hash,
        "binary": str(args.binary.resolve()),
        "binary_sha256": binary_hash,
        "build_provenance": str(args.build_provenance.resolve()),
        "protected_files": protected_snapshot(args.protected_file),
    }
    write_json(args.output, payload)
    return 0 if not problems else 3


def finish_evaluation(args: argparse.Namespace) -> int:
    start = load_json(args.start_provenance)
    metrics = load_json(args.metrics)
    summary = load_json(args.summary)
    problems = list(start.get("problems") or [])
    source_hash = source_worktree_fingerprint(args.source.resolve())
    binary_hash = file_sha256(args.binary.resolve())
    if start.get("source_fingerprint") != source_hash:
        problems.append("source_changed_during_evaluation")
    if start.get("binary_sha256") != binary_hash:
        problems.append("binary_changed_during_evaluation")
    for item in start.get("protected_files") or []:
        path = Path(item["path"])
        if not path.is_file() or file_sha256(path) != item.get("sha256"):
            problems.append("protected_evaluator_changed")
            break
    run_contract = (metrics.get("manifest") or {}).get("protected_evaluation") or {}
    if not run_contract.get("unchanged"):
        problems.append("protected_run_contract_failed")
    metric_verdict = summary.get("eligibility") or {}
    if not metric_verdict.get("eligible"):
        problems.append("metric_eligibility_failed")
    payload = {
        **start,
        "status": "verified" if not problems else "invalid",
        "problems": list(dict.fromkeys(problems)),
        "source_fingerprint_after": source_hash,
        "binary_sha256_after": binary_hash,
        "metrics": str(args.metrics.resolve()),
        "metrics_sha256": file_sha256(args.metrics),
        "metrics_run_tag": (metrics.get("manifest") or {}).get("run_tag"),
        "metric_eligibility": metric_verdict,
    }
    write_json(args.output, payload)
    return 0 if not problems else 3


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    build = sub.add_parser("capture-build")
    build.add_argument("--source", required=True, type=Path)
    build.add_argument("--binary", required=True, type=Path)
    build.add_argument("--output", required=True, type=Path)
    start = sub.add_parser("start-evaluation")
    start.add_argument("--source", required=True, type=Path)
    start.add_argument("--binary", required=True, type=Path)
    start.add_argument("--build-provenance", required=True, type=Path)
    start.add_argument("--protected-file", action="append", default=[], type=Path)
    start.add_argument("--output", required=True, type=Path)
    finish = sub.add_parser("finish-evaluation")
    finish.add_argument("--source", required=True, type=Path)
    finish.add_argument("--binary", required=True, type=Path)
    finish.add_argument("--start-provenance", required=True, type=Path)
    finish.add_argument("--metrics", required=True, type=Path)
    finish.add_argument("--summary", required=True, type=Path)
    finish.add_argument("--output", required=True, type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.command == "capture-build":
        return capture_build(args)
    if args.command == "start-evaluation":
        return start_evaluation(args)
    return finish_evaluation(args)


if __name__ == "__main__":
    raise SystemExit(main())
