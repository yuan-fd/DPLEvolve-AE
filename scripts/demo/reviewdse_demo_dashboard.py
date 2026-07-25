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
import statistics
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any


WAIT = "WAIT"
RUN = "RUN"
DONE = "DONE"
FAIL = "FAIL"
SKIP = "SKIP"

_TEXT_CACHE: dict[Path, tuple[int, int, str]] = {}
_ACTIVITY_CACHE: dict[Path, tuple[int, int, str, int, str]] = {}


@dataclass(frozen=True)
class StudentView:
    student: int
    iteration: int = 0
    source: str = WAIT
    build: str = WAIT
    evaluate: str = WAIT
    legality: str = "-"
    eligible: bool | None = None
    hpwl: float | None = None
    runtime: float | None = None
    delta_percent: float | None = None
    diff_path: Path | None = None
    metrics_path: Path | None = None
    worker_state: str = WAIT
    worker_activity: str = ""
    worker_elapsed_seconds: float | None = None
    error: str = ""


@dataclass(frozen=True)
class IterationView:
    number: int
    teacher_plan: str
    teacher_plan_activity: str
    teacher_plan_elapsed_seconds: float | None
    students: tuple[StudentView, ...]
    teacher_review: str
    teacher_review_activity: str
    teacher_review_elapsed_seconds: float | None


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
    root_cause: str = ""


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


def format_duration(seconds: float | None) -> str:
    if seconds is None:
        return "-"
    total = max(0, int(seconds))
    hours, remainder = divmod(total, 3600)
    minutes, secs = divmod(remainder, 60)
    return f"{hours:02d}:{minutes:02d}:{secs:02d}"


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
        stat = path.stat()
        cached = _TEXT_CACHE.get(path)
        cache_key = (stat.st_mtime_ns, stat.st_size)
        if cached is not None and cached[:2] == cache_key:
            return cached[2]
        text = path.read_text(encoding="utf-8", errors="replace")[-2_000_000:].lower()
        _TEXT_CACHE[path] = (stat.st_mtime_ns, stat.st_size, text)
        return text
    except OSError:
        return ""


def public_error(value: Any) -> str:
    """Extract a compact public error without exposing hidden model reasoning."""
    if isinstance(value, dict):
        for key in ("detail", "message", "error"):
            if key in value:
                return public_error(value[key])
        return ""
    text = str(value or "").strip()
    for _ in range(2):
        if not (text.startswith("{") and text.endswith("}")):
            break
        try:
            decoded = json.loads(text)
        except json.JSONDecodeError:
            break
        nested = public_error(decoded)
        if not nested or nested == text:
            break
        text = nested
    return " ".join(text.split())[:1000]


def operation_error(operation_dir: Path) -> str:
    """Return the first actionable Codex error recorded for an operation."""
    for event in read_events(operation_dir / "codex_events.jsonl"):
        if event.get("type") == "error":
            message = public_error(event.get("message"))
            if message:
                return message
        if event.get("type") == "turn.failed":
            message = public_error(event.get("error"))
            if message:
                return message
    summary = load_json(operation_dir / "codex_usage_summary.json") or {}
    return public_error(summary.get("last_error_message"))


def operation_elapsed_seconds(operation_dir: Path) -> float | None:
    summary = load_json(operation_dir / "codex_usage_summary.json")
    if summary is not None:
        elapsed = as_float(summary.get("elapsed_seconds"))
        if elapsed is not None:
            return max(0.0, elapsed)
    if not operation_dir.is_dir():
        return None
    timestamps: list[float] = []
    try:
        timestamps.append(operation_dir.stat().st_mtime)
        timestamps.extend(path.stat().st_mtime for path in operation_dir.iterdir())
    except OSError:
        return None
    return max(0.0, time.time() - min(timestamps)) if timestamps else None


def operation_activity(operation_dir: Path) -> str:
    """Summarize visible Codex tool events without exposing hidden reasoning."""
    events_path = operation_dir / "codex_events.jsonl"
    try:
        stat = events_path.stat()
    except OSError:
        elapsed = operation_elapsed_seconds(operation_dir)
        if elapsed is None:
            return ""
        return f"model session starting | elapsed {format_duration(elapsed)}"
    cache_key = (stat.st_mtime_ns, stat.st_size)
    cached = _ACTIVITY_CACHE.get(events_path)
    if cached is not None and cached[:2] == cache_key:
        updated, command_count, detail = cached[2:]
    else:
        try:
            lines = events_path.read_text(encoding="utf-8", errors="replace").splitlines()
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
        updated = time.strftime("%H:%M:%S", time.localtime(stat.st_mtime))
        _ACTIVITY_CACHE[events_path] = (
            stat.st_mtime_ns,
            stat.st_size,
            updated,
            command_count,
            detail,
        )
    age = max(0, int(time.time() - stat.st_mtime))
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
    error = operation_error(operation_dir)

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
        if source in {WAIT, RUN}:
            source = FAIL
            if build == WAIT:
                build = SKIP
            if evaluate == WAIT:
                evaluate = SKIP
        elif build in {WAIT, RUN}:
            build = FAIL
            if evaluate == WAIT:
                evaluate = SKIP
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
    eligibility = metrics_payload.get("eligibility") if metrics_payload is not None else None
    eligible = (
        bool(eligibility.get("eligible"))
        if isinstance(eligibility, dict) and "eligible" in eligibility
        else None
    )
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
        eligible=eligible,
        hpwl=hpwl,
        runtime=runtime,
        delta_percent=delta,
        diff_path=diff_path if diff_has_content(diff_path) else None,
        metrics_path=metrics_path if metrics_payload is not None else None,
        worker_state=op_state,
        worker_activity=operation_activity(operation_dir),
        worker_elapsed_seconds=operation_elapsed_seconds(operation_dir),
        error=error,
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
                    teacher_plan_elapsed_seconds=None,
                    students=tuple(StudentView(student=i) for i in range(1, students + 1)),
                    teacher_review=WAIT,
                    teacher_review_activity="",
                    teacher_review_elapsed_seconds=None,
                )
                for index in range(1, iterations + 1)
            ),
            latest_event="Waiting for experiments.tsv",
            baseline_hpwl=None,
            final=bool(result),
            failed=result.startswith("FAIL"),
            round_dir=None,
            root_cause="",
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
                teacher_plan_elapsed_seconds=operation_elapsed_seconds(plan_operation),
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
                teacher_review_elapsed_seconds=operation_elapsed_seconds(review_operation),
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
    root_cause = next(
        (
            student.error
            for iteration in iteration_views
            for student in iteration.students
            if student.error
        ),
        "",
    )
    if not root_cause:
        for number in range(1, iterations + 1):
            iter_name = f"iter_{number:02d}"
            for kind in ("teacher_plan", "teacher_review"):
                root_cause = operation_error(
                    operations_root / f"{round_id}_{iter_name}_{kind}"
                )
                if root_cause:
                    break
            if root_cause:
                break
    return DashboardView(
        round_id=round_id,
        batch_status=status,
        iterations=tuple(iteration_views),
        latest_event=latest_event_text(events),
        baseline_hpwl=baseline,
        final=final,
        failed=failed,
        round_dir=round_root,
        root_cause=root_cause,
    )


class Palette:
    def __init__(self, enabled: bool) -> None:
        self.enabled = enabled

    def paint(self, value: str, code: str) -> str:
        return f"\033[{code}m{value}\033[0m" if self.enabled else value

    def state(self, value: str) -> str:
        code = {
            WAIT: "2;37",
            RUN: "1;33",
            DONE: "1;32",
            FAIL: "1;31",
            SKIP: "2;37",
        }.get(value, "0")
        return self.paint(f"{value:<5}", code)


def fmt_number(value: float | None, digits: int = 1, suffix: str = "") -> str:
    return "-" if value is None else f"{value:.{digits}f}{suffix}"


def clean_legal(value: str) -> str:
    return "CLEAN" if value.lower() == "clean" else value[:9]


def eligibility_text(value: bool | None) -> str:
    if value is True:
        return "PASS"
    if value is False:
        return "FAIL"
    return "-"


def all_students(view: DashboardView) -> list[StudentView]:
    return [student for iteration in view.iterations for student in iteration.students]


def best_candidate(view: DashboardView) -> StudentView | None:
    candidates = [
        student
        for student in all_students(view)
        if student.hpwl is not None
        and student.legality.lower() == "clean"
        and student.eligible is True
    ]
    return min(candidates, key=lambda item: (float(item.hpwl), item.runtime or float("inf"))) if candidates else None


def current_phase(view: DashboardView) -> tuple[str, str, float | None]:
    if view.failed:
        return "Run failed", view.root_cause or "inspect the launcher and operation logs", None
    if view.final:
        return "Run complete", "final results ready", None
    if view.round_id is None:
        return "Initialization", "creating the experiment batch", None

    for iteration in view.iterations:
        if iteration.teacher_plan != DONE:
            activity = iteration.teacher_plan_activity or "waiting for the Teacher model session"
            return (
                f"Iteration {iteration.number} / Teacher planning",
                activity,
                iteration.teacher_plan_elapsed_seconds,
            )

        unfinished = [
            student
            for student in iteration.students
            if student.worker_state not in {DONE, FAIL}
        ]
        if unfinished:
            source_done = sum(student.source == DONE for student in iteration.students)
            build_done = sum(student.build == DONE for student in iteration.students)
            eval_done = sum(student.evaluate == DONE for student in iteration.students)
            active = next(
                (
                    f"Student {student.student:02d}: {student.worker_activity}"
                    for student in unfinished
                    if student.worker_state == RUN and student.worker_activity
                ),
                "waiting for the parallel Student wave to start",
            )
            counts = (
                f"source {source_done}/{len(iteration.students)}, "
                f"build {build_done}/{len(iteration.students)}, "
                f"evaluate {eval_done}/{len(iteration.students)}"
            )
            elapsed_values = [
                student.worker_elapsed_seconds
                for student in iteration.students
                if student.worker_elapsed_seconds is not None
            ]
            return (
                f"Iteration {iteration.number} / parallel Students",
                f"{counts} | {active}",
                max(elapsed_values) if elapsed_values else None,
            )

        if iteration.teacher_review != DONE:
            activity = iteration.teacher_review_activity or "waiting for Teacher review"
            return (
                f"Iteration {iteration.number} / Teacher review",
                activity,
                iteration.teacher_review_elapsed_seconds,
            )
    return "Finalization", "writing the final round manifest", None


def milestone_progress(view: DashboardView) -> tuple[int, int]:
    complete = 0
    total = 0
    for iteration in view.iterations:
        total += 2 + 3 * len(iteration.students)
        complete += iteration.teacher_plan in {DONE, FAIL, SKIP}
        complete += iteration.teacher_review in {DONE, FAIL, SKIP}
        for student in iteration.students:
            complete += student.source in {DONE, FAIL, SKIP}
            complete += student.build in {DONE, FAIL, SKIP}
            complete += student.evaluate in {DONE, FAIL, SKIP}
    return int(complete), total


def progress_bar(complete: int, total: int, width: int = 28) -> str:
    filled = 0 if total <= 0 else min(width, round(width * complete / total))
    return "[" + "#" * filled + "-" * (width - filled) + "]"


def adaptive_eta(view: DashboardView) -> str:
    if view.failed:
        return "stopped"
    if view.final:
        return "complete"
    plan_samples = [
        iteration.teacher_plan_elapsed_seconds
        for iteration in view.iterations
        if iteration.teacher_plan == DONE and iteration.teacher_plan_elapsed_seconds is not None
    ]
    student_samples = [
        student.worker_elapsed_seconds
        for iteration in view.iterations
        for student in iteration.students
        if student.worker_state in {DONE, FAIL} and student.worker_elapsed_seconds is not None
    ]
    review_samples = [
        iteration.teacher_review_elapsed_seconds
        for iteration in view.iterations
        if iteration.teacher_review == DONE and iteration.teacher_review_elapsed_seconds is not None
    ]
    missing: list[str] = []
    if not plan_samples:
        missing.append("Teacher plan")
    if not student_samples:
        missing.append("Student wave")
    if not review_samples:
        missing.append("Teacher review")
    if missing:
        return "learning Iteration 1 timings; awaiting " + ", ".join(missing)

    plan_estimate = statistics.median(plan_samples)
    student_estimate = statistics.median(student_samples)
    review_estimate = statistics.median(review_samples)
    remaining = 0.0
    for iteration in view.iterations:
        if iteration.teacher_plan == WAIT:
            remaining += plan_estimate
        elif iteration.teacher_plan == RUN:
            remaining += max(
                0.0, plan_estimate - (iteration.teacher_plan_elapsed_seconds or 0.0)
            )

        unfinished = [
            student
            for student in iteration.students
            if student.worker_state not in {DONE, FAIL}
        ]
        if unfinished:
            student_remaining = []
            for student in unfinished:
                if student.worker_state == RUN:
                    student_remaining.append(
                        max(0.0, student_estimate - (student.worker_elapsed_seconds or 0.0))
                    )
                else:
                    student_remaining.append(student_estimate)
            remaining += max(student_remaining, default=0.0)

        if iteration.teacher_review == WAIT:
            remaining += review_estimate
        elif iteration.teacher_review == RUN:
            remaining += max(
                0.0,
                review_estimate - (iteration.teacher_review_elapsed_seconds or 0.0),
            )
    return f"~{format_duration(remaining)} remaining (adaptive, same-run samples)"


def final_result_visual(view: DashboardView) -> list[str]:
    candidates = [student for student in all_students(view) if student.hpwl is not None]
    clean = [student for student in candidates if student.legality.lower() == "clean"]
    eligible = [student for student in clean if student.eligible is True]
    best = best_candidate(view)
    lines = [
        "Final QoR",
        f"  Evaluated candidates : {len(candidates)}",
        f"  Legal candidates     : {len(clean)}",
        f"  Protected-gate pass  : {len(eligible)}",
    ]
    if best is None:
        lines.append("  Best candidate       : none")
        return lines
    if view.baseline_hpwl is not None:
        difference = float(best.hpwl) - view.baseline_hpwl
        direction = "better" if difference < 0 else ("worse" if difference > 0 else "equal")
        lines.extend(
            [
                f"  Default HPWL         : {view.baseline_hpwl:,.1f} um",
                "                         |",
                "                         v",
                f"  Best HPWL            : {float(best.hpwl):,.1f} um",
                f"  Difference           : {difference:+,.1f} um "
                f"({fmt_number(best.delta_percent, 3, '%')}, {direction})",
            ]
        )
    lines.extend(
        [
            f"  Winner               : Iteration {best.iteration} / Student {best.student:02d}",
            f"  Legality             : {clean_legal(best.legality)}",
            f"  Runtime              : {fmt_number(best.runtime, 2, 's')}",
        ]
    )
    if eligible:
        lines.append("  Top eligible candidates:")
        for rank, candidate in enumerate(
            sorted(eligible, key=lambda item: (float(item.hpwl), item.runtime or float("inf")))[:4],
            start=1,
        ):
            marker = "*" if rank == 1 else " "
            lines.append(
                f"    {marker}{rank}. I{candidate.iteration}/S{candidate.student:02d}  "
                f"HPWL {float(candidate.hpwl):,.1f}  "
                f"delta {fmt_number(candidate.delta_percent, 3, '%'):>9}  "
                f"runtime {fmt_number(candidate.runtime, 2, 's'):>10}"
            )
    return lines


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
    heartbeat_tick: int = 0,
    backend_alive: bool = True,
) -> str:
    status_color = "1;31" if view.failed else ("1;32" if view.final else "1;33")
    phase, doing, phase_elapsed = current_phase(view)
    complete, total = milestone_progress(view)
    spinner = "|/-\\"[heartbeat_tick % 4]
    if view.failed:
        backend = "FAILED"
    elif view.final:
        backend = "COMPLETE"
    elif not backend_alive:
        backend = "STOPPED"
    elif "active now" in doing:
        backend = "ACTIVE"
    elif "last event" in doing:
        backend = "MODEL THINKING / NO NEW TOOL EVENT"
    else:
        backend = "RUNNING"
    lines = [
        palette.paint("DPLEvolve — Live ReviewDSE Demo", "1;36"),
        (
            f"Case {case_id} | {len(view.iterations)} iterations | "
            f"{len(view.iterations[0].students) if view.iterations else 0} Students | "
            f"elapsed {time.strftime('%H:%M:%S', time.gmtime(max(0, elapsed_seconds)))}"
        ),
        f"Teacher {teacher_model} | Students {student_model}",
        f"Run {view.round_id or '(creating batch)'} | {palette.paint(view.batch_status, status_color)}",
        f"Heartbeat {spinner} {time.strftime('%H:%M:%S')} | backend {backend}",
        f"Current phase : {phase}",
        f"Doing         : {doing}",
        f"Phase elapsed : {format_duration(phase_elapsed)}",
        f"ETA           : {adaptive_eta(view)}",
        f"Progress      : {progress_bar(complete, total)} {complete}/{total} observable milestones",
        "",
    ]
    for iteration in view.iterations:
        lines.append(palette.paint(f"Iteration {iteration.number}", "1;37"))
        lines.append(f"  Teacher plan    {palette.state(iteration.teacher_plan)}")
        if iteration.teacher_plan == RUN and iteration.teacher_plan_activity:
            lines.append(f"  Live activity   {iteration.teacher_plan_activity}")
        lines.append("  Student     Source Build Eval  Legal      Gate   HPWL (um)    delta    runtime")
        for student in iteration.students:
            lines.append(
                f"  Student {student.student:02d}  "
                f"{palette.state(student.source)} "
                f"{palette.state(student.build)} "
                f"{palette.state(student.evaluate)} "
                f"{clean_legal(student.legality):<10} "
                f"{eligibility_text(student.eligible):<5} "
                f"{fmt_number(student.hpwl):>12} "
                f"{fmt_number(student.delta_percent, 3, '%'):>9} "
                f"{fmt_number(student.runtime, 2, 's'):>10}"
            )
            if student.worker_state == RUN and student.worker_activity:
                lines.append(f"              -> {student.worker_activity}")
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
        lines.extend(["", *final_result_visual(view), "", *final_paths(view, launcher_log)])
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
            heartbeat_tick=int((time.monotonic() - started) / max(0.25, args.refresh_seconds)),
            backend_alive=alive,
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
