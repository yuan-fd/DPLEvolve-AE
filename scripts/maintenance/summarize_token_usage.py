#!/usr/bin/env python3
"""Summarize Codex cumulative usage snapshots by DPLEvolve case."""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
from collections import defaultdict
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("backup_root", type=Path)
    parser.add_argument(
        "--campaign-prefix",
        default="evolve_9case_20260517_t55x_s54x_4x15_rerun1_",
    )
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def case_from_campaign(campaign: str) -> str | None:
    marker = "_place_batch_20260421_220319_"
    if marker not in campaign:
        return None
    suffix = campaign.split(marker, 1)[1]
    match = re.match(r"(.+?)_\d+x\d+_", suffix)
    return match.group(1) if match else None


def aggregate(root: Path, prefix: str) -> list[dict[str, int | str]]:
    totals: dict[str, dict[str, int]] = defaultdict(
        lambda: {"operations": 0, "failed": 0, "input": 0, "cached": 0, "output": 0}
    )
    sessions: dict[str, dict[tuple[str, str], dict[str, int]]] = defaultdict(dict)
    for campaign_dir in sorted(root.glob(f"{prefix}*")):
        case = case_from_campaign(campaign_dir.name)
        if case is None:
            continue
        for path in campaign_dir.rglob("codex_usage_summary.json"):
            try:
                payload = json.loads(path.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError):
                continue
            usage = payload.get("usage") or {}
            row = totals[case]
            row["operations"] += 1
            row["failed"] += int(int(payload.get("returncode") or 0) != 0)
            row["input"] += int(usage.get("input_tokens") or 0)
            row["cached"] += int(usage.get("cached_input_tokens") or 0)
            row["output"] += int(usage.get("output_tokens") or 0)

            # Each operation file records the cumulative total for a persistent
            # Codex thread, not an operation-local delta. Keep the largest
            # snapshot per thread to avoid counting the same tokens once per
            # review iteration.
            thread_id = str(payload.get("thread_id") or path.parent)
            key = (campaign_dir.name, thread_id)
            snapshot = {
                "input": int(usage.get("input_tokens") or 0),
                "cached": int(usage.get("cached_input_tokens") or 0),
                "output": int(usage.get("output_tokens") or 0),
            }
            previous = sessions[case].get(key)
            if previous is None or snapshot["input"] + snapshot["output"] > (
                previous["input"] + previous["output"]
            ):
                sessions[case][key] = snapshot

    rows = []
    for case, values in sorted(totals.items()):
        snapshot_uncached = values["input"] - values["cached"]
        final = {"input": 0, "cached": 0, "output": 0}
        for usage in sessions[case].values():
            for field in final:
                final[field] += usage[field]
        session_uncached = final["input"] - final["cached"]
        rows.append(
            {
                "case": case,
                "operations": values["operations"],
                "failed_operations": values["failed"],
                "sessions": len(sessions[case]),
                "snapshot_input_tokens": values["input"],
                "snapshot_cached_input_tokens": values["cached"],
                "snapshot_output_tokens": values["output"],
                "snapshot_active_tokens": snapshot_uncached + values["output"],
                "snapshot_logged_tokens": values["input"] + values["output"],
                "session_input_tokens": final["input"],
                "session_cached_input_tokens": final["cached"],
                "session_output_tokens": final["output"],
                "session_active_tokens": session_uncached + final["output"],
                "session_logged_tokens": final["input"] + final["output"],
            }
        )
    return rows


def main() -> int:
    args = parse_args()
    if not args.backup_root.is_dir():
        print(f"[ERROR] Backup root not found: {args.backup_root}", file=sys.stderr)
        return 1
    rows = aggregate(args.backup_root, args.campaign_prefix)
    if not rows:
        print("[ERROR] No matching usage summaries found.", file=sys.stderr)
        return 1

    fields = list(rows[0])
    stream = args.output.open("w", encoding="utf-8", newline="") if args.output else sys.stdout
    try:
        writer = csv.DictWriter(stream, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    finally:
        if args.output:
            stream.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
