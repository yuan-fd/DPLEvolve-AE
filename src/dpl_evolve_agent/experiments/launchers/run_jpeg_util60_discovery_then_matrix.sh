#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AGENT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${AGENT_ROOT}/scripts/runtime_env.sh"
dpl_init_runtime "run_jpeg_util60_discovery_then_matrix.sh"

ROUND_ID="jpeg_util60_discovery_4x12"
PLAN_FILE="${DPL_EVOLVE_AGENT_ROOT}/configs/experiment_plans/full_cross_case_core_util.tsv"
MAIN_CASE="jpeg_nangate45"
MAIN_FLOW_VARIANT="full_matrix_jpeg_util60_place"
THREADS="10"
CHILDREN="4"
ITERATIONS="12"
MAX_PARALLEL="4"
BASELINE_PARALLEL="9"
MATRIX_ROW_PARALLEL="6"
START_KIND="framework"
TEACHER_MODEL="gpt-5.5"
TEACHER_REASONING_EFFORT="xhigh"
STUDENT_MODEL="gpt-5.4"
STUDENT_REASONING_EFFORT="high"
SKIP_CORE_BUILD=0
SKIP_BASELINE_MATRIX=0
BASELINE_ONLY=0
DISCOVERY_ONLY=0
MATRIX_ONLY=0
DRY_RUN=0

usage() {
  cat <<'EOF'
Usage: run_jpeg_util60_discovery_then_matrix.sh [options]

One-click knowledge pipeline:

  1. Run the full cross-case baseline matrix.
  2. Evolve only jpeg_nangate45 at CORE_UTILIZATION=60 with 4 students x 12 iterations.
  3. Commit every generated student source revision and test it on every plan row.

With a complete 4x12 round and the default 9-row plan, the final validation
stage yields 48 x 9 candidate-case results.

Options:
  --round-id NAME              Stable Teacher round id. Default: jpeg_util60_discovery_4x12.
  --plan PATH                  Matrix plan. Default: full_cross_case_core_util.tsv.
  --main-flow-variant NAME     JPEG util60 placement snapshot. Default: full_matrix_jpeg_util60_place.
  --threads N                  OpenROAD threads. Default: 10.
  --children N                 Student agents. Default: 4.
  --iterations N               Iterations. Default: 12.
  --max-parallel N             Parallel students inside one round. Default: 4.
  --baseline-parallel N        Parallel baseline matrix rows. Default: 9.
  --matrix-row-parallel N      Parallel rows per fixed-candidate matrix. Default: 6.
  --start-kind KIND            framework, diamond, default_negotiation,
                               evolved_diamond, evolved_negotiation,
                               or prepared.
                               Default: framework.
  --teacher-model NAME         Default: gpt-5.5.
  --teacher-reasoning-effort E Default: xhigh.
  --student-model NAME         Default: gpt-5.4.
  --student-reasoning-effort E Default: high.
  --skip-core-build            Do not run configure/build_openroad_core.sh.
  --skip-baseline-matrix       Reuse existing snapshots and baselines.
  --baseline-only              Stop after full baseline matrix.
  --discovery-only             Stop after JPEG util60 Teacher/Student evolution.
  --matrix-only                Only enumerate and run candidate matrices for an existing round.
  --dry-run                    Print commands without running them.
  --help                       Show this message.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --round-id) ROUND_ID="$2"; shift 2 ;;
    --plan) PLAN_FILE="$2"; shift 2 ;;
    --main-flow-variant) MAIN_FLOW_VARIANT="$2"; shift 2 ;;
    --threads) THREADS="$2"; shift 2 ;;
    --children) CHILDREN="$2"; shift 2 ;;
    --iterations) ITERATIONS="$2"; shift 2 ;;
    --max-parallel) MAX_PARALLEL="$2"; shift 2 ;;
    --baseline-parallel) BASELINE_PARALLEL="$2"; shift 2 ;;
    --matrix-row-parallel) MATRIX_ROW_PARALLEL="$2"; shift 2 ;;
    --start-kind) START_KIND="$2"; shift 2 ;;
    --teacher-model) TEACHER_MODEL="$2"; shift 2 ;;
    --teacher-reasoning-effort) TEACHER_REASONING_EFFORT="$2"; shift 2 ;;
    --student-model) STUDENT_MODEL="$2"; shift 2 ;;
    --student-reasoning-effort) STUDENT_REASONING_EFFORT="$2"; shift 2 ;;
    --skip-core-build) SKIP_CORE_BUILD=1; shift ;;
    --skip-baseline-matrix) SKIP_BASELINE_MATRIX=1; shift ;;
    --baseline-only) BASELINE_ONLY=1; shift ;;
    --discovery-only) DISCOVERY_ONLY=1; shift ;;
    --matrix-only) MATRIX_ONLY=1; shift ;;
    --dry-run) DRY_RUN=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) echo "[ERROR] Unknown argument: $1" >&2; usage >&2; exit 1 ;;
  esac
done

PLAN_FILE="$(realpath -m "${PLAN_FILE}")"
PIPELINE_ROOT="${DPL_EVOLVE_STATE_ROOT}/experiment_batches/${ROUND_ID}_pipeline"
PIPELINE_LOG="${PIPELINE_ROOT}/run.log"
mkdir -p "${PIPELINE_ROOT}"
: > "${PIPELINE_LOG}"

log() {
  local msg="$*"
  printf '[jpeg_util60_pipeline] %s\n' "${msg}" | tee -a "${PIPELINE_LOG}"
}

run_cmd() {
  log "+ $(printf '%q ' "$@")"
  if [[ "${DRY_RUN}" -eq 1 ]]; then
    return 0
  fi
  "$@" 2>&1 | tee -a "${PIPELINE_LOG}"
}

count_enabled_plan_rows() {
  local count=0
  local enabled case_id core_util flow_variant round_id row_start_kind notes
  while IFS=$'\t' read -r enabled case_id core_util flow_variant round_id row_start_kind notes || [[ -n "${enabled:-}" ]]; do
    [[ -z "${enabled:-}" || "${enabled}" == \#* ]] && continue
    [[ "${enabled}" == "enabled" ]] && continue
    [[ "${enabled}" == "0" || "${enabled}" == "false" || "${enabled}" == "skip" ]] && continue
    count=$((count + 1))
  done < "${PLAN_FILE}"
  printf '%s\n' "${count}"
}

PLAN_ROW_COUNT="$(count_enabled_plan_rows)"

log "round_id=${ROUND_ID}"
log "plan=${PLAN_FILE}"
log "main_case=${MAIN_CASE}"
log "main_flow_variant=${MAIN_FLOW_VARIANT}"
log "plan_rows=${PLAN_ROW_COUNT}"
log "expected_complete_candidate_results=$((CHILDREN * ITERATIONS * PLAN_ROW_COUNT))"
log "baseline_parallel=${BASELINE_PARALLEL}"
log "matrix_row_parallel=${MATRIX_ROW_PARALLEL}"

if [[ "${MATRIX_ONLY}" -ne 1 && "${SKIP_CORE_BUILD}" -ne 1 ]]; then
  run_cmd "${AGENT_ROOT}/scripts/workspace/configure_openroad_core.sh"
  run_cmd "${AGENT_ROOT}/scripts/workspace/build_openroad_core.sh" "${THREADS}"
fi

if [[ "${MATRIX_ONLY}" -ne 1 && "${SKIP_BASELINE_MATRIX}" -ne 1 ]]; then
  run_cmd "${AGENT_ROOT}/scripts/matrix/run_candidate_matrix.sh" \
    --matrix-id "${ROUND_ID}_baselines" \
    --plan "${PLAN_FILE}" \
    --threads "${THREADS}" \
    --max-parallel "${BASELINE_PARALLEL}" \
    --baseline-only
fi

if [[ "${BASELINE_ONLY}" -eq 1 ]]; then
  log "baseline_only_done"
  exit 0
fi

if [[ "${MATRIX_ONLY}" -ne 1 ]]; then
  run_cmd "${DPL_EVOLVE_PYTHON}" "${AGENT_ROOT}/scripts/optimize_case_with_codex.py" \
    --case "${MAIN_CASE}" \
    --flow-variant "${MAIN_FLOW_VARIANT}" \
    --round-id "${ROUND_ID}" \
    --start-kind "${START_KIND}" \
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
fi

if [[ "${DISCOVERY_ONLY}" -eq 1 ]]; then
  log "discovery_only_done"
  exit 0
fi

matrix_args=(
  "${AGENT_ROOT}/scripts/matrix/run_round_candidate_matrices.sh"
  --round-id "${ROUND_ID}"
  --plan "${PLAN_FILE}"
  --matrix-prefix "${ROUND_ID}_all_candidates"
  --threads "${THREADS}"
  --matrix-row-parallel "${MATRIX_ROW_PARALLEL}"
  --skip-baseline-matrix
)
if [[ "${DRY_RUN}" -eq 1 ]]; then
  matrix_args+=(--dry-run)
fi
run_cmd "${matrix_args[@]}"

log "done pipeline_root=${PIPELINE_ROOT}"
