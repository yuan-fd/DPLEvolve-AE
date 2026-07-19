#!/usr/bin/env python3
"""Report canonical stage-wise DPL metrics for one or more teacher rounds.

The headline HPWL values come from the normalized `metrics.json` files written
by the evaluator.  Those files use OpenROAD/DPL log HPWL (`hpwl_stages` and
`hpwl`) rather than the auxiliary bbox proxy.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


AGENT_ROOT = Path(os.environ.get("DPL_EVOLVE_AGENT_ROOT", Path(__file__).resolve().parents[2])).resolve()
DEFAULT_ORFS_ROOT = Path(os.environ.get("ORFS_ROOT", AGENT_ROOT.parent / "OpenROAD-flow-scripts")).resolve()
DEFAULT_STATE_ROOT = Path(os.environ.get("DPL_EVOLVE_STATE_ROOT", AGENT_ROOT / ".dpl_evolve_state")).resolve()
if str(AGENT_ROOT) not in sys.path:
    sys.path.insert(0, str(AGENT_ROOT))

from scripts.repo.case_registry import get_case  # noqa: E402


@dataclass
class StageRow:
    round_id: str
    result: str
    evidence_path: Path
    evidence_kind: str
    hpwlg: float | None
    hpwl_legalized: float | None
    hpwl_after_improve: float | None
    hpwl_final: float | None
    delta_legalization_percent: float | None
    delta_improve_percent: float | None
    delta_final_percent: float | None
    runtime_seconds: float | None
    avg_displacement_micron: float | None
    max_displacement_micron: float | None
    legality: str
    gain_hr: float | None = None
    hpwl_gain_percent: float | None = None
    runtime_penalty_pp: float | None = None
    runtime_ratio: float | None = None


VALUE_REFERENCE_RESULT = "baseline_probe_openroad_dpl_flow"
# Runtime is a value check, not the primary objective.  Runtime improvements
# and small noise within this deadband are not rewarded; only material
# slowdowns are penalized.
RUNTIME_DEADBAND_RATIO = 1.10
RUNTIME_PENALTY_AT_2X = 1.0


CANONICAL_BASELINES = (
    "baseline_probe_openroad_dpl_flow",
    "baseline_probe_openroad_dpl_negotiation",
    "baseline_probe_evolve_default",
)
CANONICAL_LINES = (
    "openroad_dpl_flow",
    "openroad_dpl_negotiation",
    "evolve_default",
)


def is_expected_result_suffix(suffix: str) -> bool:
    if suffix in CANONICAL_BASELINES:
        return True
    return re.match(r"^iter_\d+_student_\d+_", suffix) is not None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Print stage-wise HPWL/runtime/displacement metrics for DPL evolution rounds."
    )
    parser.add_argument("--round-id", action="append", required=True)
    parser.add_argument("--orfs-root", type=Path, default=DEFAULT_ORFS_ROOT)
    parser.add_argument("--state-root", type=Path, default=DEFAULT_STATE_ROOT)
    parser.add_argument(
        "--format",
        choices=("markdown", "tsv", "json"),
        default="markdown",
        help="Output format. Default: markdown.",
    )
    parser.add_argument(
        "--include-path",
        action="store_true",
        help="Include metrics.json path in markdown/tsv output.",
    )
    parser.add_argument(
        "--show-missing",
        action="store_true",
        help="Also report round status when no metrics have landed yet.",
    )
    parser.add_argument(
        "--expected-iterations",
        type=int,
        help="Show missing rows through this many iterations for each round.",
    )
    parser.add_argument(
        "--children",
        type=int,
        help="Show this many student slots when expanding expected rows.",
    )
    parser.add_argument(
        "--no-expected",
        action="store_true",
        help="Only show rows with metrics/log evidence; do not add missing expected rows.",
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
    if value is None:
        return ""
    return f"{value:.{digits}f}{suffix}"


def runtime_penalty_pp(runtime_ratio: float) -> float:
    if runtime_ratio <= RUNTIME_DEADBAND_RATIO:
        return 0.0
    denominator = math.sqrt(2.0) - math.sqrt(RUNTIME_DEADBAND_RATIO)
    if denominator <= 0.0:
        return 0.0
    return RUNTIME_PENALTY_AT_2X * (
        math.sqrt(runtime_ratio) - math.sqrt(RUNTIME_DEADBAND_RATIO)
    ) / denominator


def annotate_hpwl_runtime_gains(rows: list[StageRow]) -> None:
    ref = next(
        (
            row
            for row in rows
            if row.result == VALUE_REFERENCE_RESULT
            and row.legality == "clean"
            and row.hpwl_final not in (None, 0)
            and row.runtime_seconds not in (None, 0)
        ),
        None,
    )
    if ref is None:
        return
    assert ref.hpwl_final is not None and ref.runtime_seconds is not None
    for row in rows:
        if (
            row.legality != "clean"
            or row.hpwl_final in (None, 0)
            or row.runtime_seconds in (None, 0)
        ):
            continue
        assert row.hpwl_final is not None and row.runtime_seconds is not None
        row.runtime_ratio = float(row.runtime_seconds) / float(ref.runtime_seconds)
        hpwl_gain_percent = (
            (float(ref.hpwl_final) - float(row.hpwl_final))
            / float(ref.hpwl_final)
            * 100.0
        )
        runtime_penalty = runtime_penalty_pp(row.runtime_ratio)
        row.hpwl_gain_percent = hpwl_gain_percent
        row.runtime_penalty_pp = runtime_penalty
        row.gain_hr = hpwl_gain_percent - runtime_penalty


def result_sort_key(result: str) -> tuple[int, int, int, str]:
    if result.startswith("baseline_probe_openroad_dpl_flow"):
        return (0, 0, 0, result)
    if result.startswith("baseline_probe_openroad_dpl_negotiation"):
        return (0, 1, 0, result)
    if result.startswith("baseline_probe_evolve"):
        return (0, 2, 0, result)
    match = re.search(r"iter_(\d+)_student_(\d+)", result)
    if match:
        return (1, int(match.group(1)), int(match.group(2)), result)
    return (2, 0, 0, result)


def strip_round_prefix(round_id: str, run_tag: str) -> str:
    prefix = f"{round_id}_"
    if run_tag.startswith(prefix):
        return run_tag[len(prefix) :]
    return run_tag


def run_tag_matches_round(round_id: str, run_tag: str) -> bool:
    prefix = f"{round_id}_"
    if not run_tag.startswith(prefix):
        return False
    return is_expected_result_suffix(run_tag[len(prefix) :])


def discover_metrics(orfs_root: Path, round_id: str) -> list[Path]:
    report_root = orfs_root / "flow" / "reports"
    if not report_root.exists():
        return []
    pattern = f"{round_id}_*/metrics.json"
    return sorted(
        path
        for path in report_root.glob(f"**/dpl_evolve_baseline/{pattern}")
        if run_tag_matches_round(round_id, path.parent.name)
    )


def discover_logs(orfs_root: Path, round_id: str) -> list[Path]:
    log_root = orfs_root / "flow" / "logs"
    if not log_root.exists():
        return []
    pattern = f"{round_id}_*/dpl_evolve_{round_id}_*_legalize.log"
    return sorted(
        path
        for path in log_root.glob(f"**/dpl_evolve_baseline/{pattern}")
        if run_tag_matches_round(round_id, path.parent.name)
    )


def load_row(round_id: str, metrics_path: Path) -> StageRow | None:
    try:
        data = json.loads(metrics_path.read_text(encoding="utf-8"))
    except Exception:
        return None
    stages = data.get("hpwl_stages")
    if not isinstance(stages, dict):
        stages = {}
    hpwl = data.get("hpwl")
    if not isinstance(hpwl, dict):
        hpwl = {}
    disp = data.get("displacement")
    if not isinstance(disp, dict):
        disp = {}
    legality = data.get("legality")
    if not isinstance(legality, dict):
        legality = {}

    hpwlg = as_float(stages.get("global_micron")) or as_float(hpwl.get("before_micron"))
    final = as_float(stages.get("final_micron")) or as_float(hpwl.get("after_micron"))
    violations = str(legality.get("placement_violations", ""))
    legal_status = "clean" if violations.strip() == "" else violations

    return StageRow(
        round_id=round_id,
        result=strip_round_prefix(round_id, metrics_path.parent.name),
        evidence_path=metrics_path,
        evidence_kind="metrics",
        hpwlg=hpwlg,
        hpwl_legalized=as_float(stages.get("legalized_micron")),
        hpwl_after_improve=as_float(stages.get("after_improve_micron")),
        hpwl_final=final,
        delta_legalization_percent=as_float(stages.get("delta_legalization_percent")),
        delta_improve_percent=as_float(stages.get("delta_improve_percent")),
        delta_final_percent=as_float(stages.get("delta_final_percent"))
        or as_float(hpwl.get("delta_percent")),
        runtime_seconds=as_float(data.get("runtime_seconds")),
        avg_displacement_micron=as_float(disp.get("average_displacement_micron")),
        max_displacement_micron=as_float(disp.get("max_displacement_micron")),
        legality=legal_status,
    )


def last_float(pattern: str, text: str) -> float | None:
    values = re.findall(pattern, text, flags=re.MULTILINE)
    if not values:
        return None
    return as_float(values[-1])


def first_float(pattern: str, text: str) -> float | None:
    match = re.search(pattern, text, flags=re.MULTILINE)
    return as_float(match.group(1)) if match else None


def load_log_row(round_id: str, log_path: Path) -> StageRow | None:
    try:
        text = log_path.read_text(encoding="utf-8", errors="replace")
    except Exception:
        return None
    hpwlg = first_float(r"^original HPWL\s+([0-9.+-eE]+)\s+u", text)
    if hpwlg is None:
        hpwlg = first_float(r"^Original HPWL\s+([0-9.+-eE]+)\s+u", text)
    legalized = last_float(r"^legalized HPWL\s+([0-9.+-eE]+)\s+u", text)
    improved = last_float(r"^Final HPWL\s+([0-9.+-eE]+)\s+u", text)
    final = last_float(r"^\[INFO DPL-0022\]\s+HPWL after\s+([0-9.+-eE]+)\s+u", text)
    if final is None:
        final = improved if improved is not None else legalized
    if hpwlg is None and legalized is None and improved is None and final is None:
        return None

    def delta_pct(value: float | None, base: float | None) -> float | None:
        if value is None or base in (None, 0.0):
            return None
        return (value - base) / base * 100.0

    elapsed = last_float(r"Elapsed time:\s+([0-9.]+)\[h:\]min:sec", text)
    avg_disp = last_float(r"average displacement\s+([0-9.+-eE]+)\s+u", text)
    max_disp = last_float(r"max displacement\s+([0-9.+-eE]+)\s+u", text)
    run_tag = log_path.parent.name
    legality = "partial_log"
    if "Found 0 overlaps" in text and "Found 0 row alignment" in text:
        legality = "clean_or_partial"
    return StageRow(
        round_id=round_id,
        result=strip_round_prefix(round_id, run_tag),
        evidence_path=log_path,
        evidence_kind="log",
        hpwlg=hpwlg,
        hpwl_legalized=legalized,
        hpwl_after_improve=improved,
        hpwl_final=final,
        delta_legalization_percent=delta_pct(legalized, hpwlg),
        delta_improve_percent=delta_pct(improved, legalized),
        delta_final_percent=delta_pct(final, hpwlg),
        runtime_seconds=elapsed,
        avg_displacement_micron=avg_disp,
        max_displacement_micron=max_disp,
        legality=legality,
    )


def round_status(state_root: Path, round_id: str) -> str:
    round_dir = teacher_round_dir(state_root, round_id)
    if not round_dir.exists():
        return "round_dir_missing"
    round_log = round_dir / "round.log"
    if not round_log.exists():
        return "round_log_missing"
    lines = round_log.read_text(encoding="utf-8", errors="replace").splitlines()
    return lines[-1] if lines else "round_log_empty"


def teacher_round_dir(state_root: Path, round_id: str) -> Path:
    return state_root / round_id / "teacher_rounds"


def round_dir(state_root: Path, round_id: str) -> Path:
    return teacher_round_dir(state_root, round_id)


def round_case_id(state_root: Path, round_id: str) -> str:
    manifest = round_dir(state_root, round_id) / "manifest.json"
    if manifest.is_file():
        try:
            data = json.loads(manifest.read_text(encoding="utf-8"))
            case_id = data.get("case")
            if isinstance(case_id, str) and case_id:
                return case_id
        except Exception:
            pass
    events = round_dir(state_root, round_id) / "events.jsonl"
    if events.is_file():
        for line in events.read_text(encoding="utf-8", errors="replace").splitlines():
            try:
                event = json.loads(line)
            except Exception:
                continue
            case_id = (event.get("fields") or {}).get("case")
            if isinstance(case_id, str) and case_id:
                return case_id
    return ""


def round_event_field(state_root: Path, round_id: str, field: str) -> str:
    events = round_dir(state_root, round_id) / "events.jsonl"
    if not events.is_file():
        return ""
    for line in events.read_text(encoding="utf-8", errors="replace").splitlines():
        try:
            event = json.loads(line)
        except Exception:
            continue
        value = (event.get("fields") or {}).get(field)
        if isinstance(value, str) and value:
            return value
    return ""


def round_flow_variant(state_root: Path, round_id: str) -> str:
    return round_event_field(state_root, round_id, "flow_variant")


def metrics_line(metrics_path: Path) -> str:
    try:
        data = json.loads(metrics_path.read_text(encoding="utf-8"))
    except Exception:
        return ""
    manifest = data.get("manifest")
    if isinstance(manifest, dict):
        line = manifest.get("line")
        if isinstance(line, str):
            return line
    return ""


def discover_reused_baseline_metrics(
    *, orfs_root: Path, state_root: Path, round_id: str
) -> dict[str, Path]:
    """Find canonical baseline rows reused from an older baseline preflight.

    Rounds launched with --reuse-baseline-preflight may only write the current
    round's openroad_dpl_flow probe.  Teacher still compares against the latest
    canonical baseline rows for all three lines, so the report should surface
    those rows instead of printing misleading "missing" placeholders.
    """
    case_id = round_case_id(state_root, round_id)
    flow_variant = round_flow_variant(state_root, round_id)
    if not case_id or not flow_variant:
        return {}
    try:
        case = get_case(case_id)
    except SystemExit:
        return {}
    report_root = (
        orfs_root
        / "flow"
        / "reports"
        / case.platform
        / case.design
        / flow_variant
        / "dpl_evolve_baseline"
    )
    if not report_root.exists():
        return {}
    by_line: dict[str, Path] = {}
    candidates = sorted(
        report_root.glob("*/metrics.json"), key=lambda path: path.stat().st_mtime
    )
    for metrics_path in candidates:
        run_tag = metrics_path.parent.name
        if "_student_" in run_tag or "_iter_" in run_tag:
            continue
        line = metrics_line(metrics_path)
        if line in CANONICAL_LINES:
            by_line[line] = metrics_path
    return by_line


def discover_packet_baseline_metrics(*, state_root: Path, round_id: str) -> dict[str, Path]:
    """Read the baseline metrics paths frozen into this round's packets.

    Reused baseline preflight rows are part of the Teacher context.  Looking up
    the latest metrics under ORFS can drift after later experiments, so prefer
    the metrics_json paths that were written into the round packet.
    """
    line_to_path: dict[str, Path] = {}
    packet_root = round_dir(state_root, round_id)
    for packet in sorted(packet_root.glob("iter_*/packet/baseline_artifacts.md")):
        current_line = ""
        try:
            lines = packet.read_text(encoding="utf-8", errors="replace").splitlines()
        except Exception:
            continue
        for raw_line in lines:
            line = raw_line.strip()
            section = re.fullmatch(r"###\s+([A-Za-z0-9_]+)", line)
            if section:
                current_line = section.group(1)
                continue
            if not current_line:
                continue
            metrics = re.fullmatch(r"-\s+metrics_json:\s+`([^`]+)`", line)
            if metrics:
                metrics_path = Path(metrics.group(1))
                if metrics_path.is_file():
                    line_to_path[current_line] = metrics_path
    return line_to_path


def discovered_iteration_count(state_root: Path, round_id: str) -> int:
    count = 0
    for path in round_dir(state_root, round_id).glob("iter_*"):
        match = re.fullmatch(r"iter_(\d+)", path.name)
        if match and path.is_dir():
            count = max(count, int(match.group(1)))
    return count


def discovered_child_count(state_root: Path, round_id: str) -> int:
    count = 0
    for path in (round_dir(state_root, round_id) / "students").glob("student_*"):
        match = re.fullmatch(r"student_(\d+)", path.name)
        if match and path.is_dir():
            count = max(count, int(match.group(1)))
    return count


def missing_row(round_id: str, result: str) -> StageRow:
    return StageRow(
        round_id=round_id,
        result=result,
        evidence_path=Path(""),
        evidence_kind="missing",
        hpwlg=None,
        hpwl_legalized=None,
        hpwl_after_improve=None,
        hpwl_final=None,
        delta_legalization_percent=None,
        delta_improve_percent=None,
        delta_final_percent=None,
        runtime_seconds=None,
        avg_displacement_micron=None,
        max_displacement_micron=None,
        legality="missing",
    )


def add_expected_rows(
    *,
    rows: list[StageRow],
    state_root: Path,
    round_id: str,
    expected_iterations: int | None,
    children: int | None,
) -> list[StageRow]:
    present = {row.result for row in rows}
    expanded = list(rows)
    for baseline in CANONICAL_BASELINES:
        if baseline not in present:
            expanded.append(missing_row(round_id, baseline))

    iteration_count = expected_iterations or discovered_iteration_count(state_root, round_id)
    child_count = children or discovered_child_count(state_root, round_id)
    case_id = round_case_id(state_root, round_id)
    if iteration_count <= 0 or child_count <= 0 or not case_id:
        return expanded
    for iteration in range(1, iteration_count + 1):
        for child in range(1, child_count + 1):
            result = f"iter_{iteration:02d}_student_{child:02d}_{case_id}"
            if result not in present:
                expanded.append(missing_row(round_id, result))
    return expanded


def print_markdown(rows_by_round: dict[str, list[StageRow]], *, include_path: bool) -> None:
    base_columns = [
        "round",
        "result",
        "G_HR",
        "HPWL_gain%",
        "runtime_penalty_pp",
        "HPWLg",
        "HPWLlg",
        "dLG%",
        "HPWLimprove",
        "dIP%",
        "HPWL final",
        "dFinal%",
        "runtime_s",
        "avg_disp",
        "max_disp",
        "legality",
        "source",
    ]
    columns = base_columns + (["metrics"] if include_path else [])
    print("| " + " | ".join(columns) + " |")
    print("|" + "|".join(["---"] * len(columns)) + "|")
    for round_id, rows in rows_by_round.items():
        for row in sorted(rows, key=lambda item: result_sort_key(item.result)):
            values = [
                round_id,
                row.result,
                fmt(row.gain_hr, 3),
                fmt(row.hpwl_gain_percent, 4),
                fmt(row.runtime_penalty_pp, 4),
                fmt(row.hpwlg),
                fmt(row.hpwl_legalized),
                fmt(row.delta_legalization_percent, 2, "%"),
                fmt(row.hpwl_after_improve),
                fmt(row.delta_improve_percent, 2, "%"),
                fmt(row.hpwl_final),
                fmt(row.delta_final_percent, 2, "%"),
                fmt(row.runtime_seconds, 3),
                fmt(row.avg_displacement_micron, 3),
                fmt(row.max_displacement_micron, 3),
                row.legality,
                row.evidence_kind,
            ]
            if include_path:
                values.append(str(row.evidence_path))
            print("| " + " | ".join(values) + " |")


def print_tsv(rows_by_round: dict[str, list[StageRow]], *, include_path: bool) -> None:
    fields = [
        "round",
        "result",
        "gain_hr",
        "hpwl_gain_percent",
        "runtime_penalty_pp",
        "hpwl_global_micron",
        "hpwl_legalized_micron",
        "delta_legalization_percent",
        "hpwl_after_improve_micron",
        "delta_improve_percent",
        "hpwl_final_micron",
        "delta_final_percent",
        "runtime_seconds",
        "avg_displacement_micron",
        "max_displacement_micron",
        "legality",
        "source",
    ]
    if include_path:
        fields.append("metrics_path")
    print("\t".join(fields))
    for round_id, rows in rows_by_round.items():
        for row in sorted(rows, key=lambda item: result_sort_key(item.result)):
            values = [
                round_id,
                row.result,
                fmt(row.gain_hr, 6),
                fmt(row.hpwl_gain_percent, 6),
                fmt(row.runtime_penalty_pp, 6),
                fmt(row.hpwlg),
                fmt(row.hpwl_legalized),
                fmt(row.delta_legalization_percent, 6),
                fmt(row.hpwl_after_improve),
                fmt(row.delta_improve_percent, 6),
                fmt(row.hpwl_final),
                fmt(row.delta_final_percent, 6),
                fmt(row.runtime_seconds, 6),
                fmt(row.avg_displacement_micron, 6),
                fmt(row.max_displacement_micron, 6),
                row.legality,
                row.evidence_kind,
            ]
            if include_path:
                values.append(str(row.evidence_path))
            print("\t".join(values))


def main() -> int:
    args = parse_args()
    rows_by_round: dict[str, list[StageRow]] = {}
    for round_id in args.round_id:
        rows: list[StageRow] = []
        seen_run_tags: set[str] = set()
        for metrics_path in discover_metrics(args.orfs_root, round_id):
            row = load_row(round_id, metrics_path)
            if row is not None:
                rows.append(row)
                seen_run_tags.add(metrics_path.parent.name)
        present_results = {row.result for row in rows}
        packet_baselines = discover_packet_baseline_metrics(
            state_root=args.state_root,
            round_id=round_id,
        )
        reused_baselines = discover_reused_baseline_metrics(
            orfs_root=args.orfs_root,
            state_root=args.state_root,
            round_id=round_id,
        )
        for line, result in (
            ("openroad_dpl_flow", "baseline_probe_openroad_dpl_flow"),
            ("openroad_dpl_negotiation", "baseline_probe_openroad_dpl_negotiation"),
            ("evolve_default", "baseline_probe_evolve_default"),
        ):
            if result in present_results:
                continue
            metrics_path = packet_baselines.get(line) or reused_baselines.get(line)
            if metrics_path is None:
                continue
            row = load_row(round_id, metrics_path)
            if row is not None:
                row.result = result
                row.evidence_kind = (
                    "metrics_packet" if line in packet_baselines else "metrics_reused"
                )
                rows.append(row)
                present_results.add(result)
                seen_run_tags.add(metrics_path.parent.name)
        for log_path in discover_logs(args.orfs_root, round_id):
            if log_path.parent.name in seen_run_tags:
                continue
            row = load_log_row(round_id, log_path)
            if row is not None:
                rows.append(row)
        if not args.no_expected:
            rows = add_expected_rows(
                rows=rows,
                state_root=args.state_root,
                round_id=round_id,
                expected_iterations=args.expected_iterations,
                children=args.children,
            )
        annotate_hpwl_runtime_gains(rows)
        rows_by_round[round_id] = rows

    if args.format == "json":
        payload = {
            round_id: [
                {
                    "result": row.result,
                    "evidence_path": str(row.evidence_path),
                    "evidence_kind": row.evidence_kind,
                    "gain_hr": row.gain_hr,
                    "hpwl_gain_percent": row.hpwl_gain_percent,
                    "runtime_penalty_pp": row.runtime_penalty_pp,
                    "runtime_ratio": row.runtime_ratio,
                    "hpwl_global_micron": row.hpwlg,
                    "hpwl_legalized_micron": row.hpwl_legalized,
                    "delta_legalization_percent": row.delta_legalization_percent,
                    "hpwl_after_improve_micron": row.hpwl_after_improve,
                    "delta_improve_percent": row.delta_improve_percent,
                    "hpwl_final_micron": row.hpwl_final,
                    "delta_final_percent": row.delta_final_percent,
                    "runtime_seconds": row.runtime_seconds,
                    "avg_displacement_micron": row.avg_displacement_micron,
                    "max_displacement_micron": row.max_displacement_micron,
                    "legality": row.legality,
                }
                for row in sorted(rows, key=lambda item: result_sort_key(item.result))
            ]
            for round_id, rows in rows_by_round.items()
        }
        print(json.dumps(payload, indent=2, sort_keys=True))
    elif args.format == "tsv":
        print_tsv(rows_by_round, include_path=args.include_path)
    else:
        print_markdown(rows_by_round, include_path=args.include_path)

    if args.show_missing:
        for round_id, rows in rows_by_round.items():
            if not rows:
                print(f"\nNo metrics found for {round_id}. Status: {round_status(args.state_root, round_id)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
