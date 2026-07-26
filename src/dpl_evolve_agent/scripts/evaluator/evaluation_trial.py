#!/usr/bin/env python3
"""Allocate immutable evaluator-attempt directories."""

from __future__ import annotations

import argparse
import datetime as dt
import fcntl
import re
from pathlib import Path


_TRIAL_RE = re.compile(r"^eval_(\d+)_")


def allocate_trial(root: Path, *, timestamp: str | None = None) -> Path:
    """Atomically create and return the next immutable trial directory."""
    root.mkdir(parents=True, exist_ok=True)
    stamp = timestamp or dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    lock_path = root / ".allocation.lock"
    with lock_path.open("a+", encoding="utf-8") as lock_file:
        fcntl.flock(lock_file.fileno(), fcntl.LOCK_EX)
        numbers = []
        for path in root.iterdir():
            match = _TRIAL_RE.match(path.name)
            if match and path.is_dir():
                numbers.append(int(match.group(1)))
        number = max(numbers, default=0) + 1
        while True:
            candidate = root / f"eval_{number:03d}_{stamp}"
            try:
                candidate.mkdir()
            except FileExistsError:
                number += 1
                continue
            return candidate


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, type=Path)
    args = parser.parse_args()
    print(allocate_trial(args.root).name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
