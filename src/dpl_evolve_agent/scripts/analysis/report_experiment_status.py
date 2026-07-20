#!/usr/bin/env python3
"""Summarize a DPL-evolve launch plus canonical stage-wise metrics.

This is a human/Teacher monitoring wrapper around `report_stage_metrics.py`.
It keeps the headline comparison on evaluator/OpenROAD-DPL `metrics.json`
rows, and uses partial DPL logs only as in-flight evidence.
"""
from __future__ import annotations

import argparse
import csv
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Any


SCRIPT_ROOT = Path(__file__).resolve().parent
AGENT_ROOT = Path(os.environ.get("DPL_EVOLVE_AGENT_ROOT", Path(__file__).resolve().parents[2])).resolve()
DEFAULT_ORFS_ROOT = Path(os.environ.get("ORFS_ROOT", AGENT_ROOT.parent / "OpenROAD-flow-scripts")).resolve()
DEFAULT_STATE_ROOT = Path(os.environ.get("DPL_EVOLVE_STATE_ROOT", AGENT_ROOT / ".dpl_evolve_state")).resolve()

if str(AGENT_ROOT) not in sys.path:
    sys.path.insert(0, str(AGENT_ROOT))

from scripts.evaluator import report_stage_metrics as stage_metrics  # noqa: E402


@dataclass
class RoundInfo:
    round_id: str
    case: str = ""
    flow_variant: str = ""
    status: str = "seen"
    start_time: str = ""
    end_time: str = ""
    attempts: int = 0
    notes: list[str] = field(default_factory=list)


@dataclass
class ProcessSummary:
    active_round_ids: set[str]
    counts: dict[str, int]
    lines: list[str]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Report active DPL-evolve launch status and canonical stage-wise "
            "HPWL/runtime/displacement metrics."
        )
    )
    parser.add_argument(
        "--launch-dir",
        type=Path,
        help=(
            "Launch directory under .dpl_evolve_state/launch_runs. Defaults to "
            "the most recently modified launch directory when --round-id is not set."
        ),
    )
    parser.add_argument(
        "--round-id",
        action="append",
        help="Additional or standalone teacher round id to report.",
    )
    parser.add_argument("--orfs-root", type=Path, default=DEFAULT_ORFS_ROOT)
    parser.add_argument("--state-root", type=Path, default=DEFAULT_STATE_ROOT)
    parser.add_argument(
        "--format",
        choices=("markdown", "tsv", "json"),
        default="markdown",
        help="Output format for the summary. Default: markdown.",
    )
    parser.add_argument(
        "--detail-round",
        action="append",
        help=(
            "Print stage-wise rows for a round id. Also accepts 'active' or "
            "'latest'. May be passed multiple times."
        ),
    )
    parser.add_argument(
        "--detail-tail",
        type=int,
        default=0,
        help="Only print the last N sorted stage rows for --detail-round.",
    )
    parser.add_argument(
        "--include-path",
        action="store_true",
        help="Include metrics/log evidence path in detailed stage rows.",
    )
    parser.add_argument(
        "--show-expected",
        action="store_true",
        help="Expand missing expected rows in detailed tables.",
    )
    parser.add_argument(
        "--expected-iterations",
        type=int,
        help="Expected iteration count when --show-expected is used.",
    )
    parser.add_argument(
        "--children",
        type=int,
        help="Expected student count when --show-expected is used.",
    )
    parser.add_argument(
        "--show-processes",
        action="store_true",
        help="Print sanitized active process lines in markdown output.",
    )
    parser.add_argument(
        "--no-processes",
        action="store_true",
        help="Do not inspect the process table.",
    )
    return parser.parse_args()


def latest_launch_dir(state_root: Path) -> Path | None:
    launch_root = state_root / "launch_runs"
    if not launch_root.exists():
        return None
    candidates = [path for path in launch_root.iterdir() if path.is_dir()]
    if not candidates:
        return None
    return max(candidates, key=lambda path: path.stat().st_mtime)


def parse_launch_summary(launch_dir: Path | None) -> dict[str, RoundInfo]:
    rounds: dict[str, RoundInfo] = {}
    if launch_dir is None:
        return rounds
    summary = launch_dir / "summary.tsv"
    if not summary.is_file():
        return rounds
    with summary.open(newline="", encoding="utf-8", errors="replace") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        for row in reader:
            round_id = row.get("round_id", "")
            if not round_id:
                continue
            info = rounds.setdefault(round_id, RoundInfo(round_id=round_id))
            info.case = row.get("case", "") or info.case
            info.flow_variant = row.get("flow_variant", "") or info.flow_variant
            info.status = row.get("status", "") or info.status
            info.start_time = info.start_time or row.get("start_time", "")
            info.end_time = row.get("end_time", "") or info.end_time
            info.attempts += 1
    return rounds


def parse_run_log(launch_dir: Path | None) -> dict[str, RoundInfo]:
    rounds: dict[str, RoundInfo] = {}
    if launch_dir is None:
        return rounds
    run_log = launch_dir / "run.log"
    if not run_log.is_file():
        return rounds
    command_re = re.compile(
        r"--case\s+(?P<case>\S+).*?--flow-variant\s+(?P<flow>\S+).*?"
        r"--round-id\s+(?P<round>\S+)"
    )
    text = run_log.read_text(encoding="utf-8", errors="replace")
    for match in command_re.finditer(text):
        round_id = match.group("round")
        info = rounds.setdefault(round_id, RoundInfo(round_id=round_id))
        info.case = match.group("case")
        info.flow_variant = match.group("flow")
    return rounds


def enrich_round_info(info: RoundInfo, state_root: Path) -> RoundInfo:
    if not info.case:
        info.case = stage_metrics.round_case_id(state_root, info.round_id)
    if not info.flow_variant:
        info.flow_variant = stage_metrics.round_flow_variant(state_root, info.round_id)
    return info


def active_process_summary(round_ids: set[str], state_root: Path = DEFAULT_STATE_ROOT) -> ProcessSummary:
    counts: dict[str, int] = {
        "optimize_case": 0,
        "run_codex_exec": 0,
        "codex_exec": 0,
        "build": 0,
        "evaluate": 0,
        "openroad": 0,
    }
    lines: list[str] = []
    active_rounds: set[str] = set()
    user = os.environ.get("USER", "")
    try:
        output = subprocess.check_output(
            ["ps", "-o", "pid,ppid,etime,stat,cmd", "-u", user],
            text=True,
            stderr=subprocess.DEVNULL,
        )
    except Exception:  # Broad except acceptable for reporting scripts
        return ProcessSummary(active_round_ids=set(), counts=counts, lines=[])

    interesting = (
        "optimize_case_with_codex",
        "run_codex_exec.py",
        "codex exec",
        "build_openroad_variant_relink",
        "20_evaluate_candidate",
        "run_baseline.sh",
        "openroad -exit",
    )
    def accept_round_id(round_id: str) -> bool:
        if not round_id:
            return False
        if round_id in round_ids:
            return True
        return (state_root / round_id).is_dir()

    for raw_line in output.splitlines()[1:]:
        if not any(token in raw_line for token in interesting):
            continue
        round_arg = re.search(r"--round-id\s+(\S+)", raw_line)
        if round_arg and accept_round_id(round_arg.group(1)):
            active_rounds.add(round_arg.group(1))
        op_arg = re.search(r"--operation-id\s+(\S+?_iter_\d+_(?:teacher|student))", raw_line)
        if op_arg:
            round_id = re.sub(r"_iter_\d+_(?:teacher|student).*", "", op_arg.group(1))
            if accept_round_id(round_id):
                active_rounds.add(round_id)
        for round_id in round_ids:
            if round_id and round_id in raw_line:
                active_rounds.add(round_id)
        if "optimize_case_with_codex" in raw_line:
            counts["optimize_case"] += 1
        if "run_codex_exec.py" in raw_line:
            counts["run_codex_exec"] += 1
        if "codex exec" in raw_line:
            counts["codex_exec"] += 1
        if "build_openroad_variant_relink" in raw_line:
            counts["build"] += 1
        if "20_evaluate_candidate" in raw_line or "run_baseline.sh" in raw_line:
            counts["evaluate"] += 1
        if "openroad -exit" in raw_line:
            counts["openroad"] += 1
        lines.append(sanitize_process_line(raw_line))
    return ProcessSummary(active_round_ids=active_rounds, counts=counts, lines=lines)


def sanitize_process_line(line: str) -> str:
    replacements = [
        (r"--prompt-file\s+\S+", "--prompt-file ..."),
        (r"--session-state\s+\S+", "--session-state ..."),
        (r"--session-env-file\s+\S+", "--session-env-file ..."),
        (r"--add-dir\s+\S+", ""),
        (
            r"\S*/dpl_evolve_agent/\.dpl_evolve_state/\S+/teacher_rounds/\S+",
            "...",
        ),
        (r"\S*/OpenROAD-flow-scripts", "ORFS"),
    ]
    out = line
    for pattern, replacement in replacements:
        out = re.sub(pattern, replacement, out)
    return re.sub(r"\s+", " ", out).strip()


def collect_rows(
    *,
    orfs_root: Path,
    state_root: Path,
    round_id: str,
    include_expected: bool,
    expected_iterations: int | None,
    children: int | None,
) -> list[stage_metrics.StageRow]:
    rows: list[stage_metrics.StageRow] = []
    seen_run_tags: set[str] = set()
    for metrics_path in stage_metrics.discover_metrics(orfs_root, round_id):
        row = stage_metrics.load_row(round_id, metrics_path)
        if row is not None:
            rows.append(row)
            seen_run_tags.add(metrics_path.parent.name)

    present_results = {row.result for row in rows}
    packet_baselines = stage_metrics.discover_packet_baseline_metrics(
        state_root=state_root,
        round_id=round_id,
    )
    reused_baselines = stage_metrics.discover_reused_baseline_metrics(
        orfs_root=orfs_root,
        state_root=state_root,
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
        row = stage_metrics.load_row(round_id, metrics_path)
        if row is None:
            continue
        row.result = result
        row.evidence_kind = "metrics_packet" if line in packet_baselines else "metrics_reused"
        rows.append(row)
        present_results.add(result)
        seen_run_tags.add(metrics_path.parent.name)

    for log_path in stage_metrics.discover_logs(orfs_root, round_id):
        if log_path.parent.name in seen_run_tags:
            continue
        row = stage_metrics.load_log_row(round_id, log_path)
        if row is not None:
            rows.append(row)

    if include_expected:
        rows = stage_metrics.add_expected_rows(
            rows=rows,
            state_root=state_root,
            round_id=round_id,
            expected_iterations=expected_iterations,
            children=children,
        )
    stage_metrics.annotate_hpwl_runtime_gains(rows)
    return sorted(rows, key=lambda item: stage_metrics.result_sort_key(item.result))


def is_clean_metric(row: stage_metrics.StageRow) -> bool:
    return (
        row.evidence_kind.startswith("metrics")
        and row.legality == "clean"
        and row.hpwl_final is not None
    )


def best_summary(rows: list[stage_metrics.StageRow]) -> dict[str, Any]:
    baselines = [
        row
        for row in rows
        if row.result.startswith("baseline_probe_") and is_clean_metric(row)
    ]
    candidates = [
        row
        for row in rows
        if not row.result.startswith("baseline_probe_") and is_clean_metric(row)
    ]
    best_baseline = next(
        (row for row in baselines if row.result == "baseline_probe_openroad_dpl_flow"),
        None,
    )
    if best_baseline is None and baselines:
        best_baseline = min(baselines, key=lambda row: row.hpwl_final)
    hpwl_winning_candidates = (
        []
        if best_baseline is None or best_baseline.hpwl_final is None
        else [
            row
            for row in candidates
            if row.hpwl_final is not None and row.hpwl_final < best_baseline.hpwl_final
        ]
    )
    if hpwl_winning_candidates:
        best_candidate = max(
            hpwl_winning_candidates,
            key=lambda row: (
                -row.hpwl_final if row.hpwl_final is not None else float("-inf"),
                row.gain_hr if row.gain_hr is not None else float("-inf"),
            ),
        )
    else:
        best_candidate = min(candidates, key=lambda row: row.hpwl_final) if candidates else None
    improvement = None
    if (
        best_baseline is not None
        and best_candidate is not None
        and best_baseline.hpwl_final not in (None, 0.0)
    ):
        improvement = (
            (best_candidate.hpwl_final - best_baseline.hpwl_final)
            / best_baseline.hpwl_final
            * 100.0
        )
    latest = rows[-1] if rows else None
    global_hpwl = None
    for row in (best_baseline, best_candidate, latest):
        if row is not None and row.hpwlg is not None:
            global_hpwl = row.hpwlg
            break
    if global_hpwl is None:
        for row in rows:
            if row.hpwlg is not None:
                global_hpwl = row.hpwlg
                break
    return {
        "hpwl_global_micron": global_hpwl,
        "best_baseline": best_baseline,
        "best_candidate": best_candidate,
        "improvement_percent": improvement,
        "gain_hr": None if best_candidate is None else best_candidate.gain_hr,
        "latest": latest,
    }


def row_to_json(row: stage_metrics.StageRow | None) -> dict[str, Any] | None:
    if row is None:
        return None
    return {
        "result": row.result,
        "hpwl_global_micron": row.hpwlg,
        "hpwl_legalized_micron": row.hpwl_legalized,
        "hpwl_after_improve_micron": row.hpwl_after_improve,
        "hpwl_final_micron": row.hpwl_final,
        "delta_legalization_percent": row.delta_legalization_percent,
        "delta_improve_percent": row.delta_improve_percent,
        "delta_final_percent": row.delta_final_percent,
        "runtime_seconds": row.runtime_seconds,
        "gain_hr": row.gain_hr,
        "hpwl_gain_percent": row.hpwl_gain_percent,
        "runtime_penalty_pp": row.runtime_penalty_pp,
        "runtime_ratio": row.runtime_ratio,
        "avg_displacement_micron": row.avg_displacement_micron,
        "max_displacement_micron": row.max_displacement_micron,
        "legality": row.legality,
        "evidence_kind": row.evidence_kind,
        "evidence_path": str(row.evidence_path),
    }


def fmt(value: float | None, digits: int = 1, suffix: str = "") -> str:
    return "" if value is None else f"{value:.{digits}f}{suffix}"


def print_markdown(
    *,
    launch_dir: Path | None,
    rounds: list[RoundInfo],
    rows_by_round: dict[str, list[stage_metrics.StageRow]],
    processes: ProcessSummary | None,
    show_processes: bool,
    detail_rounds: list[str],
    detail_tail: int,
    include_path: bool,
) -> None:
    print("# DPL Evolve Metrics Status")
    print()
    print(f"- generated_at: `{datetime.now().astimezone().isoformat(timespec='seconds')}`")
    if launch_dir is not None:
        print(f"- launch_dir: `{launch_dir}`")
    if processes is not None:
        active = ", ".join(sorted(processes.active_round_ids)) or "none"
        counts = ", ".join(
            f"{name}={value}" for name, value in processes.counts.items() if value
        )
        print(f"- active_round_ids: `{active}`")
        print(f"- active_process_counts: `{counts or 'none'}`")

    print()
    print("## Round Summary")
    print(
        "| case | status | round | HPWLg | best baseline | baseline final | "
        "best HPWL evolved | G_HR check | evolved final | HPWL vs baseline | runtime_s | legality | "
        "attempts |"
    )
    print("|---|---|---|---:|---|---:|---|---:|---:|---:|---:|---|---:|")
    for info in rounds:
        rows = rows_by_round.get(info.round_id, [])
        summary = best_summary(rows)
        best_baseline = summary["best_baseline"]
        best_candidate = summary["best_candidate"]
        print(
            "| "
            + " | ".join(
                [
                    info.case,
                    info.status,
                    f"`{info.round_id}`",
                    fmt(summary["hpwl_global_micron"]),
                    best_baseline.result if best_baseline else "",
                    fmt(best_baseline.hpwl_final if best_baseline else None),
                    best_candidate.result if best_candidate else "",
                    fmt(best_candidate.gain_hr if best_candidate else None, 3),
                    fmt(best_candidate.hpwl_final if best_candidate else None),
                    fmt(summary["improvement_percent"], 3, "%"),
                    fmt(best_candidate.runtime_seconds if best_candidate else None, 3),
                    best_candidate.legality if best_candidate else "",
                    str(info.attempts),
                ]
            )
            + " |"
        )

    if show_processes and processes is not None and processes.lines:
        print()
        if processes.lines and any(processes.lines):
            print("## Active Process Lines")
            for line in processes.lines:
                print(f"- `{line}`")

    for round_id in detail_rounds:
        rows = rows_by_round.get(round_id, [])
        if detail_tail > 0:
            rows = rows[-detail_tail:]
        print()
        print(f"## Stage Metrics: `{round_id}`")
        columns = [
            "result",
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
        if include_path:
            columns.append("evidence")
        print("| " + " | ".join(columns) + " |")
        print("|" + "|".join(["---"] * len(columns)) + "|")
        for row in rows:
            values = [
                row.result,
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


def print_tsv(
    *, rounds: list[RoundInfo], rows_by_round: dict[str, list[stage_metrics.StageRow]]
) -> None:
    print(
        "\t".join(
            [
                "case",
                "status",
                "round_id",
                "hpwl_global_micron",
                "best_baseline",
                "baseline_final_micron",
                "best_evolved",
                "evolved_gain_hr",
                "evolved_final_micron",
                "improvement_vs_baseline_percent",
                "runtime_seconds",
                "legality",
                "attempts",
            ]
        )
    )
    for info in rounds:
        summary = best_summary(rows_by_round.get(info.round_id, []))
        baseline = summary["best_baseline"]
        evolved = summary["best_candidate"]
        print(
            "\t".join(
                [
                    info.case,
                    info.status,
                    info.round_id,
                    fmt(summary["hpwl_global_micron"]),
                    baseline.result if baseline else "",
                    fmt(baseline.hpwl_final if baseline else None),
                    evolved.result if evolved else "",
                    fmt(evolved.gain_hr if evolved else None, 6),
                    fmt(evolved.hpwl_final if evolved else None),
                    fmt(summary["improvement_percent"], 6),
                    fmt(evolved.runtime_seconds if evolved else None, 6),
                    evolved.legality if evolved else "",
                    str(info.attempts),
                ]
            )
        )


def resolve_detail_rounds(
    requested: list[str] | None,
    rounds: list[RoundInfo],
    processes: ProcessSummary | None,
) -> list[str]:
    if not requested:
        return []
    result: list[str] = []
    by_round = {info.round_id for info in rounds}
    active = sorted(processes.active_round_ids) if processes is not None else []
    latest = rounds[-1].round_id if rounds else ""
    for token in requested:
        if token == "active":
            result.extend(active or ([latest] if latest else []))
        elif token == "latest":
            if latest:
                result.append(latest)
        else:
            result.append(token)
    deduped: list[str] = []
    for round_id in result:
        if round_id and round_id not in deduped:
            deduped.append(round_id)
            if round_id not in by_round:
                rounds.append(RoundInfo(round_id=round_id, status="explicit"))
                by_round.add(round_id)
    return deduped


def main() -> int:
    args = parse_args()
    launch_dir = args.launch_dir
    if launch_dir is None and not args.round_id:
        launch_dir = latest_launch_dir(args.state_root)
    if launch_dir is not None and not launch_dir.is_absolute():
        launch_dir = (Path.cwd() / launch_dir).resolve()

    round_map = parse_run_log(launch_dir)
    for round_id, info in parse_launch_summary(launch_dir).items():
        existing = round_map.setdefault(round_id, RoundInfo(round_id=round_id))
        existing.case = info.case or existing.case
        existing.flow_variant = info.flow_variant or existing.flow_variant
        existing.status = info.status or existing.status
        existing.start_time = info.start_time or existing.start_time
        existing.end_time = info.end_time or existing.end_time
        existing.attempts = max(existing.attempts, info.attempts)
    for round_id in args.round_id or []:
        round_map.setdefault(round_id, RoundInfo(round_id=round_id, status="explicit"))

    rounds = [enrich_round_info(info, args.state_root) for info in round_map.values()]
    rounds.sort(key=lambda info: (info.start_time or "", info.round_id))

    processes = None
    if not args.no_processes:
        processes = active_process_summary({info.round_id for info in rounds}, args.state_root)
        for round_id in sorted(processes.active_round_ids):
            if round_id not in round_map:
                round_map[round_id] = enrich_round_info(
                    RoundInfo(round_id=round_id, status="active"),
                    args.state_root,
                )
        rounds = [enrich_round_info(info, args.state_root) for info in round_map.values()]
        rounds.sort(key=lambda info: (info.start_time or "", info.round_id))
        for info in rounds:
            if info.round_id in processes.active_round_ids and info.status != "ok":
                info.status = "active"

    detail_rounds = resolve_detail_rounds(args.detail_round, rounds, processes)
    rows_by_round: dict[str, list[stage_metrics.StageRow]] = {}
    for info in rounds:
        rows_by_round[info.round_id] = collect_rows(
            orfs_root=args.orfs_root,
            state_root=args.state_root,
            round_id=info.round_id,
            include_expected=args.show_expected,
            expected_iterations=args.expected_iterations,
            children=args.children,
        )

    if args.format == "json":
        payload = {
            "generated_at": datetime.now().astimezone().isoformat(timespec="seconds"),
            "launch_dir": str(launch_dir) if launch_dir is not None else "",
            "active_round_ids": sorted(processes.active_round_ids)
            if processes is not None
            else [],
            "process_counts": processes.counts if processes is not None else {},
            "rounds": [
                {
                    "case": info.case,
                    "flow_variant": info.flow_variant,
                    "round_id": info.round_id,
                    "status": info.status,
                    "start_time": info.start_time,
                    "end_time": info.end_time,
                    "attempts": info.attempts,
                    "best": {
                        "hpwl_global_micron": best_summary(
                            rows_by_round.get(info.round_id, [])
                        )["hpwl_global_micron"],
                        "baseline": row_to_json(
                            best_summary(rows_by_round.get(info.round_id, []))[
                                "best_baseline"
                            ]
                        ),
                        "evolved": row_to_json(
                            best_summary(rows_by_round.get(info.round_id, []))[
                                "best_candidate"
                            ]
                        ),
                        "improvement_percent": best_summary(
                            rows_by_round.get(info.round_id, [])
                        )["improvement_percent"],
                    },
                }
                for info in rounds
            ],
            "details": {
                round_id: [row_to_json(row) for row in rows_by_round.get(round_id, [])]
                for round_id in detail_rounds
            },
        }
        print(json.dumps(payload, indent=2, sort_keys=True))
    elif args.format == "tsv":
        print_tsv(rounds=rounds, rows_by_round=rows_by_round)
    else:
        print_markdown(
            launch_dir=launch_dir,
            rounds=rounds,
            rows_by_round=rows_by_round,
            processes=processes,
            show_processes=args.show_processes,
            detail_rounds=detail_rounds,
            detail_tail=args.detail_tail,
            include_path=args.include_path,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
