"""Generated context and route-insight packet builders for Teacher/Student rounds."""
from __future__ import annotations

import json
import math
from pathlib import Path
from typing import Any

from scripts.teacher_loop.common import (
    MetricSummary,
    iter_name,
    render_prompt_template,
    round_id_from_dir,
)
from scripts.teacher_loop.constants import CANONICAL_LINES
from scripts.teacher_loop.evidence import (
    candidate_artifacts,
    candidate_artifact_problems,
    candidate_artifact_status_text,
    design_characteristics_block,
    discover_case_metrics,
    discover_round_candidate_metrics,
    expected_metrics_path,
    hpwl_runtime_gain,
    hpwl_runtime_gain_factors,
    metric_table,
    operation_artifact_block,
    parse_candidate_tag,
    runtime_ratio,
    stage_donor_signal,
    summarize_metrics,
    value_reference_anchor,
)
from scripts.teacher_loop.prompt_rendering import (
    format_multiplier,
    labeled_line_excerpt,
    markdown_section_excerpt,
    previous_teacher_review_text,
)


def peer_mechanism_summary(
    *,
    runtime: Any,
    round_dir: Path,
    round_id: str,
    case_id: str,
    summary: MetricSummary,
) -> tuple[str, str, str]:
    """Summarize one peer in bounded text for student prompts.

    Full source/diff/card paths stay in peer_learning.md.  Student prompts only
    need enough evidence to decide what to inspect.
    """
    artifacts = candidate_artifacts(
        runtime=runtime,
        round_dir=round_dir,
        round_id=round_id,
        case_id=case_id,
        candidate=summary,
    )
    text = ""
    if artifacts and artifacts.knowledge_card.exists():
        text = artifacts.knowledge_card.read_text(encoding="utf-8", errors="replace")
        mechanism = (
            markdown_section_excerpt(text, "Mechanism")
            or markdown_section_excerpt(text, "Route And Hypothesis")
            or markdown_section_excerpt(text, "Source Map")
        )
        lesson = markdown_section_excerpt(text, "Causal lesson")
        next_step = (
            markdown_section_excerpt(text, "Next Teacher Handoff")
            or markdown_section_excerpt(text, "Next Repair Or Pivot")
            or markdown_section_excerpt(text, "Next experiment")
        )
        if mechanism or lesson or next_step:
            return (
                mechanism,
                lesson,
                next_step,
            )
    if artifacts and artifacts.last_message.exists():
        text = artifacts.last_message.read_text(encoding="utf-8", errors="replace")
    mechanism = labeled_line_excerpt(
        text,
        ("Mechanism", "core idea", "Implemented", "Touched files"),
    )
    lesson = labeled_line_excerpt(
        text,
        ("Causal lesson", "Next uncertainty", "Next step", "Result"),
    )
    next_step = labeled_line_excerpt(
        text,
        (
            "Next Teacher Handoff",
            "Next Repair Or Pivot",
            "Next experiment",
            "Next uncertainty",
            "Next step",
        ),
    )
    return mechanism, lesson, next_step


def peer_briefing(
    *,
    runtime: Any,
    round_dir: Path,
    case_id: str,
    flow_variant: str,
    iteration: int,
    current_student: str,
    student_runtime_multiplier: float = 2.0,
) -> str:
    if iteration <= 1:
        return render_prompt_template("context/no_prior_peer_results.md").strip()

    round_id = round_id_from_dir(round_dir)
    previous = iter_name(iteration - 1)
    baseline_metrics: dict[str, MetricSummary | None] = {}
    for line in CANONICAL_LINES:
        run_tag = f"{round_id}_baseline_probe_{line}"
        baseline_metrics[line] = summarize_metrics(
            expected_metrics_path(
                runtime=runtime,
                case_id=case_id,
                flow_variant=flow_variant,
                run_tag=run_tag,
            )
        )
    if not baseline_metrics.get("openroad_dpl_flow"):
        baseline_metrics = discover_case_metrics(
            orfs_root=runtime.orfs_root,
            case_id=case_id,
            flow_variant=flow_variant,
        )
    _baseline_label, baseline = value_reference_anchor(baseline_metrics)
    metrics_by_student: dict[str, MetricSummary] = {}
    invalid_rows: list[tuple[str, MetricSummary, list[str]]] = []
    for summary in discover_round_candidate_metrics(
        orfs_root=runtime.orfs_root,
        case_id=case_id,
        flow_variant=flow_variant,
        round_id=round_id,
    ):
        iter_part, student_id, _route_label = parse_candidate_tag(
            round_id, case_id, summary.tag
        )
        if iter_part == previous and student_id:
            problems = candidate_artifact_problems(
                runtime=runtime,
                round_dir=round_dir,
                round_id=round_id,
                case_id=case_id,
                candidate=summary,
            )
            if problems:
                invalid_rows.append((student_id, summary, problems))
                continue
            metrics_by_student[student_id] = summary

    if not metrics_by_student:
        invalid_note = ""
        if invalid_rows:
            invalid_note = "\n\n" + render_prompt_template(
                "context/invalid_prior_metric_rows.md",
                invalid_rows="; ".join(
                    f"{student_id}={candidate_artifact_status_text(problems)}"
                    for student_id, _summary, problems in invalid_rows[:4]
                ),
            )
        return render_prompt_template(
            "context/no_valid_prior_peer_metrics.md",
            previous_iteration=previous,
            invalid_note=invalid_note,
        )

    def peer_rank(student: str) -> tuple[float, float]:
        summary = metrics_by_student[student]
        hpwl_rank = (
            -float(summary.hpwl_after)
            if summary.hpwl_after is not None
            else float("-inf")
        )
        gain = hpwl_runtime_gain(summary, baseline)
        gain_rank = gain if gain is not None else float("-inf")
        return hpwl_rank, gain_rank

    best_student = max(metrics_by_student, key=peer_rank)
    lines = [
        render_prompt_template(
            "context/prior_peer_briefing_header.md",
            previous_iteration=previous,
            best_student=best_student,
        ).rstrip()
    ]
    for student_id in sorted(metrics_by_student):
        summary = metrics_by_student[student_id]
        label = "you" if student_id == current_student else "peer"
        hpwl = "" if summary.hpwl_after is None else f"{summary.hpwl_after:.3f}"
        runtime_s = (
            "" if summary.runtime_seconds is None else f"{summary.runtime_seconds:.3f}s"
        )
        avg_disp = "" if summary.avg_disp is None else f"{summary.avg_disp:.4f}"
        max_disp = "" if summary.max_disp is None else f"{summary.max_disp:.4f}"
        violations = summary.violations or "clean"
        gain_factors = hpwl_runtime_gain_factors(summary, baseline)
        gain_text = "" if gain_factors is None else f"{gain_factors['score']:.3f}"
        h_text = "" if gain_factors is None else f"{gain_factors['H']:.4f}"
        r_text = "" if gain_factors is None else f"{gain_factors['R']:.4f}"
        delta_vs_ref = (
            None
            if baseline is None
            or baseline.hpwl_after is None
            or summary.hpwl_after is None
            else summary.hpwl_after - baseline.hpwl_after
        )
        delta_vs_ref_text = "" if delta_vs_ref is None else f"{delta_vs_ref:.3f}"
        runtime_ratio_value = runtime_ratio(summary, baseline)
        runtime_ratio_text = (
            "" if runtime_ratio_value is None else f"{runtime_ratio_value:.2f}x"
        )
        stage_pct = " / ".join(
            [
                "lg:"
                + (
                    ""
                    if summary.hpwl_stage_delta_legalization_percent is None
                    else f"{summary.hpwl_stage_delta_legalization_percent:.2f}%"
                ),
                "ip:"
                + (
                    ""
                    if summary.hpwl_stage_delta_improve_percent is None
                    else f"{summary.hpwl_stage_delta_improve_percent:.2f}%"
                ),
                "final:"
                + (
                    ""
                    if summary.hpwl_stage_delta_final_percent is None
                else f"{summary.hpwl_stage_delta_final_percent:.2f}%"
                ),
            ]
        )
        lines.append(
            "- {student} ({label}): analysis G_HR `{score}` HPWL_gain/runtime_penalty_pp `{h}/{r}`, "
            "HPWL `{hpwl}`, delta_vs_default `{delta_vs_ref}`, runtime `{runtime_s}` "
            "({runtime_ratio}), "
            "avg/max disp `{avg_disp}/{max_disp}`, legality `{violations}`; "
            "stage `{stage_pct}`, signal `{signal}`".format(
                student=student_id,
                label=label,
                score=gain_text,
                h=h_text,
                r=r_text,
                hpwl=hpwl,
                delta_vs_ref=delta_vs_ref_text,
                runtime_s=runtime_s,
                runtime_ratio=runtime_ratio_text,
                avg_disp=avg_disp,
                max_disp=max_disp,
                violations=violations,
                stage_pct=stage_pct,
                signal=stage_donor_signal(
                    summary,
                    baseline,
                    budget_multiplier=student_runtime_multiplier,
                ),
            )
        )
    if invalid_rows:
        lines.append("")
        lines.append(
            render_prompt_template(
                "context/invalid_prior_metric_rows.md",
                invalid_rows="; ".join(
                    f"{student_id}={candidate_artifact_status_text(problems)}"
                    for student_id, _summary, problems in invalid_rows[:4]
                ),
            ).rstrip()
        )
    return "\n".join(lines)


def prior_iteration_context(
    *,
    runtime: Any,
    round_dir: Path,
    round_id: str,
    iteration: int,
    strategies: list[str],
) -> str:
    if iteration <= 1:
        return render_prompt_template("context/no_previous_iteration.md").strip()

    previous = iteration - 1
    review_op = f"{round_id}_{iter_name(previous)}_teacher_review"
    blocks = [
        render_prompt_template(
            "context/prior_iteration_context_header.md",
            previous_iteration=iter_name(previous),
            previous_teacher_review_artifact=operation_artifact_block(
                runtime, review_op
            ),
            previous_teacher_review_text=previous_teacher_review_text(
                runtime=runtime,
                round_id=round_id,
                iteration=iteration,
                warn_chars=12000,
            ),
        ).rstrip()
    ]
    for idx, route_label in enumerate(strategies, start=1):
        student_id = f"student_{idx:02d}"
        child_op = f"{round_id}_{iter_name(previous)}_{student_id}"
        blocks.append(
            render_prompt_template(
                "context/prior_child_artifact.md",
                student_id=student_id,
                route_label=route_label,
                operation_artifact=operation_artifact_block(runtime, child_op),
                lineage_path=round_dir / "students" / student_id / "lineage.json",
            ).rstrip()
        )
    return "\n".join(blocks)


def write_iteration_context(
    *,
    iteration_dir: Path,
    context: str,
) -> Path:
    context_dir = iteration_dir / "context"
    context_dir.mkdir(parents=True, exist_ok=True)
    path = context_dir / "iteration_context.md"
    path.write_text(context, encoding="utf-8")
    return path


def write_teacher_routing_context(
    *,
    iteration_dir: Path,
    case_id: str,
    flow_variant: str,
    threads: int,
    start_kind: str,
    round_id: str,
    iteration: int,
    full_context_path: Path,
    case_feature_route_insight_packet_path: Path,
    manual_teacher_guidance_path: Path | None = None,
) -> Path:
    context_dir = iteration_dir / "context"
    context_dir.mkdir(parents=True, exist_ok=True)
    path = context_dir / "teacher_routing_context.md"
    path.write_text(
        render_prompt_template(
            "context/teacher_routing_context.md",
            case_id=case_id,
            flow_variant=flow_variant,
            threads=threads,
            start_kind=start_kind,
            round_id=round_id,
            iteration_name=iter_name(iteration),
            full_context_path=full_context_path,
            case_feature_route_insight_packet_path=case_feature_route_insight_packet_path,
            manual_teacher_guidance_path=(
                manual_teacher_guidance_path
                if manual_teacher_guidance_path is not None
                else "none"
            ),
        ),
        encoding="utf-8",
    )
    return path


def _float_value(value: Any) -> float | None:
    try:
        if value is None or value == "":
            return None
        value = float(value)
    except (TypeError, ValueError):
        return None
    if math.isnan(value) or math.isinf(value):
        return None
    return value


def _read_json(path: Path) -> dict[str, Any]:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return {}


def _markdown_section(text: str, heading_prefix: str) -> str:
    lines = text.splitlines()
    start: int | None = None
    for idx, line in enumerate(lines):
        if line.startswith(heading_prefix):
            start = idx + 1
            break
    if start is None:
        return ""
    end = len(lines)
    for idx in range(start, len(lines)):
        line = lines[idx]
        if line.startswith("## ") and not line.startswith("### "):
            end = idx
            break
    return "\n".join(lines[start:end]).strip()


def write_case_feature_route_insight_packet(
    *,
    path: Path,
    agent_root: Path,
    case_id: str,
    flow_variant: str,
    metrics: dict[str, MetricSummary | None],
    iteration: int,
) -> Path:
    """Write current evidence plus route insight without preselecting a route."""
    outline_path = (
        agent_root
        / "knowledge"
        / "routing"
        / "case_feature_to_mechanism_route_map.md"
    )
    roadmap_path = (
        agent_root
        / "knowledge"
        / "routing"
        / "mechanism_reconstruction_roadmap.md"
    )
    outline_text = outline_path.read_text(encoding="utf-8", errors="replace")
    roadmap_text = roadmap_path.read_text(encoding="utf-8", errors="replace")
    feature_map = _markdown_section(outline_text, "## Feature-Mechanism Map")
    route_map = _markdown_section(outline_text, "## Route Map")
    selection_rules = _markdown_section(outline_text, "## Blueprint Selection Rules")
    student_requirements = _markdown_section(outline_text, "## Student Packet Requirements")
    shared_anchors = _markdown_section(roadmap_text, "## Shared Source Anchors")
    anchor_label, anchor = value_reference_anchor(metrics)
    anchor_data = _read_json(anchor.metrics_path) if anchor is not None else {}
    design = anchor_data.get("design_metrics", {})
    disp = anchor_data.get("displacement", {})
    summary_data = (
        _read_json(anchor.metrics_path.parent / "legalize_summary.json")
        if anchor is not None
        else {}
    )
    utilization = _float_value(design.get("utilization"))
    movable = _float_value(disp.get("movable_instance_count"))
    fixed_count = _float_value(summary_data.get("fixed_instance_count"))
    placed_count = _float_value(summary_data.get("placed_instance_count"))
    negotiation = metrics.get("openroad_dpl_negotiation")

    lines = [
        "# Case Feature Evidence And Route Insight Packet",
        "",
        "This generated packet does not score, rank, or preselect blueprints.",
        "Teacher must compare the current metrics and physical features below",
        "against the case-type insight tables, then choose the start basin and",
        "mechanism route explicitly in the plan.  Treat all route rows as candidate",
        "mechanism insights, not as code-enforced assignments.",
        "",
        "## Current Evidence For Teacher Classification",
        "",
        f"- case: `{case_id}`",
        f"- flow_variant: `{flow_variant}`",
        f"- iteration: `{iter_name(iteration)}`",
        f"- value reference: `{anchor_label or 'missing'}`",
        f"- case size / movable cells: `{movable if movable is not None else 'unknown'}`",
        f"- density / utilization: `{utilization if utilization is not None else 'unknown'}`",
        f"- fixed instances: `{fixed_count if fixed_count is not None else 'unknown'}` "
        "(not a macro count; fixed cells may include taps/endcaps/fillers or true "
        "large blockages; macro means physically large instance)",
        f"- placed instances: `{placed_count if placed_count is not None else 'unknown'}`",
    ]
    if anchor is not None:
        lines.extend(
            [
                f"- HPWLg: `{anchor.hpwl_global}`",
                f"- delta HPWLlg%: `{anchor.hpwl_stage_delta_legalization_percent}`",
                f"- delta HPWLimprove%: `{anchor.hpwl_stage_delta_improve_percent}`",
                f"- delta HPWLfinal%: `{anchor.hpwl_stage_delta_final_percent}`",
                f"- runtime_seconds: `{anchor.runtime_seconds}`",
            ]
        )
    if negotiation is not None:
        lines.extend(
            [
                f"- negotiation final HPWL: `{negotiation.hpwl_after}`",
                f"- negotiation runtime_seconds: `{negotiation.runtime_seconds}`",
            ]
        )
    lines.extend(
        [
            "",
            "## Teacher Classification Checklist",
            "",
            "- Decide whether the dominant HPWL source is legalizer producer quality,",
            "  handoff lifetime, DPO exact-consumer strength, post-DPO closure, or",
            "  canonical preservation.",
            "- Decide from evidence whether negotiation is actually the useful producer;",
            "  do not infer a negotiation start only from large improve-stage recovery.",
            "- Compare density, movable count, runtime, true macro/large-blockage or",
            "  row-fragmentation evidence, and stage-wise HPWL movement against the",
            "  route tables below.",
            "- Assign at least one Student to the strongest feature-matched full",
            "  producer-handoff-consumer chain, plus diversity routes when evidence is",
            "  ambiguous or the current route has plateaued.",
            "- For every selected blueprint, include a complete-chain audit in the",
            "  Student packet: start basin, producer payload, handoff state, exact",
            "  consumer, post-consumer closure, required counters, already-live links,",
            "  missing links, and the link this Student will implement or repair.",
            "- Use non-warm-start knowledge only after naming a route or failure",
            "  bucket; it should sharpen a mechanism, diagnosis, liveness counter,",
            "  or cost-control repair, not rank routes for Teacher.",
            "",
        ]
    )
    for title, content in (
        ("Feature-Mechanism Map", feature_map),
        ("Route Map", route_map),
        ("Blueprint Selection Rules", selection_rules),
        ("Student Packet Requirements", student_requirements),
    ):
        if content:
            lines.extend([f"## {title}", "", content, ""])
    lines.extend(
        [
            "## Detailed Roadmap Access",
            "",
            f"- roadmap file: `{roadmap_path}`",
            "- Teacher should open or query only the roadmap section for the route it",
            "  selects after reading the evidence above.",
            "- Student should use the Teacher-assigned route section for concrete source",
            "  handles, implementation order, counters, proof criteria, and first repair.",
            "- For an explicit blueprint assignment, Student should query the",
            "  exact stack with `08_query_knowledge.sh --stage teacher_review --q \"Blueprint D+A stack\"`",
            "  or replace `D+A` with the assigned single-letter blueprint before editing and report the completeness",
            "  audit after evaluation.",
            "",
        ]
    )
    if shared_anchors:
        lines.extend(["## Shared Source Anchors", "", shared_anchors, ""])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")
    return path


def common_context(
    *,
    case_id: str,
    flow_variant: str,
    threads: int,
    start_kind: str,
    runtime: Any,
    metrics: dict[str, MetricSummary | None],
    packet_path: Path,
    case_feature_route_insight_packet_path: Path,
    baseline_artifacts_path: Path,
    start_seed_calibration_path: Path,
    design_characteristics_path: Path,
    peer_learning_path: Path,
    round_id: str,
    iteration: int,
    prior_context: str,
    scoreboard: str,
    elite_seed_source: Path | None,
    student_runtime_multiplier: float,
) -> str:
    elite_text = (
        render_prompt_template("context/no_elite_seed.md").strip()
        if elite_seed_source is None
        else render_prompt_template(
            "context/elite_seed_context.md",
            elite_seed_source=elite_seed_source,
        )
    )
    context_template = (
        "context/common_context_followup.md"
        if iteration > 1
        else "context/common_context.md"
    )
    return render_prompt_template(
        context_template,
        case_id=case_id,
        flow_variant=flow_variant,
        threads=threads,
        start_kind=start_kind,
        round_id=round_id,
        iteration_name=iter_name(iteration),
        orfs_root=runtime.orfs_root,
        agent_root=runtime.agent_root,
        state_root=runtime.state_root,
        packet_path=packet_path,
        case_feature_route_insight_packet=case_feature_route_insight_packet_path,
        design_characteristics=design_characteristics_block(
            case_id=case_id,
            flow_variant=flow_variant,
            metrics=metrics,
        ),
        design_characteristics_path=design_characteristics_path,
        dpl_evolve_src=runtime.orfs_root
        / "tools"
        / "OpenROAD"
        / "src"
        / "dpl_evolve"
        / "src",
        legalm_guidance=runtime.agent_root / "family_variants" / "legalm_guidance",
        openroad_diamond=runtime.agent_root / "family_variants" / "openroad_diamond",
        openroad_negotiation_nblg=runtime.agent_root
        / "family_variants"
        / "openroad_negotiation_nblg",
        dreamplace_abacus=runtime.agent_root / "family_variants" / "dreamplace_abacus",
        dense_legalizer_routing_insight=runtime.agent_root
        / "knowledge"
        / "support"
        / "case_evolution"
        / "dense_legalizer_routing.md",
        strategy_inspiration_insight=runtime.agent_root
        / "knowledge"
        / "support"
        / "case_evolution"
        / "strategy_inspiration_by_design_feature.md",
        case_type_quality_mechanism_insight=runtime.agent_root
        / "knowledge"
        / "routing"
        / "case_feature_to_mechanism_route_map.md",
        mechanism_reconstruction_roadmap=runtime.agent_root
        / "knowledge"
        / "routing"
        / "mechanism_reconstruction_roadmap.md",
        dpo_source_mechanisms_insight=runtime.agent_root
        / "knowledge"
        / "support"
        / "dpo"
        / "source_level_mechanisms.md",
        openroad_native_handoff_insight=runtime.agent_root
        / "knowledge"
        / "support"
        / "dpo"
        / "openroad_native_handoff.md",
        skill_index=runtime.agent_root / "knowledge" / "index",
        skill_query_script=runtime.agent_root / "scripts" / "repo" / "query_knowledge.py",
        skill_cards_dir=runtime.agent_root / "knowledge" / "skills",
        algorithms_dir=runtime.agent_root / "knowledge" / "algorithms",
        teacher_coaching_skill=runtime.agent_root
        / "skills"
        / "teacher_peer_coaching.md",
        metric_table=metric_table(metrics),
        scoreboard=scoreboard,
        elite_text=elite_text,
        baseline_artifacts_path=baseline_artifacts_path,
        start_seed_calibration_path=start_seed_calibration_path,
        peer_learning_path=peer_learning_path,
        prior_context=prior_context,
        student_runtime_multiplier=format_multiplier(student_runtime_multiplier),
    )


__all__ = [
    "common_context",
    "peer_briefing",
    "peer_mechanism_summary",
    "prior_iteration_context",
    "write_case_feature_route_insight_packet",
    "write_iteration_context",
    "write_teacher_routing_context",
]
