#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOOTSTRAP_AGENT_ROOT="${DPL_EVOLVE_AGENT_ROOT:-$(cd "${SCRIPT_DIR}/../.." && pwd)}"
BOOTSTRAP_AGENT_ROOT="$(realpath -m "${BOOTSTRAP_AGENT_ROOT}")"
source "${BOOTSTRAP_AGENT_ROOT}/scripts/runtime_env.sh"
dpl_init_runtime "scripts/calibration/calibrate_start_seeds.sh"

CASE_ID=""
FLOW_VARIANT=""
ROUND_ID=""
OUTPUT_ROOT=""
PLAN_FILE=""
THREADS="10"
MAX_PARALLEL="1"
START_KINDS="framework,diamond,default_negotiation"
SKIP_BASELINE=0
SKIP_PLACE=1
DRY_RUN=0

usage() {
  cat <<'EOF'
Usage: calibrate_start_seeds.sh --case ID --flow-variant NAME --round-id ID [options]

Build and evaluate prepared start-kind seed sources on one target case before
a Teacher/Student round.  This is a target-local start-kind probe, not the
paper-level Level 1 calibration pass.  The output is a compact initial
donor-evidence packet that lets Teacher compare framework, diamond, and
default-negotiation starts before assigning routes.

Options:
  --case ID              Case id under problems/.
  --flow-variant NAME    Existing FLOW_VARIANT containing 3_4_place_resized.odb.
  --round-id ID          Teacher round id. Used for output naming.
  --plan PATH            Optional candidate-matrix plan. If omitted, a one-row
                         plan for --case/--flow-variant is generated.
  --start-kinds LIST     Comma-separated start kinds. Default:
                         framework,diamond,default_negotiation
  --threads N            OpenROAD build/eval threads. Default: 10.
  --max-parallel N       Parallel rows inside each candidate matrix. Default: 1.
  --skip-baseline        Reuse existing canonical baseline rows.
  --no-skip-place        Allow placement snapshot generation if missing.
  --dry-run              Print planned commands only.
  --help                 Show this message.

Output:
  $DPL_EVOLVE_STATE_ROOT/<round-id>/start_seed_calibration/
    plan.tsv
    manifest.tsv
    start_seed_calibration.md
    start_seed_calibration.tsv
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --case) CASE_ID="$2"; shift 2 ;;
    --flow-variant) FLOW_VARIANT="$2"; shift 2 ;;
    --round-id) ROUND_ID="$2"; shift 2 ;;
    --output-root) OUTPUT_ROOT="$2"; shift 2 ;;
    --plan) PLAN_FILE="$2"; shift 2 ;;
    --start-kinds) START_KINDS="$2"; shift 2 ;;
    --threads) THREADS="$2"; shift 2 ;;
    --max-parallel) MAX_PARALLEL="$2"; shift 2 ;;
    --skip-baseline) SKIP_BASELINE=1; shift ;;
    --no-skip-place) SKIP_PLACE=0; shift ;;
    --dry-run) DRY_RUN=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) echo "[ERROR] Unknown argument: $1" >&2; usage >&2; exit 1 ;;
  esac
done

if [[ -z "${CASE_ID}" || -z "${FLOW_VARIANT}" || -z "${ROUND_ID}" ]]; then
  echo "[ERROR] --case, --flow-variant, and --round-id are required." >&2
  usage >&2
  exit 1
fi

OUTPUT_ROOT="${OUTPUT_ROOT:-${DPL_EVOLVE_STATE_ROOT}/${ROUND_ID}}"
OUTPUT_ROOT="$(realpath -m "${OUTPUT_ROOT}")"
CAL_ROOT="${OUTPUT_ROOT}/start_seed_calibration"
MATRIX_OUTPUT_ROOT="${OUTPUT_ROOT}/candidate_matrices"
MANIFEST="${CAL_ROOT}/manifest.tsv"
SUMMARY_MD="${CAL_ROOT}/start_seed_calibration.md"
SUMMARY_TSV="${CAL_ROOT}/start_seed_calibration.tsv"
mkdir -p "${CAL_ROOT}"

if [[ -z "${PLAN_FILE}" ]]; then
  PLAN_FILE="${CAL_ROOT}/plan.tsv"
  cat > "${PLAN_FILE}" <<EOF
enabled	case	core_utilization	flow_variant	round_id	start_kind	notes
1	${CASE_ID}	default	${FLOW_VARIANT}	${ROUND_ID}_start_seed_calibration	-	auto-generated target start-kind probe
EOF
else
  PLAN_FILE="$(realpath -m "${PLAN_FILE}")"
fi

printf "start_kind\tmatrix_id\tseed_src\tresults_tsv\tstatus\texit_code\n" > "${MANIFEST}"

shell_join() {
  local out=()
  local arg
  for arg in "$@"; do
    out+=("$(printf '%q' "${arg}")")
  done
  printf '%s' "${out[*]}"
}

run_cmd() {
  echo "[target_start_kind_probe] + $(shell_join "$@")"
  if [[ "${DRY_RUN}" -eq 1 ]]; then
    return 0
  fi
  "$@"
}

seed_for_kind() {
  case "$1" in
    framework) printf '%s\n' "${DPL_EVOLVE_STATE_ROOT}/seed_sources/framework_dpl_evolve" ;;
    diamond) printf '%s\n' "${DPL_EVOLVE_STATE_ROOT}/seed_sources/diamond_dpl_evolve" ;;
    default_negotiation) printf '%s\n' "${DPL_EVOLVE_STATE_ROOT}/seed_sources/default_negotiation_dpl_evolve" ;;
    *)
      echo "[ERROR] Unsupported start kind for calibration: $1" >&2
      return 1
    ;;
  esac
}

BASELINE_ONLY_MATRIX_ID="${ROUND_ID}_start_seed_baselines"
if [[ "${SKIP_BASELINE}" -ne 1 ]]; then
  baseline_cmd=(
    "${DPL_EVOLVE_AGENT_ROOT}/scripts/matrix/run_candidate_matrix.sh"
    --matrix-id "${BASELINE_ONLY_MATRIX_ID}"
    --output-root "${MATRIX_OUTPUT_ROOT}"
    --plan "${PLAN_FILE}"
    --threads "${THREADS}"
    --max-parallel "${MAX_PARALLEL}"
    --baseline-only
  )
  if [[ "${SKIP_PLACE}" -eq 1 ]]; then
    baseline_cmd+=(--skip-place)
  fi
  if [[ "${DRY_RUN}" -eq 1 ]]; then
    baseline_cmd+=(--dry-run)
  fi
  run_cmd "${baseline_cmd[@]}"
fi

IFS=',' read -r -a START_KIND_ARRAY <<< "${START_KINDS}"
fail_count=0
for start_kind in "${START_KIND_ARRAY[@]}"; do
  start_kind="$(printf '%s' "${start_kind}" | xargs)"
  [[ -n "${start_kind}" ]] || continue
  seed_src="$(seed_for_kind "${start_kind}")"
  if [[ ! -f "${seed_src}/CMakeLists.txt" ]]; then
    echo "[ERROR] Missing seed source for ${start_kind}: ${seed_src}" >&2
    echo "[ERROR] Run scripts/workspace/prepare_workspace.sh first." >&2
    exit 1
  fi
  matrix_id="${ROUND_ID}_start_seed_${start_kind}"
  results_tsv="${MATRIX_OUTPUT_ROOT}/${matrix_id}/results.tsv"
  cmd=(
    "${DPL_EVOLVE_AGENT_ROOT}/scripts/matrix/run_candidate_matrix.sh"
    --matrix-id "${matrix_id}"
    --output-root "${MATRIX_OUTPUT_ROOT}"
    --candidate-src "${seed_src}"
    --candidate-label "${start_kind}"
    --plan "${PLAN_FILE}"
    --threads "${THREADS}"
    --max-parallel "${MAX_PARALLEL}"
  )
  if [[ "${SKIP_PLACE}" -eq 1 ]]; then
    cmd+=(--skip-place)
  fi
  cmd+=(--skip-baseline)
  if [[ "${DRY_RUN}" -eq 1 ]]; then
    cmd+=(--dry-run)
  fi
  status="PASS"
  exit_code="0"
  if run_cmd "${cmd[@]}"; then
    :
  else
    status="FAIL"
    exit_code="$?"
    fail_count=$((fail_count + 1))
  fi
  printf "%s\t%s\t%s\t%s\t%s\t%s\n" \
    "${start_kind}" "${matrix_id}" "${seed_src}" "${results_tsv}" "${status}" "${exit_code}" \
    >> "${MANIFEST}"
done

if [[ "${DRY_RUN}" -ne 1 ]]; then
  run_cmd "${DPL_EVOLVE_PYTHON}" "${SCRIPT_DIR}/summarize_start_seed_calibration.py" \
    --manifest "${MANIFEST}" \
    --output-md "${SUMMARY_MD}" \
    --output-tsv "${SUMMARY_TSV}" \
    --round-id "${ROUND_ID}"
fi

echo "[target_start_kind_probe] manifest=${MANIFEST}"
echo "[target_start_kind_probe] summary_md=${SUMMARY_MD}"
echo "[target_start_kind_probe] summary_tsv=${SUMMARY_TSV}"
if [[ "${fail_count}" -gt 0 ]]; then
  echo "[ERROR] ${fail_count} start-kind calibration matrix failed; see ${MANIFEST}" >&2
  exit 1
fi
