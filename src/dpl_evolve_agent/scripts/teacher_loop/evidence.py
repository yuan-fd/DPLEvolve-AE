"""Metrics, baseline evidence, peer-learning, and artifact packets."""
from __future__ import annotations

import json
import math
import re
import shlex
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Callable

from runtime_paths import clean_subprocess_env
from scripts.repo.case_registry import get_case
from scripts.teacher_loop.common import (
    CandidateArtifacts,
    ChildRound,
    MetricSummary,
    iter_name,
)
from scripts.teacher_loop.constants import CANONICAL_LINES

VALUE_REFERENCE_LINE = "openroad_dpl_flow"
HPWL_RUNTIME_GAIN_FORMULA = (
    "G_HR = 100 * (HPWL_ref - HPWL_sol) / HPWL_ref "
    "- P(runtime_sol / runtime_ref), where P(r)=0 for r <= 1.10 "
    "and P(2.0)=1.0 percentage point"
)
# Runtime is a value check, not the primary objective.  Runtime improvements
# and small runtime noise are not rewarded; only material slowdowns are
# penalized so Teacher does not chase timing noise.
RUNTIME_DEADBAND_RATIO = 1.10
RUNTIME_PENALTY_AT_2X = 1.0


def load_json(path: Path) -> dict[str, Any] | None:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return None


def canonical_hpwl(data: dict[str, Any]) -> dict[str, Any]:
    """Return only the OpenROAD/DPL pin-based HPWL metric.

    `hpwl_proxy` is a legacy cell-bbox proxy.  It is useful for debugging data
    extraction, but it must not drive Teacher decisions, baseline reuse, or
    candidate promotion.
    """
    hpwl = data.get("hpwl")
    if isinstance(hpwl, dict) and hpwl.get("source") != "cell_bbox_proxy":
        return hpwl
    hpwl = data.get("hpwl_openroad_log")
    if isinstance(hpwl, dict):
        return hpwl
    return {}


def hpwl_delta_percent(hpwl: dict[str, Any]) -> float | None:
    if hpwl.get("delta_percent") is not None:
        return float(hpwl["delta_percent"])
    before = hpwl.get("before_micron")
    delta = hpwl.get("delta_micron")
    if before in (None, 0) or delta is None:
        return None
    before_value = float(before)
    if before_value == 0.0:
        return None
    return float(delta) / before_value * 100.0


def _float_or_none(value: Any) -> float | None:
    if value is None or value == "":
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def runtime_penalty_pp(runtime_ratio: float) -> float:
    if runtime_ratio <= RUNTIME_DEADBAND_RATIO:
        return 0.0
    denominator = math.sqrt(2.0) - math.sqrt(RUNTIME_DEADBAND_RATIO)
    if denominator <= 0.0:
        return 0.0
    return RUNTIME_PENALTY_AT_2X * (
        math.sqrt(runtime_ratio) - math.sqrt(RUNTIME_DEADBAND_RATIO)
    ) / denominator


def _last_float(pattern: str, text: str) -> float | None:
    matches = re.findall(pattern, text, flags=re.MULTILINE)
    return _float_or_none(matches[-1]) if matches else None


def _first_float(pattern: str, text: str) -> float | None:
    match = re.search(pattern, text, flags=re.MULTILINE)
    return _float_or_none(match.group(1)) if match else None


def hpwl_stage_metrics(metrics_path: Path, data: dict[str, Any], hpwl: dict[str, Any]) -> dict[str, float | None]:
    """Return stage-wise HPWL, backfilled from the DPL log for older metrics."""
    stages = data.get("hpwl_stages")
    if not isinstance(stages, dict):
        stages = {}

    hpwl_global = _float_or_none(stages.get("global_micron"))
    hpwl_legalized = _float_or_none(stages.get("legalized_micron"))
    hpwl_after_improve = _float_or_none(stages.get("after_improve_micron"))
    hpwl_final = _float_or_none(stages.get("final_micron"))

    log_value = stages.get("log") or hpwl.get("log")
    log_path = Path(str(log_value)) if log_value else None
    if log_path is not None and not log_path.is_absolute():
        log_path = metrics_path.parent / log_path
    if log_path is not None and log_path.is_file():
        text = log_path.read_text(encoding="utf-8", errors="replace")
        hpwl_global = hpwl_global or _first_float(r"^original HPWL\s+([0-9.+-eE]+)\s+u", text)
        hpwl_global = hpwl_global or _first_float(r"^Original HPWL\s+([0-9.+-eE]+)\s+u", text)
        hpwl_legalized = hpwl_legalized or _last_float(r"^legalized HPWL\s+([0-9.+-eE]+)\s+u", text)
        hpwl_after_improve = hpwl_after_improve or _last_float(r"^Final HPWL\s+([0-9.+-eE]+)\s+u", text)
        hpwl_final = hpwl_final or _last_float(
            r"^\[INFO DPL-0022\]\s+HPWL after\s+([0-9.+-eE]+)\s+u", text
        )

    hpwl_global = hpwl_global if hpwl_global is not None else _float_or_none(hpwl.get("before_micron"))
    hpwl_final = hpwl_final if hpwl_final is not None else _float_or_none(hpwl.get("after_micron"))

    delta_legalization = (
        None if hpwl_global is None or hpwl_legalized is None else hpwl_legalized - hpwl_global
    )
    delta_improve = (
        None
        if hpwl_legalized is None or hpwl_after_improve is None
        else hpwl_after_improve - hpwl_legalized
    )
    delta_final = (
        None if hpwl_global is None or hpwl_final is None else hpwl_final - hpwl_global
    )

    def pct(delta: float | None) -> float | None:
        if delta is None or hpwl_global in (None, 0):
            return None
        return delta / hpwl_global * 100.0

    return {
        "global": hpwl_global,
        "legalized": hpwl_legalized,
        "after_improve": hpwl_after_improve,
        "final": hpwl_final,
        "delta_legalization": delta_legalization,
        "delta_improve": delta_improve,
        "delta_final": delta_final,
        "delta_legalization_percent": pct(delta_legalization),
        "delta_improve_percent": pct(delta_improve),
        "delta_final_percent": pct(delta_final),
    }


def summarize_metrics(metrics_path: Path) -> MetricSummary | None:
    data = load_json(metrics_path)
    if not data:
        return None
    hpwl = canonical_hpwl(data)
    stages = hpwl_stage_metrics(metrics_path, data, hpwl)
    disp = data.get("displacement", {})
    legal = data.get("legality", {})
    legalization = data.get("legalization", {})
    manifest = data.get("manifest", {})
    return MetricSummary(
        tag=metrics_path.parent.name,
        metrics_path=metrics_path,
        mode=str(legalization.get("legalizer_mode", "")),
        line=str(manifest.get("line", "")),
        run_tag=str(manifest.get("run_tag", metrics_path.parent.name)),
        hpwl_after=hpwl.get("after_micron"),
        hpwl_delta=hpwl.get("delta_micron"),
        runtime_seconds=data.get("runtime_seconds"),
        avg_disp=disp.get("average_displacement_micron"),
        max_disp=disp.get("max_displacement_micron"),
        violations=str(legal.get("placement_violations", "")),
        hpwl_delta_percent=hpwl_delta_percent(hpwl),
        hpwl_global=stages["global"],
        hpwl_legalized=stages["legalized"],
        hpwl_after_improve=stages["after_improve"],
        hpwl_stage_delta_legalization=stages["delta_legalization"],
        hpwl_stage_delta_improve=stages["delta_improve"],
        hpwl_stage_delta_final=stages["delta_final"],
        hpwl_stage_delta_legalization_percent=stages["delta_legalization_percent"],
        hpwl_stage_delta_improve_percent=stages["delta_improve_percent"],
        hpwl_stage_delta_final_percent=stages["delta_final_percent"],
    )


def is_canonical_baseline_summary(summary: MetricSummary, line: str) -> bool:
    """Return true only for orchestrator baseline-suite rows.

    Student evaluations also use `legalizer_mode=evolve_default`, so mode alone
    must never define the canonical baseline.  The baseline suite writes
    `baseline_probe_<line>` run tags; keep the match narrow so Teacher does not
    mistake a student candidate for the clean comparison anchor.
    """
    tag = summary.run_tag or summary.tag
    return (
        summary.line == line
        and f"baseline_probe_{line}" in tag
        and "_student_" not in tag
        and "_iter_" not in tag
        and summary.hpwl_after is not None
    )


def discover_case_metrics(
    *,
    orfs_root: Path,
    case_id: str,
    flow_variant: str,
) -> dict[str, MetricSummary | None]:
    info = get_case(case_id)
    report_root = (
        orfs_root
        / "flow"
        / "reports"
        / info.platform
        / info.design
        / flow_variant
        / "dpl_evolve_baseline"
    )
    candidates = sorted(
        (
            path
            for line in CANONICAL_LINES
            for path in report_root.glob(f"*baseline_probe_{line}*/metrics.json")
            if "_student_" not in path.parent.name and "_iter_" not in path.parent.name
        ),
        key=lambda path: path.stat().st_mtime,
    )
    summaries = [item for path in candidates if (item := summarize_metrics(path))]

    by_line: dict[str, MetricSummary | None] = {line: None for line in CANONICAL_LINES}
    for summary in summaries:
        for line in CANONICAL_LINES:
            if is_canonical_baseline_summary(summary, line):
                by_line[line] = summary

    return by_line


def is_clean_placement(summary: MetricSummary) -> bool:
    return summary.violations.strip().lower() in {"", "0", "clean", "none"}


def parse_candidate_tag(round_id: str, case_id: str, tag: str) -> tuple[str, str, str]:
    body = tag
    suffix = f"_{case_id}"
    if body.endswith(suffix):
        body = body[: -len(suffix)]
    prefix = f"{round_id}_"
    if body.startswith(prefix):
        body = body[len(prefix) :]
    if "_student_" not in body:
        return "", "", body
    iter_part, rest = body.split("_student_", 1)
    pieces = rest.split("_")
    if not pieces:
        return iter_part, "", body
    route_label = "_".join(pieces[1:]) if len(pieces) > 1 else "teacher_assigned"
    return iter_part, f"student_{pieces[0]}", route_label


def candidate_iteration_number(iter_part: str) -> int | None:
    if not iter_part.startswith("iter_"):
        return None
    try:
        return int(iter_part.split("_", 1)[1])
    except (IndexError, ValueError):
        return None


def candidate_operation_id(round_id: str, case_id: str, tag: str) -> str:
    body = tag
    suffix = f"_{case_id}"
    if body.endswith(suffix):
        body = body[: -len(suffix)]
    return body if body.startswith(f"{round_id}_") else f"{round_id}_{body}"


def candidate_artifacts(
    *,
    runtime: Any,
    round_dir: Path,
    round_id: str,
    case_id: str,
    candidate: MetricSummary,
) -> CandidateArtifacts | None:
    iter_part, student_id, route_label = parse_candidate_tag(
        round_id, case_id, candidate.tag
    )
    if not iter_part or not student_id:
        return None
    operation_id = candidate_operation_id(round_id, case_id, candidate.tag)
    operation_dir = runtime.operations_dir / operation_id
    student_root = round_dir / "students" / student_id
    return CandidateArtifacts(
        iter_part=iter_part,
        iter_number=candidate_iteration_number(iter_part),
        student_id=student_id,
        route_label=route_label,
        operation_id=operation_id,
        operation_dir=operation_dir,
        last_message=operation_dir / "codex_last_message.txt",
        source_repo=student_root / "workspace" / "variant" / "dpl_evolve",
        source_ref=source_ref_for_iteration(
            student_root=student_root,
            iter_part=iter_part,
        ),
        implementation_diff=student_root
        / iter_part
        / "artifacts"
        / "implementation.diff",
        knowledge_card=student_root / iter_part / "artifacts" / "knowledge_card.md",
    )


def _candidate_private_binary(round_dir: Path, artifacts: CandidateArtifacts) -> Path:
    stable_binary = (
        round_dir
        / "students"
        / artifacts.student_id
        / "workspace"
        / "variant"
        / "install"
        / "OpenROAD"
        / "bin"
        / "openroad"
    )
    if stable_binary.exists():
        return stable_binary
    return (
        round_dir
        / "students"
        / artifacts.student_id
        / artifacts.iter_part
        / "variant"
        / "install"
        / "OpenROAD"
        / "bin"
        / "openroad"
    )


def candidate_artifact_problems(
    *,
    runtime: Any,
    round_dir: Path,
    round_id: str,
    case_id: str,
    candidate: MetricSummary,
) -> list[str]:
    """Return non-promotable artifact problems for a student metric row.

    Metrics alone are not enough to seed the next iteration.  A promotable
    donor must have a successful Codex operation and source/knowledge artifacts
    that explain and reproduce the measured result.
    """
    artifacts = candidate_artifacts(
        runtime=runtime,
        round_dir=round_dir,
        round_id=round_id,
        case_id=case_id,
        candidate=candidate,
    )
    if artifacts is None:
        return ["missing_candidate_artifacts"]

    usage_path = artifacts.operation_dir / "codex_usage_summary.json"
    usage = load_json(usage_path) or {}
    returncode = usage.get("returncode")
    agent_message_count = int(usage.get("agent_message_count", 0) or 0)
    last_message_size = (
        artifacts.last_message.stat().st_size if artifacts.last_message.exists() else 0
    )
    diff_size = (
        artifacts.implementation_diff.stat().st_size
        if artifacts.implementation_diff.exists()
        else 0
    )
    knowledge_size = (
        artifacts.knowledge_card.stat().st_size
        if artifacts.knowledge_card.exists()
        else 0
    )
    private_binary = _candidate_private_binary(round_dir, artifacts)

    problems: list[str] = []
    if not usage_path.exists():
        problems.append("missing_usage_summary")
    if agent_message_count <= 0:
        problems.append("no_agent_message")
    if last_message_size <= 0 and not str(usage.get("last_agent_message") or "").strip():
        problems.append("empty_last_message")
    if not private_binary.exists():
        problems.append("missing_private_binary")
    if not artifacts.source_repo.exists():
        problems.append("missing_source_repo")
    if not artifacts.source_ref:
        problems.append("missing_source_ref")
    elif artifacts.source_repo.exists() and not git_ref_exists(
        artifacts.source_repo, artifacts.source_ref
    ):
        problems.append("missing_source_object")
    if not artifacts.implementation_diff.exists():
        problems.append("missing_implementation_diff")
    elif diff_size <= 0:
        problems.append("empty_implementation_diff")
    if not candidate.metrics_path.exists():
        problems.append("missing_metrics_json")
    if not is_clean_placement(candidate):
        problems.append("placement_not_clean")
    if candidate.hpwl_after is None:
        problems.append("missing_hpwl_after")
    return problems


def candidate_artifact_status_text(problems: list[str]) -> str:
    if not problems:
        return "valid"
    return "invalid:" + ",".join(problems[:4])


def is_promotable_candidate(
    *,
    runtime: Any,
    round_dir: Path,
    round_id: str,
    case_id: str,
    candidate: MetricSummary,
) -> bool:
    return not candidate_artifact_problems(
        runtime=runtime,
        round_dir=round_dir,
        round_id=round_id,
        case_id=case_id,
        candidate=candidate,
    )


def discover_round_candidate_metrics(
    *,
    orfs_root: Path,
    case_id: str,
    flow_variant: str,
    round_id: str,
    before_iteration: int | None = None,
) -> list[MetricSummary]:
    info = get_case(case_id)
    report_root = (
        orfs_root
        / "flow"
        / "reports"
        / info.platform
        / info.design
        / flow_variant
        / "dpl_evolve_baseline"
    )
    summaries: list[MetricSummary] = []
    for path in report_root.glob(f"{round_id}_iter_*_student_*/metrics.json"):
        summary = summarize_metrics(path)
        if summary is None:
            continue
        if before_iteration is not None:
            iter_part, _student_id, _route_label = parse_candidate_tag(
                round_id, case_id, summary.tag
            )
            iter_number = candidate_iteration_number(iter_part)
            if iter_number is None or iter_number >= before_iteration:
                continue
        summaries.append(summary)
    return sorted(summaries, key=lambda item: item.tag)


def candidate_beats_baseline(
    summary: MetricSummary,
    baseline: MetricSummary | None,
) -> bool:
    hpwl_win = candidate_beats_hpwl_only(summary, baseline)
    gain = hpwl_runtime_gain(summary, baseline)
    if gain is not None:
        return hpwl_win and gain > 0.0
    return hpwl_win


def candidate_beats_hpwl_only(
    summary: MetricSummary,
    baseline: MetricSummary | None,
) -> bool:
    return (
        baseline is not None
        and is_clean_placement(baseline)
        and baseline.hpwl_after is not None
        and is_clean_placement(summary)
        and summary.hpwl_after is not None
        and summary.hpwl_after < baseline.hpwl_after
    )


def runtime_ratio(
    summary: MetricSummary,
    baseline: MetricSummary | None,
) -> float | None:
    if (
        baseline is None
        or baseline.runtime_seconds in (None, 0)
        or summary.runtime_seconds is None
    ):
        return None
    return float(summary.runtime_seconds) / float(baseline.runtime_seconds)


def hpwl_runtime_gain_factors(
    summary: MetricSummary,
    baseline: MetricSummary | None,
) -> dict[str, float] | None:
    """Return HPWL/runtime value-gain factors versus the default-flow reference.

    The value reference is the OpenROAD DPL default-flow line.  Other canonical
    lines are useful donors/start points, but they should not redefine the
    value reference for the round.
    """
    if (
        baseline is None
        or not is_clean_placement(baseline)
        or not is_clean_placement(summary)
        or baseline.hpwl_after in (None, 0)
        or summary.hpwl_after in (None, 0)
        or baseline.runtime_seconds in (None, 0)
        or summary.runtime_seconds in (None, 0)
    ):
        return None
    hpwl_ref = float(baseline.hpwl_after)
    hpwl_sol = float(summary.hpwl_after)
    hpwl_global = (
        None if summary.hpwl_global in (None, 0) else float(summary.hpwl_global)
    )
    runtime_ref = float(baseline.runtime_seconds)
    runtime_sol = float(summary.runtime_seconds)
    if (
        hpwl_ref <= 0
        or hpwl_sol <= 0
        or runtime_ref <= 0
        or runtime_sol <= 0
    ):
        return None
    runtime_ratio_value = runtime_sol / runtime_ref
    hpwl_gain_percent = 100.0 * (hpwl_ref - hpwl_sol) / hpwl_ref
    runtime_penalty = runtime_penalty_pp(runtime_ratio_value)
    gain = hpwl_gain_percent - runtime_penalty
    return {
        "H_raw": hpwl_gain_percent,
        "R_raw": runtime_penalty,
        "H": hpwl_gain_percent,
        "R": runtime_penalty,
        "cost": -gain,
        "score": gain,
        "gain": gain,
        "hpwl_gain_percent": hpwl_gain_percent,
        "runtime_penalty_pp": runtime_penalty,
        "hpwl_global": hpwl_global,
        "runtime_ratio": runtime_ratio_value,
    }


def hpwl_runtime_gain(
    summary: MetricSummary,
    baseline: MetricSummary | None,
) -> float | None:
    factors = hpwl_runtime_gain_factors(summary, baseline)
    return None if factors is None else factors["score"]


def value_reference_anchor(
    metrics: dict[str, MetricSummary | None],
) -> tuple[str | None, MetricSummary | None]:
    """Return the fixed value reference: OpenROAD DPL default flow.

    If the default-flow row is missing, fall back to the best clean
    canonical line so old partial packets remain inspectable, but generated
    rounds should normally have the fixed reference.
    """
    diamond = metrics.get(VALUE_REFERENCE_LINE)
    if (
        diamond is not None
        and is_clean_placement(diamond)
        and diamond.hpwl_after is not None
        and diamond.runtime_seconds not in (None, 0)
    ):
        return VALUE_REFERENCE_LINE, diamond
    return strongest_baseline_anchor(metrics)


def objective_sort_value(
    summary: MetricSummary,
    baseline: MetricSummary | None,
) -> tuple[float]:
    """Return the evolve optimization key.

    Evolve selection is HPWL-only inside the strict legality/artifact/runtime
    gates.  G_HR is still reported for experiment analysis, but it must not
    decide elite continuation or tie-break a lower-HPWL candidate.
    """

    if summary.hpwl_after is not None:
        return (-float(summary.hpwl_after),)
    return (float("-inf"),)


def efficiency_signal(
    summary: MetricSummary,
    baseline: MetricSummary | None,
    *,
    budget_multiplier: float = 2.0,
) -> str:
    ratio = runtime_ratio(summary, baseline)
    if ratio is None:
        return "efficiency unknown"
    if ratio <= 1.5:
        return f"efficient ({ratio:.2f}x baseline)"
    if ratio <= budget_multiplier:
        return f"bounded ({ratio:.2f}x baseline)"
    return f"inefficient ({ratio:.2f}x baseline; not an effective donor)"


def stage_donor_signal(
    candidate: MetricSummary,
    baseline: MetricSummary | None,
    *,
    budget_multiplier: float = 2.0,
) -> str:
    """Classify candidate evidence without reducing it to final HPWL only.

    A strong HPWL result that needs much more than the student evaluation
    budget is quality evidence, not an effective donor.  Runtime-heavy search is
    useful only when it is scoped, measured, and tied to a concrete HPWL source.
    """
    if baseline is None or not is_clean_placement(candidate):
        return "unclassified"

    ratio = runtime_ratio(candidate, baseline)
    inefficient = ratio is not None and ratio > budget_multiplier

    if candidate_beats_baseline(candidate, baseline):
        if inefficient:
            return (
                "quality-only HPWL final signal; inefficient runtime "
                f"{ratio:.2f}x baseline, not an effective donor"
            )
        gain = hpwl_runtime_gain(candidate, baseline)
        gain_text = "" if gain is None else f" G_HR-analysis={gain:.3f}"
        return f"final HPWL donor{gain_text}"

    gain = hpwl_runtime_gain(candidate, baseline)
    if gain is not None and candidate_beats_hpwl_only(candidate, baseline):
        return (
            f"HPWL donor with G_HR-analysis={gain:.3f}; preserve HPWL, "
            "then repair runtime/quality if needed"
        )

    signals: list[str] = []
    if (
        baseline.hpwl_stage_delta_legalization is not None
        and candidate.hpwl_stage_delta_legalization is not None
    ):
        gain = baseline.hpwl_stage_delta_legalization - candidate.hpwl_stage_delta_legalization
        if gain > 0:
            signals.append(f"legalization stage donor: {gain:.1f} HPWL less damage")
    if (
        baseline.hpwl_stage_delta_improve is not None
        and candidate.hpwl_stage_delta_improve is not None
    ):
        gain = baseline.hpwl_stage_delta_improve - candidate.hpwl_stage_delta_improve
        if gain > 0:
            signals.append(f"improve stage donor: {gain:.1f} HPWL more recovery")
    if (
        baseline.hpwl_stage_delta_final is not None
        and candidate.hpwl_stage_delta_final is not None
    ):
        gain = baseline.hpwl_stage_delta_final - candidate.hpwl_stage_delta_final
        if gain > 0:
            signals.append(f"final-stage donor: {gain:.1f} HPWL lower final delta")

    if signals:
        signal = "; ".join(signals)
        if inefficient:
            return (
                "quality-only stage signal; inefficient runtime "
                f"{ratio:.2f}x baseline, not an effective donor: {signal}"
            )
        return signal
    return "negative evidence"


def strongest_baseline_anchor(
    metrics: dict[str, MetricSummary | None],
) -> tuple[str | None, MetricSummary | None]:
    """Return the clean baseline with the lowest canonical HPWL."""
    best_label: str | None = None
    best_summary: MetricSummary | None = None
    for label in CANONICAL_LINES:
        summary = metrics.get(label)
        if (
            summary is None
            or not is_clean_placement(summary)
            or summary.hpwl_after is None
        ):
            continue
        if (
            best_summary is None
            or best_summary.hpwl_after is None
            or summary.hpwl_after < best_summary.hpwl_after
        ):
            best_label = label
            best_summary = summary
    return best_label, best_summary


def five_percent_target_hpwl(baseline: MetricSummary | None) -> float | None:
    if baseline is None or baseline.hpwl_after is None:
        return None
    return baseline.hpwl_after * 0.95


def round_scoreboard(
    *,
    runtime: Any,
    case_id: str,
    flow_variant: str,
    round_id: str,
    metrics: dict[str, MetricSummary | None],
    before_iteration: int | None = None,
) -> str:
    candidates = discover_round_candidate_metrics(
        orfs_root=runtime.orfs_root,
        case_id=case_id,
        flow_variant=flow_variant,
        round_id=round_id,
        before_iteration=before_iteration,
    )
    if not candidates:
        return "No student candidate metrics have been found for this round yet."

    baseline_label, baseline = value_reference_anchor(metrics)
    baseline_hpwl = baseline.hpwl_after if baseline else None
    target_hpwl = five_percent_target_hpwl(baseline)
    lines = [
        f"Value gain: `{HPWL_RUNTIME_GAIN_FORMULA}` versus `{baseline_label or 'missing'}`; "
        "this is an analysis metric only. The evolve optimization target and "
        "elite continuation key are strict final HPWL inside legality/artifact/"
        "runtime gates. Use G_HR only to identify HPWL donors that need runtime "
        "repair. Use `HPWL_gain%`/`delta_vs_ref` for default-baseline comparison; "
        "`stage_dHPWL*` columns are diagnostic flow-internal movement only.",
        "",
        "| iter | student | route_label | artifact_gate | G_HR | HPWL_gain% | runtime_penalty_pp | HPWLg | stage_dHPWLlg% | stage_dHPWLip% | stage_dHPWLfinal% | HPWL final | delta_vs_ref | value_gt_ref | gap_to_5pct_target | runtime_s | runtime_ratio | avg_disp | max_disp | violations | metrics |",
        "|---|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---:|---:|---:|---:|---:|---|---|",
    ]
    best: MetricSummary | None = None
    best_beating: MetricSummary | None = None
    for summary in candidates:
        iter_part, student_id, route_label = parse_candidate_tag(
            round_id, case_id, summary.tag
        )
        artifact_problems = candidate_artifact_problems(
            runtime=runtime,
            round_dir=runtime.state_root / round_id / "teacher_rounds",
            round_id=round_id,
            case_id=case_id,
            candidate=summary,
        )
        promotable = not artifact_problems
        if promotable and (
            best is None
            or objective_sort_value(summary, baseline) > objective_sort_value(best, baseline)
        ):
            best = summary
        if promotable and candidate_beats_hpwl_only(summary, baseline) and (
            best_beating is None
            or objective_sort_value(summary, baseline)
            > objective_sort_value(best_beating, baseline)
        ):
            best_beating = summary
        delta = (
            None
            if baseline_hpwl is None or summary.hpwl_after is None
            else summary.hpwl_after - baseline_hpwl
        )
        target_gap = (
            None
            if target_hpwl is None or summary.hpwl_after is None
            else summary.hpwl_after - target_hpwl
        )
        gain_factors = hpwl_runtime_gain_factors(summary, baseline)
        runtime_factor = runtime_ratio(summary, baseline)
        lines.append(
            "| {iter_part} | {student_id} | {route_label} | {artifact_gate} | {score} | {h} | {r} | {hpwlg} | {d_lg_pct} | {d_ip_pct} | {d_final_pct} | {hpwl} | {delta} | "
            "{beats} | {target_gap} | {runtime_s} | {runtime_ratio} | {avg_disp} | {max_disp} | {violations} | `{metrics_path}` |".format(
                iter_part=iter_part or "?",
                student_id=student_id or "?",
                route_label=route_label or summary.tag,
                artifact_gate=candidate_artifact_status_text(artifact_problems),
                score="" if gain_factors is None else f"{gain_factors['score']:.3f}",
                h="" if gain_factors is None else f"{gain_factors['H']:.4f}",
                r="" if gain_factors is None else f"{gain_factors['R']:.4f}",
                hpwlg="" if summary.hpwl_global is None else f"{summary.hpwl_global:.3f}",
                d_lg_pct=""
                if summary.hpwl_stage_delta_legalization_percent is None
                else f"{summary.hpwl_stage_delta_legalization_percent:.2f}%",
                d_ip_pct=""
                if summary.hpwl_stage_delta_improve_percent is None
                else f"{summary.hpwl_stage_delta_improve_percent:.2f}%",
                d_final_pct=""
                if summary.hpwl_stage_delta_final_percent is None
                else f"{summary.hpwl_stage_delta_final_percent:.2f}%",
                hpwl="" if summary.hpwl_after is None else f"{summary.hpwl_after:.3f}",
                delta="" if delta is None else f"{delta:.3f}",
                beats="yes" if promotable and candidate_beats_baseline(summary, baseline) else "no",
                target_gap="" if target_gap is None else f"{target_gap:.3f}",
                runtime_s=""
                if summary.runtime_seconds is None
                else f"{summary.runtime_seconds:.3f}",
                runtime_ratio="" if runtime_factor is None else f"{runtime_factor:.2f}x",
                avg_disp="" if summary.avg_disp is None else f"{summary.avg_disp:.4f}",
                max_disp="" if summary.max_disp is None else f"{summary.max_disp:.4f}",
                violations=summary.violations or "clean",
                metrics_path=summary.metrics_path,
            )
        )

    if best and best.hpwl_after is not None:
        best_iter, best_student, best_route_label = parse_candidate_tag(
            round_id, case_id, best.tag
        )
        best_delta = (
            None
            if baseline_hpwl is None
            else best.hpwl_after - baseline_hpwl
        )
        lines.extend(
            [
                "",
                "Best valid clean HPWL candidate so far: "
                f"`{best_iter or '?'}` / `{best_student or '?'}` / "
                f"`{best_route_label or best.tag}` with analysis G_HR "
                f"`{(hpwl_runtime_gain(best, baseline) or float('nan')):.3f}` "
                f"and HPWL `{best.hpwl_after:.3f}`"
                + (
                    ""
                    if best_delta is None
                    else f" (`{best_delta:.3f}` HPWL vs {baseline_label or 'value reference'})."
                ),
            ]
        )
    if best_beating and best_beating.hpwl_after is not None:
        beat_iter, beat_student, beat_route_label = parse_candidate_tag(
            round_id, case_id, best_beating.tag
        )
        lines.append(
            "Best HPWL-beating candidate for G_HR analysis: "
            f"`{beat_iter or '?'}` / `{beat_student or '?'}` / "
            f"`{beat_route_label or best_beating.tag}`."
        )
    else:
        lines.append(
            "No prior valid student candidate beats the HPWL reference "
            f"(`{baseline_label or 'unknown'}`) yet."
        )
    if target_hpwl is not None:
        lines.append(
            f"Five-percent target from `{baseline_label}`: `{target_hpwl:.3f}`."
        )
    return "\n".join(lines)


def best_round_candidate(
    *,
    runtime: Any,
    case_id: str,
    flow_variant: str,
    round_id: str,
    baseline: MetricSummary | None = None,
    require_baseline_beat: bool = False,
    before_iteration: int | None = None,
) -> MetricSummary | None:
    best: MetricSummary | None = None
    best_iter = -1
    for summary in discover_round_candidate_metrics(
        orfs_root=runtime.orfs_root,
        case_id=case_id,
        flow_variant=flow_variant,
        round_id=round_id,
        before_iteration=before_iteration,
    ):
        if not is_clean_placement(summary) or summary.hpwl_after is None:
            continue
        if not is_promotable_candidate(
            runtime=runtime,
            round_dir=runtime.state_root / round_id / "teacher_rounds",
            round_id=round_id,
            case_id=case_id,
            candidate=summary,
        ):
            continue
        if require_baseline_beat and not candidate_beats_hpwl_only(summary, baseline):
            continue
        iter_part, _student_id, _route_label = parse_candidate_tag(
            round_id, case_id, summary.tag
        )
        iter_number = candidate_iteration_number(iter_part) or -1
        if (
            best is None
            or objective_sort_value(summary, baseline) > objective_sort_value(best, baseline)
            or (
                objective_sort_value(summary, baseline)
                == objective_sort_value(best, baseline)
                and iter_number > best_iter
            )
        ):
            best = summary
            best_iter = iter_number
    return best


def source_ref_for_iteration(*, student_root: Path, iter_part: str) -> str | None:
    data = load_json(student_root / "lineage.json") or {}
    for item in data.get("iterations", []):
        if item.get("iteration") == iter_part:
            value = item.get("source_candidate_ref") or item.get("source_commit")
            if value:
                return str(value)
            break
    record = student_root / iter_part / "artifacts" / "source_commit.json"
    payload = load_json(record) or {}
    value = payload.get("source_candidate_ref") or payload.get("source_commit")
    return str(value) if value else None


def git_ref_exists(source_repo: Path, source_ref: str) -> bool:
    result = subprocess.run(
        ["git", "-C", str(source_repo), "cat-file", "-e", f"{source_ref}^{{tree}}"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        text=False,
        env=clean_subprocess_env(),
    )
    return result.returncode == 0


def source_repo_ref_for_candidate(
    *,
    round_dir: Path,
    round_id: str,
    case_id: str,
    candidate: MetricSummary | None,
) -> tuple[Path, str] | None:
    if candidate is None:
        return None
    iter_part, student_id, _route_label = parse_candidate_tag(
        round_id, case_id, candidate.tag
    )
    if not iter_part or not student_id:
        return None
    student_root = round_dir / "students" / student_id
    source_repo = student_root / "workspace" / "variant" / "dpl_evolve"
    source_ref = source_ref_for_iteration(student_root=student_root, iter_part=iter_part)
    if source_repo.exists() and source_ref and git_ref_exists(source_repo, source_ref):
        return source_repo, source_ref
    return None


def materialize_source_ref(*, source_repo: Path, source_ref: str, target_dir: Path) -> Path:
    """Export one source revision to a plain directory for seeding/evaluation."""
    if target_dir.exists():
        shutil.rmtree(target_dir)
    target_dir.mkdir(parents=True, exist_ok=True)
    producer = subprocess.Popen(
        ["git", "-C", str(source_repo), "archive", "--format=tar", source_ref],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=False,
        env=clean_subprocess_env(),
    )
    assert producer.stdout is not None
    consumer = subprocess.run(
        ["tar", "-x", "-C", str(target_dir)],
        stdin=producer.stdout,
        stderr=subprocess.PIPE,
        text=False,
        env=clean_subprocess_env(),
    )
    producer.stdout.close()
    _stdout, producer_stderr = producer.communicate()
    if producer.returncode != 0:
        raise RuntimeError(
            "git archive failed for "
            f"{source_repo}@{source_ref}: {producer_stderr.decode(errors='replace')}"
        )
    if consumer.returncode != 0:
        raise RuntimeError(
            "tar extraction failed for "
            f"{target_dir}: {consumer.stderr.decode(errors='replace')}"
        )
    if not (target_dir / "CMakeLists.txt").is_file():
        raise RuntimeError(f"materialized source lacks CMakeLists.txt: {target_dir}")
    return target_dir


def peer_candidate_row(
    *,
    runtime: Any,
    round_dir: Path,
    round_id: str,
    case_id: str,
    candidate: MetricSummary,
    baseline: MetricSummary | None,
    budget_multiplier: float = 2.0,
) -> list[str]:
    artifacts = candidate_artifacts(
        runtime=runtime,
        round_dir=round_dir,
        round_id=round_id,
        case_id=case_id,
        candidate=candidate,
    )
    iter_part, student_id, route_label = parse_candidate_tag(
        round_id, case_id, candidate.tag
    )
    artifact_problems = candidate_artifact_problems(
        runtime=runtime,
        round_dir=round_dir,
        round_id=round_id,
        case_id=case_id,
        candidate=candidate,
    )
    source_repo = artifacts.source_repo if artifacts else None
    source_ref = artifacts.source_ref if artifacts else None
    implementation_diff = artifacts.implementation_diff if artifacts else None
    knowledge_card = artifacts.knowledge_card if artifacts else None
    delta = (
        None
        if baseline is None
        or baseline.hpwl_after is None
        or candidate.hpwl_after is None
        else candidate.hpwl_after - baseline.hpwl_after
    )
    beats = "yes" if candidate_beats_baseline(candidate, baseline) else "no"
    gain_factors = hpwl_runtime_gain_factors(candidate, baseline)
    runtime_factor = runtime_ratio(candidate, baseline)
    gain_text = "" if gain_factors is None else f"{gain_factors['score']:.3f}"
    hr_text = "" if gain_factors is None else f"{gain_factors['H']:.4f}/{gain_factors['R']:.4f}"
    runtime_ratio_text = "" if runtime_factor is None else f"{runtime_factor:.2f}x"
    summary = (
        f"- `{iter_part or '?'}` / `{student_id or '?'}` / "
        f"`{route_label or candidate.tag}`: HPWLg `{candidate.hpwl_global}`, "
        f"dHPWL lg/ip/final `{_fmt_stage_pct(candidate)}`, "
        f"HPWL final `{candidate.hpwl_after}`, "
        f"delta_vs_ref `{delta}`, G_HR "
        f"`{gain_text}`, "
        f"HPWL_gain/runtime_penalty_pp `{hr_text}`, "
        f"runtime `{candidate.runtime_seconds}`, runtime_ratio "
        f"`{runtime_ratio_text}`, "
        f"efficiency `{efficiency_signal(candidate, baseline, budget_multiplier=budget_multiplier)}`, "
        f"avg/max disp `{candidate.avg_disp}/{candidate.max_disp}`, "
        f"legality `{candidate.violations or 'clean'}`, value_gt_ref `{beats}`, "
        f"artifact_gate `{candidate_artifact_status_text(artifact_problems)}`, "
        f"stage_signal `{stage_donor_signal(candidate, baseline, budget_multiplier=budget_multiplier)}`"
    )
    pointers = (
        f"  - inspect only if useful: metrics `{candidate.metrics_path}`; "
        f"source_repo `{source_repo or ''}` source_ref `{source_ref or ''}`; "
        f"diff `{implementation_diff or ''}`; "
        f"knowledge `{knowledge_card or ''}`"
    )
    return [summary, pointers]


def write_peer_learning_packet(
    *,
    path: Path,
    runtime: Any,
    round_dir: Path,
    case_id: str,
    flow_variant: str,
    round_id: str,
    iteration: int,
    metrics: dict[str, MetricSummary | None],
    student_runtime_multiplier: float = 2.0,
) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    baseline_label, baseline = value_reference_anchor(metrics)
    target_hpwl = five_percent_target_hpwl(baseline)
    candidates = discover_round_candidate_metrics(
        orfs_root=runtime.orfs_root,
        case_id=case_id,
        flow_variant=flow_variant,
        round_id=round_id,
        before_iteration=iteration,
    )
    clean_candidates_all = [
        item
        for item in candidates
        if is_clean_placement(item) and item.hpwl_after is not None
    ]
    invalid_clean_candidates = [
        item
        for item in clean_candidates_all
        if candidate_artifact_problems(
            runtime=runtime,
            round_dir=round_dir,
            round_id=round_id,
            case_id=case_id,
            candidate=item,
        )
    ]
    clean_candidates = [
        item
        for item in clean_candidates_all
        if not candidate_artifact_problems(
            runtime=runtime,
            round_dir=round_dir,
            round_id=round_id,
            case_id=case_id,
            candidate=item,
        )
    ]
    clean_candidates.sort(
        key=lambda item: objective_sort_value(item, baseline),
        reverse=True,
    )
    beating_candidates = [
        item for item in clean_candidates if candidate_beats_hpwl_only(item, baseline)
    ]

    chunks = [
        "# Generated Peer Learning Packet",
        "",
        "This file is generated by the orchestrator.  It is the code-learning "
        "index for the next Teacher/Student step.",
        "",
        "Teacher routing note: if there is no prior clean donor, stop after the "
        "`Best Clean Candidates` section. Do not read beyond that during the "
        "initial routing pass.",
        "",
        "Use it to borrow mechanisms and source from valid peer candidates. "
        "A candidate is valid only when the Codex operation, source commit, "
        "diff, knowledge card, private binary, metrics, and legality all pass "
        "the artifact gate. Elite continuation is selected by lowest strict "
        "final HPWL inside legality, artifact, and hard runtime gates. "
        f"`{HPWL_RUNTIME_GAIN_FORMULA}` is an analysis metric only: use it to "
        "explain runtime value and route runtime repair, not to choose the "
        "elite parent. Treat "
        "5% below the value reference as a credible research target, not a "
        "hard gate; below-5% stage donors can still be useful when efficient.",
        "",
        "## Baseline Anchor",
    ]
    if baseline is None:
        chunks.append("- value_reference_line: missing")
    else:
        chunks.extend(
            [
                f"- value_reference_line: `{baseline_label}`",
                f"- value_gain: `{HPWL_RUNTIME_GAIN_FORMULA}`",
                f"- HPWLg: `{baseline.hpwl_global}`",
                f"- delta_HPWL_legalization: `{baseline.hpwl_stage_delta_legalization}`",
                f"- delta_HPWL_improve: `{baseline.hpwl_stage_delta_improve}`",
                f"- delta_HPWL_final: `{baseline.hpwl_stage_delta_final}`",
                f"- stage_delta_pct_lg_ip_final: `{_fmt_stage_pct(baseline)}`",
                f"- hpwl_after: `{baseline.hpwl_after}`",
                f"- five_percent_target_hpwl: `{target_hpwl}`",
                f"- runtime_seconds: `{baseline.runtime_seconds}`",
                f"- avg_disp: `{baseline.avg_disp}`",
                f"- max_disp: `{baseline.max_disp}`",
                f"- violations: `{baseline.violations or 'clean'}`",
                f"- metrics_json: `{baseline.metrics_path}`",
            ]
        )

    chunks.extend(["", "## HPWL-Beating Candidates With G_HR Analysis"])
    if not beating_candidates:
        chunks.append(
            "No prior valid clean student candidate beats the HPWL reference yet. "
            "Students may still inspect the best valid clean candidate as a clue, "
            "but it is not an elite seed unless it is the lowest final HPWL row "
            "inside the hard gates."
        )
    else:
        for candidate in beating_candidates[:3]:
            chunks.extend(
                peer_candidate_row(
                    runtime=runtime,
                    round_dir=round_dir,
                    round_id=round_id,
                    case_id=case_id,
                    candidate=candidate,
                    baseline=baseline,
                    budget_multiplier=student_runtime_multiplier,
                )
            )

    chunks.extend(["", "## Best Clean Candidates"])
    if not clean_candidates:
        chunks.append("No prior valid clean student candidate metrics are available.")
    else:
        for candidate in clean_candidates[:3]:
            chunks.extend(
                peer_candidate_row(
                    runtime=runtime,
                    round_dir=round_dir,
                    round_id=round_id,
                    case_id=case_id,
                    candidate=candidate,
                    baseline=baseline,
                    budget_multiplier=student_runtime_multiplier,
                )
            )

    if invalid_clean_candidates:
        chunks.extend(["", "## Invalid Metric Rows"])
        for candidate in invalid_clean_candidates[:5]:
            iter_part, student_id, route_label = parse_candidate_tag(
                round_id, case_id, candidate.tag
            )
            problems = candidate_artifact_problems(
                runtime=runtime,
                round_dir=round_dir,
                round_id=round_id,
                case_id=case_id,
                candidate=candidate,
            )
            chunks.append(
                f"- `{iter_part or '?'}` / `{student_id or '?'}` / "
                f"`{route_label or candidate.tag}`: non-promotable despite "
                f"metrics; artifact_gate `{candidate_artifact_status_text(problems)}`; "
                f"metrics `{candidate.metrics_path}`"
            )

    chunks.extend(
        [
            "",
            "## Use Policy",
            "- Teacher should compare code and metrics before assigning a borrow/merge direction.",
            "- Invalid metric rows are diagnostic only. Do not call them clean elites, do not seed from them, and do not ask a Student to inherit their source.",
            "- If an invalid row is the best final-HPWL signal only because its source object is missing, treat it as lost-source donor evidence: preserve the mechanism summary from metrics, diff, logs, or knowledge card, assign at most one reconstruction route from a valid current-run ref, and keep the rest of the roster on broad mechanism search.",
            f"- Teacher must separate effective donors from quality-only signals.  A candidate that wins mainly through uncontrolled repeated subpasses, untargeted scans, or random trial-and-error, or exceeds the {student_runtime_multiplier:g}x runtime budget, is not an effective donor until the mechanism is reimplemented with scoped targeting, counters, and bounded complexity.",
            "- Teacher must separate final donors from stage donors.  If a candidate improves one stage but loses later, preserve a repair route for the later-stage handoff.",
            "- Student may inspect peer code when it directly supports the chosen implementation change.",
            "- If a student opens peer code or diff, first summarize the peer idea in 2-3 lines, then decide what to borrow.",
            "- Prefer learning a mechanism over blind wholesale replacement.",
            "- A clean lowest-final-HPWL candidate is the elite continuation candidate inside the hard runtime gate. G_HR is analysis-only; use it to route runtime repair, not to replace the HPWL elite. A faster but worse-HPWL candidate is runtime/control evidence, not the headline elite.",
            "- Sub-1% wins over `evolve_default` are donor evidence, not success.",
        ]
    )
    path.write_text("\n".join(chunks) + "\n", encoding="utf-8")
    return path


def seed_private_source_from_elite(
    *,
    seed_source: Path,
    target_source: Path,
) -> dict[str, str | bool | None]:
    if not seed_source.exists():
        raise FileNotFoundError(f"missing elite seed source: {seed_source}")
    ignore = shutil.ignore_patterns(
        ".git",
        "__pycache__",
        "*.pyc",
        "*.o",
        "*.a",
        "*.so",
        "CMakeFiles",
        "cmake_install.cmake",
    )
    if target_source.exists() and (target_source / ".git").exists():
        return _seed_into_existing_git_source(
            seed_source=seed_source,
            target_source=target_source,
            ignore=ignore,
        )
    if target_source.exists():
        shutil.rmtree(target_source)
    target_source.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(
        seed_source,
        target_source,
        ignore=ignore,
    )
    return {"mode": "fresh_copy", "backup_ref": None, "seed_commit": None}


def _seed_git(
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


def _seed_repo_has_head(repo: Path) -> bool:
    return (
        _seed_git(
            repo,
            "rev-parse",
            "--verify",
            "HEAD",
            check=False,
            capture=True,
        ).returncode
        == 0
    )


def _seed_configure_git_repo(repo: Path) -> None:
    top = _seed_git(repo, "rev-parse", "--show-toplevel", capture=True).stdout.strip()
    if Path(top).resolve() != repo.resolve():
        raise RuntimeError(f"elite seed target is not an independent git repo: {repo}")
    _seed_git(repo, "config", "user.email", "dpl-evolve-agent@example.invalid")
    _seed_git(repo, "config", "user.name", "dpl-evolve-agent")


def _seed_commit_if_staged(repo: Path, message: str) -> str | None:
    if _seed_git(repo, "diff", "--cached", "--quiet", check=False).returncode == 0:
        return None
    _seed_git(repo, "commit", "-m", message)
    return _seed_git(repo, "rev-parse", "HEAD", capture=True).stdout.strip()


def _replace_worktree_preserving_git(
    *,
    seed_source: Path,
    target_source: Path,
    ignore: Callable[[str, list[str]], set[str]],
) -> None:
    tmp = target_source.parent / f".{target_source.name}.elite_seed_tmp"
    if tmp.exists():
        shutil.rmtree(tmp)
    shutil.copytree(seed_source, tmp, ignore=ignore)
    try:
        for child in target_source.iterdir():
            if child.name == ".git":
                continue
            if child.is_dir() and not child.is_symlink():
                shutil.rmtree(child)
            else:
                child.unlink()
        for child in tmp.iterdir():
            shutil.move(str(child), str(target_source / child.name))
    finally:
        if tmp.exists():
            shutil.rmtree(tmp)


def _seed_into_existing_git_source(
    *,
    seed_source: Path,
    target_source: Path,
    ignore: Callable[[str, list[str]], set[str]],
) -> dict[str, str | bool | None]:
    """Seed an elite source while preserving candidate refs and git objects."""

    _seed_configure_git_repo(target_source)
    had_head = _seed_repo_has_head(target_source)
    backup_ref: str | None = None
    preserve_commit: str | None = None
    if had_head:
        _seed_git(target_source, "add", "-A")
        preserve_commit = _seed_commit_if_staged(
            target_source,
            "Preserve workspace before elite seed",
        )
        backup_ref = f"backup/pre_elite_seed/{time.strftime('%Y%m%d%H%M%S')}-{time.time_ns() % 1000000}"
        _seed_git(target_source, "branch", "-f", backup_ref, "HEAD")
    _replace_worktree_preserving_git(
        seed_source=seed_source,
        target_source=target_source,
        ignore=ignore,
    )
    _seed_git(target_source, "add", "-A")
    seed_commit = _seed_commit_if_staged(
        target_source,
        "Seed elite source while preserving candidate refs",
    )
    if seed_commit is None and _seed_repo_has_head(target_source):
        seed_commit = _seed_git(
            target_source,
            "rev-parse",
            "HEAD",
            capture=True,
        ).stdout.strip()
    return {
        "mode": "preserved_git",
        "had_head": had_head,
        "backup_ref": backup_ref,
        "preserve_commit": preserve_commit,
        "seed_commit": seed_commit,
    }


def baseline_suite_preflight_command(
    *,
    runtime: Any,
    case_id: str,
    flow_variant: str,
    threads: int,
    round_id: str,
) -> list[str]:
    command = [
        str(runtime.agent_root / "baseline" / "run_baseline_suite.sh"),
        "--case",
        case_id,
        "--flow-variant",
        flow_variant,
        "--threads",
        str(threads),
        "--tag-prefix",
        f"{round_id}_baseline_probe",
    ]
    core_binary = openroad_core_binary(runtime)
    if core_binary is not None:
        command.extend(["--openroad-binary", str(core_binary)])
    return command


def openroad_core_binary(runtime: Any) -> Path | None:
    """Return the freshly built common OpenROAD core binary if identifiable."""
    try:
        proc = subprocess.run(
            [
                "git",
                "-C",
                str(runtime.orfs_root / "tools" / "OpenROAD"),
                "rev-parse",
                "--short",
                "HEAD",
            ],
            capture_output=True,
            text=True,
            check=True,
        )
    except Exception:
        return None
    anchor_id = proc.stdout.strip()
    if not anchor_id:
        return None
    return (
        runtime.state_root
        / "openroad_core"
        / anchor_id
        / "install"
        / "OpenROAD"
        / "bin"
        / "openroad"
    )


def baseline_suite_complete(metrics: dict[str, MetricSummary | None]) -> bool:
    return all(metrics.get(line) is not None for line in CANONICAL_LINES)


def ensure_baseline_suite(
    *,
    runtime: Any,
    case_id: str,
    flow_variant: str,
    threads: int,
    round_id: str,
    refresh: bool,
    dry_run: bool,
) -> tuple[dict[str, MetricSummary | None], list[list[str]]]:
    command = baseline_suite_preflight_command(
        runtime=runtime,
        case_id=case_id,
        flow_variant=flow_variant,
        threads=threads,
        round_id=round_id,
    )
    metrics = discover_case_metrics(
        orfs_root=runtime.orfs_root,
        case_id=case_id,
        flow_variant=flow_variant,
    )
    if baseline_suite_complete(metrics) and not refresh:
        return metrics, []

    if dry_run:
        return metrics, [command]

    print("[optimize_case] baseline suite preflight:", " ".join(shlex.quote(part) for part in command))
    subprocess.run(command, check=True, env=clean_subprocess_env())
    metrics = discover_case_metrics(
        orfs_root=runtime.orfs_root,
        case_id=case_id,
        flow_variant=flow_variant,
    )
    if not baseline_suite_complete(metrics):
        missing = ", ".join(line for line in CANONICAL_LINES if metrics.get(line) is None)
        raise SystemExit(
            "Baseline suite preflight completed but missing metrics for "
            f"{missing} under flow_variant={flow_variant}."
        )
    return metrics, [command]


def metric_table(metrics: dict[str, MetricSummary | None]) -> str:
    value_label, value_baseline = value_reference_anchor(metrics)
    lines = [
        f"Value reference: `{value_label or VALUE_REFERENCE_LINE}`; value gain `{HPWL_RUNTIME_GAIN_FORMULA}`.",
        "",
        "| line | G_HR | HPWL_gain% | runtime_penalty_pp | HPWLg | delta HPWLlg | delta HPWLip | delta HPWL final | HPWL final | runtime_s | avg_disp | max_disp | violations |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|",
    ]
    for name in CANONICAL_LINES:
        summary = metrics.get(name)
        if summary is None:
            lines.append(f"| {name} | missing |  |  |  |  |  |  |  |  |  |  |  |")
            continue
        gain_factors = hpwl_runtime_gain_factors(summary, value_baseline)
        lines.append(
            "| {name} | {score} | {h} | {r} | {hpwlg} | {d_lg} | {d_ip} | {d_final} | {hpwl_after} | {runtime} | "
            "{avg_disp} | {max_disp} | {violations} |".format(
                name=name,
                score="" if gain_factors is None else f"{gain_factors['score']:.3f}",
                h="" if gain_factors is None else f"{gain_factors['H']:.4f}",
                r="" if gain_factors is None else f"{gain_factors['R']:.4f}",
                hpwlg="" if summary.hpwl_global is None else f"{summary.hpwl_global:.3f}",
                d_lg=""
                if summary.hpwl_stage_delta_legalization is None
                else f"{summary.hpwl_stage_delta_legalization:.3f}",
                d_ip=""
                if summary.hpwl_stage_delta_improve is None
                else f"{summary.hpwl_stage_delta_improve:.3f}",
                d_final=""
                if summary.hpwl_stage_delta_final is None
                else f"{summary.hpwl_stage_delta_final:.3f}",
                hpwl_after="" if summary.hpwl_after is None else f"{summary.hpwl_after:.3f}",
                runtime=""
                if summary.runtime_seconds is None
                else f"{summary.runtime_seconds:.3f}",
                avg_disp="" if summary.avg_disp is None else f"{summary.avg_disp:.4f}",
                max_disp="" if summary.max_disp is None else f"{summary.max_disp:.4f}",
                violations=summary.violations or "clean",
            )
        )
    return "\n".join(lines)


def _fmt_float(value: Any, digits: int = 3, suffix: str = "") -> str:
    if value is None:
        return ""
    try:
        return f"{float(value):.{digits}f}{suffix}"
    except (TypeError, ValueError):
        return str(value)


def _fmt_stage_pct(summary: MetricSummary) -> str:
    parts = []
    for label, value in (
        ("lg", summary.hpwl_stage_delta_legalization_percent),
        ("ip", summary.hpwl_stage_delta_improve_percent),
        ("final", summary.hpwl_stage_delta_final_percent),
    ):
        parts.append(f"{label}:{_fmt_float(value, 2, '%')}")
    return " / ".join(parts)


def design_characteristics_block(
    *,
    case_id: str,
    flow_variant: str,
    metrics: dict[str, MetricSummary | None],
) -> str:
    """Summarize case facts Teacher should pass to students.

    Keep this evidence-derived and compact.  The Teacher can add judgment, but
    this block gives it the concrete design/baseline facts that matter before
    asking students to modify algorithms.
    """
    anchor_label, anchor = value_reference_anchor(metrics)
    target_hpwl = five_percent_target_hpwl(anchor)
    anchor_data = load_json(anchor.metrics_path) if anchor is not None else None
    design = (anchor_data or {}).get("design_metrics", {})
    disp = (anchor_data or {}).get("displacement", {})
    legalize = (anchor_data or {}).get("legalization", {})
    summary_data = load_json(anchor.metrics_path.parent / "legalize_summary.json") if anchor is not None else None
    fixed_count = (summary_data or {}).get("fixed_instance_count")
    placed_count = (summary_data or {}).get("placed_instance_count")

    utilization = design.get("utilization")
    util_text = ""
    if utilization is not None:
        util_text = _fmt_float(float(utilization) * 100.0, 1, "%")

    lines = [
        f"- case: `{case_id}`",
        f"- flow_variant/input: `{flow_variant}` / `3_4_place_resized.odb`",
        "- density: core_area={core}um^2, instance_area={inst}um^2, "
        "utilization={util}".format(
            core=_fmt_float(design.get("core_area"), 1),
            inst=_fmt_float(design.get("instance_area"), 1),
            util=util_text,
        ),
        "- population: instances={inst_count}, movable={movable}, fixed={fixed}, placed={placed}".format(
            inst_count=design.get("instance_count", ""),
            movable=disp.get("movable_instance_count", ""),
            fixed=fixed_count if fixed_count is not None else "",
            placed=placed_count if placed_count is not None else "",
        ),
        "- fixed-count note: fixed instances are not macro evidence by themselves; "
        "they may include taps/endcaps/fillers or true large fixed blockages. "
        "Treat a macro as a physically large instance proven by geometry/source evidence.",
        "- baseline flow: `{seq}`".format(seq=legalize.get("stage_sequence", "")),
        "- baseline displacement: avg={avg}um, max={maxd}um, moved={moved}".format(
            avg=_fmt_float(disp.get("average_displacement_micron"), 4),
            maxd=_fmt_float(disp.get("max_displacement_micron"), 4),
            moved=disp.get("moved_instance_count", ""),
        ),
        "- G_HR value reference: `{label}`, five_percent_target_hpwl={target}".format(
            label=anchor_label or "",
            target=_fmt_float(target_hpwl, 3),
        ),
    ]

    lines.extend(
        [
            "",
            "HPWL values below use OpenROAD/DPL pin-based log HPWL only. "
            "`hpwl_proxy` is debug-only and is never a promotion metric.",
            "Primary evolve target remains canonical HPWL reduction. "
            f"`{HPWL_RUNTIME_GAIN_FORMULA}` relative to "
            f"`{anchor_label or VALUE_REFERENCE_LINE}` is the value/balance gain "
            "for deciding whether the runtime cost is worth the HPWL gain.",
            "",
            "| line | G_HR | HPWL_gain% | runtime_penalty_pp | HPWLg | delta HPWLlg | delta HPWLip | delta HPWL final | HPWL final | stage_delta_pct | runtime_s | violations |",
            "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|",
        ]
    )
    for line in CANONICAL_LINES:
        summary = metrics.get(line)
        if summary is None:
            lines.append(f"| {line} | missing |  |  |  |  |  |  |  |  |  |  |")
            continue
        gain_factors = hpwl_runtime_gain_factors(summary, anchor)
        lines.append(
            "| {line} | {score} | {h} | {r} | {hpwlg} | {d_lg} | {d_ip} | {d_final} | {after} | {stage_pct} | {runtime} | {violations} |".format(
                line=line,
                score="" if gain_factors is None else f"{gain_factors['score']:.3f}",
                h="" if gain_factors is None else f"{gain_factors['H']:.4f}",
                r="" if gain_factors is None else f"{gain_factors['R']:.4f}",
                hpwlg=_fmt_float(summary.hpwl_global, 3),
                d_lg=_fmt_float(summary.hpwl_stage_delta_legalization, 3),
                d_ip=_fmt_float(summary.hpwl_stage_delta_improve, 3),
                d_final=_fmt_float(summary.hpwl_stage_delta_final, 3),
                after=_fmt_float(summary.hpwl_after, 3),
                stage_pct=_fmt_stage_pct(summary),
                runtime=_fmt_float(summary.runtime_seconds, 3),
                violations=summary.violations or "clean",
            )
        )
    return "\n".join(lines)


def write_design_characteristics_packet(
    *,
    path: Path,
    case_id: str,
    flow_variant: str,
    metrics: dict[str, MetricSummary | None],
) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "# Generated Design Characteristics\n\n"
        + design_characteristics_block(
            case_id=case_id,
            flow_variant=flow_variant,
            metrics=metrics,
        )
        + "\n",
        encoding="utf-8",
    )
    return path


def baseline_artifact_block(
    *,
    runtime: Any,
    case_id: str,
    flow_variant: str,
    metrics: dict[str, MetricSummary | None],
) -> str:
    info = get_case(case_id)
    flow_root = runtime.orfs_root / "flow"
    blocks = []
    value_label, value_baseline = value_reference_anchor(metrics)
    blocks.extend(
        [
            f"- value_reference_line: `{value_label or VALUE_REFERENCE_LINE}`",
            f"- value_gain: `{HPWL_RUNTIME_GAIN_FORMULA}`",
            "",
        ]
    )
    for line in CANONICAL_LINES:
        summary = metrics.get(line)
        blocks.append(f"### {line}")
        if summary is None:
            blocks.append("- status: missing; run the baseline suite before making a promotion decision.")
            continue
        tag = summary.tag
        rel_root = Path(info.platform) / info.design / flow_variant / "dpl_evolve_baseline" / tag
        report_dir = flow_root / "reports" / rel_root
        log_dir = flow_root / "logs" / rel_root
        result_dir = flow_root / "results" / rel_root
        gain_factors = hpwl_runtime_gain_factors(summary, value_baseline)
        check_report_path = report_dir / "check_placement_report.json"
        check_report_value = (
            f"`{check_report_path}`" if check_report_path.is_file() else "absent"
        )
        gain_text = "" if gain_factors is None else f"{gain_factors['score']:.3f}"
        h_text = "" if gain_factors is None else f"{gain_factors['H']:.6f}"
        r_text = "" if gain_factors is None else f"{gain_factors['R']:.6f}"
        blocks.extend(
            [
                f"- run_tag: `{tag}`",
                f"- G_HR: `{gain_text}`",
                f"- G_HR_HPWL_gain_percent: `{h_text}`",
                f"- G_HR_runtime_penalty_pp: `{r_text}`",
                f"- HPWLg: `{summary.hpwl_global}`",
                f"- delta_HPWL_legalization: `{summary.hpwl_stage_delta_legalization}`",
                f"- delta_HPWL_improve: `{summary.hpwl_stage_delta_improve}`",
                f"- delta_HPWL_final: `{summary.hpwl_stage_delta_final}`",
                f"- stage_delta_pct_lg_ip_final: `{_fmt_stage_pct(summary)}`",
                f"- HPWL_final: `{summary.hpwl_after}`",
                f"- metrics_json: `{summary.metrics_path}`",
                f"- report_dir: `{report_dir}`",
                f"- result_dir: `{result_dir}`",
                f"- log_dir: `{log_dir}`",
                f"- legalize_log: `{log_dir / f'dpl_evolve_{tag}_legalize.log'}`",
                f"- metrics_log: `{log_dir / '3_5_place_dp.log'}`",
                f"- legalize_summary: `{report_dir / 'legalize_summary.json'}`",
                f"- check_report: {check_report_value}",
                f"- legalized_odb: `{result_dir / 'legalized.odb'}`",
            ]
        )
    return "\n".join(blocks)


def write_baseline_artifacts_packet(
    *,
    path: Path,
    runtime: Any,
    case_id: str,
    flow_variant: str,
    metrics: dict[str, MetricSummary | None],
) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "# Generated Baseline Artifact Packet\n\n"
        + "Teacher routing note: the design-characteristics packet already carries the\n"
        + "compact comparison table. Open this file only when you need an exact artifact\n"
        + "path or a stage/log path that is not already pinned down there.\n\n"
        + baseline_artifact_block(
            runtime=runtime,
            case_id=case_id,
            flow_variant=flow_variant,
            metrics=metrics,
        )
        + "\n",
        encoding="utf-8",
    )
    return path


def operation_artifact_block(runtime: Any, operation_id: str) -> str:
    op_dir = runtime.operations_dir / operation_id
    return f"""- operation_id: `{operation_id}`
  operation_dir: `{op_dir}`
  last_message: `{op_dir / "codex_last_message.txt"}`
  usage_summary: `{op_dir / "codex_usage_summary.json"}`
  events_large_do_not_read_by_default: `{op_dir / "codex_events.jsonl"}`
  stderr: `{op_dir / "codex_stderr.log"}`
"""


def expected_metrics_path(
    *,
    runtime: Any,
    case_id: str,
    flow_variant: str,
    run_tag: str,
) -> Path:
    info = get_case(case_id)
    return (
        runtime.orfs_root
        / "flow"
        / "reports"
        / info.platform
        / info.design
        / flow_variant
        / "dpl_evolve_baseline"
        / run_tag
        / "metrics.json"
    )


def child_artifact_status(
    *,
    runtime: Any,
    case_id: str,
    flow_variant: str,
    child: ChildRound,
) -> dict[str, Any]:
    usage = load_json(child.usage_summary) or {}
    metrics_path = expected_metrics_path(
        runtime=runtime,
        case_id=case_id,
        flow_variant=flow_variant,
        run_tag=child.run_tag,
    )
    metric_summary = summarize_metrics(metrics_path) if metrics_path.exists() else None
    diff_size = child.implementation_diff.stat().st_size if child.implementation_diff.exists() else 0
    knowledge_size = child.knowledge_card.stat().st_size if child.knowledge_card.exists() else 0
    source_trials = child.implementation_diff.parent / "source_trials.jsonl"
    source_trials_size = source_trials.stat().st_size if source_trials.exists() else 0
    source_trials_count = 0
    source_trial_actions: list[str] = []
    source_trial_refs: list[str] = []
    rejected_refs: list[str] = []
    kept_refs: list[str] = []
    has_rejected_evidence = False
    has_kept_or_finalized = False
    finalized_source = False
    final_trial_status = "unmarked"
    if source_trials.exists():
        with source_trials.open(encoding="utf-8", errors="replace") as fh:
            for line in fh:
                line = line.strip()
                if not line:
                    continue
                source_trials_count += 1
                try:
                    payload = json.loads(line)
                except json.JSONDecodeError:
                    continue
                action = str(payload.get("action") or "").strip()
                if action:
                    source_trial_actions.append(action)
                ref = str(payload.get("ref") or payload.get("candidate_ref") or "").strip()
                if ref:
                    source_trial_refs.append(ref)
                if action == "reject":
                    has_rejected_evidence = True
                    if ref:
                        rejected_refs.append(ref)
                    final_trial_status = "reject"
                if action == "keep":
                    has_kept_or_finalized = True
                    final_trial_status = "keep"
                    if ref:
                        kept_refs.append(ref)
                if action == "finalize":
                    finalized_source = True
                    has_kept_or_finalized = True
    last_message_size = child.last_message.stat().st_size if child.last_message.exists() else 0
    returncode = usage.get("returncode")
    agent_message_count = int(usage.get("agent_message_count", 0) or 0)

    warnings: list[str] = []
    problems: list[str] = []
    if not child.usage_summary.exists():
        problems.append("missing_usage_summary")
    elif returncode != 0:
        warnings.append(f"codex_returncode_{returncode}")
    if agent_message_count <= 0:
        problems.append("no_agent_message")
    if last_message_size <= 0:
        if str(usage.get("last_agent_message") or "").strip():
            warnings.append("last_message_materialized_from_stream")
        else:
            problems.append("empty_last_message")
    if not child.private_binary.exists():
        problems.append("missing_private_binary")
    rejected_final_workbench = final_trial_status == "reject"
    rejected_only_workbench = rejected_final_workbench and not metrics_path.exists()
    if not metrics_path.exists() and not rejected_only_workbench:
        problems.append("missing_metrics_json")
    if not child.implementation_diff.exists() and not rejected_only_workbench:
        problems.append("missing_implementation_diff")
    elif diff_size <= 0 and not rejected_only_workbench:
        if source_trials_count > 0:
            warnings.append("empty_final_diff_with_trial_evidence")
        else:
            problems.append("empty_implementation_diff")
    if not child.knowledge_card.exists():
        warnings.append("missing_knowledge_card")
    elif knowledge_size <= 0:
        warnings.append("empty_knowledge_card")
    if rejected_only_workbench:
        warnings.append("rejected_only_workbench")
    elif rejected_final_workbench:
        warnings.append("final_status_reject")

    status = "valid" if not problems else "invalid"
    return {
        "status": status,
        "problems": problems,
        "warnings": warnings,
        "usage": usage,
        "returncode": returncode,
        "agent_message_count": agent_message_count,
        "last_message_exists": child.last_message.exists(),
        "last_message_size": last_message_size,
        "private_binary_exists": child.private_binary.exists(),
        "metrics_path": metrics_path,
        "metrics_exists": metrics_path.exists(),
        "metric_summary": metric_summary,
        "diff_exists": child.implementation_diff.exists(),
        "diff_size": diff_size,
        "source_trials": source_trials,
        "source_trials_exists": source_trials.exists(),
        "source_trials_size": source_trials_size,
        "source_trials_count": source_trials_count,
        "source_trial_actions": source_trial_actions,
        "source_trial_refs": source_trial_refs,
        "rejected_refs": rejected_refs,
        "kept_refs": kept_refs,
        "has_rejected_evidence": has_rejected_evidence,
        "has_kept_or_finalized": has_kept_or_finalized,
        "finalized_source": finalized_source,
        "final_trial_status": final_trial_status,
        "rejected_final_workbench": rejected_final_workbench,
        "rejected_only_workbench": rejected_only_workbench,
        "knowledge_card": child.knowledge_card,
        "knowledge_exists": child.knowledge_card.exists(),
        "knowledge_size": knowledge_size,
    }


def child_artifact_status_block(
    *,
    runtime: Any,
    case_id: str,
    flow_variant: str,
    child: ChildRound,
) -> str:
    status = child_artifact_status(
        runtime=runtime,
        case_id=case_id,
        flow_variant=flow_variant,
        child=child,
    )
    metric_summary = status["metric_summary"]
    metric_lines = ["- metrics_summary: `missing`"]
    if metric_summary is not None:
        metric_lines = [
            "- metrics_summary:",
            f"  - HPWLg: `{metric_summary.hpwl_global}`",
            f"  - delta_HPWL_legalization: `{metric_summary.hpwl_stage_delta_legalization}`",
            f"  - delta_HPWL_improve: `{metric_summary.hpwl_stage_delta_improve}`",
            f"  - delta_HPWL_final: `{metric_summary.hpwl_stage_delta_final}`",
            f"  - stage_delta_pct_lg_ip_final: `{_fmt_stage_pct(metric_summary)}`",
            f"  - hpwl_after: `{metric_summary.hpwl_after}`",
            f"  - hpwl_delta: `{metric_summary.hpwl_delta}`",
            f"  - runtime_seconds: `{metric_summary.runtime_seconds}`",
            f"  - avg_disp: `{metric_summary.avg_disp}`",
            f"  - max_disp: `{metric_summary.max_disp}`",
            f"  - violations: `{metric_summary.violations or 'clean'}`",
        ]

    problems = status["problems"]
    problem_text = ", ".join(problems) if problems else "none"
    warnings = status.get("warnings") or []
    warning_text = ", ".join(warnings) if warnings else "none"
    return "\n".join(
        [
            f"- artifact_status: `{status['status']}`",
            f"- artifact_problems: `{problem_text}`",
            f"- artifact_warnings: `{warning_text}`",
            f"- codex_returncode: `{status['returncode']}`",
            f"- agent_message_count: `{status['agent_message_count']}`",
            f"- last_message_exists: `{status['last_message_exists']}`",
            f"- last_message_size: `{status['last_message_size']}`",
            f"- private_binary_exists: `{status['private_binary_exists']}`",
            f"- metrics_json_expected: `{status['metrics_path']}`",
            f"- metrics_json_exists: `{status['metrics_exists']}`",
            f"- implementation_diff_exists: `{status['diff_exists']}`",
            f"- implementation_diff_size: `{status['diff_size']}`",
            f"- source_trials: `{status['source_trials']}`",
            f"- source_trials_exists: `{status['source_trials_exists']}`",
            f"- source_trials_count: `{status['source_trials_count']}`",
            f"- source_trials_size: `{status['source_trials_size']}`",
            f"- source_trial_actions: `{','.join(status['source_trial_actions']) if status['source_trial_actions'] else 'none'}`",
            f"- kept_refs: `{'; '.join(status['kept_refs']) if status['kept_refs'] else 'none'}`",
            f"- rejected_refs: `{'; '.join(status['rejected_refs']) if status['rejected_refs'] else 'none'}`",
            f"- has_rejected_evidence: `{status['has_rejected_evidence']}`",
            f"- has_kept_or_finalized: `{status['has_kept_or_finalized']}`",
            f"- finalized_source: `{status['finalized_source']}`",
            f"- final_trial_status: `{status['final_trial_status']}`",
            f"- rejected_final_workbench: `{status['rejected_final_workbench']}`",
            f"- rejected_only_workbench: `{status['rejected_only_workbench']}`",
            f"- knowledge_card: `{status['knowledge_card']}`",
            f"- knowledge_card_exists: `{status['knowledge_exists']}`",
            f"- knowledge_card_size: `{status['knowledge_size']}`",
            *metric_lines,
        ]
    )


def write_child_artifact_status_json(
    *,
    path: Path,
    runtime: Any,
    case_id: str,
    flow_variant: str,
    child_rounds: list[ChildRound],
) -> Path:
    rows: list[dict[str, Any]] = []
    for child in child_rounds:
        status = child_artifact_status(
            runtime=runtime,
            case_id=case_id,
            flow_variant=flow_variant,
            child=child,
        )
        metric_summary = status.pop("metric_summary")
        status.pop("usage", None)
        status["metrics_path"] = str(status["metrics_path"])
        status["source_trials"] = str(status["source_trials"])
        status["knowledge_card"] = str(status["knowledge_card"])
        if metric_summary is not None:
            status["metrics_summary"] = {
                "HPWLg": metric_summary.hpwl_global,
                "delta_HPWL_legalization": metric_summary.hpwl_stage_delta_legalization,
                "delta_HPWL_improve": metric_summary.hpwl_stage_delta_improve,
                "delta_HPWL_final": metric_summary.hpwl_stage_delta_final,
                "delta_HPWL_legalization_percent": metric_summary.hpwl_stage_delta_legalization_percent,
                "delta_HPWL_improve_percent": metric_summary.hpwl_stage_delta_improve_percent,
                "delta_HPWL_final_percent": metric_summary.hpwl_stage_delta_final_percent,
                "hpwl_after": metric_summary.hpwl_after,
                "hpwl_delta": metric_summary.hpwl_delta,
                "runtime_seconds": metric_summary.runtime_seconds,
                "avg_disp": metric_summary.avg_disp,
                "max_disp": metric_summary.max_disp,
                "violations": metric_summary.violations,
            }
        else:
            status["metrics_summary"] = None
        rows.append(
            {
                "student_id": child.student_id,
                "route_label": child.route_label,
                "operation_id": child.operation_id,
                "run_tag": child.run_tag,
                **status,
            }
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
    for row in rows:
        print(
            "[optimize_case] child_artifacts "
            f"{row['student_id']} status={row['status']} "
            f"problems={','.join(row['problems']) or 'none'}"
        )
    return path
