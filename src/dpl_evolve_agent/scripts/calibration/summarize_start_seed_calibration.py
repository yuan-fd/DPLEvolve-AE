#!/usr/bin/env python3
"""Summarize target start-kind probe candidate matrices.

The probe runner evaluates active prepared `dpl_evolve` seed sources:
`framework`, `diamond`, and `default_negotiation` on one target case before a
Teacher round.  This is target-local initialization evidence, not paper-level
Level 1 calibration.  This helper turns the generated candidate-matrix results
into a compact table that Teacher can use as initial donor evidence.
"""
from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
from typing import Any


def as_float(value: Any) -> float | None:
    if value in (None, ""):
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def fmt(value: float | None, digits: int = 1, suffix: str = "") -> str:
    if value is None:
        return ""
    return f"{value:.{digits}f}{suffix}"


def load_metrics(path: Path) -> dict[str, float | str | None]:
    if not path.is_file():
        return {
            "legality": "missing",
            "hpwlg": None,
            "hpwllg": None,
            "hpwlip": None,
            "hpwlf": None,
            "runtime": None,
            "avg_disp": None,
            "max_disp": None,
        }
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return {
            "legality": "bad_json",
            "hpwlg": None,
            "hpwllg": None,
            "hpwlip": None,
            "hpwlf": None,
            "runtime": None,
            "avg_disp": None,
            "max_disp": None,
        }
    stages = data.get("hpwl_stages") if isinstance(data, dict) else {}
    if not isinstance(stages, dict):
        stages = {}
    hpwl = data.get("hpwl")
    if not isinstance(hpwl, dict) or hpwl.get("source") == "cell_bbox_proxy":
        hpwl = data.get("hpwl_openroad_log")
    if not isinstance(hpwl, dict):
        hpwl = {}
    disp = data.get("displacement")
    if not isinstance(disp, dict):
        disp = {}
    legality = data.get("legality")
    if not isinstance(legality, dict):
        legality = {}
    violations = str(legality.get("placement_violations", "")).strip()
    return {
        "legality": "clean" if not violations else violations,
        "hpwlg": as_float(stages.get("global_micron")) or as_float(hpwl.get("before_micron")),
        "hpwllg": as_float(stages.get("legalized_micron")),
        "hpwlip": as_float(stages.get("after_improve_micron")),
        "hpwlf": as_float(stages.get("final_micron")) or as_float(hpwl.get("after_micron")),
        "runtime": as_float(data.get("runtime_seconds")),
        "avg_disp": as_float(disp.get("average_displacement_micron")),
        "max_disp": as_float(disp.get("max_displacement_micron")),
    }


def baseline_metrics_path(candidate_metrics: Path, baseline_tag: str) -> Path:
    baseline_run_tag = f"{baseline_tag}_openroad_dpl_flow"
    return candidate_metrics.parent.parent / baseline_run_tag / "metrics.json"


def round_baseline_metrics_path(candidate_metrics: Path, round_id: str | None) -> Path | None:
    if not round_id:
        return None
    baseline_run_tag = f"{round_id}_baseline_probe_openroad_dpl_flow"
    return candidate_metrics.parent.parent / baseline_run_tag / "metrics.json"


def calibration_baseline_metrics_path(
    candidate_metrics: Path,
    round_id: str | None,
    case_id: str,
) -> Path | None:
    if not round_id or not case_id:
        return None
    baseline_run_tag = (
        f"{round_id}_start_seed_baselines_{case_id}_baseline_probe_openroad_dpl_flow"
    )
    return candidate_metrics.parent.parent / baseline_run_tag / "metrics.json"


def load_reference_metrics(
    candidate_metrics: Path,
    baseline_tag: str,
    round_id: str | None,
    case_id: str,
) -> tuple[dict[str, float | str | None], str]:
    matrix_baseline = baseline_metrics_path(candidate_metrics, baseline_tag)
    if matrix_baseline.is_file():
        return load_metrics(matrix_baseline), str(matrix_baseline)
    round_baseline = round_baseline_metrics_path(candidate_metrics, round_id)
    if round_baseline is not None and round_baseline.is_file():
        return load_metrics(round_baseline), str(round_baseline)
    calibration_baseline = calibration_baseline_metrics_path(
        candidate_metrics,
        round_id,
        case_id,
    )
    if calibration_baseline is not None and calibration_baseline.is_file():
        return load_metrics(calibration_baseline), str(calibration_baseline)
    return load_metrics(matrix_baseline), str(matrix_baseline)


def read_matrix_manifest(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8", errors="replace") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def read_results(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8", errors="replace") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def collect_rows(manifest: list[dict[str, str]], round_id: str | None) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for entry in manifest:
        start_kind = entry.get("start_kind", "")
        matrix_id = entry.get("matrix_id", "")
        results_path = Path(entry.get("results_tsv", ""))
        matrix_status = entry.get("status", "")
        for result in read_results(results_path):
            candidate_metrics = Path(result.get("candidate_metrics", ""))
            candidate = load_metrics(candidate_metrics)
            baseline, reference_metrics = load_reference_metrics(
                candidate_metrics,
                result.get("baseline_tag", ""),
                round_id,
                result.get("case", ""),
            )
            hpwlf = as_float(candidate.get("hpwlf"))
            base_hpwlf = as_float(baseline.get("hpwlf"))
            runtime = as_float(candidate.get("runtime"))
            base_runtime = as_float(baseline.get("runtime"))
            delta_um = None
            delta_pct = None
            runtime_ratio = None
            if hpwlf is not None and base_hpwlf not in (None, 0.0):
                delta_um = hpwlf - float(base_hpwlf)
                delta_pct = delta_um / float(base_hpwlf) * 100.0
            if runtime is not None and base_runtime not in (None, 0.0):
                runtime_ratio = runtime / float(base_runtime)
            rows.append(
                {
                    "start_kind": start_kind,
                    "case": result.get("case", ""),
                    "flow_variant": result.get("flow_variant", ""),
                    "status": result.get("status", matrix_status),
                    "legality": candidate.get("legality"),
                    "hpwlg": candidate.get("hpwlg"),
                    "hpwllg": candidate.get("hpwllg"),
                    "hpwlip": candidate.get("hpwlip"),
                    "hpwlf": hpwlf,
                    "delta_vs_diamond_um": delta_um,
                    "delta_vs_diamond_pct": delta_pct,
                    "runtime": runtime,
                    "runtime_ratio_vs_diamond": runtime_ratio,
                    "avg_disp": candidate.get("avg_disp"),
                    "max_disp": candidate.get("max_disp"),
                    "matrix_id": matrix_id,
                    "candidate_metrics": str(candidate_metrics),
                    "reference_metrics": reference_metrics,
                }
            )
    return rows


def write_tsv(path: Path, rows: list[dict[str, Any]]) -> None:
    fields = [
        "start_kind",
        "case",
        "flow_variant",
        "status",
        "legality",
        "hpwlg",
        "hpwllg",
        "hpwlip",
        "hpwlf",
        "delta_vs_diamond_um",
        "delta_vs_diamond_pct",
        "runtime",
        "runtime_ratio_vs_diamond",
        "avg_disp",
        "max_disp",
        "matrix_id",
        "candidate_metrics",
        "reference_metrics",
    ]
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t")
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def markdown_table(rows: list[dict[str, Any]], detail_paths: bool) -> str:
    lines = [
        "| start_kind | case | status | legality | HPWLg | HPWLlg | HPWLimprove | HPWLfinal | vs Diamond | runtime | RTx | avg_disp | max_disp |",
        "|---|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for row in rows:
        delta = fmt(as_float(row.get("delta_vs_diamond_um")), 1)
        pct = fmt(as_float(row.get("delta_vs_diamond_pct")), 3, "%")
        vs = ""
        if delta or pct:
            vs = f"{delta} ({pct})"
        lines.append(
            "| {start_kind} | {case} | {status} | {legality} | {hpwlg} | {hpwllg} | {hpwlip} | {hpwlf} | {vs} | {runtime} | {rtx} | {avg} | {maxd} |".format(
                start_kind=row.get("start_kind", ""),
                case=row.get("case", ""),
                status=row.get("status", ""),
                legality=row.get("legality", ""),
                hpwlg=fmt(as_float(row.get("hpwlg")), 1),
                hpwllg=fmt(as_float(row.get("hpwllg")), 1),
                hpwlip=fmt(as_float(row.get("hpwlip")), 1),
                hpwlf=fmt(as_float(row.get("hpwlf")), 1),
                vs=vs,
                runtime=fmt(as_float(row.get("runtime")), 3),
                rtx=fmt(as_float(row.get("runtime_ratio_vs_diamond")), 3, "x"),
                avg=fmt(as_float(row.get("avg_disp")), 3),
                maxd=fmt(as_float(row.get("max_disp")), 3),
            )
        )
        if detail_paths:
            lines.append(
                f"| | metrics | | | | | | | `{row.get('candidate_metrics', '')}` | | | | |"
            )
    return "\n".join(lines)


def write_markdown(path: Path, rows: list[dict[str, Any]], *, detail_paths: bool) -> None:
    lines = [
        "# Target Start-Kind Probe",
        "",
        "This table evaluates prepared start-kind seed sources before Teacher/Student",
        "evolution begins on the current target.  It is initial donor evidence only:",
        "promotion still uses",
        "the full Teacher round evaluator, legality, final HPWL, displacement, and",
        "runtime.",
        "",
        markdown_table(rows, detail_paths=detail_paths),
        "",
        "Interpretation:",
        "",
        "- A lower `HPWLfinal` than Diamond is useful initial quality evidence.",
        "- A better `HPWLlg` that does not survive to `HPWLfinal` is a stage donor or handoff clue.",
        "- `default_negotiation` should show negotiation-primary route evidence in its logs before use as that donor.",
    ]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output-md", type=Path, required=True)
    parser.add_argument("--output-tsv", type=Path, required=True)
    parser.add_argument(
        "--round-id",
        default="",
        help=(
            "Teacher round id. Used to fall back to "
            "<round-id>_baseline_probe_openroad_dpl_flow when candidate-matrix "
            "baselines were intentionally skipped."
        ),
    )
    parser.add_argument("--detail-paths", action="store_true")
    args = parser.parse_args()

    rows = collect_rows(read_matrix_manifest(args.manifest), args.round_id or None)
    rows.sort(key=lambda row: (str(row.get("case", "")), str(row.get("start_kind", ""))))
    args.output_md.parent.mkdir(parents=True, exist_ok=True)
    args.output_tsv.parent.mkdir(parents=True, exist_ok=True)
    write_tsv(args.output_tsv, rows)
    write_markdown(args.output_md, rows, detail_paths=args.detail_paths)
    print(args.output_md)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
