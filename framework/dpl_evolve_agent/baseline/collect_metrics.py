#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import math
import os
import re
from pathlib import Path
from typing import Any

from flow_paths import resolve_flow_path


def load_json(path: str | None) -> dict[str, Any]:
    if not path:
        return {}
    json_path = resolve_flow_path(path)
    if not json_path.exists():
        return {}
    with json_path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def load_snapshot(path: str) -> dict[str, dict[str, str]]:
    rows: dict[str, dict[str, str]] = {}
    snapshot_path = resolve_flow_path(path)
    if not snapshot_path.exists():
        return rows
    with snapshot_path.open("r", encoding="utf-8") as fh:
        reader = csv.DictReader(fh, delimiter="\t")
        for row in reader:
            rows[row["inst"]] = row
    return rows


def displacement_metrics(
    before: dict[str, dict[str, str]], after: dict[str, dict[str, str]]
) -> dict[str, Any]:
    movable = 0
    moved = 0
    total_disp = 0.0
    total_abs_dx = 0.0
    total_abs_dy = 0.0
    max_disp = 0.0
    unmatched_before: list[str] = []
    unmatched_after = sorted(set(after) - set(before))

    for name, before_row in before.items():
        if name not in after:
            unmatched_before.append(name)
            continue
        if before_row["is_fixed"] == "1":
            continue

        after_row = after[name]
        before_x = float(before_row["x"])
        before_y = float(before_row["y"])
        after_x = float(after_row["x"])
        after_y = float(after_row["y"])
        dx = after_x - before_x
        dy = after_y - before_y
        disp = math.hypot(dx, dy)

        movable += 1
        total_disp += disp
        total_abs_dx += abs(dx)
        total_abs_dy += abs(dy)
        max_disp = max(max_disp, disp)
        if dx != 0.0 or dy != 0.0:
            moved += 1

    average_disp = total_disp / movable if movable else 0.0
    average_abs_dx = total_abs_dx / movable if movable else 0.0
    average_abs_dy = total_abs_dy / movable if movable else 0.0

    return {
        "movable_instance_count": movable,
        "moved_instance_count": moved,
        "average_displacement_dbu": average_disp,
        "max_displacement_dbu": max_disp,
        "average_abs_dx_dbu": average_abs_dx,
        "average_abs_dy_dbu": average_abs_dy,
        "unmatched_before_instances": unmatched_before,
        "unmatched_after_instances": unmatched_after,
    }


def add_micron_metrics(metrics: dict[str, Any], dbu_per_micron: float | None) -> None:
    if not dbu_per_micron:
        return
    for key in (
        "average_displacement_dbu",
        "max_displacement_dbu",
        "average_abs_dx_dbu",
        "average_abs_dy_dbu",
    ):
        if key in metrics:
            metrics[key.replace("_dbu", "_micron")] = metrics[key] / dbu_per_micron


def select_metrics(source: dict[str, Any], mapping: dict[str, str]) -> dict[str, Any]:
    selected: dict[str, Any] = {}
    for dst_key, src_key in mapping.items():
        if src_key in source:
            selected[dst_key] = source[src_key]
    return selected


def normalize_metric_keys(source: dict[str, Any]) -> dict[str, Any]:
    return {
        key.replace(":", "__").replace("/", "__"): value for key, value in source.items()
    }


def select_present(source: dict[str, Any], keys: tuple[str, ...]) -> dict[str, Any]:
    return {key: source[key] for key in keys if key in source}


def relative_artifact(path: str | None, report_dir: Path) -> str | None:
    if not path:
        return None
    artifact = resolve_flow_path(path)
    if not artifact.exists():
        return None
    return os.path.relpath(artifact, report_dir)


def optional_artifact(
    path: str | None,
    report_dir: Path,
    *,
    requested: bool | None = None,
) -> dict[str, str]:
    if requested is False or (requested is None and not path):
        return {"status": "not_requested"}
    relative = relative_artifact(path, report_dir)
    if relative is None:
        return {"status": "absent"}
    return {"status": "present", "path": relative}


def _first_float(pattern: str, text: str, flags: int = re.MULTILINE) -> float | None:
    match = re.search(pattern, text, flags=flags)
    if match is None:
        return None
    try:
        return float(match.group(1))
    except ValueError:
        return None


def _last_float(pattern: str, text: str, flags: int = re.MULTILINE) -> float | None:
    matches = re.findall(pattern, text, flags=flags)
    if not matches:
        return None
    try:
        return float(matches[-1])
    except ValueError:
        return None


def parse_legalize_log_hpwl(path: str | None) -> dict[str, Any]:
    """Extract OpenROAD/DPL's pin-based HPWL from the legalize log.

    The older `hpwl_proxy` in this file is a cell-bbox proxy computed from ODB
    instance rectangles.  DPL reports pin-based HPWL in its own log; this is the
    OpenROAD/DPL comparison metric, but it is not routed wirelength.
    """
    if not path:
        return {}
    log_path = resolve_flow_path(path)
    if not log_path.exists():
        return {}
    text = log_path.read_text(encoding="utf-8", errors="replace")

    before = _first_float(r"^original HPWL\s+([0-9.+-eE]+)\s+u", text)
    if before is None:
        before = _first_float(r"^Original HPWL\s+([0-9.+-eE]+)\s+u", text)

    after = _last_float(r"^\[INFO DPL-0022\]\s+HPWL after\s+([0-9.+-eE]+)\s+u", text)
    if after is None:
        after = _last_float(r"^Final HPWL\s+([0-9.+-eE]+)\s+u", text)
    if after is None:
        after = _last_float(r"^legalized HPWL\s+([0-9.+-eE]+)\s+u", text)

    if before is None and after is None:
        return {}
    result: dict[str, Any] = {"source": "openroad_dpl_log", "log": str(log_path)}
    if before is not None:
        result["before_micron"] = before
    if after is not None:
        result["after_micron"] = after
    if before is not None and after is not None:
        result["delta_micron"] = after - before
        if before != 0.0:
            result["delta_ratio"] = (after - before) / before
            result["delta_percent"] = result["delta_ratio"] * 100.0
    return result


def parse_legalize_log_hpwl_stages(path: str | None) -> dict[str, Any]:
    """Extract stage-wise HPWL from the DPL legalize log.

    The stage names match the evolved flow checkpoints used in Teacher/Student
    reports:
    - global: input placement before legalization.
    - legalized: after detailed placement legalization.
    - after_improve: after improve_placement.
    - final: after optimize_mirroring.
    """
    if not path:
        return {}
    log_path = resolve_flow_path(path)
    if not log_path.exists():
        return {}
    text = log_path.read_text(encoding="utf-8", errors="replace")

    hpwl_global = _first_float(r"^original HPWL\s+([0-9.+-eE]+)\s+u", text)
    if hpwl_global is None:
        hpwl_global = _first_float(r"^Original HPWL\s+([0-9.+-eE]+)\s+u", text)
    hpwl_legalized = _last_float(r"^legalized HPWL\s+([0-9.+-eE]+)\s+u", text)
    hpwl_after_improve = _last_float(r"^Final HPWL\s+([0-9.+-eE]+)\s+u", text)
    hpwl_final = _last_float(
        r"^\[INFO DPL-0022\]\s+HPWL after\s+([0-9.+-eE]+)\s+u", text
    )
    if hpwl_final is None:
        hpwl_final = hpwl_after_improve if hpwl_after_improve is not None else hpwl_legalized

    if hpwl_global is None and hpwl_legalized is None and hpwl_after_improve is None and hpwl_final is None:
        return {}

    result: dict[str, Any] = {"source": "openroad_dpl_log", "log": str(log_path)}
    if hpwl_global is not None:
        result["global_micron"] = hpwl_global
    if hpwl_legalized is not None:
        result["legalized_micron"] = hpwl_legalized
    if hpwl_after_improve is not None:
        result["after_improve_micron"] = hpwl_after_improve
    if hpwl_final is not None:
        result["final_micron"] = hpwl_final
    if hpwl_global is not None:
        if hpwl_legalized is not None:
            result["delta_legalization_micron"] = hpwl_legalized - hpwl_global
        if hpwl_after_improve is not None and hpwl_legalized is not None:
            result["delta_improve_micron"] = hpwl_after_improve - hpwl_legalized
        if hpwl_final is not None:
            result["delta_final_micron"] = hpwl_final - hpwl_global
        if hpwl_global != 0.0:
            for delta_key, percent_key in (
                ("delta_legalization_micron", "delta_legalization_percent"),
                ("delta_improve_micron", "delta_improve_percent"),
                ("delta_final_micron", "delta_final_percent"),
            ):
                if delta_key in result:
                    result[percent_key] = result[delta_key] / hpwl_global * 100.0
    return result


def add_delta_ratio(metric: dict[str, Any], *, before_key: str, after_key: str) -> None:
    before = metric.get(before_key)
    after = metric.get(after_key)
    if before is None or after is None:
        return
    try:
        before_value = float(before)
        after_value = float(after)
    except (TypeError, ValueError):
        return
    if before_value == 0.0:
        return
    metric["delta_ratio"] = (after_value - before_value) / before_value
    metric["delta_percent"] = metric["delta_ratio"] * 100.0


def main() -> None:
    parser = argparse.ArgumentParser(description="Combine dpl_evolve baseline metrics.")
    parser.add_argument("--before", required=True, help="Before-placement TSV snapshot.")
    parser.add_argument("--after", required=True, help="After-placement TSV snapshot.")
    parser.add_argument("--run-summary", help="Strict-track summary JSON.")
    parser.add_argument("--post-metrics-summary", help="Post-legalization ODB metrics summary JSON.")
    parser.add_argument("--manifest", help="Run manifest JSON.")
    parser.add_argument("--detailed-placement-report", help="Detailed placement JSON report.")
    parser.add_argument(
        "--legalize-log",
        help="OpenROAD legalize log containing DPL pin-based HPWL reports.",
    )
    parser.add_argument("--orfs-metrics", help="ORFS stage metrics JSON.")
    parser.add_argument(
        "--orfs-metadata",
        help="Placement-only normalized metadata.json derived from 3_5_place_dp.json.",
    )
    parser.add_argument("--runtime-seconds", type=float, help="Wall-clock runtime for the run.")
    parser.add_argument(
        "--run-status",
        default="ok",
        help="Overall strict flow status: ok, flow_timeout, flow_failed, etc.",
    )
    parser.add_argument(
        "--legalize-exit-status",
        type=int,
        default=0,
        help="Exit status from the legalize/improve/mirror OpenROAD flow.",
    )
    parser.add_argument(
        "--legalize-timeout-seconds",
        type=int,
        default=0,
        help="Timeout applied only to the legalize/improve/mirror flow, if any.",
    )
    parser.add_argument("--output", required=True, help="Output JSON path.")
    args = parser.parse_args()

    before = load_snapshot(args.before)
    after = load_snapshot(args.after)
    run_summary = load_json(args.run_summary)
    post_metrics_summary = load_json(args.post_metrics_summary)
    manifest = load_json(args.manifest)
    dp_report = load_json(args.detailed_placement_report)
    orfs_metrics = load_json(args.orfs_metrics)
    orfs_metadata = load_json(args.orfs_metadata)
    merged_stage_metrics = normalize_metric_keys(orfs_metrics)
    merged_stage_metrics.update(orfs_metadata)
    output = resolve_flow_path(args.output)
    report_dir = output.parent

    dbu_per_micron = None
    for source in (run_summary, post_metrics_summary):
        if "dbu_per_micron" in source:
            dbu_per_micron = float(source["dbu_per_micron"])
            break

    displacement = displacement_metrics(before, after)
    add_micron_metrics(displacement, dbu_per_micron)

    hpwl_before = None
    hpwl_after = None
    hpwl_delta = None
    if "hpwl_before_dbu" in run_summary and "hpwl_after_dbu" in run_summary:
        hpwl_before = run_summary["hpwl_before_dbu"]
        hpwl_after = run_summary["hpwl_after_dbu"]
        hpwl_delta = run_summary.get("hpwl_delta_dbu", hpwl_after - hpwl_before)

    payload: dict[str, Any] = {
        "manifest": manifest,
        "status": args.run_status,
        "legalize_exit_status": args.legalize_exit_status,
        "legalize_timeout_seconds": args.legalize_timeout_seconds or None,
        "runtime_seconds": args.runtime_seconds,
        "dbu_per_micron": dbu_per_micron,
        "displacement": displacement,
    }
    if args.run_status != "ok":
        payload["failure"] = {
            "status": args.run_status,
            "legalize_exit_status": args.legalize_exit_status,
            "legalize_timeout_seconds": args.legalize_timeout_seconds or None,
        }

    legalization = select_present(
        run_summary,
        (
            "status",
            "track",
            "legalizer_mode",
            "stage_sequence",
            "place_command",
            "improve_command",
            "optimize_command",
            "padding_command",
            "balance_rows",
            "enable_dpo",
        ),
    )
    if legalization:
        payload["legalization"] = legalization
    elif args.run_status != "ok":
        payload["legalization"] = {
            "status": args.run_status,
            "legalize_exit_status": args.legalize_exit_status,
            "legalizer_mode": manifest.get("engine"),
        }

    metrics_stage = {
        "elapsed_seconds": merged_stage_metrics.get("detailedplace__elapsed_seconds"),
        "cpu_total_seconds": merged_stage_metrics.get("detailedplace__cpu__total"),
        "peak_memory_kb": merged_stage_metrics.get("detailedplace__mem__peak"),
        "warning_count": merged_stage_metrics.get("detailedplace__flow__warnings__count"),
        "error_count": merged_stage_metrics.get("detailedplace__flow__errors__count"),
        "warning_type_count": merged_stage_metrics.get("detailedplace__flow__warnings__type_count"),
    }
    metrics_stage = {key: value for key, value in metrics_stage.items() if value is not None}
    if metrics_stage:
        payload["metrics_stage"] = metrics_stage

    if hpwl_before is not None:
        payload["hpwl_proxy"] = {
            "before_dbu": hpwl_before,
            "after_dbu": hpwl_after,
            "delta_dbu": hpwl_delta,
        }
        if dbu_per_micron:
            payload["hpwl_proxy"]["before_micron"] = hpwl_before / dbu_per_micron
            payload["hpwl_proxy"]["after_micron"] = hpwl_after / dbu_per_micron
            payload["hpwl_proxy"]["delta_micron"] = hpwl_delta / dbu_per_micron
            add_delta_ratio(
                payload["hpwl_proxy"],
                before_key="before_micron",
                after_key="after_micron",
            )
        else:
            add_delta_ratio(
                payload["hpwl_proxy"],
                before_key="before_dbu",
                after_key="after_dbu",
            )

    hpwl_openroad_log = parse_legalize_log_hpwl(args.legalize_log)
    if hpwl_openroad_log:
        if dbu_per_micron:
            for key in ("before_micron", "after_micron", "delta_micron"):
                if key in hpwl_openroad_log:
                    hpwl_openroad_log[key.replace("_micron", "_dbu")] = int(
                        round(float(hpwl_openroad_log[key]) * dbu_per_micron)
                    )
        payload["hpwl_openroad_log"] = hpwl_openroad_log
        payload["hpwl"] = hpwl_openroad_log

    hpwl_stages = parse_legalize_log_hpwl_stages(args.legalize_log)
    if hpwl_stages:
        if dbu_per_micron:
            for key in (
                "global_micron",
                "legalized_micron",
                "after_improve_micron",
                "final_micron",
                "delta_legalization_micron",
                "delta_improve_micron",
                "delta_final_micron",
            ):
                if key in hpwl_stages:
                    hpwl_stages[key.replace("_micron", "_dbu")] = int(
                        round(float(hpwl_stages[key]) * dbu_per_micron)
                    )
        payload["hpwl_stages"] = hpwl_stages

    payload["timing_metrics"] = select_metrics(
        merged_stage_metrics,
        {
            "setup_tns": "detailedplace__timing__setup__tns",
            "hold_tns": "detailedplace__timing__hold__tns",
            "setup_ws": "detailedplace__timing__setup__ws",
            "hold_ws": "detailedplace__timing__hold__ws",
            "fmax": "detailedplace__timing__fmax",
            "setup_violation_count": "detailedplace__timing__drv__setup_violation_count",
            "hold_violation_count": "detailedplace__timing__drv__hold_violation_count",
        },
    )
    payload["power_metrics"] = select_metrics(
        merged_stage_metrics,
        {
            "total": "detailedplace__power__total",
            "internal": "detailedplace__power__internal__total",
            "switching": "detailedplace__power__switching__total",
            "leakage": "detailedplace__power__leakage__total",
        },
    )
    payload["design_metrics"] = select_metrics(
        merged_stage_metrics,
        {
            "instance_count": "detailedplace__design__instance__count",
            "instance_area": "detailedplace__design__instance__area",
            "core_area": "detailedplace__design__core__area",
            "die_area": "detailedplace__design__die__area",
            "utilization": "detailedplace__design__instance__utilization",
            "estimated_wirelength": "detailedplace__route__wirelength__estimated",
        },
    )
    check_report_raw = post_metrics_summary.get("check_report")
    check_report_state = optional_artifact(
        check_report_raw,
        report_dir,
        requested=post_metrics_summary.get("check_report_status") != "not_requested",
    )
    detailed_report_state = optional_artifact(
        args.detailed_placement_report,
        report_dir,
        requested=args.detailed_placement_report is not None,
    )
    payload["legality"] = {
        "placement_violations": post_metrics_summary.get(
            "placement_violations",
            run_summary.get("placement_violations"),
        ),
        "check_report": check_report_state.get("path"),
        "check_report_status": check_report_state["status"],
    }
    payload["optional_artifacts"] = {
        "check_placement_report_json": check_report_state,
        "detailed_placement_report_json": detailed_report_state,
    }

    supporting_files = {
        "before_snapshot_tsv": relative_artifact(args.before, report_dir),
        "after_snapshot_tsv": relative_artifact(args.after, report_dir),
        "legalize_summary_json": relative_artifact(args.run_summary, report_dir),
        "post_metrics_summary_json": relative_artifact(args.post_metrics_summary, report_dir),
        "legalize_log": relative_artifact(args.legalize_log, report_dir),
        "raw_metrics_json": relative_artifact(args.orfs_metrics, report_dir),
        "normalized_metadata_json": relative_artifact(args.orfs_metadata, report_dir),
    }
    supporting_files = {
        key: value for key, value in supporting_files.items() if value is not None
    }
    for name, artifact in payload["optional_artifacts"].items():
        if artifact["status"] == "present":
            supporting_files[name] = artifact["path"]
    if supporting_files:
        payload["supporting_files"] = supporting_files

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
