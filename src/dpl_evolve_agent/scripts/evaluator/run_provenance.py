#!/usr/bin/env python3
"""Fingerprint the protected input and evaluator used by run_baseline.sh."""
from __future__ import annotations

import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
from typing import Any


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def stat_identity(path: Path) -> dict[str, int]:
    info = path.stat()
    return {
        "device": info.st_dev,
        "inode": info.st_ino,
        "size_bytes": info.st_size,
        "mtime_ns": info.st_mtime_ns,
        "ctime_ns": info.st_ctime_ns,
    }


def cached_sha256(path: Path) -> str:
    """Hash large immutable ODBs once per exact filesystem identity."""
    state_root = os.environ.get("DPL_EVOLVE_STATE_ROOT")
    if not state_root:
        return sha256(path)
    cache_dir = Path(state_root) / "provenance"
    cache_dir.mkdir(parents=True, exist_ok=True)
    cache_path = cache_dir / "protected_file_hashes.json"
    lock_path = cache_dir / "protected_file_hashes.lock"
    identity = stat_identity(path)
    key = str(path.resolve())
    with lock_path.open("a+", encoding="utf-8") as lock:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
        try:
            cache = json.loads(cache_path.read_text(encoding="utf-8")) if cache_path.is_file() else {}
        except (OSError, json.JSONDecodeError):
            cache = {}
        cached = cache.get(key) or {}
        if cached.get("identity") == identity and cached.get("sha256"):
            return str(cached["sha256"])
        digest = sha256(path)
        cache[key] = {"identity": identity, "sha256": digest}
        temporary = cache_path.with_suffix(".tmp")
        temporary.write_text(json.dumps(cache, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        temporary.replace(cache_path)
        return digest


def entry(path: Path, role: str) -> dict[str, Any]:
    resolved = path.resolve()
    return {
        "role": role,
        "path": str(resolved),
        "sha256": cached_sha256(resolved),
        "identity": stat_identity(resolved),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--input-snapshot", type=Path)
    parser.add_argument("--openroad-binary", type=Path)
    parser.add_argument("--protected-file", action="append", default=[], type=Path)
    args = parser.parse_args()
    payload = json.loads(args.manifest.read_text(encoding="utf-8"))
    if not args.verify:
        if args.input_snapshot is None:
            parser.error("--input-snapshot is required when capturing")
        files = [entry(args.input_snapshot, "input_odb")]
        if args.openroad_binary is not None:
            files.append(entry(args.openroad_binary, "openroad_binary"))
        files.extend(entry(path, "evaluator") for path in args.protected_file)
        payload["protected_evaluation"] = {
            "schema_version": 1,
            "files": files,
            "unchanged": None,
            "problems": [],
        }
    else:
        contract = payload.get("protected_evaluation") or {}
        problems = []
        for item in contract.get("files") or []:
            path = Path(item.get("path", ""))
            if not path.is_file():
                problems.append(f"missing:{item.get('role')}:{path}")
            elif stat_identity(path) != item.get("identity"):
                if cached_sha256(path) != item.get("sha256"):
                    problems.append(f"changed:{item.get('role')}:{path}")
        contract["unchanged"] = not problems
        contract["problems"] = problems
        payload["protected_evaluation"] = contract
    args.manifest.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0 if not (payload.get("protected_evaluation") or {}).get("problems") else 3


if __name__ == "__main__":
    raise SystemExit(main())
