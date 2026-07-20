#!/usr/bin/env python3
"""Teacher/child Codex entrypoint for case-specific DPL-Evolve optimization.

This script is intentionally an orchestration layer, not another evaluator.
It creates a teacher-authored optimization round for one case, writes bounded
child prompts, and optionally launches `codex exec` workers through the existing
`run_codex_exec.py` recorder.
"""
from __future__ import annotations

import json
import os
import shlex
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace
from typing import Any

AGENT_ROOT = Path(__file__).resolve().parents[2]
if str(AGENT_ROOT) not in sys.path:
    sys.path.insert(0, str(AGENT_ROOT))

from runtime_paths import clean_subprocess_env, resolve_runtime_paths
from scripts.repo.case_registry import get_case
from scripts.teacher_loop.cli import parse_args
from scripts.teacher_loop.common import (
    ChildRound,
    MetricSummary,
    RoundLogger,
    default_round_id,
    iter_name,
    student_workspace_paths,
    teacher_session_paths,
)
from scripts.teacher_loop.constants import CANONICAL_LINES


from scripts.teacher_loop.evidence import (
    baseline_suite_complete,
    child_artifact_status,
    discover_case_metrics,
    ensure_baseline_suite,
    materialize_source_ref,
    round_scoreboard,
    source_repo_ref_for_candidate,
    value_reference_anchor,
    best_round_candidate,
    write_baseline_artifacts_packet,
    write_child_artifact_status_json,
    write_design_characteristics_packet,
    write_peer_learning_packet,
)

from scripts.teacher_loop.execution import (
    append_lineage,
    build_codex_exec_command,
    commit_child_sources,
    inline_teacher_plan_for_children,
    resumable_thread_id,
    run_commands,
    seed_elite_children,
)
from scripts.teacher_loop.context_packets import (
    common_context,
    prior_iteration_context,
    write_case_feature_route_insight_packet,
    write_iteration_context,
    write_teacher_routing_context,
)
from scripts.teacher_loop.packet_builders import (
    build_packet,
    write_child_workspace_packet,
    write_review_artifacts_packet,
    write_round_readme,
)
from scripts.teacher_loop.prompt_rendering import (
    agent_prompt_warnings,
    child_prompt,
    compact_child_prompt,
    compact_teacher_prompt,
    compact_teacher_review_prompt,
    prompt_audit,
    teacher_prompt,
    teacher_review_prompt,
)
from scripts.teacher_loop.workspace_scripts import (
    validate_start_kind_seed,
)


def child_prompt_warning_lines(
    *,
    child_rounds: list[ChildRound],
    max_agent_prompt_bytes: int,
) -> list[str]:
    lines: list[str] = []
    for child in child_rounds:
        if not child.prompt_path.exists():
            lines.append(f"{child.student_id}: missing prompt {child.prompt_path}")
            continue
        text = child.prompt_path.read_text(encoding="utf-8", errors="replace")
        for warning in agent_prompt_warnings(
            text, max_agent_prompt_bytes=max_agent_prompt_bytes
        ):
            try:
                prompt_label = child.prompt_path.relative_to(AGENT_ROOT)
            except ValueError:
                prompt_label = child.prompt_path
            lines.append(
                f"{child.student_id}: {prompt_label}: {warning}"
            )
    return lines


def start_seed_calibration_command(
    *,
    runtime: Any,
    output_root: Path,
    case_id: str,
    flow_variant: str,
    round_id: str,
    threads: int,
    start_kinds: list[str],
    reuse_baseline: bool,
    dry_run: bool,
) -> list[str]:
    command = [
        str(runtime.agent_root / "scripts" / "calibration" / "calibrate_start_seeds.sh"),
        "--case",
        case_id,
        "--flow-variant",
        flow_variant,
        "--round-id",
        round_id,
        "--output-root",
        str(output_root),
        "--threads",
        str(threads),
        "--max-parallel",
        "1",
        "--start-kinds",
        ",".join(start_kinds),
    ]
    if reuse_baseline:
        command.append("--skip-baseline")
    if dry_run:
        command.append("--dry-run")
    return command


def runtime_with_round_outputs(runtime: Any, round_root: Path) -> Any:
    return SimpleNamespace(
        orfs_root=runtime.orfs_root,
        agent_root=runtime.agent_root,
        state_root=runtime.state_root,
        packet_dir=round_root / "packets",
        checkpoints_dir=round_root / "checkpoints",
        operations_dir=round_root / "checkpoints" / "operations",
    )


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    runtime = resolve_runtime_paths(
        anchor_file=__file__,
        agent_root_levels_up=2,
        script_name="optimize_case_with_codex.py",
    )
    get_case(args.case)  # Validate early.
    if not args.flow_variant:
        args.flow_variant = os.environ.get(
            "DPL_EVOLVE_FLOW_VARIANT",
            "<FLOW_VARIANT_WITH_3_4_PLACE_RESIZED_ODB>",
        )
    if args.launch and args.flow_variant.startswith("<"):
        raise SystemExit(
            "--launch requires --flow-variant or DPL_EVOLVE_FLOW_VARIANT; "
            "the dry-run placeholder is not runnable."
        )
    if args.launch and args.skip_baseline_preflight:
        raise SystemExit(
            "--skip-baseline-preflight is only valid for dry-run prompt audits. "
            "Real launched smoke/evolve rounds must run the three-line "
            "canonical baseline preflight."
        )

    iterations = max(1, args.iterations)
    start_iteration = max(1, min(args.start_iteration, iterations))
    if args.calibration_mode:
        if iterations != 1 or start_iteration != 1:
            raise SystemExit(
                "--calibration-mode is a single-iteration mechanism validation "
                "sweep. Use --iterations 1 --start-iteration 1 and scale with "
                "--children/--max-parallel instead."
            )
    if args.route_label:
        route_labels = args.route_label[: max(1, args.children)]
    else:
        route_labels = [
            f"teacher_assigned_{idx:02d}"
            for idx in range(1, max(1, args.children) + 1)
        ]
    teacher_model = args.teacher_model or args.model
    teacher_reasoning_effort = args.teacher_reasoning_effort or args.reasoning_effort
    student_model = args.student_model or args.model
    student_reasoning_effort = args.student_reasoning_effort or args.reasoning_effort
    if args.student_runtime_multiplier <= 0:
        raise SystemExit("--student-runtime-multiplier must be positive.")
    round_id = args.round_id or default_round_id(args.case)
    round_root = runtime.state_root / round_id
    runtime = runtime_with_round_outputs(runtime, round_root)
    round_dir = round_root / "teacher_rounds"
    round_dir.mkdir(parents=True, exist_ok=True)
    logger = RoundLogger(round_dir)
    teacher_session = teacher_session_paths(round_dir=round_dir, round_id=round_id)
    run_codex = runtime.agent_root / "scripts" / "run_codex_exec.py"
    child_cwd = (args.child_cwd or runtime.agent_root.parent).resolve()
    codex_add_dirs = [runtime.agent_root, runtime.orfs_root, runtime.state_root]
    dry_run = args.dry_run or not args.launch
    logger.event(
        "round",
        "start",
        round_id=round_id,
        case=args.case,
        flow_variant=args.flow_variant,
        start_kind=args.start_kind,
        iterations=iterations,
        start_iteration=start_iteration,
        children=len(route_labels),
        dry_run=dry_run,
        launch=args.launch,
        student_runtime_multiplier=args.student_runtime_multiplier,
        calibration_mode=args.calibration_mode,
    )

    all_commands: list[list[str]] = []
    manifest_iterations: list[dict[str, Any]] = []
    last_metrics: dict[str, MetricSummary | None] | None = None
    baseline_preflight_commands: list[list[str]] = []
    calibration_summary_path: Path | None = None
    calibration_tsv_path: Path | None = None
    calibration_commands: list[list[str]] = []

    prepare_command: list[str] | None = None
    core_configure_command: list[str] | None = None
    core_build_command: list[str] | None = None
    if args.prepare_workspace:
        prepare_command = [
            str(runtime.agent_root / "scripts" / "workspace" / "prepare_workspace.sh"),
            "--workspace-root",
            str(runtime.orfs_root),
        ]
        if args.prepare_force:
            prepare_command.append("--force")
        all_commands.append(prepare_command)
        if dry_run:
            logger.event(
                "prepare",
                "planned",
                command=" ".join(shlex.quote(part) for part in prepare_command),
            )
        else:
            logger.event("prepare", "start", command=prepare_command)
            rc = subprocess.run(prepare_command, env=clean_subprocess_env()).returncode
            if rc:
                logger.event("prepare", "failed", return_code=rc)
                return rc
            logger.event("prepare", "done")

    # Variant relink jobs depend on the common OpenROAD core matching the
    # prepared OpenROAD commit.  Build it once at the top level immediately
    # after prepare/patch so child agents only edit and relink their private
    # dpl_evolve sources.
    if (args.launch or args.prepare_workspace) and not args.skip_core_build:
        core_configure_command = [
            str(runtime.agent_root / "scripts" / "workspace" / "configure_openroad_core.sh"),
        ]
        core_build_command = [
            str(runtime.agent_root / "scripts" / "workspace" / "build_openroad_core.sh"),
            "--threads",
            str(args.threads),
        ]
        all_commands.extend([core_configure_command, core_build_command])
        if dry_run:
            logger.event(
                "core",
                "planned",
                configure=" ".join(
                    shlex.quote(part) for part in core_configure_command
                ),
                build=" ".join(shlex.quote(part) for part in core_build_command),
            )
        else:
            logger.event("core", "configure_start", command=core_configure_command)
            rc = subprocess.run(
                core_configure_command, env=clean_subprocess_env()
            ).returncode
            if rc:
                logger.event("core", "configure_failed", return_code=rc)
                return rc
            logger.event("core", "configure_done")
            logger.event("core", "build_start", command=core_build_command)
            rc = subprocess.run(core_build_command, env=clean_subprocess_env()).returncode
            if rc:
                logger.event("core", "build_failed", return_code=rc)
                return rc
            logger.event("core", "build_done")
    elif args.skip_core_build:
        logger.event("core", "skipped", reason="--skip-core-build")

    seed_ready, seed_path = validate_start_kind_seed(runtime, args.start_kind)
    if not seed_ready:
        logger.event(
            "prepare",
            "seed_missing",
            start_kind=args.start_kind,
            seed_path=seed_path,
            launch=args.launch,
        )
        if args.launch:
            raise SystemExit(
                f"Missing start-kind seed for {args.start_kind}: {seed_path}. "
                "Run optimize_case_with_codex.py with --prepare-workspace or "
                "run scripts/workspace/prepare_workspace.sh first."
            )

    if not args.skip_baseline_preflight:
        logger.event(
            "baseline",
            "preflight_start",
            refresh=args.refresh_baseline or not args.reuse_baseline_preflight,
            reuse=args.reuse_baseline_preflight,
        )
        baseline_metrics, baseline_preflight_commands = ensure_baseline_suite(
            runtime=runtime,
            case_id=args.case,
            flow_variant=args.flow_variant,
            threads=args.threads,
            round_id=round_id,
            refresh=args.refresh_baseline or not args.reuse_baseline_preflight,
            dry_run=dry_run,
        )
        last_metrics = baseline_metrics
        all_commands.extend(baseline_preflight_commands)
        logger.event(
            "baseline",
            "preflight_ready",
            command_count=len(baseline_preflight_commands),
            openroad_hpwl=None
            if baseline_metrics.get("openroad_dpl_flow") is None
            else baseline_metrics["openroad_dpl_flow"].hpwl_after,
            negotiation_hpwl=None
            if baseline_metrics.get("openroad_dpl_negotiation") is None
            else baseline_metrics["openroad_dpl_negotiation"].hpwl_after,
            evolve_command_hpwl=None
            if baseline_metrics.get("evolve_default") is None
            else baseline_metrics["evolve_default"].hpwl_after,
        )
    else:
        logger.event("baseline", "preflight_skipped")

    if args.calibrate_start_seeds:
        calibration_start_kinds = args.calibration_start_kind or [
            "framework",
            "diamond",
            "default_negotiation",
        ]
        calibration_command = start_seed_calibration_command(
            runtime=runtime,
            output_root=round_root,
            case_id=args.case,
            flow_variant=args.flow_variant,
            round_id=round_id,
            threads=args.threads,
            start_kinds=calibration_start_kinds,
            reuse_baseline=not args.refresh_baseline and not args.skip_baseline_preflight,
            dry_run=dry_run,
        )
        calibration_commands.append(calibration_command)
        all_commands.append(calibration_command)
        calibration_root = round_root / "start_seed_calibration"
        calibration_summary_path = calibration_root / "start_seed_calibration.md"
        calibration_tsv_path = calibration_root / "start_seed_calibration.tsv"
        if dry_run:
            logger.event(
                "calibration",
                "planned",
                command=" ".join(shlex.quote(part) for part in calibration_command),
                summary=calibration_summary_path,
            )
        else:
            logger.event("calibration", "start", command=calibration_command)
            rc = subprocess.run(
                calibration_command, env=clean_subprocess_env()
            ).returncode
            if rc:
                logger.event("calibration", "failed", return_code=rc)
                return rc
            logger.event(
                "calibration",
                "done",
                summary=calibration_summary_path,
                tsv=calibration_tsv_path,
            )
    else:
        logger.event("calibration", "skipped")

    for iteration in range(start_iteration, iterations + 1):
        iteration_dir = round_dir / iter_name(iteration)
        prompt_dir = iteration_dir / "prompts"
        packet_dir = iteration_dir / "packet"
        prompt_dir.mkdir(parents=True, exist_ok=True)
        packet_dir.mkdir(parents=True, exist_ok=True)
        for stale_packet in (
            "teacher_review_artifacts.md",
            "child_artifact_status.md",
            "operation_status.json",
        ):
            stale_path = packet_dir / stale_packet
            if stale_path.exists():
                stale_path.unlink()
        logger.event(
            "iteration",
            "generate_start",
            iteration=iter_name(iteration),
            iteration_dir=iteration_dir,
        )

        packet_path = build_packet(
            agent_root=runtime.agent_root,
            case_id=args.case,
            packet_dir=packet_dir,
        )
        metrics = discover_case_metrics(
            orfs_root=runtime.orfs_root,
            case_id=args.case,
            flow_variant=args.flow_variant,
        )
        baseline_artifacts_path = write_baseline_artifacts_packet(
            path=packet_dir / "baseline_artifacts.md",
            runtime=runtime,
            case_id=args.case,
            flow_variant=args.flow_variant,
            metrics=metrics,
        )
        start_seed_calibration_packet = packet_dir / "start_seed_calibration.md"
        if calibration_summary_path is not None and calibration_summary_path.exists():
            start_seed_calibration_packet.write_text(
                calibration_summary_path.read_text(encoding="utf-8", errors="replace"),
                encoding="utf-8",
            )
        else:
            start_seed_calibration_packet.write_text(
                "# Start Seed Calibration\n\n"
                "No start-kind calibration matrix was requested or completed for this round.\n",
                encoding="utf-8",
            )
        design_characteristics_path = write_design_characteristics_packet(
            path=packet_dir / "design_characteristics.md",
            case_id=args.case,
            flow_variant=args.flow_variant,
            metrics=metrics,
        )
        if (
            not dry_run
            and not args.skip_baseline_preflight
            and not baseline_suite_complete(metrics)
        ):
            missing = ", ".join(line for line in CANONICAL_LINES if metrics.get(line) is None)
            raise SystemExit(
                "Missing canonical baseline metrics after baseline preflight "
                f"({missing}); refusing to start Teacher planning without the "
                "full comparison anchor."
            )
        last_metrics = metrics
        prior_context = prior_iteration_context(
            runtime=runtime,
            round_dir=round_dir,
            round_id=round_id,
            iteration=iteration,
            strategies=route_labels,
        )
        scoreboard = round_scoreboard(
            runtime=runtime,
            case_id=args.case,
            flow_variant=args.flow_variant,
            round_id=round_id,
            metrics=metrics,
            before_iteration=iteration,
        )
        peer_learning_path = write_peer_learning_packet(
            path=packet_dir / "peer_learning.md",
            runtime=runtime,
            round_dir=round_dir,
            case_id=args.case,
            flow_variant=args.flow_variant,
            round_id=round_id,
            iteration=iteration,
            metrics=metrics,
            student_runtime_multiplier=args.student_runtime_multiplier,
        )
        elite_candidate = (
            best_round_candidate(
                runtime=runtime,
                case_id=args.case,
                flow_variant=args.flow_variant,
                round_id=round_id,
                baseline=value_reference_anchor(metrics)[1],
                require_baseline_beat=True,
                before_iteration=iteration,
            )
            if iteration > 1
            else None
        )
        elite_source_info = source_repo_ref_for_candidate(
            round_dir=round_dir,
            round_id=round_id,
            case_id=args.case,
            candidate=elite_candidate,
        )
        if elite_source_info is None:
            elite_source = None
        else:
            elite_repo, elite_ref = elite_source_info
            elite_source = materialize_source_ref(
                source_repo=elite_repo,
                source_ref=elite_ref,
                target_dir=round_dir
                / "materialized_sources"
                / f"{iter_name(iteration)}_elite"
                / "dpl_evolve",
            )
        case_feature_route_insight_packet = write_case_feature_route_insight_packet(
            path=packet_dir / "case_feature_route_insight_packet.md",
            agent_root=runtime.agent_root,
            case_id=args.case,
            flow_variant=args.flow_variant,
            metrics=metrics,
            iteration=iteration,
        )
        context = common_context(
            case_id=args.case,
            flow_variant=args.flow_variant,
            threads=args.threads,
            start_kind=args.start_kind,
            runtime=runtime,
            metrics=metrics,
            packet_path=packet_path,
            case_feature_route_insight_packet_path=case_feature_route_insight_packet,
            baseline_artifacts_path=baseline_artifacts_path,
            start_seed_calibration_path=start_seed_calibration_packet,
            design_characteristics_path=design_characteristics_path,
            peer_learning_path=peer_learning_path,
            round_id=round_id,
            iteration=iteration,
            prior_context=prior_context,
            scoreboard=scoreboard,
            elite_seed_source=elite_source,
            student_runtime_multiplier=args.student_runtime_multiplier,
        )
        full_context_path = write_iteration_context(
            iteration_dir=iteration_dir,
            context=context,
        )
        manual_teacher_guidance_path = round_dir / "manual_teacher_guidance.md"
        if manual_teacher_guidance_path.is_file():
            logger.event(
                "iteration",
                "manual_teacher_guidance_detected",
                iteration=iter_name(iteration),
                path=manual_teacher_guidance_path,
            )
        else:
            manual_teacher_guidance_path = None
        teacher_context_path = write_teacher_routing_context(
            iteration_dir=iteration_dir,
            case_id=args.case,
            flow_variant=args.flow_variant,
            threads=args.threads,
            start_kind=args.start_kind,
            round_id=round_id,
            iteration=iteration,
            full_context_path=full_context_path,
            case_feature_route_insight_packet_path=case_feature_route_insight_packet,
            manual_teacher_guidance_path=manual_teacher_guidance_path,
        )
        # Iteration 1 should build a global mechanism map from the knowledge
        # base.  Follow-up iterations can stay compact and evidence-driven.
        compact_followup_prompts = iteration > 1

        teacher_baseline_path = packet_dir / "baseline_routing_note.md"
        teacher_baseline_path.write_text(
            "\n".join(
                [
                    "# Baseline Routing Note",
                    "",
                    "Use `design_characteristics.md` as the primary baseline",
                    "summary for routing. Open the full baseline artifact packet",
                    "only if you need an exact metrics/log/result path.",
                    "",
                    f"- full packet: `{baseline_artifacts_path}`",
                    "",
                ]
            ),
            encoding="utf-8",
        )
        teacher_peer_path = packet_dir / "peer_learning_routing_note.md"
        teacher_peer_path.write_text(
            "\n".join(
                [
                    "# Peer Learning Routing Note",
                    "",
                    (
                        "No prior clean donors exist before `iter_01`. "
                        "Do not open the full peer-learning packet during the "
                        "initial routing pass unless a borrow decision is "
                        "explicitly required."
                        if iteration <= 1
                        else "Open the full peer-learning packet only if a "
                        "borrow or elite-continuation decision is actually "
                        "needed."
                    ),
                    "",
                    f"- full packet: `{peer_learning_path}`",
                    "",
                ]
            ),
            encoding="utf-8",
        )

        teacher_op = f"{round_id}_{iter_name(iteration)}_teacher_plan"
        teacher_path = prompt_dir / "teacher_plan.md"
        teacher_path.write_text(
            (
                compact_teacher_prompt(
                    context_path=teacher_context_path,
                    current_run_packet_path=packet_path,
                    design_characteristics_path=design_characteristics_path,
                    baseline_artifacts_path=teacher_baseline_path,
                    peer_learning_path=teacher_peer_path,
                    start_seed_calibration_path=start_seed_calibration_packet,
                    manual_teacher_guidance_path=manual_teacher_guidance_path,
                    strategies=route_labels,
                    children=len(route_labels),
                    iteration=iteration,
                    student_runtime_multiplier=args.student_runtime_multiplier,
                    calibration_mode=args.calibration_mode,
                )
                if compact_followup_prompts
                else teacher_prompt(
                    context_path=teacher_context_path,
                    current_run_packet_path=packet_path,
                    design_characteristics_path=design_characteristics_path,
                    baseline_artifacts_path=teacher_baseline_path,
                    peer_learning_path=teacher_peer_path,
                    start_seed_calibration_path=start_seed_calibration_packet,
                    manual_teacher_guidance_path=manual_teacher_guidance_path,
                    strategies=route_labels,
                    children=len(route_labels),
                    iteration=iteration,
                    student_runtime_multiplier=args.student_runtime_multiplier,
                    calibration_mode=args.calibration_mode,
                )
            ),
            encoding="utf-8",
        )
        teacher_resume_session_id = None
        if args.resume_sessions and iteration > 1:
            teacher_resume_session_id = resumable_thread_id(
                runtime=runtime,
                operation_id=f"{round_id}_{iter_name(iteration - 1)}_teacher_plan",
                expected_cwd=child_cwd,
                allow_unsafe_resume=args.resume_child_sessions,
            )
        teacher_cmd = build_codex_exec_command(
            run_codex=run_codex,
            operation_id=teacher_op,
            prompt_path=teacher_path,
            sandbox=args.sandbox,
            add_dirs=codex_add_dirs,
            model=teacher_model,
            reasoning_effort=teacher_reasoning_effort,
            profile=args.profile,
            resume_session_id=teacher_resume_session_id,
            session_identity=teacher_session.identity,
            session_state=teacher_session.session_state,
            session_env_file=teacher_session.session_env_file,
            operations_dir=runtime.operations_dir,
            cwd=child_cwd,
            skip_git_repo_check=True,
        )

        child_commands: list[list[str]] = []
        child_rounds: list[ChildRound] = []
        if not args.teacher_only:
            for idx, route_label in enumerate(route_labels, start=1):
                student_id = f"student_{idx:02d}"
                operation_id = f"{round_id}_{iter_name(iteration)}_{student_id}"
                run_tag = f"{operation_id}_{args.case}"
                prompt_path = prompt_dir / f"{student_id}.md"
                elite_seed_source = elite_source if idx == 1 else None
                workspace_packet = write_child_workspace_packet(
                    path=packet_dir / f"{student_id}_workspace.md",
                    runtime=runtime,
                    round_dir=round_dir,
                    teacher_operation_id=teacher_op,
                    teacher_prompt_path=teacher_path,
                    case_id=args.case,
                    flow_variant=args.flow_variant,
                    threads=args.threads,
                    start_kind=args.start_kind,
                    route_label=route_label,
                    child_id=idx,
                    iteration=iteration,
                    student_id=student_id,
                    run_tag=run_tag,
                    stable_workspace=True,
                    elite_seed_source=elite_seed_source,
                    peer_learning_path=peer_learning_path,
                    student_runtime_multiplier=args.student_runtime_multiplier,
                    baseline_metrics=metrics,
                )
                prompt_path.write_text(
                    (
                        compact_child_prompt(
                            context_path=full_context_path,
                            runtime=runtime,
                            round_dir=round_dir,
                            teacher_operation_id=teacher_op,
                            teacher_prompt_path=teacher_path,
                            case_id=args.case,
                            flow_variant=args.flow_variant,
                            threads=args.threads,
                            route_label=route_label,
                            child_id=idx,
                            iteration=iteration,
                            student_id=student_id,
                            run_tag=run_tag,
                            workspace_packet_path=workspace_packet,
                            peer_learning_path=peer_learning_path,
                            stable_workspace=True,
                            elite_seed_source=elite_seed_source,
                            student_runtime_multiplier=args.student_runtime_multiplier,
                        )
                        if compact_followup_prompts
                        else child_prompt(
                            context_path=full_context_path,
                            runtime=runtime,
                            round_dir=round_dir,
                            teacher_operation_id=teacher_op,
                            teacher_prompt_path=teacher_path,
                            case_id=args.case,
                            flow_variant=args.flow_variant,
                            threads=args.threads,
                            route_label=route_label,
                            child_id=idx,
                            iteration=iteration,
                            student_id=student_id,
                            run_tag=run_tag,
                            workspace_packet_path=workspace_packet,
                            peer_learning_path=peer_learning_path,
                            compact=compact_followup_prompts,
                            stable_workspace=True,
                            elite_seed_source=elite_seed_source,
                            student_runtime_multiplier=args.student_runtime_multiplier,
                        )
                    ),
                    encoding="utf-8",
                )
                append_lineage(
                    round_dir=round_dir,
                    student_id=student_id,
                    route_label=route_label,
                    iteration=iteration,
                    operation_id=operation_id,
                    run_tag=run_tag,
                    prompt_path=prompt_path,
                )
                paths = student_workspace_paths(
                    round_dir=round_dir,
                    iteration=iteration,
                    student_id=student_id,
                    stable_workspace=True,
                )
                child_round = ChildRound(
                    student_id=student_id,
                    route_label=route_label,
                    operation_id=operation_id,
                    run_tag=run_tag,
                    prompt_path=prompt_path,
                    workspace_packet=workspace_packet,
                    operation_dir=runtime.operations_dir / operation_id,
                    last_message=runtime.operations_dir
                    / operation_id
                    / "codex_last_message.txt",
                    usage_summary=runtime.operations_dir
                    / operation_id
                    / "codex_usage_summary.json",
                    stderr_log=runtime.operations_dir
                    / operation_id
                    / "codex_stderr.log",
                    events_jsonl=runtime.operations_dir
                    / operation_id
                    / "codex_events.jsonl",
                    student_workspace=paths.student_workspace,
                    variant_root=paths.variant_root,
                    dpl_src=paths.dpl_src,
                    private_binary=paths.private_binary,
                    implementation_diff=paths.implementation_diff,
                    knowledge_card=paths.knowledge_card,
                    source_branch=paths.source_branch,
                    source_base_record=paths.source_base_record,
                    source_commit_record=paths.source_commit_record,
                    session_state=paths.session_state,
                    session_env_file=paths.session_env_file,
                    elite_seed_source=elite_seed_source,
                )
                child_rounds.append(child_round)
                resume_session_id = None
                if args.resume_sessions and iteration > 1:
                    previous_op = f"{round_id}_{iter_name(iteration - 1)}_{student_id}"
                    resume_session_id = resumable_thread_id(
                        runtime=runtime,
                        operation_id=previous_op,
                        expected_cwd=child_cwd,
                        allow_unsafe_resume=args.resume_child_sessions,
                    )
                child_commands.append(
                    build_codex_exec_command(
                        run_codex=run_codex,
                        operation_id=operation_id,
                        prompt_path=prompt_path,
                        sandbox=args.sandbox,
                        add_dirs=codex_add_dirs,
                        model=student_model,
                        reasoning_effort=student_reasoning_effort,
                        profile=args.profile,
                        resume_session_id=resume_session_id,
                        session_identity=f"{round_id}:{student_id}",
                        session_state=paths.session_state,
                        session_env_file=paths.session_env_file,
                        operations_dir=runtime.operations_dir,
                        cwd=child_cwd,
                        skip_git_repo_check=True,
                    )
                )

        review_commands: list[list[str]] = []
        if not args.teacher_only:
            review_op = f"{round_id}_{iter_name(iteration)}_teacher_review"
            review_path = prompt_dir / "teacher_review.md"
            review_packet_path = write_review_artifacts_packet(
                path=packet_dir / "teacher_review_artifacts.md",
                runtime=runtime,
                case_id=args.case,
                flow_variant=args.flow_variant,
                child_rounds=child_rounds,
            )
            review_path.write_text(
                (
                    compact_teacher_review_prompt(
                        context_path=full_context_path,
                        review_packet_path=review_packet_path,
                        student_runtime_multiplier=args.student_runtime_multiplier,
                        calibration_mode=args.calibration_mode,
                    )
                    if compact_followup_prompts
                    else teacher_review_prompt(
                        context_path=full_context_path,
                        review_packet_path=review_packet_path,
                        student_runtime_multiplier=args.student_runtime_multiplier,
                        calibration_mode=args.calibration_mode,
                    )
                ),
                encoding="utf-8",
            )
            review_resume_session_id = None
            if args.resume_sessions and iteration > 1:
                review_resume_session_id = resumable_thread_id(
                    runtime=runtime,
                    operation_id=f"{round_id}_{iter_name(iteration - 1)}_teacher_review",
                    expected_cwd=child_cwd,
                    allow_unsafe_resume=args.resume_child_sessions,
                )
            review_commands.append(
                build_codex_exec_command(
                    run_codex=run_codex,
                    operation_id=review_op,
                    prompt_path=review_path,
                    sandbox=args.sandbox,
                    add_dirs=codex_add_dirs,
                    model=teacher_model,
                    reasoning_effort=teacher_reasoning_effort,
                    profile=args.profile,
                    resume_session_id=review_resume_session_id,
                    session_identity=teacher_session.identity,
                    session_state=teacher_session.session_state,
                    session_env_file=teacher_session.session_env_file,
                    operations_dir=runtime.operations_dir,
                    cwd=child_cwd,
                    skip_git_repo_check=True,
                )
            )

        iteration_commands = [teacher_cmd, *child_commands, *review_commands]
        all_commands.extend(iteration_commands)
        manifest_iterations.append(
            {
                "iteration": iter_name(iteration),
                "iteration_dir": str(iteration_dir),
                "start_kind": args.start_kind,
                "calibration_mode": args.calibration_mode,
                "packet": str(packet_path),
                "peer_learning_packet": str(peer_learning_path),
                "full_context": str(full_context_path),
                "compact_child_prompts": compact_followup_prompts,
                "teacher_plan_operation": teacher_op,
                "children": [
                    {
                        "student_id": child.student_id,
                        "route_label": child.route_label,
                        "operation_id": child.operation_id,
                        "run_tag": child.run_tag,
                        "prompt": str(child.prompt_path),
                        "workspace_packet": str(child.workspace_packet),
                        "student_workspace": str(child.student_workspace),
                        "private_dpl_src": str(child.dpl_src),
                        "source_branch": child.source_branch,
                        "source_repo": str(child.dpl_src),
                        "source_base_record": str(child.source_base_record),
                        "source_commit_record": str(child.source_commit_record),
                        "elite_seed_source": None
                        if child.elite_seed_source is None
                        else str(child.elite_seed_source),
                        "implementation_diff": str(child.implementation_diff),
                        "knowledge_card": str(child.knowledge_card),
                    }
                    for child in child_rounds
                ],
                "teacher_review_operation": None
                if args.teacher_only
                else f"{round_id}_{iter_name(iteration)}_teacher_review",
                "teacher_review_artifacts": None
                if args.teacher_only
                else str(packet_dir / "teacher_review_artifacts.md"),
                "child_artifact_status": None
                if args.teacher_only
                else str(packet_dir / "child_artifact_status.json"),
                "commands": iteration_commands,
            }
        )

        print(f"[optimize_case] round_dir: {round_dir}")
        print(f"[optimize_case] iteration: {iteration_dir}")
        print(f"[optimize_case] packet: {packet_path}")
        print(f"[optimize_case] prompts: {prompt_dir}")
        logger.event(
            "iteration",
            "generate_done",
            iteration=iter_name(iteration),
            context=full_context_path,
            peer_learning=peer_learning_path,
            prompts=prompt_dir,
            teacher_op=teacher_op,
            students=len(child_rounds),
            review=not args.teacher_only,
            elite_source=elite_source,
            command_count=len(iteration_commands),
        )

        if not dry_run:
            logger.event(
                "teacher",
                "plan_launch",
                iteration=iter_name(iteration),
                operation_id=teacher_op,
            )
            rc = run_commands([teacher_cmd], max_parallel=1, dry_run=False)
            if rc:
                logger.event(
                    "teacher",
                    "plan_failed",
                    iteration=iter_name(iteration),
                    operation_id=teacher_op,
                    return_code=rc,
                )
                return rc
            logger.event(
                "teacher",
                "plan_done",
                iteration=iter_name(iteration),
                operation_id=teacher_op,
            )
            if child_rounds:
                seed_elite_children(child_rounds)
                inline_teacher_plan_for_children(
                    child_rounds=child_rounds,
                    teacher_last_message=runtime.operations_dir
                    / teacher_op
                    / "codex_last_message.txt",
                )
                prompt_warnings = child_prompt_warning_lines(
                    child_rounds=child_rounds,
                    max_agent_prompt_bytes=args.prompt_audit_max_agent_bytes,
                )
                if prompt_warnings:
                    for warning in prompt_warnings:
                        print(
                            f"[optimize_case] prompt warning: {warning}",
                            file=sys.stderr,
                        )
                    logger.event(
                        "audit",
                        "child_prompt_prelaunch_warnings",
                        iteration=iter_name(iteration),
                        warnings=prompt_warnings,
                    )
                    if args.fail_on_prompt_audit:
                        logger.event(
                            "audit",
                            "prompt_audit_nonfatal",
                            iteration=iter_name(iteration),
                            reason="--fail-on-prompt-audit is deprecated; continuing",
                        )
                audit_json, audit_md, audit_warnings = prompt_audit(
                    round_dir=round_dir,
                    max_agent_prompt_bytes=args.prompt_audit_max_agent_bytes,
                )
                logger.event(
                    "audit",
                    "child_prompt_prelaunch_audit",
                    iteration=iter_name(iteration),
                    json=audit_json,
                    markdown=audit_md,
                    warnings=audit_warnings,
                )
                if audit_warnings and args.fail_on_prompt_audit:
                    logger.event(
                        "audit",
                        "prompt_audit_nonfatal",
                        iteration=iter_name(iteration),
                        reason="--fail-on-prompt-audit is deprecated; continuing",
                    )
            if child_commands:
                logger.event(
                    "student",
                    "launch",
                    iteration=iter_name(iteration),
                    count=len(child_commands),
                    max_parallel=max(1, args.max_parallel),
                )
                rc = run_commands(
                    child_commands,
                    max_parallel=max(1, args.max_parallel),
                    dry_run=False,
                )
                if rc:
                    print(
                        "[optimize_case] warning: one or more child workers "
                        "failed; continuing to Teacher review so failures can "
                        "be analyzed and turned into next-iteration guidance.",
                        file=sys.stderr,
                    )
                    logger.event(
                        "student",
                        "completed_with_failures",
                        iteration=iter_name(iteration),
                        return_code=rc,
                    )
                else:
                    logger.event(
                        "student",
                        "completed",
                        iteration=iter_name(iteration),
                        count=len(child_commands),
                    )
                commit_child_sources(
                    child_rounds=child_rounds,
                    round_dir=round_dir,
                    iteration=iteration,
                )
                write_child_artifact_status_json(
                    path=packet_dir / "child_artifact_status.json",
                    runtime=runtime,
                    case_id=args.case,
                    flow_variant=args.flow_variant,
                    child_rounds=child_rounds,
                )
                artifact_statuses = [
                    child_artifact_status(
                        runtime=runtime,
                        case_id=args.case,
                        flow_variant=args.flow_variant,
                        child=child,
                    )
                    for child in child_rounds
                ]
                valid_children = [
                    child.student_id
                    for child, status in zip(child_rounds, artifact_statuses)
                    if status.get("status") == "valid"
                ]
                reviewable_children = [
                    child.student_id
                    for child, status in zip(child_rounds, artifact_statuses)
                    if status.get("status") == "valid"
                    or status.get("rejected_only_workbench")
                ]
                if not valid_children and child_rounds:
                    if reviewable_children:
                        logger.event(
                            "student",
                            "rejected_only_reviewable",
                            iteration=iter_name(iteration),
                            students=",".join(reviewable_children),
                        )
                    else:
                        problem_text = "; ".join(
                            f"{child.student_id}: {','.join(status.get('problems') or [])}"
                            for child, status in zip(child_rounds, artifact_statuses)
                        )
                        print(
                            "[optimize_case] error: all child artifacts are invalid; "
                            f"stopping this iteration before Teacher review. {problem_text}",
                            file=sys.stderr,
                        )
                        logger.event(
                            "student",
                            "all_artifacts_invalid",
                            iteration=iter_name(iteration),
                            problems=problem_text,
                        )
                        return 1
                write_review_artifacts_packet(
                    path=packet_dir / "teacher_review_artifacts.md",
                    runtime=runtime,
                    case_id=args.case,
                    flow_variant=args.flow_variant,
                    child_rounds=child_rounds,
                )
            if review_commands:
                logger.event(
                    "teacher",
                    "review_launch",
                    iteration=iter_name(iteration),
                    operation_id=f"{round_id}_{iter_name(iteration)}_teacher_review",
                )
                rc = run_commands(review_commands, max_parallel=1, dry_run=False)
                if rc:
                    logger.event(
                        "teacher",
                        "review_failed",
                        iteration=iter_name(iteration),
                        return_code=rc,
                    )
                    return rc
                logger.event(
                    "teacher",
                    "review_done",
                    iteration=iter_name(iteration),
                    operation_id=f"{round_id}_{iter_name(iteration)}_teacher_review",
                )

    write_round_readme(
        runtime=runtime,
        round_dir=round_dir,
        case_id=args.case,
        flow_variant=args.flow_variant,
        start_kind=args.start_kind,
        strategies=route_labels,
        metrics=last_metrics or {},
        launch_commands=all_commands,
    )
    (round_dir / "manifest.json").write_text(
        json.dumps(
            {
                "round_id": round_id,
                "case": args.case,
                "flow_variant": args.flow_variant,
                "start_kind": args.start_kind,
                "round_dir": str(round_dir),
                "iterations": manifest_iterations,
                "teacher_session": {
                    "identity": teacher_session.identity,
                    "workspace": str(teacher_session.workspace),
                    "session_state": str(teacher_session.session_state),
                    "session_env_file": str(teacher_session.session_env_file),
                },
                "route_labels": route_labels,
                "model": args.model,
                "reasoning_effort": args.reasoning_effort,
                "teacher_model": teacher_model,
                "teacher_reasoning_effort": teacher_reasoning_effort,
                "student_model": student_model,
                "student_reasoning_effort": student_reasoning_effort,
                "resume_sessions": args.resume_sessions,
                "student_runtime_multiplier": args.student_runtime_multiplier,
                "calibration_mode": args.calibration_mode,
                "compact_after_first": True,
                "stable_student_workspaces": True,
                "child_cwd": str(child_cwd),
                "prepare_workspace": args.prepare_workspace,
                "prepare_force": args.prepare_force,
                "prepare_command": prepare_command,
                "core_configure_command": core_configure_command,
                "core_build_command": core_build_command,
                "baseline_preflight_commands": baseline_preflight_commands,
                "commands": all_commands,
                "dry_run": dry_run,
                "launch": args.launch,
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )

    if dry_run or args.audit_prompts:
        audit_json, audit_md, audit_warnings = prompt_audit(
            round_dir=round_dir,
            max_agent_prompt_bytes=args.prompt_audit_max_agent_bytes,
        )
        print(f"[optimize_case] prompt_audit_json: {audit_json}")
        print(f"[optimize_case] prompt_audit_md: {audit_md}")
        print(f"[optimize_case] prompt_audit_warnings: {audit_warnings}")
        logger.event(
            "audit",
            "prompt_audit",
            json=audit_json,
            markdown=audit_md,
            warnings=audit_warnings,
        )
        if audit_warnings and args.fail_on_prompt_audit:
            logger.event(
                "audit",
                "prompt_audit_nonfatal",
                reason="--fail-on-prompt-audit is deprecated; continuing",
            )

    if dry_run:
        rc = run_commands(
            all_commands,
            max_parallel=max(1, args.max_parallel),
            dry_run=True,
        )
        logger.event(
            "round",
            "dry_run_done",
            round_dir=round_dir,
            command_count=len(all_commands),
            return_code=rc,
        )
        return rc
    logger.event("round", "done", round_dir=round_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
