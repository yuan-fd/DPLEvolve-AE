from __future__ import annotations

import argparse
from pathlib import Path
import sys


AGENT_ROOT = Path(__file__).resolve().parents[2]
if str(AGENT_ROOT) not in sys.path:
    sys.path.insert(0, str(AGENT_ROOT))

from scripts.teacher_loop.constants import START_KINDS


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Teacher/child Codex entrypoint for optimizing one DPL case."
    )
    parser.add_argument("--case", default="jpeg_nangate45")
    parser.add_argument(
        "--flow-variant",
        default=None,
        help=(
            "ORFS FLOW_VARIANT containing 3_4_place_resized.odb. If omitted, "
            "uses DPL_EVOLVE_FLOW_VARIANT or a placeholder for dry-run prompts."
        ),
    )
    parser.add_argument("--round-id")
    parser.add_argument(
        "--start-kind",
        choices=START_KINDS,
        default="framework",
        help=(
            "Initial dpl_evolve source seed for student variants. "
            "`framework` starts from the constrained top-level framework with "
            "LEGALM-style producer/frontier support and bounded DPO handoff; "
            "`diamond` starts from the clean dpl_evolve/base implementation; "
            "`default_negotiation` starts from a framework-compatible source "
            "layout with negotiation enabled by default. "
            "Only these three prepared start branches are active for normal "
            "Teacher/Student workspaces."
        ),
    )
    parser.add_argument(
        "--prepare-workspace",
        action="store_true",
        help=(
            "Run scripts/workspace/prepare_workspace.sh before baseline preflight. This "
            "anchors ORFS/OpenROAD, applies supported patches, and refreshes "
            "start-kind seed snapshots."
        ),
    )
    parser.add_argument(
        "--prepare-force",
        action="store_true",
        help="Pass --force to prepare_workspace.sh when --prepare-workspace is used.",
    )
    parser.add_argument(
        "--skip-core-build",
        action="store_true",
        help=(
            "Do not configure/build the shared OpenROAD core inside this round. "
            "Use this when an outer batch launcher has already built the core."
        ),
    )
    parser.add_argument(
        "--iterations",
        type=int,
        default=1,
        help="Teacher/child/review loop iterations to generate or launch.",
    )
    parser.add_argument("--children", type=int, default=4)
    parser.add_argument(
        "--route-label",
        action="append",
        dest="route_label",
        default=[],
        help=(
            "Optional bookkeeping label for a student slot. It does not assign "
            "an algorithm direction; Teacher still chooses directions from "
            "context and source evidence."
        ),
    )
    parser.add_argument("--threads", type=int, default=10)
    parser.add_argument("--max-parallel", type=int, default=2)
    parser.add_argument(
        "--student-runtime-multiplier",
        type=float,
        default=2.0,
        help=(
            "Hard timeout multiplier for each student OpenROAD "
            "legalize/improve/mirror flow run, relative to the canonical "
            "openroad_dpl_flow metrics.json:runtime_seconds. Post-run metrics "
            "collection is not timeout-wrapped. Default 2.0 is a tolerant "
            "runtime budget for bounded search, not a target to maximize."
        ),
    )
    parser.add_argument("--model")
    parser.add_argument(
        "--reasoning-effort",
        choices=["low", "medium", "high", "xhigh"],
        help="Passed to codex exec as model_reasoning_effort.",
    )
    parser.add_argument("--teacher-model", help="Model for Teacher planning/review.")
    parser.add_argument(
        "--teacher-reasoning-effort",
        choices=["low", "medium", "high", "xhigh"],
        help="Reasoning effort for Teacher planning/review.",
    )
    parser.add_argument("--student-model", help="Model for Student workers.")
    parser.add_argument(
        "--student-reasoning-effort",
        choices=["low", "medium", "high", "xhigh"],
        help="Reasoning effort for Student implementation workers.",
    )
    parser.add_argument(
        "--resume-sessions",
        action="store_true",
        help=(
            "Resume prior Codex sessions only when the previous operation "
            "summary proves the session is safe to continue."
        ),
    )
    parser.add_argument(
        "--resume-child-sessions",
        action="store_true",
        help=(
            "Unsafe opt-in: for iteration > 1, resume each student's previous "
            "Codex exec session."
        ),
    )
    parser.add_argument("--profile")
    parser.add_argument(
        "--sandbox",
        default="danger-full-access",
        help=(
            "Sandbox mode passed to codex exec. The default bypasses Codex's "
            "bubblewrap sandbox because older bwrap builds on this machine do "
            "not support the options required by workspace-write."
        ),
    )
    parser.add_argument(
        "--child-cwd",
        type=Path,
        help="Cwd for Teacher/Student Codex sessions. Defaults to the parent workspace.",
    )
    parser.add_argument("--teacher-only", action="store_true")
    parser.add_argument(
        "--skip-baseline-preflight",
        action="store_true",
        help=(
            "Dry-run only: do not run/refresh the canonical baseline suite "
            "before prompt planning."
        ),
    )
    parser.add_argument(
        "--refresh-baseline",
        action="store_true",
        help="Deprecated alias: the canonical baseline suite is refreshed by default.",
    )
    parser.add_argument(
        "--reuse-baseline-preflight",
        action="store_true",
        help="Reuse canonical baseline metrics when all rows already exist.",
    )
    parser.add_argument(
        "--calibrate-start-seeds",
        dest="calibrate_start_seeds",
        action="store_true",
        help=(
            "Before Teacher planning, build/evaluate prepared start-kind seed "
            "sources and write a start_seed_calibration packet for initial "
            "seed donor evidence."
        ),
    )
    parser.add_argument(
        "--calibrate-start-patches",
        dest="calibrate_start_seeds",
        action="store_true",
        help=(
            "Deprecated alias for --calibrate-start-seeds. It evaluates "
            "prepared seed branches; Students do not apply patch files."
        ),
    )
    parser.add_argument(
        "--calibration-start-kind",
        action="append",
        dest="calibration_start_kind",
        choices=[
            "framework",
            "diamond",
            "default_negotiation",
        ],
        default=[],
        help=(
            "Start-kind seed to include in --calibrate-start-seeds. "
            "Repeatable. Default: framework, diamond, and default_negotiation."
        ),
    )
    parser.add_argument(
        "--calibration-mode",
        action="store_true",
        help=(
            "Use the single-iteration mechanism calibration Teacher prompt. "
            "Teacher assigns one independent mechanism per child, spreads "
            "starts across framework/diamond/default_negotiation and stages, "
            "and reviews results as mechanism evidence rather than evolution "
            "continuation. Requires --iterations 1."
        ),
    )
    parser.add_argument(
        "--level1-evidence",
        type=Path,
        help=(
            "Frozen paper-level Level 1 method/source-start evidence packet. "
            "Level 2 target search copies this immutable packet into every "
            "iteration context; it never updates the packet."
        ),
    )
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument(
        "--audit-prompts",
        action="store_true",
        help="Write prompt_audit.json/PROMPT_AUDIT.md after prompt generation.",
    )
    parser.add_argument(
        "--prompt-audit-max-agent-bytes",
        type=int,
        default=24000,
        help=(
            "Warn when an agent-facing prompt is larger than this many bytes. "
            "This is a context-hygiene guard, not an instruction to remove "
            "useful task constraints."
        ),
    )
    parser.add_argument(
        "--fail-on-prompt-audit",
        action="store_true",
        help=(
            "Deprecated no-op kept for old launch scripts; prompt audit "
            "warnings are recorded but never stop an experiment."
        ),
    )
    parser.add_argument("--launch", action="store_true")
    parser.add_argument(
        "--start-iteration",
        type=int,
        default=1,
        help="Start generating/launching from this 1-based iteration.",
    )
    return parser.parse_args(argv)
