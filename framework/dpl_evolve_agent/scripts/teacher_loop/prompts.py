"""Prompt, packet, context, and prompt-audit generation."""
from __future__ import annotations

from typing import Any

from scripts.teacher_loop.prompt_rendering import (
    ABSOLUTE_PATH_RE,
    INFRASTRUCTURE_REVIEW_MARKERS,
    NOISY_CONTEXT_MARKERS,
    PROMPT_FORBIDDEN_TERMS,
    absolute_path_count,
    agent_prompt_warnings,
    child_prompt,
    compact_inline_text,
    compact_child_prompt,
    compact_teacher_prompt,
    compact_teacher_review_prompt,
    focused_message_excerpt,
    format_multiplier,
    labeled_line_excerpt,
    markdown_section_excerpt,
    previous_teacher_review_text,
    prompt_audit,
    read_text_excerpt,
    teacher_prompt,
    teacher_review_prompt,
)
from scripts.teacher_loop.context_packets import (
    common_context,
    peer_briefing,
    peer_mechanism_summary,
    prior_iteration_context,
    write_case_feature_route_insight_packet,
    write_iteration_context,
    write_teacher_routing_context,
)
from scripts.teacher_loop.workspace_scripts import (
    child_evaluation_timeout,
    child_parent_source,
    start_kind_source,
    validate_start_kind_seed,
    write_executable_script,
    write_student_workspace_scripts,
)


def _packet_builder_function(name: str) -> Any:
    import importlib

    return getattr(importlib.import_module("scripts.teacher_loop.packet_builders"), name)


def build_packet(*args: Any, **kwargs: Any) -> Any:
    return _packet_builder_function("build_packet")(*args, **kwargs)


def write_child_workspace_packet(*args: Any, **kwargs: Any) -> Any:
    return _packet_builder_function("write_child_workspace_packet")(*args, **kwargs)


def write_review_artifacts_packet(*args: Any, **kwargs: Any) -> Any:
    return _packet_builder_function("write_review_artifacts_packet")(*args, **kwargs)


def write_round_readme(*args: Any, **kwargs: Any) -> Any:
    return _packet_builder_function("write_round_readme")(*args, **kwargs)
