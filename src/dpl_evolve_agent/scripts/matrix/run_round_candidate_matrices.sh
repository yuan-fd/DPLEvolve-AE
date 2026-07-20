#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOOTSTRAP_AGENT_ROOT="${DPL_EVOLVE_AGENT_ROOT:-$(cd "${SCRIPT_DIR}/../.." && pwd)}"
BOOTSTRAP_AGENT_ROOT="$(realpath -m "${BOOTSTRAP_AGENT_ROOT}")"
source "${BOOTSTRAP_AGENT_ROOT}/scripts/runtime_env.sh"
dpl_init_runtime "run_round_candidate_matrices.sh"

ROUND_ID=""
ROUND_DIR=""
PLAN_FILE="${DPL_EVOLVE_AGENT_ROOT}/configs/experiment_plans/full_cross_case_core_util.tsv"
MATRIX_PREFIX=""
THREADS="10"
MATRIX_ROW_PARALLEL="6"
SKIP_BASELINE_MATRIX=0
FORCE_PLACE=0
DRY_RUN=0
LIMIT=""
STUDENT_FILTER=""
ITERATION_FILTER=""

usage() {
  cat <<'EOF'
Usage: run_round_candidate_matrices.sh --round-id NAME [options]

Enumerate all committed student source revisions from one Teacher/Student round
and run a fixed-source candidate matrix for each.  This is the "4 students x
12 iterations -> test every generated program on every case" path.

Options:
  --round-id NAME          Teacher round id under .dpl_evolve_state/<round-id>/teacher_rounds/.
  --round-dir PATH         Explicit round directory override.
  --plan PATH              TSV matrix plan. Default: full_cross_case_core_util.tsv.
  --matrix-prefix NAME     Output prefix under <round-id>/candidate_matrices/.
                            Default: <round-id>_matrix.
  --threads N              OpenROAD threads. Default: 10.
  --matrix-row-parallel N  Parallel case/utilization rows inside each fixed
                           candidate matrix. Default: 6.
  --skip-baseline-matrix   Assume snapshots/baselines already exist.
  --force-place            Regenerate placement snapshots during baseline matrix.
  --student ID             Only run one student, e.g. student_03.
  --iteration ITER         Only run one iteration, e.g. iter_07.
  --limit N                Run at most N candidates.
  --dry-run                Print commands without running them.
  --help                   Show this message.

Output:
  .dpl_evolve_state/<round-id>/candidate_matrix_batches/<matrix-prefix>/candidates.tsv

Each candidate matrix builds exactly one candidate source once, then evaluates
that same binary on every enabled plan row.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --round-id) ROUND_ID="$2"; shift 2 ;;
    --round-dir) ROUND_DIR="$2"; shift 2 ;;
    --plan) PLAN_FILE="$2"; shift 2 ;;
    --matrix-prefix) MATRIX_PREFIX="$2"; shift 2 ;;
    --threads) THREADS="$2"; shift 2 ;;
    --matrix-row-parallel) MATRIX_ROW_PARALLEL="$2"; shift 2 ;;
    --skip-baseline-matrix) SKIP_BASELINE_MATRIX=1; shift ;;
    --force-place) FORCE_PLACE=1; shift ;;
    --student) STUDENT_FILTER="$2"; shift 2 ;;
    --iteration) ITERATION_FILTER="$2"; shift 2 ;;
    --limit) LIMIT="$2"; shift 2 ;;
    --dry-run) DRY_RUN=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) echo "[ERROR] Unknown argument: $1" >&2; usage >&2; exit 1 ;;
  esac
done

if [[ -z "${ROUND_DIR}" ]]; then
  if [[ -z "${ROUND_ID}" ]]; then
    echo "[ERROR] Missing --round-id or --round-dir" >&2
    usage >&2
    exit 1
  fi
  ROUND_DIR="${DPL_EVOLVE_STATE_ROOT}/${ROUND_ID}/teacher_rounds"
else
  ROUND_DIR="$(realpath -m "${ROUND_DIR}")"
  if [[ -z "${ROUND_ID}" ]]; then
    if [[ "$(basename "${ROUND_DIR}")" == "teacher_rounds" ]]; then
      ROUND_ID="$(basename "$(dirname "${ROUND_DIR}")")"
    else
      ROUND_ID="$(basename "${ROUND_DIR}")"
    fi
  fi
fi

if [[ ! -d "${ROUND_DIR}/students" ]]; then
  echo "[ERROR] Missing round students directory: ${ROUND_DIR}/students" >&2
  exit 1
fi

PLAN_FILE="$(realpath -m "${PLAN_FILE}")"
if [[ ! -f "${PLAN_FILE}" ]]; then
  echo "[ERROR] Missing plan file: ${PLAN_FILE}" >&2
  exit 1
fi

MATRIX_PREFIX="${MATRIX_PREFIX:-${ROUND_ID}_matrix}"
ROUND_ROOT="${DPL_EVOLVE_STATE_ROOT}/${ROUND_ID}"
BATCH_ROOT="${ROUND_ROOT}/candidate_matrix_batches/${MATRIX_PREFIX}"
MATRIX_OUTPUT_ROOT="${ROUND_ROOT}/candidate_matrices"
BATCH_LOG="${BATCH_ROOT}/run.log"
CANDIDATES_TSV="${BATCH_ROOT}/candidates.tsv"
mkdir -p "${BATCH_ROOT}"
: > "${BATCH_LOG}"
printf "student\titeration\tcandidate_src\tmatrix_id\tresults_tsv\tstatus\texit_code\n" > "${CANDIDATES_TSV}"

log() {
  local msg="$*"
  printf '[round_candidate_matrices] %s\n' "${msg}" | tee -a "${BATCH_LOG}"
}

run_cmd() {
  log "+ $(printf '%q ' "$@")"
  if [[ "${DRY_RUN}" -eq 1 ]]; then
    return 0
  fi
  "$@" 2>&1 | tee -a "${BATCH_LOG}"
}

log "round_id=${ROUND_ID}"
log "round_dir=${ROUND_DIR}"
log "plan=${PLAN_FILE}"
log "matrix_prefix=${MATRIX_PREFIX}"
log "matrix_output_root=${MATRIX_OUTPUT_ROOT}"
log "matrix_row_parallel=${MATRIX_ROW_PARALLEL}"
MATERIALIZED_ROOT="${BATCH_ROOT}/materialized_sources"

enabled_plan_rows() {
  awk -F '\t' '
    $0 !~ /^#/ && NF >= 4 && $1 != "" && $1 != "enabled" \
      && $1 != "0" && $1 != "false" && $1 != "skip" { count++ }
    END { print count + 0 }
  ' "${PLAN_FILE}"
}

result_rows() {
  local path="$1"
  if [[ ! -f "${path}" ]]; then
    printf '0\n'
    return
  fi
  awk 'NR > 1 { count++ } END { print count + 0 }' "${path}"
}

result_has_metric_columns() {
  local path="$1"
  [[ -f "${path}" ]] || return 1
  head -n 1 "${path}" | rg -q $'status\texit_code\thpwl_after_micron\tavg_displacement_micron\tmax_displacement_micron\truntime_seconds'
}

if [[ "${SKIP_BASELINE_MATRIX}" -ne 1 ]]; then
  baseline_args=(
    "${DPL_EVOLVE_AGENT_ROOT}/scripts/matrix/run_candidate_matrix.sh"
    --matrix-id "${MATRIX_PREFIX}_baselines"
    --output-root "${MATRIX_OUTPUT_ROOT}"
    --plan "${PLAN_FILE}"
    --threads "${THREADS}"
    --baseline-only
  )
  if [[ "${FORCE_PLACE}" -eq 1 ]]; then
    baseline_args+=(--force-place)
  fi
  if [[ "${DRY_RUN}" -eq 1 ]]; then
    baseline_args+=(--dry-run)
  fi
  run_cmd "${baseline_args[@]}"
fi

count=0
passed=0
failed=0
skipped_complete=0
skipped_failed=0
expected_rows="$(enabled_plan_rows)"
export_args=(
  "${DPL_EVOLVE_PYTHON}" "${DPL_EVOLVE_AGENT_ROOT}/scripts/analysis/export_round_candidate_sources.py"
  --round-dir "${ROUND_DIR}"
  --output-root "${MATERIALIZED_ROOT}"
)
if [[ -n "${STUDENT_FILTER}" ]]; then
  export_args+=(--student "${STUDENT_FILTER}")
fi
if [[ -n "${ITERATION_FILTER}" ]]; then
  export_args+=(--iteration "${ITERATION_FILTER}")
fi
while IFS=$'\t' read -r student iter_part source_repo source_ref candidate_src; do
  [[ "${student}" == "student" ]] && continue

  if [[ -n "${STUDENT_FILTER}" && "${student}" != "${STUDENT_FILTER}" ]]; then
    continue
  fi
  if [[ -n "${ITERATION_FILTER}" && "${iter_part}" != "${ITERATION_FILTER}" ]]; then
    continue
  fi

  if [[ -n "${LIMIT}" && "${count}" -ge "${LIMIT}" ]]; then
    break
  fi
  count=$((count + 1))

  label="${student}_${iter_part}"
  matrix_id="${MATRIX_PREFIX}_${label}"
  results_tsv="${MATRIX_OUTPUT_ROOT}/${matrix_id}/results.tsv"
  matrix_log="${MATRIX_OUTPUT_ROOT}/${matrix_id}/run.log"

  if [[ -f "${results_tsv}" ]]; then
    rows_done="$(result_rows "${results_tsv}")"
    if [[ "${rows_done}" -ge "${expected_rows}" && "${expected_rows}" -gt 0 ]]; then
      if result_has_metric_columns "${results_tsv}"; then
        run_cmd "${DPL_EVOLVE_PYTHON}" "${DPL_EVOLVE_AGENT_ROOT}/scripts/evaluator/normalize_candidate_matrix_results.py" "${results_tsv}"
        skipped_complete=$((skipped_complete + 1))
        log "skip_complete matrix_id=${matrix_id} rows=${rows_done}/${expected_rows}"
        printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
          "${student}" "${iter_part}" "${candidate_src}" "${matrix_id}" "${results_tsv}" \
          "SKIP_COMPLETE" "0" >> "${CANDIDATES_TSV}"
        continue
      fi
      log "normalize_legacy_schema matrix_id=${matrix_id} rows=${rows_done}/${expected_rows}"
      run_cmd "${DPL_EVOLVE_PYTHON}" "${DPL_EVOLVE_AGENT_ROOT}/scripts/evaluator/normalize_candidate_matrix_results.py" "${results_tsv}"
      skipped_complete=$((skipped_complete + 1))
      printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
        "${student}" "${iter_part}" "${candidate_src}" "${matrix_id}" "${results_tsv}" \
        "SKIP_COMPLETE_NORMALIZED" "0" >> "${CANDIDATES_TSV}"
      continue
    else
      log "rerun_incomplete matrix_id=${matrix_id} rows=${rows_done}/${expected_rows}"
    fi
  fi

  args=(
    "${DPL_EVOLVE_AGENT_ROOT}/scripts/matrix/run_candidate_matrix.sh"
    --matrix-id "${matrix_id}"
    --output-root "${MATRIX_OUTPUT_ROOT}"
    --candidate-src "${candidate_src}"
    --candidate-label "${label}"
    --plan "${PLAN_FILE}"
    --threads "${THREADS}"
    --max-parallel "${MATRIX_ROW_PARALLEL}"
    --skip-place
    --skip-baseline
  )
  if [[ "${DRY_RUN}" -eq 1 ]]; then
    args+=(--dry-run)
  fi
  if run_cmd "${args[@]}"; then
    passed=$((passed + 1))
    printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
      "${student}" "${iter_part}" "${candidate_src}" "${matrix_id}" "${results_tsv}" \
      "PASS" "0" >> "${CANDIDATES_TSV}"
  else
    exit_code="$?"
    failed=$((failed + 1))
    log "candidate_failed matrix_id=${matrix_id} exit_code=${exit_code}; continuing"
    printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
      "${student}" "${iter_part}" "${candidate_src}" "${matrix_id}" "${results_tsv}" \
      "FAIL" "${exit_code}" >> "${CANDIDATES_TSV}"
  fi
done < <(
  "${export_args[@]}"
)

log "done candidates=${count} passed=${passed} failed=${failed} skipped_complete=${skipped_complete} skipped_failed=${skipped_failed} manifest=${CANDIDATES_TSV}"
