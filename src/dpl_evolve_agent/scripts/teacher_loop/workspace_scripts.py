"""Student workspace source/start/helper-script API."""
from __future__ import annotations

import math
import os
import shlex
import sys
from pathlib import Path
from typing import Any

from scripts.teacher_loop.common import (
    MetricSummary,
    StudentWorkspace,
    render_prompt_template,
    stable_candidate_ref,
    student_workspace_paths,
)
from scripts.teacher_loop.evidence import (
    expected_metrics_path,
    summarize_metrics,
)
from scripts.teacher_loop.prompt_rendering import format_multiplier


def start_kind_source(runtime: Any, start_kind: str) -> Path:
    if start_kind == "framework":
        return runtime.state_root / "seed_sources" / "framework_dpl_evolve"
    if start_kind == "diamond":
        return runtime.state_root / "seed_sources" / "diamond_dpl_evolve"
    if start_kind == "source_topk_diamond":
        return runtime.state_root / "seed_sources" / "source_topk_diamond_dpl_evolve"
    if start_kind == "default_negotiation":
        return runtime.state_root / "seed_sources" / "default_negotiation_dpl_evolve"
    if start_kind == "prepared":
        return runtime.orfs_root / "tools" / "OpenROAD" / "src" / "dpl_evolve"
    raise ValueError(f"unsupported start_kind: {start_kind}")


def validate_start_kind_seed(runtime: Any, start_kind: str) -> tuple[bool, Path]:
    source = start_kind_source(runtime, start_kind)
    return (source / "CMakeLists.txt").is_file(), source


def child_evaluation_timeout(
    *,
    runtime: Any,
    case_id: str,
    flow_variant: str,
    round_id: str,
    runtime_multiplier: float,
    baseline_metrics: dict[str, MetricSummary | None] | None = None,
) -> tuple[int | None, str]:
    """Return the hard DPL flow timeout from canonical reported runtime."""
    summary = None
    if baseline_metrics is not None:
        summary = baseline_metrics.get("openroad_dpl_flow")
    runtime_s = summary.runtime_seconds if summary is not None else None
    if runtime_s is not None and runtime_s > 0:
        baseline_path = summary.metrics_path
    else:
        baseline_path = None

    baseline_tag = f"{round_id}_baseline_probe_openroad_dpl_flow"
    fallback_path = expected_metrics_path(
        runtime=runtime,
        case_id=case_id,
        flow_variant=flow_variant,
        run_tag=baseline_tag,
    )
    if runtime_s is None or runtime_s <= 0:
        baseline = summarize_metrics(fallback_path)
        runtime_s = baseline.runtime_seconds if baseline is not None else None
        baseline_path = fallback_path
    if runtime_s is None or runtime_s <= 0:
        return (
            None,
            "Unavailable: canonical openroad_dpl_flow reported runtime is missing. "
            f"Run baseline preflight first. Expected metrics: `{fallback_path}`",
        )
    multiplier_label = format_multiplier(runtime_multiplier)
    timeout_s = max(1, int(math.ceil(runtime_s * runtime_multiplier)))
    return (
        timeout_s,
        "DPL flow timeout: "
        f"`{timeout_s}s` = {multiplier_label}x canonical openroad_dpl_flow reported runtime "
        f"`{runtime_s:.3f}s` from the canonical baseline `metrics.json:runtime_seconds`. "
        "The timeout is applied only to the OpenROAD legalize/improve/mirror flow run; "
        "post-run metrics/evaluator collection is not timeout-wrapped, so timeout failures "
        "should still write metrics.json failure evidence when possible. Runtime below the "
        "limit is acceptable when it buys cached, capped, explainable HPWL-source search; "
        "DPL-flow-timeout runs are non-promotable.",
    )


def child_parent_source(
    *,
    runtime: Any,
    round_dir: Path,
    iteration: int,
    student_id: str,
    stable_workspace: bool,
    elite_seed_source: Path | None,
    start_kind: str,
) -> Path:
    if elite_seed_source is not None:
        return elite_seed_source
    if iteration > 1:
        if stable_workspace:
            return (
                round_dir
                / "students"
                / student_id
                / "workspace"
                / "variant"
                / "dpl_evolve"
            )
        return student_workspace_paths(
            round_dir=round_dir,
            iteration=iteration - 1,
            student_id=student_id,
            stable_workspace=stable_workspace,
        ).dpl_src
    return start_kind_source(runtime, start_kind)


def write_executable_script(path: Path, text: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    path.chmod(0o755)
    return path


def write_student_workspace_scripts(
    *,
    runtime: Any,
    paths: StudentWorkspace,
    case_id: str,
    flow_variant: str,
    threads: int,
    start_kind: str,
    run_tag: str,
    parent_src: Path,
    use_seed_override: bool,
    timeout_seconds: int | None,
    baseline_metrics: dict[str, MetricSummary | None] | None = None,
) -> dict[str, Path]:
    """Write fixed helper scripts into the Student workspace.

    The Student should spend context on source behavior, not on reconstructing
    relink/evaluator/git boilerplate.  These scripts are intentionally verbose
    in names and short in logic so failures are easy to report.
    """
    script_dir = paths.student_workspace / "scripts" / paths.source_branch.split("/")[-1]
    metrics_path = expected_metrics_path(
        runtime=runtime,
        case_id=case_id,
        flow_variant=flow_variant,
        run_tag=run_tag,
    )
    baseline_metrics_path = ""
    if baseline_metrics is not None:
        baseline_summary = baseline_metrics.get("openroad_dpl_flow")
        if baseline_summary is not None:
            baseline_metrics_path = str(baseline_summary.metrics_path)
    seed_lines = (
        '  --seed-src "$PARENT_SRC" \\\n'
        '  --force \\\n'
        if use_seed_override
        else ""
    )
    legalize_timeout_arg = (
        f'  --legalize-timeout-seconds {timeout_seconds} \\\n'
        if timeout_seconds is not None
        else ""
    )

    common_vars = f"""AGENT_ROOT={shlex.quote(str(runtime.agent_root))}
ORFS_ROOT={shlex.quote(str(runtime.orfs_root))}
STATE_ROOT={shlex.quote(str(runtime.state_root))}
DPL_EVOLVE_PYTHON={shlex.quote(os.environ.get("DPL_EVOLVE_PYTHON", sys.executable))}
VARIANT_ROOT={shlex.quote(str(paths.variant_root))}
DPL_SRC={shlex.quote(str(paths.dpl_src))}
PRIVATE_BINARY={shlex.quote(str(paths.private_binary))}
ARTIFACT_DIR={shlex.quote(str(paths.artifact_dir))}
SOURCE_BRANCH={shlex.quote(paths.source_branch)}
ITERATION_BRANCH={shlex.quote(f"iteration/{run_tag}")}
CANDIDATE_SOURCE_REF={shlex.quote(stable_candidate_ref(run_tag))}
SOURCE_BASE_RECORD={shlex.quote(str(paths.source_base_record))}
SOURCE_COMMIT_RECORD={shlex.quote(str(paths.source_commit_record))}
METRICS_SUMMARY_JSON={shlex.quote(str(paths.candidate_metrics_summary_json))}
METRICS_SUMMARY_MD={shlex.quote(str(paths.candidate_metrics_summary_md))}
BUILD_PROVENANCE_JSON="$ARTIFACT_DIR/candidate_build_provenance.json"
EVALUATION_START_JSON="$ARTIFACT_DIR/candidate_evaluation_start.json"
EVALUATION_PROVENANCE_JSON="$ARTIFACT_DIR/candidate_evaluation_provenance.json"
IMPLEMENTATION_DIFF={shlex.quote(str(paths.implementation_diff))}
KNOWLEDGE_CARD={shlex.quote(str(paths.knowledge_card))}
THREADS={shlex.quote(str(threads))}
CASE_ID={shlex.quote(case_id)}
FLOW_VARIANT={shlex.quote(flow_variant)}
RUN_TAG={shlex.quote(run_tag)}
START_KIND={shlex.quote(start_kind)}
PARENT_SRC={shlex.quote(str(parent_src))}
METRICS_PATH={shlex.quote(str(metrics_path))}
BASELINE_METRICS_PATH={shlex.quote(baseline_metrics_path)}

export DPL_EVOLVE_AGENT_ROOT="$AGENT_ROOT"
export ORFS_ROOT
export DPL_EVOLVE_STATE_ROOT="$STATE_ROOT"
export DPL_EVOLVE_PYTHON

ensure_dpl_git_repo() {{
  local top
  top="$(git -C "$DPL_SRC" rev-parse --show-toplevel 2>/dev/null || true)"
  top="$(realpath -m "$top")"
  if [[ "$top" != "$(realpath -m "$DPL_SRC")" ]]; then
    echo "[ERROR] private source is not an independent git repo: $DPL_SRC top=$top" >&2
    exit 2
  fi
}}

acquire_workspace_git_lock() {{
  if [[ "${{DPL_EVOLVE_WORKSPACE_LOCK_HELD:-0}}" == "1" ]]; then
    return 0
  fi
  mkdir -p "$VARIANT_ROOT"
  exec 9>"$VARIANT_ROOT/.workspace_git.lock"
  if ! flock -n 9; then
    echo "[ERROR] another generated workspace helper is already mutating this private source." >&2
    echo "[INFO] Run dependent helpers serially: prepare/switch/build/evaluate/trial/finalize." >&2
    exit 2
  fi
}}

assert_no_git_index_lock() {{
  if [[ -e "$DPL_SRC/.git/index.lock" ]]; then
    echo "[ERROR] git index lock exists in private source: $DPL_SRC/.git/index.lock" >&2
    echo "[INFO] Do not run prepare/switch/build/evaluate/checkpoint/preserve/commit helpers in parallel." >&2
    echo "[INFO] If no helper process is active, remove the stale lock manually after inspecting processes." >&2
    exit 2
  fi
}}
"""

    scripts = {
        "prepare": script_dir / "00_prepare_source.sh",
        "prepare_start_source": script_dir / "01_prepare_start_source.sh",
        "source_status": script_dir / "02_source_status.sh",
        "peer_source": script_dir / "03_fetch_peer_source.sh",
        "switch_start_branch": script_dir / "04_switch_start_branch.sh",
        "source_context": script_dir / "05_source_context.sh",
        "query_knowledge": script_dir / "08_query_knowledge.sh",
        "build": script_dir / "10_build_variant.sh",
        "fresh_build": script_dir / "11_build_variant_fresh.sh",
        "evaluate": script_dir / "20_evaluate_candidate.sh",
        "report_metrics": script_dir / "21_report_candidate_metrics.sh",
        "keep_and_finalize": script_dir / "22_keep_and_finalize.sh",
        "trial": script_dir / "25_trial_source.sh",
        "finalize": script_dir / "30_finalize_source.sh",
        "after_edit": script_dir / "run_after_edit.sh",
    }

    write_executable_script(
        scripts["prepare"],
        f"""#!/usr/bin/env bash
	set -euo pipefail
{common_vars}
acquire_workspace_git_lock
mkdir -p "$VARIANT_ROOT" "$ARTIFACT_DIR"
assert_no_git_index_lock
"$AGENT_ROOT/scripts/workspace/create_variant_start.sh" \\
  --orfs-root "$ORFS_ROOT" \\
  --variant-root "$VARIANT_ROOT" \\
  --start-kind "$START_KIND" \\
  --dpl-src "$DPL_SRC" \\
{seed_lines}  --git-branch "$SOURCE_BRANCH"
ensure_dpl_git_repo
assert_no_git_index_lock
current_branch="$(git -C "$DPL_SRC" branch --show-current)"
if [[ "$current_branch" != "$SOURCE_BRANCH" ]]; then
  echo "[ERROR] expected source branch $SOURCE_BRANCH, got $current_branch" >&2
  exit 2
fi

seed_for_kind() {{
  case "$1" in
    framework)
      printf '%s\\n' "$STATE_ROOT/seed_sources/framework_dpl_evolve"
      ;;
    diamond)
      printf '%s\\n' "$STATE_ROOT/seed_sources/diamond_dpl_evolve"
      ;;
    source_topk_diamond)
      printf '%s\\n' "$STATE_ROOT/seed_sources/source_topk_diamond_dpl_evolve"
      ;;
    default_negotiation)
      printf '%s\\n' "$STATE_ROOT/seed_sources/default_negotiation_dpl_evolve"
      ;;
    *)
      return 2
      ;;
  esac
}}

materialize_start_branch() {{
  local kind="$1"
  local branch="start/$kind"
  local seed
  local tmp_branch
  seed="$(seed_for_kind "$kind")"
  if [[ ! -f "$seed/CMakeLists.txt" ]]; then
    echo "[WARN] cannot prepare $branch; missing seed: $seed" >&2
    return 0
  fi
  tmp_branch="__dpl_evolve_seed_${{kind}}_$$"
  git -C "$DPL_SRC" switch --orphan "$tmp_branch" >/dev/null
  git -C "$DPL_SRC" rm -r --cached . >/dev/null 2>&1 || true
  find "$DPL_SRC" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {{}} +
  rsync -a --delete --exclude='.git' "$seed/" "$DPL_SRC/"
  if [[ "$kind" == "diamond" ]]; then
    local rel
    for rel in \\
      src/DifferentialGuidance.cpp \\
      src/EvolveLegalizer.cpp \\
      src/EvolveNegotiationRepair.cpp \\
      src/LegalmFullLegalization.cpp \\
      src/LegalmGuidance.cpp \\
      src/LegalmRowAssignment.cpp \\
      src/LegalmTechPenalty.cpp \\
      src/StudentAlgorithm.cpp; do
      if [[ -f "$DPL_SRC/$rel" ]]; then
        continue
      fi
      mkdir -p "$(dirname "$DPL_SRC/$rel")"
      cat > "$DPL_SRC/$rel" <<'STUB'
// Build-surface compatibility translation unit for lean start seeds.
STUB
    done
  fi
  git -C "$DPL_SRC" add -A
  git -C "$DPL_SRC" commit -m "Seed $branch source" >/dev/null
  git -C "$DPL_SRC" branch -M "$branch"
  echo "[INFO] prepared_start_branch=$branch commit=$(git -C "$DPL_SRC" rev-parse HEAD)"
}}

prepare_start_branches() {{
  if [[ -n "$(git -C "$DPL_SRC" status --short)" ]]; then
    echo "[WARN] private source is dirty; skip start branch materialization" >&2
    return 0
  fi
  local return_branch
  return_branch="$(git -C "$DPL_SRC" branch --show-current)"
  if [[ "$return_branch" != "$SOURCE_BRANCH" ]]; then
    echo "[WARN] current branch is $return_branch, expected $SOURCE_BRANCH; skip start branch materialization" >&2
    return 0
  fi
  for kind in framework diamond source_topk_diamond default_negotiation; do
    materialize_start_branch "$kind"
  done
  git -C "$DPL_SRC" switch "$SOURCE_BRANCH" >/dev/null
  echo "[INFO] available_start_branches:"
  git -C "$DPL_SRC" for-each-ref --format='[INFO]   %(refname:short) %(objectname:short)' refs/heads/start/
}}

prepare_start_branches
git -C "$DPL_SRC" switch -C "$SOURCE_BRANCH" >/dev/null
if [[ -s "$SOURCE_BASE_RECORD" ]]; then
  base_commit="$(head -n 1 "$SOURCE_BASE_RECORD")"
  if git -C "$DPL_SRC" rev-parse --verify "$base_commit^{{commit}}" >/dev/null 2>&1; then
    git -C "$DPL_SRC" branch -f "$ITERATION_BRANCH" "$base_commit" >/dev/null
  else
    echo "[WARN] recorded base commit is missing; refreshing iteration base" >&2
    git -C "$DPL_SRC" branch -f "$ITERATION_BRANCH" HEAD >/dev/null
    git -C "$DPL_SRC" rev-parse HEAD > "$SOURCE_BASE_RECORD"
  fi
else
  git -C "$DPL_SRC" branch -f "$ITERATION_BRANCH" HEAD >/dev/null
  git -C "$DPL_SRC" rev-parse HEAD > "$SOURCE_BASE_RECORD"
fi
echo "[INFO] source=$DPL_SRC"
echo "[INFO] dev_branch=$SOURCE_BRANCH"
echo "[INFO] iteration_branch=$ITERATION_BRANCH"
echo "[INFO] base_commit=$(cat "$SOURCE_BASE_RECORD")"
		""",
    )

    write_executable_script(
        scripts["prepare_start_source"],
        f"""#!/usr/bin/env bash
set -euo pipefail
{common_vars}
SCRIPT_DIR="$(cd "$(dirname "${{BASH_SOURCE[0]}}")" && pwd)"

kind=""
usage() {{
  cat >&2 <<'USAGE'
Usage:
  01_prepare_start_source.sh --kind <prepared-start-kind>

Atomically run source preparation and switch to the Teacher-assigned prepared
start branch. Use this instead of launching 00_prepare_source.sh and
04_switch_start_branch.sh separately when Teacher assigns a start point.
USAGE
}}

while [[ "$#" -gt 0 ]]; do
  case "$1" in
    --kind)
      kind="$2"
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "[ERROR] unknown argument: $1" >&2
      usage
      exit 2
      ;;
  esac
done

if [[ -z "$kind" ]]; then
  echo "[ERROR] missing --kind" >&2
  usage
  exit 2
fi

acquire_workspace_git_lock
assert_no_git_index_lock
DPL_EVOLVE_WORKSPACE_LOCK_HELD=1 "$SCRIPT_DIR/00_prepare_source.sh"
assert_no_git_index_lock
DPL_EVOLVE_WORKSPACE_LOCK_HELD=1 "$SCRIPT_DIR/04_switch_start_branch.sh" --kind "$kind"
""",
    )

    write_executable_script(
        scripts["source_status"],
        f"""#!/usr/bin/env bash
set -euo pipefail
{common_vars}
if [[ ! -d "$DPL_SRC/.git" ]]; then
  echo "[INFO] source repo is not prepared yet: $DPL_SRC"
  echo "[INFO] run 00_prepare_source.sh or 01_prepare_start_source.sh first"
  exit 0
fi
ensure_dpl_git_repo
echo "[INFO] source=$DPL_SRC"
echo "[INFO] expected_source_branch=$SOURCE_BRANCH"
echo "[INFO] current_branch=$(git -C "$DPL_SRC" branch --show-current)"
echo "[INFO] head=$(git -C "$DPL_SRC" rev-parse --short HEAD) $(git -C "$DPL_SRC" log -1 --pretty=%s)"
if [[ -s "$SOURCE_BASE_RECORD" ]]; then
  echo "[INFO] source_base_commit=$(head -n 1 "$SOURCE_BASE_RECORD")"
else
  echo "[INFO] source_base_commit=<not recorded yet>"
fi
if git -C "$DPL_SRC" show-ref --verify --quiet "refs/heads/$CANDIDATE_SOURCE_REF"; then
  echo "[INFO] stable_candidate_ref=$CANDIDATE_SOURCE_REF $(git -C "$DPL_SRC" rev-parse --short "$CANDIDATE_SOURCE_REF")"
else
  echo "[INFO] stable_candidate_ref=$CANDIDATE_SOURCE_REF <not pinned yet>"
fi
echo "[INFO] source status:"
git -C "$DPL_SRC" status --short
echo "[INFO] dev branches:"
git -C "$DPL_SRC" for-each-ref --format='[INFO]   %(refname:short) %(objectname:short) %(subject)' "refs/heads/dev/" | tail -20 || true
echo "[INFO] iteration refs:"
git -C "$DPL_SRC" for-each-ref --format='[INFO]   %(refname:short) %(objectname:short) %(subject)' "refs/heads/iteration/" | tail -20 || true
echo "[INFO] candidate refs:"
git -C "$DPL_SRC" for-each-ref --format='[INFO]   %(refname:short) %(objectname:short) %(subject)' "refs/heads/candidate/" | tail -20 || true
echo "[INFO] kept refs:"
git -C "$DPL_SRC" for-each-ref --format='[INFO]   %(refname:short) %(objectname:short) %(subject)' "refs/heads/kept/" | tail -20 || true
echo "[INFO] rejected refs:"
git -C "$DPL_SRC" for-each-ref --format='[INFO]   %(refname:short) %(objectname:short) %(subject)' "refs/heads/rejected/" | tail -20 || true
echo "[INFO] start branches:"
git -C "$DPL_SRC" for-each-ref --format='[INFO]   %(refname:short) %(objectname:short) %(subject)' "refs/heads/start/" || true
""",
    )

    write_executable_script(
        scripts["peer_source"],
        f"""#!/usr/bin/env bash
set -euo pipefail
{common_vars}
peer_repo=""
peer_ref=""
label=""
paths=()

usage() {{
  cat >&2 <<'USAGE'
Usage:
  03_fetch_peer_source.sh --peer-repo <repo> --peer-ref <ref> [--label <name>] [--path <src/path> ...]

Fetch another Student's evaluated source ref into this local repo as
peer/<label>, then write a diff summary under this iteration's artifacts.
This helper does not modify the working tree. Use it to inspect and migrate
selected mechanisms onto the current dev branch.
USAGE
}}

while [[ "$#" -gt 0 ]]; do
  case "$1" in
    --peer-repo)
      peer_repo="$2"
      shift 2
      ;;
    --peer-ref)
      peer_ref="$2"
      shift 2
      ;;
    --label)
      label="$2"
      shift 2
      ;;
    --path)
      paths+=("$2")
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "[ERROR] unknown argument: $1" >&2
      usage
      exit 2
      ;;
  esac
done

if [[ -z "$peer_repo" || -z "$peer_ref" ]]; then
  echo "[ERROR] --peer-repo and --peer-ref are required" >&2
  usage
  exit 2
fi

acquire_workspace_git_lock
ensure_dpl_git_repo
assert_no_git_index_lock
peer_repo="$(realpath -m "$peer_repo")"
if [[ ! -d "$peer_repo/.git" ]]; then
  echo "[ERROR] peer repo is not a local git repo: $peer_repo" >&2
  exit 2
fi

if [[ -z "$label" ]]; then
  label="$(basename "$(dirname "$peer_repo")")_$(basename "$peer_ref")"
fi
safe_label="$("$DPL_EVOLVE_PYTHON" - "$label" <<'PY'
import re
import sys

label = sys.argv[1]
safe = re.sub(r"[^A-Za-z0-9._-]+", "_", label).strip("._-")
print(safe or "peer_source")
PY
)"
peer_branch="peer/$safe_label"
git -C "$DPL_SRC" check-ref-format --branch "$peer_branch" >/dev/null
git -C "$DPL_SRC" fetch --no-tags "$peer_repo" "+$peer_ref:refs/heads/$peer_branch" >/dev/null
peer_commit="$(git -C "$DPL_SRC" rev-parse "$peer_branch")"

out_dir="$ARTIFACT_DIR/peer_imports"
mkdir -p "$out_dir"
summary="$out_dir/${{safe_label}}.summary.txt"
diff_path="$out_dir/${{safe_label}}.diff"

{{
  echo "[INFO] source=$DPL_SRC"
  echo "[INFO] current_branch=$(git -C "$DPL_SRC" branch --show-current)"
  echo "[INFO] local_peer_ref=$peer_branch"
  echo "[INFO] peer_repo=$peer_repo"
  echo "[INFO] peer_ref=$peer_ref"
  echo "[INFO] peer_commit=$peer_commit"
  if [[ "${{#paths[@]}}" -gt 0 ]]; then
    printf '[INFO] path_filters='
    printf '%s ' "${{paths[@]}}"
    printf '\\n'
  else
    echo "[INFO] path_filters=<all>"
  fi
  echo "[INFO] diff stat:"
  git -C "$DPL_SRC" diff --stat HEAD.."$peer_branch" -- "${{paths[@]}}" || true
  echo "[INFO] changed files:"
  git -C "$DPL_SRC" diff --name-status HEAD.."$peer_branch" -- "${{paths[@]}}" || true
}} | tee "$summary"

git -C "$DPL_SRC" diff --no-ext-diff HEAD.."$peer_branch" -- "${{paths[@]}}" > "$diff_path" || true
echo "[INFO] peer_summary=$summary"
echo "[INFO] peer_diff=$diff_path"
echo "[INFO] inspect_file_example=git -C '$DPL_SRC' show '$peer_branch:src/Optdp.cpp'"
echo "[INFO] migrate selectively on $SOURCE_BRANCH, then build/evaluate before final commit."
""",
    )

    write_executable_script(
        scripts["switch_start_branch"],
        f"""#!/usr/bin/env bash
set -euo pipefail
{common_vars}
acquire_workspace_git_lock
ensure_dpl_git_repo
assert_no_git_index_lock

kind=""
target_ref=""
usage() {{
  cat >&2 <<'USAGE'
Usage:
  04_switch_start_branch.sh --kind <prepared-start-kind>
  04_switch_start_branch.sh --ref <local-ref-or-commit>

Reset the dev source branch to one prepared local start branch or any local
kept/rejected/candidate/peer ref. Examples:
  framework
  diamond
  default_negotiation
  kept/<run_tag>/<label>
  rejected/<run_tag>/<label>

Run this after 00_prepare_source.sh and before source edits when Teacher
assigns a different start point for this Student. This is intentionally the
explicit hard-reset helper; reject itself does not reset dev.
USAGE
}}

while [[ "$#" -gt 0 ]]; do
  case "$1" in
    --kind)
      kind="$2"
      shift 2
      ;;
    --ref)
      target_ref="$2"
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "[ERROR] unknown argument: $1" >&2
      usage
      exit 2
      ;;
  esac
done

if [[ -n "$kind" && -n "$target_ref" ]]; then
  echo "[ERROR] use only one of --kind or --ref" >&2
  usage
  exit 2
fi
if [[ -z "$kind" && -z "$target_ref" ]]; then
  echo "[ERROR] expected --kind or --ref" >&2
  usage
  exit 2
fi
if [[ -n "$kind" ]]; then
  if [[ "$kind" == */* ]]; then
    echo "[ERROR] expected --kind to be one prepared start branch suffix without '/'" >&2
    usage
    exit 2
  fi
  target_ref="start/$kind"
fi

if [[ -n "$(git -C "$DPL_SRC" status --short)" ]]; then
  echo "[ERROR] private source has uncommitted changes; commit/reject/keep/finalize before reset." >&2
  git -C "$DPL_SRC" status --short >&2
  exit 2
fi
target_commit="$(git -C "$DPL_SRC" rev-parse --verify "$target_ref^{{commit}}" 2>/dev/null || true)"
if [[ -z "$target_commit" ]]; then
  echo "[ERROR] missing reset target ref: $target_ref" >&2
  echo "[INFO] existing refs:" >&2
  git -C "$DPL_SRC" for-each-ref --format='  %(refname:short) %(objectname:short)' refs/heads/start/ refs/heads/kept/ refs/heads/rejected/ refs/heads/candidate/ refs/heads/peer/ >&2 || true
  exit 2
fi

git -C "$DPL_SRC" switch -C "$SOURCE_BRANCH" "$target_commit" >/dev/null
git -C "$DPL_SRC" branch -f "$ITERATION_BRANCH" "$target_commit" >/dev/null
git -C "$DPL_SRC" rev-parse HEAD > "$SOURCE_BASE_RECORD"
"$DPL_EVOLVE_PYTHON" - "$ARTIFACT_DIR/start_branch.json" "${{kind:-}}" "$target_ref" "$(git -C "$DPL_SRC" rev-parse HEAD)" <<'PY'
import json
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
path.parent.mkdir(parents=True, exist_ok=True)
payload = {{
    "active_start_kind": sys.argv[2],
    "active_start_ref": sys.argv[3],
    "active_start_commit": sys.argv[4],
}}
path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\\n", encoding="utf-8")
PY
RESET_TARGET_REF="$target_ref" RESET_TARGET_COMMIT="$(git -C "$DPL_SRC" rev-parse HEAD)" RESET_KIND="${{kind:-}}" "$DPL_EVOLVE_PYTHON" - "$ARTIFACT_DIR/source_trials.jsonl" <<'PY'
import json
import os
import sys
from pathlib import Path

path = Path(sys.argv[1])
path.parent.mkdir(parents=True, exist_ok=True)
payload = {{
    "action": "reset_dev",
    "ref": os.environ["RESET_TARGET_REF"],
    "source_commit": os.environ["RESET_TARGET_COMMIT"],
    "kind": os.environ.get("RESET_KIND") or None,
}}
with path.open("a", encoding="utf-8") as fh:
    fh.write(json.dumps(payload, sort_keys=True) + "\\n")
PY
echo "[INFO] reset source_branch=$SOURCE_BRANCH from $target_ref"
echo "[INFO] iteration_branch=$ITERATION_BRANCH"
echo "[INFO] base_commit=$(cat "$SOURCE_BASE_RECORD")"
""",
    )

    write_executable_script(
        scripts["source_context"],
        f"""#!/usr/bin/env bash
set -euo pipefail
{common_vars}
ensure_dpl_git_repo
cd "$DPL_SRC"

usage() {{
  cat >&2 <<'USAGE'
Usage:
  05_source_context.sh <module-relative-file> [symbol-or-text]
  05_source_context.sh <module-relative-file> --symbol <symbol-or-text>

Examples:
  05_source_context.sh src/Optdp.cpp improvePlacement
  05_source_context.sh src/Optdp.cpp --symbol improvePlacement
  05_source_context.sh src/optimization/detailed_manager.h DetailedMgr

This script only prints current local source context for edit anchoring.
Use the excerpt to make coherent mechanism-level source edits when justified by
the Teacher route and your diagnosis.
USAGE
}}

if [[ "$#" -lt 1 ]]; then
  usage
  exit 2
fi

file="$1"
shift || true

symbol=""
if [[ "$#" -gt 0 ]]; then
  case "$1" in
    --symbol|-s)
      shift
      if [[ "$#" -lt 1 ]]; then
        echo "[ERROR] --symbol requires a value" >&2
        usage
        exit 2
      fi
      symbol="$1"
      shift || true
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    --*)
      echo "[ERROR] unknown option: $1" >&2
      usage
      exit 2
      ;;
    *)
      symbol="$1"
      shift || true
      ;;
  esac
fi

if [[ ! -f "$file" ]]; then
  echo "[ERROR] source file not found under private_dpl_evolve_source: $file" >&2
  echo "[INFO] close module-relative matches:" >&2
  rg --files | rg --fixed-strings -- "$file" >&2 || true
  base="$(basename "$file")"
  if [[ "$base" != "$file" ]]; then
    rg --files | rg --fixed-strings -- "$base" >&2 || true
  fi
  exit 2
fi

echo "[INFO] source=$DPL_SRC"
echo "[INFO] file=$file"

if [[ -z "$symbol" ]]; then
  echo "[INFO] no symbol provided; listing likely declarations/functions"
  rg -n '^[[:space:]]*(class|struct|enum|namespace|[A-Za-z_][A-Za-z_0-9:<>~*&[:space:]]+[[:space:]]+[A-Za-z_~][A-Za-z_0-9:~]*[[:space:]]*\\(|[A-Za-z_][A-Za-z_0-9:~]*::[A-Za-z_~][A-Za-z_0-9:~]*[[:space:]]*\\()' "$file" | head -120 || true
  exit 0
fi

echo "[INFO] symbol=$symbol"
match_lines="$(rg -n --fixed-strings -- "$symbol" "$file" || true)"
if [[ -z "$match_lines" ]]; then
  match_lines="$(rg -n -- "$symbol" "$file" || true)"
fi

if [[ -z "$match_lines" ]]; then
  echo "[WARN] symbol/text not found in $file: $symbol" >&2
  echo "[INFO] likely declarations/functions:" >&2
  rg -n '^[[:space:]]*(class|struct|enum|namespace|[A-Za-z_][A-Za-z_0-9:<>~*&[:space:]]+[[:space:]]+[A-Za-z_~][A-Za-z_0-9:~]*[[:space:]]*\\(|[A-Za-z_][A-Za-z_0-9:~]*::[A-Za-z_~][A-Za-z_0-9:~]*[[:space:]]*\\()' "$file" | head -120 >&2 || true
  exit 1
fi

echo "$match_lines" | head -20
first_line="$(printf '%s\\n' "$match_lines" | head -1 | cut -d: -f1)"
if [[ "$first_line" =~ ^[0-9]+$ ]]; then
  start=$(( first_line > 35 ? first_line - 35 : 1 ))
  end=$(( first_line + 100 ))
  echo "[INFO] excerpt lines $start..$end"
  nl -ba "$file" | sed -n "${{start}},${{end}}p"
else
  echo "[WARN] could not compute excerpt line for $symbol" >&2
fi
	""",
    )

    write_executable_script(
        scripts["query_knowledge"],
        f"""#!/usr/bin/env bash
	set -euo pipefail
{common_vars}
json_mode=0
query_text=""
orig_args=("$@")
while [[ "$#" -gt 0 ]]; do
  case "$1" in
    --json)
      json_mode=1
      shift
      ;;
    --q)
      query_text="$2"
      shift 2
      ;;
    --q=*)
      query_text="${{1#--q=}}"
      shift
      ;;
    *)
      shift
      ;;
  esac
done
if [[ "$json_mode" -eq 0 && -n "$query_text" ]]; then
  set +e
  BLUEPRINT_QUERY="$query_text" BLUEPRINT_ROADMAP="$AGENT_ROOT/knowledge/routing/mechanism_reconstruction_roadmap.md" "$DPL_EVOLVE_PYTHON" - <<'PY'
import os
import re
import sys
from pathlib import Path

query = os.environ.get("BLUEPRINT_QUERY", "")
roadmap = Path(os.environ["BLUEPRINT_ROADMAP"])
match = re.search(r"\\bblueprint\\s+(D\\s*\\+\\s*A|[A-J])\\b", query, re.IGNORECASE)
if not match:
    match = re.search(r"\\b([A-J])\\s*\\.\\s*[A-Z][A-Za-z-]", query)
if match and roadmap.exists():
    letter = re.sub(r"\\s+", "", match.group(1).upper())
    text = roadmap.read_text(encoding="utf-8", errors="replace")
    heading = f"## Blueprint {{letter}}:"
    lines = text.splitlines()
    start = None
    for idx, line in enumerate(lines):
        if line.startswith(heading):
            start = idx
            break
    if start is not None:
        end = len(lines)
        for idx in range(start + 1, len(lines)):
            if lines[idx].startswith("## Blueprint ") or lines[idx].startswith("## Complete-Chain"):
                end = idx
                break
        print(f"[INFO] selected_blueprint_roadmap=Blueprint {{letter}}")
        print("\\n".join(lines[start:end]).strip())
        print()
        sys.exit(42)
PY
  blueprint_rc=$?
  set -e
  if [[ "$blueprint_rc" -eq 42 ]]; then
    exit 0
  elif [[ "$blueprint_rc" -ne 0 ]]; then
    exit "$blueprint_rc"
  fi
fi
exec "$DPL_EVOLVE_PYTHON" "$AGENT_ROOT/scripts/repo/query_knowledge.py" "${{orig_args[@]}}"
""",
    )

    write_executable_script(
        scripts["build"],
        f"""#!/usr/bin/env bash
        set -euo pipefail
{common_vars}
acquire_workspace_git_lock
ensure_dpl_git_repo
assert_no_git_index_lock
"$AGENT_ROOT/scripts/workspace/configure_openroad_variant_relink.sh" \\
  --variant-root "$VARIANT_ROOT" \\
  --dpl-src "$DPL_SRC"
"$DPL_EVOLVE_PYTHON" "$AGENT_ROOT/scripts/workspace/build_openroad_variant_relink.py" \\
  --variant-root "$VARIANT_ROOT" \\
  --dpl-src "$DPL_SRC" \\
  --threads "$THREADS"
test -x "$PRIVATE_BINARY"
"$DPL_EVOLVE_PYTHON" "$AGENT_ROOT/scripts/evaluator/candidate_provenance.py" capture-build \
  --source "$DPL_SRC" \
  --binary "$PRIVATE_BINARY" \
  --output "$BUILD_PROVENANCE_JSON"
echo "[INFO] private_binary=$PRIVATE_BINARY"
echo "[INFO] build_provenance=$BUILD_PROVENANCE_JSON"
""",
    )

    write_executable_script(
        scripts["fresh_build"],
        f"""#!/usr/bin/env bash
set -euo pipefail
{common_vars}
acquire_workspace_git_lock
ensure_dpl_git_repo
assert_no_git_index_lock
FRESH_BUILD_DIR="$VARIANT_ROOT/build_fresh"
"$AGENT_ROOT/scripts/workspace/configure_openroad_variant_relink.sh" \\
  --variant-root "$VARIANT_ROOT" \\
  --build-dir "$FRESH_BUILD_DIR" \\
  --dpl-src "$DPL_SRC"
"$DPL_EVOLVE_PYTHON" "$AGENT_ROOT/scripts/workspace/build_openroad_variant_relink.py" \\
  --variant-root "$VARIANT_ROOT" \\
  --build-dir "$FRESH_BUILD_DIR" \\
  --dpl-src "$DPL_SRC" \\
  --threads "$THREADS"
test -x "$PRIVATE_BINARY"
"$DPL_EVOLVE_PYTHON" "$AGENT_ROOT/scripts/evaluator/candidate_provenance.py" capture-build \
  --source "$DPL_SRC" \
  --binary "$PRIVATE_BINARY" \
  --output "$BUILD_PROVENANCE_JSON"
echo "[INFO] private_binary=$PRIVATE_BINARY"
echo "[INFO] build_provenance=$BUILD_PROVENANCE_JSON"
""",
    )

    write_executable_script(
        scripts["evaluate"],
        f"""#!/usr/bin/env bash
set -euo pipefail
{common_vars}
SCRIPT_DIR="$(cd "$(dirname "${{BASH_SOURCE[0]}}")" && pwd)"
acquire_workspace_git_lock
ensure_dpl_git_repo
assert_no_git_index_lock
RUN_BASELINE="$AGENT_ROOT/baseline/run_baseline.sh"
EVALUATION_TRIALS_DIR="$ARTIFACT_DIR/evaluation_trials"
mkdir -p "$ARTIFACT_DIR" "$EVALUATION_TRIALS_DIR"
if [[ ! -f "$BUILD_PROVENANCE_JSON" ]]; then
  echo "[ERROR] build provenance missing; run 10_build_variant.sh first: $BUILD_PROVENANCE_JSON" >&2
  exit 2
fi
EVALUATION_ID="$("$DPL_EVOLVE_PYTHON" \
  "$AGENT_ROOT/scripts/evaluator/evaluation_trial.py" \
  --root "$EVALUATION_TRIALS_DIR")"
EVALUATION_DIR="$EVALUATION_TRIALS_DIR/$EVALUATION_ID"
TRIAL_RUN_TAG="${{RUN_TAG}}_${{EVALUATION_ID}}"
TRIAL_METRICS_PATH="$(dirname "$(dirname "$METRICS_PATH")")/$TRIAL_RUN_TAG/metrics.json"
TRIAL_METRICS_SUMMARY_JSON="$EVALUATION_DIR/candidate_metrics_summary.json"
TRIAL_METRICS_SUMMARY_MD="$EVALUATION_DIR/candidate_metrics_summary.md"
TRIAL_EVALUATION_START_JSON="$EVALUATION_DIR/candidate_evaluation_start.json"
TRIAL_EVALUATION_PROVENANCE_JSON="$EVALUATION_DIR/candidate_evaluation_provenance.json"
cp "$BUILD_PROVENANCE_JSON" "$EVALUATION_DIR/candidate_build_provenance.json"
"$DPL_EVOLVE_PYTHON" "$AGENT_ROOT/scripts/evaluator/candidate_provenance.py" start-evaluation \
  --source "$DPL_SRC" \
  --binary "$PRIVATE_BINARY" \
  --build-provenance "$BUILD_PROVENANCE_JSON" \
  --protected-file "$RUN_BASELINE" \
  --protected-file "$AGENT_ROOT/baseline/collect_metrics.py" \
  --protected-file "$AGENT_ROOT/scripts/evaluator/report_candidate_metrics.py" \
  --protected-file "$AGENT_ROOT/scripts/evaluator/candidate_eligibility.py" \
  --output "$TRIAL_EVALUATION_START_JSON"
set +e
"$RUN_BASELINE" \\
  --line evolve_default \\
  --case "$CASE_ID" \\
  --flow-variant "$FLOW_VARIANT" \\
  --threads "$THREADS" \\
  --openroad-binary "$PRIVATE_BINARY" \\
{legalize_timeout_arg}\
  --run-tag "$TRIAL_RUN_TAG"
status=$?
set -e
echo "$status" > "$EVALUATION_DIR/evaluate_exit_status.txt"
echo "$status" > "$ARTIFACT_DIR/evaluate_exit_status.txt"
if [[ "$status" -ne 0 ]]; then
  ATTEMPT_STATUS="$status" ATTEMPT_METRICS="$TRIAL_METRICS_PATH" ATTEMPT_TAG="$TRIAL_RUN_TAG" ATTEMPT_ID="$EVALUATION_ID" "$DPL_EVOLVE_PYTHON" - "$ARTIFACT_DIR/failed_attempts.jsonl" "$EVALUATION_DIR/trial_manifest.json" <<'PY'
import json
import os
import sys
from pathlib import Path

path = Path(sys.argv[1])
manifest_path = Path(sys.argv[2])
path.parent.mkdir(parents=True, exist_ok=True)
metrics_path = Path(os.environ["ATTEMPT_METRICS"])
payload = {{
    "evaluation_id": os.environ["ATTEMPT_ID"],
    "run_tag": os.environ["ATTEMPT_TAG"],
    "exit_status": int(os.environ["ATTEMPT_STATUS"]),
    "metrics_path": str(metrics_path),
    "metrics_exists": metrics_path.exists(),
}}
if metrics_path.exists():
    try:
        metrics = json.loads(metrics_path.read_text(encoding="utf-8"))
        payload["status"] = metrics.get("status")
        payload["failure"] = metrics.get("failure")
        payload["runtime_seconds"] = metrics.get("runtime_seconds")
    except Exception as exc:
        payload["metrics_parse_error"] = str(exc)
with path.open("a", encoding="utf-8") as fh:
    fh.write(json.dumps(payload, sort_keys=True) + "\\n")
manifest_path.write_text(
    json.dumps(payload, indent=2, sort_keys=True) + "\\n", encoding="utf-8"
)
PY
  echo "[ERROR] immutable evaluation $EVALUATION_ID returned status $status" >&2
  echo "[INFO] trial_run_tag=$TRIAL_RUN_TAG" >&2
  echo "[INFO] trial_dir=$EVALUATION_DIR" >&2
  exit "$status"
fi
if [[ ! -f "$TRIAL_METRICS_PATH" ]]; then
  echo "[ERROR] evaluator succeeded but immutable metrics are missing: $TRIAL_METRICS_PATH" >&2
  exit 2
fi
cp "$TRIAL_EVALUATION_START_JSON" "$EVALUATION_START_JSON"
"$DPL_EVOLVE_PYTHON" "$AGENT_ROOT/scripts/evaluator/report_candidate_metrics.py" \
  --metrics "$TRIAL_METRICS_PATH" \
  $([[ -f "$BASELINE_METRICS_PATH" ]] && printf '%s %q' --reference-metrics "$BASELINE_METRICS_PATH") \
  --output-json "$TRIAL_METRICS_SUMMARY_JSON" \
  --output-md "$TRIAL_METRICS_SUMMARY_MD" >/dev/null
"$DPL_EVOLVE_PYTHON" "$AGENT_ROOT/scripts/evaluator/candidate_provenance.py" finish-evaluation \
  --source "$DPL_SRC" \
  --binary "$PRIVATE_BINARY" \
  --start-provenance "$TRIAL_EVALUATION_START_JSON" \
  --metrics "$TRIAL_METRICS_PATH" \
  --summary "$TRIAL_METRICS_SUMMARY_JSON" \
  --output "$TRIAL_EVALUATION_PROVENANCE_JSON"
TRIAL_METRICS_PATH="$TRIAL_METRICS_PATH" \
CANONICAL_METRICS_PATH="$METRICS_PATH" \
CANONICAL_RUN_TAG="$RUN_TAG" \
EVALUATION_ID="$EVALUATION_ID" \
TRIAL_RUN_TAG="$TRIAL_RUN_TAG" \
EVALUATION_DIR="$EVALUATION_DIR" \
"$DPL_EVOLVE_PYTHON" - <<'PY'
import json
import os
from pathlib import Path

source = Path(os.environ["TRIAL_METRICS_PATH"])
target = Path(os.environ["CANONICAL_METRICS_PATH"])
payload = json.loads(source.read_text(encoding="utf-8"))
manifest = payload.setdefault("manifest", {{}})
trial_run_tag = os.environ["TRIAL_RUN_TAG"]
flow_home = Path(manifest["flow_home"])
result_dir = flow_home / manifest["results_dir"] / "dpl_evolve_baseline" / trial_run_tag
log_dir = flow_home / manifest["log_dir"] / "dpl_evolve_baseline" / trial_run_tag
manifest["run_tag"] = os.environ["CANONICAL_RUN_TAG"]
payload["immutable_evaluation"] = {{
    "evaluation_id": os.environ["EVALUATION_ID"],
    "trial_run_tag": trial_run_tag,
    "trial_metrics_path": str(source),
    "trial_log_dir": str(log_dir),
    "trial_output_odb": str(result_dir / "legalized.odb"),
}}
target.parent.mkdir(parents=True, exist_ok=True)
target.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\\n", encoding="utf-8")
trial_manifest = {{
    "evaluation_id": os.environ["EVALUATION_ID"],
    "status": "ok",
    "trial_run_tag": trial_run_tag,
    "trial_metrics_path": str(source),
    "trial_log_dir": str(log_dir),
    "trial_output_odb": str(result_dir / "legalized.odb"),
    "canonical_run_tag": os.environ["CANONICAL_RUN_TAG"],
    "canonical_metrics_path": str(target),
    "orfs_report_dir": str(source.parent),
}}
Path(os.environ["EVALUATION_DIR"], "trial_manifest.json").write_text(
    json.dumps(trial_manifest, indent=2, sort_keys=True) + "\\n",
    encoding="utf-8",
)
Path(os.environ["EVALUATION_DIR"]).parent.joinpath("latest.json").write_text(
    json.dumps(trial_manifest, indent=2, sort_keys=True) + "\\n",
    encoding="utf-8",
)
PY
"$SCRIPT_DIR/21_report_candidate_metrics.sh"
"$DPL_EVOLVE_PYTHON" "$AGENT_ROOT/scripts/evaluator/candidate_provenance.py" finish-evaluation \
  --source "$DPL_SRC" \
  --binary "$PRIVATE_BINARY" \
  --start-provenance "$EVALUATION_START_JSON" \
  --metrics "$METRICS_PATH" \
  --summary "$METRICS_SUMMARY_JSON" \
  --output "$EVALUATION_PROVENANCE_JSON"
echo "[INFO] evaluation_id=$EVALUATION_ID"
echo "[INFO] immutable_metrics=$TRIAL_METRICS_PATH"
echo "[INFO] immutable_trial_dir=$EVALUATION_DIR"
echo "[INFO] canonical_metrics=$METRICS_PATH"
echo "[INFO] evaluation_provenance=$EVALUATION_PROVENANCE_JSON"
""",
    )

    write_executable_script(
        scripts["report_metrics"],
        f"""#!/usr/bin/env bash
set -euo pipefail
{common_vars}
mkdir -p "$ARTIFACT_DIR"
if [[ ! -f "$METRICS_PATH" ]]; then
  echo "[ERROR] metrics json missing: $METRICS_PATH" >&2
  exit 2
fi
"$DPL_EVOLVE_PYTHON" "$AGENT_ROOT/scripts/evaluator/report_candidate_metrics.py" \\
  --metrics "$METRICS_PATH" \\
  $([[ -f "$BASELINE_METRICS_PATH" ]] && printf '%s %q' --reference-metrics "$BASELINE_METRICS_PATH") \\
  --output-json "$METRICS_SUMMARY_JSON" \\
  --output-md "$METRICS_SUMMARY_MD" >/dev/null
echo "[INFO] metrics_summary_json=$METRICS_SUMMARY_JSON"
echo "[INFO] metrics_summary_md=$METRICS_SUMMARY_MD"
""",
    )

    write_executable_script(
        scripts["keep_and_finalize"],
        """#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ "${1:-}" != "--reason" || "$#" -lt 2 ]]; then
  echo "Usage: 22_keep_and_finalize.sh --reason <why-kept>" >&2
  exit 2
fi
reason="$2"
"$SCRIPT_DIR/25_trial_source.sh" keep --reason "$reason"
"$SCRIPT_DIR/30_finalize_source.sh"
""",
    )

    write_executable_script(
        scripts["trial"],
        f"""#!/usr/bin/env bash
set -euo pipefail
{common_vars}
COMMAND="${{1:-}}"
if [[ "$#" -gt 0 ]]; then
  shift
fi
TRIAL_STATE="$ARTIFACT_DIR/source_trial_state.json"
TRIAL_LOG="$ARTIFACT_DIR/source_trials.jsonl"
TRIAL_DIR="$ARTIFACT_DIR/source_trials"

usage() {{
  cat >&2 <<'USAGE'
Usage:
  25_trial_source.sh begin --label <short-mechanism> [--from-ref <kept-or-rejected-ref>]
  25_trial_source.sh reject --reason <why-rejected>
  25_trial_source.sh keep --reason <why-kept>
  25_trial_source.sh status

begin  : checkpoint the current clean/base state and open one active trial.
         With --from-ref, reset the dev branch to that prior kept/rejected ref
         as the trial workbench.
reject : commit/pin current work as rejected evidence, snapshot diff/metrics,
         keep the rejected code checked out on the dev branch, and clear state.
keep   : commit/pin current work as kept evidence and clear state; branch stays
         at the kept source.
status : print active trial state and source branch status.
USAGE
}}

sanitize_label() {{
  local raw="$1"
  local safe
  safe="$(printf '%s' "$raw" | tr -cs 'A-Za-z0-9._-' '_' | sed 's/^[_\\.-]*//; s/[_\\.-]*$//' | cut -c1-80)"
  if [[ -z "$safe" ]]; then
    safe="attempt"
  fi
  printf '%s\n' "$safe"
}}

require_iteration_branch() {{
  current_branch="$(git -C "$DPL_SRC" branch --show-current)"
  if [[ "$current_branch" != "$SOURCE_BRANCH" ]]; then
    echo "[ERROR] expected source branch $SOURCE_BRANCH, got $current_branch" >&2
    exit 2
  fi
}}

commit_current_if_needed() {{
  local message="$1"
  git -C "$DPL_SRC" add -A
  if ! git -C "$DPL_SRC" diff --cached --quiet; then
    git -C "$DPL_SRC" commit -m "$message" >&2
  else
    echo "[INFO] no staged changes; pinning current HEAD" >&2
  fi
  git -C "$DPL_SRC" rev-parse HEAD
}}

write_diff_for_commit() {{
  local base_commit="$1"
  local commit="$2"
  local diff_path="$3"
  mkdir -p "$(dirname "$diff_path")"
  git -C "$DPL_SRC" diff --no-ext-diff "$base_commit".."$commit" > "$diff_path" || true
  if [[ ! -s "$diff_path" ]]; then
    git -C "$DPL_SRC" diff --no-ext-diff "$base_commit" "$commit" > "$diff_path" || true
  fi
}}

snapshot_metrics() {{
  local prefix="$1"
  local out=""
  if [[ -f "$METRICS_PATH" ]]; then
    out="$TRIAL_DIR/${{prefix}}.metrics.json"
    mkdir -p "$TRIAL_DIR"
    cp -f "$METRICS_PATH" "$out"
  fi
  printf '%s\n' "$out"
}}

append_trial_event() {{
  local action="$1"
  local ref="$2"
  local commit="$3"
  local reason="${{4:-}}"
  local diff_path="${{5:-}}"
  local metrics_snapshot="${{6:-}}"
  SOURCE_BRANCH_NOW="$(git -C "$DPL_SRC" branch --show-current)" \
  TRIAL_ACTION="$action" \
  TRIAL_REF="$ref" \
  TRIAL_COMMIT="$commit" \
  TRIAL_REASON="$reason" \
  TRIAL_DIFF="$diff_path" \
  TRIAL_METRICS="$metrics_snapshot" \
  TRIAL_RUN_TAG="$RUN_TAG" \
  DPL_SRC="$DPL_SRC" \
  "$DPL_EVOLVE_PYTHON" - "$TRIAL_LOG" <<'PY'
import json
import os
import sys
from pathlib import Path

path = Path(sys.argv[1])
path.parent.mkdir(parents=True, exist_ok=True)
payload = {{
    "action": os.environ["TRIAL_ACTION"],
    "branch": os.environ["SOURCE_BRANCH_NOW"],
    "diff_path": os.environ.get("TRIAL_DIFF") or None,
    "metrics_snapshot": os.environ.get("TRIAL_METRICS") or None,
    "reason": os.environ.get("TRIAL_REASON") or None,
    "ref": os.environ.get("TRIAL_REF") or None,
    "run_tag": os.environ["TRIAL_RUN_TAG"],
    "source_commit": os.environ.get("TRIAL_COMMIT") or None,
    "source_repo": os.environ["DPL_SRC"],
}}
with path.open("a", encoding="utf-8") as fh:
    fh.write(json.dumps(payload, sort_keys=True) + "\\n")
PY
}}

load_state_exports() {{
  "$DPL_EVOLVE_PYTHON" - "$TRIAL_STATE" <<'PY'
import json
import shlex
import sys
from pathlib import Path

path = Path(sys.argv[1])
if not path.exists():
    raise SystemExit("[ERROR] no active trial state; run begin first")
payload = json.loads(path.read_text(encoding="utf-8"))
for key, value in payload.items():
    env_key = "TRIAL_" + key.upper()
    print(f"{{env_key}}={{shlex.quote(str(value))}}")
PY
}}

label=""
reason=""
from_ref=""
while [[ "$#" -gt 0 ]]; do
  case "$1" in
    --label|-l)
      if [[ "$#" -lt 2 ]]; then echo "[ERROR] --label requires a value" >&2; usage; exit 2; fi
      label="$2"; shift 2 ;;
    --reason|-r)
      if [[ "$#" -lt 2 ]]; then echo "[ERROR] --reason requires a value" >&2; usage; exit 2; fi
      reason="$2"; shift 2 ;;
    --from-ref)
      if [[ "$#" -lt 2 ]]; then echo "[ERROR] --from-ref requires a value" >&2; usage; exit 2; fi
      from_ref="$2"; shift 2 ;;
    --help|-h)
      usage; exit 0 ;;
    *)
      echo "[ERROR] unknown argument: $1" >&2; usage; exit 2 ;;
  esac
done

case "$COMMAND" in
  begin)
    if [[ -z "$label" ]]; then
      echo "[ERROR] begin requires --label" >&2
      usage
      exit 2
    fi
    acquire_workspace_git_lock
    ensure_dpl_git_repo
    assert_no_git_index_lock
    require_iteration_branch
    mkdir -p "$ARTIFACT_DIR" "$TRIAL_DIR"
    if [[ -f "$TRIAL_STATE" ]]; then
      echo "[ERROR] active trial already exists: $TRIAL_STATE" >&2
      echo "[INFO] Resolve it with: 25_trial_source.sh keep|reject --reason <why>" >&2
      exit 2
    fi
    safe_label="$(sanitize_label "$label")"
    base_commit="$(commit_current_if_needed "trial base $SOURCE_BRANCH $safe_label")"
    stamp="$(date +%Y%m%d_%H%M%S)"
    checkpoint_ref="checkpoint/${{RUN_TAG}}/${{stamp}}-${{safe_label}}"
    git -C "$DPL_SRC" check-ref-format --branch "$checkpoint_ref" >/dev/null
    git -C "$DPL_SRC" branch -f "$checkpoint_ref" "$base_commit" >/dev/null
    start_ref="$base_commit"
    start_commit="$base_commit"
    if [[ -n "$from_ref" ]]; then
      start_commit="$(git -C "$DPL_SRC" rev-parse --verify "$from_ref^{{commit}}")"
      git -C "$DPL_SRC" reset --hard "$start_commit" >/dev/null
      start_ref="$from_ref"
      echo "[INFO] trial_from_ref=$from_ref"
      echo "[INFO] trial_start_commit=$start_commit"
    fi
    TRIAL_LABEL="$label" \
    TRIAL_SAFE_LABEL="$safe_label" \
    TRIAL_BASE_COMMIT="$base_commit" \
    TRIAL_CHECKPOINT_REF="$checkpoint_ref" \
    TRIAL_FROM_REF="$start_ref" \
    TRIAL_START_COMMIT="$start_commit" \
    TRIAL_STARTED_AT="$stamp" \
    "$DPL_EVOLVE_PYTHON" - "$TRIAL_STATE" <<'PY'
import json
import os
import sys
from pathlib import Path

path = Path(sys.argv[1])
payload = {{
    "base_commit": os.environ["TRIAL_BASE_COMMIT"],
    "checkpoint_ref": os.environ["TRIAL_CHECKPOINT_REF"],
    "from_ref": os.environ["TRIAL_FROM_REF"],
    "label": os.environ["TRIAL_LABEL"],
    "safe_label": os.environ["TRIAL_SAFE_LABEL"],
    "start_commit": os.environ["TRIAL_START_COMMIT"],
    "started_at": os.environ["TRIAL_STARTED_AT"],
}}
path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\\n", encoding="utf-8")
PY
    append_trial_event "begin" "$checkpoint_ref" "$base_commit" "trial opened from $start_ref" "" ""
    echo "[INFO] trial_base_commit=$base_commit"
    echo "[INFO] trial_checkpoint_ref=$checkpoint_ref"
    echo "[INFO] trial_state=$TRIAL_STATE"
    ;;
  reject)
    if [[ -z "$reason" ]]; then
      echo "[ERROR] reject requires --reason" >&2
      usage
      exit 2
    fi
    acquire_workspace_git_lock
    ensure_dpl_git_repo
    assert_no_git_index_lock
    require_iteration_branch
    mkdir -p "$TRIAL_DIR"
    eval "$(load_state_exports)"
    stamp="$(date +%Y%m%d_%H%M%S)"
    rejected_commit="$(commit_current_if_needed "rejected $SOURCE_BRANCH ${{TRIAL_SAFE_LABEL}}: $reason")"
    rejected_ref="rejected/${{RUN_TAG}}/${{stamp}}-${{TRIAL_SAFE_LABEL}}"
    git -C "$DPL_SRC" check-ref-format --branch "$rejected_ref" >/dev/null
    git -C "$DPL_SRC" branch -f "$rejected_ref" "$rejected_commit" >/dev/null
    diff_path="$TRIAL_DIR/${{stamp}}-${{TRIAL_SAFE_LABEL}}.rejected.diff"
    write_diff_for_commit "$TRIAL_BASE_COMMIT" "$rejected_commit" "$diff_path"
    metrics_snapshot="$(snapshot_metrics "${{stamp}}-${{TRIAL_SAFE_LABEL}}.rejected")"
    append_trial_event "reject" "$rejected_ref" "$rejected_commit" "$reason" "$diff_path" "$metrics_snapshot"
    rm -f "$TRIAL_STATE"
    echo "[INFO] rejected_commit=$rejected_commit"
    echo "[INFO] rejected_ref=$rejected_ref"
    echo "[INFO] rejected_diff=$diff_path"
    if [[ -n "$metrics_snapshot" ]]; then echo "[INFO] rejected_metrics_snapshot=$metrics_snapshot"; fi
    echo "[INFO] current_source_commit=$rejected_commit"
    echo "[INFO] rejected code remains checked out; use begin --from-ref or 04_switch_start_branch.sh --ref to choose a new dev starting point."
    echo "[INFO] source_trials=$TRIAL_LOG"
    ;;
  keep)
    if [[ -z "$reason" ]]; then
      echo "[ERROR] keep requires --reason" >&2
      usage
      exit 2
    fi
    acquire_workspace_git_lock
    ensure_dpl_git_repo
    assert_no_git_index_lock
    require_iteration_branch
    mkdir -p "$TRIAL_DIR"
    eval "$(load_state_exports)"
    stamp="$(date +%Y%m%d_%H%M%S)"
    kept_commit="$(commit_current_if_needed "kept $SOURCE_BRANCH ${{TRIAL_SAFE_LABEL}}: $reason")"
    kept_ref="kept/${{RUN_TAG}}/${{stamp}}-${{TRIAL_SAFE_LABEL}}"
    git -C "$DPL_SRC" check-ref-format --branch "$kept_ref" >/dev/null
    git -C "$DPL_SRC" branch -f "$kept_ref" "$kept_commit" >/dev/null
    diff_path="$TRIAL_DIR/${{stamp}}-${{TRIAL_SAFE_LABEL}}.kept.diff"
    write_diff_for_commit "$TRIAL_BASE_COMMIT" "$kept_commit" "$diff_path"
    metrics_snapshot="$(snapshot_metrics "${{stamp}}-${{TRIAL_SAFE_LABEL}}.kept")"
    append_trial_event "keep" "$kept_ref" "$kept_commit" "$reason" "$diff_path" "$metrics_snapshot"
    rm -f "$TRIAL_STATE"
    echo "[INFO] kept_commit=$kept_commit"
    echo "[INFO] kept_ref=$kept_ref"
    echo "[INFO] kept_diff=$diff_path"
    if [[ -n "$metrics_snapshot" ]]; then echo "[INFO] kept_metrics_snapshot=$metrics_snapshot"; fi
    echo "[INFO] source_trials=$TRIAL_LOG"
    ;;
  status)
    ensure_dpl_git_repo
    echo "[INFO] branch=$(git -C "$DPL_SRC" branch --show-current)"
    echo "[INFO] head=$(git -C "$DPL_SRC" rev-parse --short HEAD)"
    git -C "$DPL_SRC" status --short
    if [[ -f "$TRIAL_STATE" ]]; then
      echo "[INFO] active_trial_state=$TRIAL_STATE"
      cat "$TRIAL_STATE"
    else
      echo "[INFO] active_trial_state=<none>"
    fi
    if [[ -f "$TRIAL_LOG" ]]; then
      echo "[INFO] source_trials=$TRIAL_LOG"
      tail -n 5 "$TRIAL_LOG"
    fi
    ;;
  --help|-h|"")
    usage
    if [[ -z "$COMMAND" ]]; then exit 2; fi
    ;;
  *)
    echo "[ERROR] unknown command: $COMMAND" >&2
    usage
    exit 2
    ;;
esac
""",
    )

    write_executable_script(
        scripts["finalize"],
        f"""#!/usr/bin/env bash
set -euo pipefail
{common_vars}
TRIAL_STATE="$ARTIFACT_DIR/source_trial_state.json"
TRIAL_LOG="$ARTIFACT_DIR/source_trials.jsonl"

acquire_workspace_git_lock
ensure_dpl_git_repo
assert_no_git_index_lock
current_branch="$(git -C "$DPL_SRC" branch --show-current)"
if [[ "$current_branch" != "$SOURCE_BRANCH" ]]; then
  echo "[ERROR] expected source branch $SOURCE_BRANCH, got $current_branch" >&2
  exit 2
fi
if [[ -f "$TRIAL_STATE" ]]; then
  echo "[ERROR] active trial still exists: $TRIAL_STATE" >&2
  echo "[INFO] Resolve it first with: 25_trial_source.sh keep|reject --reason <why>" >&2
  exit 2
fi

git -C "$DPL_SRC" status --short
git -C "$DPL_SRC" add -A
if ! git -C "$DPL_SRC" diff --cached --quiet; then
  git -C "$DPL_SRC" commit -m {shlex.quote(f"{paths.source_branch} source update")}
fi
commit="$(git -C "$DPL_SRC" rev-parse HEAD)"
dirty="$(git -C "$DPL_SRC" status --short)"
git -C "$DPL_SRC" check-ref-format --branch "$CANDIDATE_SOURCE_REF" >/dev/null
git -C "$DPL_SRC" branch -f "$CANDIDATE_SOURCE_REF" "$commit" >/dev/null
if [[ -s "$SOURCE_BASE_RECORD" ]]; then
  SOURCE_BASE_COMMIT="$(head -n 1 "$SOURCE_BASE_RECORD")"
else
  SOURCE_BASE_COMMIT="$(git -C "$DPL_SRC" rev-list --max-parents=0 HEAD | tail -n 1)"
fi
mkdir -p "$ARTIFACT_DIR"

FINAL_SOURCE_BRANCH="$current_branch" \
FINAL_SOURCE_COMMIT="$commit" \
FINAL_CANDIDATE_REF="$CANDIDATE_SOURCE_REF" \
FINAL_DIFF="$IMPLEMENTATION_DIFF" \
FINAL_KNOWLEDGE="$KNOWLEDGE_CARD" \
DPL_SRC="$DPL_SRC" \
RUN_TAG="$RUN_TAG" \
"$DPL_EVOLVE_PYTHON" - "$TRIAL_LOG" <<'PY'
import json
import os
import sys
from pathlib import Path

path = Path(sys.argv[1])
path.parent.mkdir(parents=True, exist_ok=True)
payload = {{
    "action": "finalize",
    "branch": os.environ["FINAL_SOURCE_BRANCH"],
    "candidate_ref": os.environ["FINAL_CANDIDATE_REF"],
    "diff_path": os.environ["FINAL_DIFF"],
    "knowledge_card": os.environ["FINAL_KNOWLEDGE"],
    "run_tag": os.environ["RUN_TAG"],
    "source_commit": os.environ["FINAL_SOURCE_COMMIT"],
    "source_repo": os.environ["DPL_SRC"],
}}
with path.open("a", encoding="utf-8") as fh:
    fh.write(json.dumps(payload, sort_keys=True) + "\\n")
PY

TRIAL_SUMMARY_MD="$ARTIFACT_DIR/source_trials_summary.md"
SOURCE_COMMIT="$commit" \
DIRTY_STATUS="$dirty" \
CANDIDATE_SOURCE_REF="$CANDIDATE_SOURCE_REF" \
DPL_SRC="$DPL_SRC" \
SOURCE_BASE_COMMIT="$SOURCE_BASE_COMMIT" \
"$DPL_EVOLVE_PYTHON" - "$SOURCE_COMMIT_RECORD" "$SOURCE_BRANCH" "$TRIAL_LOG" "$TRIAL_SUMMARY_MD" <<'PY'
import json
import os
import sys
from pathlib import Path

path, branch, trial_log, trial_summary = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
trial_events = []
kept_refs = []
rejected_refs = []
final_status = "unmarked"
trial_log_path = Path(trial_log)
if trial_log_path.exists():
    for raw_line in trial_log_path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not raw_line.strip():
            continue
        try:
            event = json.loads(raw_line)
        except json.JSONDecodeError:
            continue
        trial_events.append(event)
        action = event.get("action")
        ref = event.get("ref") or event.get("candidate_ref")
        if action == "keep" and ref:
            kept_refs.append(ref)
        elif action == "reject" and ref:
            rejected_refs.append(ref)
        if action in {"keep", "reject"}:
            final_status = action
payload = {{
    "source_base_commit": os.environ.get("SOURCE_BASE_COMMIT") or None,
    "source_candidate_ref": os.environ["CANDIDATE_SOURCE_REF"],
    "source_branch": branch,
    "source_commit": os.environ["SOURCE_COMMIT"],
    "source_repo": os.environ.get("DPL_SRC"),
    "dirty_after_commit": bool(os.environ.get("DIRTY_STATUS", "")),
    "dirty_status": os.environ.get("DIRTY_STATUS", ""),
    "source_trials": trial_log,
    "kept_refs": kept_refs,
    "rejected_refs": rejected_refs,
    "final_status": final_status,
    "trial_event_count": len(trial_events),
}}
with open(path, "w", encoding="utf-8") as fh:
    json.dump(payload, fh, indent=2, sort_keys=True)
    fh.write("\\n")
summary_path = Path(trial_summary)
summary_path.parent.mkdir(parents=True, exist_ok=True)
lines = [
    "# Source Trials Summary",
    "",
    f"- source_branch: `{{branch}}`",
    f"- final_source_commit: `{{os.environ['SOURCE_COMMIT']}}`",
    f"- candidate_ref: `{{os.environ['CANDIDATE_SOURCE_REF']}}`",
    f"- trial_event_count: `{{len(trial_events)}}`",
    f"- final_status: `{{final_status}}`",
    f"- kept_refs: `{{len(kept_refs)}}`",
    f"- rejected_refs: `{{len(rejected_refs)}}`",
    "",
]
for event in trial_events:
    action = event.get("action") or ""
    ref = event.get("ref") or event.get("candidate_ref") or ""
    commit = event.get("source_commit") or ""
    reason = event.get("reason") or ""
    diff = event.get("diff_path") or ""
    metrics = event.get("metrics_snapshot") or ""
    lines.append(f"## {{action}}")
    if ref:
        lines.append(f"- ref: `{{ref}}`")
    if commit:
        lines.append(f"- source_commit: `{{commit}}`")
    if reason:
        lines.append(f"- reason: {{reason}}")
    if diff:
        lines.append(f"- diff_path: `{{diff}}`")
    if metrics:
        lines.append(f"- metrics_snapshot: `{{metrics}}`")
    lines.append("")
summary_path.write_text("\\n".join(lines).rstrip() + "\\n", encoding="utf-8")
PY

git -C "$DPL_SRC" diff --no-ext-diff "$SOURCE_BASE_COMMIT".."$commit" \
  > "$IMPLEMENTATION_DIFF" || true
if [[ ! -s "$IMPLEMENTATION_DIFF" ]]; then
  git -C "$DPL_SRC" diff --no-ext-diff "$SOURCE_BASE_COMMIT" "$commit" \
    > "$IMPLEMENTATION_DIFF" || true
fi
if [[ ! -s "$IMPLEMENTATION_DIFF" ]]; then
  echo "[WARN] implementation diff is empty: $IMPLEMENTATION_DIFF" >&2
else
  echo "[INFO] implementation_diff=$IMPLEMENTATION_DIFF"
fi

if [[ ! -f "$KNOWLEDGE_CARD" ]]; then
  cat > "$KNOWLEDGE_CARD" <<'EOF_KNOWLEDGE'
# Knowledge Card

## Route And Hypothesis

- route action:
- HPWL source:
- expected HPWL source:
- mechanism strength:
- stage-wise proof target:
- expected stage movement:

## Source Map

- changed files:
- changed functions/state:
- mechanism class:
- support handles:

## Execution Evidence

- counters/log signals:
- measured HPWL-source attribution:
- first-result diagnosis:
- source/binary/metrics alignment:
- rejected refs:

## Result

- final HPWL:
- stage-wise HPWL:
- runtime:
- legality/displacement:
- kept/ref:

## Causal Lesson

- why it worked or failed:
- low-ROI / implementation / handoff / method status:

## Next Repair Or Pivot

- next action:

## Next Teacher Handoff

- current bottleneck:
- attempted self-diagnosis/repair:
- why one repair was enough or insufficient:
- source refs/handles for Teacher:
- proposed next route:
EOF_KNOWLEDGE
fi

echo "[INFO] source_commit=$commit"
echo "[INFO] source_candidate_ref=$CANDIDATE_SOURCE_REF"
echo "[INFO] source_commit_record=$SOURCE_COMMIT_RECORD"
echo "[INFO] source_trials=$TRIAL_LOG"
echo "[INFO] source_trials_summary=$TRIAL_SUMMARY_MD"
echo "[INFO] knowledge_card=$KNOWLEDGE_CARD"
if [[ -n "$dirty" ]]; then
  echo "[WARN] source repo still dirty after finalize:" >&2
  printf '%s\n' "$dirty" >&2
fi
""",
    )

    write_executable_script(
        scripts["after_edit"],
        """#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
"$SCRIPT_DIR/10_build_variant.sh"
"$SCRIPT_DIR/20_evaluate_candidate.sh"
"$SCRIPT_DIR/30_finalize_source.sh"
""",
    )

    readme = script_dir / "README.md"
    readme.write_text(
        render_prompt_template("packets/student_scripts_readme.md"),
        encoding="utf-8",
    )
    return scripts


__all__ = [
    "child_evaluation_timeout",
    "child_parent_source",
    "start_kind_source",
    "validate_start_kind_seed",
    "write_executable_script",
    "write_student_workspace_scripts",
]
