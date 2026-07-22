#!/usr/bin/env python3
"""Verify that an external paper-data scope is complete and fully checksummed."""

from __future__ import annotations

import argparse
import hashlib
import re
from pathlib import Path, PurePosixPath


LINE = re.compile(r"^([0-9a-fA-F]{64})\s+[ *]?(.+)$")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--scope", required=True, choices=("table5", "table6"))
    args = parser.parse_args()

    root = args.root.resolve()
    scope_root = root / args.scope
    manifest = scope_root / "MANIFEST.sha256"
    if not manifest.is_file():
        raise ValueError(f"missing checksum manifest: {manifest}")

    recorded: dict[str, str] = {}
    for number, raw in enumerate(manifest.read_text(encoding="utf-8").splitlines(), 1):
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        match = LINE.match(raw)
        if match is None:
            raise ValueError(f"{manifest}:{number}: malformed SHA-256 line")
        digest, name = match.groups()
        relative = PurePosixPath(name)
        if relative.is_absolute() or ".." in relative.parts or relative.parts[:1] != (args.scope,):
            raise ValueError(f"{manifest}:{number}: path must stay under {args.scope}/: {name}")
        if name in recorded:
            raise ValueError(f"{manifest}:{number}: duplicate path: {name}")
        recorded[name] = digest.lower()

    symlinks = sorted(
        path.relative_to(root).as_posix()
        for path in scope_root.rglob("*")
        if path.is_symlink()
    )
    if symlinks:
        raise ValueError("paper data must not contain symlinks: " + ", ".join(symlinks))
    actual = {
        path.relative_to(root).as_posix()
        for path in scope_root.rglob("*")
        if path.is_file() and path != manifest
    }
    listed = set(recorded)
    missing = sorted(listed - actual)
    unsigned = sorted(actual - listed)
    if missing:
        raise ValueError("manifest lists missing files: " + ", ".join(missing))
    if unsigned:
        raise ValueError("paper data contains unsigned files: " + ", ".join(unsigned))
    if not listed:
        raise ValueError(f"checksum manifest is empty: {manifest}")

    mismatches = []
    for name, expected in sorted(recorded.items()):
        observed = sha256(root / Path(name))
        if observed != expected:
            mismatches.append(f"{name}: expected {expected}, got {observed}")
    if mismatches:
        raise ValueError("checksum mismatch:\n  " + "\n  ".join(mismatches))
    print(f"[PASS] verified {len(recorded)} fully enumerated {args.scope} paper-data files")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, UnicodeError) as exc:
        raise SystemExit(f"[ERROR] {exc}") from None
