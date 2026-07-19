#!/usr/bin/env python3
"""Compare two dpl_evolve runs from metrics.json or evaluator JSON."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


def hpwl_metrics(metrics: dict[str, Any]) -> dict[str, Any]:
    return metrics.get("hpwl") or metrics.get("hpwl_openroad_log") or metrics["hpwl_proxy"]


def hpwl_delta_percent(metrics: dict[str, Any]) -> float:
    hpwl = hpwl_metrics(metrics)
    if "delta_percent" in hpwl:
        return float(hpwl["delta_percent"])
    before = float(hpwl["before_micron"])
    delta = float(hpwl["delta_micron"])
    if before == 0.0:
        raise ZeroDivisionError("cannot compute HPWL delta percent with zero before HPWL")
    return delta / before * 100.0


METRICS = {
    "hpwl_after_micron": ("lower", lambda m: hpwl_metrics(m)["after_micron"]),
    "hpwl_delta_micron": ("lower", lambda m: hpwl_metrics(m)["delta_micron"]),
    "hpwl_delta_percent": ("lower", hpwl_delta_percent),
    "setup_ws": ("higher", lambda m: m["timing_metrics"]["setup_ws"]),
    "setup_tns": ("higher", lambda m: m["timing_metrics"]["setup_tns"]),
    "hold_ws": ("higher", lambda m: m["timing_metrics"]["hold_ws"]),
    "hold_tns": ("higher", lambda m: m["timing_metrics"]["hold_tns"]),
    "runtime_seconds": ("lower", lambda m: m["runtime_seconds"]),
    "avg_displacement_micron": (
        "lower",
        lambda m: m["displacement"]["average_displacement_micron"],
    ),
    "max_displacement_micron": (
        "lower",
        lambda m: m["displacement"]["max_displacement_micron"],
    ),
}


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text())


def resolve_metrics(input_path: Path) -> tuple[dict[str, Any], dict[str, Any] | None, Path]:
    payload = load_json(input_path)
    if (
        ("hpwl" in payload or "hpwl_openroad_log" in payload or "hpwl_proxy" in payload)
        and "manifest" in payload
    ):
        return payload, None, input_path

    metrics_path = payload.get("metrics_path")
    if metrics_path:
        metrics_file = Path(metrics_path)
        if not metrics_file.is_absolute():
            metrics_file = (input_path.parent / metrics_file).resolve()
        metrics = load_json(metrics_file)
        return metrics, payload, metrics_file

    raise ValueError(f"Unsupported run payload: {input_path}")


def compare_value(direction: str, left: float, right: float, eps: float = 1e-9) -> str:
    if abs(left - right) <= eps:
        return "same"
    if direction == "lower":
        return "right_better" if right < left else "left_better"
    return "right_better" if right > left else "left_better"


def summarize_run(metrics: dict[str, Any], evaluator: dict[str, Any] | None, source: Path) -> dict[str, Any]:
    summary = {
        "source": str(source),
        "engine": metrics["manifest"]["engine"],
        "command_set": metrics["manifest"]["command_set"],
        "track": metrics["legalization"]["track"],
        "legalization_status": metrics["legalization"]["status"],
        "placement_violations": metrics["legality"]["placement_violations"],
    }
    if evaluator is not None:
        summary["evaluator_status"] = evaluator.get("status")
    for name, (_, getter) in METRICS.items():
        summary[name] = getter(metrics)
    return summary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--left", required=True, help="Baseline or reference run JSON.")
    parser.add_argument("--right", required=True, help="Candidate or newer run JSON.")
    parser.add_argument("--left-label", default="left")
    parser.add_argument("--right-label", default="right")
    parser.add_argument("--json-out", help="Optional JSON summary output path.")
    args = parser.parse_args()

    left_input = Path(args.left).resolve()
    right_input = Path(args.right).resolve()

    left_metrics, left_eval, left_metrics_path = resolve_metrics(left_input)
    right_metrics, right_eval, right_metrics_path = resolve_metrics(right_input)

    left_summary = summarize_run(left_metrics, left_eval, left_metrics_path)
    right_summary = summarize_run(right_metrics, right_eval, right_metrics_path)

    rows: list[dict[str, Any]] = []
    right_better = 0
    left_better = 0
    for name, (direction, _) in METRICS.items():
        left_val = left_summary[name]
        right_val = right_summary[name]
        winner = compare_value(direction, left_val, right_val)
        if winner == "right_better":
            right_better += 1
        elif winner == "left_better":
            left_better += 1
        rows.append(
            {
                "metric": name,
                "direction": direction,
                args.left_label: left_val,
                args.right_label: right_val,
                "delta_right_minus_left": right_val - left_val,
                "winner": winner,
            }
        )

    verdict = "mixed"
    if right_better > 0 and left_better == 0:
        verdict = "right_pareto_nonworse"
    elif left_better > 0 and right_better == 0:
        verdict = "left_pareto_nonworse"

    report = {
        "left": left_summary,
        "right": right_summary,
        "comparison": rows,
        "verdict": verdict,
        "right_better_metrics": right_better,
        "left_better_metrics": left_better,
    }

    print(f"Compare {args.left_label} vs {args.right_label}")
    print(f"left metrics:  {left_metrics_path}")
    print(f"right metrics: {right_metrics_path}")
    print(f"verdict: {verdict}")
    print()
    print(
        f"{'metric':28} {'goal':8} {args.left_label:>16} {args.right_label:>16} {'winner':>18}"
    )
    print("-" * 92)
    for row in rows:
        print(
            f"{row['metric']:28} {row['direction']:8} "
            f"{row[args.left_label]:16.6f} {row[args.right_label]:16.6f} {row['winner']:>18}"
        )

    if args.json_out:
        out = Path(args.json_out)
        out.write_text(json.dumps(report, indent=2))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
