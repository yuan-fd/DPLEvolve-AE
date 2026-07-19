from __future__ import annotations

from dataclasses import asdict, dataclass, field
from typing import Any


def compact_dict(item: Any) -> dict[str, Any]:
    """Return a JSON-serializable dict for dataclass-backed records."""

    if hasattr(item, "__dataclass_fields__"):
        return asdict(item)
    if isinstance(item, dict):
        return dict(item)
    raise TypeError(f"Unsupported record type: {type(item)!r}")


@dataclass(frozen=True)
class CandidateProgram:
    program_id: str
    created_at: str
    source_type: str
    entrypoint: str = "detailed_placement_evolve"
    parent_program_id: str | None = None
    patch_path: str | None = None
    materialized_source: str | None = None
    touched_files: list[str] = field(default_factory=list)
    touched_symbols: list[str] = field(default_factory=list)
    algorithm_stage: str | None = None
    hypothesis: str | None = None
    metadata: dict[str, Any] = field(default_factory=dict)


@dataclass(frozen=True)
class BuildResult:
    build_id: str
    program_id: str
    created_at: str
    status: str
    binary_path: str | None = None
    build_log: str | None = None
    threads: int | None = None
    elapsed_seconds: float | None = None
    error_signature: str | None = None
    metadata: dict[str, Any] = field(default_factory=dict)


@dataclass(frozen=True)
class EvalResult:
    eval_id: str
    program_id: str
    created_at: str
    case: str
    line: str
    status: str
    metrics_json: str | None = None
    report_json: str | None = None
    telemetry_json: str | None = None
    legal: bool | None = None
    hpwl_delta: float | None = None
    runtime_seconds: float | None = None
    peak_rss_mb: float | None = None
    fallback_used: bool | None = None
    failure_stage: str | None = None
    error_signature: str | None = None
    metadata: dict[str, Any] = field(default_factory=dict)


@dataclass(frozen=True)
class TeacherReview:
    review_id: str
    program_id: str
    created_at: str
    decision: str
    summary: str
    strengths: list[str] = field(default_factory=list)
    weaknesses: list[str] = field(default_factory=list)
    next_guidance: list[str] = field(default_factory=list)
    promote_to: str | None = None
    metadata: dict[str, Any] = field(default_factory=dict)


@dataclass(frozen=True)
class DatabaseCommit:
    commit_id: str
    created_at: str
    table: str
    record_id: str | None
    record_hash: str
    source: str
