#!/usr/bin/env python3
"""Build Table 4 from fresh default, BO, and fixed-source replay outputs."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path


def load_json(path: Path) -> dict:
    if not path.is_file():
        raise ValueError(f"missing fresh result: {path}")
    return json.loads(path.read_text(encoding="utf-8"))


def hpwl_runtime(metrics: dict) -> tuple[float, float]:
    if metrics.get("status") != "ok":
        raise ValueError(f"fresh metrics status is not ok: {metrics.get('status')!r}")
    violations = (metrics.get("legality") or {}).get("placement_violations")
    if str(violations) not in ("0", "0.0"):
        raise ValueError(f"fresh metrics are not explicitly legal: {violations!r}")
    stages = metrics.get("hpwl_stages") or {}
    missing_stages = [
        field
        for field in ("global_micron", "legalized_micron", "after_improve_micron", "final_micron")
        if stages.get(field) is None
    ]
    if missing_stages:
        raise ValueError("fresh metrics lack complete HPWL trajectory: " + ", ".join(missing_stages))
    hpwl = metrics.get("hpwl") or metrics.get("hpwl_openroad_log") or {}
    final = hpwl.get("after_micron")
    if final is None:
        final = (metrics.get("hpwl_stages") or {}).get("final_micron")
    runtime = metrics.get("runtime_seconds")
    if final is None or runtime is None:
        raise ValueError("metrics record lacks final HPWL or runtime")
    return float(final), float(runtime)


def replay_row(path: Path) -> dict[str, str]:
    if not path.is_file():
        raise ValueError(f"missing fresh selected-source replay: {path}")
    with path.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    if len(rows) != 1 or rows[0].get("status") != "PASS":
        raise ValueError(f"selected-source replay is not one PASS row: {path}")
    row = rows[0]
    for field in (
        "hpwl_global_micron",
        "hpwl_legalized_micron",
        "hpwl_after_improve_micron",
        "hpwl_after_micron",
        "runtime_seconds",
    ):
        if row.get(field, "") == "":
            raise ValueError(f"selected-source replay lacks {field}: {path}")
    metrics_path = Path(row.get("candidate_metrics", ""))
    metrics = load_json(metrics_path)
    if metrics.get("status") != "ok":
        raise ValueError(f"selected-source metrics status is not ok: {metrics_path}")
    violations = (metrics.get("legality") or {}).get("placement_violations")
    if str(violations) not in ("0", "0.0"):
        raise ValueError(f"selected-source replay is not explicitly legal: {metrics_path}")
    return row


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--orfs-root", required=True, type=Path)
    parser.add_argument("--state-root", required=True, type=Path)
    parser.add_argument("--flow-variant", default="paper9_place")
    parser.add_argument("--selected-manifest", required=True, type=Path)
    parser.add_argument("--expected", required=True, type=Path)
    parser.add_argument("--delta-tolerance-pp", type=float, default=0.06)
    parser.add_argument("--runtime-ratio-tolerance", type=float, default=0.20)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    selected = load_json(args.selected_manifest)
    expected = load_json(args.expected)["cases"]
    rows = []
    for spec in selected["programs"]:
        case = spec["case"]
        default_path = (
            args.orfs_root / "flow" / "reports" / spec["platform"] / spec["design"]
            / args.flow_variant / "dpl_evolve_baseline"
            / f"bo9_openroad_dpl_flow_{case}" / "metrics.json"
        )
        default_hpwl, default_runtime = hpwl_runtime(load_json(default_path))
        bo_path = (
            args.state_root / "bo_runs"
            / f"openroad_dpl_hpwl_only_9case_bo_{args.flow_variant}_{case}" / "best.json"
        )
        bo = load_json(bo_path)
        if bo.get("status") != "ok" or int(bo.get("legalize_exit_status", -1)) != 0:
            raise ValueError(f"BO best record is not a clean legal trial: {bo_path}")
        bo_metrics_path = Path(bo.get("metrics_path", ""))
        bo_hpwl, bo_runtime = hpwl_runtime(load_json(bo_metrics_path))

        track_values = {}
        for track in ("hpwl", "ghr"):
            run_id = f"paper_table4_{track}_{case}"
            path = (
                args.state_root / "paper_reproduction" / "table4" / run_id
                / run_id / "results.tsv"
            )
            replay = replay_row(path)
            track_values[track] = (
                float(replay["hpwl_after_micron"]),
                float(replay["runtime_seconds"]),
            )

        def delta(value: float) -> float:
            return (value / default_hpwl - 1.0) * 100.0

        fresh = {
                "case": case,
                "default_H_f": default_hpwl,
                "default_runtime_s": default_runtime,
                "bo_delta_percent": delta(bo_hpwl),
                "bo_runtime_ratio": bo_runtime / default_runtime,
                "reviewdse_hpwl_delta_percent": delta(track_values["hpwl"][0]),
                "reviewdse_hpwl_runtime_ratio": track_values["hpwl"][1] / default_runtime,
                "reviewdse_ghr_delta_percent": delta(track_values["ghr"][0]),
                "reviewdse_ghr_runtime_ratio": track_values["ghr"][1] / default_runtime,
                "default_metrics": str(default_path),
                "bo_best": str(bo_path),
                "bo_metrics": str(bo_metrics_path),
            }
        target = expected[case]
        fresh["expected_bo_delta_percent"] = target["bo_delta"]
        fresh["expected_reviewdse_hpwl_delta_percent"] = target["hpwl_delta"]
        fresh["expected_reviewdse_ghr_delta_percent"] = target["ghr_delta"]
        fresh["expected_bo_runtime_ratio"] = target["bo_runtime"]
        fresh["expected_reviewdse_hpwl_runtime_ratio"] = target["hpwl_runtime"]
        fresh["expected_reviewdse_ghr_runtime_ratio"] = target["ghr_runtime"]
        delta_fields = (
            ("bo_delta_percent", "bo_delta"),
            ("reviewdse_hpwl_delta_percent", "hpwl_delta"),
            ("reviewdse_ghr_delta_percent", "ghr_delta"),
        )
        runtime_fields = (
            ("bo_runtime_ratio", "bo_runtime"),
            ("reviewdse_hpwl_runtime_ratio", "hpwl_runtime"),
            ("reviewdse_ghr_runtime_ratio", "ghr_runtime"),
        )
        delta_match = all(
            abs(float(fresh[field]) - float(target[target_field])) <= args.delta_tolerance_pp
            for field, target_field in delta_fields
        )
        runtime_match = all(
            abs(float(fresh[field]) - float(target[target_field])) <= args.runtime_ratio_tolerance
            for field, target_field in runtime_fields
        )
        fresh["scientific_verdict"] = (
            "match"
            if delta_match and runtime_match
            else "mismatch"
        )
        rows.append(fresh)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]), delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
        mean = {"case": "Mean"}
        for field in (
            "bo_delta_percent", "bo_runtime_ratio",
            "reviewdse_hpwl_delta_percent", "reviewdse_hpwl_runtime_ratio",
            "reviewdse_ghr_delta_percent", "reviewdse_ghr_runtime_ratio",
        ):
            mean[field] = sum(float(row[field]) for row in rows) / len(rows)
        writer.writerow(mean)
    mismatches = [row["case"] for row in rows if row["scientific_verdict"] != "match"]
    if mismatches:
        raise ValueError(
            "fresh Table 4 results exceed scientific tolerances "
            f"(delta={args.delta_tolerance_pp:.3f} percentage point, "
            f"runtime ratio={args.runtime_ratio_tolerance:.3f}): "
            + ", ".join(mismatches)
        )
    print(f"[PASS] summarized nine fresh Table 4 cases: {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, json.JSONDecodeError, csv.Error) as exc:
        raise SystemExit(f"[ERROR] {exc}") from None
