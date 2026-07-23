#!/usr/bin/env python3
"""Verify a frozen Level 1 packet before a paper-profile Level 2 launch."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


EXPECTED_CASES = {
    "jpeg_util90_nangate45",
    "aes_nangate45",
    "swerv_wrapper_nangate45",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--packet", required=True, type=Path)
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()
    manifest_path = args.manifest or args.packet.with_suffix(".json")
    if not args.packet.is_file() or not manifest_path.is_file():
        raise ValueError("Level 1 packet or companion JSON manifest is missing")
    data = json.loads(manifest_path.read_text(encoding="utf-8"))
    if data.get("status") != "complete_fresh_public_reconstruction":
        raise ValueError("Level 1 manifest does not describe a completed fresh reconstruction")
    if data.get("packet", {}).get("sha256") != sha256(args.packet):
        raise ValueError("Level 1 packet SHA-256 does not match its manifest")
    profile = data.get("public_reconstruction_profile") or {}
    if profile.get("iterations") != 1 or float(profile.get("runtime_gate", -1)) != 2.0:
        raise ValueError("Level 1 iteration/runtime contract mismatch")
    if (profile.get("teacher") or {}) != {"model": "gpt-5.5", "reasoning_effort": "xhigh"}:
        raise ValueError("Level 1 Teacher profile mismatch")
    if (profile.get("student") or {}) != {"model": "gpt-5.4", "reasoning_effort": "xhigh"}:
        raise ValueError("Level 1 Student profile mismatch")
    rounds = data.get("rounds") or []
    if len(rounds) != 3 or {row.get("case") for row in rounds} != EXPECTED_CASES:
        raise ValueError("Level 1 manifest does not cover the exact three calibration cases")
    expected_children = int(profile.get("students_per_case", 0))
    if expected_children < 1 or any(row.get("students_launched") != expected_children for row in rounds):
        raise ValueError("Level 1 launched-Student counts are incomplete")
    if len(data.get("source_starts") or []) != 3:
        raise ValueError("Level 1 manifest does not pin all three source starts")
    print(
        f"[PASS] verified fresh Level 1 reconstruction: 3 cases x "
        f"{expected_children} Students; packet={args.packet}"
    )
    print("[INFO] Author-time Level 1 breadth/packet was not retained; exact process replay is not claimed.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as exc:
        raise SystemExit(f"[ERROR] {exc}") from None
