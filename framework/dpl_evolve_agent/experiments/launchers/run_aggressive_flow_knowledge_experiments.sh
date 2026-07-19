#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AGENT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${AGENT_ROOT}/scripts/runtime_env.sh"
dpl_init_runtime "run_aggressive_flow_knowledge_experiments.sh"

PREFIX="flow_knowledge"
PLAN_FILE=""
THREADS="10"
CHILDREN="4"
ITERATIONS="12"
MAX_PARALLEL="4"
START_KIND="framework"
TEACHER_MODEL="gpt-5.5"
TEACHER_REASONING_EFFORT="xhigh"
STUDENT_MODEL="gpt-5.4"
STUDENT_REASONING_EFFORT="high"
DRY_RUN=0
BASELINE_ONLY=0
SKIP_PLACE=0
FORCE_PLACE=0
SKIP_CORE_BUILD=0
PREPARE_WORKSPACE=0

usage() {
  cat <<'EOF'
Usage: run_aggressive_flow_knowledge_experiments.sh [options]

Runs the large-flow-knowledge experiment plan:

  1. Build/refresh the common OpenROAD core.
  2. Create placement snapshots.
  3. Run canonical baselines for every experiment point.
  4. Sequentially launch Teacher/Student loops, with 4 students in parallel.

Default experiment plan:

  - ariane133_nangate45, default CORE_UTILIZATION
  - jpeg_nangate45, CORE_UTILIZATION=40
  - jpeg_nangate45, CORE_UTILIZATION=60
  - jpeg_nangate45, CORE_UTILIZATION=80
  - ibex_nangate45, default CORE_UTILIZATION

Options:
  --plan PATH                   TSV experiment plan. Columns:
                                enabled, case, core_utilization, flow_variant,
                                round_id, start_kind, notes.
  --prefix NAME                 Stable output/round prefix. Default: flow_knowledge.
  --threads N                   OpenROAD threads. Default: 10.
  --children N                  Student agents per round. Default: 4.
  --iterations N                Iterations per case/utilization. Default: 12.
  --max-parallel N              Parallel students inside one round. Default: 4.
  --start-kind KIND             framework, diamond, default_negotiation,
                                evolved_diamond, evolved_negotiation,
                                or prepared.
                                Default: framework.
  --teacher-model NAME          Default: gpt-5.5.
  --teacher-reasoning-effort E  Default: xhigh.
  --student-model NAME          Default: gpt-5.4.
  --student-reasoning-effort E  Default: high.
  --prepare-workspace           Anchor/patch ORFS before building core.
  --skip-core-build             Do not run configure/build_openroad_core.sh.
  --skip-place                  Reuse existing 3_4_place_resized.odb snapshots.
  --force-place                 Regenerate placement snapshots even if present.
  --baseline-only               Stop after baseline suite refresh.
  --dry-run                     Print commands without running them.
  --help                        Show this message.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --prefix) PREFIX="$2"; shift 2 ;;
    --plan) PLAN_FILE="$2"; shift 2 ;;
    --threads) THREADS="$2"; shift 2 ;;
    --children) CHILDREN="$2"; shift 2 ;;
    --iterations) ITERATIONS="$2"; shift 2 ;;
    --max-parallel) MAX_PARALLEL="$2"; shift 2 ;;
    --start-kind) START_KIND="$2"; shift 2 ;;
    --teacher-model) TEACHER_MODEL="$2"; shift 2 ;;
    --teacher-reasoning-effort) TEACHER_REASONING_EFFORT="$2"; shift 2 ;;
    --student-model) STUDENT_MODEL="$2"; shift 2 ;;
    --student-reasoning-effort) STUDENT_REASONING_EFFORT="$2"; shift 2 ;;
    --prepare-workspace) PREPARE_WORKSPACE=1; shift ;;
    --skip-core-build) SKIP_CORE_BUILD=1; shift ;;
    --skip-place) SKIP_PLACE=1; shift ;;
    --force-place) FORCE_PLACE=1; shift ;;
    --baseline-only) BASELINE_ONLY=1; shift ;;
    --dry-run) DRY_RUN=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) echo "[ERROR] Unknown argument: $1" >&2; usage >&2; exit 1 ;;
  esac
done

case "${START_KIND}" in
  diamond|framework|default_negotiation|evolved_diamond|evolved_negotiation|prepared) ;;
  *)
    echo "[ERROR] Unsupported --start-kind: ${START_KIND}" >&2
    exit 1
    ;;
esac

BATCH_ROOT="${DPL_EVOLVE_STATE_ROOT}/experiment_batches/${PREFIX}"
BATCH_LOG="${BATCH_ROOT}/run.log"
EXPERIMENTS_TSV="${BATCH_ROOT}/experiments.tsv"
mkdir -p "${BATCH_ROOT}"
: > "${BATCH_LOG}"
printf "case\tcore_utilization\tflow_variant\tround_id\tsnapshot\tstart_kind\tnotes\n" > "${EXPERIMENTS_TSV}"

log() {
  local msg="$*"
  printf '[flow_knowledge] %s\n' "${msg}" | tee -a "${BATCH_LOG}"
}

run_cmd() {
  log "+ $(printf '%q ' "$@")"
  if [[ "${DRY_RUN}" -eq 1 ]]; then
    return 0
  fi
  "$@" 2>&1 | tee -a "${BATCH_LOG}"
}

run_cmd_env() {
  local env_name="$1"
  local env_value="$2"
  shift 2
  log "+ env ${env_name}=$(printf '%q' "${env_value}") $(printf '%q ' "$@")"
  if [[ "${DRY_RUN}" -eq 1 ]]; then
    return 0
  fi
  env "${env_name}=${env_value}" "$@" 2>&1 | tee -a "${BATCH_LOG}"
}

case_field() {
  "${DPL_EVOLVE_PYTHON}" "${AGENT_ROOT}/scripts/repo/case_registry.py" --case "$1" --field "$2"
}

snapshot_path() {
  local case_id="$1"
  local flow_variant="$2"
  local platform design
  platform="$(case_field "${case_id}" platform)"
  design="$(case_field "${case_id}" design)"
  printf '%s\n' "${ORFS_ROOT}/flow/results/${platform}/${design}/${flow_variant}/3_4_place_resized.odb"
}

declare -a EXPERIMENTS=()

load_default_experiments() {
  EXPERIMENTS=(
    "ariane133_nangate45||${PREFIX}_ariane133_place|${PREFIX}_ariane133|${START_KIND}|default ariane133 density"
    "jpeg_nangate45|40|${PREFIX}_jpeg_util40_place|${PREFIX}_jpeg_util40|${START_KIND}|jpeg low utilization"
    "jpeg_nangate45|60|${PREFIX}_jpeg_util60_place|${PREFIX}_jpeg_util60|${START_KIND}|jpeg medium utilization"
    "jpeg_nangate45|80|${PREFIX}_jpeg_util80_place|${PREFIX}_jpeg_util80|${START_KIND}|jpeg high utilization"
    "ibex_nangate45||${PREFIX}_ibex_place|${PREFIX}_ibex|${START_KIND}|default ibex density"
  )
}

load_plan_file() {
  local plan="$1"
  local line enabled case_id core_util flow_variant round_id row_start_kind notes

  if [[ ! -f "${plan}" ]]; then
    echo "[ERROR] Missing experiment plan: ${plan}" >&2
    exit 1
  fi

  EXPERIMENTS=()
  while IFS=$'\t' read -r enabled case_id core_util flow_variant round_id row_start_kind notes || [[ -n "${enabled:-}" ]]; do
    [[ -z "${enabled:-}" || "${enabled}" == \#* ]] && continue
    [[ "${enabled}" == "enabled" ]] && continue
    [[ "${enabled}" == "0" || "${enabled}" == "false" || "${enabled}" == "skip" ]] && continue
    core_util="${core_util:-}"
    [[ "${core_util}" == "-" || "${core_util}" == "default" ]] && core_util=""
    row_start_kind="${row_start_kind:-${START_KIND}}"
    [[ "${row_start_kind}" == "-" || -z "${row_start_kind}" ]] && row_start_kind="${START_KIND}"
    notes="${notes:-}"
    if [[ -z "${case_id:-}" || -z "${flow_variant:-}" || -z "${round_id:-}" ]]; then
      echo "[ERROR] Bad plan row in ${plan}: ${line:-${enabled} ${case_id:-} ${flow_variant:-} ${round_id:-}}" >&2
      exit 1
    fi
    EXPERIMENTS+=("${case_id}|${core_util}|${flow_variant}|${round_id}|${row_start_kind}|${notes}")
  done < "${plan}"

  if [[ "${#EXPERIMENTS[@]}" -eq 0 ]]; then
    echo "[ERROR] No enabled experiments in ${plan}" >&2
    exit 1
  fi
}

if [[ -n "${PLAN_FILE}" ]]; then
  PLAN_FILE="$(realpath -m "${PLAN_FILE}")"
  load_plan_file "${PLAN_FILE}"
else
  load_default_experiments
fi

log "batch_root=${BATCH_ROOT}"
log "plan=${PLAN_FILE:-builtin_default}"
log "orfs_root=${ORFS_ROOT}"
log "agent_root=${DPL_EVOLVE_AGENT_ROOT}"
log "start_kind=${START_KIND} children=${CHILDREN} iterations=${ITERATIONS} max_parallel=${MAX_PARALLEL}"

if [[ "${PREPARE_WORKSPACE}" -eq 1 ]]; then
  run_cmd "${AGENT_ROOT}/scripts/workspace/prepare_workspace.sh" --workspace-root "${ORFS_ROOT}"
fi

if [[ "${SKIP_CORE_BUILD}" -ne 1 ]]; then
  run_cmd "${AGENT_ROOT}/scripts/workspace/configure_openroad_core.sh"
  run_cmd "${AGENT_ROOT}/scripts/workspace/build_openroad_core.sh" "${THREADS}"
fi

log "phase=place_and_baseline"
for row in "${EXPERIMENTS[@]}"; do
  IFS='|' read -r case_id core_util flow_variant round_id row_start_kind notes <<< "${row}"
  snapshot="$(snapshot_path "${case_id}" "${flow_variant}")"
  printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
    "${case_id}" "${core_util:-default}" "${flow_variant}" "${round_id}" "${snapshot}" "${row_start_kind}" "${notes}" \
    >> "${EXPERIMENTS_TSV}"

  if [[ "${SKIP_PLACE}" -eq 1 ]]; then
    log "skip_place case=${case_id} flow_variant=${flow_variant}"
  elif [[ -f "${snapshot}" && "${FORCE_PLACE}" -ne 1 ]]; then
    log "reuse_snapshot case=${case_id} flow_variant=${flow_variant} snapshot=${snapshot}"
  elif [[ -n "${core_util}" ]]; then
    run_cmd_env CORE_UTILIZATION "${core_util}" \
      "${AGENT_ROOT}/scripts/evaluator/run_place_batch.sh" \
      --case "${case_id}" \
      --flow-variant "${flow_variant}" \
      --max-tasks 1 \
      --num-cores "${THREADS}" \
      --target place
  else
    run_cmd "${AGENT_ROOT}/scripts/evaluator/run_place_batch.sh" \
      --case "${case_id}" \
      --flow-variant "${flow_variant}" \
      --max-tasks 1 \
      --num-cores "${THREADS}" \
      --target place
  fi

  run_cmd "${DPL_EVOLVE_AGENT_ROOT}/baseline/run_baseline_suite.sh" \
    --case "${case_id}" \
    --flow-variant "${flow_variant}" \
    --threads "${THREADS}" \
    --tag-prefix "${round_id}_baseline_probe"
done

if [[ "${BASELINE_ONLY}" -eq 1 ]]; then
  log "baseline_only_done experiments=${EXPERIMENTS_TSV}"
  exit 0
fi

log "phase=teacher_student_loops"
for row in "${EXPERIMENTS[@]}"; do
  IFS='|' read -r case_id core_util flow_variant round_id row_start_kind notes <<< "${row}"
  log "launch_round case=${case_id} core_utilization=${core_util:-default} round_id=${round_id} start_kind=${row_start_kind} notes=${notes}"
  run_cmd "${DPL_EVOLVE_PYTHON}" "${AGENT_ROOT}/scripts/optimize_case_with_codex.py" \
    --case "${case_id}" \
    --flow-variant "${flow_variant}" \
    --round-id "${round_id}" \
    --start-kind "${row_start_kind}" \
    --children "${CHILDREN}" \
    --iterations "${ITERATIONS}" \
    --max-parallel "${MAX_PARALLEL}" \
    --threads "${THREADS}" \
    --teacher-model "${TEACHER_MODEL}" \
    --teacher-reasoning-effort "${TEACHER_REASONING_EFFORT}" \
    --student-model "${STUDENT_MODEL}" \
    --student-reasoning-effort "${STUDENT_REASONING_EFFORT}" \
    --reuse-baseline-preflight \
    --launch
done

log "done experiments=${EXPERIMENTS_TSV}"
