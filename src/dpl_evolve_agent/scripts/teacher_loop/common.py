"""Shared data structures and small helpers for Teacher/Student rounds."""
from __future__ import annotations

import datetime as dt
import json
import re
from dataclasses import dataclass
from pathlib import Path
from string import Template
from typing import Any

from scripts.teacher_loop.constants import (
    CANONICAL_LINES,
    PREPARED_START_KIND_PATTERN,
    START_KINDS,
)

AGENT_ROOT = Path(__file__).resolve().parents[2]
PROMPT_TEMPLATE_DIR = AGENT_ROOT / "prompt_templates" / "teacher_loop"

@dataclass(frozen=True)
class MetricSummary:
    tag: str
    metrics_path: Path
    mode: str
    line: str
    run_tag: str
    hpwl_after: float | None
    hpwl_delta: float | None
    runtime_seconds: float | None
    avg_disp: float | None
    max_disp: float | None
    violations: str
    hpwl_delta_percent: float | None = None
    hpwl_global: float | None = None
    hpwl_legalized: float | None = None
    hpwl_after_improve: float | None = None
    hpwl_stage_delta_legalization: float | None = None
    hpwl_stage_delta_improve: float | None = None
    hpwl_stage_delta_final: float | None = None
    hpwl_stage_delta_legalization_percent: float | None = None
    hpwl_stage_delta_improve_percent: float | None = None
    hpwl_stage_delta_final_percent: float | None = None


@dataclass(frozen=True)
class CandidateArtifacts:
    iter_part: str
    iter_number: int | None
    student_id: str
    route_label: str
    operation_id: str
    operation_dir: Path
    last_message: Path
    source_repo: Path
    source_ref: str | None
    implementation_diff: Path
    knowledge_card: Path


@dataclass(frozen=True)
class ChildRound:
    student_id: str
    route_label: str
    operation_id: str
    run_tag: str
    prompt_path: Path
    workspace_packet: Path
    operation_dir: Path
    last_message: Path
    usage_summary: Path
    stderr_log: Path
    events_jsonl: Path
    student_workspace: Path
    variant_root: Path
    dpl_src: Path
    private_binary: Path
    implementation_diff: Path
    knowledge_card: Path
    source_branch: str
    source_base_record: Path
    source_commit_record: Path
    session_state: Path
    session_env_file: Path
    elite_seed_source: Path | None = None


@dataclass(frozen=True)
class StudentWorkspace:
    student_workspace: Path
    variant_root: Path
    dpl_src: Path
    variant_env: Path
    private_binary: Path
    artifact_dir: Path
    candidate_metrics_summary_json: Path
    candidate_metrics_summary_md: Path
    implementation_diff: Path
    knowledge_card: Path
    source_branch: str
    source_base_record: Path
    source_commit_record: Path
    session_state: Path
    session_env_file: Path


@dataclass(frozen=True)
class WorkerSession:
    identity: str
    workspace: Path
    session_state: Path
    session_env_file: Path


def render_prompt_template(name: str, **values: Any) -> str:
    path = PROMPT_TEMPLATE_DIR / name
    text = path.read_text(encoding="utf-8")
    mapping = {key: str(value) for key, value in values.items()}
    return Template(text).safe_substitute(mapping)


def default_round_id(case_id: str) -> str:
    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    return f"teacher_{case_id}_{stamp}"


def iter_name(iteration: int) -> str:
    return f"iter_{iteration:02d}"


def stable_candidate_ref(run_tag: str) -> str:
    """Return a long-lived local git branch name for one evaluated candidate."""
    safe = re.sub(r"[^A-Za-z0-9._/-]+", "_", str(run_tag)).strip("./")
    safe = re.sub(r"/+", "/", safe)
    if not safe:
        safe = "candidate"
    return f"candidate/{safe}"


def round_id_from_dir(round_dir: Path) -> str:
    manifest = round_dir / "manifest.json"
    if manifest.is_file():
        try:
            payload = json.loads(manifest.read_text(encoding="utf-8"))
            value = payload.get("round_id")
            if isinstance(value, str) and value:
                return value
        except Exception:
            pass
    if round_dir.name == "teacher_rounds" and round_dir.parent.name:
        return round_dir.parent.name
    return round_dir.name


def assigned_start_kind_from_insight(text: str) -> str | None:
    """Extract the primary prepared start-branch kind from Teacher text.

    Only explicit primary assignments to active prepared starts should become
    unconditional Student switches.
    """
    fallback_markers = (
        "if ",
        "fallback",
        "clean reset",
        "reset is needed",
        "needed, use",
        "otherwise",
        "unless",
    )
    field_prefixes = (
        "route action:",
        "start/parent:",
        "start/donor family:",
        "continuation parent:",
        "runtime tier:",
    )
    primary_patterns = (
        rf"\buse the prepared start branch\s*`?({PREPARED_START_KIND_PATTERN})`?",
        rf"\bstart from\s*`?({PREPARED_START_KIND_PATTERN})`?",
        rf"\buse\s*`?({PREPARED_START_KIND_PATTERN})`?\s+as (?:the )?(?:prepared )?start",
    )

    for raw_line in text.splitlines():
        line = " ".join(raw_line.strip().split())
        if not line:
            continue
        line = re.sub(r"^\s*[-*]\s+", "", line)
        line_lower = line.lower()
        if not any(line_lower.startswith(prefix) for prefix in field_prefixes):
            continue
        if any(marker in line_lower for marker in fallback_markers):
            continue
        if line_lower.startswith(("start/parent:", "start/donor family:", "continuation parent:")):
            kinds = re.findall(
                rf"`({PREPARED_START_KIND_PATTERN})`|\b({PREPARED_START_KIND_PATTERN})\b",
                line,
                flags=re.IGNORECASE,
            )
            flattened = [match[0] or match[1] for match in kinds]
            unique = []
            for kind in flattened:
                if kind not in unique:
                    unique.append(kind)
            if len(unique) == 1:
                return unique[0]
            if len(unique) > 1:
                continue
        for pattern in primary_patterns:
            match = re.search(pattern, line, flags=re.IGNORECASE)
            if match:
                return match.group(1)
    return None


def student_workspace_paths(
    *,
    round_dir: Path,
    iteration: int,
    student_id: str,
    stable_workspace: bool = False,
) -> StudentWorkspace:
    student_root = round_dir / "students" / student_id
    if stable_workspace:
        student_workspace = student_root / "workspace"
        artifact_dir = student_root / iter_name(iteration) / "artifacts"
    else:
        student_workspace = student_root / iter_name(iteration)
        artifact_dir = student_workspace / "artifacts"
    variant_root = student_workspace / "variant"
    dpl_src = variant_root / "dpl_evolve"
    return StudentWorkspace(
        student_workspace=student_workspace,
        variant_root=variant_root,
        dpl_src=dpl_src,
        variant_env=variant_root / "variant_env.sh",
        private_binary=variant_root / "install" / "OpenROAD" / "bin" / "openroad",
        artifact_dir=artifact_dir,
        candidate_metrics_summary_json=artifact_dir / "candidate_metrics_summary.json",
        candidate_metrics_summary_md=artifact_dir / "candidate_metrics_summary.md",
        implementation_diff=artifact_dir / "implementation.diff",
        knowledge_card=artifact_dir / "knowledge_card.md",
        source_branch=f"dev/{student_id}",
        source_base_record=artifact_dir / "source_base_commit.txt",
        source_commit_record=artifact_dir / "source_commit.json",
        session_state=student_workspace / ".codex_session_state.json",
        session_env_file=student_workspace / ".codex_identity.env",
    )


def teacher_session_paths(*, round_dir: Path, round_id: str) -> WorkerSession:
    workspace = round_dir / "teacher_workspace"
    return WorkerSession(
        identity=f"{round_id}:teacher",
        workspace=workspace,
        session_state=workspace / ".codex_session_state.json",
        session_env_file=workspace / ".codex_identity.env",
    )


def json_safe(value: Any) -> Any:
    if isinstance(value, Path):
        return str(value)
    if isinstance(value, dict):
        return {str(key): json_safe(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [json_safe(item) for item in value]
    return value


class RoundLogger:
    def __init__(self, round_dir: Path) -> None:
        self.round_dir = round_dir
        self.round_dir.mkdir(parents=True, exist_ok=True)
        self.log_path = round_dir / "round.log"
        self.events_path = round_dir / "events.jsonl"

    def event(self, stage: str, message: str, **fields: Any) -> None:
        timestamp = dt.datetime.now().isoformat(timespec="seconds")
        field_text = " ".join(
            f"{key}={json_safe(value)}" for key, value in fields.items()
        )
        line = f"[{timestamp}] [{stage}] {message}"
        if field_text:
            line = f"{line} {field_text}"
        print(
            f"[optimize_case] {stage}: {message}"
            + (f" {field_text}" if field_text else "")
        )
        with self.log_path.open("a", encoding="utf-8") as fh:
            fh.write(line + "\n")
        with self.events_path.open("a", encoding="utf-8") as fh:
            fh.write(
                json.dumps(
                    {
                        "time": timestamp,
                        "stage": stage,
                        "message": message,
                        "fields": json_safe(fields),
                    },
                    sort_keys=True,
                )
                + "\n"
            )
