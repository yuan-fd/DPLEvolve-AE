#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path
from typing import Any


FIELDS = [
    "case",
    "core_utilization",
    "flow_variant",
    "baseline_tag",
    "candidate_tag",
    "status",
    "exit_code",
    "hpwl_after_micron",
    "hpwl_delta_percent",
    "hpwl_global_micron",
    "hpwl_legalized_micron",
    "hpwl_after_improve_micron",
    "hpwl_delta_legalization_micron",
    "hpwl_delta_improve_micron",
    "hpwl_delta_final_micron",
    "hpwl_delta_legalization_percent",
    "hpwl_delta_improve_percent",
    "hpwl_delta_final_percent",
    "avg_displacement_micron",
    "max_displacement_micron",
    "runtime_seconds",
    "candidate_metrics",
]


def legalize_log_path(metrics_path: Path) -> Path | None:
    parts = metrics_path.parts
    try:
        flow_index = parts.index("flow")
        reports_index = flow_index + 1
        platform = parts[reports_index + 1]
        design = parts[reports_index + 2]
        flow_variant = parts[reports_index + 3]
        run_tag = parts[reports_index + 5]
    except (ValueError, IndexError):
        return None
    flow_root = Path(*parts[: flow_index + 1]) if metrics_path.is_absolute() else Path(*parts[: flow_index + 1])
    return (
        flow_root
        / "logs"
        / platform
        / design
        / flow_variant
        / "dpl_evolve_baseline"
        / run_tag
        / f"dpl_evolve_{run_tag}_legalize.log"
    )


def parse_runtime(text: str) -> str:
    matches = re.findall(r"Elapsed time:\s+([0-9:.]+)\[h:\]min:sec", text, flags=re.MULTILINE)
    if not matches:
        return ""
    parts = [float(part) for part in matches[-1].split(":")]
    if len(parts) == 3:
        return str(parts[0] * 3600 + parts[1] * 60 + parts[2])
    if len(parts) == 2:
        return str(parts[0] * 60 + parts[1])
    if len(parts) == 1:
        return str(parts[0])
    return ""


def _float_or_none(value: Any) -> float | None:
    if value in (None, ""):
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def stage_summary(
    data: dict[str, Any], hpwl: dict[str, Any], log_path: Path | None
) -> list[str]:
    stages = data.get("hpwl_stages") if isinstance(data, dict) else {}
    if not isinstance(stages, dict):
        stages = {}
    hpwlg = _float_or_none(stages.get("global_micron")) or _float_or_none(
        hpwl.get("before_micron")
    )
    legalized = _float_or_none(stages.get("legalized_micron"))
    improved = _float_or_none(stages.get("after_improve_micron"))
    final = _float_or_none(stages.get("final_micron")) or _float_or_none(
        hpwl.get("after_micron")
    )
    if log_path is not None and log_path.is_file():
        text = log_path.read_text(encoding="utf-8", errors="replace")

        def first(pattern: str) -> float | None:
            match = re.search(pattern, text, flags=re.MULTILINE)
            return _float_or_none(match.group(1)) if match else None

        def last(pattern: str) -> float | None:
            matches = re.findall(pattern, text, flags=re.MULTILINE)
            return _float_or_none(matches[-1]) if matches else None

        hpwlg = hpwlg or first(r"^original HPWL\s+([0-9.+-eE]+)\s+u")
        hpwlg = hpwlg or first(r"^Original HPWL\s+([0-9.+-eE]+)\s+u")
        legalized = legalized or last(r"^legalized HPWL\s+([0-9.+-eE]+)\s+u")
        improved = improved or last(r"^Final HPWL\s+([0-9.+-eE]+)\s+u")
        final = final or last(r"^\[INFO DPL-0022\]\s+HPWL after\s+([0-9.+-eE]+)\s+u")
    d_lg = None if hpwlg is None or legalized is None else legalized - hpwlg
    d_ip = None if legalized is None or improved is None else improved - legalized
    d_final = None if hpwlg is None or final is None else final - hpwlg

    def pct(delta: float | None) -> float | None:
        if delta is None or hpwlg in (None, 0.0):
            return None
        return delta / hpwlg * 100.0

    values = [hpwlg, legalized, improved, d_lg, d_ip, d_final, pct(d_lg), pct(d_ip), pct(d_final)]
    return ["" if value is None else str(value) for value in values]


def metric_summary(path: str) -> tuple[str, str, list[str], str, str, str, bool]:
    if not path:
        return "", "", [""] * 9, "", "", "", False
    metrics_path = Path(path)
    if not metrics_path.is_file():
        log_path = legalize_log_path(metrics_path)
        return log_summary(log_path)
    try:
        data: dict[str, Any] = json.loads(metrics_path.read_text(encoding="utf-8"))
    except Exception:
        log_path = legalize_log_path(metrics_path)
        return log_summary(log_path)
    hpwl = data.get("hpwl")
    if not isinstance(hpwl, dict) or hpwl.get("source") == "cell_bbox_proxy":
        hpwl = data.get("hpwl_openroad_log")
    if not isinstance(hpwl, dict):
        hpwl = {}
    disp = data.get("displacement", {})
    hpwl_after = str(hpwl.get("after_micron", "") or "")
    hpwl_delta_percent = str(hpwl.get("delta_percent", "") or "")
    if not hpwl_delta_percent:
        try:
            before_value = float(hpwl.get("before_micron", ""))
            delta_value = float(hpwl.get("delta_micron", ""))
            if before_value != 0.0:
                hpwl_delta_percent = str(delta_value / before_value * 100.0)
        except (TypeError, ValueError):
            pass
    if not hpwl_after:
        log_hpwl, log_delta_percent, _, _, _, _, _ = log_summary(
            legalize_log_path(metrics_path)
        )
        hpwl_after = log_hpwl
        hpwl_delta_percent = hpwl_delta_percent or log_delta_percent
    stage_values = stage_summary(data, hpwl, legalize_log_path(metrics_path))
    return (
        hpwl_after,
        hpwl_delta_percent,
        stage_values,
        str(disp.get("average_displacement_micron", "") or ""),
        str(disp.get("max_displacement_micron", "") or ""),
        str(data.get("runtime_seconds", "") or ""),
        True,
    )


def log_summary(path: Path | None) -> tuple[str, str, list[str], str, str, str, bool]:
    if path is None or not path.is_file():
        return "", "", [""] * 9, "", "", "", False
    text = path.read_text(encoding="utf-8", errors="replace")

    def last(pattern: str) -> str:
        matches = re.findall(pattern, text, flags=re.MULTILINE)
        return matches[-1] if matches else ""

    hpwl = (
        last(r"^\[INFO DPL-0022\]\s+HPWL after\s+([0-9.+-eE]+)\s+u")
        or last(r"^Final HPWL\s+([0-9.+-eE]+)\s+u")
        or last(r"^legalized HPWL\s+([0-9.+-eE]+)\s+u")
    )
    before = (
        last(r"^original HPWL\s+([0-9.+-eE]+)\s+u")
        or last(r"^Original HPWL\s+([0-9.+-eE]+)\s+u")
    )
    delta_percent = ""
    try:
        before_value = float(before)
        after_value = float(hpwl)
        if before_value != 0.0:
            delta_percent = str((after_value - before_value) / before_value * 100.0)
    except (TypeError, ValueError):
        pass
    avg = last(r"average displacement\s+([0-9.+-eE]+)\s+u")
    maxd = last(r"max displacement\s+([0-9.+-eE]+)\s+u")
    runtime = parse_runtime(text)
    stages = stage_summary({}, {"before_micron": before, "after_micron": hpwl}, path)
    return hpwl, delta_percent, stages, avg, maxd, runtime, any((hpwl, avg, maxd, runtime))


def normalize(path: Path) -> None:
    rows: list[dict[str, str]]
    with path.open("r", encoding="utf-8", newline="") as fh:
        reader = csv.DictReader(fh, delimiter="\t")
        rows = list(reader)

    normalized: list[dict[str, str]] = []
    for row in rows:
        metrics_path = row.get("candidate_metrics", "")
        hpwl, hpwl_delta_percent, stage_values, avg_disp, max_disp, runtime, has_metrics = metric_summary(metrics_path)
        hpwl = row.get("hpwl_after_micron") or hpwl
        hpwl_delta_percent = row.get("hpwl_delta_percent") or hpwl_delta_percent
        avg_disp = row.get("avg_displacement_micron") or avg_disp
        max_disp = row.get("max_displacement_micron") or max_disp
        runtime = row.get("runtime_seconds") or runtime
        status = row.get("status") or ("PASS" if has_metrics else "FAIL_MISSING_METRICS")
        exit_code = row.get("exit_code") or ("0" if has_metrics else "1")
        normalized.append(
            {
                "case": row.get("case", ""),
                "core_utilization": row.get("core_utilization", ""),
                "flow_variant": row.get("flow_variant", ""),
                "baseline_tag": row.get("baseline_tag", ""),
                "candidate_tag": row.get("candidate_tag", ""),
                "status": status,
                "exit_code": exit_code,
                "hpwl_after_micron": hpwl,
                "hpwl_delta_percent": hpwl_delta_percent,
                "hpwl_global_micron": row.get("hpwl_global_micron") or stage_values[0],
                "hpwl_legalized_micron": row.get("hpwl_legalized_micron") or stage_values[1],
                "hpwl_after_improve_micron": row.get("hpwl_after_improve_micron") or stage_values[2],
                "hpwl_delta_legalization_micron": row.get("hpwl_delta_legalization_micron") or stage_values[3],
                "hpwl_delta_improve_micron": row.get("hpwl_delta_improve_micron") or stage_values[4],
                "hpwl_delta_final_micron": row.get("hpwl_delta_final_micron") or stage_values[5],
                "hpwl_delta_legalization_percent": row.get("hpwl_delta_legalization_percent") or stage_values[6],
                "hpwl_delta_improve_percent": row.get("hpwl_delta_improve_percent") or stage_values[7],
                "hpwl_delta_final_percent": row.get("hpwl_delta_final_percent") or stage_values[8],
                "avg_displacement_micron": avg_disp,
                "max_displacement_micron": max_disp,
                "runtime_seconds": runtime,
                "candidate_metrics": metrics_path,
            }
        )

    tmp = path.with_suffix(path.suffix + ".tmp")
    with tmp.open("w", encoding="utf-8", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=FIELDS, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(normalized)
    tmp.replace(path)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Upgrade candidate matrix results.tsv to include comparison metrics."
    )
    parser.add_argument("results_tsv", type=Path)
    args = parser.parse_args()
    normalize(args.results_tsv)


if __name__ == "__main__":
    main()
