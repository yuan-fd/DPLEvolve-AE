#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AGENT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${AGENT_ROOT}/scripts/runtime_env.sh"
dpl_init_runtime "run_best_evolved_9case_transfer_matrix.sh"

RUN_STAMP="$(date +%Y%m%d_%H%M%S)"
MATRIX_PREFIX="${TRANSFER_MATRIX_PREFIX:-best_evolved_9case_transfer_${RUN_STAMP}}"
SOURCE_TABLE="${TRANSFER_SOURCE_TABLE:-${AGENT_ROOT}/../article/ARTICLE/data/article_tables/dse_bo_evolve_9case_sources.tsv}"
DEFAULT_TABLE="${TRANSFER_DEFAULT_TABLE:-${AGENT_ROOT}/../article/ARTICLE/data/article_tables/dse_bo_evolve_9case_comparison.tsv}"
PLAN_FILE="${TRANSFER_PLAN_FILE:-${AGENT_ROOT}/configs/experiment_plans/best_evolved_transfer_9case.tsv}"
OUTPUT_ROOT="${TRANSFER_OUTPUT_ROOT:-}"
ARTICLE_OUTPUT_DIR="${TRANSFER_ARTICLE_OUTPUT_DIR:-${AGENT_ROOT}/../article/ARTICLE/data/article_tables}"
THREADS="${TRANSFER_THREADS:-8}"
ROW_PARALLEL="${TRANSFER_ROW_PARALLEL:-3}"
PROGRAM_PARALLEL="${TRANSFER_PROGRAM_PARALLEL:-1}"
SKIP_PLACE="${TRANSFER_SKIP_PLACE:-1}"
SKIP_BASELINE="${TRANSFER_SKIP_BASELINE:-1}"
LIMIT="${TRANSFER_LIMIT:-}"
DRY_RUN=0

usage() {
  cat <<'EOF'
Usage: run_best_evolved_9case_transfer_matrix.sh [options]

Run the paper cross-case transfer experiment:
  1. Read the article 9-case main-table source selection.
  2. Materialize each best evolved source commit as a frozen program.
  3. Evaluate every frozen program unchanged on the same 9-case place-batch plan.
  4. Summarize average transfer HPWL/runtime/score.

Options:
  --matrix-prefix NAME       Output id. Default includes timestamp.
  --source-table PATH        Table containing OpenROAD Evolve source rows.
  --default-table PATH       Main comparison table containing OpenROAD default rows.
  --plan PATH                9-case transfer plan.
  --output-root PATH         Experiment output root.
  --article-output-dir PATH  Where summary TSV/Markdown files are copied.
  --threads N               OpenROAD threads per candidate run. Default: 8.
  --row-parallel N           Parallel target cases per frozen program. Default: 3.
  --program-parallel N       Parallel frozen programs. Default: 1.
  --limit N                  Run at most N frozen programs.
  --no-skip-place            Allow missing placement snapshots to be regenerated.
  --no-skip-baseline         Refresh baseline suites before candidate runs.
  --dry-run                  Print commands only where supported.
  --help                     Show this message.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --matrix-prefix) MATRIX_PREFIX="$2"; shift 2 ;;
    --source-table) SOURCE_TABLE="$2"; shift 2 ;;
    --default-table) DEFAULT_TABLE="$2"; shift 2 ;;
    --plan) PLAN_FILE="$2"; shift 2 ;;
    --output-root) OUTPUT_ROOT="$2"; shift 2 ;;
    --article-output-dir) ARTICLE_OUTPUT_DIR="$2"; shift 2 ;;
    --threads) THREADS="$2"; shift 2 ;;
    --row-parallel) ROW_PARALLEL="$2"; shift 2 ;;
    --program-parallel) PROGRAM_PARALLEL="$2"; shift 2 ;;
    --limit) LIMIT="$2"; shift 2 ;;
    --no-skip-place) SKIP_PLACE=0; shift ;;
    --no-skip-baseline) SKIP_BASELINE=0; shift ;;
    --dry-run) DRY_RUN=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) echo "[ERROR] Unknown argument: $1" >&2; usage >&2; exit 1 ;;
  esac
done

for value_name in THREADS ROW_PARALLEL PROGRAM_PARALLEL; do
  value="${!value_name}"
  if ! [[ "${value}" =~ ^[0-9]+$ ]] || [[ "${value}" -lt 1 ]]; then
    echo "[ERROR] ${value_name} must be a positive integer, got '${value}'" >&2
    exit 2
  fi
done
if [[ -n "${LIMIT}" ]] && ! [[ "${LIMIT}" =~ ^[0-9]+$ ]]; then
  echo "[ERROR] --limit must be an integer, got '${LIMIT}'" >&2
  exit 2
fi

SOURCE_TABLE="$(realpath -m "${SOURCE_TABLE}")"
DEFAULT_TABLE="$(realpath -m "${DEFAULT_TABLE}")"
PLAN_FILE="$(realpath -m "${PLAN_FILE}")"
if [[ -z "${OUTPUT_ROOT}" ]]; then
  OUTPUT_ROOT="${DPL_EVOLVE_STATE_ROOT}/best_evolved_transfer/${MATRIX_PREFIX}"
fi
OUTPUT_ROOT="$(realpath -m "${OUTPUT_ROOT}")"
ARTICLE_OUTPUT_DIR="$(realpath -m "${ARTICLE_OUTPUT_DIR}")"
MATERIALIZED_ROOT="${OUTPUT_ROOT}/materialized_programs"
MATRIX_ROOT="${OUTPUT_ROOT}/matrices"
LOG_DIR="${OUTPUT_ROOT}/logs"
MANIFEST="${OUTPUT_ROOT}/program_manifest.tsv"
STATUS_FILE="${OUTPUT_ROOT}/status.tsv"
SUMMARY_DIR="${OUTPUT_ROOT}/summary"
mkdir -p "${MATERIALIZED_ROOT}" "${MATRIX_ROOT}" "${LOG_DIR}" "${SUMMARY_DIR}"

if [[ ! -f "${SOURCE_TABLE}" ]]; then
  echo "[ERROR] Missing source table: ${SOURCE_TABLE}" >&2
  exit 1
fi
if [[ ! -f "${DEFAULT_TABLE}" ]]; then
  echo "[ERROR] Missing default table: ${DEFAULT_TABLE}" >&2
  exit 1
fi
if [[ ! -f "${PLAN_FILE}" ]]; then
  echo "[ERROR] Missing transfer plan: ${PLAN_FILE}" >&2
  exit 1
fi

echo "Best evolved 9-case transfer matrix"
echo "  matrix_prefix : ${MATRIX_PREFIX}"
echo "  source_table  : ${SOURCE_TABLE}"
echo "  default_table : ${DEFAULT_TABLE}"
echo "  plan          : ${PLAN_FILE}"
echo "  output_root   : ${OUTPUT_ROOT}"
echo "  row_parallel  : ${ROW_PARALLEL}"
echo "  program_parallel: ${PROGRAM_PARALLEL}"
echo

"${DPL_EVOLVE_PYTHON}" "${AGENT_ROOT}/experiments/analysis/materialize_best_evolved_sources.py" \
  --source-table "${SOURCE_TABLE}" \
  --output-root "${MATERIALIZED_ROOT}" \
  --manifest "${MANIFEST}"

printf "program\tdiscovery_case\tstatus\tstart\tend\tlog\tresults_tsv\n" > "${STATUS_FILE}"

run_program() {
  local program="$1"
  local discovery_case="$2"
  local src="$3"
  local log="${LOG_DIR}/${program}.log"
  local start_ts end_ts rc results
  start_ts="$(date '+%F %T')"
  results="${MATRIX_ROOT}/${program}/results.tsv"
  args=(
    "${AGENT_ROOT}/scripts/matrix/run_candidate_matrix.sh"
    --matrix-id "${program}"
    --output-root "${MATRIX_ROOT}"
    --candidate-src "${src}"
    --candidate-label "${program}"
    --plan "${PLAN_FILE}"
    --threads "${THREADS}"
    --max-parallel "${ROW_PARALLEL}"
  )
  if [[ "${SKIP_PLACE}" == "1" ]]; then
    args+=(--skip-place)
  fi
  if [[ "${SKIP_BASELINE}" == "1" ]]; then
    args+=(--skip-baseline)
  fi
  if [[ "${DRY_RUN}" == "1" ]]; then
    args+=(--dry-run)
  fi

  set +e
  {
    printf '[START] %s\n' "${start_ts}"
    printf '[INFO] program=%s discovery_case=%s src=%s\n' "${program}" "${discovery_case}" "${src}"
    printf '[INFO] command:'
    printf ' %q' "${args[@]}"
    printf '\n'
    "${args[@]}"
  } > "${log}" 2>&1
  rc=$?
  set -e
  end_ts="$(date '+%F %T')"
  if [[ "${rc}" -eq 0 ]]; then
    printf "%s\t%s\tPASS\t%s\t%s\t%s\t%s\n" "${program}" "${discovery_case}" "${start_ts}" "${end_ts}" "${log}" "${results}" >> "${STATUS_FILE}"
  else
    printf "%s\t%s\tFAIL(%s)\t%s\t%s\t%s\t%s\n" "${program}" "${discovery_case}" "${rc}" "${start_ts}" "${end_ts}" "${log}" "${results}" >> "${STATUS_FILE}"
  fi
  return "${rc}"
}

declare -a pids=()
overall_rc=0
count=0
while IFS=$'\t' read -r program discovery_case source_repo source_commit materialized_src metrics_summary round_id; do
  [[ "${program}" == "program" ]] && continue
  if [[ -n "${LIMIT}" && "${count}" -ge "${LIMIT}" ]]; then
    break
  fi
  count=$((count + 1))
  while (( $(jobs -pr | wc -l) >= PROGRAM_PARALLEL )); do
    if ! wait -n; then
      overall_rc=1
    fi
  done
  echo "[INFO] launch program=${program} discovery_case=${discovery_case}"
  run_program "${program}" "${discovery_case}" "${materialized_src}" &
  pids+=("$!")
done < "${MANIFEST}"

for pid in "${pids[@]}"; do
  if ! wait "${pid}"; then
    overall_rc=1
  fi
done

"${DPL_EVOLVE_PYTHON}" "${AGENT_ROOT}/experiments/analysis/summarize_best_evolved_transfer.py" \
  --manifest "${MANIFEST}" \
  --matrix-root "${MATRIX_ROOT}" \
  --default-table "${DEFAULT_TABLE}" \
  --output-dir "${SUMMARY_DIR}"

mkdir -p "${ARTICLE_OUTPUT_DIR}"
cp "${SUMMARY_DIR}/evolved_program_cross_case_matrix.tsv" "${ARTICLE_OUTPUT_DIR}/"
cp "${SUMMARY_DIR}/evolved_program_transfer_summary.tsv" "${ARTICLE_OUTPUT_DIR}/"
cp "${SUMMARY_DIR}/evolved_program_transfer_summary.md" "${ARTICLE_OUTPUT_DIR}/"
if [[ -f "${MANIFEST%.tsv}.skipped.tsv" ]]; then
  cp "${MANIFEST%.tsv}.skipped.tsv" "${ARTICLE_OUTPUT_DIR}/evolved_program_transfer_skipped.tsv"
fi

echo
echo "Transfer matrix finished. Status:"
cat "${STATUS_FILE}"
echo
echo "Summary:"
cat "${SUMMARY_DIR}/evolved_program_transfer_summary.md"
exit "${overall_rc}"
