"""Generated packet builders for Teacher/Student rounds."""
from __future__ import annotations

import datetime as dt
import shlex
import subprocess
import sys
from pathlib import Path
from typing import Any

from runtime_paths import clean_subprocess_env
from scripts.teacher_loop.common import (
    ChildRound,
    MetricSummary,
    iter_name,
    render_prompt_template,
    round_id_from_dir,
    stable_candidate_ref,
    student_workspace_paths,
)
from scripts.teacher_loop.evidence import (
    baseline_artifact_block,
    child_artifact_status_block,
    expected_metrics_path,
    metric_table,
)
from scripts.teacher_loop.workspace_scripts import (
    child_evaluation_timeout,
    child_parent_source,
    write_student_workspace_scripts,
)


def build_packet(
    *,
    agent_root: Path,
    case_id: str,
    packet_dir: Path,
) -> Path:
    problem_path = subprocess.check_output(
        [
            sys.executable,
            str(agent_root / "scripts" / "repo" / "case_registry.py"),
            "--case",
            case_id,
            "--field",
            "problem",
        ],
        text=True,
        env=clean_subprocess_env(),
    ).strip()
    problem_dir = str(Path(problem_path).parent)
    subprocess.run(
        [
            sys.executable,
            str(agent_root / "adapters" / "science_codeevolve" / "kb_query.py"),
            "--problem_dir",
            problem_dir,
            "--out_dir",
            str(packet_dir),
        ],
        check=True,
        env=clean_subprocess_env(),
    )
    return packet_dir / "current_run_packet.md"


def write_child_workspace_packet(
    *,
    path: Path,
    runtime: Any,
    round_dir: Path,
    teacher_operation_id: str,
    teacher_prompt_path: Path,
    case_id: str,
    flow_variant: str,
    threads: int,
    start_kind: str,
    route_label: str,
    child_id: int,
    iteration: int,
    student_id: str,
    run_tag: str,
    stable_workspace: bool,
    elite_seed_source: Path | None,
    peer_learning_path: Path,
    student_runtime_multiplier: float,
    baseline_metrics: dict[str, MetricSummary | None] | None = None,
) -> Path:
    paths = student_workspace_paths(
        round_dir=round_dir,
        iteration=iteration,
        student_id=student_id,
        stable_workspace=stable_workspace,
    )
    parent_src = child_parent_source(
        runtime=runtime,
        round_dir=round_dir,
        iteration=iteration,
        student_id=student_id,
        stable_workspace=stable_workspace,
        elite_seed_source=elite_seed_source,
        start_kind=start_kind,
    )
    use_seed_override = elite_seed_source is not None or (
        iteration > 1 and not stable_workspace
    )
    timeout_seconds, timeout_note = child_evaluation_timeout(
        runtime=runtime,
        case_id=case_id,
        flow_variant=flow_variant,
        round_id=round_id_from_dir(round_dir),
        runtime_multiplier=student_runtime_multiplier,
        baseline_metrics=baseline_metrics,
    )
    script_paths = write_student_workspace_scripts(
        runtime=runtime,
        paths=paths,
        case_id=case_id,
        flow_variant=flow_variant,
        threads=threads,
        start_kind=start_kind,
        run_tag=run_tag,
        parent_src=parent_src,
        use_seed_override=use_seed_override,
        timeout_seconds=timeout_seconds,
        baseline_metrics=baseline_metrics,
    )
    lineage_path = round_dir / "students" / student_id / "lineage.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    metrics_path = expected_metrics_path(
        runtime=runtime,
        case_id=case_id,
        flow_variant=flow_variant,
        run_tag=run_tag,
    )
    agent_root = runtime.agent_root
    teacher_last_message = runtime.operations_dir / teacher_operation_id / "codex_last_message.txt"
    workspace_template = (
        "packets/student_workspace_packet_followup.md"
        if iteration > 1
        else "packets/student_workspace_packet.md"
    )
    path.write_text(
        render_prompt_template(
            workspace_template,
            student_id=student_id,
            child_id=child_id,
            route_label=route_label,
            start_kind=start_kind,
            iteration_name=iter_name(iteration),
            case_id=case_id,
            flow_variant=flow_variant,
            threads=threads,
            run_tag=run_tag,
            parent_src=parent_src,
            source_branch=paths.source_branch,
            source_candidate_ref=stable_candidate_ref(run_tag),
            agent_root=agent_root,
            optional_reference_index=agent_root / "family_variants" / "REFERENCE_INDEX.yaml",
            optional_legalm_reference=agent_root
            / "family_variants"
            / "legalm_guidance"
            / "README.md",
            optional_diamond_reference=agent_root
            / "family_variants"
            / "openroad_diamond"
            / "README.md",
            optional_negotiation_reference=agent_root
            / "family_variants"
            / "openroad_negotiation_nblg"
            / "README.md",
            optional_dpo_source_mechanisms_insight=agent_root
            / "knowledge"
            / "support"
            / "dpo"
            / "source_level_mechanisms.md",
            optional_strategy_inspiration_insight=agent_root
            / "knowledge"
            / "support"
            / "case_evolution"
            / "strategy_inspiration_by_design_feature.md",
            optional_case_feature_route_insight_packet=path.parent
            / "case_feature_route_insight_packet.md",
            optional_case_type_quality_mechanism_insight=agent_root
            / "knowledge"
            / "routing"
            / "case_feature_to_mechanism_route_map.md",
            optional_mechanism_reconstruction_roadmap=agent_root
            / "knowledge"
            / "routing"
            / "mechanism_reconstruction_roadmap.md",
            optional_openroad_native_handoff_insight=agent_root
            / "knowledge"
            / "support"
            / "dpo"
            / "openroad_native_handoff.md",
            skill_index=agent_root / "knowledge" / "index",
            skill_cards_dir=agent_root / "knowledge" / "skills",
            algorithms_dir=agent_root / "knowledge" / "algorithms",
            skill_query_script=agent_root / "scripts" / "repo" / "query_knowledge.py",
            query_knowledge_script=script_paths["query_knowledge"],
            lineage_path=lineage_path,
            student_workspace=paths.student_workspace,
            variant_root=paths.variant_root,
            dpl_src=paths.dpl_src,
            source_src_dir=paths.dpl_src / "src",
            source_optimization_dir=paths.dpl_src / "src" / "optimization",
            source_objective_dir=paths.dpl_src / "src" / "objective",
            private_binary=paths.private_binary,
            artifact_dir=paths.artifact_dir,
            candidate_metrics_summary_json=paths.candidate_metrics_summary_json,
            candidate_metrics_summary_md=paths.candidate_metrics_summary_md,
            failed_attempts=paths.artifact_dir / "failed_attempts.jsonl",
            implementation_diff=paths.implementation_diff,
            knowledge_card=paths.knowledge_card,
            source_base_record=paths.source_base_record,
            source_commit_record=paths.source_commit_record,
            session_state=paths.session_state,
            session_env_file=paths.session_env_file,
            command_script_dir=paths.student_workspace / "scripts",
            prepare_source_script=script_paths["prepare"],
            prepare_start_source_script=script_paths["prepare_start_source"],
            source_status_script=script_paths["source_status"],
            fetch_peer_source_script=script_paths["peer_source"],
            switch_start_branch_script=script_paths["switch_start_branch"],
            source_context_script=script_paths["source_context"],
            build_variant_script=script_paths["build"],
            fresh_build_script=script_paths["fresh_build"],
            evaluate_candidate_script=script_paths["evaluate"],
            report_candidate_metrics_script=script_paths["report_metrics"],
            keep_and_finalize_script=script_paths["keep_and_finalize"],
            trial_source_script=script_paths["trial"],
            finalize_source_script=script_paths["finalize"],
            after_edit_script=script_paths["after_edit"],
            peer_learning_path=peer_learning_path,
            teacher_prompt_path=teacher_prompt_path,
            teacher_operation_id=teacher_operation_id,
            teacher_plan_last_message=teacher_last_message,
            metrics_path=metrics_path,
            timeout_note=timeout_note,
            patch_rules_skill=agent_root / "skills" / "patch_rules.md",
            source_git_workflow_skill=agent_root / "skills" / "source_git_workflow.md",
            build_openroad_skill=agent_root / "skills" / "build_openroad.md",
            evaluate_run_skill=agent_root / "skills" / "evaluate_run.md",
            trace_logging_skill=agent_root / "skills" / "trace_logging.md",
        ),
        encoding="utf-8",
    )
    return path


def write_review_artifacts_packet(
    *,
    path: Path,
    runtime: Any,
    case_id: str,
    flow_variant: str,
    child_rounds: list[ChildRound],
) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    chunks = [render_prompt_template("packets/teacher_review_artifacts_header.md")]
    for child in child_rounds:
        chunks.append(
            render_prompt_template(
                "packets/teacher_review_artifacts_child.md",
                student_id=child.student_id,
                route_label=child.route_label,
                operation_id=child.operation_id,
                operation_dir=child.operation_dir,
                run_tag=child.run_tag,
                prompt_path=child.prompt_path,
                workspace_packet=child.workspace_packet,
                last_message=child.last_message,
                usage_summary=child.usage_summary,
                stderr_log=child.stderr_log,
                events_jsonl=child.events_jsonl,
                student_workspace=child.student_workspace,
                dpl_src=child.dpl_src,
                source_branch=child.source_branch,
                private_binary=child.private_binary,
                failed_attempts=child.implementation_diff.parent
                / "failed_attempts.jsonl",
                source_trials=child.implementation_diff.parent
                / "source_trials.jsonl",
                implementation_diff=child.implementation_diff,
                knowledge_card=child.knowledge_card,
                source_base_record=child.source_base_record,
                source_commit_record=child.source_commit_record,
                session_state=child.session_state,
                session_env_file=child.session_env_file,
                artifact_status_block=child_artifact_status_block(
                    runtime=runtime,
                    case_id=case_id,
                    flow_variant=flow_variant,
                    child=child,
                ),
            )
        )
    path.write_text("\n".join(chunks), encoding="utf-8")
    return path


def write_round_readme(
    *,
    runtime: Any,
    round_dir: Path,
    case_id: str,
    flow_variant: str,
    start_kind: str,
    strategies: list[str],
    metrics: dict[str, MetricSummary | None],
    launch_commands: list[list[str]],
) -> None:
    lines = [
        f"# Codex Optimization Round: {case_id}",
        "",
        f"- flow_variant: `{flow_variant}`",
        f"- start_kind: `{start_kind}`",
        f"- created_at: `{dt.datetime.now().isoformat()}`",
        "",
        "## Baseline Evidence",
        metric_table(metrics),
        "",
        "## Baseline Artifact Paths",
        baseline_artifact_block(
            runtime=runtime,
            case_id=case_id,
            flow_variant=flow_variant,
            metrics=metrics,
        ),
        "",
        "## Student Route Labels",
        *[
            f"- `{name}`: Teacher-assigned route label for bookkeeping."
            for name in strategies
        ],
        "",
        "## Launch Commands",
        "```bash",
    ]
    lines.extend(" ".join(shlex.quote(part) for part in command) for command in launch_commands)
    lines.extend(["```", ""])
    round_dir.joinpath("README.md").write_text("\n".join(lines), encoding="utf-8")

__all__ = [
    "build_packet",
    "write_child_workspace_packet",
    "write_review_artifacts_packet",
    "write_round_readme",
]
