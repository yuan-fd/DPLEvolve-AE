#!/usr/bin/env python3
"""One-command live checker for active DPL-evolve experiment batches.

This is intentionally lightweight.  It reads only round-local state under
`.dpl_evolve_state` and the process table, so it is safe to run repeatedly while
large experiments are active.
"""
from __future__ import annotations

import argparse
import csv
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path
from typing import Any


AGENT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_STATE_ROOT = AGENT_ROOT / ".dpl_evolve_state"

if str(AGENT_ROOT) not in sys.path:
    sys.path.insert(0, str(AGENT_ROOT))

from scripts.analysis import report_experiment_quick_status as quick_status  # noqa: E402


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Print a compact live status for the current DPL-evolve run."
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
    parser.add_argument(
        "--round-id",
        action="append",
        default=[],
        help="Also include a standalone round id.",
    )
    parser.add_argument(
        "--top",
        type=int,
        default=5,
        help="Number of best candidate rows to print per round. Default: 5.",
    )
    parser.add_argument(
        "--no-processes",
        action="store_true",
        help="Skip process-table inspection.",
    )
    parser.add_argument(
        "--show-status-files",
        action="store_true",
        help="Print raw experiments.tsv/status.tsv tail for the selected batch.",
    )
    return parser.parse_args()


def resolve_batch(args: argparse.Namespace) -> Path | None:
    batch = args.batch_root
    if batch is None:
        batch = quick_status.latest_batch(args.state_root, args.run_prefix)
    if batch is not None and not batch.is_absolute():
        batch = (Path.cwd() / batch).resolve()
    return batch


def load_round_specs(batch_root: Path | None, round_ids: list[str]) -> list[quick_status.RoundSpec]:
    specs = quick_status.load_batch_specs(batch_root)
    seen = {spec.round_id for spec in specs}
    for round_id in round_ids:
        if round_id not in seen:
            specs.append(quick_status.RoundSpec(case="", round_id=round_id, status="explicit"))
            seen.add(round_id)
    return specs


def read_status_tail(batch_root: Path, name: str, limit: int = 20) -> list[str]:
    path = batch_root / name
    if not path.is_file():
        return [f"missing {path}"]
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    return lines[-limit:] or ["(empty)"]


def active_process_rows(round_ids: set[str], batch_name: str, run_prefix: str | None) -> list[str]:
    user = os.environ.get("USER", "")
    try:
        output = subprocess.check_output(
            ["ps", "-u", user, "-o", "pid,ppid,stat,etime,cmd"],
            text=True,
            stderr=subprocess.DEVNULL,
        )
    except Exception:
        return []

    tokens = [
        "run_evolve_9case",
        "optimize_case_with_codex.py",
        "run_codex_exec.py",
        "codex exec",
        "build_openroad_variant",
        "openroad -exit",
        "ninja",
        "cmake",
        "cc1plus",
    ]
    selectors = {batch_name}
    if run_prefix:
        selectors.add(run_prefix)
    selectors.update(round_ids)

    rows: list[str] = []
    self_pid = str(os.getpid())
    for line in output.splitlines()[1:]:
        if line.split(maxsplit=1)[0] == self_pid:
            continue
        if "check_evolve_now.py" in line:
            continue
        if not any(token in line for token in tokens):
            continue
        if selectors and not any(selector and selector in line for selector in selectors):
            continue
        rows.append(line)
    return rows


def process_counts(rows: list[str]) -> dict[str, int]:
    counts = {
        "launcher": 0,
        "controller": 0,
        "student": 0,
        "codex": 0,
        "build_eval": 0,
    }
    for line in rows:
        if "run_evolve_9case" in line:
            counts["launcher"] += 1
        if "optimize_case_with_codex.py" in line:
            counts["controller"] += 1
        if "run_codex_exec.py" in line:
            counts["student"] += 1
        if "codex exec" in line:
            counts["codex"] += 1
        if any(token in line for token in ("build_openroad_variant", "openroad -exit", "ninja", "cmake", "cc1plus")):
            counts["build_eval"] += 1
    return counts


def operation_status(state_root: Path, round_id: str) -> tuple[int, int, list[str]]:
    ops_root = state_root / round_id / "checkpoints" / "operations"
    if not ops_root.is_dir():
        return 0, 0, []
    total = 0
    done = 0
    incomplete: list[str] = []
    for op in sorted(path for path in ops_root.iterdir() if path.is_dir()):
        total += 1
        if (op / "codex_usage_summary.json").is_file():
            done += 1
        else:
            incomplete.append(op.name)
    return total, done, incomplete


def source_trial_summary(state_root: Path, round_id: str) -> dict[str, Any]:
    round_root = state_root / round_id / "teacher_rounds" / "students"
    summary: dict[str, Any] = {
        "event_count": 0,
        "kept": [],
        "rejected": [],
        "finalize": 0,
    }
    if not round_root.is_dir():
        return summary
    for path in sorted(round_root.glob("student_*/iter_*/artifacts/source_trials.jsonl")):
        rel = path.relative_to(state_root / round_id)
        for raw_line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            if not raw_line.strip():
                continue
            try:
                event = json.loads(raw_line)
            except json.JSONDecodeError:
                continue
            action = str(event.get("action") or "")
            ref = str(event.get("ref") or event.get("candidate_ref") or "")
            commit = str(event.get("source_commit") or "")
            reason = str(event.get("reason") or "")
            item = {
                "action": action,
                "ref": ref,
                "commit": commit,
                "reason": reason,
                "path": str(rel),
            }
            summary["event_count"] += 1
            if action == "keep":
                summary["kept"].append(item)
            elif action == "reject":
                summary["rejected"].append(item)
            elif action == "finalize":
                summary["finalize"] += 1
    return summary


def candidate_rows(state_root: Path, round_id: str) -> list[quick_status.CandidateRow]:
    return quick_status.load_candidates(state_root, round_id)


def baseline_row(state_root: Path, round_id: str) -> Any:
    return quick_status.parse_baseline_packet(state_root, round_id)


def fmt_float(value: float | None, digits: int = 1) -> str:
    if value is None:
        return "-"
    return f"{value:.{digits}f}"


def print_batch_files(batch_root: Path) -> None:
    print("\n## Batch Files")
    for name in ("experiments.tsv", "status.tsv"):
        print(f"\n### {name}")
        for line in read_status_tail(batch_root, name):
            print(line)


def print_round_detail(state_root: Path, specs: list[quick_status.RoundSpec], top: int) -> None:
    print("\n## Rounds")
    for spec in specs:
        spec = quick_status.enrich_spec(state_root, spec)
        baseline = baseline_row(state_root, spec.round_id)
        candidates = candidate_rows(state_root, spec.round_id)
        best = quick_status.best_candidate(candidates, baseline)
        delta = quick_status.pct_delta(best, baseline)
        gain = quick_status.gain_hr(best, baseline)
        total, done, incomplete = operation_status(state_root, spec.round_id)
        trials = source_trial_summary(state_root, spec.round_id)
        print(f"\n### {spec.case or '(unknown case)'}")
        print(f"- round_id: `{spec.round_id}`")
        print(f"- status: `{spec.status or 'seen'}`")
        print(f"- operations: `{done}/{total}` done")
        print(
            "- source_trials: "
            f"`events={trials['event_count']}, kept={len(trials['kept'])}, "
            f"rejected={len(trials['rejected'])}, finalize={trials['finalize']}`"
        )
        if trials["rejected"]:
            print("- recent rejected refs:")
            for item in trials["rejected"][-5:]:
                reason = item["reason"]
                if len(reason) > 120:
                    reason = reason[:117] + "..."
                print(f"  - `{item['ref']}` reason={reason}")
        if incomplete:
            print("- incomplete:")
            for name in incomplete[:12]:
                print(f"  - `{name}`")
            if len(incomplete) > 12:
                print(f"  - ... {len(incomplete) - 12} more")
        print(
            "- best: "
            f"`{best.iter_name}/{best.student_id}` "
            f"HPWL `{fmt_float(best.hpwl_final if best else None)}` "
            f"delta `{fmt_float(delta, 3)}%` "
            f"G_HR `{fmt_float(gain, 3)}` "
            f"runtime `{fmt_float(best.runtime_seconds if best else None, 3)}s`"
            if best
            else "- best: `none`"
        )
        if baseline is not None:
            print(
                f"- baseline: HPWL `{fmt_float(baseline.hpwl_final)}` "
                f"runtime `{fmt_float(baseline.runtime_seconds, 3)}s`"
            )
        clean = [row for row in candidates if row.legality == "clean"]
        clean.sort(
            key=lambda row: (
                row.hpwl_final,
                row.runtime_seconds if row.runtime_seconds is not None else float("inf"),
            )
        )
        if clean:
            print(f"- top {min(top, len(clean))}:")
            for row in clean[:top]:
                row_delta = quick_status.pct_delta(row, baseline)
                print(
                    f"  - `{row.iter_name}/{row.student_id}` "
                    f"HPWL `{row.hpwl_final:.1f}` "
                    f"delta `{fmt_float(row_delta, 3)}%` "
                    f"runtime `{fmt_float(row.runtime_seconds, 3)}s`"
                )


def main() -> int:
    args = parse_args()
    state_root = args.state_root.resolve()
    batch_root = resolve_batch(args)
    specs = load_round_specs(batch_root, args.round_id)
    specs = [quick_status.enrich_spec(state_root, spec) for spec in specs]
    batch_name = batch_root.name if batch_root else ""
    round_ids = {spec.round_id for spec in specs}

    print("# DPL Evolve Live Check")
    print(f"- generated_at: `{time.strftime('%Y-%m-%d %H:%M:%S %Z')}`")
    print(f"- state_root: `{state_root}`")
    print(f"- batch: `{batch_root or 'none'}`")

    if batch_root is not None and args.show_status_files:
        print_batch_files(batch_root)

    if not args.no_processes:
        rows = active_process_rows(round_ids, batch_name, args.run_prefix)
        counts = process_counts(rows)
        print("\n## Processes")
        print(
            "- counts: "
            + ", ".join(f"{key}={value}" for key, value in counts.items())
        )
        if rows:
            print("- sample:")
            for line in rows[:12]:
                print(f"  - `{line[:240]}`")
            if len(rows) > 12:
                print(f"  - ... {len(rows) - 12} more")
        else:
            print("- sample: none")

    print_round_detail(state_root, specs, max(1, args.top))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
