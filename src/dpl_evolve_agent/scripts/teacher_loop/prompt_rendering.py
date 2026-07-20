"""Prompt rendering and prompt-audit API for Teacher/Student rounds.

This module owns prompt-safe text excerpting, prompt audit behavior, and
Teacher/Student prompt construction.
"""
from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any

from scripts.teacher_loop.common import (
    assigned_start_kind_from_insight,
    iter_name,
    render_prompt_template,
)


NOISY_CONTEXT_MARKERS = (
    "[" + "trun" + "cated]",
    "[" + "excerpt " + "trun" + "cated]",
    "<" + "br>",
)

INFRASTRUCTURE_REVIEW_MARKERS = (
    "bwrap: Unknown option --perms",
    "could not read `teacher_review_artifacts.md`",
    "could not read teacher_review_artifacts.md",
    "Blocked before review",
    "Blocked: the local command tool is failing",
    "every shell command is failing",
)


def read_text_excerpt(path: Path, *, max_chars: int = 6000) -> str:
    if not path.exists():
        return render_prompt_template("context/missing_text_source.md", path=path).strip()
    text = path.read_text(encoding="utf-8", errors="replace").strip()
    if not text:
        return render_prompt_template("context/empty_text_source.md", path=path).strip()
    if len(text) <= max_chars:
        return text
    return render_prompt_template("context/long_content_pointer.md", path=path).strip()


def previous_teacher_review_text(
    *, runtime: Any, round_id: str, iteration: int, warn_chars: int = 12000
) -> str:
    """Return the complete previous Teacher review.

    Review evidence must not be summarized or capped before another agent sees
    it. Long reviews get a warning header, but the full text remains available
    in the generated context.
    """
    if iteration <= 1:
        return render_prompt_template("context/no_previous_teacher_review.md").strip()
    review_op = f"{round_id}_{iter_name(iteration - 1)}_teacher_review"
    review_path = runtime.operations_dir / review_op / "codex_last_message.txt"
    if not review_path.exists():
        return render_prompt_template(
            "context/missing_previous_teacher_review.md",
            review_path=review_path,
        ).strip()
    text = review_path.read_text(encoding="utf-8", errors="replace").strip()
    if not text:
        return render_prompt_template(
            "context/empty_previous_teacher_review.md",
            review_path=review_path,
        ).strip()
    if any(marker in text for marker in INFRASTRUCTURE_REVIEW_MARKERS):
        return render_prompt_template("context/previous_review_infra_omitted.md").strip()
    if len(text) <= warn_chars:
        return text
    return (
        render_prompt_template(
            "context/long_previous_teacher_review_warning.md",
            review_path=review_path,
            review_chars=len(text),
            warn_chars=warn_chars,
        ).strip()
        + "\n\n"
        + text
    )


def compact_inline_text(text: str, *, max_chars: int = 220) -> str:
    """Return one prompt-safe line without noisy truncation markers."""
    cleaned = text.replace("|", "/")
    for marker in NOISY_CONTEXT_MARKERS:
        cleaned = cleaned.replace(marker, " ")
    cleaned = " ".join(cleaned.split())
    if len(cleaned) <= max_chars:
        return cleaned
    clipped = cleaned[: max_chars - 3].rstrip()
    for sep in (". ", "; ", ", ", " "):
        idx = clipped.rfind(sep)
        if idx >= max_chars // 2:
            clipped = clipped[: idx + (1 if sep == " " else 1)].rstrip()
            break
    return clipped.rstrip(".,;:") + "..."


def format_multiplier(value: float) -> str:
    if float(value).is_integer():
        return str(int(value))
    return f"{value:g}"


def markdown_section_excerpt(text: str, heading: str, *, max_chars: int = 220) -> str:
    """Extract a short section body from a student knowledge card."""
    wanted = heading.strip().lower()
    in_section = False
    lines: list[str] = []
    for raw in text.splitlines():
        line = raw.strip()
        if line.startswith("#"):
            title = line.lstrip("#").strip().lower().rstrip(":")
            if in_section and title != wanted:
                break
            in_section = title == wanted
            continue
        if in_section and line:
            lines.append(line.lstrip("- ").strip())
        if in_section and len(" ".join(lines)) >= max_chars:
            break
    return compact_inline_text(" ".join(lines), max_chars=max_chars) if lines else ""


def labeled_line_excerpt(
    text: str, labels: tuple[str, ...], *, max_chars: int = 220
) -> str:
    """Extract a compact labeled line from a student final message."""
    normalized_labels = tuple(label.lower().rstrip(":") for label in labels)
    for raw in text.splitlines():
        line = raw.strip().lstrip("- ").strip()
        if not line:
            continue
        low = line.lower()
        for label in normalized_labels:
            if low.startswith(label + ":"):
                return compact_inline_text(
                    line.split(":", 1)[1].strip(), max_chars=max_chars
                )
    for raw in text.splitlines():
        line = raw.strip()
        if line:
            return compact_inline_text(line, max_chars=max_chars)
    return "No concise report found."


def focused_message_excerpt(text: str, *, max_chars: int = 900) -> str:
    """Compatibility helper for execution.py with compact, non-redundant output."""
    per_field = max(160, max_chars // 4)
    fields = [
        (
            "mechanism",
            labeled_line_excerpt(
                text,
                (
                    "Mechanism",
                    "core idea",
                    "Implemented",
                    "Touched files",
                    "Route And Hypothesis",
                    "Source Map",
                ),
                max_chars=per_field,
            ),
        ),
        (
            "result",
            labeled_line_excerpt(
                text,
                ("Result", "Final canonical metrics", "metrics"),
                max_chars=per_field,
            ),
        ),
        (
            "next",
            labeled_line_excerpt(
                text,
                (
                    "Next Teacher Handoff",
                    "Next Repair Or Pivot",
                    "Next experiment",
                    "Next uncertainty",
                    "Next step",
                ),
                max_chars=per_field,
            ),
        ),
    ]
    useful = [
        f"{label}: {value}"
        for label, value in fields
        if value and value != "No concise report found."
    ]
    if useful:
        return "\n".join(useful)
    return compact_inline_text(text, max_chars=max_chars)


PROMPT_FORBIDDEN_TERMS = (
    "Required Commands",
    "```bash",
    "Dispatch",
    "dispatch packet",
    "global synthesis",
    "execute only",
    *NOISY_CONTEXT_MARKERS,
    "reported mechanism / next clue",
    "one coherent algorithmic change",
)


ABSOLUTE_PATH_RE = re.compile(
    r"(?<![\w$])/(?:[^\s`'\"<>|]+/)+[^\s`'\"<>|]+"
)


def absolute_path_count(text: str) -> int:
    """Return a best-effort count of literal absolute paths in prompt text."""
    return len(ABSOLUTE_PATH_RE.findall(text))


def agent_prompt_warnings(
    text: str, *, max_agent_prompt_bytes: int = 12000
) -> list[str]:
    """Return warnings for text that will be sent directly to an agent."""
    warnings: list[str] = []
    if len(text.encode("utf-8")) > max_agent_prompt_bytes:
        warnings.append(f"agent prompt exceeds {max_agent_prompt_bytes} bytes")
    for term in PROMPT_FORBIDDEN_TERMS:
        if term in text:
            warnings.append(f"agent prompt contains `{term}`")
    if absolute_path_count(text) > 35:
        warnings.append("agent prompt has many absolute paths")
    seen: dict[str, int] = {}
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("|") or len(line) < 24:
            continue
        seen[line] = seen.get(line, 0) + 1
    repeated_lines = [line for line, count in seen.items() if count > 2]
    if repeated_lines:
        warnings.append(
            "agent prompt repeats lines: " + "; ".join(repeated_lines[:3])
        )
    assignment_start = assigned_start_kind_from_insight(text)
    immediate_start = None
    for raw_line in text.splitlines():
        if raw_line.startswith("Initial workspace setup:"):
            immediate_match = re.search(
                r"(?:prepare_start_source_script|switch_start_branch_script) --kind\s+([A-Za-z0-9_]+)",
                raw_line,
            )
            immediate_start = immediate_match.group(1) if immediate_match else None
            break
    if assignment_start and immediate_start != assignment_start:
        warnings.append(
            "Teacher assigned start branch "
            f"`{assignment_start}` but immediate action uses `{immediate_start or 'none'}`"
        )
    if immediate_start and not assignment_start:
        warnings.append(
            "immediate action switches start branch but Teacher assignment has no primary prepared start"
        )
    return warnings


def prompt_audit(
    *, round_dir: Path, max_agent_prompt_bytes: int = 24000
) -> tuple[Path, Path, int]:
    """Audit generated prompts without launching agents.

    Machine packets are allowed to contain commands and long paths. Agent
    prompts should stay compact and should not contain generated shell blocks.
    """
    rows: list[dict[str, Any]] = []
    warnings = 0
    files = (
        sorted(round_dir.glob("iter_*/prompts/*.md"))
        + sorted(round_dir.glob("iter_*/packet/*.md"))
        + sorted(round_dir.glob("iter_*/context/*.md"))
    )
    for path in files:
        rel = path.relative_to(round_dir)
        text = path.read_text(encoding="utf-8", errors="replace")
        rel_posix = f"/{rel.as_posix()}"
        is_agent_prompt = "/prompts/" in rel_posix
        is_context = "/context/" in rel_posix
        file_warnings: list[str] = []
        if is_agent_prompt:
            file_warnings.extend(
                agent_prompt_warnings(
                    text, max_agent_prompt_bytes=max_agent_prompt_bytes
                )
            )
        elif is_context and len(text.encode("utf-8")) > 30000:
            file_warnings.append(
                "generated context is large; evidence is retained uncapped"
            )
        warnings += len(file_warnings)
        rows.append(
            {
                "file": str(rel),
                "kind": (
                    "agent_prompt"
                    if is_agent_prompt
                    else "generated_context"
                    if is_context
                    else "generated_packet"
                ),
                "bytes": len(text.encode("utf-8")),
                "lines": text.count("\n") + 1,
                "absolute_path_count": absolute_path_count(text),
                "bash_blocks": text.count("```bash"),
                "warnings": file_warnings,
            }
        )

    audit_json = round_dir / "prompt_audit.json"
    audit_md = round_dir / "PROMPT_AUDIT.md"
    audit_json.write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
    lines = [
        "# Prompt Audit",
        "",
        f"- files: `{len(rows)}`",
        f"- warnings: `{warnings}`",
        "",
        "| file | kind | bytes | lines | paths | bash | warnings |",
        "|---|---|---:|---:|---:|---:|---|",
    ]
    for row in rows:
        warning_text = "; ".join(row["warnings"]) if row["warnings"] else "ok"
        lines.append(
            "| {file} | {kind} | {bytes} | {lines} | {paths} | {bash} | {warnings} |".format(
                file=row["file"],
                kind=row["kind"],
                bytes=row["bytes"],
                lines=row["lines"],
                paths=row["absolute_path_count"],
                bash=row["bash_blocks"],
                warnings=warning_text.replace("|", "\\|"),
            )
        )
    audit_md.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return audit_json, audit_md, warnings


def teacher_prompt(
    *,
    context_path: Path,
    current_run_packet_path: Path,
    design_characteristics_path: Path,
    baseline_artifacts_path: Path,
    peer_learning_path: Path,
    start_seed_calibration_path: Path,
    manual_teacher_guidance_path: Path | None = None,
    strategies: list[str],
    children: int,
    iteration: int,
    student_runtime_multiplier: float,
    calibration_mode: bool = False,
) -> str:
    student_roster = "\n".join(
        render_prompt_template(
            "teacher/student_roster_line.md",
            student_id=f"student_{idx:02d}",
        ).rstrip()
        for idx in range(1, children + 1)
    )
    route_lines = "\n".join(
        render_prompt_template(
            "teacher/student_slot_line.md",
            student_id=f"student_{idx:02d}",
        ).rstrip()
        for idx, _name in enumerate(strategies, start=1)
    )
    teacher_rules = render_prompt_template(
        "teacher/rules.md",
        student_runtime_multiplier=format_multiplier(student_runtime_multiplier),
    )
    teacher_plan_output = render_prompt_template(
        "teacher/plan_output.md",
        student_insight_packet_format=render_prompt_template(
            "shared/student_insight_packet_format.md"
        ),
    )
    if calibration_mode:
        teacher_rules = (
            teacher_rules.rstrip()
            + "\n\n"
            + render_prompt_template(
                "teacher/calibration_rules.md",
                student_runtime_multiplier=format_multiplier(student_runtime_multiplier),
                children=children,
            ).strip()
        )
        teacher_plan_output = render_prompt_template(
            "teacher/calibration_plan_output.md",
            student_insight_packet_format=render_prompt_template(
                "shared/student_insight_packet_format.md"
            ),
            children=children,
        )
    return render_prompt_template(
        "teacher/plan.md",
        context_path=context_path,
        current_run_packet_path=current_run_packet_path,
        design_characteristics_path=design_characteristics_path,
        baseline_artifacts_path=baseline_artifacts_path,
        peer_learning_path=peer_learning_path,
        start_seed_calibration_path=start_seed_calibration_path,
        manual_teacher_guidance_path=(
            manual_teacher_guidance_path
            if manual_teacher_guidance_path is not None
            else "none"
        ),
        iteration_name=iter_name(iteration),
        route_lines=route_lines,
        student_roster=student_roster,
        teacher_rules=teacher_rules,
        teacher_plan_output=teacher_plan_output,
        children=children,
    )


def compact_teacher_prompt(
    *,
    context_path: Path,
    current_run_packet_path: Path,
    design_characteristics_path: Path,
    baseline_artifacts_path: Path,
    peer_learning_path: Path,
    start_seed_calibration_path: Path,
    manual_teacher_guidance_path: Path | None = None,
    strategies: list[str],
    children: int,
    iteration: int,
    student_runtime_multiplier: float,
    calibration_mode: bool = False,
) -> str:
    student_roster = "\n".join(
        render_prompt_template(
            "teacher/student_roster_line.md",
            student_id=f"student_{idx:02d}",
        ).rstrip()
        for idx in range(1, children + 1)
    )
    route_lines = "\n".join(
        render_prompt_template(
            "teacher/student_slot_line.md",
            student_id=f"student_{idx:02d}",
        ).rstrip()
        for idx, _name in enumerate(strategies, start=1)
    )
    teacher_rules = render_prompt_template(
        "teacher/rules_followup.md",
        student_runtime_multiplier=format_multiplier(student_runtime_multiplier),
    )
    teacher_plan_output = render_prompt_template(
        "teacher/plan_output.md",
        student_insight_packet_format=render_prompt_template(
            "shared/student_insight_packet_format.md"
        ),
    )
    if calibration_mode:
        teacher_rules = (
            teacher_rules.rstrip()
            + "\n\n"
            + render_prompt_template(
                "teacher/calibration_rules.md",
                student_runtime_multiplier=format_multiplier(student_runtime_multiplier),
                children=children,
            ).strip()
        )
        teacher_plan_output = render_prompt_template(
            "teacher/calibration_plan_output.md",
            student_insight_packet_format=render_prompt_template(
                "shared/student_insight_packet_format.md"
            ),
            children=children,
        )
    return render_prompt_template(
        "teacher/plan_compact.md",
        context_path=context_path,
        current_run_packet_path=current_run_packet_path,
        design_characteristics_path=design_characteristics_path,
        baseline_artifacts_path=baseline_artifacts_path,
        peer_learning_path=peer_learning_path,
        start_seed_calibration_path=start_seed_calibration_path,
        manual_teacher_guidance_path=(
            manual_teacher_guidance_path
            if manual_teacher_guidance_path is not None
            else "none"
        ),
        iteration_name=iter_name(iteration),
        route_lines=route_lines,
        student_roster=student_roster,
        teacher_rules=teacher_rules,
        teacher_plan_output=teacher_plan_output,
        children=children,
    )


def teacher_review_prompt(
    *,
    context_path: Path,
    review_packet_path: Path,
    student_runtime_multiplier: float,
    calibration_mode: bool = False,
) -> str:
    teacher_review_rules = render_prompt_template(
        "teacher/review_rules.md",
        student_runtime_multiplier=format_multiplier(student_runtime_multiplier),
    )
    teacher_review_output = render_prompt_template(
        "teacher/review_output.md",
        student_insight_packet_format=render_prompt_template(
            "shared/student_insight_packet_format.md"
        ),
    )
    if calibration_mode:
        teacher_review_rules = (
            teacher_review_rules.rstrip()
            + "\n\n"
            + render_prompt_template(
                "teacher/calibration_rules.md",
                student_runtime_multiplier=format_multiplier(student_runtime_multiplier),
            ).strip()
        )
        teacher_review_output = render_prompt_template(
            "teacher/calibration_review_output.md",
            student_insight_packet_format=render_prompt_template(
                "shared/student_insight_packet_format.md"
            ),
        )
    return render_prompt_template(
        "teacher/review.md",
        context_path=context_path,
        review_packet_path=review_packet_path,
        teacher_review_rules=teacher_review_rules,
        teacher_review_output=teacher_review_output,
    )


def compact_teacher_review_prompt(
    *,
    context_path: Path,
    review_packet_path: Path,
    student_runtime_multiplier: float,
    calibration_mode: bool = False,
) -> str:
    teacher_review_rules = render_prompt_template(
        "teacher/review_rules_followup.md",
        student_runtime_multiplier=format_multiplier(student_runtime_multiplier),
    )
    teacher_review_output = render_prompt_template(
        "teacher/review_output.md",
        student_insight_packet_format=render_prompt_template(
            "shared/student_insight_packet_format.md"
        ),
    )
    if calibration_mode:
        teacher_review_rules = (
            teacher_review_rules.rstrip()
            + "\n\n"
            + render_prompt_template(
                "teacher/calibration_rules.md",
                student_runtime_multiplier=format_multiplier(student_runtime_multiplier),
            ).strip()
        )
        teacher_review_output = render_prompt_template(
            "teacher/calibration_review_output.md",
            student_insight_packet_format=render_prompt_template(
                "shared/student_insight_packet_format.md"
            ),
        )
    return render_prompt_template(
        "teacher/review_compact.md",
        context_path=context_path,
        review_packet_path=review_packet_path,
        teacher_review_rules=teacher_review_rules,
        teacher_review_output=teacher_review_output,
    )


def child_prompt(
    *,
    context_path: Path,
    runtime: Any,
    round_dir: Path,
    teacher_operation_id: str,
    teacher_prompt_path: Path,
    case_id: str,
    flow_variant: str,
    threads: int,
    route_label: str,
    child_id: int,
    iteration: int,
    student_id: str,
    run_tag: str,
    workspace_packet_path: Path,
    peer_learning_path: Path,
    compact: bool = False,
    stable_workspace: bool = False,
    elite_seed_source: Path | None = None,
    student_runtime_multiplier: float = 2.0,
) -> str:
    elite_note = ""
    if elite_seed_source is not None:
        elite_note = render_prompt_template(
            "student/elite_initial_note.md",
            elite_seed_source=elite_seed_source,
        )
    return render_prompt_template(
        "student/initial.md",
        context_path=context_path,
        child_id=child_id,
        student_id=student_id,
        route_label=route_label,
        iteration_name=iter_name(iteration),
        workspace_packet_path=workspace_packet_path,
        peer_learning_path=peer_learning_path,
        teacher_plan_last_message=runtime.operations_dir
        / teacher_operation_id
        / "codex_last_message.txt",
        elite_note=elite_note,
        student_rules=render_prompt_template(
            "student/rules.md",
            student_runtime_multiplier=format_multiplier(student_runtime_multiplier),
        ),
    )


def compact_child_prompt(
    *,
    context_path: Path,
    runtime: Any,
    round_dir: Path,
    teacher_operation_id: str,
    teacher_prompt_path: Path,
    case_id: str,
    flow_variant: str,
    threads: int,
    route_label: str,
    child_id: int,
    iteration: int,
    student_id: str,
    run_tag: str,
    workspace_packet_path: Path,
    peer_learning_path: Path,
    stable_workspace: bool = False,
    elite_seed_source: Path | None = None,
    student_runtime_multiplier: float = 2.0,
) -> str:
    from scripts.teacher_loop.context_packets import peer_briefing

    elite_note = ""
    if elite_seed_source is not None:
        elite_note = render_prompt_template(
            "student/elite_followup_note.md",
            elite_seed_source=elite_seed_source,
        )
    peer_context = peer_briefing(
        runtime=runtime,
        round_dir=round_dir,
        case_id=case_id,
        flow_variant=flow_variant,
        iteration=iteration,
        current_student=student_id,
        student_runtime_multiplier=student_runtime_multiplier,
    )

    return render_prompt_template(
        "student/followup.md",
        context_path=context_path,
        student_id=student_id,
        route_label=route_label,
        iteration_name=iter_name(iteration),
        teacher_plan_last_message=runtime.operations_dir
        / teacher_operation_id
        / "codex_last_message.txt",
        workspace_packet_path=workspace_packet_path,
        peer_learning_path=peer_learning_path,
        case_id=case_id,
        flow_variant=flow_variant,
        threads=threads,
        run_tag=run_tag,
        elite_note=elite_note,
        peer_context=peer_context,
        student_rules=render_prompt_template(
            "student/rules_followup.md",
            student_runtime_multiplier=format_multiplier(student_runtime_multiplier),
        ),
    )

__all__ = [
    "ABSOLUTE_PATH_RE",
    "INFRASTRUCTURE_REVIEW_MARKERS",
    "NOISY_CONTEXT_MARKERS",
    "PROMPT_FORBIDDEN_TERMS",
    "absolute_path_count",
    "agent_prompt_warnings",
    "child_prompt",
    "compact_child_prompt",
    "compact_inline_text",
    "compact_teacher_prompt",
    "compact_teacher_review_prompt",
    "format_multiplier",
    "focused_message_excerpt",
    "labeled_line_excerpt",
    "markdown_section_excerpt",
    "previous_teacher_review_text",
    "prompt_audit",
    "read_text_excerpt",
    "teacher_prompt",
    "teacher_review_prompt",
]
