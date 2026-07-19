"""Codex operation execution, lineage, resume, and source versioning."""
from __future__ import annotations

import json
import shlex
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

from runtime_paths import clean_subprocess_env
from scripts.teacher_loop.common import (
    ChildRound,
    assigned_start_kind_from_insight,
    iter_name,
    stable_candidate_ref,
)
from scripts.teacher_loop.evidence import load_json, seed_private_source_from_elite
from scripts.teacher_loop.prompt_rendering import (
    focused_message_excerpt,
    read_text_excerpt,
)

MAX_CHILD_TEACHER_INSIGHT_CHARS = 2800


def operation_thread_id(runtime: Any, operation_id: str) -> str | None:
    summary = load_json(runtime.operations_dir / operation_id / "codex_usage_summary.json")
    if not summary:
        return None
    thread_id = summary.get("thread_id")
    return str(thread_id) if thread_id else None


def operation_safe_to_resume(runtime: Any, operation_id: str) -> tuple[bool, str]:
    summary_path = runtime.operations_dir / operation_id / "codex_usage_summary.json"
    summary = load_json(summary_path)
    if not summary:
        return False, "missing usage summary"
    if summary.get("returncode") != 0:
        return False, f"returncode={summary.get('returncode')}"
    if not summary.get("thread_id"):
        return False, "missing thread id"
    if summary.get("local_session_recorded") is False:
        return False, "thread not recorded in Codex session store"
    if int(summary.get("agent_message_count", 0) or 0) <= 0:
        return False, "no agent messages"
    if not str(summary.get("last_agent_message") or "").strip():
        return False, "empty last agent message"
    return True, "ok"


def operation_thread_id_if_cwd_matches(
    runtime: Any, operation_id: str, expected_cwd: Path
) -> str | None:
    invocation = load_json(runtime.operations_dir / operation_id / "codex_invocation.json")
    if not invocation:
        return None
    recorded_cwd = Path(
        invocation.get("codex_cwd") or invocation.get("repo_root") or ""
    )
    if recorded_cwd != expected_cwd:
        return None
    return operation_thread_id(runtime, operation_id)


def resumable_thread_id(
    *,
    runtime: Any,
    operation_id: str,
    expected_cwd: Path,
    allow_unsafe_resume: bool,
) -> str | None:
    safe, reason = operation_safe_to_resume(runtime, operation_id)
    if not safe and not allow_unsafe_resume:
        print(
            "[optimize_case] not resuming "
            f"{operation_id}: previous operation is unsafe ({reason})",
            file=sys.stderr,
        )
        return None
    thread_id = operation_thread_id_if_cwd_matches(runtime, operation_id, expected_cwd)
    if thread_id or not allow_unsafe_resume:
        return thread_id
    return operation_thread_id(runtime, operation_id)


def build_codex_exec_command(
    *,
    run_codex: Path,
    operation_id: str,
    prompt_path: Path,
    sandbox: str,
    add_dirs: list[Path],
    model: str | None,
    reasoning_effort: str | None,
    profile: str | None,
    resume_session_id: str | None = None,
    session_identity: str | None = None,
    session_state: Path | None = None,
    session_env_file: Path | None = None,
    operations_dir: Path | None = None,
    cwd: Path | None = None,
    skip_git_repo_check: bool = False,
) -> list[str]:
    cmd = [
        sys.executable,
        str(run_codex),
        "--operation-id",
        operation_id,
        "--prompt-file",
        str(prompt_path),
        "--sandbox",
        sandbox,
    ]
    if operations_dir is not None:
        cmd.extend(["--operations-dir", str(operations_dir)])
    for add_dir in add_dirs:
        cmd.extend(["--add-dir", str(add_dir)])
    if cwd is not None:
        cmd.extend(["--cwd", str(cwd)])
    if skip_git_repo_check:
        cmd.append("--skip-git-repo-check")
    if model:
        cmd.extend(["--model", model])
    if reasoning_effort:
        cmd.extend(["--config", f'model_reasoning_effort="{reasoning_effort}"'])
    if profile:
        cmd.extend(["--profile", profile])
    if resume_session_id:
        cmd.extend(["--resume-session-id", resume_session_id])
    if session_identity:
        cmd.extend(["--session-identity", session_identity])
    if session_state is not None:
        cmd.extend(["--session-state", str(session_state)])
    if session_env_file is not None:
        cmd.extend(["--session-env-file", str(session_env_file)])
    return cmd


def append_lineage(
    *,
    round_dir: Path,
    student_id: str,
    route_label: str,
    iteration: int,
    operation_id: str,
    run_tag: str,
    prompt_path: Path,
) -> None:
    student_dir = round_dir / "students" / student_id
    student_dir.mkdir(parents=True, exist_ok=True)
    lineage_path = student_dir / "lineage.json"
    if lineage_path.exists():
        data = load_json(lineage_path) or {}
    else:
        data = {
            "student_id": student_id,
            "route_label": route_label,
            "iterations": [],
        }
    data.setdefault("iterations", []).append(
        {
            "iteration": iter_name(iteration),
            "route_label": route_label,
            "operation_id": operation_id,
            "run_tag": run_tag,
            "prompt": str(prompt_path),
        }
    )
    lineage_path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def update_lineage_iteration(
    *,
    round_dir: Path,
    student_id: str,
    iteration: int,
    updates: dict[str, Any],
) -> None:
    student_dir = round_dir / "students" / student_id
    lineage_path = student_dir / "lineage.json"
    data = load_json(lineage_path) or {}
    target = iter_name(iteration)
    for item in data.get("iterations", []):
        if item.get("iteration") == target:
            item.update(updates)
            break
    else:
        data.setdefault("iterations", []).append({"iteration": target, **updates})
    lineage_path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def run_commands(
    commands: list[list[str]],
    *,
    max_parallel: int,
    dry_run: bool,
) -> int:
    if dry_run:
        for command in commands:
            print(" ".join(shlex.quote(part) for part in command))
        return 0

    active: list[subprocess.Popen[Any]] = []
    failures = 0
    pending = list(commands)
    while pending or active:
        while pending and len(active) < max_parallel:
            command = pending.pop(0)
            print("[optimize_case] launch:", " ".join(shlex.quote(part) for part in command))
            active.append(subprocess.Popen(command, env=clean_subprocess_env()))
        next_active: list[subprocess.Popen[Any]] = []
        for proc in active:
            rc = proc.poll()
            if rc is None:
                next_active.append(proc)
            elif rc != 0:
                failures += 1
        active = next_active
        if active:
            time.sleep(2)
    return 1 if failures else 0


def _git(
    repo: Path,
    *args: str,
    check: bool = True,
    capture: bool = False,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", "-C", str(repo), *args],
        check=check,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.PIPE if capture else None,
        env=clean_subprocess_env(),
    )


def ensure_child_source_repo(source: Path) -> str | None:
    """Ensure `source` is a local git repo and return its HEAD commit."""
    if not source.exists():
        return None
    if not (source / ".git").exists():
        subprocess.run(
            ["git", "init", str(source)],
            check=True,
            text=True,
            env=clean_subprocess_env(),
        )
    top = _git(source, "rev-parse", "--show-toplevel", capture=True).stdout.strip()
    if Path(top).resolve() != source.resolve():
        raise RuntimeError(
            f"child source is not an independent git repo: {source} top={top}"
        )
    _git(source, "config", "user.email", "dpl-evolve-agent@example.invalid")
    _git(source, "config", "user.name", "dpl-evolve-agent")
    if not _git(source, "rev-parse", "--verify", "HEAD", check=False).returncode == 0:
        _git(source, "add", "-A")
        if _git(source, "diff", "--cached", "--quiet", check=False).returncode != 0:
            _git(source, "commit", "-m", "Initialize dpl_evolve workspace")
    head = _git(source, "rev-parse", "HEAD", capture=True)
    return head.stdout.strip()


def current_branch(source: Path) -> str:
    branch = _git(source, "branch", "--show-current", capture=True, check=False)
    return branch.stdout.strip() if branch.returncode == 0 else ""


def source_trial_summary(trial_log: Path) -> dict[str, Any]:
    """Summarize generated trial helper events without interpreting metrics."""
    events: list[dict[str, Any]] = []
    kept_refs: list[str] = []
    rejected_refs: list[str] = []
    reset_refs: list[str] = []
    final_status = "unmarked"
    if trial_log.exists():
        with trial_log.open(encoding="utf-8", errors="replace") as fh:
            for raw_line in fh:
                line = raw_line.strip()
                if not line:
                    continue
                try:
                    event = json.loads(line)
                except json.JSONDecodeError:
                    continue
                events.append(event)
                action = str(event.get("action") or "").strip()
                ref = str(event.get("ref") or event.get("candidate_ref") or "").strip()
                if action == "keep" and ref:
                    kept_refs.append(ref)
                elif action == "reject" and ref:
                    rejected_refs.append(ref)
                elif action == "reset_dev" and ref:
                    reset_refs.append(ref)
                if action in {"keep", "reject"}:
                    final_status = action
    return {
        "source_trials": str(trial_log),
        "trial_event_count": len(events),
        "trial_actions": [
            str(event.get("action") or "").strip()
            for event in events
            if str(event.get("action") or "").strip()
        ],
        "kept_refs": kept_refs,
        "rejected_refs": rejected_refs,
        "reset_refs": reset_refs,
        "final_status": final_status,
    }


def commit_child_sources(
    *,
    child_rounds: list[ChildRound],
    round_dir: Path,
    iteration: int,
) -> None:
    """Commit each child's current source after its iteration.

    The stable workspace is the durable source repo.  Per-iteration lineage
    stores the source commit, replacing the old copied-source tree.
    """
    for child in child_rounds:
        if not child.dpl_src.exists():
            print(
                "[optimize_case] warning: cannot commit missing child source "
                f"for {child.student_id}: {child.dpl_src}",
                file=sys.stderr,
            )
            continue
        ensure_child_source_repo(child.dpl_src)
        _git(child.dpl_src, "add", "-A")
        changed = _git(child.dpl_src, "diff", "--cached", "--quiet", check=False).returncode != 0
        if changed:
            _git(
                child.dpl_src,
                "commit",
                "-m",
                f"{child.student_id} {child.run_tag} source",
            )
            commit_source = "orchestrator_fallback"
        else:
            commit_source = "student_or_no_change"
        commit = _git(child.dpl_src, "rev-parse", "HEAD", capture=True).stdout.strip()
        status = _git(child.dpl_src, "status", "--short", capture=True).stdout
        dirty = bool(status.strip())
        base_commit = (
            child.source_base_record.read_text(encoding="utf-8").strip()
            if child.source_base_record.exists()
            else ""
        )
        if base_commit:
            diff = _git(
                child.dpl_src,
                "diff",
                "--no-ext-diff",
                f"{base_commit}..{commit}",
                capture=True,
                check=False,
            )
            child.implementation_diff.parent.mkdir(parents=True, exist_ok=True)
            child.implementation_diff.write_text(diff.stdout, encoding="utf-8")
        candidate_ref = stable_candidate_ref(child.run_tag)
        _git(child.dpl_src, "check-ref-format", "--branch", candidate_ref)
        _git(child.dpl_src, "branch", "-f", candidate_ref, commit)
        trial_log = child.implementation_diff.parent / "source_trials.jsonl"
        existing_record = load_json(child.source_commit_record) or {}
        trial_summary = source_trial_summary(trial_log)
        record = {
            **existing_record,
            "student_id": child.student_id,
            "route_label": child.route_label,
            "operation_id": child.operation_id,
            "run_tag": child.run_tag,
            "source_repo": str(child.dpl_src),
            "source_branch": current_branch(child.dpl_src),
            "source_candidate_ref": candidate_ref,
            "source_base_commit": base_commit or None,
            "source_commit": commit,
            "fallback_commit_created": changed,
            "commit_source": commit_source,
            "dirty_after_commit": dirty,
            **trial_summary,
        }
        child.source_commit_record.parent.mkdir(parents=True, exist_ok=True)
        child.source_commit_record.write_text(
            json.dumps(record, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        update_lineage_iteration(
            round_dir=round_dir,
            student_id=child.student_id,
            iteration=iteration,
            updates={
                "source_repo": str(child.dpl_src),
                "source_branch": current_branch(child.dpl_src),
                "source_candidate_ref": candidate_ref,
                "source_commit": commit,
                "source_commit_record": str(child.source_commit_record),
                "fallback_commit_created": changed,
                "source_dirty_after_commit": dirty,
                "source_final_status": trial_summary["final_status"],
                "source_kept_refs": trial_summary["kept_refs"],
                "source_rejected_refs": trial_summary["rejected_refs"],
            },
        )
        print(
            "[optimize_case] committed "
            f"{child.student_id} source: {commit} ref={candidate_ref} "
            f"repo={child.dpl_src}"
        )


def teacher_insight_for_student(
    text: str,
    student_id: str,
    fallback_max_chars: int = 1800,
) -> str:
    """Return the global diagnosis plus one `### student_XX` section."""
    section_markers = ("## Insight Packets", "## Next Insight Packets")
    global_part = text
    for marker in section_markers:
        if marker in text:
            global_part = text.split(marker, 1)[0]
            break
    global_lines = [line.strip() for line in global_part.splitlines() if line.strip()]
    global_keep_prefixes = (
        "Design read:",
        "Global diagnosis:",
        "Runtime/search stance:",
        "Mechanism knowledge used:",
        "Case-type prior:",
        "Complete-chain audit:",
        "Failure-mode target:",
        "HPWL source target:",
        "Stacking decision:",
        "Implementation anchors:",
    )
    global_kept: list[str] = []
    for line in global_lines:
        if any(line.startswith(prefix) for prefix in global_keep_prefixes):
            global_kept.append(line)
    global_excerpt = "\n".join(global_kept[:10] or global_lines[:4]).strip()

    def zero_based_alias(student: str) -> str | None:
        prefix = "student_"
        if not student.startswith(prefix):
            return None
        try:
            number = int(student[len(prefix) :])
        except ValueError:
            return None
        if number <= 0:
            return None
        return f"{prefix}{number - 1:02d}"

    def find_section_start(header: str) -> int | None:
        needle = f"### {header}".lower()
        for idx, line in enumerate(lines):
            if line.strip().lower().startswith(needle):
                return idx
        return None

    lines = text.splitlines()
    has_zero_based_roster = find_section_start("student_00") is not None
    alias = zero_based_alias(student_id)
    start = None
    matched_header = student_id
    if has_zero_based_roster and alias is not None:
        start = find_section_start(alias)
        matched_header = alias if start is not None else student_id
    if start is None:
        start = find_section_start(student_id)
        matched_header = student_id
    if start is None and alias is not None:
        start = find_section_start(alias)
        matched_header = alias if start is not None else student_id
    if start is None:
        fallback = focused_message_excerpt(text, max_chars=fallback_max_chars)
        return fallback

    end = len(lines)
    for idx in range(start + 1, len(lines)):
        stripped = lines[idx].strip()
        if stripped.startswith("### ") and stripped.lower().startswith("### student_"):
            end = idx
            break
    student_lines = lines[start:end]
    if matched_header != student_id and student_lines:
        student_lines = [f"### {student_id}"] + student_lines[1:]
    student_section = "\n".join(student_lines).strip()
    parts = []
    if global_excerpt:
        parts.extend([global_excerpt, ""])
    parts.append(student_section)
    return compact_teacher_child_insight(
        "\n".join(parts).strip(), max_chars=MAX_CHILD_TEACHER_INSIGHT_CHARS
    )


def compact_teacher_child_insight(text: str, *, max_chars: int) -> str:
    """Keep a child's assigned Teacher plan useful while bounding prompt size."""
    if len(text) <= max_chars:
        return text

    keep_prefixes = (
        "Design read:",
        "Global diagnosis:",
        "Runtime/search stance:",
        "### student_",
        "- route:",
        "- start/parent:",
        "- donor scope:",
        "- HPWL source:",
        "- expected HPWL source:",
        "- mechanism strength:",
        "- stage-wise proof target:",
        "- measured HPWL-source attribution:",
        "- first-result diagnosis:",
        "HPWL source target:",
        "- HPWL-source diagnosis:",
        "- first patch handles:",
        "- support handles:",
        "- mechanism plan:",
        "- skills/cards:",
        "- expected proof:",
        "- runtime/complexity:",
        "- stop/pivot:",
        "- route type:",
        "- route action:",
        "- start/donor family:",
        "- continuation parent:",
        "- HPWL source mechanism:",
        "- why this can beat a small donor:",
        "- evidence:",
        "- active workbench repair, if any:",
        "- low-ROI stop rule, if any:",
        "- step-by-step execution chain:",
        "- plan chain:",
        "- plan A legalization/detailed placement:",
        "- plan B improve placement:",
        "- plan C co-optimization/handoff:",
        "A legalization:",
        "B improve placement:",
        "C optional coordination/selection:",
        "- pivot condition:",
        "- function/state insertion points:",
        "- skills to use:",
        "- efficiency/runtime-control:",
        "- efficiency/bounded-search:",
        "- runtime tier:",
        "- runtime-budget use:",
        "- low-ROI cutoff:",
        "- record:",
    )
    kept: list[str] = []
    for raw in text.splitlines():
        line = " ".join(raw.strip().split())
        if not line:
            continue
        if any(line.startswith(prefix) for prefix in keep_prefixes):
            if line.startswith("- function/state insertion points:") and len(line) > 520:
                line = line[:517].rstrip(" ,") + "..."
            elif len(line) > 700:
                line = line[:697].rstrip(" ,") + "..."
            kept.append(line)

    if not kept:
        return focused_message_excerpt(text, max_chars=max_chars)

    compact = "\n".join(kept)
    if len(compact) <= max_chars:
        return compact

    budgeted: list[str] = []
    used = 0
    for line in kept:
        if used + len(line) + 1 > max_chars:
            break
        budgeted.append(line)
        used += len(line) + 1
    return "\n".join(budgeted).strip()


def inline_teacher_plan_for_children(
    *, child_rounds: list[ChildRound], teacher_last_message: Path
) -> None:
    """Append each child's assigned Teacher insight after Teacher finishes.

    Child prompt files are created before Teacher runs so commands can be
    assembled, but child workers are launched after Teacher.  Append only the
    relevant `### student_XX` section so children do not waste context on peer
    instructions they are not executing.
    """
    if not teacher_last_message.exists():
        teacher_text = ""
    else:
        teacher_text = teacher_last_message.read_text(
            encoding="utf-8", errors="replace"
        ).strip()
    begin_marker = "<!-- TEACHER_ASSIGNMENT_BEGIN -->"
    end_marker = "<!-- TEACHER_ASSIGNMENT_END -->"
    marker = "## Current Teacher Insight Packet"
    for child in child_rounds:
        if not child.prompt_path.exists():
            continue
        assigned = teacher_insight_for_student(teacher_text, child.student_id)
        assigned_start_kind = assigned_start_kind_from_insight(assigned)
        start_action = (
            "After opening the workspace packet, run `prepare_source_script`. Do "
            "not run `switch_start_branch_script` unless Teacher's primary route "
            "explicitly says to use a prepared start branch. If Teacher only "
            "assigns continuation from an elite/final-donor source, keep the "
            "pre-seeded/materialized elite source. Then inspect source and refine "
            "the mechanism before coding."
        )
        if assigned_start_kind:
            start_action = (
                "After opening the workspace packet, run "
                f"`prepare_start_source_script --kind {assigned_start_kind}` before "
                "editing source. Do not launch prepare and switch helpers as "
                "parallel commands. Then inspect source and refine the mechanism "
                "before coding."
            )
        section = (
            f"{begin_marker}\n"
            f"{marker}\n\n"
            "This is your authoritative Teacher assignment and primary "
            "research hypothesis for this iteration. Use it before the "
            "workspace packet's round-default `start_kind`, then combine it "
            "with focused source inspection and metrics/log evidence rather "
            "than blindly executing Teacher prose.\n\n"
            f"Initial workspace setup: {start_action}\n\n"
            f"{assigned}\n"
            f"{end_marker}\n\n"
        )
        prompt_text = child.prompt_path.read_text(encoding="utf-8", errors="replace")
        if begin_marker in prompt_text and end_marker in prompt_text:
            before, remainder = prompt_text.split(begin_marker, 1)
            _, after = remainder.split(end_marker, 1)
            prompt_text = (before + after.lstrip()).lstrip()
        child.prompt_path.write_text(section + prompt_text, encoding="utf-8")


def seed_elite_children(child_rounds: list[ChildRound]) -> None:
    for child in child_rounds:
        if child.elite_seed_source is None:
            continue
        seed_info = seed_private_source_from_elite(
            seed_source=child.elite_seed_source,
            target_source=child.dpl_src,
        )
        print(
            "[optimize_case] seeded "
            f"{child.student_id} from elite: {child.elite_seed_source} "
            f"mode={seed_info.get('mode')} "
            f"backup_ref={seed_info.get('backup_ref') or '<none>'} "
            f"seed_commit={seed_info.get('seed_commit') or '<none>'}"
        )
