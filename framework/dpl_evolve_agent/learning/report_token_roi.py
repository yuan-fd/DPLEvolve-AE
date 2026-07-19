#!/usr/bin/env python3
"""Report token cost and evidence ROI from RunDB."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

AGENT_ROOT = Path(__file__).resolve().parents[1]
if str(AGENT_ROOT) not in sys.path:
    sys.path.insert(0, str(AGENT_ROOT))

from memory.knowledge_db import load_stats
from memory.run_db import load_jsonl, run_db_path
from runtime_paths import resolve_runtime_paths


def token_cost(record: dict[str, Any]) -> int:
    usage = record.get("token_usage", {}) or {}
    return int(usage.get("input_tokens", 0) or 0) + int(usage.get("output_tokens", 0) or 0)


def summarize_records(records: list[dict[str, Any]]) -> dict[str, Any]:
    total_tokens = sum(token_cost(record) for record in records)
    accepted = [record for record in records if record.get("status") == "accepted"]
    improved = [record for record in records if record.get("improved")]
    scored = [record for record in records if record.get("score") is not None]
    best_score = max((float(record["score"]) for record in scored), default=None)
    score_sum = sum(float(record.get("score") or 0.0) for record in scored)
    return {
        "attempts": len(records),
        "accepted": len(accepted),
        "improved": len(improved),
        "total_tokens": total_tokens,
        "best_score": best_score,
        "score_sum": score_sum,
        "accepted_per_1k_tokens": round(len(accepted) / (total_tokens / 1000.0), 6)
        if total_tokens
        else 0.0,
        "score_sum_per_1k_tokens": round(score_sum / (total_tokens / 1000.0), 6)
        if total_tokens
        else 0.0,
    }


def group_by(records: list[dict[str, Any]], key: str) -> dict[str, dict[str, Any]]:
    groups: dict[str, list[dict[str, Any]]] = {}
    for record in records:
        value = str(record.get(key) or "unknown")
        groups.setdefault(value, []).append(record)
    return {name: summarize_records(items) for name, items in sorted(groups.items())}


def build_report(state_root: Path, *, loop_id: str | None = None) -> dict[str, Any]:
    records = load_jsonl(run_db_path(state_root))
    if loop_id is not None:
        records = [record for record in records if record.get("loop_id") == loop_id]
    return {
        "summary": summarize_records(records),
        "by_family": group_by(records, "patch_family"),
        "by_symbol": group_by(records, "touched_symbol"),
        "posterior_stats": load_stats(state_root),
    }


def markdown_report(report: dict[str, Any]) -> str:
    summary = report["summary"]
    lines = [
        "# Token ROI Report",
        "",
        "## Summary",
        f"- attempts: {summary['attempts']}",
        f"- accepted: {summary['accepted']}",
        f"- improved: {summary['improved']}",
        f"- total_tokens: {summary['total_tokens']}",
        f"- best_score: {summary['best_score']}",
        f"- accepted_per_1k_tokens: {summary['accepted_per_1k_tokens']}",
        f"- score_sum_per_1k_tokens: {summary['score_sum_per_1k_tokens']}",
        "",
        "## By Family",
    ]
    for family, stats in report["by_family"].items():
        lines.append(
            f"- {family}: attempts={stats['attempts']}, accepted={stats['accepted']}, "
            f"tokens={stats['total_tokens']}, best_score={stats['best_score']}, "
            f"score_per_1k={stats['score_sum_per_1k_tokens']}"
        )
    lines.extend(["", "## By Symbol"])
    for symbol, stats in report["by_symbol"].items():
        lines.append(
            f"- {symbol}: attempts={stats['attempts']}, accepted={stats['accepted']}, "
            f"tokens={stats['total_tokens']}, best_score={stats['best_score']}, "
            f"score_per_1k={stats['score_sum_per_1k_tokens']}"
        )
    return "\n".join(lines) + "\n"


def write_token_roi_reports(
    state_root: Path,
    out_dir: Path,
    *,
    loop_id: str | None = None,
) -> dict[str, str]:
    report = build_report(state_root, loop_id=loop_id)
    out_dir.mkdir(parents=True, exist_ok=True)
    json_path = out_dir / "token_roi_report.json"
    md_path = out_dir / "token_roi_report.md"
    json_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    md_path.write_text(markdown_report(report), encoding="utf-8")
    return {"json": str(json_path), "markdown": str(md_path)}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--state-root", type=Path)
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--loop-id")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    if args.state_root is None:
        runtime = resolve_runtime_paths(
            anchor_file=__file__,
            agent_root_levels_up=1,
            script_name="report_token_roi.py",
        )
        state_root = runtime.state_root
    else:
        state_root = args.state_root.resolve()
    report = build_report(state_root, loop_id=args.loop_id)
    if args.out_dir:
        write_token_roi_reports(state_root, args.out_dir, loop_id=args.loop_id)
    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print(markdown_report(report), end="")


if __name__ == "__main__":
    main()
