#!/usr/bin/env python3
"""Terminal dashboard for one real ReviewDSE Teacher/Student round.

Every displayed transition is derived from the batch TSVs, round events,
Codex operation records, Student diffs/provenance, or evaluator metrics.
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


WAIT = "WAIT"
RUN = "RUN"
DONE = "DONE"
FAIL = "FAIL"


@dataclass(frozen=True)
class StudentView:
    student: int
    iteration: int = 0
    source: str = WAIT
    build: str = WAIT
    evaluate: str = WAIT
    legality: str = "-"
    hpwl: float | None = None
    runtime: float | None = None
    delta_percent: float | None = None
    diff_path: Path | None = None
    metrics_path: Path | None = None


@dataclass(frozen=True)
class IterationView:
    number: int
    teacher_plan: str
    teacher_plan_activity: str
    students: tuple[StudentView, ...]
    teacher_review: str
    teacher_review_activity: str


@dataclass(frozen=True)
class DashboardView:
    round_id: str | None
    batch_status: str
    iterations: tuple[IterationView, ...]
    latest_event: str
    baseline_hpwl: float | None
    final: bool
    failed: bool
    round_dir: Path | None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Watch a real ReviewDSE run in the terminal.")
    parser.add_argument("--state-root", type=Path, required=True)
    parser.add_argument("--batch-root", type=Path, required=True)
    parser.add_argument("--case", required=True)
    parser.add_argument("--students", type=int, required=True)
    parser.add_argument("--iterations", type=int, required=True)
    parser.add_argument("--teacher-model", required=True)
    parser.add_argument("--student-model", required=True)
    parser.add_argument("--launcher-pid", type=int)
    parser.add_argument("--launcher-log", type=Path)
    parser.add_argument("--refresh-seconds", type=float, default=2.0)
    parser.add_argument("--watch", action="store_true")
    parser.add_argument("--no-color", action="store_true")
    return parser.parse_args()


def load_json(path: Path) -> dict[str, Any] | None:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError, UnicodeDecodeError):
        return None
    return value if isinstance(value, dict) else None


def read_tsv(path: Path) -> list[dict[str, str]]:
    try:
        with path.open(newline="", encoding="utf-8", errors="replace") as stream:
            return list(csv.DictReader(stream, delimiter="\t"))
    except (OSError, csv.Error):
        return []


def read_events(path: Path) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return events
    for raw in lines:
        try:
            event = json.loads(raw)
        except json.JSONDecodeError:
            continue
        if isinstance(event, dict):
            events.append(event)
    return events


def as_float(value: Any) -> float | None:
    try:
        return None if value in (None, "") else float(value)
    except (TypeError, ValueError):
        return None


def discover_round_id(batch_root: Path, case_id: str) -> str | None:
    rows = read_tsv(batch_root / "experiments.tsv")
    for row in reversed(rows):
        if row.get("case") == case_id and row.get("round_id"):
            return row["round_id"]
    return None


def batch_result(batch_root: Path, case_id: str, round_id: str | None) -> str:
    for row in reversed(read_tsv(batch_root / "status.tsv")):
        if round_id and row.get("round_id") == round_id:
            return row.get("status", "")
        if not round_id and row.get("case") == case_id:
            return row.get("status", "")
    return ""


def event_matches(
    events: list[dict[str, Any]], stage: str, message: str, iteration: str
) -> bool:
    for event in events:
        fields = event.get("fields") if isinstance(event.get("fields"), dict) else {}
        if (
            event.get("stage") == stage
            and event.get("message") == message
            and fields.get("iteration") == iteration
        ):
            return True
    return False


def operation_state(operation_dir: Path) -> str:
    summary = load_json(operation_dir / "codex_usage_summary.json")
    if summary is not None:
        try:
            return DONE if int(summary.get("returncode")) == 0 else FAIL
        except (TypeError, ValueError):
            return FAIL
    if operation_dir.is_dir():
        return RUN
    return WAIT


def teacher_state(
    *,
    events: list[dict[str, Any]],
    operations_root: Path,
    operation_id: str,
    iteration: str,
    kind: str,
) -> str:
    failed_message = f"{kind}_failed"
    done_message = f"{kind}_done"
    launch_message = f"{kind}_launch"
    if event_matches(events, "teacher", failed_message, iteration):
        return FAIL
    if event_matches(events, "teacher", done_message, iteration):
        return DONE
    state = operation_state(operations_root / operation_id)
    if state != WAIT:
        return state
    if event_matches(events, "teacher", launch_message, iteration):
        return RUN
    return WAIT


def operation_text(operation_dir: Path) -> str:
    path = operation_dir / "codex_events.jsonl"
    try:
        return path.read_text(encoding="utf-8", errors="replace")[-2_000_000:].lower()
    except OSError:
        return ""


def operation_activity(operation_dir: Path) -> str:
    """Summarize visible Codex tool events without exposing hidden reasoning."""
    events_path = operation_dir / "codex_events.jsonl"
    try:
        lines = events_path.read_text(encoding="utf-8", errors="replace").splitlines()
        updated = time.strftime("%H:%M:%S", time.localtime(events_path.stat().st_mtime))
    except OSError:
        return ""

    command_count = 0
    last_command = ""
    turn_done = False
    for raw in lines:
        try:
            event = json.loads(raw)
        except json.JSONDecodeError:
            continue
        item = event.get("item") if isinstance(event.get("item"), dict) else {}
        if event.get("type") == "item.completed" and item.get("type") == "command_execution":
            command_count += 1
            last_command = str(item.get("command", ""))
        elif event.get("type") == "item.started" and item.get("type") == "command_execution":
            last_command = str(item.get("command", ""))
        elif event.get("type") == "turn.completed":
            turn_done = True

    detail = "working"
    basename_matches = re.findall(
        r"([A-Za-z0-9_.+-]+\.(?:cxx|cpp|cc|h|hpp|py|md|json|jsonl|tcl))",
        last_command,
        flags=re.IGNORECASE,
    )
    if basename_matches:
        detail = f"inspecting {basename_matches[-1]}"
    elif "query_knowledge" in last_command:
        detail = "querying the mechanism knowledge index"
    elif re.search(r"(?:^|\s)rg(?:\s|$)", last_command):
        detail = "searching source evidence"
    elif last_command:
        detail = "running a repository tool"
    if turn_done:
        detail = "model turn completed"
    age = max(0, int(time.time() - events_path.stat().st_mtime))
    freshness = "active now" if age < 30 else f"last event {age}s ago"
    return f"{updated} | {command_count} tools completed | {detail} | {freshness}"


def provenance_state(payload: dict[str, Any] | None) -> str | None:
    if payload is None:
        return None
    status = str(payload.get("status", "")).lower()
    if status in {"complete", "completed", "ok", "success", "verified", "valid"}:
        return DONE
    if status in {"failed", "failure", "error", "invalid"}:
        return FAIL
    return RUN


def diff_has_content(path: Path) -> bool:
    try:
        return path.is_file() and path.stat().st_size > 0
    except OSError:
        return False


def student_view(
    *,
    round_id: str,
    round_root: Path,
    operations_root: Path,
    iteration: int,
    student: int,
    baseline_hpwl: float | None,
) -> StudentView:
    iter_name = f"iter_{iteration:02d}"
    student_name = f"student_{student:02d}"
    operation_id = f"{round_id}_{iter_name}_{student_name}"
    operation_dir = operations_root / operation_id
    op_state = operation_state(operation_dir)
    artifact_dir = round_root / "students" / student_name / iter_name / "artifacts"

    diff_path = artifact_dir / "implementation.diff"
    source_commit = artifact_dir / "source_commit.json"
    build_payload = load_json(artifact_dir / "candidate_build_provenance.json")
    eval_start = load_json(artifact_dir / "candidate_evaluation_start.json")
    eval_payload = load_json(artifact_dir / "candidate_evaluation_provenance.json")
    metrics_path = artifact_dir / "candidate_metrics_summary.json"
    metrics_payload = load_json(metrics_path)
    event_text = operation_text(operation_dir)

    build_seen = any(
        token in event_text
        for token in ("build_openroad_variant", "10_build", "ninja", "cmake --build")
    )
    evaluate_seen = any(
        token in event_text
        for token in ("20_evaluate", "evaluate_candidate", "openroad -exit")
    )

    build = provenance_state(build_payload)
    evaluation_provenance = provenance_state(eval_payload)
    evaluate = evaluation_provenance
    if metrics_payload is not None:
        metrics_ok = str(metrics_payload.get("status", "ok")).lower() == "ok"
        evaluate = FAIL if evaluation_provenance == FAIL or not metrics_ok else DONE
    elif eval_start is not None or evaluate_seen:
        evaluate = RUN

    if evaluate in {RUN, DONE}:
        build = DONE
    elif build is None and build_seen:
        build = RUN

    source_done = diff_has_content(diff_path) or source_commit.is_file()
    if build in {RUN, DONE} or evaluate in {RUN, DONE}:
        source_done = True
    source = DONE if source_done else (RUN if op_state == RUN else WAIT)

    if build is None:
        build = RUN if op_state == RUN and source_done else WAIT
    if evaluate is None:
        evaluate = WAIT

    exit_status_path = artifact_dir / "evaluate_exit_status.txt"
    try:
        if int(exit_status_path.read_text(encoding="utf-8").strip()) != 0:
            evaluate = FAIL
    except (OSError, ValueError):
        pass

    if op_state == FAIL:
        if source == RUN:
            source = FAIL
        elif build == RUN:
            build = FAIL
        elif evaluate in {WAIT, RUN}:
            evaluate = FAIL
    elif op_state == DONE and metrics_payload is None:
        if evaluate == RUN:
            evaluate = FAIL
        elif build == RUN:
            build = FAIL
        elif source == RUN:
            source = FAIL

    canonical = {}
    if metrics_payload is not None and isinstance(metrics_payload.get("canonical"), dict):
        canonical = metrics_payload["canonical"]
    hpwl = as_float(canonical.get("final_hpwl_micron"))
    runtime = as_float(canonical.get("runtime_seconds"))
    legality = str(canonical.get("legality", "-")) or "-"
    delta = None
    if hpwl is not None and baseline_hpwl not in (None, 0):
        delta = 100.0 * (hpwl - float(baseline_hpwl)) / float(baseline_hpwl)

    return StudentView(
        student=student,
        iteration=iteration,
        source=source,
        build=build,
        evaluate=evaluate,
        legality=legality,
        hpwl=hpwl,
        runtime=runtime,
        delta_percent=delta,
        diff_path=diff_path if diff_has_content(diff_path) else None,
        metrics_path=metrics_path if metrics_payload is not None else None,
    )


def baseline_hpwl(round_root: Path) -> float | None:
    packets = sorted(round_root.glob("iter_*/packet/baseline_artifacts.md"))
    if not packets:
        return None
    try:
        text = packets[-1].read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None
    section = re.search(
        r"(?ms)^###\s+openroad_dpl_flow\s*$.*?(?=^###\s+|\Z)", text
    )
    if not section:
        return None
    match = re.search(r"(?m)^-\s+HPWL_final:\s+`([^`]*)`", section.group(0))
    return as_float(match.group(1)) if match else None


def latest_event_text(events: list[dict[str, Any]]) -> str:
    if not events:
        return "Waiting for the real round event stream"
    event = events[-1]
    stamp = str(event.get("time", ""))[11:19]
    stage = str(event.get("stage", "event"))
    message = str(event.get("message", ""))
    fields = event.get("fields") if isinstance(event.get("fields"), dict) else {}
    iteration = fields.get("iteration", "")
    detail = f" {iteration}" if iteration else ""
    return f"{stamp}  {stage}/{message}{detail}".strip()


def collect_view(
    *, state_root: Path, batch_root: Path, case_id: str, students: int, iterations: int
) -> DashboardView:
    round_id = discover_round_id(batch_root, case_id)
    result = batch_result(batch_root, case_id, round_id)
    if round_id is None:
        return DashboardView(
            round_id=None,
            batch_status="INITIALIZING",
            iterations=tuple(
                IterationView(
                    number=index,
                    teacher_plan=WAIT,
                    teacher_plan_activity="",
                    students=tuple(StudentView(student=i) for i in range(1, students + 1)),
                    teacher_review=WAIT,
                    teacher_review_activity="",
                )
                for index in range(1, iterations + 1)
            ),
            latest_event="Waiting for experiments.tsv",
            baseline_hpwl=None,
            final=bool(result),
            failed=result.startswith("FAIL"),
            round_dir=None,
        )

    case_root = state_root / round_id
    round_root = case_root / "teacher_rounds"
    operations_root = case_root / "checkpoints" / "operations"
    events = read_events(round_root / "events.jsonl")
    baseline = baseline_hpwl(round_root)
    iteration_views: list[IterationView] = []
    for number in range(1, iterations + 1):
        iter_name = f"iter_{number:02d}"
        plan_id = f"{round_id}_{iter_name}_teacher_plan"
        review_id = f"{round_id}_{iter_name}_teacher_review"
        plan_operation = operations_root / plan_id
        review_operation = operations_root / review_id
        iteration_views.append(
            IterationView(
                number=number,
                teacher_plan=teacher_state(
                    events=events,
                    operations_root=operations_root,
                    operation_id=plan_id,
                    iteration=iter_name,
                    kind="plan",
                ),
                teacher_plan_activity=operation_activity(plan_operation),
                students=tuple(
                    student_view(
                        round_id=round_id,
                        round_root=round_root,
                        operations_root=operations_root,
                        iteration=number,
                        student=student,
                        baseline_hpwl=baseline,
                    )
                    for student in range(1, students + 1)
                ),
                teacher_review=teacher_state(
                    events=events,
                    operations_root=operations_root,
                    operation_id=review_id,
                    iteration=iter_name,
                    kind="review",
                ),
                teacher_review_activity=operation_activity(review_operation),
            )
        )
    final = bool(result) or any(
        event.get("stage") == "round" and event.get("message") == "done" for event in events
    )
    failed = result.startswith("FAIL") or any(
        event.get("message") in {"plan_failed", "review_failed", "all_artifacts_invalid"}
        for event in events
    )
    status = result or ("FAILED" if failed else "RUNNING")
    return DashboardView(
        round_id=round_id,
        batch_status=status,
        iterations=tuple(iteration_views),
        latest_event=latest_event_text(events),
        baseline_hpwl=baseline,
        final=final,
        failed=failed,
        round_dir=round_root,
    )


class Palette:
    def __init__(self, enabled: bool) -> None:
        self.enabled = enabled

    def paint(self, value: str, code: str) -> str:
        return f"\033[{code}m{value}\033[0m" if self.enabled else value

    def state(self, value: str) -> str:
        code = {WAIT: "2;37", RUN: "1;33", DONE: "1;32", FAIL: "1;31"}.get(value, "0")
        return self.paint(f"{value:<5}", code)


def fmt_number(value: float | None, digits: int = 1, suffix: str = "") -> str:
    return "-" if value is None else f"{value:.{digits}f}{suffix}"


def clean_legal(value: str) -> str:
    return "CLEAN" if value.lower() == "clean" else value[:9]


def all_students(view: DashboardView) -> list[StudentView]:
    return [student for iteration in view.iterations for student in iteration.students]


def best_candidate(view: DashboardView) -> StudentView | None:
    candidates = [
        student
        for student in all_students(view)
        if student.hpwl is not None and student.legality.lower() == "clean"
    ]
    return min(candidates, key=lambda item: (float(item.hpwl), item.runtime or float("inf"))) if candidates else None


def final_paths(view: DashboardView, launcher_log: Path | None) -> list[str]:
    lines: list[str] = []
    best = best_candidate(view)
    if best is not None:
        lines.append("Best clean candidate")
        lines.append(
            f"  Iteration {best.iteration} / Student {best.student:02d} | "
            f"HPWL {fmt_number(best.hpwl)} um | "
            f"delta {fmt_number(best.delta_percent, 3, '%')} | runtime {fmt_number(best.runtime, 2, 's')}"
        )
        if best.diff_path:
            lines.append(f"  Source diff : {best.diff_path}")
        if best.metrics_path:
            lines.append(f"  Metrics     : {best.metrics_path}")
    if view.round_dir is not None:
        review_paths = sorted(
            (view.round_dir.parent / "checkpoints" / "operations").glob(
                "*_teacher_review/codex_last_message.txt"
            )
        )
        lines.append(f"Round state  : {view.round_dir}")
        if review_paths:
            lines.append(f"Teacher review: {review_paths[-1]}")
    if launcher_log is not None:
        lines.append(f"Launcher log : {launcher_log}")
    return lines


def render(
    *,
    view: DashboardView,
    case_id: str,
    teacher_model: str,
    student_model: str,
    elapsed_seconds: float,
    palette: Palette,
    launcher_log: Path | None = None,
) -> str:
    status_color = "1;31" if view.failed else ("1;32" if view.final else "1;33")
    lines = [
        palette.paint("DPLEvolve — Live ReviewDSE Demo", "1;36"),
        (
            f"Case {case_id} | {len(view.iterations)} iterations | "
            f"{len(view.iterations[0].students) if view.iterations else 0} Students | "
            f"elapsed {time.strftime('%H:%M:%S', time.gmtime(max(0, elapsed_seconds)))}"
        ),
        f"Teacher {teacher_model} | Students {student_model}",
        f"Run {view.round_id or '(creating batch)'} | {palette.paint(view.batch_status, status_color)}",
        "",
    ]
    for iteration in view.iterations:
        lines.append(palette.paint(f"Iteration {iteration.number}", "1;37"))
        lines.append(f"  Teacher plan    {palette.state(iteration.teacher_plan)}")
        if iteration.teacher_plan == RUN and iteration.teacher_plan_activity:
            lines.append(f"  Live activity   {iteration.teacher_plan_activity}")
        lines.append("  Student     Source Build Eval  Legal          HPWL (um)    delta    runtime")
        for student in iteration.students:
            lines.append(
                f"  Student {student.student:02d}  "
                f"{palette.state(student.source)} "
                f"{palette.state(student.build)} "
                f"{palette.state(student.evaluate)} "
                f"{clean_legal(student.legality):<10} "
                f"{fmt_number(student.hpwl):>12} "
                f"{fmt_number(student.delta_percent, 3, '%'):>9} "
                f"{fmt_number(student.runtime, 2, 's'):>10}"
            )
        lines.append(f"  Teacher review  {palette.state(iteration.teacher_review)}")
        if iteration.teacher_review == RUN and iteration.teacher_review_activity:
            lines.append(f"  Live activity   {iteration.teacher_review_activity}")
        lines.append("")

    best = best_candidate(view)
    if best is None:
        lines.append("Best clean candidate: waiting for protected OpenROAD metrics")
    else:
        lines.append(
            palette.paint("Best clean candidate: ", "1;37")
            + f"Iteration {best.iteration} / Student {best.student:02d}, "
            + f"HPWL {fmt_number(best.hpwl)} um, "
            + f"delta {fmt_number(best.delta_percent, 3, '%')}, runtime {fmt_number(best.runtime, 2, 's')}"
        )
    lines.append(f"Latest real event: {view.latest_event}")

    if view.final or view.failed:
        lines.extend(["", *final_paths(view, launcher_log)])
    return "\n".join(lines) + "\n"


def process_alive(pid: int | None) -> bool:
    if pid is None:
        return True
    try:
        stat = Path(f"/proc/{pid}/stat").read_text(encoding="utf-8")
        if stat.split()[2] == "Z":
            return False
        os.kill(pid, 0)
        return True
    except (OSError, IndexError):
        return False


def main() -> int:
    args = parse_args()
    use_color = sys.stdout.isatty() and not args.no_color and "NO_COLOR" not in os.environ
    palette = Palette(use_color)
    started = time.monotonic()
    first = True
    while True:
        view = collect_view(
            state_root=args.state_root.resolve(),
            batch_root=args.batch_root.resolve(),
            case_id=args.case,
            students=args.students,
            iterations=args.iterations,
        )
        alive = process_alive(args.launcher_pid)
        if not alive and not view.final:
            view = DashboardView(
                round_id=view.round_id,
                batch_status="LAUNCHER STOPPED",
                iterations=view.iterations,
                latest_event=view.latest_event,
                baseline_hpwl=view.baseline_hpwl,
                final=True,
                failed=True,
                round_dir=view.round_dir,
            )
        output = render(
            view=view,
            case_id=args.case,
            teacher_model=args.teacher_model,
            student_model=args.student_model,
            elapsed_seconds=time.monotonic() - started,
            palette=palette,
            launcher_log=args.launcher_log,
        )
        if sys.stdout.isatty():
            sys.stdout.write("\033[2J\033[H")
        elif not first:
            sys.stdout.write("\n")
        sys.stdout.write(output)
        sys.stdout.flush()
        first = False
        if not args.watch or view.final:
            return 0
        time.sleep(max(0.25, args.refresh_seconds))


if __name__ == "__main__":
    raise SystemExit(main())
