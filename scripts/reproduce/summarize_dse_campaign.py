#!/usr/bin/env python3
"""Summarize a fresh paper-scale ReviewDSE campaign from its full population."""
from __future__ import annotations

import argparse
import csv
import json
import math
import sys
from collections import defaultdict
from pathlib import Path
from types import SimpleNamespace
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
AGENT_ROOT = ROOT / "src" / "dpl_evolve_agent"
if str(AGENT_ROOT) not in sys.path:
    sys.path.insert(0, str(AGENT_ROOT))

from scripts.repo.case_registry import get_case
from scripts.teacher_loop.evidence import (
    candidate_artifact_problems,
    summarize_metrics,
)


def load_json(path: Path) -> dict[str, Any]:
    if not path.is_file():
        raise ValueError(f"missing required campaign artifact: {path}")
    return json.loads(path.read_text(encoding="utf-8"))


def read_tsv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        raise ValueError(f"missing campaign index: {path}")
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def runtime_penalty(runtime_ratio: float) -> float:
    if runtime_ratio <= 1.10:
        return 0.0
    return (math.sqrt(runtime_ratio) - math.sqrt(1.10)) / (
        math.sqrt(2.0) - math.sqrt(1.10)
    )


def gain_hr(default_hpwl: float, hpwl: float, runtime_ratio: float) -> float:
    return 100.0 * (default_hpwl - hpwl) / default_hpwl - runtime_penalty(runtime_ratio)


def select_winners(candidates: list[dict[str, Any]]) -> tuple[dict[str, Any], dict[str, Any]]:
    if not candidates:
        raise ValueError("cannot select from an empty eligible population")
    hpwl_best = min(
        candidates,
        key=lambda row: (row["hpwl"], row["runtime_seconds"], row["iteration"], row["student"]),
    )
    ghr_best = max(
        candidates,
        key=lambda row: (row["gain_hr"], -row["hpwl"], -row["runtime_seconds"], -row["iteration"]),
    )
    return hpwl_best, ghr_best


def aggregate_round_usage(round_root: Path) -> dict[str, int]:
    totals = {"operations": 0, "failed_operations": 0}
    sessions: dict[str, dict[str, int]] = {}
    operations = round_root / "checkpoints" / "operations"
    for path in operations.glob("*/codex_usage_summary.json"):
        try:
            payload = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        usage = payload.get("usage") or {}
        totals["operations"] += 1
        totals["failed_operations"] += int(int(payload.get("returncode") or 0) != 0)
        thread_id = str(payload.get("thread_id") or path.parent)
        snapshot = {
            "input": int(usage.get("input_tokens") or 0),
            "cached": int(usage.get("cached_input_tokens") or 0),
            "output": int(usage.get("output_tokens") or 0),
        }
        previous = sessions.get(thread_id)
        if previous is None or snapshot["input"] + snapshot["output"] > (
            previous["input"] + previous["output"]
        ):
            sessions[thread_id] = snapshot
    final = {"input": 0, "cached": 0, "output": 0}
    for snapshot in sessions.values():
        for field in final:
            final[field] += snapshot[field]
    return {
        **totals,
        "sessions": len(sessions),
        "input_tokens": final["input"],
        "cached_input_tokens": final["cached"],
        "output_tokens": final["output"],
        "logged_tokens": final["input"] + final["output"],
        "active_tokens": final["input"] - final["cached"] + final["output"],
    }


def baseline_from_manifest(manifest: dict[str, Any]) -> tuple[float, float, str]:
    baseline = (manifest.get("baseline_metrics") or {}).get("openroad_dpl_flow") or {}
    hpwl = baseline.get("hpwl_after")
    runtime = baseline.get("runtime_seconds")
    path = baseline.get("metrics_path")
    if hpwl in (None, 0) or runtime in (None, 0) or not path:
        raise ValueError(
            f"round {manifest.get('round_id')} lacks its exact OpenROAD-default baseline anchor"
        )
    return float(hpwl), float(runtime), str(path)


def summarize_round(
    *, round_root: Path, orfs_root: Path, expected_iterations: int, expected_children: int
) -> tuple[dict[str, Any], dict[str, Any]]:
    round_dir = round_root / "teacher_rounds"
    manifest = load_json(round_dir / "manifest.json")
    case = str(manifest.get("case") or "")
    round_id = str(manifest.get("round_id") or round_root.name)
    flow_variant = str(manifest.get("flow_variant") or "")
    protocol = {
        "teacher_model": "gpt-5.5",
        "teacher_reasoning_effort": "xhigh",
        "student_model": "gpt-5.4",
        "student_reasoning_effort": "xhigh",
        "student_runtime_multiplier": 2.0,
        "start_kind": "framework",
    }
    protocol_mismatches = [
        f"{field}={manifest.get(field)!r} (expected {expected!r})"
        for field, expected in protocol.items()
        if manifest.get(field) != expected
    ]
    if protocol_mismatches:
        raise ValueError(f"{case}: paper protocol mismatch: " + "; ".join(protocol_mismatches))
    level1_path = manifest.get("level1_evidence_source")
    level1_hash = manifest.get("level1_evidence_sha256")
    if not level1_path or not level1_hash:
        raise ValueError(f"{case}: paper campaign did not fingerprint frozen Level 1 evidence")
    iterations = manifest.get("iterations") or []
    expected_names = {f"iter_{index:02d}" for index in range(1, expected_iterations + 1)}
    actual_names = {str(item.get("iteration")) for item in iterations}
    if actual_names != expected_names:
        raise ValueError(
            f"{case}: expected iterations {sorted(expected_names)}, got {sorted(actual_names)}"
        )
    runtime = SimpleNamespace(
        orfs_root=orfs_root,
        operations_dir=round_root / "checkpoints" / "operations",
    )
    info = get_case(case)
    default_hpwl, default_runtime, default_metrics = baseline_from_manifest(manifest)
    candidates: list[dict[str, Any]] = []
    rejected: list[dict[str, Any]] = []
    for iteration in sorted(iterations, key=lambda item: item["iteration"]):
        children = iteration.get("children") or []
        if len(children) != expected_children:
            raise ValueError(
                f"{case} {iteration.get('iteration')}: expected {expected_children} Students, got {len(children)}"
            )
        iteration_number = int(str(iteration["iteration"]).split("_", 1)[1])
        for child in children:
            run_tag = str(child.get("run_tag") or "")
            metrics_path = (
                orfs_root / "flow" / "reports" / info.platform / info.design
                / flow_variant / "dpl_evolve_baseline" / run_tag / "metrics.json"
            )
            summary = summarize_metrics(metrics_path)
            if summary is None:
                rejected.append({
                    "iteration": iteration_number,
                    "student": child.get("student_id"),
                    "run_tag": run_tag,
                    "problems": ["missing_or_invalid_metrics"],
                })
                continue
            problems = candidate_artifact_problems(
                runtime=runtime,
                round_dir=round_dir,
                round_id=round_id,
                case_id=case,
                candidate=summary,
            )
            if problems:
                rejected.append({
                    "iteration": iteration_number,
                    "student": child.get("student_id"),
                    "run_tag": run_tag,
                    "problems": problems,
                })
                continue
            assert summary.hpwl_after is not None and summary.runtime_seconds is not None
            ratio = float(summary.runtime_seconds) / default_runtime
            row = {
                "iteration": iteration_number,
                "student": str(child.get("student_id") or ""),
                "route_label": str(child.get("route_label") or ""),
                "run_tag": run_tag,
                "hpwl": float(summary.hpwl_after),
                "runtime_seconds": float(summary.runtime_seconds),
                "runtime_ratio": ratio,
                "delta_percent": (float(summary.hpwl_after) / default_hpwl - 1.0) * 100.0,
                "gain_hr": gain_hr(default_hpwl, float(summary.hpwl_after), ratio),
                "metrics": str(metrics_path),
            }
            candidates.append(row)
    planned = expected_iterations * expected_children
    if len(candidates) + len(rejected) != planned:
        raise ValueError(f"{case}: population accounting is not {planned} candidates")
    if not candidates:
        raise ValueError(f"{case}: no candidate passed the protected eligibility gate")
    hpwl_best, ghr_best = select_winners(candidates)
    usage = aggregate_round_usage(round_root)
    output = {
        "case": case,
        "round_id": round_id,
        "flow_variant": flow_variant,
        "planned_candidates": planned,
        "eligible_candidates": len(candidates),
        "ineligible_candidates": len(rejected),
        "default_H_f": default_hpwl,
        "default_runtime_s": default_runtime,
        "default_metrics": default_metrics,
    }
    for prefix, winner in (("hpwl", hpwl_best), ("ghr", ghr_best)):
        for field, value in winner.items():
            output[f"{prefix}_{field}"] = value
    output.update({f"tokens_{key}": value for key, value in usage.items()})
    output["tokens_logged_billions"] = usage["logged_tokens"] / 1_000_000_000.0
    output["tokens_active_billions"] = usage["active_tokens"] / 1_000_000_000.0
    audit = {
        "case": case,
        "round_id": round_id,
        "eligible": candidates,
        "rejected": rejected,
        "selected": {"hpwl": hpwl_best, "ghr": ghr_best},
        "usage": usage,
    }
    return output, audit


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--batch-root", required=True, type=Path)
    parser.add_argument("--orfs-root", required=True, type=Path)
    parser.add_argument("--expected", type=Path)
    parser.add_argument("--expected-iterations", type=int, default=10)
    parser.add_argument("--expected-children", type=int, default=4)
    parser.add_argument("--delta-tolerance-pp", type=float, default=0.50)
    parser.add_argument("--runtime-ratio-tolerance", type=float, default=0.35)
    parser.add_argument("--require-paper-tolerance", action="store_true")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--audit-output", required=True, type=Path)
    args = parser.parse_args()

    experiments = read_tsv(args.batch_root / "experiments.tsv")
    statuses = {row["case"]: row for row in read_tsv(args.batch_root / "status.tsv")}
    if not experiments:
        raise ValueError("campaign contains no experiments")
    rows: list[dict[str, Any]] = []
    audits = []
    for experiment in experiments:
        case = experiment["case"]
        status = statuses.get(case) or {}
        if status.get("status") != "PASS":
            raise ValueError(f"{case}: campaign launcher status is {status.get('status', 'missing')}")
        round_root = args.batch_root.parents[1] / experiment["round_id"]
        row, audit = summarize_round(
            round_root=round_root,
            orfs_root=args.orfs_root,
            expected_iterations=args.expected_iterations,
            expected_children=args.expected_children,
        )
        rows.append(row)
        audits.append(audit)

    expected = load_json(args.expected).get("cases", {}) if args.expected else {}
    if expected and set(expected) != {row["case"] for row in rows}:
        raise ValueError("fresh campaign case set does not exactly match Table 4")
    mismatches = []
    for row in rows:
        target = expected.get(row["case"])
        if not target:
            row["scientific_verdict"] = "not_compared"
            continue
        checks = [
            abs(row["hpwl_delta_percent"] - float(target["hpwl_delta"])) <= args.delta_tolerance_pp,
            abs(row["ghr_delta_percent"] - float(target["ghr_delta"])) <= args.delta_tolerance_pp,
            abs(row["hpwl_runtime_ratio"] - float(target["hpwl_runtime"])) <= args.runtime_ratio_tolerance,
            abs(row["ghr_runtime_ratio"] - float(target["ghr_runtime"])) <= args.runtime_ratio_tolerance,
        ]
        row["expected_hpwl_delta_percent"] = target["hpwl_delta"]
        row["expected_ghr_delta_percent"] = target["ghr_delta"]
        row["expected_hpwl_runtime_ratio"] = target["hpwl_runtime"]
        row["expected_ghr_runtime_ratio"] = target["ghr_runtime"]
        row["expected_hpwl_iteration"] = target["hpwl_iteration"]
        row["expected_ghr_iteration"] = target["ghr_iteration"]
        row["expected_tokens_logged_billions"] = target["tokens_logged"]
        row["expected_tokens_active_billions"] = target["tokens_active"]
        row["scientific_verdict"] = "match" if all(checks) else "mismatch"
        if not all(checks):
            mismatches.append(row["case"])

    args.output.parent.mkdir(parents=True, exist_ok=True)
    fields = list(rows[0])
    mean = {"case": "Mean", "scientific_verdict": "summary"}
    for field in (
        "hpwl_delta_percent", "hpwl_runtime_ratio", "ghr_delta_percent",
        "ghr_runtime_ratio", "tokens_logged_billions", "tokens_active_billions",
    ):
        mean[field] = sum(float(row[field]) for row in rows) / len(rows)
    with args.output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
        writer.writerow(mean)
    args.audit_output.parent.mkdir(parents=True, exist_ok=True)
    args.audit_output.write_text(
        json.dumps({"schema_version": 1, "rounds": audits}, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"[PASS] summarized {len(rows)} complete ReviewDSE populations: {args.output}")
    if mismatches:
        print("[WARN] fresh stochastic search is outside configured paper tolerance: " + ", ".join(mismatches))
        if args.require_paper_tolerance:
            return 4
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, json.JSONDecodeError, csv.Error) as exc:
        raise SystemExit(f"[ERROR] {exc}") from None
