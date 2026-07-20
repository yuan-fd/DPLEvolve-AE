#!/usr/bin/env python3
"""Fast state-only status for DPL-evolve experiment batches.

This intentionally avoids scanning ORFS report/log trees.  It reads only the
round-local Teacher packets and Student candidate summaries, so it is safe to
run frequently as a heartbeat while large experiments are active.
"""
from __future__ import annotations

import argparse
import csv
import json
import os
import re
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any


SCRIPT_ROOT = Path(__file__).resolve().parent
AGENT_ROOT = Path(os.environ.get("DPL_EVOLVE_AGENT_ROOT", Path(__file__).resolve().parents[2])).resolve()
DEFAULT_STATE_ROOT = Path(os.environ.get("DPL_EVOLVE_STATE_ROOT", AGENT_ROOT / ".dpl_evolve_state")).resolve()

if str(AGENT_ROOT) not in sys.path:
    sys.path.insert(0, str(AGENT_ROOT))

from scripts.evaluator import report_stage_metrics as stage_metrics  # noqa: E402


@dataclass
class RoundSpec:
    case: str
    round_id: str
    status: str = ""


@dataclass
class CandidateRow:
    iter_number: int
    student_number: int
    iter_name: str
    student_id: str
    hpwl_final: float
    runtime_seconds: float | None
    legality: str
    metrics_path: Path
    mtime: float


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Print a fast state-only DPL-evolve experiment summary."
    )
    parser.add_argument(
        "--batch-root",
        type=Path,
        help="Exact .dpl_evolve_state/experiment_batches/<batch> directory.",
    )
    parser.add_argument(
        "--run-prefix",
        help="Select the newest experiment batch whose name starts with this prefix.",
    )
    parser.add_argument("--state-root", type=Path, default=DEFAULT_STATE_ROOT)
    parser.add_argument("--round-id", action="append", help="Additional standalone round id.")
    parser.add_argument(
        "--format",
        choices=("markdown", "tsv", "json"),
        default="markdown",
    )
    return parser.parse_args()


def as_float(value: Any) -> float | None:
    if value in (None, ""):
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def fmt(value: float | None, digits: int = 1, suffix: str = "") -> str:
    return "" if value is None else f"{value:.{digits}f}{suffix}"


def latest_batch(state_root: Path, prefix: str | None) -> Path | None:
    root = state_root / "experiment_batches"
    if not root.is_dir():
        return None
    pattern = f"{prefix}*" if prefix else "*"
    candidates = [path for path in root.glob(pattern) if path.is_dir()]
    if not candidates:
        return None
    return max(candidates, key=lambda path: path.stat().st_mtime)


def load_batch_specs(batch_root: Path | None) -> list[RoundSpec]:
    if batch_root is None:
        return []
    experiments = batch_root / "experiments.tsv"
    specs: dict[str, RoundSpec] = {}
    if experiments.is_file():
        with experiments.open(newline="", encoding="utf-8", errors="replace") as stream:
            for row in csv.DictReader(stream, delimiter="\t"):
                round_id = row.get("round_id", "")
                case = row.get("case", "")
                if round_id:
                    specs[round_id] = RoundSpec(case=case, round_id=round_id)
    status_tsv = batch_root / "status.tsv"
    if status_tsv.is_file():
        with status_tsv.open(newline="", encoding="utf-8", errors="replace") as stream:
            for row in csv.DictReader(stream, delimiter="\t"):
                round_id = row.get("round_id", "")
                if not round_id:
                    continue
                spec = specs.setdefault(
                    round_id,
                    RoundSpec(case=row.get("case", ""), round_id=round_id),
                )
                spec.status = row.get("status", "") or spec.status
                spec.case = row.get("case", "") or spec.case
    return list(specs.values())


def enrich_spec(state_root: Path, spec: RoundSpec) -> RoundSpec:
    if not spec.case:
        spec.case = stage_metrics.round_case_id(state_root, spec.round_id)
    if not spec.status:
        status = stage_metrics.round_status(state_root, spec.round_id)
        if "completed" in status.lower() or "iteration_complete" in status.lower():
            spec.status = "seen"
        elif "round_dir_missing" in status:
            spec.status = "missing"
        else:
            spec.status = "seen"
    return spec


def parse_baseline_packet(state_root: Path, round_id: str) -> stage_metrics.StageRow | None:
    """Read the OpenROAD default-flow baseline frozen into the round packet."""
    round_root = state_root / round_id / "teacher_rounds"
    packets = sorted(round_root.glob("iter_*/packet/baseline_artifacts.md"))
    if not packets:
        return None
    current_line = ""
    values: dict[str, str] = {}
    try:
        lines = packets[-1].read_text(encoding="utf-8", errors="replace").splitlines()
    except Exception:  # Broad except acceptable for reporting scripts
        return None
    for raw_line in lines:
        line = raw_line.strip()
        section = re.fullmatch(r"###\s+([A-Za-z0-9_]+)", line)
        if section:
            current_line = section.group(1)
            continue
        if current_line != "openroad_dpl_flow":
            continue
        match = re.fullmatch(r"-\s+([A-Za-z0-9_]+):\s+`([^`]*)`", line)
        if match:
            values[match.group(1)] = match.group(2)
    hpwl_final = as_float(values.get("HPWL_final"))
    if hpwl_final is None:
        return None
    hpwlg = as_float(values.get("HPWLg"))
    metrics_path = Path(values.get("metrics_json", ""))
    runtime_seconds = None
    if metrics_path.is_file():
        try:
            payload = json.loads(metrics_path.read_text(encoding="utf-8"))
            runtime_seconds = as_float(payload.get("runtime_seconds"))
        except Exception:  # Broad except acceptable for reporting scripts
            runtime_seconds = None
    return stage_metrics.StageRow(
        round_id=round_id,
        result="baseline_probe_openroad_dpl_flow",
        evidence_path=metrics_path,
        evidence_kind="metrics_packet",
        hpwlg=hpwlg,
        hpwl_legalized=None,
        hpwl_after_improve=None,
        hpwl_final=hpwl_final,
        delta_legalization_percent=None,
        delta_improve_percent=None,
        delta_final_percent=None,
        runtime_seconds=runtime_seconds,
        avg_displacement_micron=None,
        max_displacement_micron=None,
        legality="clean",
    )


def load_candidates(state_root: Path, round_id: str) -> list[CandidateRow]:
    round_root = state_root / round_id / "teacher_rounds" / "students"
    rows: list[CandidateRow] = []
    for path in sorted(round_root.glob("student_*/iter_*/artifacts/candidate_metrics_summary.json")):
        try:
            payload = json.loads(path.read_text(encoding="utf-8"))
            canonical = payload.get("canonical") or {}
            hpwl = as_float(canonical.get("final_hpwl_micron"))
            if hpwl is None:
                continue
            legality = str(canonical.get("legality", ""))
            runtime = as_float(canonical.get("runtime_seconds"))
            iter_name = next(part for part in path.parts if re.fullmatch(r"iter_\d+", part))
            student_id = next(part for part in path.parts if re.fullmatch(r"student_\d+", part))
            rows.append(
                CandidateRow(
                    iter_number=int(iter_name.split("_")[1]),
                    student_number=int(student_id.split("_")[1]),
                    iter_name=iter_name,
                    student_id=student_id,
                    hpwl_final=hpwl,
                    runtime_seconds=runtime,
                    legality=legality,
                    metrics_path=path,
                    mtime=path.stat().st_mtime,
                )
            )
        except Exception:  # Broad except acceptable for reporting scripts
            continue
    return rows


def runtime_penalty(runtime_ratio: float) -> float:
    return stage_metrics.runtime_penalty_pp(runtime_ratio)


def best_candidate(
    candidates: list[CandidateRow],
    baseline: stage_metrics.StageRow | None,
) -> CandidateRow | None:
    clean = [row for row in candidates if row.legality == "clean"]
    if not clean:
        return None
    def quality_key(row: CandidateRow) -> tuple[float, float]:
        runtime = row.runtime_seconds if row.runtime_seconds is not None else float("inf")
        return (row.hpwl_final, runtime)
    if baseline is not None and baseline.hpwl_final is not None:
        winners = [row for row in clean if row.hpwl_final < baseline.hpwl_final]
        if winners:
            return min(winners, key=quality_key)
    return min(clean, key=quality_key)


def pct_delta(candidate: CandidateRow | None, baseline: stage_metrics.StageRow | None) -> float | None:
    if (
        candidate is None
        or baseline is None
        or baseline.hpwl_final in (None, 0)
    ):
        return None
    return (candidate.hpwl_final - baseline.hpwl_final) / baseline.hpwl_final * 100.0


def gain_hr(candidate: CandidateRow | None, baseline: stage_metrics.StageRow | None) -> float | None:
    if (
        candidate is None
        or baseline is None
        or baseline.hpwl_final in (None, 0)
    ):
        return None
    hpwl_gain = (baseline.hpwl_final - candidate.hpwl_final) / baseline.hpwl_final * 100.0
    if baseline.runtime_seconds in (None, 0) or candidate.runtime_seconds in (None, 0):
        return hpwl_gain
    ratio = float(candidate.runtime_seconds) / float(baseline.runtime_seconds)
    return hpwl_gain - runtime_penalty(ratio)


def best_age(candidates: list[CandidateRow]) -> str:
    if not candidates:
        return "n/a"
    age_s = max(0, int(time.time() - max(row.mtime for row in candidates)))
    return f"{age_s}s"


def collect(
    *,
    state_root: Path,
    batch_root: Path | None,
    round_ids: list[str],
) -> tuple[Path | None, list[dict[str, Any]]]:
    specs = load_batch_specs(batch_root)
    seen = {spec.round_id for spec in specs}
    for round_id in round_ids:
        if round_id not in seen:
            specs.append(RoundSpec(case="", round_id=round_id, status="explicit"))
            seen.add(round_id)
    specs = [enrich_spec(state_root, spec) for spec in specs]
    records: list[dict[str, Any]] = []
    for spec in sorted(specs, key=lambda item: (item.case, item.round_id)):
        baseline = parse_baseline_packet(state_root, spec.round_id)
        candidates = load_candidates(state_root, spec.round_id)
        best = best_candidate(candidates, baseline)
        records.append(
            {
                "case": spec.case,
                "status": spec.status,
                "round_id": spec.round_id,
                "max_iter": max((row.iter_number for row in candidates), default=0),
                "metrics_count": len(candidates),
                "valid_count": sum(1 for row in candidates if row.legality == "clean"),
                "age": best_age(candidates),
                "baseline_hpwl": baseline.hpwl_final if baseline else None,
                "best_hpwl": best.hpwl_final if best else None,
                "delta_percent": pct_delta(best, baseline),
                "gain_hr": gain_hr(best, baseline),
                "runtime_seconds": best.runtime_seconds if best else None,
                "best_result": f"{best.iter_name}/{best.student_id}" if best else "",
            }
        )
    return batch_root, records


def print_markdown(batch_root: Path | None, records: list[dict[str, Any]]) -> None:
    print("# DPL Evolve Quick Status")
    print()
    print(f"- generated_at: `{time.strftime('%Y-%m-%d %H:%M:%S %Z')}`")
    if batch_root is not None:
        print(f"- batch: `{batch_root}`")
    print()
    print(
        "| case | status | iter | metrics/valid | age | baseline HPWL | best HPWL | HPWL vs baseline | G_HR | runtime_s | best |"
    )
    print("|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|")
    for row in records:
        print(
            "| "
            + " | ".join(
                [
                    row["case"],
                    row["status"],
                    str(row["max_iter"]),
                    f"{row['metrics_count']}/{row['valid_count']}",
                    row["age"],
                    fmt(row["baseline_hpwl"]),
                    fmt(row["best_hpwl"]),
                    fmt(row["delta_percent"], 3, "%"),
                    fmt(row["gain_hr"], 3),
                    fmt(row["runtime_seconds"], 3),
                    row["best_result"],
                ]
            )
            + " |"
        )


def print_tsv(records: list[dict[str, Any]]) -> None:
    columns = [
        "case",
        "status",
        "max_iter",
        "metrics_count",
        "valid_count",
        "age",
        "baseline_hpwl",
        "best_hpwl",
        "delta_percent",
        "gain_hr",
        "runtime_seconds",
        "best_result",
        "round_id",
    ]
    print("\t".join(columns))
    for row in records:
        print(
            "\t".join(
                str(row.get(column, ""))
                if not isinstance(row.get(column), float)
                else f"{row[column]:.6f}"
                for column in columns
            )
        )


def main() -> int:
    args = parse_args()
    batch_root = args.batch_root
    if batch_root is None:
        batch_root = latest_batch(args.state_root, args.run_prefix)
    if batch_root is not None and not batch_root.is_absolute():
        batch_root = (Path.cwd() / batch_root).resolve()
    _, records = collect(
        state_root=args.state_root,
        batch_root=batch_root,
        round_ids=args.round_id or [],
    )
    if args.format == "json":
        print(json.dumps({"batch": str(batch_root) if batch_root else "", "rounds": records}, indent=2))
    elif args.format == "tsv":
        print_tsv(records)
    else:
        print_markdown(batch_root, records)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
